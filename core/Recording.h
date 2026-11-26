#pragma once

#include "Note.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace roll
{
struct RecordedMidiEvent
{
    int pitch = 60;
    int channel = 1;
    int velocity = 100;
    double beat = 0.0;
    bool noteOn = false;
};

inline std::vector<Note> makeRecordedNotes (const std::vector<RecordedMidiEvent>& events,
                                            double startBeat, double endBeat)
{
    if (! std::isfinite (startBeat) || ! std::isfinite (endBeat))
        return {};

    struct ActiveNote { double beat; int velocity; };
    std::array<std::deque<ActiveNote>, 16 * 128> active;
    std::vector<Note> notes;
    const auto indexFor = [] (int channel, int pitch)
    {
        return std::clamp (channel, 1, 16) * 128 + std::clamp (pitch, 0, 127) - 128;
    };
    const double end = std::max (startBeat, endBeat);

    for (const auto& event : events)
    {
        if (event.pitch < 0 || event.pitch > 127 || event.channel < 1 || event.channel > 16
            || ! std::isfinite (event.beat))
            continue;

        auto& pending = active[static_cast<std::size_t> (indexFor (event.channel, event.pitch))];
        if (event.noteOn && event.velocity > 0)
        {
            pending.push_back ({ std::max (startBeat, event.beat),
                                 std::clamp (event.velocity, 1, 127) });
            continue;
        }

        if (! pending.empty())
        {
            const auto note = pending.front();
            pending.pop_front();
            const double noteEnd = std::max (note.beat + 0.01, std::min (end, event.beat));
            notes.push_back ({ event.pitch, note.beat - startBeat,
                               noteEnd - note.beat, note.velocity });
        }
    }

    for (std::size_t key = 0; key < active.size(); ++key)
    {
        const int pitch = static_cast<int> (key % 128);
        auto& pending = active[key];
        while (! pending.empty())
        {
            const auto note = pending.front();
            pending.pop_front();
            const double noteEnd = std::max (note.beat + 0.01, end);
            notes.push_back ({ pitch, note.beat - startBeat,
                               noteEnd - note.beat, note.velocity });
        }
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        return a.startBeats == b.startBeats ? a.pitch < b.pitch : a.startBeats < b.startBeats;
    });
    return notes;
}
} // namespace roll
