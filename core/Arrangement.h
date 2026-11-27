#pragma once
#include "Clip.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace roll
{
enum class TrackType { Midi, Audio, Group };
enum class TrackOutputRouting { Master, ExtOut };
enum class AutomationParameterType {
    Volume, Pan, Mute, PluginParameter,
    AudioClipGain, AudioClipPitch, AudioClipFadeIn, AudioClipFadeOut
};

struct AutomationTarget
{
    AutomationParameterType type = AutomationParameterType::Volume;
    std::string pluginIdentifier, parameterId, parameterName;
    int parameterIndex = -1, deviceIndex = -1;
    double currentValue = 0.0;
    int clipId = -1;

    bool operator== (const AutomationTarget& other) const
    {
        if (type != other.type)
            return false;
        if (type != AutomationParameterType::PluginParameter)
            if (type == AutomationParameterType::AudioClipGain
                || type == AutomationParameterType::AudioClipPitch
                || type == AutomationParameterType::AudioClipFadeIn
                || type == AutomationParameterType::AudioClipFadeOut)
                return clipId < 0 || other.clipId < 0 || clipId == other.clipId;
            else
                return true;
        return pluginIdentifier == other.pluginIdentifier
            && (deviceIndex < 0 || other.deviceIndex < 0 || deviceIndex == other.deviceIndex)
            && (parameterId.empty() ? parameterIndex == other.parameterIndex
                                    : parameterId == other.parameterId);
    }
};

struct AutomationPoint
{
    double beat = 0.0, value = 0.0;
    friend bool operator== (const AutomationPoint&, const AutomationPoint&) = default;
};

struct AutomationEnvelope
{
    AutomationTarget target;
    std::vector<AutomationPoint> points;
};

inline double automationValueAt (const AutomationEnvelope& envelope, double beat, double fallback)
{
    if (envelope.points.empty())
        return fallback;
    if (beat <= envelope.points.front().beat)
        return envelope.points.front().value;
    if (beat >= envelope.points.back().beat)
        return envelope.points.back().value;

    const auto next = std::upper_bound (envelope.points.begin(), envelope.points.end(), beat,
        [] (double position, const AutomationPoint& point) { return position < point.beat; });
    const auto previous = next - 1;
    const double length = next->beat - previous->beat;
    const double proportion = length > 0.0 ? (beat - previous->beat) / length : 0.0;
    return previous->value + (next->value - previous->value) * proportion;
}

inline std::pair<double, double> automationValueRange (AutomationParameterType type)
{
    switch (type)
    {
        case AutomationParameterType::Volume: return { 0.0, 1.5 };
        case AutomationParameterType::Pan: return { -1.0, 1.0 };
        case AutomationParameterType::Mute: return { 0.0, 1.0 };
        case AutomationParameterType::PluginParameter: return { 0.0, 1.0 };
        case AutomationParameterType::AudioClipGain: return { -24.0, 24.0 };
        case AutomationParameterType::AudioClipPitch: return { -25.0, 25.0 };
        case AutomationParameterType::AudioClipFadeIn: return { 0.0, 64.0 };
        case AutomationParameterType::AudioClipFadeOut: return { 0.0, 64.0 };
    }
    return { 0.0, 1.0 };
}

inline bool isValidAutomationEnvelope (const AutomationEnvelope& envelope)
{
    constexpr std::size_t maxPoints = 65536;
    if (envelope.points.empty() || envelope.points.size() > maxPoints)
        return false;
    if (envelope.target.type == AutomationParameterType::PluginParameter
        && (envelope.target.pluginIdentifier.empty()
            || envelope.target.deviceIndex < -1
            || (envelope.target.parameterId.empty() && envelope.target.parameterIndex < 0)))
        return false;
    if ((envelope.target.type == AutomationParameterType::AudioClipGain
         || envelope.target.type == AutomationParameterType::AudioClipPitch
         || envelope.target.type == AutomationParameterType::AudioClipFadeIn
         || envelope.target.type == AutomationParameterType::AudioClipFadeOut)
        && envelope.target.clipId < 1)
        return false;

    const auto [minimum, maximum] = automationValueRange (envelope.target.type);
    if (! std::isfinite (envelope.target.currentValue)
        || envelope.target.currentValue < minimum || envelope.target.currentValue > maximum)
        return false;
    double previousBeat = -1.0;
    for (const auto& point : envelope.points)
    {
        if (! std::isfinite (point.beat) || point.beat < 0.0 || point.beat <= previousBeat
            || ! std::isfinite (point.value) || point.value < minimum || point.value > maximum)
            return false;
        previousBeat = point.beat;
    }
    return true;
}

// A clip placed on a track's timeline (position in beats).
struct Placement
{
    int         id = 0;
    double      start = 0.0;
    bool        enabled = true;
    std::uint32_t colour = 0;
    std::string name;
    Clip        clip { 4.0 };

    double end() const { return start + clip.length(); }
};

struct WaveformRange
{
    float minimum = 0.0f;
    float maximum = 0.0f;

    friend bool operator== (const WaveformRange&, const WaveformRange&) = default;
};

struct AudioWarpMarker
{
    double sourceSeconds = 0.0;
    double beat = 0.0;

    friend bool operator== (const AudioWarpMarker&, const AudioWarpMarker&) = default;
};

inline double audioFadeCurveValue (double progress, double curve)
{
    const double t = std::clamp (progress, 0.0, 1.0);
    const double smooth = t * t * (3.0 - 2.0 * t);
    const double amount = std::clamp (std::abs (curve), 0.0, 1.0);
    const double control = curve >= 0.0 ? 1.0 : 0.0;
    const double quadratic = 2.0 * (1.0 - t) * t * control + t * t;
    return std::clamp (smooth + amount * (quadratic - smooth), 0.0, 1.0);
}

struct AudioPlacement
{
    int         id = 0;
    double      start = 0.0;
    double      lengthBeats = 0.0;
    double      durationSeconds = 0.0;
    double      sourceOffsetSeconds = 0.0;
    double      sourceLoopStartSeconds = 0.0;
    double      sourceLoopEndSeconds = 0.0;
    double      fadeInBeats = 0.0;
    double      fadeOutBeats = 0.0;
    double      fadeInCurve = 0.0;
    double      fadeOutCurve = 0.0;
    double      sourceBpm = 120.0;
    std::uint32_t colour = 0;
    double pitchSemitones = 0.0;
    double pitchCents = 0.0;
    double gainDb = 0.0;
    int warpMode = 0; // Beats, Tones, Texture, Re-Pitch, Complex, Complex Pro
    bool        enabled = true;
    bool        warpEnabled = true;
    bool        reverse = false;
    std::string name, filePath;
    std::vector<WaveformRange> waveform;
    std::vector<AudioWarpMarker> warpMarkers;

    double end() const { return start + lengthBeats; }
};

inline double audioSourceSecondsAtBeat (const AudioPlacement& clip, double beat)
{
    if (clip.warpMarkers.size() >= 2)
    {
        const auto& markers = clip.warpMarkers;
        if (beat <= markers.front().beat)
            return markers.front().sourceSeconds;
        if (beat >= markers.back().beat)
            return markers.back().sourceSeconds;

        const auto next = std::upper_bound (markers.begin(), markers.end(), beat,
            [] (double position, const AudioWarpMarker& marker) { return position < marker.beat; });
        const auto previous = next - 1;
        const double proportion = (beat - previous->beat) / (next->beat - previous->beat);
        return previous->sourceSeconds
             + (next->sourceSeconds - previous->sourceSeconds) * proportion;
    }

    const double proportion = clip.lengthBeats > 0.0
        ? std::clamp (beat / clip.lengthBeats, 0.0, 1.0) : 0.0;
    return clip.sourceOffsetSeconds + clip.durationSeconds * proportion;
}

inline double audioSourcePositionAtBeat (const AudioPlacement& clip, double beat,
                                         double projectBpm, double sourceDurationSeconds)
{
    double position = 0.0;
    if (clip.warpMarkers.size() >= 2)
    {
        const auto& markers = clip.warpMarkers;
        if (beat < markers.front().beat)
        {
            const auto& first = markers[0];
            const auto& second = markers[1];
            position = first.sourceSeconds + (beat - first.beat)
                * (second.sourceSeconds - first.sourceSeconds) / (second.beat - first.beat);
        }
        else if (beat > markers.back().beat)
        {
            const auto& previous = markers[markers.size() - 2];
            const auto& last = markers.back();
            position = last.sourceSeconds + (beat - last.beat)
                * (last.sourceSeconds - previous.sourceSeconds) / (last.beat - previous.beat);
        }
        else
            position = audioSourceSecondsAtBeat (clip, beat);
    }
    else
    {
        const double playbackBpm = clip.warpEnabled ? clip.sourceBpm : projectBpm;
        position = clip.sourceOffsetSeconds + beat * 60.0 / std::max (playbackBpm, 20.0);
    }

    if (clip.reverse)
        position = clip.sourceOffsetSeconds + clip.durationSeconds
                 - (position - clip.sourceOffsetSeconds);

    double loopStart = std::max (0.0, clip.sourceLoopStartSeconds);
    double loopEnd = std::min (sourceDurationSeconds, clip.sourceLoopEndSeconds);
    if (loopEnd <= loopStart)
    {
        loopStart = 0.0;
        loopEnd = sourceDurationSeconds;
    }
    const double loopLength = loopEnd - loopStart;
    if (loopLength > 0.0 && (position < loopStart || position >= loopEnd))
    {
        double wrapped = std::fmod (position - loopStart, loopLength);
        if (wrapped < 0.0)
            wrapped += loopLength;
        position = loopStart + wrapped;
    }
    return std::clamp (position, 0.0, std::max (0.0, sourceDurationSeconds));
}

inline bool isValidAudioWarpMarkers (const std::vector<AudioWarpMarker>& markers,
                                     double sourceOffset, double durationSeconds,
                                     double lengthBeats)
{
    if (markers.empty())
        return true;
    if (markers.size() < 2 || markers.size() > 4096
        || ! std::isfinite (sourceOffset) || ! std::isfinite (durationSeconds)
        || ! std::isfinite (lengthBeats) || durationSeconds <= 0.0 || lengthBeats <= 0.0)
        return false;

    constexpr double epsilon = 1.0e-7;
    if (std::abs (markers.front().sourceSeconds - sourceOffset) > epsilon
        || std::abs (markers.front().beat) > epsilon
        || std::abs (markers.back().sourceSeconds - (sourceOffset + durationSeconds)) > epsilon
        || std::abs (markers.back().beat - lengthBeats) > epsilon)
        return false;

    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        const auto& marker = markers[i];
        if (! std::isfinite (marker.sourceSeconds) || ! std::isfinite (marker.beat)
            || marker.sourceSeconds < sourceOffset - epsilon
            || marker.sourceSeconds > sourceOffset + durationSeconds + epsilon
            || marker.beat < -epsilon || marker.beat > lengthBeats + epsilon)
            return false;
        if (i > 0 && (marker.sourceSeconds <= markers[i - 1].sourceSeconds
                      || marker.beat <= markers[i - 1].beat))
            return false;
    }
    return true;
}

