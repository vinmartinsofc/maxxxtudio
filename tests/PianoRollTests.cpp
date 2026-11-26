#include <catch2/catch_test_macros.hpp>
#include "PianoRollEditor.h"

TEST_CASE ("Piano Roll locators move independently and remain ordered")
{
    roll::Clip clip (8.0);
    const int first = clip.addPianoRollLocator (1.0);
    const int second = clip.addPianoRollLocator (3.0);

    REQUIRE (clip.movePianoRollLocator (first, 5.0));
    REQUIRE (clip.pianoRollLocators()[0].id == second);
    REQUIRE (clip.pianoRollLocators()[1].id == first);
    REQUIRE (clip.pianoRollLocators()[1].beat == 5.0);
    REQUIRE_FALSE (clip.movePianoRollLocator (first, 9.0));
    REQUIRE_FALSE (clip.movePianoRollLocator (-1, 2.0));
}
TEST_CASE ("Zero key mutes and restores selected Piano Roll notes")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    roll::Clip clip (4.0);
    const int noteId = clip.addNote ({ 60, 0.0, 1.0, 100 });

    RollModel model;
    model.setClip (&clip);
    model.selection.insert (noteId);
    NoteGrid grid (model);

    const juce::KeyPress topRowZero ('0', juce::ModifierKeys(), '0');
    REQUIRE (grid.keyPressed (topRowZero));
    REQUIRE (clip.find (noteId)->muted);
    REQUIRE (clip.notes().size() == 1);

    const juce::KeyPress numpadZero (juce::KeyPress::numberPad0, juce::ModifierKeys(), 0);
    REQUIRE (grid.keyPressed (numpadZero));
    REQUIRE_FALSE (clip.find (noteId)->muted);
    REQUIRE (clip.notes().size() == 1);
}

TEST_CASE ("Piano keys select notes by pitch and Ctrl+D duplicates the selection")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    roll::Clip clip (4.0);
    const int first = clip.addNote ({ 60, 0.0, 0.5, 100 });
    const int second = clip.addNote ({ 60, 1.0, 0.5, 100 });
    const int otherPitch = clip.addNote ({ 62, 2.0, 0.5, 100 });

    RollModel model;
    model.setClip (&clip);
    model.selectNotesAtPitch (60, false);
    REQUIRE (model.selection.size() == 2);
    REQUIRE (model.isSelected (first));
    REQUIRE (model.isSelected (second));
    REQUIRE_FALSE (model.isSelected (otherPitch));

    model.selectNotesAtPitch (62, true);
    REQUIRE (model.selection.size() == 3);

    NoteGrid grid (model);
    const juce::KeyPress duplicate ('D', juce::ModifierKeys::ctrlModifier, 'D');
    REQUIRE (grid.keyPressed (duplicate));
    REQUIRE (clip.notes().size() == 6);
}

TEST_CASE ("Shift arrow transposes every selected MIDI note by an octave")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    roll::Clip clip (4.0);
    clip.setNotes ({
        { 60, 0.0, 0.5, 100 },
        { 64, 1.0, 0.5, 100 },
        { 67, 2.0, 0.5, 100 }
    });

    RollModel model;
    model.setClip (&clip);
    model.selectAll();
    REQUIRE (model.selection.size() == 3);

    NoteGrid grid (model);
    const juce::KeyPress octaveUp (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0);
    REQUIRE (grid.keyPressed (octaveUp));
    CHECK (clip.notes()[0].pitch == 72);
    CHECK (clip.notes()[1].pitch == 76);
    CHECK (clip.notes()[2].pitch == 79);

    const juce::KeyPress octaveDown (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0);
    REQUIRE (grid.keyPressed (octaveDown));
    CHECK (clip.notes()[0].pitch == 60);
    CHECK (clip.notes()[1].pitch == 64);
    CHECK (clip.notes()[2].pitch == 67);
}
