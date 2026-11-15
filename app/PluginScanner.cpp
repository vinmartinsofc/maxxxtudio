#include "PluginScanner.h"
#include "TaskRegistry.h"
#include <algorithm>
#include <cmath>

struct PluginScanner::ScanState
{
    std::atomic<bool> cancelled { false };
};

juce::File PluginScanner::dataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ProjectFreedom");
    dir.createDirectory();
    return dir;
}

PluginScanner::PluginScanner (juce::AudioPluginFormatManager& fm, TaskRegistry& tasks)
    : formatManager (fm), taskRegistry (tasks)
{
    if (auto xml = juce::XmlDocument::parse (dataDirectory().getChildFile ("plugins.xml")))
        knownList.recreateFromXml (*xml);
}

PluginScanner::~PluginScanner()
{
    stopScan();
    alive->store (false, std::memory_order_release);
}

std::vector<juce::File> PluginScanner::searchRoots() const
{
    std::vector<juce::File> roots;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        const auto locations = formatManager.getFormat (i)->getDefaultLocationsToSearch();
        for (int path = 0; path < locations.getNumPaths(); ++path)
            roots.push_back (locations[path]);
    }
    return roots;
}

void PluginScanner::startScan()
{
    if (activeScan != nullptr)
        return;

    activeScan = std::make_shared<ScanState>();
    const std::weak_ptr<ScanState> weakScan = activeScan;
    // O escopo compartilhado permanece vivo ate o callback de conclusao, inclusive em cancelamentos.
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Escaneando plugins", "global", 0.0, true,
        [weakScan]
        {
            if (const auto scan = weakScan.lock())
                scan->cancelled.store (true, std::memory_order_release);
        });

    if (onStarted)
        onStarted();

    const auto scan = activeScan;
    const auto flag = alive;
    scanPool.addJob ([this, scan, task, flag]
    {
        runScan (scan, task, flag);
    });
}

void PluginScanner::cancelScan()
{
    // O pedido e observado entre arquivos; o JUCE nao oferece interrupcao segura dentro do plugin atual.
    if (activeScan != nullptr)
        activeScan->cancelled.store (true, std::memory_order_release);
}

void PluginScanner::stopScan()
{
    if (activeScan == nullptr)
        return;

    cancelScan();
    // Aguarda o worker antes que o scanner ou o TaskRegistry sejam destruidos.
    scanPool.removeAllJobs (true, -1);
    activeScan.reset();
}

void PluginScanner::saveList()
{
    if (auto xml = knownList.createXml())
        xml->writeTo (dataDirectory().getChildFile ("plugins.xml"));
}

void PluginScanner::runScan (const std::shared_ptr<ScanState>& scan,
                             const std::shared_ptr<TaskScope>& task,
                             const std::shared_ptr<std::atomic<bool>>& flag)
{
    const auto shouldCancel = [&scan]
    {
        const auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
        return scan->cancelled.load (std::memory_order_acquire)
            || (job != nullptr && job->shouldExit());
    };

    const auto deadMans = dataDirectory().getChildFile ("scan-crash-guard.txt");
    const int formatCount = formatManager.getNumFormats();
    bool cancelled = false;

    for (int i = 0; i < formatCount && ! shouldCancel(); ++i)
    {
        auto* format = formatManager.getFormat (i);
        juce::PluginDirectoryScanner directoryScanner (
            knownList, *format, format->getDefaultLocationsToSearch(), true, deadMans, false);

        juce::String name;
        bool hasMoreFiles = true;
        while (hasMoreFiles && ! shouldCancel())
        {
            name.clear();
            hasMoreFiles = directoryScanner.scanNextFile (true, name);

            // O JUCE mede os candidatos ja examinados neste formato; o app registra VST3.
            const auto formatProgress = static_cast<double> (directoryScanner.getProgress());
            const auto safeProgress = std::isfinite (formatProgress)
                ? std::clamp (formatProgress, 0.0, 1.0) : 1.0;
            task->setProgress (formatCount > 0
                ? (static_cast<double> (i) + safeProgress) / formatCount : 1.0);

            if (name.isNotEmpty()
                && ! juce::MessageManager::callAsync ([this, flag, name]
                {
                    if (flag->load (std::memory_order_acquire) && onProgress)
                        onProgress (name);
                }))
            {
                juce::Logger::writeToLog ("PluginScanner: nao foi possivel publicar o progresso na message thread.");
            }
        }
    }

    cancelled = shouldCancel();
    if (! cancelled)
    {
        const auto types = knownList.getTypes();
        for (const auto& type : types)
        {
            for (int i = 0; i < formatManager.getNumFormats(); ++i)
            {
                auto* format = formatManager.getFormat (i);
                if (type.pluginFormatName == format->getName())
                {
                    if (! format->fileMightContainThisPluginType (type.fileOrIdentifier))
                        knownList.removeType (type);
                    break;
                }
            }
        }

        saveList();
        task->setProgress (1.0);
    }

    if (! juce::MessageManager::callAsync ([this, flag, scan, task, cancelled]
    {
        if (! flag->load (std::memory_order_acquire))
            return;

        task->end();
        if (activeScan == scan)
            activeScan.reset();
        if (onFinished)
            onFinished (cancelled);
    }))
    {
        juce::Logger::writeToLog ("PluginScanner: nao foi possivel publicar o fim do escaneamento.");
    }
}
