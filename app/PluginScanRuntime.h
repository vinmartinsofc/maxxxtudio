#pragma once

#include "PluginWorkerProtocol.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#if defined(_WIN32)
class PluginScanRuntime final : private juce::Thread
{
public:
    PluginScanRuntime (const juce::String& sessionId, DWORD parentProcessId);
    ~PluginScanRuntime() override;
    bool isReady() const noexcept { return ready; }

private:
    void run() override;
    juce::File fileFor (const juce::String& suffix) const;

    juce::String sessionId;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::FileSearchPath searchPaths;
    HANDLE parentProcess = nullptr;
    bool ready = false;
};
#endif
