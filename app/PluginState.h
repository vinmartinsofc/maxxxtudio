#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

struct PluginState
{
    int trackId = 0;
    int order = 0;
    juce::String identifier;
    bool bypassed = false;
    int groupId = 0;
    juce::MemoryBlock state;
};
