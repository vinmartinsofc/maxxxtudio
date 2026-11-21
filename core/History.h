#pragma once
#include "Note.h"
#include <vector>

namespace roll
{
// Undo/redo using snapshots of clip notes. Simple and robust:
// Clips have a few hundred notes, so copying the list is inexpensive.
class History
{
public:
    // Call BEFORE modifying. Saves the current state and invalidates "redo".
    void record(const std::vector<Note>& before);

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // Replaces 'current' with the previous/next state. Returns false if there is nothing to do.
    bool undo(std::vector<Note>& current);
    bool redo(std::vector<Note>& current);

    void clear() { undoStack.clear(); redoStack.clear(); }

private:
    static constexpr size_t maxDepth = 200;
    std::vector<std::vector<Note>> undoStack, redoStack;
};
} // namespace roll
