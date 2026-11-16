#include "MainComponent.h"
#include "WindowPlacement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace
{
double sourceBpmFromFileName (const juce::File& file, double fallback)
{
    const auto name = file.getFileNameWithoutExtension().toLowerCase();
    const int marker = name.indexOf ("bpm");
    if (marker <= 0)
        return fallback;

    int numberEnd = marker;
    while (numberEnd > 0 && (name[numberEnd - 1] == ' ' || name[numberEnd - 1] == '_' || name[numberEnd - 1] == '-'))
        --numberEnd;
    int start = numberEnd;
    while (start > 0)
    {
        const auto character = name[start - 1];
        if (character < '0' || character > '9')
            break;
        --start;
    }

    const double bpm = name.substring (start, numberEnd).getDoubleValue();
    return bpm >= 20.0 && bpm <= 300.0 ? bpm : fallback;
}

bool isProjectFile (const juce::File& file)
{
    const auto extension = file.getFileExtension();
    return extension.equalsIgnoreCase (".pfs") || extension.equalsIgnoreCase (".freedom");
}

// A fileira central começa em C3; a fileira superior preenche as teclas pretas.
constexpr std::array<std::pair<int, int>, 15> computerMidiKeyboardMap {{
    { 'A', 0 }, { 'W', 1 }, { 'S', 2 }, { 'E', 3 }, { 'D', 4 },
    { 'F', 5 }, { 'T', 6 }, { 'G', 7 }, { 'Y', 8 }, { 'H', 9 },
    { 'U', 10 }, { 'J', 11 }, { 'K', 12 }, { 'O', 13 }, { 'L', 14 }
}};

bool isTextEditorFocused()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr
        && (dynamic_cast<juce::TextEditor*> (focused) != nullptr
            || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr);
}

juce::String midiNoteName (int pitch)
{
    static constexpr std::array<const char*, 12> names {{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    }};
    return names[static_cast<std::size_t> (pitch % 12)] + juce::String (pitch / 12 - 2);
}

}

class MainComponent::ActionIndicator final : public juce::Component, private juce::Timer
{
public:
    ActionIndicator()
    {
        message.setColour (juce::Label::textColourId, theme::text());
        message.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        message.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (message);
        setInterceptsMouseClicks (false, false);
    }

    void showMessage (const juce::String& text, icons::Icon icon, bool busy)
    {
        message.setText (text, juce::dontSendNotification);
        const bool wasVisible = isVisible();
        if (busy != loading)
        {
            loading = busy;
            if (loading)
                startTimerHz (30);
            else
                stopTimer();
        }
        currentIcon = icon;
        setVisible (true);
        if (! wasVisible)
            toFront (false);
        repaint();
    }

    void hide()
    {
        if (loading)
        {
            loading = false;
            stopTimer();
        }
        setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel().withAlpha (0.97f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
        if (loading)
        {
            drawLoadingCube (g, juce::Rectangle<float> (4.0f, 4.0f, 44.0f, 44.0f));
        }
        else
            if (const auto* icon = icons::get (currentIcon, theme::accent(), 24))
                icon->drawWithin (g, juce::Rectangle<float> (12.0f, 12.0f, 24.0f, 24.0f),
                                  juce::RectanglePlacement::centred, 1.0f);
    }

    void resized() override
    {
        message.setBounds (52, 0, getWidth() - 60, getHeight());
    }

private:
    struct Point3D { float x, y, z; };

    void timerCallback() override
    {
        angle = std::fmod (angle + 0.075f, juce::MathConstants<float>::twoPi);
        repaint();
    }

    void drawLoadingCube (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        constexpr std::array<Point3D, 8> vertices
        {{
            { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
            { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
        }};
        constexpr std::array<std::array<int, 4>, 6> faces
        {{
            {{ 0, 3, 2, 1 }}, {{ 4, 5, 6, 7 }}, {{ 0, 4, 7, 3 }},
            {{ 1, 2, 6, 5 }}, {{ 0, 1, 5, 4 }}, {{ 3, 7, 6, 2 }}
        }};
        constexpr std::array<Point3D, 6> normals
        {{
            { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 },
            { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }
        }};
        const float cosineY = std::cos (angle);
        const float sineY = std::sin (angle);
        const float cosineX = std::cos (angle * 0.72f + 0.35f);
        const float sineX = std::sin (angle * 0.72f + 0.35f);
        std::array<juce::Point<float>, 8> projected;
        std::array<float, 8> depth;

        const auto rotate = [=] (Point3D point)
        {
            const float x = point.x * cosineY + point.z * sineY;
            const float z = -point.x * sineY + point.z * cosineY;
            return Point3D { x, point.y * cosineX - z * sineX,
                             point.y * sineX + z * cosineX };
        };
        const float scale = std::min (bounds.getWidth(), bounds.getHeight()) * 0.36f;
        const auto centre = bounds.getCentre();
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            const auto point = rotate (vertices[i]);
            depth[i] = point.z;
            const float perspective = 1.0f / (1.0f - point.z * 0.12f);
            projected[i] = { centre.x + point.x * scale * perspective,
                             centre.y + point.y * scale * perspective };
        }

        std::array<int, 6> faceOrder { 0, 1, 2, 3, 4, 5 };
        const auto averageDepth = [&] (int face)
        {
            float value = 0.0f;
            for (const int vertex : faces[static_cast<std::size_t> (face)])
                value += depth[static_cast<std::size_t> (vertex)];
            return value * 0.25f;
        };
        std::sort (faceOrder.begin(), faceOrder.end(),
                   [&] (int first, int second) { return averageDepth (first) < averageDepth (second); });

        for (const int faceIndex : faceOrder)
        {
            const auto index = static_cast<std::size_t> (faceIndex);
            juce::Path face;
            for (int corner = 0; corner < 4; ++corner)
            {
                const auto point = projected[static_cast<std::size_t> (faces[index][static_cast<std::size_t> (corner)])];
                if (corner == 0) face.startNewSubPath (point);
                else face.lineTo (point);
            }
            face.closeSubPath();

            const auto normal = rotate (normals[index]);
            const float lighting = std::max (0.0f, -0.45f * normal.x - 0.55f * normal.y + 0.70f * normal.z);
            const auto colour = theme::accent().withMultipliedBrightness (0.48f + lighting * 0.52f);
            g.setColour (colour);
            g.fillPath (face);
            g.setColour (theme::accent().brighter (0.25f).withAlpha (0.85f));
            g.strokePath (face, juce::PathStrokeType (0.8f));
        }
    }

    icons::Icon currentIcon = icons::Icon::Pointer;
    float angle = 0.0f;
    bool loading = false;
    juce::Label message;
};

class MainComponent::ComputerMidiIndicator final : public juce::Component, private juce::Timer
{
public:
    explicit ComputerMidiIndicator (bool alignToParentTopRight = false)
        : alignToParentTopRight (alignToParentTopRight)
    {
        setInterceptsMouseClicks (false, false);
    }

    void showStatus (const juce::String& noteText, const juce::String& detailText, bool noteHeld)
    {
        const bool wasVisible = isVisible();
        const bool changed = primaryText != noteText || secondaryText != detailText || ! wasVisible;
        primaryText = noteText;
        secondaryText = detailText;
        held = noteHeld;
        setVisible (true);
        if (! wasVisible)
            toFront (false);

        if (held)
            stopTimer();
        else if (changed)
            startTimer (1100);

        if (changed)
            repaint();
    }

    void hide()
    {
        stopTimer();
        held = false;
        setVisible (false);
    }

    void alignWithParent() { parentSizeChanged(); }
    const juce::String& getPrimaryText() const { return primaryText; }
    const juce::String& getSecondaryText() const { return secondaryText; }
    bool isHeld() const { return held; }

    void parentSizeChanged() override
    {
        if (! alignToParentTopRight)
            return;

        if (auto* parent = getParentComponent())
        {
            const int width = std::min (224, std::max (0, parent->getWidth() - 16));
            setBounds (std::max (8, parent->getWidth() - width - 8), 8, width, 44);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel().withAlpha (0.97f));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (theme::accent());
        g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.2f);

        auto textBounds = getLocalBounds().reduced (12, 4);
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (primaryText, textBounds.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (secondaryText, textBounds, juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (! held)
            setVisible (false);
    }

    juce::String primaryText, secondaryText;
    bool held = false;
    bool alignToParentTopRight = false;
};

// Janela de plugin: mantém o editor visível e oferece rolagem como fallback.
class MainComponent::PluginWindow : public juce::DocumentWindow, private juce::KeyListener
{
public:
    PluginWindow (const juce::String& name,
                  std::unique_ptr<juce::AudioProcessorEditor> editor,
                  const juce::Rectangle<int>& displayUserArea,
                  int cascadeIndex,
                  const std::vector<roll::Rect>& occupiedWindows,
                  std::function<void()> onActiveWindowStatusChanged,
                  std::function<bool (const juce::KeyPress&)> onTransportKeyPress)
        : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
        , onActiveWindowStatusChanged (std::move (onActiveWindowStatusChanged))
        , onTransportKeyPress (std::move (onTransportKeyPress))
    {
        setUsingNativeTitleBar (true);
        editorComponent = editor.get();
        addTransportKeyListenerRecursively (*editorComponent);
        addKeyListener (this);
        const bool editorResizable = editor->isResizable();

        setResizable (true, false);
        const int horizontalMargin = std::min (16, std::max (0, (displayUserArea.getWidth() - 1) / 2));
        const int verticalMargin = std::min (60, std::max (0, (displayUserArea.getHeight() - 1) / 2));
        const auto available = displayUserArea.reduced (horizontalMargin, verticalMargin);

        const bool needsViewport = ! editorResizable
                                || editor->getWidth() > available.getWidth()
                                || editor->getHeight() > available.getHeight();
        if (needsViewport)
        {
            auto viewport = std::make_unique<juce::Viewport>();
            viewport->setScrollBarsShown (true, true);
            viewport->setSize (std::min (editor->getWidth(), available.getWidth()),
                               std::min (editor->getHeight(), available.getHeight()));
            viewport->setViewedComponent (editor.release(), true);
            setContentOwned (viewport.release(), true);
        }
        else
        {
            setContentOwned (editor.release(), true);
        }

        computerMidiIndicator = std::make_unique<ComputerMidiIndicator> (true);
        addAndMakeVisible (computerMidiIndicator.get());
        computerMidiIndicator->setVisible (false);
        computerMidiIndicator->alignWithParent();

        const auto placeWindow = [this, &displayUserArea, cascadeIndex, &occupiedWindows]
        {
            return roll::WindowPlacement::placePluginWindow (
                { 0, 0, getWidth(), getHeight() },
                { displayUserArea.getX(), displayUserArea.getY(), displayUserArea.getWidth(), displayUserArea.getHeight() },
                cascadeIndex, 64, occupiedWindows);
        };
        auto bounds = placeWindow();
        setBounds (bounds.x, bounds.y, bounds.width, bounds.height);
        setVisible (true);
        bounds = placeWindow();
        setBounds (bounds.x, bounds.y, bounds.width, bounds.height);
    }

    ~PluginWindow() override
    {
        if (editorComponent != nullptr)
            removeTransportKeyListenerRecursively (*editorComponent);
        removeKeyListener (this);
    }

    void showComputerMidiStatus (const juce::String& primary,
                                const juce::String& secondary,
                                bool noteHeld)
    {
        computerMidiIndicator->showStatus (primary, secondary, noteHeld);
    }

    void hideComputerMidiStatus() { computerMidiIndicator->hide(); }

    void closeButtonPressed() override { setVisible (false); }

    void activeWindowStatusChanged() override
    {
        juce::DocumentWindow::activeWindowStatusChanged();
        if (onActiveWindowStatusChanged)
            onActiveWindowStatusChanged();
    }

private:
    void addTransportKeyListenerRecursively (juce::Component& component)
    {
        component.addKeyListener (this);
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i))
                addTransportKeyListenerRecursively (*child);
    }

    void removeTransportKeyListenerRecursively (juce::Component& component)
    {
        component.removeKeyListener (this);
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i))
                removeTransportKeyListenerRecursively (*child);
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        return onTransportKeyPress && onTransportKeyPress (key);
    }

    juce::AudioProcessorEditor* editorComponent = nullptr;
    std::function<void()> onActiveWindowStatusChanged;
    std::function<bool (const juce::KeyPress&)> onTransportKeyPress;
    std::unique_ptr<ComputerMidiIndicator> computerMidiIndicator;
};

