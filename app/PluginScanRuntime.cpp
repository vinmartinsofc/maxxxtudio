#include "PluginScanRuntime.h"

#include <algorithm>
#include <cmath>
#if defined(__unix__)
#include <cerrno>
#include <signal.h>
#include <sys/types.h>
#endif

namespace
{
void writeProgress (const juce::File& file, double progress, const juce::String& name)
{
    file.replaceWithText (juce::String (progress, 6) + "\n" + name);
}
}

PluginScanRuntime::PluginScanRuntime (const juce::String& id, std::int64_t parentId)
    : juce::Thread ("Isolated plugin scanner"), sessionId (id), parentProcessId (parentId)
{
#if defined(_WIN32)
    parentProcess = OpenProcess (SYNCHRONIZE, FALSE, static_cast<DWORD> (parentId));
    const auto configXml = juce::XmlDocument::parse (fileFor ("input.xml"));
    if (parentProcess == nullptr || configXml == nullptr || ! configXml->hasTagName ("PLUGIN_SCAN"))
        return;
#else
    const auto configXml = juce::XmlDocument::parse (fileFor ("input.xml"));
    if (! isParentRunning() || configXml == nullptr || ! configXml->hasTagName ("PLUGIN_SCAN"))
        return;
#endif

    const auto* pluginListXml = configXml->getChildByName ("KNOWNPLUGINS");
    if (pluginListXml == nullptr)
        return;
    knownPlugins.recreateFromXml (*pluginListXml);

    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    for (auto* path = configXml->getFirstChildElement();
         path != nullptr;
         path = path->getNextElement())
        if (path->hasTagName ("PATH") && path->getStringAttribute ("format") == "VST3")
            searchPaths.add (path->getStringAttribute ("directory"));
    searchPaths.removeRedundantPaths();
    ready = true;
    startThread();
}

PluginScanRuntime::~PluginScanRuntime()
{
    signalThreadShouldExit();
    stopThread (2000);
#if defined(_WIN32)
    if (parentProcess != nullptr)
        CloseHandle (parentProcess);
#endif
}

juce::File PluginScanRuntime::fileFor (const juce::String& suffix) const
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("maxxx-tudio-scan-" + sessionId + "-" + suffix);
}

bool PluginScanRuntime::isParentRunning() const noexcept
{
#if defined(_WIN32)
    return parentProcess != nullptr && WaitForSingleObject (parentProcess, 0) == WAIT_TIMEOUT;
#elif defined(__unix__)
    if (parentProcessId <= 0)
        return false;
    return ::kill (static_cast<pid_t> (parentProcessId), 0) == 0 || errno == EPERM;
#else
    return true;
#endif
}

void PluginScanRuntime::run()
{
    const auto deadMans = fileFor ("crash-guard.txt");
    const auto progressFile = fileFor ("progress.txt");
    const int formatCount = formats.getNumFormats();
    bool cancelled = false;

    for (int formatIndex = 0; formatIndex < formatCount && ! threadShouldExit(); ++formatIndex)
    {
        if (! isParentRunning())
        {
            juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
            return;
        }

        auto* format = formats.getFormat (formatIndex);
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, searchPaths, true, deadMans, false);
        juce::String currentName;
        bool hasMoreFiles = true;
        while (hasMoreFiles && ! threadShouldExit()
               && isParentRunning())
        {
            currentName.clear();
            hasMoreFiles = scanner.scanNextFile (true, currentName);
            const double formatProgress = scanner.getProgress();
            const double totalProgress = formatCount > 0
                ? (formatIndex + std::clamp (std::isfinite (formatProgress) ? formatProgress : 1.0, 0.0, 1.0))
                    / formatCount
                : 1.0;
            writeProgress (progressFile, totalProgress, currentName);
        }
    }

    cancelled = threadShouldExit() || ! isParentRunning();
    if (cancelled)
    {
        juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
        return;
    }

    const auto types = knownPlugins.getTypes();
    for (const auto& type : types)
    {
        bool formatFound = false;
        for (int index = 0; index < formats.getNumFormats(); ++index)
        {
            auto* format = formats.getFormat (index);
            if (type.pluginFormatName != format->getName())
                continue;

            formatFound = true;
            const juce::File pluginFile (type.fileOrIdentifier);
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
                knownPlugins.removeType (type);
            break;
        }
        if (! formatFound)
            knownPlugins.removeType (type);
    }

    if (const auto result = knownPlugins.createXml())
    {
        result->writeTo (fileFor ("output.xml"));
        writeProgress (progressFile, 1.0, {});
    }

    juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
}
