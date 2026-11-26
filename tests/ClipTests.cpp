#include <catch2/catch_test_macros.hpp>
#include "Clip.h"
#include "History.h"
#include "Recording.h"

using namespace roll;

TEST_CASE("snapToGrid rounds to the grid")
{
    REQUIRE(snapToGrid(0.12, 0.25) == 0.0);
    REQUIRE(snapToGrid(0.13, 0.25) == 0.25);
    REQUIRE(snapToGrid(1.9, 0.5)   == 2.0);
}

TEST_CASE("Clip adds, finds, and removes notes by ID")
{
    Clip clip(8.0);
    int a = clip.addNote({ 60, 1.0, 2.0, 100 });
    int b = clip.addNote({ 64, 0.0, 1.0, 100 });
    REQUIRE(a != b);
    REQUIRE(clip.noteAt(60, 1.5) == a);
    REQUIRE(clip.noteAt(60, 3.0) == -1);   // Exclusive end.
    REQUIRE(clip.noteAt(61, 1.5) == -1);
    REQUIRE(clip.removeNote(a));
    REQUIRE_FALSE(clip.removeNote(a));
    REQUIRE(clip.find(b) != nullptr);
    REQUIRE(clip.notes().size() == 1);
}

TEST_CASE("setNotes preserves IDs and does not reuse them")
{
    Clip clip(8.0);
    int a = clip.addNote({ 60, 0.0, 1.0, 100 });
    auto saved = clip.notes();
    clip.removeNote(a);
    clip.setNotes(saved);
    int c = clip.addNote({ 62, 0.0, 1.0, 100 });
    REQUIRE(c != a);
}

TEST_CASE("History supports undo and redo")
{
    Clip clip(8.0);
    History h;
    h.record(clip.notes());                 // estado vazio
    clip.addNote({ 60, 0.0, 1.0, 100 });
    REQUIRE(h.canUndo());

    auto cur = clip.notes();
    REQUIRE(h.undo(cur));
    REQUIRE(cur.empty());
    REQUIRE(h.canRedo());
    REQUIRE(h.redo(cur));
    REQUIRE(cur.size() == 1);
    REQUIRE_FALSE(h.canRedo());
}

TEST_CASE("A new note deletes the note it covers at the beginning")
{
    Clip clip(8.0);
    int old = clip.addNote({ 60, 2.0, 1.0, 100 });
    int fresh = clip.addNote({ 60, 1.5, 2.0, 100 });        // Covers the beginning of the old note.
    clip.resolveOverlaps({ fresh });
    REQUIRE(clip.find(old) == nullptr);
    REQUIRE(clip.find(fresh) != nullptr);
}

TEST_CASE("A new note shortens the note it covers at the tail")
{
    Clip clip(8.0);
    int old = clip.addNote({ 60, 0.0, 3.0, 100 });
    int fresh = clip.addNote({ 60, 2.0, 1.0, 100 });
    clip.resolveOverlaps({ fresh });
    REQUIRE(clip.find(old) != nullptr);
    REQUIRE(clip.find(old)->lengthBeats == 2.0);
}

TEST_CASE("Different pitches do not interfere")
{
    Clip clip(8.0);
    int a = clip.addNote({ 60, 0.0, 3.0, 100 });
    int b = clip.addNote({ 62, 1.0, 1.0, 100 });
    clip.resolveOverlaps({ b });
    REQUIRE(clip.find(a)->lengthBeats == 3.0);
}

TEST_CASE("Recorded MIDI pairs notes and closes held notes at the capture end")
{
    const std::vector<RecordedMidiEvent> events {
        { 60, 1, 91, 4.5, true },
        { 60, 1, 0, 5.25, false },
        { 64, 2, 73, 5.0, true }
    };

    const auto notes = makeRecordedNotes (events, 4.0, 6.0);
    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0].pitch == 60);
    REQUIRE (notes[0].startBeats == 0.5);
    REQUIRE (notes[0].lengthBeats == 0.75);
    REQUIRE (notes[0].velocity == 91);
    REQUIRE (notes[1].pitch == 64);
    REQUIRE (notes[1].startBeats == 1.0);
    REQUIRE (notes[1].lengthBeats == 1.0);
    REQUIRE (notes[1].velocity == 73);
}
