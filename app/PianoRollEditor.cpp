#include "PianoRollEditor.h"
#include "ZoomCursor.h"
#include <algorithm>
#include <cmath>

namespace
{
bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// Convencao MIDI: nota 60 = C3.
juce::String pitchName (int pitch, RollModel::NoteSpelling spelling, int root, bool scaleOn)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const int pitchClass = pitch % 12;
    const bool automaticFlatKey = scaleOn
        && (root == 1 || root == 3 || root == 5 || root == 6 || root == 8 || root == 10);
    const bool useFlats = spelling == RollModel::NoteSpelling::Flats
        || (spelling == RollModel::NoteSpelling::Auto && automaticFlatKey);
    juce::String name (useFlats ? flats[pitchClass] : sharps[pitchClass]);
    if (spelling == RollModel::NoteSpelling::Both && isBlackKey (pitch))
        name = juce::String (sharps[pitchClass]) + "/" + flats[pitchClass];
    return name + juce::String (pitch / 12 - 2);
}

bool isLetter (const juce::KeyPress& k, char c)
{
    return juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (k.getKeyCode())) == static_cast<juce::juce_wchar> (c);
}
} // namespace

// ============================================================ NoteGrid

void NoteGrid::updateSize()
{
    setSize (std::max (1, static_cast<int> (model.gridWidthPixels())), std::max (1, model.totalHeight()));
}

void NoteGrid::setPlayhead (double beats)
{
    const float oldX = beatsToX (playheadBeats), newX = beatsToX (beats);
    playheadBeats = beats;
    repaint (static_cast<int> (oldX) - 2, 0, 5, getHeight());
    repaint (static_cast<int> (newX) - 2, 0, 5, getHeight());
}

juce::Rectangle<float> NoteGrid::noteRect (const roll::Note& n) const
{
    const int row = model.pitchToRow (n.pitch);
    if (row < 0) return {};
    return { beatsToX (n.startBeats), static_cast<float> (row * model.rowHeight + 1),
             std::max (3.0f, static_cast<float> (n.lengthBeats * model.pixelsPerBeat)),
             static_cast<float> (model.rowHeight - 2) };
}

int NoteGrid::hitNote (juce::Point<float> p) const
{
    const auto& notes = model.clip().notes();
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
        if (noteRect (*it).contains (p)) return it->id;
    return -1;
}

void NoteGrid::showContextMenu (juce::Point<int> screenPosition)
{
    const bool hasSelection = ! model.selection.empty();
    const bool canSelectNotes = ! model.clip().notes().empty();
    juce::PopupMenu editMenu, noteMenu, gridMenu, viewMenu, spellingMenu, menu;
    editMenu.addItem (1, "Desfazer", model.canGlobalUndo ? model.canGlobalUndo() : model.history.canUndo());
    editMenu.addItem (2, "Refazer", model.canGlobalRedo ? model.canGlobalRedo() : model.history.canRedo());
    editMenu.addSeparator();
    editMenu.addItem (3, "Recortar", hasSelection);
    editMenu.addItem (4, "Copiar", hasSelection);
    editMenu.addItem (5, "Colar", model.hasClipboardContents());
    editMenu.addItem (6, "Duplicar", hasSelection);
    editMenu.addItem (7, "Apagar", hasSelection);
    editMenu.addItem (8, "Quantizar", hasSelection);
    editMenu.addItem (9, "Selecionar todas as notas", canSelectNotes);

    noteMenu.addItem (20, "Silenciar / reativar", hasSelection);
    noteMenu.addSeparator();
    noteMenu.addItem (21, "Mover para a esquerda", hasSelection);
    noteMenu.addItem (22, "Mover para a direita", hasSelection);
    noteMenu.addItem (23, "Subir um semitom", hasSelection);
    noteMenu.addItem (24, "Descer um semitom", hasSelection);
    noteMenu.addItem (25, "Subir uma oitava", hasSelection);
    noteMenu.addItem (26, "Descer uma oitava", hasSelection);
    noteMenu.addSeparator();
    noteMenu.addItem (27, "Aumentar duracao", hasSelection);
    noteMenu.addItem (28, "Reduzir duracao", hasSelection);

    const auto addGridChoice = [&gridMenu, this] (int id, const juce::String& label, double beats, bool triplet)
    {
        const bool selected = std::abs (model.baseGrid - beats) < 1.0e-9 && model.triplet == triplet;
        gridMenu.addItem (id, label, true, selected);
    };
    addGridChoice (101, "1/1", 4.0, false);
    addGridChoice (102, "1/2", 2.0, false);
    addGridChoice (103, "1/4", 1.0, false);
    addGridChoice (104, "1/8", 0.5, false);
    addGridChoice (105, "1/16", 0.25, false);
    addGridChoice (106, "1/32", 0.125, false);
    addGridChoice (107, "1/64", 0.0625, false);
    gridMenu.addSeparator();
    addGridChoice (108, "1/4T", 1.0, true);
    addGridChoice (109, "1/8T", 0.5, true);
    addGridChoice (110, "1/16T", 0.25, true);
    addGridChoice (111, "1/32T", 0.125, true);
    addGridChoice (112, "1/64T", 0.0625, true);

    viewMenu.addItem (30, "Modo de desenho", true, model.drawMode);
    viewMenu.addItem (31, "Destacar escala", true, model.highlight);
    viewMenu.addItem (32, "Modo de escala", true, model.scaleOn);
    viewMenu.addItem (33, "Fold", true, model.fold);
    viewMenu.addItem (34, "Fold da escala", true, model.foldScale);
    viewMenu.addItem (35, "Pre-escuta de notas", true, model.previewOn);
    viewMenu.addSeparator();
    viewMenu.addItem (36, "Snap a grade", true, model.snap);

    spellingMenu.addItem (201, "Automatica", true, model.noteSpelling == RollModel::NoteSpelling::Auto);
    spellingMenu.addItem (202, "Sustenidos", true, model.noteSpelling == RollModel::NoteSpelling::Sharps);
    spellingMenu.addItem (203, "Bemois", true, model.noteSpelling == RollModel::NoteSpelling::Flats);
    spellingMenu.addItem (204, "Sustenidos e bemois", true, model.noteSpelling == RollModel::NoteSpelling::Both);

    menu.addSubMenu ("Editar notas", editMenu);
    menu.addSubMenu ("Acoes das notas", noteMenu);
    menu.addSubMenu ("Grade", gridMenu);
    menu.addItem (10, "Grade tercinada", true, model.triplet);
    menu.addSubMenu ("Visualizacao", viewMenu);
    menu.addSubMenu ("Grafia das notas", spellingMenu);

    juce::Component::SafePointer<NoteGrid> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
        { screenPosition.x, screenPosition.y, 1, 1 }),
        [safeThis] (int result)
        {
            if (safeThis == nullptr) return;
            auto& m = safeThis->model;
            switch (result)
            {
                case 1:
                    if (m.onGlobalUndoRedo) m.onGlobalUndoRedo (false);
                    else m.undo();
                    break;
                case 2:
                    if (m.onGlobalUndoRedo) m.onGlobalUndoRedo (true);
                    else m.redo();
                    break;
                case 3: m.copySelected (true); break;
                case 4: m.copySelected (false); break;
                case 5: m.paste(); break;
                case 6: m.duplicateSelected(); break;
                case 7: m.deleteSelected(); break;
                case 8: m.quantizeSelected(); break;
                case 9: m.selectAll(); break;
                case 20: m.toggleMuteSelected(); break;
                case 21: m.nudgeSelected (-m.gridBeats, 0); break;
                case 22: m.nudgeSelected (m.gridBeats, 0); break;
                case 23: m.nudgeSelected (0.0, 1); break;
                case 24: m.nudgeSelected (0.0, -1); break;
                case 25: m.nudgeSelected (0.0, 12); break;
                case 26: m.nudgeSelected (0.0, -12); break;
                case 27: m.resizeSelected (m.gridBeats); break;
                case 28: m.resizeSelected (-m.gridBeats); break;
                case 30: m.drawMode = ! m.drawMode; m.sendChangeMessage(); break;
                case 31: m.highlight = ! m.highlight; m.sendChangeMessage(); break;
                case 32: m.scaleOn = ! m.scaleOn; m.rebuildRows(); m.sendChangeMessage(); break;
                case 33: m.fold = ! m.fold; m.rebuildRows(); m.sendChangeMessage(); break;
                case 34: m.foldScale = ! m.foldScale; m.rebuildRows(); m.sendChangeMessage(); break;
                case 35: m.previewOn = ! m.previewOn; m.sendChangeMessage(); break;
                case 36: m.toggleSnap(); break;
                case 10: m.toggleTriplet(); break;
                case 101: m.setGrid (4.0, false); break;
                case 102: m.setGrid (2.0, false); break;
                case 103: m.setGrid (1.0, false); break;
                case 104: m.setGrid (0.5, false); break;
                case 105: m.setGrid (0.25, false); break;
                case 106: m.setGrid (0.125, false); break;
                case 107: m.setGrid (0.0625, false); break;
                case 108: m.setGrid (1.0, true); break;
                case 109: m.setGrid (0.5, true); break;
                case 110: m.setGrid (0.25, true); break;
                case 111: m.setGrid (0.125, true); break;
                case 112: m.setGrid (0.0625, true); break;
                case 201: m.noteSpelling = RollModel::NoteSpelling::Auto; m.sendChangeMessage(); break;
                case 202: m.noteSpelling = RollModel::NoteSpelling::Sharps; m.sendChangeMessage(); break;
                case 203: m.noteSpelling = RollModel::NoteSpelling::Flats; m.sendChangeMessage(); break;
                case 204: m.noteSpelling = RollModel::NoteSpelling::Both; m.sendChangeMessage(); break;
                default: break;
            }
        });
}