// Track + mixer parameters. Clips are heap-allocated (stable pointers).
class Track
{
public:
    Track() = default;
    Track (const Track&);
    Track& operator= (const Track&);
    Track (Track&&) noexcept = default;
    Track& operator= (Track&&) noexcept = default;

    int         id = 0;
    std::string name;
    TrackType   type = TrackType::Midi;
    double      volume = 1.0;     // ganho linear (0..1.5)
    double      pan = 0.0;        // -1..1
    std::uint32_t colour = 0;     // zero uses the automatic track colour
    bool        mute = false;
    bool        solo = false;
    bool        collapsed = false;
    bool        frozen = false;
    bool        freezeReversible = true;
    std::unique_ptr<AudioPlacement> frozenAudioClip;
    TrackOutputRouting outputRouting = TrackOutputRouting::Master;
    int         parentGroupId = 0;

    std::vector<std::unique_ptr<Placement>> clips;
    std::vector<std::unique_ptr<AudioPlacement>> audioClips;
    std::vector<AutomationEnvelope> automation;

    Placement* addClip (double start, double length);
    Placement* find (int clipId) const;
    Placement* copyClip (const Placement& source, double start);
    int splitClip (int clipId, double beat);
    std::unique_ptr<Placement> take (int clipId);          // Removes from this track (for moving between tracks).
    void put (std::unique_ptr<Placement> p);
    bool remove (int clipId);

