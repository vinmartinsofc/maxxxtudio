#include "Clip.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace roll
{
double snapToGrid(double beats, double gridBeats)
{
    if (gridBeats <= 0.0) return beats;
    return std::round(beats / gridBeats) * gridBeats;
}

int Clip::addNote(Note n)
{
    n.id = nextId++;
    notesVec.push_back(n);
    return n.id;
}

bool Clip::removeNote(int id)
{
    auto it = std::find_if(notesVec.begin(), notesVec.end(), [id](const Note& n) { return n.id == id; });
    if (it == notesVec.end()) return false;
    notesVec.erase(it);
    return true;
}

Note* Clip::find(int id)
{
    for (auto& n : notesVec) if (n.id == id) return &n;
    return nullptr;
}

const Note* Clip::find(int id) const
{
    for (const auto& n : notesVec) if (n.id == id) return &n;
    return nullptr;
}

int Clip::noteAt(int pitch, double beat) const
{
    for (auto it = notesVec.rbegin(); it != notesVec.rend(); ++it)
        if (it->pitch == pitch && beat >= it->startBeats && beat < it->endBeats())
            return it->id;
    return -1;
}

void Clip::resolveOverlaps (const std::vector<int>& ids)
{
    constexpr double eps = 1.0e-9;
    std::vector<int> toRemove;

    for (int id : ids)
    {
        const Note* m = find (id);
        if (m == nullptr) continue;
        const Note moved = *m;

        for (auto& o : notesVec)
        {
            if (o.id == moved.id || o.pitch != moved.pitch) continue;
            if (std::find (ids.begin(), ids.end(), o.id) != ids.end()) continue;   // Another new note.

            if (o.startBeats >= moved.startBeats - eps && o.startBeats < moved.endBeats() - eps)
                toRemove.push_back (o.id);                                         // Covered at the beginning.
            else if (o.startBeats < moved.startBeats - eps && o.endBeats() > moved.startBeats + eps)
                o.lengthBeats = moved.startBeats - o.startBeats;                   // Tail trimmed.
        }
    }

    for (int id : toRemove) removeNote (id);
}

void Clip::setNotes(std::vector<Note> newNotes)
{
    std::set<int> usedIds;
    for (auto& note : newNotes)
        if (note.id <= 0 || ! usedIds.insert (note.id).second)
            note.id = 0;

    for (auto& note : newNotes)
    {
        if (note.id != 0)
            continue;
        while (usedIds.count (nextId) != 0)
            ++nextId;
        note.id = nextId++;
        usedIds.insert (note.id);
    }

    for (const auto& note : newNotes)
        nextId = std::max (nextId, note.id + 1);
    notesVec = std::move (newNotes);
}
} // namespace roll