void NoteGrid::paint (juce::Graphics& g)
{
    const auto clipBox = g.getClipBounds();
    if (! model.hasClip())
    {
        g.fillAll (theme::rowBlack());
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (14.5f));
        g.drawFittedText (theme::ui ("Selecione um clip no arranjo\n(duplo clique em uma trilha cria um clip)"),
                          getLocalBounds().reduced (10), juce::Justification::centred, 3);
        return;
    }
    const int firstRow = std::max (0, clipBox.getY() / model.rowHeight);
    const int lastRow  = std::min (static_cast<int> (model.rows().size()) - 1, clipBox.getBottom() / model.rowHeight);

    for (int r = firstRow; r <= lastRow; ++r)
    {
        const int pitch = model.rowToPitch (r);
        juce::Colour bg = isBlackKey (pitch) ? theme::rowBlack() : theme::rowWhite();
        const bool showScale = model.scaleOn && model.highlight;
        if (showScale && ! model.foldScale && model.scale.contains (pitch))
            bg = theme::rowInScale().withMultipliedBrightness (isBlackKey (pitch) ? 0.85f : 1.0f);
        if (showScale && pitch % 12 == model.scale.root())
            bg = bg.brighter (0.12f);                       // tonica em destaque
        g.setColour (bg);
        g.fillRect (clipBox.getX(), r * model.rowHeight, clipBox.getWidth(), model.rowHeight);

        g.setColour (pitch % 12 == 0 ? theme::border().brighter (0.3f) : theme::border().withAlpha (0.45f));
        g.drawHorizontalLine ((r + 1) * model.rowHeight - 1, static_cast<float> (clipBox.getX()), static_cast<float> (clipBox.getRight()));
    }

    // linhas verticais: grade, beat, compasso
    const double firstBeat = std::floor (xToBeats (static_cast<float> (clipBox.getX())) / model.gridBeats) * model.gridBeats;
    for (double b = std::max (0.0, firstBeat); b <= model.clip().length() + 1e-9; b += model.gridBeats)
    {
        const float x = beatsToX (b);
        if (x > clipBox.getRight()) break;
        const bool bar  = std::abs (b / 4.0 - std::round (b / 4.0)) < 1e-6;
        const bool beat = std::abs (b - std::round (b)) < 1e-6;
        g.setColour (bar ? theme::border().brighter (0.6f) : beat ? theme::border().brighter (0.2f) : theme::border().withAlpha (0.5f));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (clipBox.getY()), static_cast<float> (clipBox.getBottom()));
    }

    // notas
    for (const auto& n : model.clip().notes())
    {
        auto r = noteRect (n);
        if (r.isEmpty() || ! r.toNearestInt().intersects (clipBox)) continue;

        const bool sel = model.isSelected (n.id);
        const float vel = static_cast<float> (n.velocity) / 127.0f;
        juce::Colour c = sel ? theme::noteSelected() : theme::noteFill();
        c = c.withMultipliedBrightness (0.55f + 0.45f * vel);
        if (n.muted) c = theme::textDim();

        g.setColour (c);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    if (drag == Drag::RubberPending || drag == Drag::Rubber)
    {
        g.setColour (theme::accent().withAlpha (0.18f));
        g.fillRect (rubberRect);
        g.setColour (theme::accent());
        g.drawRect (rubberRect, 1.0f);
    }

    if (drag == Drag::Resize && editBegun)
        if (const auto* note = model.clip().find (primaryId))
        {
            const auto noteBounds = noteRect (*note);
            const juce::String message = "Nota: " + juce::String (note->lengthBeats, 2) + " tempos";
            const int x = std::clamp (static_cast<int> (noteBounds.getRight()) + 8, 4, std::max (4, getWidth() - 150));
            const int y = std::clamp (static_cast<int> (noteBounds.getY()), 4, std::max (4, getHeight() - 26));
            const auto hint = juce::Rectangle<int> (x, y, 146, 22);
            g.setColour (theme::panel().withAlpha (0.96f));
            g.fillRoundedRectangle (hint.toFloat(), 5.0f);
            g.setColour (theme::accent());
            g.drawRoundedRectangle (hint.toFloat(), 5.0f, 1.0f);
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (message, hint.reduced (6, 0), juce::Justification::centredLeft, true);
        }

    g.setColour (theme::accent().withAlpha (0.9f));                      // marcador de insercao
    g.drawVerticalLine (static_cast<int> (beatsToX (model.insertBeat)), 0.0f, static_cast<float> (getHeight()));

    g.setColour (theme::playhead());
    g.drawVerticalLine (static_cast<int> (beatsToX (playheadBeats)), 0.0f, static_cast<float> (getHeight()));
}

void NoteGrid::setPreview (int pitch)
{
    if (previewPitch == pitch) return;
    if (previewPitch >= 0 && model.onPreview) model.onPreview (previewPitch, false);
    previewPitch = pitch;
    if (previewPitch >= 0 && model.onPreview) model.onPreview (previewPitch, true);
}