MainComponent::MainComponent()
{
    addAndMakeVisible (taskStatusBar);
    addAndMakeVisible (transport);
    transport.onProjectMenu = [this] { showProjectMenu(); };
    addAndMakeVisible (browser);
    addAndMakeVisible (audioBrowser);
    audioBrowser.setVisible (false);
    addAndMakeVisible (arrangement);
    addAndMakeVisible (editor);
    addAndMakeVisible (audioClipEditor);
    audioClipEditor.setVisible (false);
    addAndMakeVisible (deviceView);
    addAndMakeVisible (clipTab);
    addAndMakeVisible (deviceTab);
    actionIndicator = std::make_unique<ActionIndicator>();
    addAndMakeVisible (actionIndicator.get());
    computerMidiIndicator = std::make_unique<ComputerMidiIndicator>();
    addAndMakeVisible (computerMidiIndicator.get());
    computerMidiIndicator->setVisible (false);

    clipControls = { &scaleCaption, &lengthCaption, &gridCaption, &zoomCaption, &rootBox, &modeBox, &lengthBox, &scaleButton,
                     &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton,
                     &zoomHMinus, &zoomHPlus, &zoomVMinus, &zoomVPlus, &gridLabel, &editor };
    for (auto* c : clipControls) addAndMakeVisible (c);

    for (auto* l : { &scaleCaption, &lengthCaption, &gridCaption, &zoomCaption })
    {
        l->setColour (juce::Label::textColourId, theme::textDim());
        l->setFont (juce::FontOptions (12.5f));
    }
    gridLabel.setColour (juce::Label::textColourId, theme::text());

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) rootBox.addItem (names[i], i + 1);
    rootBox.setSelectedId (1, juce::dontSendNotification);
    for (int m = 0; m <= static_cast<int> (roll::Mode::MelodicMinor); ++m)
        modeBox.addItem (juce::String (roll::Scale::nameOf (static_cast<roll::Mode> (m)).data()), m + 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    for (int bars : { 1, 2, 4, 8, 16, 32 }) lengthBox.addItem (juce::String (bars) + (bars == 1 ? " compasso" : " compassos"), bars);

    for (auto* b : { &scaleButton, &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
    for (auto* b : { &clipTab, &deviceTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4801);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
    clipTab.setToggleState (true, juce::dontSendNotification);
    clipTab.onClick = [this] { audioClipEditorOpen = false; showDetail (false); };
    deviceTab.onClick = [this] { showDetail (true); };

    snapButton.setToggleState (true, juce::dontSendNotification);
    highlightButton.setToggleState (true, juce::dontSendNotification);
    previewButton.setToggleState (true, juce::dontSendNotification);
    audioClipEditor.onClose = [this]
    {
        audioClipEditorOpen = false;
        showDetail (false);
        resized();
    };
    audioClipEditor.onSourceBpmChanged = [this] (double bpm)
    {
        return project.setAudioClipSourceBpm (project.selectedTrackId, project.selectedAudioClipId, bpm);
    };
    audioClipEditor.onWarpChanged = [this] (bool enabled)
    {
        project.setAudioClipWarpEnabled (project.selectedTrackId, project.selectedAudioClipId, enabled);
    };

    // botões não roubam o foco do teclado (Espaço continua sendo play/pause)
    for (auto* b : { &scaleButton, &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton,
                     &zoomHMinus, &zoomHPlus, &zoomVMinus, &zoomVPlus, &clipTab, &deviceTab })
        b->setMouseClickGrabsKeyboardFocus (false);
    rootBox.setMouseClickGrabsKeyboardFocus (false);
    modeBox.setMouseClickGrabsKeyboardFocus (false);
    lengthBox.setMouseClickGrabsKeyboardFocus (false);

    rootBox.onChange = [this] { applyScale(); };
    modeBox.onChange = [this] { applyScale(); };
    lengthBox.onChange = [this] { applyLength(); };
    scaleButton.onClick = [this] { model.scaleOn = scaleButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    highlightButton.onClick = [this] { model.highlight = highlightButton.getToggleState(); model.sendChangeMessage(); };
    foldButton.onClick = [this] { model.fold = foldButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    foldScaleButton.onClick = [this] { model.foldScale = foldScaleButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    drawButton.onClick = [this] { model.drawMode = drawButton.getToggleState(); };
    snapButton.onClick = [this] { model.snap = snapButton.getToggleState(); model.sendChangeMessage(); };
    previewButton.onClick = [this] { model.previewOn = previewButton.getToggleState(); };
    zoomHMinus.onClick = [this] { editor.zoomHorizontal (1.0 / 1.25); };
    zoomHPlus.onClick  = [this] { editor.zoomHorizontal (1.25); };
    zoomVMinus.onClick = [this] { editor.zoomVertical (-2); };
    zoomVPlus.onClick  = [this] { editor.zoomVertical (2); };

    // ---- projeto <-> engine ----
    project.onTrackAdded = [this] (int id)
    {
        const auto* track = project.track (id);
        const auto type = track != nullptr ? track->type : roll::TrackType::Midi;
        engine.addTrack (id, type);
        if (type == roll::TrackType::Midi)
            engine.publishNotes (id, {});
        applyMixers();
    };
    project.onTrackRemoved = [this] (int id)
    {
        closeWindowsOfTrack (id);
        engine.removeTrack (id);
        publishAudioTrack (id);
    };
    project.onTrackNotes = [this] (int id)
    {
        if (auto* t = project.track (id); t != nullptr && t->type == roll::TrackType::Midi)
            engine.publishNotes (id, roll::flatten (*t));
    };
    project.onAudioClipsChanged = [this] (int id) { publishAudioTrack (id); };
    project.onMixerChanged = [this] { applyMixers(); };
    project.onSelectionChanged = [this] { onSelectionChanged(); };
    project.onLoopChanged = [this] { engine.graph().setLoopRegion (project.loopStart, project.loopEnd); };
    project.addChangeListener (this);
    model.addChangeListener (this);

    // ---- piano roll <-> projeto ----
    model.onNotesChanged = [this] { project.notifyTrackNotes (project.selectedTrackId); project.sendChangeMessage(); };
    model.onLengthChanged = [this]
    {
        project.notifyTrackNotes (project.selectedTrackId);
        project.sendChangeMessage();
    };
    model.onPreview = [this] (int pitch, bool on)
    {
        if (! on)                 engine.previewNote (project.selectedTrackId, pitch, false);
        else if (model.previewOn) engine.previewNote (project.selectedTrackId, pitch, true);
    };
    model.onInsertMarker = [this] (double local)
    {
        if (! engine.graph().isPlaying())
        {
            const double g = model.clipStartBeats + local;
            transportPosition = g;
            engine.graph().seek (g);
            arrangement.setPlayhead (g);
            editor.setPlayhead (local);
        }
    };
    editor.onSeek = [this] (double local)
    {
        const double g = model.clipStartBeats + local;
        transportPosition = g;
        engine.graph().seek (g);
        arrangement.setPlayhead (g);
        editor.setPlayhead (local);
    };

    arrangement.onSeek = [this] (double g)
    {
        pendingArrangementSeek = true;
        pendingArrangementSeekBeat = g;
        transportPosition = g;
        engine.graph().seek (g);
        arrangement.setPlayhead (g);
        editor.setPlayhead (g - model.clipStartBeats);
    };
    arrangement.onArrangementSeekRequested = [this] (double beats)
    {
        pendingArrangementSeek = true;
        pendingArrangementSeekBeat = beats;
    };
    arrangement.onLocatorLaunchRequested = [this] (double beats)
    {
        if (engine.graph().isPlaying())
            return;
        pendingArrangementSeek = false;
        if (arrangement.onSeek)
            arrangement.onSeek (beats);
        suppressSongStartOnce = true;
        transport.togglePlay();
    };
    arrangement.meterProvider = [this] (int id) { return engine.peak (id); };
    arrangement.onMasterPeakReset = [this] { engine.resetPeak (AudioEngine::masterTrackId); };
    arrangement.deviceSummary = [this] (int id) { return deviceSummary (id); };
    arrangement.onTrackDuplicateRequested = [this] (int id) { duplicateSelectedTrack (id); };
    arrangement.canAcceptPluginDrop = [this] (const juce::String& identifier, int trackId)
    {
        const auto description = scanner.list().getTypeForIdentifierString (identifier);
        return description != nullptr
            && (trackId != AudioEngine::masterTrackId || engine.canAcceptPlugin (trackId, *description));
    };
    arrangement.onPluginDropped = [this] (const juce::String& identifier, int trackId, double)
    {
        if (auto description = scanner.list().getTypeForIdentifierString (identifier))
        {
            auto* target = project.track (trackId);
            if (trackId == AudioEngine::masterTrackId && ! engine.canAcceptPlugin (trackId, *description))
            {
                browser.setStatus ("A trilha Master aceita apenas efeitos.");
                return;
            }
            if (! engine.canAcceptPlugin (trackId, *description))
            {
                const auto compatible = std::find_if (project.tracks.begin(), project.tracks.end(),
                    [this, &description] (const auto& track)
                    {
                        return engine.canAcceptPlugin (track->id, *description);
                    });
                target = compatible != project.tracks.end() ? compatible->get() : nullptr;
            }
            if (target == nullptr)
                target = project.addTrack (roll::TrackType::Midi);
            project.selectTrack (target->id);
            loadDevice (*description);
        }
    };
    arrangement.canAcceptAudioDrop = [] (const juce::File& file, int trackId)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        return trackId != AudioEngine::masterTrackId && file.existsAsFile() && (extension == ".wav" || extension == ".wave"
            || extension == ".aif" || extension == ".aiff" || extension == ".flac" || extension == ".ogg");
    };
    arrangement.onAudioDropped = [this] (const juce::File& file, int trackId, double beat)
    {
        auto* target = project.track (trackId);
        if (target == nullptr || target->type != roll::TrackType::Audio)
        {
            const auto existing = std::find_if (project.tracks.begin(), project.tracks.end(),
                [] (const auto& candidate) { return candidate->type == roll::TrackType::Audio; });
            target = existing != project.tracks.end() ? existing->get() : project.addTrack (roll::TrackType::Audio);
        }
        loadAudioFile (file, target->id, beat);
    };
    arrangement.onAudioClipDoubleClicked = [this] (int trackId, int clipId)
    {
        openAudioClipEditor (trackId, clipId);
    };
    arrangement.onLocateAudioFileRequested = [this] (const juce::File& file)
    {
        setAudioBrowserVisible (true);
        audioBrowser.revealFile (file);
    };
    arrangement.onConsolidateRequested = [this] (double startBeat, double endBeat, int firstTrack, int lastTrack)
    {
        consolidateAudioRange (startBeat, endBeat, firstTrack, lastTrack);
    };
    audioBrowser.canDeleteFile = [this] (const juce::File& file)
    {
        for (const auto& track : project.tracks)
            for (const auto& clip : track->audioClips)
                if (juce::File (juce::String (clip->filePath)) == file)
                    return false;
        return true;
    };
    arrangement.onGridChanged = [this] { refreshAudioClipEditor(); };
    audioBrowser.onPreviewRequested = [this] (const juce::File& file)
    {
        const int request = ++previewLoadToken;
        ++pendingPreviewLoads;
        updateActionIndicator();
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        engine.loadAudioFileAsync (file, [this, token, file, request]
            (const juce::String& error, std::shared_ptr<const AudioFileData> data)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;
            pendingPreviewLoads = std::max (0, pendingPreviewLoads - 1);
            updateActionIndicator();
            if (request != previewLoadToken)
                return;
            if (error.isNotEmpty() || data == nullptr)
            {
                audioBrowser.setStatus ("Erro no preview: " + error);
                return;
            }
            engine.previewAudio (std::move (data));
            audioBrowser.setStatus ("Tocando: " + file.getFileName());
        });
    };
    audioBrowser.onPreviewStopRequested = [this]
    {
        ++previewLoadToken;
        engine.stopAudioPreview();
    };
    audioBrowser.onPreviewGainChanged = [this] (float gain) { engine.setAudioPreviewGain (gain); };
    transport.onPosition = [this] (double beats)
    {
        transportPosition = beats;
        const double bpm = engine.graph().getBpm();
        if (std::abs (bpm - displayedBpm) > 1.0e-6)
        {
            displayedBpm = bpm;
            refreshAudioClipEditor();
        }
        arrangement.setPlayhead (beats);
        editor.setPlayhead (beats - model.clipStartBeats);
    };
    transport.onBeforePlay = [this]
    {
        double selectionStart = 0.0;
        if (arrangement.getTimeSelectionStart (selectionStart))
        {
            pendingArrangementSeek = false;
            transportPosition = selectionStart;
            engine.graph().seek (selectionStart);
            arrangement.setPlayhead (selectionStart);
            editor.setPlayhead (selectionStart - model.clipStartBeats);
            return;
        }

        if (pendingArrangementSeek)
        {
            const bool stillAtSeek = std::abs (transportPosition - pendingArrangementSeekBeat) < 1.0e-6;
            pendingArrangementSeek = false;
            if (stillAtSeek)
                return;
        }
        if (suppressSongStartOnce)
        {
            suppressSongStartOnce = false;
            return;
        }
        if (engine.graph().positionBeats() > 1.0e-6)
            return;
        if (const auto* marker = project.locator (project.songStartLocatorId))
        {
            transportPosition = marker->beat;
            engine.graph().seek (marker->beat);
            arrangement.setPlayhead (marker->beat);
            editor.setPlayhead (marker->beat - model.clipStartBeats);
        }
    };
    const auto rememberPausedPosition = [this]
    {
        pausedPosition = engine.graph().positionBeats();
    };
    transport.onBeforePause = rememberPausedPosition;
    transport.onBeforeStop = rememberPausedPosition;
    transport.onLoopToggled = [this] (bool on) { arrangement.setLoopEnabled (on); };
    arrangement.onLoopToggleRequested = [this] (bool on) { transport.setLoopEnabled (on); };
    transport.onBrowserToggled = [this] (bool visible) { setBrowserVisible (visible); };
    transport.onFileBrowserToggled = [this] (bool visible) { setAudioBrowserVisible (visible); };
    transport.onFavoritesToggled = [this] (bool visible) { setFavoritesVisible (visible); };
    transport.onPianoRollToggled = [this] (bool visible) { setPianoRollVisible (visible); };
    transport.onComputerMidiKeyboardToggled = [this] (bool enabled)
    {
        setComputerMidiKeyboardEnabled (enabled);
    };

    deviceView.onOpen = [this] (int trackId, int deviceId) { openDeviceEditor (trackId, deviceId); };
    deviceView.onBypass = [this] (int trackId, int deviceId, bool bypassed)
    {
        engine.setDeviceBypassed (trackId, deviceId, bypassed);
    };
    deviceView.onMove = [this] (int trackId, int deviceId, int direction)
    {
        if (engine.moveDevice (trackId, deviceId, direction))
        {
            refreshDeviceView();
            project.sendChangeMessage();
        }
    };
    deviceView.onReorder = [this] (int trackId, int deviceId, int targetDeviceId, bool afterTarget)
    {
        if (engine.reorderDevice (trackId, deviceId, targetDeviceId, afterTarget))
        {
            refreshDeviceView();
            project.sendChangeMessage();
        }
    };
    deviceView.onGroupRequested = [this] (int trackId, const std::vector<int>& deviceIds)
    {
        if (engine.groupDevices (trackId, deviceIds))
        {
            refreshDeviceView();
            project.sendChangeMessage();
            browser.setStatus ("Plugins agrupados. Ctrl+Shift+G para desagrupar.");
        }
        else
        {
            browser.setStatus ("Selecione pelo menos dois plugins consecutivos para agrupar.");
        }
    };
    deviceView.onUngroupRequested = [this] (int trackId, const std::vector<int>& deviceIds)
    {
        if (engine.ungroupDevices (trackId, deviceIds))
        {
            refreshDeviceView();
            project.sendChangeMessage();
            browser.setStatus ("Rack desagrupado.");
        }
        else
        {
            browser.setStatus ("Selecione um rack para desagrupar.");
        }
    };
    deviceView.canAcceptPluginDrop = [this] (int trackId, const juce::String& identifier)
    {
        const auto description = scanner.list().getTypeForIdentifierString (identifier);
        return description != nullptr && engine.canAcceptPlugin (trackId, *description);
    };
    deviceView.onPluginDropped = [this] (int trackId, const juce::String& identifier, int insertionIndex)
    {
        if (auto description = scanner.list().getTypeForIdentifierString (identifier))
            loadDevice (*description, trackId, insertionIndex);
    };
    deviceView.onRemove = [this] (int trackId, int deviceId)
    {
        windows.erase (deviceId);
        engine.removeDevice (trackId, deviceId);
        refreshDeviceView();
        project.sendChangeMessage();
    };

    browser.onLoadRequested = [this] (const juce::PluginDescription& d) { loadDevice (d); };
    scanner.onStarted = [this]
    {
        scanningPlugins = true;
        scanMessage = "Procurando plugins...";
        browser.setStatus (scanMessage);
        updateActionIndicator();
    };
    scanner.onProgress = [this] (const juce::String& n)
    {
        scanMessage = "Verificando: " + juce::File (n).getFileName();
        browser.setStatus (scanMessage);
    };
    scanner.onFinished = [this] (bool cancelled)
    {
        scanningPlugins = false;
        scanMessage.clear();
        browser.refresh();
        browser.setStatus (cancelled
            ? theme::ui ("Escaneamento cancelado. ") + juce::String (scanner.list().getNumTypes()) + " plugins encontrados"
            : juce::String (scanner.list().getNumTypes()) + " plugins encontrados");
        updateActionIndicator();
        if (waitingForProjectPluginScan)
        {
            waitingForProjectPluginScan = false;
            restoreNextProjectPlugin();
        }
    };

    engine.graph().setLoopRegion (project.loopStart, project.loopEnd);
    if (project.tracks.empty())
    {
        auto* midiTrack = project.addTrack();
        project.addTrack (roll::TrackType::Audio);
        project.selectTrack (midiTrack->id);
    }
    onSelectionChanged();
    changeListenerCallback (&model);
    showDetail (false);

    setWantsKeyboardFocus (true);
    setSize (1360, 800);
    startTimerHz (30);

    scanner.startScan();
}

MainComponent::~MainComponent()
{
    releaseComputerMidiNotes();

    // O registry é membro desta classe; encerre primeiro qualquer worker que o utilize.
    if (taskSimulationThread.joinable())
    {
        taskSimulationThread.request_stop();
        taskSimulationThread.join();
    }

    lifetimeToken->store (false);
    scanner.stopScan();
    projectFilePool.removeAllJobs (true, -1);
    if (audioRenderInProgress)
    {
        engine.finishOfflineRender();
        audioRenderInProgress = false;
    }
    if (projectLoadTask != nullptr)
    {
        projectLoadTask->end();
        projectLoadTask.reset();
    }
    stopTimer();
    project.removeChangeListener (this);
    model.removeChangeListener (this);
    windows.clear();                              // fecha as UIs dos plugins antes do engine
}

void MainComponent::setMainWindow (juce::TopLevelWindow* window)
{
    mainWindow = window;
    updatePluginWindowZOrder();
}

void MainComponent::updatePluginWindowZOrder()
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        auto* activeWindow = juce::TopLevelWindow::getActiveTopLevelWindow();
        bool appIsActive = activeWindow == safeThis->mainWindow
                        && safeThis->mainWindow != nullptr;
        if (! appIsActive)
            for (const auto& [deviceId, window] : safeThis->windows)
            {
                juce::ignoreUnused (deviceId);
                if (window.get() == activeWindow)
                {
                    appIsActive = true;
                    break;
                }
            }

        for (auto& [deviceId, window] : safeThis->windows)
        {
            juce::ignoreUnused (deviceId);
            if (window == nullptr)
                continue;

            const bool shouldStayOnTop = appIsActive && window->isVisible() && ! window->isMinimised();
            if (window->isAlwaysOnTop() != shouldStayOnTop)
                window->setAlwaysOnTop (shouldStayOnTop);
        }
    });
}

// ------------------------------------------------------------ layout

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    g.setColour (theme::border());
    g.fillRoundedRectangle (splitterRect.toFloat().reduced (static_cast<float> (splitterRect.getWidth()) * 0.45f, 2.0f), 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    transport.setBounds (area.removeFromTop (44));
    taskStatusBar.setBounds (area.removeFromBottom (30));
    if (actionIndicator != nullptr)
        actionIndicator->setBounds ((getWidth() - 320) / 2, 50, 320, 52);
    if (computerMidiIndicator != nullptr)
        computerMidiIndicator->setBounds (std::max (8, getWidth() - 240), 50, 224, 44);

    auto right = area.reduced (8);
    if (browser.isVisible() || audioBrowser.isVisible())
    {
        const int maxBrowserWidth = std::max (200, std::min (460, right.getWidth() - 760));
        browserWidth = std::clamp (browserWidth, 200, maxBrowserWidth);
        const auto bounds = right.removeFromLeft (browserWidth);
        browser.setBounds (browser.isVisible() ? bounds : juce::Rectangle<int>());
        audioBrowser.setBounds (audioBrowser.isVisible() ? bounds : juce::Rectangle<int>());
        browserSplitterRect = right.removeFromLeft (8);
    }
    else
    {
        browser.setBounds ({});
        audioBrowser.setBounds ({});
        browserSplitterRect = {};
    }

    rightTop = right.getY();
    rightHeight = std::max (1, right.getHeight());

    if (! pianoRollVisible)
    {
        arrangement.setBounds (right);
        splitterRect = {};
        clipTab.setBounds ({});
        deviceTab.setBounds ({});
        deviceView.setBounds ({});
        audioClipEditor.setBounds ({});
        for (auto* c : clipControls) c->setBounds ({});
        return;
    }

    const int arrH = std::clamp (static_cast<int> (right.getHeight() * arrangementRatio), 130, std::max (131, right.getHeight() - 230));
    arrangement.setBounds (right.removeFromTop (arrH));
    splitterRect = right.removeFromTop (10);

    auto tabs = right.removeFromTop (26);
    clipTab.setBounds (tabs.removeFromLeft (80));
    tabs.removeFromLeft (4);
    deviceTab.setBounds (tabs.removeFromLeft (110));
    right.removeFromTop (4);

    deviceView.setBounds (right);
    audioClipEditor.setBounds (right);

    auto side = right.removeFromLeft (170);
    right.removeFromLeft (8);
    editor.setBounds (right);

    scaleCaption.setBounds (side.removeFromTop (16));
    auto row = side.removeFromTop (26);
    rootBox.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (4);
    modeBox.setBounds (row);
    side.removeFromTop (4);
    row = side.removeFromTop (26);
    scaleButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    highlightButton.setBounds (row);
    side.removeFromTop (4);
    row = side.removeFromTop (26);
    foldButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    foldScaleButton.setBounds (row);
    side.removeFromTop (8);
    lengthCaption.setBounds (side.removeFromTop (16));
    lengthBox.setBounds (side.removeFromTop (26));
    side.removeFromTop (8);
    gridCaption.setBounds (side.removeFromTop (16));
    gridLabel.setBounds (side.removeFromTop (20));
    row = side.removeFromTop (26);
    drawButton.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    snapButton.setBounds (row);
    side.removeFromTop (4);
    previewButton.setBounds (side.removeFromTop (26).removeFromLeft (84));
    side.removeFromTop (8);
    zoomCaption.setBounds (side.removeFromTop (16));
    row = side.removeFromTop (26);
    zoomHMinus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (4);
    zoomHPlus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (8);
    zoomVMinus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (4);
    zoomVPlus.setBounds (row.removeFromLeft (40));
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingBrowserSplitter = browserSplitterRect.expanded (4, 0).contains (e.getPosition());
    draggingSplitter = splitterRect.expanded (0, 4).contains (e.getPosition());
    grabKeyboardFocus();
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBrowserSplitter)
    {
        const int maxBrowserWidth = std::max (200, std::min (460, getWidth() - 776));
        browserWidth = std::clamp (e.x - 8, 200, maxBrowserWidth);
        resized();
        repaint();
    }
    else if (draggingSplitter)
    {
        arrangementRatio = std::clamp (static_cast<double> (e.y - rightTop) / rightHeight, 0.15, 0.75);
        resized();
        repaint();
    }
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    draggingBrowserSplitter = draggingSplitter = false;
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    if (browserSplitterRect.expanded (4, 0).contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (splitterRect.expanded (0, 4).contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MainComponent::showDetail (bool devices)
{
    showingDevices = devices;
    clipTab.setToggleState (! devices, juce::dontSendNotification);
    deviceTab.setToggleState (devices, juce::dontSendNotification);
    for (auto* c : clipControls) c->setVisible (! devices && ! audioClipEditorOpen);
    deviceView.setVisible (devices);
    audioClipEditor.setVisible (! devices && audioClipEditorOpen);
    if (devices) refreshDeviceView();
}

void MainComponent::setBrowserVisible (bool visible)
{
    if (visible && audioBrowser.isVisible())
    {
        audioBrowser.setVisible (false);
        audioBrowser.setFavoritesMode (false);
        transport.setFileBrowserVisible (false);
        transport.setFavoritesVisible (false);
    }
    browser.setVisible (visible);
    transport.setBrowserVisible (visible);
    resized();
}

void MainComponent::setAudioBrowserVisible (bool visible)
{
    if (! visible)
        audioBrowser.stopPreview();
    if (visible && browser.isVisible())
    {
        browser.setVisible (false);
        transport.setBrowserVisible (false);
    }
    if (visible)
        audioBrowser.setFavoritesMode (false);
    audioBrowser.setVisible (visible);
    transport.setFileBrowserVisible (visible);
    transport.setFavoritesVisible (false);
    resized();
}

void MainComponent::setFavoritesVisible (bool visible)
{
    if (visible && browser.isVisible())
    {
        browser.setVisible (false);
        transport.setBrowserVisible (false);
    }
    audioBrowser.setFavoritesMode (visible);
    audioBrowser.setVisible (visible);
    transport.setFileBrowserVisible (false);
    transport.setFavoritesVisible (visible);
    resized();
}

void MainComponent::setPianoRollVisible (bool visible)
{
    pianoRollVisible = visible;
    transport.setPianoRollVisible (visible);
    resized();
}

void MainComponent::loadAudioFile (const juce::File& file, int trackId, double startBeat)
{
    ++pendingAudioLoads;
    audioBrowser.setStatus ("Carregando " + file.getFileName() + "...");
    updateActionIndicator();
    const int generation = projectGeneration;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    engine.loadAudioFileAsync (file, [this, token, file, trackId, startBeat, generation]
        (const juce::String& error, std::shared_ptr<const AudioFileData> data)
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load()) return;

        pendingAudioLoads = std::max (0, pendingAudioLoads - 1);
        updateActionIndicator();
        if (generation != projectGeneration)
            return;
        if (error.isNotEmpty() || data == nullptr)
        {
            audioBrowser.setStatus ("Erro ao carregar audio: " + error);
            return;
        }

        auto* track = project.track (trackId);
        if (track == nullptr || track->type != roll::TrackType::Audio)
        {
            audioBrowser.setStatus ("A trilha de audio foi removida durante o carregamento.");
            return;
        }

        const auto path = file.getFullPathName().toStdString();
        audioAssets[path] = data;
        const double sourceBpm = sourceBpmFromFileName (file, engine.graph().getBpm());
        const double lengthBeats = data->durationSeconds * sourceBpm / 60.0;
        if (project.addAudioClip (trackId, startBeat, lengthBeats, path,
                                  file.getFileName().toStdString(), data->waveform,
                                  data->durationSeconds, sourceBpm) == nullptr)
        {
            publishAudioTrack (trackId);
            audioBrowser.setStatus ("Nao foi possivel inserir o audio nesse ponto da trilha.");
            return;
        }

        audioBrowser.setStatus (file.getFileName() + " adicionado");
    });
}

void MainComponent::consolidateAudioRange (double startBeat, double endBeat, int firstTrack, int lastTrack)
{
    if (audioRenderInProgress || projectFileOperationInProgress
        || ! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || scanningPlugins)
    {
        audioBrowser.setStatus ("Aguarde os carregamentos e operacoes atuais antes de consolidar.");
        return;
    }

    if (firstTrack != lastTrack || firstTrack < 0 || firstTrack >= static_cast<int> (project.tracks.size())
        || startBeat < 0.0 || endBeat <= startBeat)
    {
        audioBrowser.setStatus ("Consolidar: selecione um trecho de uma unica faixa de audio.");
        return;
    }

    auto* track = project.tracks[static_cast<std::size_t> (firstTrack)].get();
    if (track->type != roll::TrackType::Audio)
    {
        audioBrowser.setStatus ("Consolidar: a faixa selecionada precisa ser de audio.");
        return;
    }

    constexpr double consolidationSampleRate = 44100.0;
    const double bpm = engine.graph().getBpm();
    if (! std::isfinite (bpm) || bpm <= 0.0)
    {
        audioBrowser.setStatus ("Consolidar: o BPM do projeto e invalido.");
        return;
    }
    const double durationSeconds = (endBeat - startBeat) * 60.0 / bpm;
    const auto frameCountDouble = std::ceil (durationSeconds * consolidationSampleRate);
    if (! std::isfinite (frameCountDouble) || frameCountDouble < 1.0
        || durationSeconds > 600.0
        || frameCountDouble > static_cast<double> (std::numeric_limits<int>::max()))
    {
        audioBrowser.setStatus ("Consolidar: a selecao e grande demais para renderizar (maximo de 10 minutos).");
        return;
    }

    bool foundSource = false;
    for (const auto& clip : track->audioClips)
    {
        if (! clip->enabled)
            continue;

        const double overlapStart = std::max (startBeat, clip->start);
        const double overlapEnd = std::min (endBeat, clip->end());
        if (overlapEnd <= overlapStart)
            continue;

        const auto asset = audioAssets.find (clip->filePath);
        if (asset == audioAssets.end() || asset->second == nullptr || asset->second->samples == nullptr
            || asset->second->sampleRate <= 0.0)
        {
            audioBrowser.setStatus ("Consolidar: nao foi possivel acessar a origem de " + juce::String (clip->name));
            return;
        }

        foundSource = true;
    }

    if (! foundSource)
    {
        audioBrowser.setStatus ("Consolidar: nao ha clips de audio na selecao.");
        return;
    }

    std::vector<int> overlappingIds;
    for (const auto& clip : track->audioClips)
        if (clip->start < endBeat && clip->end() > startBeat)
            overlappingIds.push_back (clip->id);

    auto outputDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile ("ProjectFreedom").getChildFile ("Consolidated");
    if (const auto error = outputDirectory.createDirectory(); error.failed())
    {
        audioBrowser.setStatus ("Consolidar: nao foi possivel criar a pasta de destino: " + error.getErrorMessage());
        return;
    }
    const auto outputFile = outputDirectory.getNonexistentChildFile (
        "Consolidated_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"),
        ".wav", false);
    const int trackId = track->id;
    const auto trackName = juce::String (track->name);

    releaseComputerMidiNotes();
    juce::String error;
    if (! engine.beginOfflineTrackRender (trackId, error))
    {
        audioBrowser.setStatus ("Nao foi possivel iniciar a consolidacao: " + error);
        return;
    }

    audioRenderInProgress = true;
    setAudioRenderUiEnabled (false);
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Consolidando faixa: " + trackName, "global", 0.0, true,
        [cancelled] { cancelled->store (true, std::memory_order_release); });
    const auto outputSampleRate = engine.offlineRenderSampleRate();
    const auto token = std::weak_ptr<std::atomic<bool>> (lifetimeToken);

    projectFilePool.addJob ([this, token, task, cancelled, outputFile, trackId,
                              startBeat, endBeat, bpm, durationSeconds, outputSampleRate,
                              overlappingIds = std::move (overlappingIds)]
    {
        const auto frameCount = static_cast<int> (std::ceil (durationSeconds * outputSampleRate));
        auto renderedSamples = std::make_shared<juce::AudioBuffer<float>> (2, frameCount);
        int lastReportedPercent = -1;
        const auto renderResult = engine.renderTrackRange (
            startBeat, endBeat - startBeat, *renderedSamples,
            [task, &lastReportedPercent] (double progress)
            {
                const int percent = static_cast<int> (progress * 100.0);
                if (percent != lastReportedPercent)
                {
                    task->setProgress (progress);
                    lastReportedPercent = percent;
                }
            },
            [cancelled] { return cancelled->load (std::memory_order_acquire); });

        auto result = renderResult;
        std::shared_ptr<AudioFileData> data;
        if (result.wasOk())
        {
            auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
            if (! fileStream->openedOk())
            {
                result = juce::Result::fail ("Nao foi possivel criar o WAV.");
            }
            else
            {
                std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));
                juce::WavAudioFormat wavFormat;
                const juce::AudioFormatWriterOptions writerOptions;
                auto writer = wavFormat.createWriterFor (outputStream,
                    writerOptions.withSampleRate (outputSampleRate).withNumChannels (2).withBitsPerSample (24));
                if (writer == nullptr
                    || ! writer->writeFromAudioSampleBuffer (*renderedSamples, 0, renderedSamples->getNumSamples()))
                {
                    writer.reset();
                    outputStream.reset();
                    result = juce::Result::fail ("Falha ao gravar o arquivo WAV.");
                }
                else
                {
                    writer.reset();
                    data = std::make_shared<AudioFileData>();
                    data->file = outputFile;
                    data->samples = renderedSamples;
                    data->sampleRate = outputSampleRate;
                    data->durationSeconds = durationSeconds;

                    constexpr int maxWaveformBins = 16384;
                    const int waveformBins = std::min (renderedSamples->getNumSamples(), maxWaveformBins);
                    data->waveform.resize (static_cast<std::size_t> (waveformBins));
                    for (int bin = 0; bin < waveformBins; ++bin)
                    {
                        const int sampleCount = renderedSamples->getNumSamples();
                        const int from = static_cast<int> (static_cast<std::int64_t> (bin) * sampleCount / waveformBins);
                        const int to = std::max (from + 1, static_cast<int> (
                            static_cast<std::int64_t> (bin + 1) * sampleCount / waveformBins));
                        float minimum = 1.0f;
                        float maximum = -1.0f;
                        for (int frame = from; frame < std::min (to, sampleCount); ++frame)
                            for (int channel = 0; channel < renderedSamples->getNumChannels(); ++channel)
                            {
                                const float value = renderedSamples->getSample (channel, frame);
                                minimum = std::min (minimum, value);
                                maximum = std::max (maximum, value);
                            }
                        data->waveform[static_cast<std::size_t> (bin)] = { minimum, maximum };
                    }
                }
            }
        }

        if (result.failed())
            outputFile.deleteFile();

        task->end();
        juce::MessageManager::callAsync ([this, token, task, cancelled, outputFile, trackId,
                                          startBeat, endBeat, bpm, durationSeconds, result, data,
                                          overlappingIds = std::move (overlappingIds)]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
            {
                outputFile.deleteFile();
                return;
            }

            engine.finishOfflineRender();
            audioRenderInProgress = false;
            setAudioRenderUiEnabled (true);

            if (cancelled->load (std::memory_order_acquire))
            {
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidacao cancelada.");
                return;
            }
            if (result.failed())
            {
                audioBrowser.setStatus ("Falha ao consolidar: " + result.getErrorMessage());
                return;
            }

            auto* targetTrack = project.track (trackId);
            if (targetTrack == nullptr || targetTrack->type != roll::TrackType::Audio || data == nullptr)
            {
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidar: a faixa mudou durante a renderizacao.");
                return;
            }

            project.beginEdit();
            bool replaced = true;
            for (const int clipId : overlappingIds)
            {
                auto* clip = targetTrack->findAudioClip (clipId);
                if (clip == nullptr)
                    continue;
                int innerClipId = clipId;
                if (clip->start < startBeat && clip->end() > startBeat)
                {
                    innerClipId = project.splitAudioClip (trackId, clipId, startBeat);
                    if (innerClipId < 0)
                    {
                        replaced = false;
                        break;
                    }
                }

                clip = targetTrack->findAudioClip (innerClipId);
                if (clip != nullptr && clip->end() > endBeat)
                    project.splitAudioClip (trackId, innerClipId, endBeat);
                clip = targetTrack->findAudioClip (innerClipId);
                if (clip != nullptr && clip->start >= startBeat - 1.0e-9
                    && clip->end() <= endBeat + 1.0e-9)
                    project.deleteAudioClip (trackId, innerClipId);
            }

            const auto path = outputFile.getFullPathName().toStdString();
            audioAssets[path] = data;
            if (replaced && project.addAudioClip (trackId, startBeat, endBeat - startBeat, path,
                                                  outputFile.getFileName().toStdString(), data->waveform,
                                                  durationSeconds, bpm) == nullptr)
                replaced = false;
            project.endEdit();
            if (! replaced)
            {
                project.undo();
                audioAssets.erase (path);
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidar: nao foi possivel substituir a selecao.");
                return;
            }

            audioBrowser.setStatus ("Selecao consolidada: " + outputFile.getFileName()
                                    + " (efeitos da faixa aplicados)");
        });
    });
}

