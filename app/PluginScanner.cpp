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
    loadSearchPaths();
}

PluginScanner::~PluginScanner()
{
    stopScan();
    alive->store (false, std::memory_order_release);
}

std::vector<juce::File> PluginScanner::searchRoots (const juce::String& formatName) const
{
    std::vector<juce::File> roots;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);
        if (format->getName() != formatName)
            continue;

        const auto locations = searchPathsForFormat (*format);
        for (int path = 0; path < locations.getNumPaths(); ++path)
            roots.push_back (locations[path]);
    }
    return roots;
}

std::vector<juce::File> PluginScanner::customSearchPaths (const juce::String& formatName) const
{
    const juce::ScopedLock lock (customPathsLock);
    std::vector<juce::File> result;
    const auto it = customPathsByFormat.find (formatName.toStdString());
    if (it == customPathsByFormat.end())
        return result;

    for (const auto& path : it->second)
        result.emplace_back (juce::String (path));
    return result;
}

bool PluginScanner::supportsPluginFormat (const juce::String& formatName) const
{
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat (i)->getName() == formatName)
            return true;

    return false;
}

juce::Result PluginScanner::addCustomSearchPath (const juce::String& formatName, const juce::File& directory)
{
    if (! supportsPluginFormat (formatName) || ! directory.isDirectory())
        return juce::Result::fail ("Formato indisponivel ou pasta invalida.");

    const auto fullPath = directory.getFullPathName();
    const auto format = formatName.toStdString();
    {
        const juce::ScopedLock lock (customPathsLock);
        auto& paths = customPathsByFormat[format];
        if (std::any_of (paths.begin(), paths.end(), [&fullPath] (const auto& path)
            { return juce::String (path).equalsIgnoreCase (fullPath); }))
            return juce::Result::fail ("Esta pasta ja esta cadastrada.");
        paths.push_back (fullPath.toStdString());
    }
    if (saveSearchPaths())
        return juce::Result::ok();

    {
        const juce::ScopedLock lock (customPathsLock);
        auto& paths = customPathsByFormat[format];
        paths.pop_back();
        if (paths.empty())
            customPathsByFormat.erase (format);
    }
    return juce::Result::fail ("Nao foi possivel salvar as pastas de plugins.");
}

juce::Result PluginScanner::removeCustomSearchPath (const juce::String& formatName, const juce::File& directory)
{
    const auto fullPath = directory.getFullPathName();
    const auto format = formatName.toStdString();
    std::size_t removedIndex = 0;
    {
        const juce::ScopedLock lock (customPathsLock);
        auto it = customPathsByFormat.find (format);
        if (it == customPathsByFormat.end())
            return juce::Result::fail ("A pasta customizada nao foi encontrada.");

        auto& paths = it->second;
        const auto path = std::find_if (paths.begin(), paths.end(), [&fullPath] (const auto& item)
            { return juce::String (item).equalsIgnoreCase (fullPath); });
        if (path == paths.end())
            return juce::Result::fail ("A pasta customizada nao foi encontrada.");

        removedIndex = static_cast<std::size_t> (std::distance (paths.begin(), path));
        paths.erase (path);
        if (paths.empty())
            customPathsByFormat.erase (it);
    }
    if (saveSearchPaths())
        return juce::Result::ok();

    {
        const juce::ScopedLock lock (customPathsLock);
        auto& paths = customPathsByFormat[format];
        paths.insert (paths.begin() + static_cast<std::ptrdiff_t> (
                          std::min (removedIndex, paths.size())),
                      fullPath.toStdString());
    }
    return juce::Result::fail ("Nao foi possivel salvar as pastas de plugins.");
}

void PluginScanner::loadSearchPaths()
{
    const auto file = dataDirectory().getChildFile ("plugin-search-paths.xml");
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("PLUGIN_SEARCH_PATHS"))
        return;

    for (auto* format = xml->getFirstChildElement();
         format != nullptr;
         format = format->getNextElement())
    {
        if (! format->hasTagName ("FORMAT"))
            continue;

        const auto formatName = format->getStringAttribute ("name");
        if (formatName.isEmpty() || ! supportsPluginFormat (formatName))
            continue;

        auto& paths = customPathsByFormat[formatName.toStdString()];
        for (auto* path = format->getFirstChildElement();
             path != nullptr;
             path = path->getNextElement())
        {
            if (! path->hasTagName ("PATH"))
                continue;

            const auto directory = path->getStringAttribute ("directory");
            if (directory.isNotEmpty())
                paths.push_back (directory.toStdString());
        }
    }
}

bool PluginScanner::saveSearchPaths() const
{
    juce::XmlElement xml ("PLUGIN_SEARCH_PATHS");
    const juce::ScopedLock lock (customPathsLock);
    for (const auto& [formatName, paths] : customPathsByFormat)
    {
        if (paths.empty())
            continue;

        auto* format = xml.createNewChildElement ("FORMAT");
        format->setAttribute ("name", juce::String (formatName));
        for (const auto& path : paths)
        {
            auto* directory = format->createNewChildElement ("PATH");
            directory->setAttribute ("directory", juce::String (path));
        }
    }
    return xml.writeTo (dataDirectory().getChildFile ("plugin-search-paths.xml"));
}

juce::FileSearchPath PluginScanner::searchPathsForFormat (juce::AudioPluginFormat& format) const
{
    auto locations = format.getDefaultLocationsToSearch();
    for (const auto& path : customSearchPaths (format.getName()))
        locations.addIfNotAlreadyThere (path);
    locations.removeRedundantPaths();
    return locations;
}

void PluginScanner::startScan()
{
    if (activeScan != nullptr)
    {
        rescanPending = true;
        cancelScan();
        return;
    }

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

    rescanPending = false;
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
            knownList, *format, searchPathsForFormat (*format), true, deadMans, false);

        juce::String name;
        bool hasMoreFiles = true;
        while (hasMoreFiles && ! shouldCancel())
        {
            name.clear();
            hasMoreFiles = directoryScanner.scanNextFile (true, name);

            // O JUCE mede progresso por formato; o app combina os formatos habilitados.
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
            bool formatFound = false;
            for (int i = 0; i < formatManager.getNumFormats(); ++i)
            {
                auto* format = formatManager.getFormat (i);
                if (type.pluginFormatName == format->getName())
                {
                    formatFound = true;
                    const juce::File pluginFile (type.fileOrIdentifier);
                    const auto searchPaths = searchPathsForFormat (*format);
                    bool isInSearchPath = false;
                    for (int path = 0; path < searchPaths.getNumPaths(); ++path)
                    {
                        const auto directory = searchPaths[path];
                        if (pluginFile == directory || pluginFile.isAChildOf (directory))
                        {
                            isInSearchPath = true;
                            break;
                        }
                    }

                    if (! isInSearchPath || ! format->fileMightContainThisPluginType (type.fileOrIdentifier))
                        knownList.removeType (type);
                    break;
                }
            }
            if (! formatFound)
                knownList.removeType (type);
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
        if (rescanPending)
        {
            rescanPending = false;
            startScan();
        }
    }))
    {
        juce::Logger::writeToLog ("PluginScanner: nao foi possivel publicar o fim do escaneamento.");
    }
}