void NoteGrid::flashPreview (int pitch)
{
    if (! model.onPreview) return;
    model.onPreview (pitch, true);
    juce::Component::SafePointer<NoteGrid> safe (this);
    juce::Timer::callAfterDelay (200, [safe, pitch]
    {
        if (safe != nullptr && safe->model.onPreview) safe->model.onPreview (pitch, false);
    });
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! model.hasClip()) return;
    const auto p = e.position;
    const int id = hitNote (p);
    if (e.mods.isPopupMenu())
    {
        if (id >= 0 && ! model.isSelected (id))
        {
            if (! e.mods.isShiftDown()) model.selection.clear();
            model.selection.insert (id);
            model.sendChangeMessage();
        }
        showContextMenu (e.getScreenPosition());
        return;
    }

    drag = Drag::None;
    editBegun = false;
    copied = false;
    original.clear();
    downBeat = xToBeats (p.x);

    if (id >= 0)
    {
        if (e.mods.isShiftDown())
        {
            baseSelection = model.selection;
            pendingRubberNoteId = id;
            rubberStart = p;
            rubberRect = {};
            drag = Drag::RubberPending;
            if (const auto* note = model.clip().find (id))
                setPreview (note->pitch);
            updateCursor (p);
            return;
        }
        if (! model.isSelected (id))
        {
            if (! e.mods.isShiftDown()) model.selection.clear();
            model.selection.insert (id);
        }

        primaryId = id;
        for (int sid : model.selection)
            if (auto* n = model.clip().find (sid)) original[sid] = *n;

        const auto r = noteRect (*model.clip().find (id));
        const float edge = std::min (6.0f, r.getWidth() * 0.4f);
        drag = (p.x > r.getRight() - edge) ? Drag::Resize : Drag::Move;
        updateCursor (p);
        setPreview (model.clip().find (id)->pitch);
        model.sendChangeMessage();
        return;
    }

    const int pitch = model.rowToPitch (yToRow (p.y));
    if (model.drawMode && pitch >= 0)
    {
        const double start = model.snap ? std::floor (downBeat / model.gridBeats) * model.gridBeats : downBeat;
        model.beginEdit();
        editBegun = true;
        roll::Note n;
        n.pitch = pitch;
        n.startBeats = std::max (0.0, start);
        n.lengthBeats = model.minLength();
        primaryId = model.clip().addNote (n);
        model.selection = { primaryId };
        original[primaryId] = *model.clip().find (primaryId);
        downBeat = n.endBeats();
        drag = Drag::Resize;
        updateCursor (p);
        setPreview (pitch);
        model.notify (false);
        return;
    }

    if (! e.mods.isShiftDown())
        model.setInsertMarker (model.snapBeats (downBeat));       // clique no fundo = marcador de insercao

    baseSelection = e.mods.isShiftDown() ? model.selection : std::set<int>{};
    model.selection = baseSelection;
    rubberStart = p;
    rubberRect = {};
    drag = Drag::Rubber;
    updateCursor (p);
    model.sendChangeMessage();
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (e.position);
}

void NoteGrid::mouseExit (const juce::MouseEvent&)
{
    if (drag == Drag::None)
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void NoteGrid::updateCursor (juce::Point<float> position)
{
    if (! model.hasClip())
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    if (drag == Drag::Resize)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (drag == Drag::Move)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (drag == Drag::Rubber)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }

    const int id = hitNote (position);
    if (id < 0)
    {
        setMouseCursor (model.drawMode ? juce::MouseCursor::CrosshairCursor
                                       : juce::MouseCursor::NormalCursor);
        return;
    }

    if (const auto* note = model.clip().find (id))
    {
        const auto bounds = noteRect (*note);
        const float edge = std::min (6.0f, bounds.getWidth() * 0.4f);
        setMouseCursor (position.x > bounds.getRight() - edge ? juce::MouseCursor::LeftRightResizeCursor
                                                               : juce::MouseCursor::DraggingHandCursor);
    }
}

void NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (drag == Drag::RubberPending)
    {
        if (e.getDistanceFromDragStart() < 4)
            return;
        drag = Drag::Rubber;
        model.selection = baseSelection;
    }

    if (drag == Drag::Rubber)
    {
        rubberRect = juce::Rectangle<float> (rubberStart, p);
        model.selection = baseSelection;
        for (const auto& n : model.clip().notes())
            if (noteRect (n).intersects (rubberRect)) model.selection.insert (n.id);
        repaint();
        model.sendChangeMessage();
        return;
    }

    if ((drag != Drag::Move && drag != Drag::Resize) || original.empty()) return;
    if (! editBegun)
    {
        if (e.getDistanceFromDragStart() < 3) return;
        model.beginEdit();
        editBegun = true;
    }

    const double beat = xToBeats (p.x);
    const auto& prim = original.at (primaryId);

    if (drag == Drag::Move)
    {
        if (e.mods.isCtrlDown() && ! copied)                       // Ctrl+arrastar: deixa copias no lugar
        {
            for (const auto& [id, o] : original)
            {
                roll::Note c = o;
                model.clip().addNote (c);
            }
            copied = true;
        }

        const bool noSnap = e.mods.isAltDown();
        double newStart = prim.startBeats + (beat - downBeat);
        newStart = noSnap ? newStart : model.snapBeats (newStart);
        double dBeats = newStart - prim.startBeats;

        const int row = std::clamp (yToRow (p.y), 0, static_cast<int> (model.rows().size()) - 1);
        int dPitch = model.rowToPitch (row) - prim.pitch;

        for (const auto& [id, o] : original)
        {
            dBeats = std::max (dBeats, -o.startBeats);
            dBeats = std::min (dBeats, model.clip().length() - model.minLength() - o.startBeats);
            dPitch = std::clamp (dPitch, -o.pitch, 127 - o.pitch);
        }

        for (const auto& [id, o] : original)
            if (auto* n = model.clip().find (id)) { n->startBeats = o.startBeats + dBeats; n->pitch = o.pitch + dPitch; }

        setPreview (prim.pitch + dPitch);
    }
    else
    {
        const bool noSnap = e.mods.isAltDown();
        double newEnd = prim.endBeats() + (beat - downBeat);
        newEnd = noSnap ? newEnd : model.snapBeats (newEnd);
        double dLen = newEnd - prim.endBeats();

        for (const auto& [id, o] : original)
            dLen = std::max (dLen, model.minLength() - o.lengthBeats);

        for (const auto& [id, o] : original)
            if (auto* n = model.clip().find (id)) n->lengthBeats = o.lengthBeats + dLen;
    }

    model.notify (false);
}

void NoteGrid::mouseUp (const juce::MouseEvent& e)
{
    setPreview (-1);
    if (drag == Drag::RubberPending && pendingRubberNoteId >= 0)
    {
        if (baseSelection.count (pendingRubberNoteId) > 0)
            baseSelection.erase (pendingRubberNoteId);
        else
            baseSelection.insert (pendingRubberNoteId);
        model.selection = baseSelection;
        model.sendChangeMessage();
    }
    if (editBegun)
    {
        model.resolveOverlaps (model.selection);
        model.notify (true);
        model.endEdit();
    }
    drag = Drag::None;
    editBegun = false;
    rubberRect = {};
    pendingRubberNoteId = -1;
    original.clear();
    updateCursor (e.position);
    repaint();
}

void NoteGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! model.hasClip()) return;
    const auto p = e.position;
    const int id = hitNote (p);

    if (id >= 0)                                   // duplo clique em nota: apaga
    {
        model.beginEdit();
        model.clip().removeNote (id);
        model.selection.erase (id);
        model.notify (true);
        model.endEdit();
        return;
    }

    const int pitch = model.rowToPitch (yToRow (p.y));
    if (pitch < 0) return;

    const double beat = xToBeats (p.x);
    roll::Note n;
    n.pitch = pitch;
    n.startBeats = std::max (0.0, model.snap ? std::floor (beat / model.gridBeats) * model.gridBeats : beat);
    n.lengthBeats = model.snap ? model.gridBeats : 0.25;
    if (n.startBeats >= model.clip().length()) return;

    model.beginEdit();
    const int newId = model.clip().addNote (n);
    model.selection = { newId };
    model.notify (true);
    model.endEdit();
    flashPreview (pitch);
}

void NoteGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())                       // Ctrl + roda: zoom horizontal
    {
        const double factor = w.deltaY > 0 ? 1.15 : 1.0 / 1.15;
        if (onZoomHorizontal) onZoomHorizontal (factor);
        return;
    }
    if (e.mods.isAltDown())                        // Alt + roda: zoom vertical
    {
        model.rowHeight = std::clamp (model.rowHeight + (w.deltaY > 0 ? 2 : -2), 8, 32);
        model.sendChangeMessage();
        return;
    }
    Component::mouseWheelMove (e, w);              // repassa ao viewport (rolagem)
}

bool NoteGrid::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const bool ctrl = mods.isCtrlDown() || mods.isCommandDown();
    const bool shift = mods.isShiftDown();
    const int code = k.getKeyCode();

    if (ctrl && isLetter (k, 'A')) { model.selectAll(); return true; }
    if (ctrl && isLetter (k, 'D')) { model.duplicateSelected(); return true; }
    if (ctrl && isLetter (k, 'C')) { model.copySelected (false); return true; }
    if (ctrl && isLetter (k, 'X')) { model.copySelected (true);  return true; }
    if (ctrl && isLetter (k, 'V')) { model.paste(); return true; }
    if (ctrl && isLetter (k, 'U')) { model.quantizeSelected(); return true; }
    if (ctrl && isLetter (k, 'Z') && ! shift)
    {
        if (model.onGlobalUndoRedo) model.onGlobalUndoRedo (false);
        else model.undo();
        return true;
    }
    if (ctrl && (isLetter (k, 'Y') || (isLetter (k, 'Z') && shift)))
    {
        if (model.onGlobalUndoRedo) model.onGlobalUndoRedo (true);
        else model.redo();
        return true;
    }

    if (ctrl && code == '1') { model.narrowGrid();    return true; }
    if (ctrl && code == '2') { model.widenGrid();     return true; }
    if (ctrl && code == '3') { model.toggleTriplet(); return true; }
    if (ctrl && code == '4') { model.toggleSnap();    return true; }

    if (! ctrl && isLetter (k, 'B')) { model.drawMode = ! model.drawMode; model.sendChangeMessage(); return true; }
    if (! ctrl && isLetter (k, 'K')) { model.highlight = ! model.highlight; model.sendChangeMessage(); return true; }
    const bool zeroKey = code == '0' || code == juce::KeyPress::numberPad0
                      || k.getTextCharacter() == '0';
    if (! ctrl && ! shift && ! mods.isAltDown() && zeroKey)
    {
        model.toggleMuteSelected();
        return true;
    }

    if (k.isKeyCode (juce::KeyPress::escapeKey)) { model.selection.clear(); model.sendChangeMessage(); return true; }
    if (k.isKeyCode (juce::KeyPress::homeKey))   { model.setInsertMarker (0.0);                return true; }
    if (k.isKeyCode (juce::KeyPress::endKey))    { model.setInsertMarker (model.clip().length()); return true; }

    // zoom horizontal com + e -
    if (! ctrl)
    {
        const auto ch = k.getTextCharacter();
        if (ch == '+' || ch == '=') { if (onZoomHorizontal) onZoomHorizontal (1.25);       return true; }
        if (ch == '-' || ch == '_') { if (onZoomHorizontal) onZoomHorizontal (1.0 / 1.25); return true; }
    }

    // Page Up/Down: rola na vertical; Ctrl+Page Up/Down: na horizontal
    if (k.isKeyCode (juce::KeyPress::pageUpKey) || k.isKeyCode (juce::KeyPress::pageDownKey))
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const int dir = k.isKeyCode (juce::KeyPress::pageUpKey) ? -1 : 1;
            auto pos = vp->getViewPosition();
            if (ctrl) pos.x += dir * vp->getMaximumVisibleWidth() * 9 / 10;
            else      pos.y += dir * vp->getMaximumVisibleHeight() * 9 / 10;
            vp->setViewPosition (std::max (0, pos.x), std::max (0, pos.y));
            return true;
        }
    }

    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        if (model.selection.empty()) return false;
        model.deleteSelected();
        return true;
    }

    if (! model.selection.empty() && ! ctrl)
    {
        if (k.isKeyCode (juce::KeyPress::upKey))   { model.nudgeSelected (0.0, shift ? 12 : 1);  return true; }
        if (k.isKeyCode (juce::KeyPress::downKey)) { model.nudgeSelected (0.0, shift ? -12 : -1); return true; }
        if (k.isKeyCode (juce::KeyPress::leftKey))  { shift ? model.resizeSelected (-model.gridBeats) : model.nudgeSelected (-model.gridBeats, 0); return true; }
        if (k.isKeyCode (juce::KeyPress::rightKey)) { shift ? model.resizeSelected ( model.gridBeats) : model.nudgeSelected ( model.gridBeats, 0); return true; }
    }
    return false;                                  // Espaco etc: sobe para a janela principal
}

// ============================================================ KeyColumn