void MainComponent::publishAudioTrack (int trackId)
{
    std::vector<AudioClipPlayback> clips;
    if (const auto* track = project.track (trackId); track != nullptr && track->type == roll::TrackType::Audio)
        for (const auto& clip : track->audioClips)
            if (clip->enabled)
                if (const auto asset = audioAssets.find (clip->filePath); asset != audioAssets.end())
                    clips.push_back ({ clip->start, clip->lengthBeats, clip->sourceBpm,
                                       clip->sourceOffsetSeconds, clip->fadeInBeats,
                                       clip->fadeOutBeats, clip->fadeInCurve, clip->fadeOutCurve,
                                       asset->second, clip->warpEnabled });
    engine.publishAudioClips (trackId, clips);

    for (auto it = audioAssets.begin(); it != audioAssets.end();)
        if (! project.retainsAudioFile (it->first))
            it = audioAssets.erase (it);
        else
            ++it;
}

bool MainComponent::selectedTrackSpan (double& start, double& end) const
{
    const auto* track = project.selectedTrack();
    if (track == nullptr)
        return false;

    start = std::numeric_limits<double>::max();
    end = 0.0;
    if (track->type == roll::TrackType::Audio)
    {
        for (const auto& clip : track->audioClips)
        {
            start = std::min (start, clip->start);
            end = std::max (end, clip->end());
        }
    }
    else
    {
        for (const auto& clip : track->clips)
        {
            start = std::min (start, clip->start);
            end = std::max (end, clip->end());
        }
    }
    return start != std::numeric_limits<double>::max();
}

