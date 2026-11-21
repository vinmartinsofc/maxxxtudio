#pragma once
#include <juce_events/juce_events.h>
#include "Clip.h"
#include "History.h"
#include "Scale.h"
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

// State shared by the piano roll (grid, keys, ruler, velocity).
// All edits go through here so undo/redo, the audio engine, and views stay synchronized.
class RollModel : public juce::ChangeBroadcaster
{
public:
    enum class NoteSpelling { Auto, Sharps, Flats, Both };

    RollModel();

    int rowHeight = 16;                         // zoom vertical

    // Clip being edited (belongs to the arrangement). The piano roll is inactive without a selected clip.
    roll::Clip& clip()              { return *clipPtr; }
    const roll::Clip& clip() const  { return *clipPtr; }
    bool hasClip() const            { return clipPtr != &dummyClip; }
    void setClip (roll::Clip* c);
    double clipStartBeats = 0.0;                // Clip position in the arrangement (for the playhead).
    roll::History history;
    roll::Scale   scale { 0, roll::Mode::Major };

    bool   scaleOn   = false;    // "Scale Mode": choose the clip's root/scale.
    bool   highlight = true;     // K key: highlight scale rows (only when scaleOn is enabled).
    bool   fold      = false;    // Fold: only rows that contain notes.
    bool   foldScale = false;    // Scale button by Fold: scale rows plus rows containing notes.
    bool   drawMode  = false;
    bool   snap      = true;
    bool   previewOn = true;     // Preview notes when selecting/moving them.
    NoteSpelling noteSpelling = NoteSpelling::Auto;
    bool   triplet   = false;
    double baseGrid      = 0.25;   // 1/16
    double gridBeats     = 0.25;   // Effective grid (baseGrid, or 2/3 of it for triplets).
    double insertBeat    = 0.0;    // Insertion marker (paste position; playback start).
    double pixelsPerBeat = 56.0;

    std::set<int> selection;

    // Lit keys: mouse clicks and notes playing back.
    int pressedPitch = -1;
    std::uint64_t activeLo = 0, activeHi = 0;
    bool isKeyLit (int p) const
    {
        return p == pressedPitch || (p < 64 ? ((activeLo >> p) & 1u) : ((activeHi >> (p - 64)) & 1u));
    }

    std::function<void()>          onNotesChanged;   // engine de audio
    std::function<void()>          onLengthChanged;  // Transport loop.
    std::function<void (int, bool)> onPreview;       // pitch, ligar/desligar
    std::function<void (double)>   onInsertMarker;   // Insertion marker changed.
    std::function<void()> onEditStarted, onEditFinished;
    std::function<void (bool redo)> onGlobalUndoRedo;
    std::function<bool()> canGlobalUndo, canGlobalRedo;

    // --- Vertical layout (visible rows, from highest to lowest) ---
    const std::vector<int>& rows() const { return rowPitches; }
    void rebuildRows();
    int  pitchToRow (int pitch) const;           // -1 se escondida
    int  rowToPitch (int row) const;             // -1 when out of range.
    int  totalHeight() const { return static_cast<int> (rowPitches.size()) * rowHeight; }
    double gridWidthPixels() const { return clip().length() * pixelsPerBeat; }

    double snapBeats (double b) const  { return snap ? roll::snapToGrid (b, gridBeats) : b; }
    double minLength() const           { return snap ? gridBeats : 1.0 / 16.0; }
    juce::String gridLabel() const;

    // --- Editing (each operation records undo and notifies the engine + views) ---
    void beginEdit();                         // Snapshot for undo.
    void endEdit();
    void notify (bool rebuildRowLayout);      // Publishes notes + updates views.
    void undo();
    void redo();
    bool isSelected (int id) const { return selection.count (id) > 0; }

    void selectAll();
    void selectNotesAtPitch (int pitch, bool additive);
    void deleteSelected();
    void duplicateSelected();
    void nudgeSelected (double dBeats, int dPitch);
    void quantizeSelected();
    void toggleMuteSelected();
    void narrowGrid();
    void widenGrid();
    void setGrid (double beats, bool useTriplet);
    void toggleSnap();
    void toggleTriplet();
    void resizeSelected (double dBeats);
    void copySelected (bool cut);
    void paste();
    bool hasClipboardContents() const { return ! clipboard.empty(); }
    void setInsertMarker (double beats);
    void resolveOverlaps (const std::set<int>& ids);

private:
    void pruneSelection();
    roll::Clip dummyClip { 16.0 };
    roll::Clip* clipPtr = &dummyClip;
    void updateGrid();
    void extendToFit (double endBeat);
    std::vector<roll::Note> clipboard;
    std::vector<int> rowPitches;
};
