#include "PluginScanner.h"
#include "PluginWorkerProtocol.h"
#include "TaskRegistry.h"
#include <algorithm>
#include <cmath>
#include <mutex>

#ifndef MAXXXTUDIO_PLUGIN_WORKER_PATH
 #define MAXXXTUDIO_PLUGIN_WORKER_PATH ""
#endif

struct PluginScanner::ScanState
{
    std::atomic<bool> cancelled { false };
    std::mutex childMutex;
    juce::ChildProcess* child = nullptr;
};

juce::File PluginScanner::dataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MaXXXtudio");
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
        return juce::Result::fail ("Unsupported format or invalid folder.");

    const auto fullPath = directory.getFullPathName();
    const auto format = formatName.toStdString();
    {
        const juce::ScopedLock lock (customPathsLock);
        auto& paths = customPathsByFormat[format];
        if (std::any_of (paths.begin(), paths.end(), [&fullPath] (const auto& path)
            { return juce::String (path).equalsIgnoreCase (fullPath); }))
            return juce::Result::fail ("This folder is already registered.");
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
    return juce::Result::fail ("Could not save plugin folders.");
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
            return juce::Result::fail ("The custom folder was not found.");

        auto& paths = it->second;
        const auto path = std::find_if (paths.begin(), paths.end(), [&fullPath] (const auto& item)
            { return juce::String (item).equalsIgnoreCase (fullPath); });
        if (path == paths.end())
            return juce::Result::fail ("The custom folder was not found.");

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
    return juce::Result::fail ("Could not save plugin folders.");
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
    // Shared state remains alive until the completion callback, including on cancellation.
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Scanning plugins", "global", 0.0, true,
        [weakScan]
        {
            if (const auto scan = weakScan.lock())
            {
                scan->cancelled.store (true, std::memory_order_release);
                const std::lock_guard lock (scan->childMutex);
                if (scan->child != nullptr)
                    scan->child->kill();
            }
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
    if (activeScan != nullptr)
    {
        activeScan->cancelled.store (true, std::memory_order_release);
        const std::lock_guard lock (activeScan->childMutex);
        if (activeScan->child != nullptr)
            activeScan->child->kill();
    }
}

void PluginScanner::stopScan()
{
    if (activeScan == nullptr)
        return;

    rescanPending = false;
    cancelScan();
    // Wait for the worker before destroying the scanner or TaskRegistry.
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

    const auto sessionId = juce::Uuid().toString().removeCharacters ("{}");
    const auto prefix = "maxxx-tudio-scan-" + sessionId + "-";
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto inputFile = temp.getChildFile (prefix + "input.xml");
    const auto outputFile = temp.getChildFile (prefix + "output.xml");
    const auto progressFile = temp.getChildFile (prefix + "progress.txt");
    auto config = std::make_unique<juce::XmlElement> ("PLUGIN_SCAN");
    if (auto pluginList = knownList.createXml())
        config->addChildElement (pluginList.release());
    for (int index = 0; index < formatManager.getNumFormats(); ++index)
    {
        auto* format = formatManager.getFormat (index);
        const auto paths = searchPathsForFormat (*format);
        for (int path = 0; path < paths.getNumPaths(); ++path)
        {
            auto* pathNode = config->createNewChildElement ("PATH");
            pathNode->setAttribute ("format", format->getName());
            pathNode->setAttribute ("directory", paths[path].getFullPathName());
        }
    }

    bool success = false;
    std::shared_ptr<juce::XmlElement> resultXml;
    juce::ChildProcess child;
    auto workerExecutable = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                .getSiblingFile ("MaXXXtudio Plugin Worker.exe");
    if (! workerExecutable.existsAsFile())
        workerExecutable = juce::File (MAXXXTUDIO_PLUGIN_WORKER_PATH);
    if (workerExecutable.existsAsFile() && config->writeTo (inputFile))
    {
        juce::StringArray arguments;
        arguments.add (workerExecutable.getFullPathName());
        arguments.add ("--plugin-scan-worker");
        arguments.add (sessionId);
        arguments.add (juce::String (GetCurrentProcessId()));
        if (child.start (arguments, 0))
        {
            {
                const std::lock_guard lock (scan->childMutex);
                scan->child = &child;
            }

            juce::String lastPluginName;
            while (! child.waitForProcessToFinish (100))
            {
                if (shouldCancel())
                {
                    child.kill();
                    break;
                }

                juce::StringArray progressLines;
                progressLines.addLines (progressFile.loadFileAsString());
                if (! progressLines.isEmpty())
                {
                    const auto progress = progressLines[0].getDoubleValue();
                    if (std::isfinite (progress))
                        task->setProgress (std::clamp (progress, 0.0, 1.0));
                    if (progressLines.size() > 1 && progressLines[1].isNotEmpty()
                        && progressLines[1] != lastPluginName)
                    {
                        lastPluginName = progressLines[1];
                        if (! juce::MessageManager::callAsync ([this, flag, lastPluginName]
                        {
                            if (flag->load (std::memory_order_acquire) && onProgress)
                                onProgress (lastPluginName);
                        }))
                            juce::Logger::writeToLog (
                                "PluginScanner: Could not publish progress to the message thread.");
                    }
                }
            }

            {
                const std::lock_guard lock (scan->childMutex);
                scan->child = nullptr;
            }
            if (! shouldCancel() && child.getExitCode() == 0)
                if (auto xml = juce::XmlDocument::parse (outputFile);
                    xml != nullptr && xml->hasTagName ("KNOWNPLUGINS"))
                {
                    resultXml = std::shared_ptr<juce::XmlElement> (xml.release());
                    success = true;
                }
        }
        else
        {
            juce::Logger::writeToLog ("PluginScanner: Could not start the isolated scanner.");
        }
    }
    else
    {
        juce::Logger::writeToLog ("PluginScanner: Scanner configuration file or executable is unavailable.");
    }

    inputFile.deleteFile();
    outputFile.deleteFile();
    progressFile.deleteFile();
    const bool cancelled = shouldCancel();
    if (! success && ! cancelled)
        juce::Logger::writeToLog ("PluginScanner: The isolated process did not complete the scan.");

    if (! juce::MessageManager::callAsync ([this, flag, scan, task, cancelled, success, resultXml]
    {
        if (! flag->load (std::memory_order_acquire))
            return;

        if (success && resultXml != nullptr)
        {
            knownList.recreateFromXml (*resultXml);
            saveList();
            task->setProgress (1.0);
        }
        else if (! cancelled)
        {
            juce::Logger::writeToLog ("PluginScanner: The scan process finished without valid results.");
        }
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
        juce::Logger::writeToLog ("PluginScanner: Could not publish scan completion.");
    }
}
