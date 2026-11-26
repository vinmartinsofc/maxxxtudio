#include <catch2/catch_test_macros.hpp>
#include "MidiClipExporter.h"
#include <cmath>

TEST_CASE ("MIDI clip export preserves note data and project tempo")
{
    roll::Clip clip (4.0);
    clip.addNote ({ 64, 0.5, 1.25, 92 });
    clip.addNote ({ 67, 2.0, 0.5, 80, true });

    juce::MemoryOutputStream output;
    REQUIRE (midi_clip_export::write (clip, 120.0, output).wasOk());

    juce::MemoryInputStream input (output.getData(), output.getDataSize(), false);
    juce::MidiFile midi;
    REQUIRE (midi.readFrom (input));
    REQUIRE (midi.getTimeFormat() == 960);
    REQUIRE (midi.getNumTracks() == 1);

    const auto* events = midi.getTrack (0);
    REQUIRE (events != nullptr);
    int noteOns = 0, noteOffs = 0, tempoEvents = 0;
    for (int i = 0; i < events->getNumEvents(); ++i)
    {
        const auto& message = events->getEventPointer (i)->message;
        if (message.isNoteOn())
        {
            ++noteOns;
            CHECK (message.getNoteNumber() == 64);
            CHECK (message.getVelocity() == 92);
            CHECK (message.getTimeStamp() == 480.0);
        }
        else if (message.isNoteOff())
        {
            ++noteOffs;
            CHECK (message.getNoteNumber() == 64);
            CHECK (message.getTimeStamp() == 1680.0);
        }
        else if (message.isTempoMetaEvent())
        {
            ++tempoEvents;
            CHECK (std::abs (message.getTempoSecondsPerQuarterNote() - 0.5) < 1.0e-6);
        }
    }
    CHECK (noteOns == 1);
    CHECK (noteOffs == 1);
    CHECK (tempoEvents == 1);
}

TEST_CASE ("MIDI clip export rejects clips without audible notes")
{
    roll::Clip clip (4.0);
    clip.addNote ({ 64, 0.0, 1.0, 92, true });

    juce::MemoryOutputStream output;
    CHECK (midi_clip_export::write (clip, 120.0, output).failed());
}

TEST_CASE ("MIDI clip export rejects tempos that cannot be represented")
{
    roll::Clip clip (4.0);
    clip.addNote ({ 64, 0.0, 1.0, 92 });

    juce::MemoryOutputStream output;
    CHECK (midi_clip_export::write (clip, 1.0e-300, output).failed());
}
