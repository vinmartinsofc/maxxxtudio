#include <catch2/catch_test_macros.hpp>
#include "PianoRollEditor.h"

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
