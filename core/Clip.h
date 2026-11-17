#pragma once
#include "Groove.h"
#include "Note.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace roll
{
// Arredonda 'beats' para a grade mais proxima (gridBeats = 0.25 -> 1/16).
double snapToGrid(double beats, double gridBeats);

struct PianoRollLocator
{
    int id = 0;
    double beat = 0.0;
};

class Clip
{
public:
    explicit Clip(double lengthInBeats = 16.0) : lengthBeats(lengthInBeats) {}

    double length() const { return lengthBeats; }
    void   setLength(double beats) { lengthBeats = beats; }
    const std::optional<GrooveTemplate>& groove() const { return grooveTemplate; }
    void setGroove (std::optional<GrooveTemplate> groove) { grooveTemplate = std::move (groove); }
    const std::vector<PianoRollLocator>& pianoRollLocators() const { return pianoRollLocatorValues; }
    int selectedPianoRollLocatorId() const { return selectedPianoRollLocator; }
    void setPianoRollLocators (std::vector<PianoRollLocator> values, int selectedId)
    {
        pianoRollLocatorValues = std::move (values);
        std::sort (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
                   [] (const auto& a, const auto& b) { return a.beat == b.beat ? a.id < b.id : a.beat < b.beat; });
        nextPianoRollLocatorId = 1;
        for (const auto& locator : pianoRollLocatorValues)
            nextPianoRollLocatorId = std::max (nextPianoRollLocatorId, locator.id + 1);
        selectPianoRollLocator (selectedId);
    }
    int addPianoRollLocator (double beat)
    {
        if (! std::isfinite (beat) || beat < 0.0 || beat > lengthBeats)
            return -1;
        const auto existing = std::find_if (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
            [beat] (const auto& locator) { return std::abs (locator.beat - beat) < 1.0e-9; });
        if (existing != pianoRollLocatorValues.end())
        {
            selectedPianoRollLocator = existing->id;
            return existing->id;
        }
        const int id = nextPianoRollLocatorId++;
        pianoRollLocatorValues.push_back ({ id, beat });
        std::sort (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
                   [] (const auto& a, const auto& b) { return a.beat == b.beat ? a.id < b.id : a.beat < b.beat; });
        selectedPianoRollLocator = id;
        return id;
    }
    bool movePianoRollLocator (int id, double beat)
    {
        const auto it = std::find_if (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
            [id] (const auto& locator) { return locator.id == id; });
        if (it == pianoRollLocatorValues.end() || ! std::isfinite (beat)
            || beat < 0.0 || beat > lengthBeats)
            return false;
        it->beat = beat;
        std::sort (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
                   [] (const auto& a, const auto& b) { return a.beat == b.beat ? a.id < b.id : a.beat < b.beat; });
        return true;
    }
    bool removePianoRollLocator (int id)
    {
        const auto it = std::find_if (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
            [id] (const auto& locator) { return locator.id == id; });
        if (it == pianoRollLocatorValues.end())
            return false;
        pianoRollLocatorValues.erase (it);
        if (selectedPianoRollLocator == id)
            selectedPianoRollLocator = -1;
        return true;
    }
    void selectPianoRollLocator (int id)
    {
        selectedPianoRollLocator = id == -1
            || std::any_of (pianoRollLocatorValues.begin(), pianoRollLocatorValues.end(),
                            [id] (const auto& locator) { return locator.id == id; })
            ? id : -1;
    }

    int  addNote(Note n);                       // atribui e retorna o id
    bool removeNote(int id);
    Note*       find(int id);
    const Note* find(int id) const;
    int  noteAt(int pitch, double beat) const;  // id, ou -1 se nao houver

    const std::vector<Note>& notes() const { return notesVec; }
    std::vector<Note>&       notes()       { return notesVec; }

    // Nota nova sobre outra do mesmo pitch: a nota coberta pelo inicio some,
    // a coberta pela cauda e encurtada ate o inicio da nova. 'ids' = notas recem movidas/criadas.
    void resolveOverlaps (const std::vector<int>& ids);

    // Substitui todas as notas (desfazer/refazer). Ids sao preservados.
    void setNotes(std::vector<Note> newNotes);

private:
    double lengthBeats;
    std::optional<GrooveTemplate> grooveTemplate;
    std::vector<PianoRollLocator> pianoRollLocatorValues;
    int selectedPianoRollLocator = -1, nextPianoRollLocatorId = 1;
    int nextId = 1;
    std::vector<Note> notesVec;
};
} // namespace roll
