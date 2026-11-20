#include "PluginScanRuntime.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>

namespace
{
void writeProgress (const juce::File& file, double progress, const juce::String& name)
{
    file.replaceWithText (juce::String (progress, 6) + "\n" + name);
}
}

PluginScanRuntime::PluginScanRuntime (const juce::String& id, DWORD parentProcessId)
    : juce::Thread ("Isolated plugin scanner"), sessionId (id)
{
    parentProcess = OpenProcess (SYNCHRONIZE, FALSE, parentProcessId);
    const auto configXml = juce::XmlDocument::parse (fileFor ("input.xml"));
    if (parentProcess == nullptr || configXml == nullptr || ! configXml->hasTagName ("PLUGIN_SCAN"))
        return;

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
    if (parentProcess != nullptr)
        CloseHandle (parentProcess);
}

juce::File PluginScanRuntime::fileFor (const juce::String& suffix) const
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("maxxx-tudio-scan-" + sessionId + "-" + suffix);
}

void PluginScanRuntime::run()
{
    const auto deadMans = fileFor ("crash-guard.txt");
    const auto progressFile = fileFor ("progress.txt");
    const int formatCount = formats.getNumFormats();
    bool cancelled = false;

    for (int formatIndex = 0; formatIndex < formatCount && ! threadShouldExit(); ++formatIndex)
    {
        if (WaitForSingleObject (parentProcess, 0) != WAIT_TIMEOUT)
        {
            juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
            return;
        }

        auto* format = formats.getFormat (formatIndex);
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, searchPaths, true, deadMans, false);
        juce::String currentName;
        bool hasMoreFiles = true;
        while (hasMoreFiles && ! threadShouldExit()
               && WaitForSingleObject (parentProcess, 0) == WAIT_TIMEOUT)
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

    cancelled = threadShouldExit() || WaitForSingleObject (parentProcess, 0) != WAIT_TIMEOUT;
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

#endif
