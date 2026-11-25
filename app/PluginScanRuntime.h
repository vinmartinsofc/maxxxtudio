#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdint>
#include <memory>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

class PluginScanRuntime final : private juce::Thread
{
public:
    PluginScanRuntime (const juce::String& sessionId, std::int64_t parentProcessId);
    ~PluginScanRuntime() override;
    bool isReady() const noexcept { return ready; }

private:
    void run() override;
    bool isParentRunning() const noexcept;
    juce::File fileFor (const juce::String& suffix) const;

    juce::String sessionId;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::FileSearchPath searchPaths;
#if defined(_WIN32)
    HANDLE parentProcess = nullptr;
#endif
    std::int64_t parentProcessId = 0;
    bool ready = false;
};