void MainComponent::playSelectedTrack()
{
    if (engine.graph().isPlaying())
    {
        transport.togglePlay();
        return;
    }

    double selectionStart = 0.0;
    if (arrangement.getTimeSelectionStart (selectionStart))
    {
        pendingArrangementSeek = true;
        pendingArrangementSeekBeat = selectionStart;
        transportPosition = selectionStart;
        engine.graph().seek (selectionStart);
        arrangement.setPlayhead (selectionStart);
        editor.setPlayhead (selectionStart - model.clipStartBeats);
        transport.togglePlay();
        return;
    }

    if (pendingArrangementSeek
        && std::abs (transportPosition - pendingArrangementSeekBeat) < 1.0e-6)
    {
        transportPosition = pendingArrangementSeekBeat;
        engine.graph().seek (pendingArrangementSeekBeat);
        arrangement.setPlayhead (pendingArrangementSeekBeat);
        editor.setPlayhead (pendingArrangementSeekBeat - model.clipStartBeats);
        transport.togglePlay();
        return;
    }
    pendingArrangementSeek = false;

    double start = 0.0, end = 0.0;
    const auto* selectedTrack = project.selectedTrack();
    const auto* selectedAudio = selectedTrack != nullptr && selectedTrack->type == roll::TrackType::Audio
        ? selectedTrack->findAudioClip (project.selectedAudioClipId) : nullptr;
    if (selectedAudio != nullptr)
    {
        start = selectedAudio->start;
        end = selectedAudio->end();
        if (transportPosition >= start && transportPosition < end)
            start = transportPosition;
    }

    if (selectedAudio != nullptr || selectedTrackSpan (start, end))
    {
        if (engine.graph().isLoop() && (start < project.loopStart || end > project.loopEnd))
            transport.setLoopEnabled (false);
        transportPosition = start;
        engine.graph().seek (start);
        arrangement.setPlayhead (start);
        editor.setPlayhead (start - model.clipStartBeats);
    }
    else
    {
        engine.graph().seek (0.0);
        arrangement.setPlayhead (0.0);
    }
    transport.togglePlay();
}

