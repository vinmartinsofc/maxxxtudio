#pragma once
#include "Note.h"
#include <cstdint>
#include <vector>

namespace roll
{
struct MidiEvent
{
    int  offset   = 0;      // Samples from the start of the block.
    bool on       = true;
    int  pitch    = 60;
    int  velocity = 100;
};

struct TransportSettings
{
    double bpm       = 120.0;
    bool   loop      = true;
    double loopStart = 0.0;     // In beats.
    double loopEnd   = 16.0;
};

// A continuous time range within an audio block (the loop may split the block in two).
struct Segment
{
    double a = 0.0, b = 0.0;      // beats [a, b)
    int    offset = 0;            // Sample offset where the range starts in the block.
    int    samples = 0;
    bool   flushBefore = false;   // Release notes before the range (loop wrap).
    bool   flushAfter  = false;   // Release notes at the end of the range (loop end).
};

struct BlockPlan
{
    static constexpr int maxSegments = 8;
    Segment seg[maxSegments];
    int    count = 0;
    int    numSamples = 0;
    double beatsPerSample = 0.0;
};

// Transport clock: one per project. Determines the time range covered by each block.
class TransportClock
{
public:
    double position() const        { return pos; }
    void   setPosition (double b)  { pos = b; }
    void   plan (const TransportSettings& t, double sampleRate, int numSamples, BlockPlan& out);

private:
    double pos = 0.0;
};

// MIDI event generator: one per track. Pure logic, without JUCE or normal-path allocations.
class Sequencer
{
public:
    Sequencer() { active.reserve (1024); }

    void process (const std::vector<Note>& notes, const BlockPlan& plan, std::vector<MidiEvent>& out);
    void flush (int sampleOffset, std::vector<MidiEvent>& out);

    bool hasActiveNotes() const { return !active.empty(); }
    void activeMask (std::uint64_t& lo, std::uint64_t& hi) const;   // pitches soando (bits 0..127)

private:
    struct Active { int id; int pitch; double end; };

    void emit (const std::vector<Note>& notes, double a, double b,
               int baseOffset, double beatsPerSample, std::vector<MidiEvent>& out);

    std::vector<Active> active;
};
} // namespace roll
