#pragma once
#include "Clip.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace roll
{
enum class TrackType { Midi, Audio };
enum class TrackOutputRouting { Master, ExtOut };

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
    TrackOutputRouting outputRouting = TrackOutputRouting::Master;

    std::vector<std::unique_ptr<Placement>> clips;
    std::vector<std::unique_ptr<AudioPlacement>> audioClips;

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
} // namespace roll