void MainComponent::resumeFromPausedPosition()
{
    if (engine.graph().isPlaying())
        return;

    pendingArrangementSeek = false;
    transportPosition = pausedPosition;
    arrangement.setPlayhead (pausedPosition);
    editor.setPlayhead (pausedPosition - model.clipStartBeats);
    transport.resumeFromPosition (pausedPosition);
}

bool MainComponent::handleTransportKeyPress (const juce::KeyPress& key)
{
    if (audioRenderInProgress)
        return true;

    if (key.isKeyCode (juce::KeyPress::spaceKey) && key.getModifiers().isShiftDown())
    {
        resumeFromPausedPosition();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        playSelectedTrack();
        return true;
    }

    return false;
}

void MainComponent::loopSelectedTrack()
{
    if (engine.graph().isLoop())
    {
        transport.setLoopEnabled (false);
        return;
    }

    double start = 0.0, end = 0.0;
    const auto* track = project.selectedTrack();
    const auto* audio = track != nullptr && track->type == roll::TrackType::Audio
        ? track->findAudioClip (project.selectedAudioClipId) : nullptr;
    const auto* midi = project.selectedClip();
    if (audio != nullptr)
    {
        start = audio->start;
        end = audio->end();
    }
    else if (midi != nullptr)
    {
        start = midi->start;
        end = midi->end();
    }
    else if (! selectedTrackSpan (start, end))
        return;

    project.setLoop (start, end);
    transport.setLoopEnabled (true);
}

// ------------------------------------------------------------ teclado

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (audioRenderInProgress)
        return true;

#if JUCE_DEBUG
    if (k.getKeyCode() == juce::KeyPress::F10Key
        && k.getModifiers().isCtrlDown() && k.getModifiers().isAltDown())
    {
        startSimulatedTask();
        return true;
    }
#endif

    const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (k.getKeyCode()));
    const bool zeroKey = k.getKeyCode() == '0'
                      || k.getKeyCode() == juce::KeyPress::numberPad0
                      || k.getTextCharacter() == '0';
    if (zeroKey && ! ctrl && ! k.getModifiers().isAltDown()
        && ! k.getModifiers().isShiftDown() && editor.hasKeyboardFocus (true))
    {
        model.toggleMuteSelected();
        return true;
    }

    if (juce::Process::isForegroundProcess() && ! isTextEditorFocused()
        && ! ctrl && ! k.getModifiers().isAltDown() && keyCode == 'M')
    {
        if (! computerMidiModeKeyDown)
        {
            computerMidiModeKeyDown = true;
            setComputerMidiKeyboardEnabled (! computerMidiKeyboardEnabled);
        }
        return true;
    }

    if (computerMidiKeyboardEnabled && juce::Process::isForegroundProcess() && ! isTextEditorFocused()
        && ! ctrl && ! k.getModifiers().isAltDown())
    {
        const bool isMappedKey = std::any_of (computerMidiKeyboardMap.begin(), computerMidiKeyboardMap.end(),
            [keyCode] (const auto& mapping) { return mapping.first == static_cast<int> (keyCode); });
        if (isMappedKey || keyCode == 'Z' || keyCode == 'X' || keyCode == 'C' || keyCode == 'V')
        {
            updateComputerMidiKeyboard();
            return true;
        }
    }

    if (ctrl && keyCode == 'S')
    {
        if (currentProjectFile.existsAsFile())
            saveProject (currentProjectFile);
        else
            chooseProjectFile (false);
        return true;
    }
    if (ctrl && keyCode == 'O')
    {
        chooseProjectFile (true);
        return true;
    }
    if (ctrl && keyCode == 'D' && ! isTextEditorFocused()
        && ! arrangement.hasKeyboardFocus (true) && ! editor.hasKeyboardFocus (true)
        && project.selectedTrackId > 0 && project.selectedClipId < 0
        && project.selectedAudioClipId < 0)
    {
        double selectionStart = 0.0;
        if (! arrangement.getTimeSelectionStart (selectionStart))
        {
            duplicateSelectedTrack (project.selectedTrackId);
            return true;
        }
    }
    if (ctrl && keyCode == 'Z' && ! k.getModifiers().isShiftDown())
        return project.undo();
    if (ctrl && (keyCode == 'Y' || (keyCode == 'Z' && k.getModifiers().isShiftDown())))
        return project.redo();

    if (k.getModifiers().isCtrlDown()
        && juce::CharacterFunctions::toLowerCase (k.getKeyCode()) == 'b')
    {
        setBrowserVisible (! browser.isVisible());
        return true;
    }
    if (k.getModifiers().isCtrlDown()
        && juce::CharacterFunctions::toLowerCase (k.getKeyCode()) == 'l')
    {
        loopSelectedTrack();
        return true;
    }
    if (handleTransportKeyPress (k))
        return true;
    if (k == juce::KeyPress::homeKey)  { transport.rewind(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::F12Key) || k == juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0))
    {
        showDetail (! showingDevices);
        return true;
    }
    if (k == juce::KeyPress (juce::KeyPress::F6Key)) { cycleFocus(); return true; }
    if (ctrl && keyCode == 'T')
    {
        project.addTrack (k.getModifiers().isShiftDown()
            ? roll::TrackType::Midi : roll::TrackType::Audio);
        return true;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    updateComputerMidiKeyboard();
    return false;
}

void MainComponent::cycleFocus()
{
    // navegação por teclado: lista de plugins -> arranjo -> piano roll
    if (browser.hasKeyboardFocus (true))          arrangement.grabFocus();
    else if (arrangement.hasKeyboardFocus (true)) { if (showingDevices) showDetail (false); editor.grabFocus(); }
    else                                          browser.grabFocus();
}

// ------------------------------------------------------------ projeto / engine

juce::String MainComponent::deviceSummary (int trackId) const
{
    juce::StringArray names;
    for (const auto& d : engine.devices (trackId)) names.add (d.name);
    const auto summary = names.joinIntoString (" > ");
    if (trackId == 0) return summary.isNotEmpty() ? summary : theme::ui ("Efeitos Master");
    const auto* track = project.track (trackId);
    if (summary.isNotEmpty())
        return summary;
    if (track == nullptr)
        return {};
    if (track->type == roll::TrackType::Group)
        return theme::ui ("Barramento de grupo");
    return track->type == roll::TrackType::Audio ? theme::ui ("Canal de audio") : juce::String();
}

void MainComponent::applyMixers()
{
    bool anySolo = false;
    for (const auto& t : project.tracks) anySolo |= t->solo;
    for (const auto& t : project.tracks)
    {
        engine.setTrackRouting (t->id, t->outputRouting, t->parentGroupId);
        bool audibleWhenSoloed = t->solo;
        if (t->type == roll::TrackType::Group)
        {
            audibleWhenSoloed |= std::any_of (project.tracks.begin(), project.tracks.end(),
                [id = t->id] (const auto& child) { return child->parentGroupId == id && child->solo; });
        }
        else if (t->parentGroupId != 0)
        {
            if (const auto* group = project.track (t->parentGroupId))
                audibleWhenSoloed |= group->solo;
        }
        engine.setMixer (t->id, static_cast<float> (t->volume), static_cast<float> (t->pan),
                         t->mute || (anySolo && ! audibleWhenSoloed));
    }
    if (auto* m = project.masterTrack.get())
        engine.setMasterMixer (static_cast<float> (m->volume), static_cast<float> (m->pan), m->mute);
}

void MainComponent::showProjectMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Abrir projeto...");
    menu.addItem (2, "Salvar", currentProjectFile.existsAsFile());
    menu.addItem (3, "Salvar como...");
    menu.addSeparator();
    menu.addItem (5, "Exportar Master como WAV...", ! audioRenderInProgress);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&transport),
        [this] (int result)
        {
            if (result == 1) chooseProjectFile (true);
            else if (result == 2) saveProject (currentProjectFile);
            else if (result == 3) chooseProjectFile (false);
            else if (result == 5) chooseWavExportFile();
        });
}

void MainComponent::chooseWavExportFile()
{
    if (audioRenderInProgress || projectFileOperationInProgress)
    {
        setProjectStatus ("Aguarde a operacao atual terminar antes de exportar.");
        return;
    }

    const auto suggestedFile = currentProjectFile.existsAsFile()
        ? currentProjectFile.withFileExtension (".wav")
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("Project Freedom.wav");
    audioExportChooser = std::make_unique<juce::FileChooser> (
        "Exportar Master como WAV", suggestedFile, "*.wav");
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    audioExportChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, token] (const juce::FileChooser& chooser)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            const auto result = chooser.getResult();
            audioExportChooser.reset();
            if (result == juce::File())
                return;

            showWavRenderOptions (result.withFileExtension (".wav"));
        });
}

