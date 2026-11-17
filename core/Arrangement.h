#pragma once
#include "Clip.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace roll
{
enum class TrackType { Midi, Audio, Group };
enum class TrackOutputRouting { Master, ExtOut };
enum class AutomationParameterType { Volume, Pan, Mute, PluginParameter };

struct AutomationTarget
{
    AutomationParameterType type = AutomationParameterType::Volume;
    std::string pluginIdentifier, parameterId, parameterName;
    int parameterIndex = -1, deviceIndex = -1;
    double currentValue = 0.0;

    bool operator== (const AutomationTarget& other) const
    {
        if (type != other.type)
            return false;
        if (type != AutomationParameterType::PluginParameter)
            return true;
        return pluginIdentifier == other.pluginIdentifier
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

// Um clip colocado na linha do tempo de uma trilha (posição em beats).
struct Placement
{
    int         id = 0;
    double      start = 0.0;
    bool        enabled = true;
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
    double      fadeInBeats = 0.0;
    double      fadeOutBeats = 0.0;
    double      fadeInCurve = 0.0;
    double      fadeOutCurve = 0.0;
    double      sourceBpm = 120.0;
    bool        enabled = true;
    bool        warpEnabled = true;
    std::string name, filePath;
    std::vector<WaveformRange> waveform;

    double end() const { return start + lengthBeats; }
};

// Trilha + parâmetros de mixagem. Clips ficam em heap (ponteiros estáveis).
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
    bool        mute = false;
    bool        solo = false;
    bool        collapsed = false;
    TrackOutputRouting outputRouting = TrackOutputRouting::Master;
    int         parentGroupId = 0;

    std::vector<std::unique_ptr<Placement>> clips;
    std::vector<std::unique_ptr<AudioPlacement>> audioClips;
    std::vector<AutomationEnvelope> automation;

    Placement* addClip (double start, double length);
    Placement* find (int clipId) const;
    Placement* copyClip (const Placement& source, double start);
    int splitClip (int clipId, double beat);
    std::unique_ptr<Placement> take (int clipId);          // tira da trilha (para mover entre trilhas)
    void put (std::unique_ptr<Placement> p);
    bool remove (int clipId);

    // O trecho [start, start+length) cabe sem encostar em outro clip (ignorando 'ignoreId')?
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

    // Id de clip único dentro da trilha.
    int nextClipId() { return nextId++; }
    void reserveClipId (int clipId) { if (clipId >= nextId) nextId = clipId + 1; }

private:
    int nextId = 1;
};

// Junta os clips da trilha numa lista de notas em tempo absoluto (o que o sequenciador toca).
// Notas além do fim do clip são cortadas/descartadas.
std::vector<Note> flatten (const Track& track);

// Conversao linear <-> decibeis para faders e medidores
float gainToDecibels (float gain, float minusInfinityDb = -60.0f);
float decibelsToGain (float decibels);
double adaptiveGridBeat (double pixelsPerBeat, double targetSpacingPixels = 12.0);
} // namespace roll