void KeyColumn::paint (juce::Graphics& g)
{
    const auto clipBox = g.getClipBounds();
    const int firstRow = std::max (0, clipBox.getY() / model.rowHeight);
    const int lastRow  = std::min (static_cast<int> (model.rows().size()) - 1, clipBox.getBottom() / model.rowHeight);

    for (int r = firstRow; r <= lastRow; ++r)
    {
        const int pitch = model.rowToPitch (r);
        const int y = r * model.rowHeight;
        const bool black = isBlackKey (pitch);

        const bool lit = model.isKeyLit (pitch);
        g.setColour (lit ? theme::accent()
                         : black ? juce::Colours::black : juce::Colours::white);
        g.fillRect (0, y, getWidth(), model.rowHeight - 1);

        if (model.scaleOn && model.highlight && model.scale.contains (pitch))
        {
            g.setColour (theme::accent().withAlpha (0.55f));
            g.fillRect (getWidth() - zoomStripWidth - 5, y, 5, model.rowHeight - 1);
        }

        const float fontSize = std::min (11.0f, std::max (7.0f, static_cast<float> (model.rowHeight - 1)));
        g.setColour (lit ? theme::text() : black ? theme::textDim() : juce::Colours::black);
        g.setFont (juce::FontOptions (fontSize, pitch % 12 == 0 ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (pitchName (pitch, model.noteSpelling, model.scale.root(), model.scaleOn),
                          juce::Rectangle<int> (4, y, getWidth() - zoomStripWidth - 10, model.rowHeight),
                          juce::Justification::centredRight, 1);
    }

    g.setColour (theme::accent().withAlpha (zoomHovered ? 0.28f : 0.10f));
    g.fillRect (getWidth() - zoomStripWidth, clipBox.getY(), zoomStripWidth, clipBox.getHeight());
    g.setColour (theme::border());
    g.drawVerticalLine (getWidth() - zoomStripWidth, static_cast<float> (clipBox.getY()),
                        static_cast<float> (clipBox.getBottom()));
}

void KeyColumn::setPitch (int pitch)
{
    if (pitch == current) return;
    if (current >= 0 && model.onPreview) model.onPreview (current, false);
    current = pitch;
    if (current >= 0 && model.onPreview) model.onPreview (current, true);
    model.pressedPitch = current;
    repaint();
}

bool KeyColumn::isZoomRegion (juce::Point<float> position) const
{
    return position.x >= getWidth() - zoomStripWidth;
}

void KeyColumn::mouseDown (const juce::MouseEvent& e)
{
    if (isZoomRegion (e.position))
    {
        zoomDragging = true;
        zoomStartScreenPosition = e.getScreenPosition();
        if (onZoomStart)
            onZoomStart (zoomStartScreenPosition.y);
        setMouseCursor (ui::zoomCursor());
        return;
    }

    const int pitch = model.rowToPitch (e.y / model.rowHeight);
    if (pitch >= 0)
    {
        model.selectNotesAtPitch (pitch, e.mods.isShiftDown());
        if (auto* editor = findParentComponentOfClass<PianoRollEditor>())
            editor->grabFocus();
    }
    setPitch (pitch);
}

void KeyColumn::mouseMove (const juce::MouseEvent& e)
{
    const bool hovering = isZoomRegion (e.position);
    if (zoomHovered != hovering)
    {
        zoomHovered = hovering;
        repaint (getWidth() - zoomStripWidth, 0, zoomStripWidth, getHeight());
    }
    setMouseCursor (hovering ? ui::zoomCursor() : juce::MouseCursor::NormalCursor);
}

void KeyColumn::mouseExit (const juce::MouseEvent&)
{
    zoomHovered = false;
    repaint (getWidth() - zoomStripWidth, 0, zoomStripWidth, getHeight());
    if (! zoomDragging)
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void KeyColumn::mouseDrag (const juce::MouseEvent& e)
{
    if (zoomDragging)
    {
        if (onZoomDrag)
        {
            const auto screenPosition = e.getScreenPosition();
            onZoomDrag (static_cast<float> (screenPosition.x - zoomStartScreenPosition.x),
                        static_cast<float> (screenPosition.y - zoomStartScreenPosition.y),
                        screenPosition.y);
        }
        return;
    }

    setPitch (model.rowToPitch (e.y / model.rowHeight));
}

void KeyColumn::mouseUp (const juce::MouseEvent& e)
{
    if (zoomDragging)
    {
        zoomDragging = false;
        mouseMove (e);
        return;
    }

    setPitch (-1);
}

// ============================================================ RulerBar

void RulerBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelLight());

    const auto clipBox = g.getClipBounds();
    const double ppb = model.pixelsPerBeat;
    const int totalBeats = static_cast<int> (std::ceil (model.clip().length()));

    g.setColour (theme::accent().withAlpha (0.22f));
    g.fillRect (0.0f, 0.0f, static_cast<float> (model.gridWidthPixels()), 4.0f);   // faixa do clip/loop

    g.setFont (juce::FontOptions (12.5f));
    for (int b = 0; b <= totalBeats; ++b)
    {
        const float x = static_cast<float> (b * ppb);
        if (x < clipBox.getX() - 40 || x > clipBox.getRight() + 40) continue;
        const bool bar = (b % 4 == 0);
        g.setColour (bar ? theme::text() : theme::textDim());
        g.drawVerticalLine (static_cast<int> (x), bar ? 6.0f : 14.0f, static_cast<float> (getHeight()));
        if (bar)
            g.drawText (juce::String (b / 4 + 1), static_cast<int> (x) + 3, 4, 40, getHeight() - 6, juce::Justification::centredLeft);
        else if (ppb >= 40)
            g.drawText (juce::String (b / 4 + 1) + "." + juce::String (b % 4 + 1), static_cast<int> (x) + 3, 12, 40, getHeight() - 12, juce::Justification::centredLeft);
    }

    for (std::size_t index = 0; index < locators.size(); ++index)
    {
        const double localBeat = locators[index].second;
        if (localBeat < 0.0 || localBeat > model.clip().length())
            continue;

        const float x = static_cast<float> (localBeat * ppb);
        const auto colour = locators[index].first == selectedLocatorId
            ? theme::recording().brighter (0.25f) : theme::recording();
        g.setColour (colour);
        g.drawVerticalLine (static_cast<int> (x), 0.0f, static_cast<float> (getHeight()));
        juce::Path flag;
        flag.startNewSubPath (x + 1.0f, 1.0f);
        flag.lineTo (x + 12.0f, 1.0f);
        flag.lineTo (x + 10.0f, 6.0f);
        flag.lineTo (x + 12.0f, 11.0f);
        flag.lineTo (x + 1.0f, 11.0f);
        flag.closeSubPath();
        g.fillPath (flag);
        g.setColour (theme::panelLight());
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawText (juce::String (static_cast<int> (index + 1)),
                    juce::Rectangle<int> (static_cast<int> (x) + 1, 0, 10, 11),
                    juce::Justification::centred, true);
    }

    if (hovered && ! hoveringLocator && getWidth() > 0)
    {
        const float centreX = std::clamp (hoveredX, 10.0f, static_cast<float> (getWidth()) - 10.0f);
        const float centreY = static_cast<float> (getHeight()) * 0.67f;
        g.setColour (theme::accent());
        juce::Path speaker;
        speaker.addTriangle (centreX - 8.0f, centreY - 3.0f,
                             centreX - 4.0f, centreY - 3.0f,
                             centreX - 1.0f, centreY - 6.0f);
        speaker.addTriangle (centreX - 8.0f, centreY + 3.0f,
                             centreX - 4.0f, centreY + 3.0f,
                             centreX - 1.0f, centreY + 6.0f);
        speaker.addRectangle (centreX - 8.0f, centreY - 3.0f, 4.0f, 6.0f);
        g.fillPath (speaker);
        juce::Path waves;
        waves.addCentredArc (centreX - 1.0f, centreY, 5.0f, 5.0f,
                             0.0f, -0.8f, 0.8f, true);
        waves.addCentredArc (centreX - 1.0f, centreY, 8.0f, 8.0f,
                             0.0f, -0.8f, 0.8f, true);
        g.strokePath (waves, juce::PathStrokeType (1.4f));
    }
}

void RulerBar::setLocators (std::vector<std::pair<int, double>> newLocators, int selectedId)
{
    locators = std::move (newLocators);
    selectedLocatorId = selectedId;
    repaint();
}

void RulerBar::seek (const juce::MouseEvent& e)
{
    if (onSeek) onSeek (std::clamp (e.position.x / model.pixelsPerBeat, 0.0, model.clip().length()));
}

int RulerBar::locatorAt (float x) const
{
    for (auto it = locators.rbegin(); it != locators.rend(); ++it)
    {
        const double localBeat = it->second;
        if (localBeat >= 0.0 && localBeat <= model.clip().length()
            && std::abs (x - static_cast<float> (localBeat * model.pixelsPerBeat)) < 14.0f)
            return it->first;
    }
    return -1;
}

bool RulerBar::isResizeHandle (juce::Point<float> position) const
{
    return position.x >= getWidth() - 8.0f;
}

void RulerBar::mouseDown (const juce::MouseEvent& e)
{
    hovered = true;
    hoveredX = e.position.x;
    hoveringLocator = locatorAt (e.position.x) >= 0;
    repaint();
    last = e.getEventRelativeTo (getParentComponent()).position;
    dragging = false;
    resizing = isResizeHandle (e.position);
    locatorDragPending = false;
    locatorDragging = false;
    draggedLocatorId = -1;

    if (! e.mods.isPopupMenu())
    {
        const int locatorId = locatorAt (e.position.x);
        const auto marker = std::find_if (locators.begin(), locators.end(),
            [locatorId] (const auto& item) { return item.first == locatorId; });
        if (marker != locators.end())
        {
            hoveringLocator = true;
            locatorDragPending = true;
            draggedLocatorId = locatorId;
            locatorGrabOffset = e.position.x / model.pixelsPerBeat - marker->second;
            resizing = false;
            return;
        }
    }
}

void RulerBar::mouseMove (const juce::MouseEvent& e)
{
    hovered = true;
    hoveredX = e.position.x;
    hoveringLocator = locatorAt (e.position.x) >= 0;
    repaint();
    setMouseCursor (locatorAt (e.position.x) >= 0 ? juce::MouseCursor::DraggingHandCursor
                       : isResizeHandle (e.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                     : juce::MouseCursor::NormalCursor);
}

void RulerBar::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    hoveringLocator = false;
    repaint();
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void RulerBar::mouseDrag (const juce::MouseEvent& e)
{
    hoveredX = e.position.x;
    hoveringLocator = locatorDragPending || locatorAt (e.position.x) >= 0;
    repaint();
    if (locatorDragPending)
    {
        if (! locatorDragging && e.getDistanceFromDragStart() < 4)
            return;
        if (! locatorDragging)
        {
            locatorDragging = true;
            if (onLocatorMoveStarted)
                onLocatorMoveStarted (draggedLocatorId);
        }
        const double rawBeat = e.position.x / model.pixelsPerBeat - locatorGrabOffset;
        const double beat = std::clamp (model.snap && ! e.mods.isAltDown() ? model.snapBeats (rawBeat) : rawBeat,
                                        0.0, model.clip().length());
        if (onLocatorMoved)
            onLocatorMoved (draggedLocatorId, beat);
        return;
    }
    if (! dragging && e.getDistanceFromDragStart() < 4) return;
    if (! dragging) last = e.getEventRelativeTo (getParentComponent()).position;
    dragging = true;
    const auto now = e.getEventRelativeTo (getParentComponent()).position;     // posicao estavel durante o rolar
    const float dx = now.x - last.x, dy = now.y - last.y;
    last = now;
    if (resizing)
    {
        if (onZoom) onZoom (std::exp (static_cast<double> (dx) * 0.0015));
        return;
    }
    if (onDrag) onDrag (dx, dy);
}

void RulerBar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() && onZoom)
        onZoom (w.deltaY > 0 ? 1.15 : 1.0 / 1.15);
}