void MainComponent::showWavRenderOptions (const juce::File& file)
{
    auto* alert = new juce::AlertWindow (
        "Exportar Master como WAV", "Cauda apos o fim do arrangement (segundos):",
        juce::AlertWindow::NoIcon);
    alert->addTextEditor ("tailSeconds", "2.0");
    alert->addButton ("Exportar", 1);
    alert->addButton ("Cancelar", 0);

    juce::Component::SafePointer<MainComponent> safeThis (this);
    alert->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, alert, file] (int result)
        {
            if (safeThis == nullptr || result != 1)
                return;

            const auto tailText = alert->getTextEditorContents ("tailSeconds")
                                      .trim().replaceCharacter (',', '.');
            const bool hasDigit = std::any_of (tailText.begin(), tailText.end(),
                [] (juce::juce_wchar character) { return juce::CharacterFunctions::isDigit (character); });
            const double tailSeconds = tailText.getDoubleValue();
            if (! hasDigit || ! std::isfinite (tailSeconds) || tailSeconds < 0.0 || tailSeconds > 60.0)
            {
                safeThis->setProjectStatus ("A cauda deve ser um valor entre 0 e 60 segundos.");
                return;
            }

            safeThis->startWavRender (file, tailSeconds);
        }), true);
}

void MainComponent::startWavRender (const juce::File& file, double tailSeconds)
{
    if (audioRenderInProgress || projectFileOperationInProgress
        || ! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || scanningPlugins)
    {
        setProjectStatus ("Aguarde os carregamentos e operacoes atuais antes de exportar.");
        return;
    }

    double endBeat = 0.0;
    for (const auto& track : project.tracks)
    {
        if (track->type == roll::TrackType::Audio)
        {
            for (const auto& clip : track->audioClips)
                if (clip->enabled)
                    endBeat = std::max (endBeat, clip->end());
        }
        else
        {
            for (const auto& clip : track->clips)
                if (clip->enabled)
                    endBeat = std::max (endBeat, clip->end());
        }
    }

    const double bpm = engine.graph().getBpm();
    if (endBeat <= 0.0 || ! std::isfinite (bpm) || bpm <= 0.0)
    {
        setProjectStatus ("O projeto nao possui audio ou clips para exportar.");
        return;
    }

    releaseComputerMidiNotes();
    juce::String error;
    if (! engine.beginOfflineRender (error))
    {
        setProjectStatus ("Nao foi possivel iniciar a exportacao: " + error);
        return;
    }

    audioRenderInProgress = true;
    setAudioRenderUiEnabled (false);
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Renderizando Master: " + file.getFileName(), "global", 0.0, true,
        [cancelled] { cancelled->store (true, std::memory_order_release); });
    const auto durationSeconds = endBeat * 60.0 / bpm + tailSeconds;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;

    projectFilePool.addJob ([this, token, task, cancelled, file, durationSeconds]
    {
        const auto result = engine.renderMasterToWav (
            file, durationSeconds,
            [task] (double progress) { task->setProgress (progress); },
            [cancelled] { return cancelled->load (std::memory_order_acquire); });

        juce::MessageManager::callAsync ([this, token, task, cancelled, file, result]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            engine.finishOfflineRender();
            audioRenderInProgress = false;
            setAudioRenderUiEnabled (true);
            task->end();

            if (cancelled->load (std::memory_order_acquire))
                setProjectStatus ("Exportacao de audio cancelada.");
            else if (result.failed())
                setProjectStatus ("Falha ao exportar WAV: " + result.getErrorMessage());
            else
                setProjectStatus ("Master exportado: " + file.getFullPathName());
        });
    });
}

void MainComponent::setAudioRenderUiEnabled (bool enabled)
{
    transport.setEnabled (enabled);
    browser.setEnabled (enabled);
    audioBrowser.setEnabled (enabled);
    arrangement.setEnabled (enabled);
    editor.setEnabled (enabled);
    audioClipEditor.setEnabled (enabled);
    deviceView.setEnabled (enabled);
    clipTab.setEnabled (enabled);
    deviceTab.setEnabled (enabled);
    for (auto* control : clipControls)
        control->setEnabled (enabled);
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->setEnabled (enabled);
    }
}

void MainComponent::openProjectFromFile (const juce::File& file)
{
    if (file.existsAsFile() && isProjectFile (file))
        loadProject (file);
}

void MainComponent::requestClose (std::function<void()> onCloseApproved)
{
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Salvar projeto?")
            .withMessage ("Deseja salvar o projeto antes de fechar?")
            .withButton ("Salvar")
            .withButton ("Nao salvar")
            .withButton ("Cancelar")
            .withAssociatedComponent (this),
        [safeThis, onCloseApproved = std::move (onCloseApproved)] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            if (result == 2)
            {
                if (onCloseApproved)
                    onCloseApproved();
                return;
            }

            if (result != 1)
                return;

            const auto saved = [onCloseApproved] (bool success)
            {
                if (success && onCloseApproved)
                    onCloseApproved();
            };
            if (safeThis->currentProjectFile.existsAsFile())
                safeThis->saveProject (safeThis->currentProjectFile, saved);
            else
                safeThis->chooseProjectFile (false, saved);
        });
}

void MainComponent::chooseProjectFile (bool open, std::function<void (bool)> onSaveComplete)
{
    const auto start = currentProjectFile.existsAsFile()
        ? currentProjectFile : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    projectFileChooser = std::make_unique<juce::FileChooser> (
        open ? "Abrir projeto" : "Salvar projeto",
        start, open ? "*.pfs;*.freedom" : "*.pfs");
    const auto chooserFlags = open ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   : juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    projectFileChooser->launchAsync (chooserFlags, [this, open, token,
                                                     onSaveComplete = std::move (onSaveComplete)]
                                                   (const juce::FileChooser& chooser) mutable
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load()) return;
        const auto result = chooser.getResult();
        projectFileChooser.reset();
        if (! result.exists() && open)
            return;
        if (result == juce::File())
        {
            if (onSaveComplete)
                onSaveComplete (false);
            return;
        }
        if (open)
            loadProject (result);
        else
            saveProject (result, std::move (onSaveComplete));
    });
}

void MainComponent::saveProject (const juce::File& requestedFile, std::function<void (bool)> onComplete)
{
    if (! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || projectFileOperationInProgress || audioRenderInProgress)
    {
        setProjectStatus ("Aguarde o fim dos carregamentos antes de salvar.");
        if (onComplete)
            onComplete (false);
        return;
    }

    auto file = requestedFile;
    if (file.getFileExtension().compareIgnoreCase (".pfs") != 0)
        file = file.withFileExtension (".pfs");

    projectFileOperationInProgress = true;
    auto task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Salvando projeto: ") + file.getFileName(), "global");
    auto snapshot = std::make_shared<ProjectFileData>();
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    if (! juce::MessageManager::callAsync ([this, token, file, task, snapshot,
                                            onComplete = std::move (onComplete)] () mutable
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;

        snapshot->project = project.createState();
        snapshot->plugins = engine.getPluginStates();
        snapshot->bpm = engine.graph().getBpm();
        snapshot->transportPosition = engine.graph().positionBeats();
        snapshot->loopEnabled = engine.graph().isLoop();
        snapshot->metronomeEnabled = engine.graph().isMetronomeEnabled();
        snapshot->metronomeGain = engine.graph().getMetronomeGain();

        projectFilePool.addJob ([this, token, file, task, snapshot,
                                 onComplete = std::move (onComplete)] () mutable
        {
            const auto result = ProjectFile::save (file, *snapshot, [task] (double progress)
            {
                task->setProgress (progress);
            });

            if (! juce::MessageManager::callAsync ([this, token, file, task, result,
                                                    onComplete = std::move (onComplete)] () mutable
            {
                task->end();
                const auto alive = token.lock();
                if (alive == nullptr || ! alive->load())
                    return;

                projectFileOperationInProgress = false;
                if (result.failed())
                {
                    setProjectStatus ("Falha ao salvar projeto: " + result.getErrorMessage());
                    if (onComplete)
                        onComplete (false);
                    return;
                }

                currentProjectFile = file;
                setProjectStatus ("Projeto salvo: " + file.getFileName());
                if (onComplete)
                    onComplete (true);
            }))
            {
                juce::Logger::writeToLog ("MainComponent: nao foi possivel publicar o resultado do salvamento.");
            }
        });
    }))
    {
        task->end();
        projectFileOperationInProgress = false;
        juce::Logger::writeToLog ("MainComponent: nao foi possivel iniciar o salvamento do projeto.");
        setProjectStatus ("Falha ao iniciar o salvamento do projeto.");
        if (onComplete)
            onComplete (false);
    }
}

void MainComponent::loadProject (const juce::File& file)
{
    if (! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || projectFileOperationInProgress || audioRenderInProgress)
    {
        setProjectStatus ("Aguarde o fim dos carregamentos antes de abrir outro projeto.");
        return;
    }

    projectFileOperationInProgress = true;
    auto task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Abrindo projeto: ") + file.getFileName(), "global", -1.0);
    auto data = std::make_shared<ProjectFileData>();
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    projectFilePool.addJob ([this, token, file, task, data]
    {
        const auto result = ProjectFile::load (file, *data, [task] (double progress)
        {
            task->setProgress (progress < 0.0 ? -1.0 : progress * 0.25);
        });

        if (! juce::MessageManager::callAsync ([this, token, file, task, data, result]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            if (result.failed())
            {
                task->end();
                projectFileOperationInProgress = false;
                setProjectStatus ("Falha ao abrir projeto: " + result.getErrorMessage());
                return;
            }

            applyLoadedProject (file, std::move (*data), task);
        }))
        {
            juce::Logger::writeToLog ("MainComponent: nao foi possivel publicar o resultado da abertura.");
        }
    });
}

void MainComponent::applyLoadedProject (const juce::File& file, ProjectFileData data,
                                       const std::shared_ptr<TaskScope>& task)
{
    projectLoadTask = task;
    projectLoadWorkDone = 0;
    projectLoadWorkTotal = data.plugins.size();
    std::set<std::string> audioPaths;
    for (const auto& track : data.project.tracks)
        if (track.type == roll::TrackType::Audio)
            for (const auto& clip : track.audioClips)
                audioPaths.insert (clip->filePath);
    projectLoadWorkTotal += audioPaths.size();
    task->setProgress (0.25);

    transport.stop();
    engine.stopAudioPreview();
    ++projectLoadToken;
    ++projectGeneration;
    pendingProjectPlugins.clear();
    loadedProjectPlugins.clear();
    nextProjectPlugin = 0;
    missingProjectPlugins = 0;
    waitingForProjectPluginScan = false;
    restoringProjectPlugins = ! data.plugins.empty();

    closeWindowsOfTrack (AudioEngine::masterTrackId);
    for (const auto& device : engine.devices (AudioEngine::masterTrackId))
        engine.removeDevice (AudioEngine::masterTrackId, device.id);

    project.replaceState (std::move (data.project));
    audioAssets.clear();
    for (const auto& track : project.tracks)
        if (track->type == roll::TrackType::Audio && project.onAudioClipsChanged)
            project.onAudioClipsChanged (track->id);

    transport.setBpm (data.bpm);
    transport.setMetronomeState (data.metronomeEnabled, data.metronomeGain);
    transport.setLoopEnabled (data.loopEnabled);
    transportPosition = data.transportPosition;
    pausedPosition = data.transportPosition;
    engine.graph().seek (transportPosition);
    arrangement.setPlayhead (transportPosition);

    currentProjectFile = file;
    pendingProjectPlugins = std::move (data.plugins);
    loadProjectAudio (data);
    setProjectStatus ("Projeto aberto: " + file.getFileName());
    if (scanningPlugins && ! pendingProjectPlugins.empty())
        waitingForProjectPluginScan = true;
    else
        restoreNextProjectPlugin();

    advanceProjectLoadTask();
}

void MainComponent::advanceProjectLoadTask()
{
    if (projectLoadTask == nullptr)
        return;

    const auto completed = std::min (projectLoadWorkDone, projectLoadWorkTotal);
    const auto fraction = projectLoadWorkTotal > 0
        ? static_cast<double> (completed) / static_cast<double> (projectLoadWorkTotal) : 1.0;
    projectLoadTask->setProgress (0.25 + 0.75 * fraction);

    if (projectLoadWorkDone >= projectLoadWorkTotal && pendingAudioLoads == 0
        && ! restoringProjectPlugins && ! waitingForProjectPluginScan)
        finishProjectLoadTask();
}

