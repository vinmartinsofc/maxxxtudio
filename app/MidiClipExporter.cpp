#include "MidiClipExporter.h"
#include <cmath>

namespace midi_clip_export
{
juce::Result write (const roll::Clip& clip, double bpm, juce::OutputStream& output)
{
    if (! std::isfinite (bpm) || bpm <= 0.0)
        return juce::Result::fail ("The project tempo is invalid.");

    constexpr int ticksPerQuarterNote = 960;
    constexpr double maxMidiDeltaTicks = 0x0fffffff;
    juce::MidiMessageSequence sequence;
    const double tempoValue = 60'000'000.0 / bpm;
    if (! std::isfinite (tempoValue) || tempoValue < 1.0 || tempoValue > 0x00ffffff)
        return juce::Result::fail ("The project tempo cannot be represented in a MIDI file.");

    const int microsecondsPerQuarterNote = static_cast<int> (std::lround (tempoValue));
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (microsecondsPerQuarterNote), 0.0);

    bool hasNotes = false;
    for (const auto& note : clip.notes())
    {
        if (note.muted)
            continue;
        if (note.pitch < 0 || note.pitch > 127 || note.velocity < 1 || note.velocity > 127
            || ! std::isfinite (note.startBeats) || ! std::isfinite (note.lengthBeats)
            || note.startBeats < 0.0 || note.lengthBeats <= 0.0
            || ! std::isfinite (note.startBeats + note.lengthBeats))
            return juce::Result::fail ("The MIDI clip contains an invalid note.");

        const double startTicks = note.startBeats * ticksPerQuarterNote;
        const double endTicks = (note.startBeats + note.lengthBeats) * ticksPerQuarterNote;
        if (endTicks > maxMidiDeltaTicks)
            return juce::Result::fail ("The MIDI clip is too long to export.");

        sequence.addEvent (juce::MidiMessage::noteOn (1, note.pitch,
            static_cast<juce::uint8> (note.velocity)), startTicks);
        sequence.addEvent (juce::MidiMessage::noteOff (1, note.pitch), endTicks);
        hasNotes = true;
    }

    if (! hasNotes)
        return juce::Result::fail ("The MIDI clip has no unmuted notes.");

    sequence.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarterNote);
    midi.addTrack (sequence);
    return midi.writeTo (output) ? juce::Result::ok()
                                 : juce::Result::fail ("Could not write the MIDI file.");
}
}