void RulerBar::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const int locatorId = locatorAt (e.position.x);
        if (locatorId >= 0)
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Apagar locator");
            juce::Component::SafePointer<RulerBar> safeThis (this);
            const auto screen = localPointToGlobal (e.position.toInt());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                { screen.x, screen.y, 1, 1 }),
                [safeThis, locatorId] (int result)
                {
                    if (result == 1 && safeThis != nullptr && safeThis->onLocatorDeleteRequested)
                        safeThis->onLocatorDeleteRequested (locatorId);
                });
        }
        dragging = false;
        resizing = false;
        locatorDragPending = locatorDragging = false;
        draggedLocatorId = -1;
        return;
    }

    if (locatorDragPending)
    {
        if (locatorDragging)
        {
            if (onLocatorMoveFinished)
                onLocatorMoveFinished();
        }
        else if (onLocatorSelected)
            onLocatorSelected (draggedLocatorId);
        locatorDragPending = locatorDragging = false;
        draggedLocatorId = -1;
        return;
    }

    if (! dragging && ! resizing)
    {
        const int locatorId = locatorAt (e.position.x);
        if (locatorId >= 0 && onLocatorSelected)
            onLocatorSelected (locatorId);
        else
            seek (e);
    }
    dragging = false;
    resizing = false;
}

// =============================================================== PianoRollOverview

juce::Rectangle<float> PianoRollOverview::visibleRangeBounds() const
{
    const double length = std::max (0.001, model.clip().length());
    const double beatsPerPixel = length / std::max (1, getWidth());
    const double startBeat = viewport.getViewPositionX() / model.pixelsPerBeat;
    const double visibleBeats = viewport.getMaximumVisibleWidth() / model.pixelsPerBeat;
    const float x = static_cast<float> (startBeat / beatsPerPixel);
    const float width = static_cast<float> (visibleBeats / beatsPerPixel);
    return juce::Rectangle<float> (x, 0.0f, width, static_cast<float> (getHeight()))
        .getIntersection (getLocalBounds().toFloat());
}

void PianoRollOverview::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel().darker (0.55f));
    const auto bounds = getLocalBounds().toFloat();
    if (model.hasClip() && bounds.getWidth() > 0.0f)
    {
        const double length = std::max (0.001, model.clip().length());
        const float beatsToPixels = static_cast<float> (bounds.getWidth() / length);
        const float pitchHeight = bounds.getHeight() / 128.0f;

        for (const auto& note : model.clip().notes())
        {
            const float x = static_cast<float> (note.startBeats) * beatsToPixels;
            const float width = std::max (1.0f, static_cast<float> (note.lengthBeats) * beatsToPixels);
            const float y = bounds.getY() + (127.0f - static_cast<float> (note.pitch)) * pitchHeight;
            const float height = std::max (1.0f, pitchHeight);
            auto colour = model.isSelected (note.id) ? theme::noteSelected() : theme::noteFill();
            if (note.muted)
                colour = theme::textDim();
            g.setColour (colour);
            g.fillRect (x, y, width, height);
        }

        const auto visible = visibleRangeBounds();
        g.setColour (theme::accent().withAlpha (hovered ? 0.26f : 0.16f));
        g.fillRect (visible);
        g.setColour (hovered ? theme::accent() : theme::border());
        g.drawRect (visible, 1.0f);
    }

    g.setColour (theme::border());
    g.drawRect (getLocalBounds(), 1);

    if (hovered)
    {
        const auto button = juce::Rectangle<float> (static_cast<float> (getWidth() - 28), 1.0f,
                                                     26.0f, static_cast<float> (getHeight() - 2));
        g.setColour (speakerHovered ? theme::accent().withAlpha (0.8f)
                                   : theme::panel().brighter (0.12f));
        g.fillRoundedRectangle (button, 3.0f);
        g.setColour (theme::text());
        const float centreY = button.getCentreY();
        juce::Path speaker;
        speaker.addTriangle (button.getX() + 6.0f, centreY - 3.0f,
                             button.getX() + 10.0f, centreY - 3.0f,
                             button.getX() + 14.0f, centreY - 7.0f);
        speaker.addTriangle (button.getX() + 6.0f, centreY + 3.0f,
                             button.getX() + 10.0f, centreY + 3.0f,
                             button.getX() + 14.0f, centreY + 7.0f);
        g.fillPath (speaker);
        g.fillRect (button.getX() + 6.0f, centreY - 3.0f, 4.0f, 6.0f);
        juce::Path waves;
        waves.addCentredArc (button.getX() + 12.0f, centreY, 5.0f, 5.0f,
                             0.0f, -0.8f, 0.8f, true);
        waves.addCentredArc (button.getX() + 12.0f, centreY, 8.0f, 8.0f,
                             0.0f, -0.8f, 0.8f, true);
        g.strokePath (waves, juce::PathStrokeType (1.4f));
        if (isTransportPlaying && isTransportPlaying())
        {
            g.fillRect (button.getX() + 18.0f, centreY - 4.0f, 2.0f, 8.0f);
            g.fillRect (button.getX() + 22.0f, centreY - 4.0f, 2.0f, 8.0f);
        }
    }
}