    // Does [start, start+length) fit without touching another clip (ignoring 'ignoreId')?
    bool isFree (double start, double length, int ignoreId = -1) const;

    AudioPlacement* addAudioClip (double start, double length, std::string filePath,
                                  std::string name, std::vector<WaveformRange> waveform = {},
                                  double durationSeconds = 0.0, double sourceBpm = 120.0);
    AudioPlacement* findAudioClip (int clipId) const;
    AudioPlacement* copyAudioClip (const AudioPlacement& source, double start);
    int splitAudioClip (int clipId, double beat);
    std::unique_ptr<AudioPlacement> takeAudioClip (int clipId);
    void putAudioClip (std::unique_ptr<AudioPlacement> clip);
    bool removeAudioClip (int clipId);
    bool isAudioFree (double start, double length, int ignoreId = -1) const;

    // Clip ID is unique within the track.
    int nextClipId() { return nextId++; }
    void reserveClipId (int clipId) { if (clipId >= nextId) nextId = clipId + 1; }

private:
    int nextId = 1;
};

// Combines a track's clips into a list of notes at absolute times (what the sequencer plays).
// Notes extending past the end of the clip are trimmed/discarded.
std::vector<Note> flatten (const Track& track);

// Linear <-> decibel conversion for faders and meters.
float gainToDecibels (float gain, float minusInfinityDb = -60.0f);
float decibelsToGain (float decibels);
double adaptiveGridBeat (double pixelsPerBeat, double targetSpacingPixels = 12.0);
} // namespace roll