void MainComponent::finishProjectLoadTask()
{
    if (projectLoadTask != nullptr)
    {
        projectLoadTask->setProgress (1.0);
        projectLoadTask->end();
        projectLoadTask.reset();
    }
    projectFileOperationInProgress = false;
    projectLoadWorkTotal = projectLoadWorkDone = 0;
}

void MainComponent::restoreNextProjectPlugin()
{
    if (waitingForProjectPluginScan)
        return;

    const int token = projectLoadToken;
    while (nextProjectPlugin < pendingProjectPlugins.size())
    {
        auto plugin = pendingProjectPlugins[nextProjectPlugin++];
        const auto description = scanner.list().getTypeForIdentifierString (plugin.identifier);
        if (description == nullptr)
        {
            ++missingProjectPlugins;
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
            continue;
        }

        engine.addDevice (plugin.trackId, *description,
            [this, token, plugin] (const juce::String& error, int deviceId)
            {
                if (token != projectLoadToken)
                {
                    if (deviceId > 0)
                        engine.removeDevice (plugin.trackId, deviceId);
                    return;
                }

                if (error.isNotEmpty() || deviceId <= 0)
                {
                    ++missingProjectPlugins;
                    setProjectStatus ("Nao foi possivel carregar " + plugin.identifier + ": " + error);
                    ++projectLoadWorkDone;
                    advanceProjectLoadTask();
                    restoreNextProjectPlugin();
                    return;
                }

                if (! engine.restorePluginState (plugin.trackId, deviceId, plugin.state))
                    ++missingProjectPlugins;
                engine.setDeviceBypassed (plugin.trackId, deviceId, plugin.bypassed);
                loadedProjectPlugins.emplace_back (plugin, deviceId);
                refreshDeviceView();
                ++projectLoadWorkDone;
                advanceProjectLoadTask();
                restoreNextProjectPlugin();
            }, plugin.order);
        return;
    }

    std::map<std::pair<int, int>, std::vector<int>> groups;
    for (const auto& [plugin, deviceId] : loadedProjectPlugins)
        if (plugin.groupId > 0)
            groups[{ plugin.trackId, plugin.groupId }].push_back (deviceId);
    for (const auto& [key, ids] : groups)
        if (ids.size() > 1)
            engine.groupDevices (key.first, ids);

    loadedProjectPlugins.clear();
    restoringProjectPlugins = false;
    refreshDeviceView();
    if (missingProjectPlugins > 0)
        setProjectStatus ("Projeto aberto; " + juce::String (missingProjectPlugins)
                          + " plugin(s) nao encontrados ou nao carregados.");
    advanceProjectLoadTask();
}

void MainComponent::loadProjectAudio (const ProjectFileData& data)
{
    std::set<std::string> paths;
    for (const auto& track : project.tracks)
        if (track->type == roll::TrackType::Audio)
            for (const auto& clip : track->audioClips)
                paths.insert (clip->filePath);

    if (paths.empty())
    {
        if (! data.missingAudioFiles.empty())
            setProjectStatus ("Projeto aberto com arquivos de audio ausentes.");
        return;
    }

    const int generation = projectGeneration;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    for (const auto& path : paths)
    {
        const juce::File file (juce::String::fromUTF8 (path.c_str()));
        ++pendingAudioLoads;
        updateActionIndicator();
        engine.loadAudioFileAsync (file, [this, token, generation, path]
            (const juce::String& error, std::shared_ptr<const AudioFileData> data)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load()) return;

            pendingAudioLoads = std::max (0, pendingAudioLoads - 1);
            updateActionIndicator();
            if (generation != projectGeneration)
                return;
            if (error.isNotEmpty() || data == nullptr)
            {
                setProjectStatus ("Audio ausente ou invalido: " + juce::File (
                    juce::String::fromUTF8 (path.c_str())).getFileName());
                ++projectLoadWorkDone;
                advanceProjectLoadTask();
                return;
            }

            audioAssets[path] = data;
            for (const auto& track : project.tracks)
                if (track->type == roll::TrackType::Audio)
                {
                    bool changed = false;
                    for (auto& clip : track->audioClips)
                        if (clip->filePath == path)
                        {
                            clip->waveform = data->waveform;
                            clip->durationSeconds = data->durationSeconds;
                            changed = true;
                        }
                    if (changed && project.onAudioClipsChanged)
                        project.onAudioClipsChanged (track->id);
                }
            refreshAudioClipEditor();
            project.sendChangeMessage();
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
        });
    }
}

void MainComponent::setProjectStatus (const juce::String& message)
{
    browser.setStatus (message);
    audioBrowser.setStatus (message);
}

void MainComponent::onSelectionChanged()
{
    auto* p = project.selectedClip();
    const auto* selectedTrack = project.selectedTrack();
    const auto* audio = selectedTrack != nullptr
        ? selectedTrack->findAudioClip (project.selectedAudioClipId) : nullptr;
    roll::Clip* target = p != nullptr ? &p->clip : nullptr;
    const bool changed = (target != (model.hasClip() ? &model.clip() : nullptr));

    model.clipStartBeats = p != nullptr ? p->start : 0.0;
    if (changed) model.setClip (target);

    if (p != nullptr)
        lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
    lengthBox.setEnabled (p != nullptr);

    if (project.selectedAudioClipId < 0)
        audioClipEditorOpen = false;
    if (audio != nullptr)
    {
        transportPosition = audio->start;
        engine.graph().seek (transportPosition);
        arrangement.setPlayhead (transportPosition);
    }
    refreshAudioClipEditor();
    showDetail (showingDevices);
    refreshDeviceView();
}

void MainComponent::openAudioClipEditor (int trackId, int clipId)
{
    project.selectAudioClip (trackId, clipId);
    audioClipEditorOpen = true;
    refreshAudioClipEditor();
    showDetail (false);
    resized();
}

void MainComponent::refreshAudioClipEditor()
{
    const auto* track = project.selectedTrack();
    const auto* clip = track != nullptr ? track->findAudioClip (project.selectedAudioClipId) : nullptr;
    std::shared_ptr<const AudioFileData> source;
    if (clip != nullptr)
        if (const auto asset = audioAssets.find (clip->filePath); asset != audioAssets.end())
            source = asset->second;
    audioClipEditor.setClip (clip, std::move (source), engine.graph().getBpm(), arrangement.gridBeats());
}

void MainComponent::refreshDeviceView()
{
    if (auto* t = project.selectedTrack())
        deviceView.setDevices (t->id, juce::String (t->name), engine.devices (t->id));
}

void MainComponent::applyScale()
{
    model.scale = roll::Scale (rootBox.getSelectedId() - 1, static_cast<roll::Mode> (modeBox.getSelectedId() - 1));
    model.rebuildRows();
    model.sendChangeMessage();
}

void MainComponent::applyLength()
{
    auto* t = project.selectedTrack();
    auto* p = project.selectedClip();
    if (t == nullptr || p == nullptr) return;

    const double newLen = lengthBox.getSelectedId() * 4.0;
    if (! t->isFree (p->start, newLen, p->id))
    {
        lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
        return;                                           // bateria em outro clip: não deixa
    }
    p->clip.setLength (newLen);
    project.growTo (p->end() + 64.0);
    project.notifyTrackNotes (t->id);
    project.sendChangeMessage();
    model.sendChangeMessage();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &model)
    {
        gridLabel.setText (model.gridLabel() + (model.drawMode ? "   (desenho)" : ""), juce::dontSendNotification);
        drawButton.setToggleState (model.drawMode, juce::dontSendNotification);
        snapButton.setToggleState (model.snap, juce::dontSendNotification);
        highlightButton.setToggleState (model.highlight, juce::dontSendNotification);
    }
    else if (src == &project)
    {
        refreshAudioClipEditor();
        model.clipStartBeats = project.selectedClip() != nullptr ? project.selectedClip()->start : 0.0;
        if (auto* p = project.selectedClip())
            lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
        model.sendChangeMessage();                         // o clip pode ter mudado de tamanho
    }
}

void MainComponent::timerCallback()
{
    updateComputerMidiKeyboard();

    std::uint64_t lo, hi;
    engine.activeMask (project.selectedTrackId, lo, hi);
    for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
    {
        if (! computerMidiNoteActive[i] || computerMidiTrackIds[i] != project.selectedTrackId)
            continue;

        const int pitch = computerMidiKeyPitch[i];
        if (pitch < 64)
            lo |= std::uint64_t { 1 } << pitch;
        else
            hi |= std::uint64_t { 1 } << (pitch - 64);
    }
    if (lo != lastLo || hi != lastHi)                     // teclas acendem com as notas tocando
    {
        lastLo = lo; lastHi = hi;
        model.activeLo = lo; model.activeHi = hi;
        editor.repaintKeys();
    }
    updateActionIndicator();
}

void MainComponent::setComputerMidiKeyboardEnabled (bool enabled)
{
    if (computerMidiKeyboardEnabled == enabled)
        return;

    computerMidiKeyboardEnabled = enabled;
    transport.setComputerMidiKeyboardEnabled (enabled);
    if (! enabled)
    {
        releaseComputerMidiNotes();
        hideComputerMidiStatus();
    }
    else
    {
        showComputerMidiStatus ("Teclado MIDI ativo",
            "Vel " + juce::String (computerMidiVelocity) + " | Oitava "
                + (computerMidiOctaveOffset > 0 ? "+" : "") + juce::String (computerMidiOctaveOffset),
            false);
    }
    startTimerHz (enabled ? 60 : 30);
}

void MainComponent::releaseComputerMidiNotes()
{
    for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
    {
        if (computerMidiNoteActive[i])
            engine.previewNote (computerMidiTrackIds[i], computerMidiKeyPitch[i], false);

        computerMidiKeyDown[i] = false;
        computerMidiNoteActive[i] = false;
    }
}

void MainComponent::updateComputerMidiKeyboard()
{
    // O timer cobre também teclas focadas em outros painéis; keyStateChanged reduz a latência.
    const auto modifiers = juce::ModifierKeys::getCurrentModifiers();
    const bool hasShortcutModifier = modifiers.isCtrlDown() || modifiers.isCommandDown() || modifiers.isAltDown();
    const bool textFocused = isTextEditorFocused();
    const bool appForeground = juce::Process::isForegroundProcess();

    const bool modeKeyDown = appForeground && ! audioRenderInProgress && ! textFocused && ! hasShortcutModifier
                          && juce::KeyPress::isKeyCurrentlyDown ('M');
    if (modeKeyDown && ! computerMidiModeKeyDown)
        setComputerMidiKeyboardEnabled (! computerMidiKeyboardEnabled);
    computerMidiModeKeyDown = modeKeyDown;

    const bool acceptKeys = computerMidiKeyboardEnabled && appForeground && ! audioRenderInProgress
                         && ! textFocused && ! hasShortcutModifier;
    // Z/X mudam a oitava e C/V ajustam a velocity em passos de 20, como no Live.
    const std::array<int, 4> controlKeys { 'Z', 'X', 'C', 'V' };
    bool statusChanged = false;
    bool noteChanged = false;
    for (std::size_t i = 0; i < controlKeys.size(); ++i)
    {
        const bool down = acceptKeys && juce::KeyPress::isKeyCurrentlyDown (controlKeys[i]);
        if (down && ! computerMidiControlKeyDown[i])
        {
            const int oldOctave = computerMidiOctaveOffset;
            const int oldVelocity = computerMidiVelocity;
            if (i == 0) computerMidiOctaveOffset = std::max (-5, computerMidiOctaveOffset - 1);
            if (i == 1) computerMidiOctaveOffset = std::min (5, computerMidiOctaveOffset + 1);
            if (i == 2) computerMidiVelocity = std::max (20, computerMidiVelocity - 20);
            if (i == 3) computerMidiVelocity = std::min (120, computerMidiVelocity + 20);
            statusChanged |= oldOctave != computerMidiOctaveOffset || oldVelocity != computerMidiVelocity;
        }
        computerMidiControlKeyDown[i] = down;
    }

    const auto* selectedTrack = project.track (project.selectedTrackId);
    const int midiTrackId = selectedTrack != nullptr && selectedTrack->type == roll::TrackType::Midi
                          ? selectedTrack->id : -1;

    for (std::size_t i = 0; i < computerMidiKeyboardMap.size(); ++i)
    {
        const auto [keyCode, semitone] = computerMidiKeyboardMap[i];
        const bool down = acceptKeys && juce::KeyPress::isKeyCurrentlyDown (keyCode);

        if (down && ! computerMidiKeyDown[i] && midiTrackId >= 0)
        {
            const int pitch = 60 + semitone + computerMidiOctaveOffset * 12;
            if (pitch >= 0 && pitch <= 127)
            {
                engine.previewNote (midiTrackId, pitch, true,
                                    static_cast<float> (computerMidiVelocity) / 127.0f);
                computerMidiKeyPitch[i] = pitch;
                computerMidiTrackIds[i] = midiTrackId;
                computerMidiNoteActive[i] = true;
                lastComputerMidiNote = midiNoteName (pitch);
                statusChanged = noteChanged = true;
            }
        }
        else if (! down && computerMidiKeyDown[i] && computerMidiNoteActive[i])
        {
            engine.previewNote (computerMidiTrackIds[i], computerMidiKeyPitch[i], false);
            computerMidiNoteActive[i] = false;
            lastComputerMidiNote = midiNoteName (computerMidiKeyPitch[i]);
            statusChanged = noteChanged = true;
        }

        computerMidiKeyDown[i] = down;
    }

    if (statusChanged)
    {
        juce::StringArray activeNotes;
        for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
            if (computerMidiNoteActive[i])
                activeNotes.add (midiNoteName (computerMidiKeyPitch[i]));

        const auto primary = activeNotes.isEmpty()
            ? (noteChanged ? "Soltou " + lastComputerMidiNote : "Teclado MIDI")
            : activeNotes.joinIntoString (" + ");
        const auto details = "Vel " + juce::String (computerMidiVelocity) + " | Oitava "
                           + (computerMidiOctaveOffset > 0 ? "+" : "")
                           + juce::String (computerMidiOctaveOffset);
        showComputerMidiStatus (primary, details, ! activeNotes.isEmpty());
    }
}