void PianoRollOverview::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && e.x >= getWidth() - 28)
    {
        if (onTransportToggle)
            onTransportToggle();
        return;
    }
    if (! e.mods.isLeftButtonDown() || ! model.hasClip() || getWidth() <= 0)
        return;

    const double length = std::max (0.001, model.clip().length());
    const double beat = std::clamp (static_cast<double> (e.x) / getWidth() * length, 0.0, length);
    const int visibleWidth = std::max (1, viewport.getMaximumVisibleWidth());
    zoomStart = model.pixelsPerBeat;
    startViewX = viewport.getViewPositionX();
    startPosition = e.position;

    if (visibleRangeBounds().contains (e.position))
    {
        anchorBeat = beat;
        anchorScreenX = std::clamp (static_cast<int> (std::lround (
            anchorBeat * model.pixelsPerBeat - startViewX)), 0, visibleWidth);
    }
    else
    {
        anchorBeat = beat;
        anchorScreenX = visibleWidth / 2;
        viewport.setViewPosition (std::max (0, static_cast<int> (std::lround (
            anchorBeat * model.pixelsPerBeat - anchorScreenX))), viewport.getViewPositionY());
        startViewX = viewport.getViewPositionX();
    }

    dragging = true;
    setMouseCursor (ui::zoomCursor());
}

void PianoRollOverview::mouseMove (const juce::MouseEvent& e)
{
    if (! hovered)
    {
        hovered = true;
    }
    const bool overSpeaker = e.x >= getWidth() - 28;
    if (speakerHovered != overSpeaker)
    {
        speakerHovered = overSpeaker;
        repaint();
    }
    setMouseCursor (overSpeaker ? juce::MouseCursor::PointingHandCursor : ui::zoomCursor());
}

void PianoRollOverview::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    speakerHovered = false;
    repaint();
    setMouseCursor (dragging ? ui::zoomCursor() : juce::MouseCursor::NormalCursor);
}

void PianoRollOverview::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const double deltaY = e.position.y - startPosition.y;
    if (onZoomAt)
        onZoomAt (zoomStart * std::exp (deltaY * 0.012), anchorBeat, anchorScreenX);

    if (getWidth() > 0)
    {
        const double startBeat = startViewX / zoomStart;
        const double panBeats = (e.position.x - startPosition.x)
            * std::max (0.001, model.clip().length()) / getWidth();
        const int viewX = std::max (0, static_cast<int> (std::lround (
            (startBeat - panBeats) * model.pixelsPerBeat)));
        viewport.setViewPosition (viewX, viewport.getViewPositionY());
    }
}

void PianoRollOverview::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    setMouseCursor (speakerHovered ? juce::MouseCursor::PointingHandCursor
                                   : hovered ? ui::zoomCursor() : juce::MouseCursor::NormalCursor);
}

void PianoRollOverview::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onFit)
        onFit();
}

// ============================================================ VelocityLane

int VelocityLane::velocityAt (float y) const
{
    const float h = static_cast<float> (getHeight() - 6);
    return std::clamp (static_cast<int> (std::lround ((1.0f - (y - 3.0f) / h) * 127.0f)), 1, 127);
}

void VelocityLane::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    const auto clipBox = g.getClipBounds();

    g.setColour (theme::border());
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    for (const auto& n : model.clip().notes())
    {
        const int x = static_cast<int> (n.startBeats * model.pixelsPerBeat);
        if (x < clipBox.getX() - 4 || x > clipBox.getRight() + 4) continue;

        const float h = static_cast<float> (getHeight() - 6) * static_cast<float> (n.velocity) / 127.0f;
        juce::Colour c = model.isSelected (n.id) ? theme::noteSelected() : theme::noteFill();
        if (n.muted) c = theme::textDim();
        g.setColour (c);
        g.fillRect (static_cast<float> (x), static_cast<float> (getHeight()) - 3.0f - h, 3.0f, h);
        g.fillEllipse (static_cast<float> (x) - 2.0f, static_cast<float> (getHeight()) - 3.0f - h - 2.0f, 7.0f, 7.0f);
    }

    g.setColour (theme::textDim());
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("Velocity", 4 + clipBox.getX() * 0, 2, 60, 12, juce::Justification::left);
}

void VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    primaryId = -1;
    editBegun = false;
    originalVel.clear();

    int bestDist = 6;
    for (const auto& n : model.clip().notes())
    {
        const int dist = std::abs (static_cast<int> (n.startBeats * model.pixelsPerBeat) + 1 - e.x);
        if (dist <= bestDist) { bestDist = dist; primaryId = n.id; }
    }
    if (primaryId < 0) return;

    if (model.isSelected (primaryId))
        for (int id : model.selection) { if (auto* n = model.clip().find (id)) originalVel[id] = n->velocity; }
    else
        originalVel[primaryId] = model.clip().find (primaryId)->velocity;

    mouseDrag (e);
}

void VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    if (primaryId < 0 || originalVel.empty()) return;
    if (! editBegun) { model.beginEdit(); editBegun = true; }

    const int delta = velocityAt (e.position.y) - originalVel.at (primaryId);
    for (const auto& [id, v] : originalVel)
        if (auto* n = model.clip().find (id)) n->velocity = std::clamp (v + delta, 1, 127);

    model.notify (false);
}

void VelocityLane::mouseUp (const juce::MouseEvent&)
{
    if (editBegun)
        model.endEdit();
    primaryId = -1;
    editBegun = false;
    originalVel.clear();
}

// ============================================================ PianoRollEditor

PianoRollEditor::PianoRollEditor (RollModel& m) : model (m)
{
    addAndMakeVisible (viewport);
    addAndMakeVisible (keyHolder);
    addAndMakeVisible (rulerHolder);
    addAndMakeVisible (overview);
    addAndMakeVisible (velocityHolder);
    keyHolder.addAndMakeVisible (keys);
    rulerHolder.addAndMakeVisible (ruler);
    velocityHolder.addAndMakeVisible (velocity);

    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, true);
    viewport.onScroll = [this]
    {
        syncPositions();
        overview.repaint();
        const int centerRow = (viewport.getViewPositionY() + viewport.getMaximumVisibleHeight() / 2) / model.rowHeight;
        const int p = model.rowToPitch (centerRow);
        if (p >= 0) lastCenterPitch = p;
    };
    ruler.onSeek = [this] (double b) { if (onSeek) onSeek (b); };
    ruler.onLocatorSelected = [this] (int id)
    {
        if (onLocatorSelected)
            onLocatorSelected (id);
    };
    ruler.onLocatorDeleteRequested = [this] (int id)
    {
        if (onLocatorDeleteRequested)
            onLocatorDeleteRequested (id);
    };
    ruler.onLocatorMoveStarted = [this] (int id)
    {
        if (onLocatorMoveStarted)
            onLocatorMoveStarted (id);
    };
    ruler.onLocatorMoved = [this] (int id, double beat)
    {
        if (onLocatorMoved)
            onLocatorMoved (id, beat);
    };
    ruler.onLocatorMoveFinished = [this]
    {
        if (onLocatorMoveFinished)
            onLocatorMoveFinished();
    };
    ruler.onDrag = [this] (float dx, float dy)
    {
        zoomHorizontal (std::exp (-static_cast<double> (dy) * 0.02));
        viewport.setViewPosition (std::max (0, viewport.getViewPositionX() - static_cast<int> (dx)), viewport.getViewPositionY());
    };
    ruler.onZoom = [this] (double factor) { zoomHorizontal (factor); };
    grid.onZoomHorizontal = [this] (double factor) { zoomHorizontal (factor); };
    grid.onFocusChange = [this] { repaint(); };
    overview.onTransportToggle = [this] { if (onTransportToggle) onTransportToggle(); };
    overview.isTransportPlaying = [this]
    {
        return isTransportPlaying && isTransportPlaying();
    };
    overview.onZoomAt = [this] (double requested, double anchorBeat, int anchorScreenX)
    {
        setHorizontalZoomAt (requested, anchorBeat, anchorScreenX);
    };
    overview.onFit = [this]
    {
        fitClipToView();
    };
    keys.onZoomStart = [this] (int screenY)
    {
        noteRulerStartRowHeight = model.rowHeight;
        noteRulerStartViewY = viewport.getViewPositionY();
        noteRulerAnchorScreenY = screenY - viewport.localPointToGlobal (juce::Point<int> { 0, 0 }).y;
        if (! model.rows().empty())
        {
            const int row = std::clamp ((noteRulerStartViewY + noteRulerAnchorScreenY)
                                        / noteRulerStartRowHeight,
                                        0, static_cast<int> (model.rows().size()) - 1);
            noteRulerAnchorPitch = model.rowToPitch (row);
        }
    };
    keys.onZoomDrag = [this] (float dx, float dy, int)
    {
        if (model.rows().empty())
            return;

        const int rowHeight = std::clamp (
            noteRulerStartRowHeight + static_cast<int> (std::lround (dx * 0.12f)), 8, 32);
        const int anchorRow = model.pitchToRow (noteRulerAnchorPitch);
        const int targetViewY = anchorRow >= 0
            ? anchorRow * rowHeight - noteRulerAnchorScreenY - static_cast<int> (std::lround (dy))
            : noteRulerStartViewY - static_cast<int> (std::lround (dy));
        const bool changed = model.rowHeight != rowHeight;

        if (changed)
        {
            model.rowHeight = rowHeight;
            grid.updateSize();
        }
        viewport.setViewPosition (viewport.getViewPositionX(), std::max (0, targetViewY));
        syncPositions();
        keys.repaint();
        grid.repaint();
        if (changed)
            model.sendChangeMessage();
    };

    grid.updateSize();
    model.addChangeListener (this);
}

