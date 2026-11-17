#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RollModel.h"
#include "Theme.h"
#include <functional>
#include <map>
#include <utility>

// Grade de notas: desenha o clip e trata mouse/teclado de edicao.
class NoteGrid : public juce::Component
{
public:
    explicit NoteGrid (RollModel& m) : model (m) { setWantsKeyboardFocus (true); }

    std::function<void()> onFocusChange;
    std::function<void (double)> onZoomHorizontal;
    void focusGained (FocusChangeType) override { if (onFocusChange) onFocusChange(); }
    void focusLost (FocusChangeType) override   { if (onFocusChange) onFocusChange(); }

    void updateSize();
    void setPlayhead (double beats);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { None, Move, Resize, RubberPending, Rubber };

    float beatsToX (double b) const  { return static_cast<float> (b * model.pixelsPerBeat); }
    double xToBeats (float x) const  { return x / model.pixelsPerBeat; }
    int   yToRow (float y) const     { return static_cast<int> (std::floor (y / model.rowHeight)); }
    juce::Rectangle<float> noteRect (const roll::Note&) const;
    int  hitNote (juce::Point<float>) const;
    void showContextMenu (juce::Point<int> screenPosition);
    void updateCursor (juce::Point<float>);
    void setPreview (int pitch);
    void flashPreview (int pitch);

    RollModel& model;
    double playheadBeats = 0.0;

    Drag   drag = Drag::None;
    bool   editBegun = false;
    bool   copied = false;                    // Ctrl+arrastar: copias ja deixadas para tras
    int    primaryId = -1;
    double downBeat = 0.0;
    std::map<int, roll::Note> original;       // notas selecionadas no inicio do gesto
    juce::Point<float> rubberStart;
    juce::Rectangle<float> rubberRect;
    std::set<int> baseSelection;
    int pendingRubberNoteId = -1;
    int previewPitch = -1;
};

// Coluna de teclas a esquerda (clique para pre-escutar).
class KeyColumn : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KeyColumn (RollModel& m) : model (m)
    {
        setTooltip ("Na regua de notas: arraste verticalmente para rolar e horizontalmente para ajustar a altura das notas.");
    }

    std::function<void (int screenY)> onZoomStart;
    std::function<void (float dx, float dy, int screenY)> onZoomDrag;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setPitch (int pitch);
    bool isZoomRegion (juce::Point<float>) const;
    static constexpr int zoomStripWidth = 8;
    RollModel& model;
    int current = -1;
    juce::Point<int> lastZoomScreenPosition;
    bool zoomDragging = false, zoomHovered = false;
    juce::Point<int> zoomStartScreenPosition;
};

class PianoRollEditor;
class SyncViewport;

// Regua com compassos/beats e a faixa do clip. Clique reposiciona o playhead.
class RulerBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RulerBar (RollModel& m) : model (m)
    {
        setTooltip ("Clique para reproduzir a partir deste ponto.");
    }
    std::function<void (double)> onSeek;
    std::function<void (int)> onLocatorSelected;
    std::function<void (int)> onLocatorDeleteRequested;
    std::function<void (int)> onLocatorMoveStarted;
    std::function<void (int, double)> onLocatorMoved;
    std::function<void()> onLocatorMoveFinished;
    std::function<void (float dx, float dy)> onDrag;    // arrastar: vertical = zoom, horizontal = rolar
    std::function<void (double)> onZoom;
    void setLocators (std::vector<std::pair<int, double>>, int selectedId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void seek (const juce::MouseEvent&);
    bool isResizeHandle (juce::Point<float>) const;
    int locatorAt (float x) const;
    RollModel& model;
    juce::Point<float> last;
    std::vector<std::pair<int, double>> locators;
    int selectedLocatorId = -1;
    float hoveredX = 0.0f;
    bool hovered = false;
    bool dragging = false, resizing = false;
    bool locatorDragPending = false, locatorDragging = false;
    int draggedLocatorId = -1;
    double locatorGrabOffset = 0.0;
};

class PianoRollOverview : public juce::Component, public juce::SettableTooltipClient
{
public:
    PianoRollOverview (RollModel& m, SyncViewport& v) : model (m), viewport (v)
    {
        setTooltip ("Arraste para navegar e ajustar o zoom. Duplo clique para enquadrar o clip.");
    }

    std::function<void (double, double, int)> onZoomAt;
    std::function<void()> onFit;
    std::function<void()> onTransportToggle;
    std::function<bool()> isTransportPlaying;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> visibleRangeBounds() const;
    RollModel& model;
    SyncViewport& viewport;
    int startViewX = 0, anchorScreenX = 0;
    double zoomStart = 0.0, anchorBeat = 0.0;
    juce::Point<float> startPosition;
    bool dragging = false, hovered = false, speakerHovered = false;
};

// Faixa de velocity: uma barra por nota; arraste para mudar.
class VelocityLane : public juce::Component
{
public:
    explicit VelocityLane (RollModel& m) : model (m) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    int velocityAt (float y) const;
    RollModel& model;
    int primaryId = -1;
    bool editBegun = false;
    std::map<int, int> originalVel;
};

class SyncViewport : public juce::Viewport
{
public:
    std::function<void()> onScroll;
    void visibleAreaChanged (const juce::Rectangle<int>& a) override
    {
        juce::Viewport::visibleAreaChanged (a);
        if (onScroll) onScroll();
    }
};

// Piano roll completo: regua + teclas + grade rolavel + velocity.
class PianoRollEditor : public juce::Component, private juce::ChangeListener
{
public:
    explicit PianoRollEditor (RollModel&);
    ~PianoRollEditor() override;

    void setPlayhead (double beats);
    void scrollToPitch (int pitch);
    void zoomHorizontal (double factor);
    void zoomVertical (int deltaPixels);
    void fitClipToView();
    void setLocators (std::vector<std::pair<int, double>>, int selectedId);
    void grabFocus() { grid.grabKeyboardFocus(); }
    void repaintKeys() { keys.repaint(); }
    std::function<void (double)> onSeek;
    std::function<void (int)> onLocatorSelected;
    std::function<void (int)> onLocatorDeleteRequested;
    std::function<void (int)> onLocatorMoveStarted;
    std::function<void (int, double)> onLocatorMoved;
    std::function<void()> onLocatorMoveFinished;
    std::function<void()> onTransportToggle;
    std::function<bool()> isTransportPlaying;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Holder : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override { g.fillAll (theme::panel()); }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncPositions();
    double constrainedHorizontalZoom (double requested) const;
    void setHorizontalZoomAt (double requested, double anchorBeat, int anchorScreenX);

    static constexpr int keyWidth = 58, overviewHeight = 22, rulerHeight = 24, velocityHeight = 72;

    RollModel& model;
    NoteGrid grid { model };
    KeyColumn keys { model };
    RulerBar ruler { model };
    SyncViewport viewport;
    PianoRollOverview overview { model, viewport };
    VelocityLane velocity { model };
    Holder keyHolder, rulerHolder, velocityHolder;
    bool scrolledOnce = false;
    bool fitToClip = true;
    int  lastCenterPitch = 60;      // mantem a posicao vertical ao ligar/desligar o fold
    int noteRulerStartRowHeight = 16, noteRulerStartViewY = 0;
    int noteRulerAnchorPitch = 60, noteRulerAnchorScreenY = 0;
};
