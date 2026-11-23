#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace pluginworker
{
inline constexpr int maxChannels = 128;
inline constexpr int maxSamples = 8192;
inline constexpr int maxMidiEvents = 512;
inline constexpr int maxMidiBytes = 256;
inline constexpr int maxParameters = 4096;
inline constexpr int parameterNameLength = 96;
inline constexpr int parameterIdLength = 64;
inline constexpr int pluginNameLength = 128;
inline constexpr int errorLength = 512;
inline constexpr int descriptionLength = 16384;
inline constexpr int statePathLength = 1024;
inline constexpr LONG protocolVersion = 7;

enum class Command : LONG
{
    none,
    process,
    prepare,
    releaseResources,
    getState,
    setState,
    showEditor,
    hideEditor,
    shutdown
};

struct MidiEvent
{
    int samplePosition = 0;
    int size = 0;
    std::uint8_t data[maxMidiBytes] {};
};

struct SharedData
{
    LONG version = 0;
    LONG state = 0;
    LONG command = static_cast<LONG> (Command::none);
    LONG numInputs = 0;
    LONG numOutputs = 0;
    LONG numParameters = 0;
    LONG hasEditor = 0;
    LONG acceptsMidi = 0;
    LONG producesMidi = 0;
    LONG midiEffect = 0;
    LONG midiCount = 0;
    LONG audioSamples = 0;
    LONG stateBytes = 0;
    LONG result = 0;
    LONG editorX = -1;
    LONG editorY = -1;
    LONG editorPositionValid = 0;
    double sampleRate = 44100.0;
    int blockSize = 512;
    LONG playHeadValid = 0;
    LONG playHeadPlaying = 0;
    LONG playHeadRecording = 0;
    LONG playHeadLooping = 0;
    int playHeadTimeSignatureNumerator = 4;
    int playHeadTimeSignatureDenominator = 4;
    std::int64_t playHeadTimeSamples = 0;
    std::int64_t playHeadBarCount = 0;
    double playHeadTimeSeconds = 0.0;
    double playHeadBpm = 120.0;
    double playHeadPpqPosition = 0.0;
    double playHeadLastBarStartPpq = 0.0;
    double playHeadLoopStartPpq = 0.0;
    double playHeadLoopEndPpq = 0.0;
    char pluginName[pluginNameLength] {};
    char error[errorLength] {};
    char pluginDescription[descriptionLength] {};
    char stateFilePath[statePathLength] {};
    char parameterNames[maxParameters][parameterNameLength] {};
    char parameterIds[maxParameters][parameterIdLength] {};
    LONG parameterValues[maxParameters] {};
    LONG parameterChanged[maxParameters] {};
    LONG parameterGestureStarted = -1;
    LONG parameterGestureEnded = -1;
    MidiEvent midi[maxMidiEvents] {};
    float audio[maxChannels][maxSamples] {};
};

inline LONG floatToBits (float value) noexcept
{
    LONG bits = 0;
    static_assert (sizeof (bits) == sizeof (value));
    std::memcpy (&bits, &value, sizeof (bits));
    return bits;
}

inline float bitsToFloat (LONG bits) noexcept
{
    float value = 0.0f;
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

inline void writeText (char* destination, int capacity, const char* text) noexcept
{
    if (capacity <= 0)
        return;

    const auto length = std::min (std::strlen (text), static_cast<std::size_t> (capacity - 1));
    std::memcpy (destination, text, length);
    destination[length] = '\0';
}
}

#endif