PianoRollEditor::~PianoRollEditor()
{
    model.removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void PianoRollEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    if (grid.hasKeyboardFocus (false))               // anel de foco (navegacao por teclado)
    {
        g.setColour (theme::accent());
        g.drawRect (getLocalBounds(), 2);
    }
}

void PianoRollEditor::zoomHorizontal (double factor)
{
    fitToClip = false;
    model.pixelsPerBeat = constrainedHorizontalZoom (model.pixelsPerBeat * factor);
    model.sendChangeMessage();
}

void PianoRollEditor::fitClipToView()
{
    fitToClip = true;
    const double length = std::max (0.001, model.clip().length());
    const int visibleWidth = std::max (1, viewport.getMaximumVisibleWidth());
    model.pixelsPerBeat = constrainedHorizontalZoom (visibleWidth / length);
    grid.updateSize();
    viewport.setViewPosition (0, viewport.getViewPositionY());
    syncPositions();
    grid.repaint();
    ruler.repaint();
    velocity.repaint();
    overview.repaint();
}

void PianoRollEditor::setHorizontalZoomAt (double requested, double anchorBeat, int anchorScreenX)
{
    fitToClip = false;
    const double nextZoom = constrainedHorizontalZoom (requested);
    if (std::abs (nextZoom - model.pixelsPerBeat) < 1.0e-9)
        return;

    model.pixelsPerBeat = nextZoom;
    grid.updateSize();
    const int viewX = std::max (0, static_cast<int> (std::lround (anchorBeat * nextZoom - anchorScreenX)));
    viewport.setViewPosition (viewX, viewport.getViewPositionY());
    syncPositions();
    grid.repaint();
    ruler.repaint();
    velocity.repaint();
    model.sendChangeMessage();
}

double PianoRollEditor::constrainedHorizontalZoom (double requested) const
{
    const double clipLength = std::max (0.001, model.clip().length());
    const double viewportWidth = std::max (1, viewport.getMaximumVisibleWidth());
    const double fitZoom = viewportWidth / clipLength;
    const double maximum = std::max (320.0, fitZoom);
    const double minimum = std::min (12.0, fitZoom);
    return std::clamp (requested, minimum, maximum);
}

void PianoRollEditor::zoomVertical (int deltaPixels)
{
    model.rowHeight = std::clamp (model.rowHeight + deltaPixels, 8, 32);
    model.sendChangeMessage();
}

void PianoRollEditor::resized()
{
    auto area = getLocalBounds();
    auto overviewArea = area.removeFromTop (overviewHeight);
    overview.setBounds (overviewArea.withLeft (keyWidth));
    auto rulerArea = area.removeFromTop (rulerHeight);
    rulerHolder.setBounds (rulerArea.withLeft (keyWidth));

    auto bottom = area.removeFromBottom (velocityHeight);
    bottom.removeFromLeft (keyWidth);
    velocityHolder.setBounds (bottom);

    auto left = area.removeFromLeft (keyWidth);
    viewport.setBounds (area);
    keyHolder.setBounds (left.withY (overviewHeight + rulerHeight)
                             .withHeight (viewport.getMaximumVisibleHeight()));

    model.pixelsPerBeat = fitToClip
        ? constrainedHorizontalZoom (static_cast<double> (std::max (1, viewport.getMaximumVisibleWidth()))
                                     / std::max (0.001, model.clip().length()))
        : constrainedHorizontalZoom (model.pixelsPerBeat);
    grid.updateSize();
    if (fitToClip)
        viewport.setViewPosition (0, viewport.getViewPositionY());
    syncPositions();

    if (! scrolledOnce && getHeight() > 0)
    {
        scrolledOnce = true;
        scrollToPitch (60);
    }
}

void PianoRollEditor::syncPositions()
{
    const auto vp = viewport.getViewPosition();
    keyHolder.setSize (keyWidth, viewport.getMaximumVisibleHeight());
    keys.setBounds (0, -vp.y, keyWidth, std::max (1, model.totalHeight()));
    ruler.setBounds (-vp.x, 0, std::max (1, static_cast<int> (model.gridWidthPixels()) + 1), rulerHeight);
    velocity.setBounds (-vp.x, 0, std::max (1, static_cast<int> (model.gridWidthPixels()) + 1), velocityHeight);
    overview.repaint();
}

void PianoRollEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    model.pixelsPerBeat = fitToClip
        ? constrainedHorizontalZoom (static_cast<double> (std::max (1, viewport.getMaximumVisibleWidth()))
                                     / std::max (0.001, model.clip().length()))
        : constrainedHorizontalZoom (model.pixelsPerBeat);
    if (fitToClip)
        viewport.setViewPosition (0, viewport.getViewPositionY());
    const int keepPitch = lastCenterPitch;
    const int oldHeight = grid.getHeight();
    grid.updateSize();
    if (grid.getHeight() != oldHeight)                     // fold/escala mudou as linhas
    {
        int best = -1, bestDist = 1000;
        for (int r = 0; r < static_cast<int> (model.rows().size()); ++r)
        {
            const int d = std::abs (model.rowToPitch (r) - keepPitch);
            if (d < bestDist) { bestDist = d; best = r; }
        }
        if (best >= 0)
            viewport.setViewPosition (viewport.getViewPositionX(),
                                      std::max (0, best * model.rowHeight - viewport.getMaximumVisibleHeight() / 2));
        lastCenterPitch = keepPitch;
    }
    syncPositions();
    grid.repaint();
    keys.repaint();
    ruler.repaint();
    velocity.repaint();
    overview.repaint();
}

void PianoRollEditor::setPlayhead (double beats)
{
    grid.setPlayhead (beats);
    overview.repaint();
}

void PianoRollEditor::setLocators (std::vector<std::pair<int, double>> locators, int selectedId)
{
    ruler.setLocators (std::move (locators), selectedId);
}

void PianoRollEditor::scrollToPitch (int pitch)
{
    const int row = model.pitchToRow (pitch);
    if (row < 0) return;
    const int y = std::max (0, row * model.rowHeight - viewport.getMaximumVisibleHeight() / 2);
    viewport.setViewPosition (viewport.getViewPositionX(), y);
}