void MainComponent::showComputerMidiStatus (const juce::String& primary,
                                           const juce::String& secondary,
                                           bool noteHeld)
{
    computerMidiIndicator->showStatus (primary, secondary, noteHeld);
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->showComputerMidiStatus (primary, secondary, noteHeld);
    }
}

void MainComponent::hideComputerMidiStatus()
{
    computerMidiIndicator->hide();
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->hideComputerMidiStatus();
    }
}

void MainComponent::startSimulatedTask()
{
    if (taskSimulationThread.joinable())
    {
        taskSimulationThread.request_stop();
        taskSimulationThread.join();
    }

    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto* registry = &taskRegistry;
    taskSimulationThread = std::jthread ([registry, cancelled] (std::stop_token stopToken)
    {
        // A tarefa simulada roda fora da UI e publica apenas progresso no registry.
        TaskScope task (*registry, theme::ui ("Tarefa simulada"), "global", 0.0, true,
                        [cancelled] { cancelled->store (true, std::memory_order_relaxed); });
        constexpr int steps = 30;
        for (int step = 1; step <= steps; ++step)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            if (stopToken.stop_requested() || cancelled->load (std::memory_order_relaxed))
                break;
            task.setProgress (static_cast<double> (step) / steps);
        }
    });
}

// ------------------------------------------------------------ dispositivos

void MainComponent::duplicateSelectedTrack (int trackId)
{
    const auto* source = project.track (trackId);
    if (source == nullptr || source->type == roll::TrackType::Group)
        return;
    if (trackDuplicationInProgress || scanningPlugins || restoringProjectPlugins
        || projectFileOperationInProgress || audioRenderInProgress || pendingAudioLoads > 0)
    {
        setProjectStatus ("Aguarde a operacao em andamento antes de duplicar a faixa.");
        return;
    }

    auto pluginStates = engine.getPluginStates();
    std::erase_if (pluginStates, [trackId] (const PluginState& state) { return state.trackId != trackId; });
    const auto duplicateId = project.duplicateTrack (trackId);
    if (duplicateId < 0)
        return;

    if (pluginStates.empty())
    {
        setProjectStatus ("Faixa duplicada.");
        return;
    }

    struct PluginCopyOperation
    {
        int targetTrackId = -1;
        size_t completed = 0;
        int failures = 0;
        std::vector<PluginState> plugins;
        std::map<int, std::vector<int>> groups;
        std::shared_ptr<TaskScope> task;
    };

    auto operation = std::make_shared<PluginCopyOperation>();
    operation->targetTrackId = duplicateId;
    operation->plugins = std::move (pluginStates);
    operation->task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Copiando dispositivos da faixa"), "track:" + juce::String (duplicateId), 0.0);
    trackDuplicationInProgress = true;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;

    auto copyNextPlugin = [this, token, operation] (auto&& copyNextPlugin, size_t index) -> void
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;
        if (project.track (operation->targetTrackId) == nullptr)
        {
            trackDuplicationInProgress = false;
            operation->task->end();
            setProjectStatus ("A faixa duplicada foi removida antes da copia dos dispositivos.");
            return;
        }
        if (index >= operation->plugins.size())
        {
            for (const auto& [groupId, devices] : operation->groups)
            {
                juce::ignoreUnused (groupId);
                if (devices.size() >= 2 && ! engine.groupDevices (operation->targetTrackId, devices))
                    ++operation->failures;
            }
            trackDuplicationInProgress = false;
            operation->task->setProgress (1.0);
            operation->task->end();
            refreshDeviceView();
            project.sendChangeMessage();
            setProjectStatus (operation->failures == 0
                ? "Faixa e dispositivos duplicados."
                : "Faixa duplicada; " + juce::String (operation->failures)
                    + " dispositivo(s) nao puderam ser copiados.");
            return;
        }

        const auto state = operation->plugins[index];
        auto description = scanner.list().getTypeForIdentifierString (state.identifier);
        if (description == nullptr)
        {
            ++operation->failures;
            ++operation->completed;
            operation->task->setProgress (static_cast<double> (operation->completed)
                                            / operation->plugins.size());
            copyNextPlugin (copyNextPlugin, index + 1);
            return;
        }

        engine.addDevice (operation->targetTrackId, *description,
            [this, token, operation, state, index, copyNextPlugin] (
                const juce::String& error, int deviceId) mutable
            {
                const auto callbackAlive = token.lock();
                if (callbackAlive == nullptr || ! callbackAlive->load())
                    return;
                if (error.isNotEmpty())
                    ++operation->failures;
                else
                {
                    if (! engine.restorePluginState (operation->targetTrackId, deviceId, state.state))
                        ++operation->failures;
                    engine.setDeviceBypassed (operation->targetTrackId, deviceId, state.bypassed);
                    if (state.groupId > 0)
                        operation->groups[state.groupId].push_back (deviceId);
                }

                ++operation->completed;
                operation->task->setProgress (static_cast<double> (operation->completed)
                                                / operation->plugins.size());
                copyNextPlugin (copyNextPlugin, index + 1);
            }, state.order);
    };
    copyNextPlugin (copyNextPlugin, 0);
}

void MainComponent::loadDevice (const juce::PluginDescription& desc, int targetTrackId, int insertionIndex)
{
    auto* t = targetTrackId >= 0 ? project.track (targetTrackId) : project.selectedTrack();
    if (t == nullptr) return;
    const int trackId = t->id;
    if (! engine.canAcceptPlugin (trackId, desc))
    {
        browser.setStatus (trackId == 0 ? theme::ui ("A trilha Master aceita apenas efeitos.")
                                        : theme::ui ("Canais de audio aceitam apenas efeitos."));
        return;
    }

    const juce::String pluginIdentifier = desc.createIdentifierString();
    const auto existingDevices = engine.devices (trackId);
    if (const auto existing = std::find_if (existingDevices.begin(), existingDevices.end(),
            [&pluginIdentifier] (const DeviceInfo& device)
            {
                return device.pluginIdentifier == pluginIdentifier;
            });
        existing != existingDevices.end())
    {
        browser.setStatus (theme::ui ("Este plugin ja esta carregado nesta trilha."));
        openDeviceEditor (trackId, existing->id);
        return;
    }

    if (desc.isInstrument)                                // o instrumento atual será substituído: fecha a UI dele
        for (const auto& d : existingDevices)
            if (d.isInstrument) windows.erase (d.id);

    // Use the same slot index that AudioEngine will use when it inserts the plugin.
    int pluginSlot = 0;
    if (! desc.isInstrument)
    {
        const int firstEffect = ! existingDevices.empty() && existingDevices.front().isInstrument ? 1 : 0;
        pluginSlot = insertionIndex < 0
            ? static_cast<int> (existingDevices.size())
            : std::clamp (insertionIndex, firstEffect, static_cast<int> (existingDevices.size()));
    }

    const auto target = "track:" + juce::String (trackId) + ":plugin:" + juce::String (pluginSlot);
    // Keep the scope alive in the async callback so every success/error path ends the task.
    auto pluginTask = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Carregando ") + desc.name, target);
    browser.setStatus ("Carregando " + desc.name + "...");
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    engine.addDevice (trackId, desc,
        [this, token, pluginTask, trackId] (const juce::String& error, int deviceId)
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;

        if (error.isNotEmpty())
        {
            browser.setStatus ("Erro: " + error);
            return;
        }
        browser.setStatus (juce::String (scanner.list().getNumTypes()) + " plugins encontrados");
        refreshDeviceView();
        project.sendChangeMessage();
        openDeviceEditor (trackId, deviceId);
    }, insertionIndex);
}

void MainComponent::updateActionIndicator()
{
    if (actionIndicator == nullptr)
        return;

    if (pendingAudioLoads > 0)
    {
        const auto text = pendingAudioLoads == 1
            ? "Carregando audio..."
            : "Carregando " + juce::String (pendingAudioLoads) + " arquivos de audio...";
        actionIndicator->showMessage (text, icons::Icon::Waveform, true);
    }
    else if (pendingPreviewLoads > 0)
    {
        actionIndicator->showMessage ("Carregando preview de audio...", icons::Icon::Waveform, true);
    }
    else if (restoringProjectPlugins)
    {
        actionIndicator->showMessage ("Restaurando plugins do projeto...", icons::Icon::Mixer, true);
    }
    else
    {
        actionIndicator->hide();
    }
}

void MainComponent::openDeviceEditor (int trackId, int deviceId)
{
    auto it = windows.find (deviceId);
    if (it != windows.end())
    {
        it->second->setMinimised (false);
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }

    if (auto ed = engine.createDeviceEditor (trackId, deviceId))
    {
        juce::String title;
        for (const auto& d : engine.devices (trackId)) if (d.id == deviceId) title = d.name;
        if (auto* t = project.track (trackId)) title += "  -  " + juce::String (t->name);

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getDisplayForRect (getScreenBounds());
        if (display == nullptr)
            display = displays.getPrimaryDisplay();

        const auto displayArea = display != nullptr ? display->userBounds.toNearestInt()
                                                    : juce::Rectangle<int> (0, 0, 1280, 720);
        std::vector<roll::Rect> occupiedWindows;
        for (const auto& [openDeviceId, window] : windows)
        {
            juce::ignoreUnused (openDeviceId);
            if (window != nullptr && window->isVisible())
            {
                const auto bounds = window->getScreenBounds();
                occupiedWindows.push_back ({ bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() });
            }
        }
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        auto window = std::make_unique<PluginWindow> (
            title, std::move (ed), displayArea, nextPluginCascadeIndex++, occupiedWindows,
            [this, token]
            {
                const auto alive = token.lock();
                if (alive != nullptr && alive->load())
                    updatePluginWindowZOrder();
            },
            [this, token] (const juce::KeyPress& key)
            {
                const auto alive = token.lock();
                return alive != nullptr && alive->load() && handleTransportKeyPress (key);
            });
        windows[deviceId] = std::move (window);
        if (computerMidiIndicator->isVisible())
            windows[deviceId]->showComputerMidiStatus (
                computerMidiIndicator->getPrimaryText(),
                computerMidiIndicator->getSecondaryText(),
                computerMidiIndicator->isHeld());
        windows[deviceId]->toFront (true);
        updatePluginWindowZOrder();
    }
    else
    {
        juce::String pluginName;
        for (const auto& device : engine.devices (trackId))
            if (device.id == deviceId) { pluginName = device.name; break; }

        browser.setStatus ("Falha ao abrir interface de " + (pluginName.isNotEmpty() ? pluginName : "plugin") + ".");
    }
}

void MainComponent::closeWindowsOfTrack (int trackId)
{
    for (const auto& d : engine.devices (trackId)) windows.erase (d.id);
}
