#pragma once

namespace roll
{
// A MIDI note within a clip. Time is measured in beats (quarter notes),
// independente de BPM e de sample rate.
struct Note
{
    int    pitch       = 60;    // 0..127
    double startBeats  = 0.0;
    double lengthBeats = 1.0;
    int    velocity    = 100;   // 1..127
    bool   muted       = false; // Disabled note (does not play).
    int    id          = 0;     // Assigned by Clip; stable for the note's lifetime.

    double endBeats() const { return startBeats + lengthBeats; }
};
} // namespace roll
