#include "ArrangementView.h"
#include "Icons.h"
#include "WaveformDisplay.h"
#include "ZoomCursor.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

juce::Colour ArrangementView::colourFor (int trackId)
{
    return juce::Colour::fromHSV (std::fmod (static_cast<float> (trackId) * 0.137f + 0.45f, 1.0f), 0.45f, 0.78f, 1.0f);
}

// ============================================================ Lanes (clips)

class ArrangementView::Lanes : public juce::Component, public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    enum class Mode { None, Move, Resize, FadeIn, FadeOut, FadeInCurve, FadeOutCurve,
                      RangeSelect, EmptyClickPending, ShiftPending, CtrlPending };

    Lanes (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    void timerCallback() override
    {
        if (mode == Mode::FadeIn || mode == Mode::FadeOut
            || mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve
            || mode == Mode::RangeSelect)
            repaint();
    }

    void updateSize()
    {
        setSize (std::max (1, static_cast<int> (project.totalBeats * owner.pixelsPerBeat)),
                 std::max (1, static_cast<int> (project.tracks.size() + 1) * trackHeight));
    }

    void focusGained (FocusChangeType) override { owner.repaint(); }
    void focusLost (FocusChangeType) override   { owner.repaint(); }

    void setPlayhead (double beats)
    {
        const int oldX = static_cast<int> (owner.playhead * owner.pixelsPerBeat);
        owner.playhead = beats;
        repaint (oldX - 2, 0, 5, getHeight());
        repaint (static_cast<int> (beats * owner.pixelsPerBeat) - 2, 0, 5, getHeight());
    }

    void clearSelection()
    {
        selectedClips.clear();
        selectionAnchor = {};
        hasRangeSelection = false;
        repaint();
    }

    bool getTimeSelectionStart (double& startBeat) const
    {
        if (! hasRangeSelection || std::abs (rangeEndBeat - rangeStartBeat) < 1.0e-9)
            return false;

        startBeat = std::min (rangeStartBeat, rangeEndBeat);
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        const auto cb = g.getClipBounds();
        const double ppb = owner.pixelsPerBeat;

        const size_t totalRows = project.tracks.size() + 1;
        for (size_t i = 0; i < totalRows; ++i)
        {
            if (i == project.tracks.size())
            {
                g.setColour (theme::panel().darker (0.2f));
                g.fillRect (cb.getX(), static_cast<int> (i) * trackHeight, cb.getWidth(), trackHeight);
                g.setColour (theme::textDim().withAlpha (0.45f));
                g.setFont (juce::FontOptions (12.5f));
                g.drawText (theme::ui ("Master Bus  (Saida Estereo 1/2)"), cb.getX() + 12, static_cast<int> (i) * trackHeight + 4, 300, 20, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour ((i % 2 == 0) ? theme::rowWhite() : theme::rowBlack());
                g.fillRect (cb.getX(), static_cast<int> (i) * trackHeight, cb.getWidth(), trackHeight);
                if (project.isTrackSelected (project.tracks[i]->id))
                {
                    g.setColour (theme::accent().withAlpha (0.10f));
                    g.fillRect (cb.getX(), static_cast<int> (i) * trackHeight, cb.getWidth(), trackHeight);
                }
            }
            g.setColour (theme::border());
            g.drawHorizontalLine ((static_cast<int> (i) + 1) * trackHeight - 1, static_cast<float> (cb.getX()), static_cast<float> (cb.getRight()));
        }

        // região de loop
        const float lx0 = static_cast<float> (project.loopStart * ppb), lx1 = static_cast<float> (project.loopEnd * ppb);
        g.setColour (theme::accent().withAlpha (owner.loopEnabled ? 0.07f : 0.03f));
        g.fillRect (lx0, 0.0f, lx1 - lx0, static_cast<float> (getHeight()));

        // linhas de compasso/beat
        const int firstBeat = std::max (0, static_cast<int> (std::floor (cb.getX() / ppb)));
        const int lastBeat = static_cast<int> (std::ceil (cb.getRight() / ppb));
        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const bool bar = (b % 4 == 0);
            if (! bar && ppb < 14.0) continue;
            g.setColour (bar ? theme::border().brighter (0.5f) : theme::border().withAlpha (0.5f));
            g.drawVerticalLine (static_cast<int> (b * ppb), static_cast<float> (cb.getY()), static_cast<float> (cb.getBottom()));
        }
        const double grid = owner.gridBeats();
        if (grid < 1.0 && grid * ppb >= 2.0)
        {
            const int firstGrid = std::max (0, static_cast<int> (std::floor (cb.getX() / (grid * ppb))));
            const int lastGrid = static_cast<int> (std::ceil (cb.getRight() / (grid * ppb)));
            for (int i = firstGrid; i <= lastGrid; ++i)
            {
                const double beat = i * grid;
                if (std::abs (std::remainder (beat, 1.0)) < 1.0e-6)
                    continue;
                g.setColour (theme::border().withAlpha (0.25f));
                g.drawVerticalLine (static_cast<int> (beat * ppb), static_cast<float> (cb.getY()),
                                    static_cast<float> (cb.getBottom()));
            }
        }

        // clips
        for (size_t i = 0; i < project.tracks.size(); ++i)
        {
            const auto& t = *project.tracks[i];
            const auto base = colourFor (t.id);
            if (t.type == roll::TrackType::Audio)
            {
                for (const auto& clip : t.audioClips)
                {
                    juce::Rectangle<float> r (static_cast<float> (clip->start * ppb), static_cast<float> (i) * trackHeight + 4.0f,
                                              static_cast<float> (clip->lengthBeats * ppb), static_cast<float> (trackHeight) - 9.0f);
                    if (! r.toNearestInt().intersects (cb)) continue;

                    const bool inRange = hasRangeSelection && static_cast<int> (i) >= std::min (rangeStartTrack, rangeEndTrack)
                        && static_cast<int> (i) <= std::max (rangeStartTrack, rangeEndTrack)
                        && clip->start >= std::min (rangeStartBeat, rangeEndBeat) - 1.0e-9
                        && clip->end() <= std::max (rangeStartBeat, rangeEndBeat) + 1.0e-9;
                    const bool selected = isClipSelected (t.id, clip->id, true) || inRange;
                    g.setColour (selected ? base.brighter (0.25f) : base);
                    g.fillRoundedRectangle (r, 4.0f);
                    g.saveState();
                    g.reduceClipRegion (r.toNearestInt());
                    g.setColour (juce::Colours::black.withAlpha (0.28f));
                    g.fillRect (r.withHeight (14.0f));
                    g.setColour (juce::Colours::black.withAlpha (0.8f));
                    g.setFont (juce::FontOptions (12.5f));
                    g.drawText (clip->name, r.toNearestInt().withTrimmedLeft (5).withHeight (14),
                                juce::Justification::centredLeft, true);

                    const auto waveformArea = r.withY (r.getY() + 15.0f).withHeight (r.getHeight() - 15.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.3f));
                    g.fillRect (waveformArea);
                    const float halfHeight = 24.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.24f));
                    g.drawHorizontalLine (static_cast<int> (waveformArea.getCentreY()),
                                          waveformArea.getX(), waveformArea.getRight());
                    drawWaveformEnvelope (g, waveformArea, clip->waveform, base.brighter (0.7f), halfHeight);

                    const float fadeTop = waveformArea.getY() + 3.0f;
                    const float fadeBottom = waveformArea.getBottom() - 3.0f;
                    const float fadeHandleInset = std::min (3.0f, r.getWidth() * 0.5f);
                    const float fadeInX = std::clamp (r.getX() + static_cast<float> (clip->fadeInBeats * ppb),
                                                      r.getX() + fadeHandleInset, r.getRight() - fadeHandleInset);
                    const float fadeOutX = std::clamp (r.getRight() - static_cast<float> (clip->fadeOutBeats * ppb),
                                                       r.getX() + fadeHandleInset, r.getRight() - fadeHandleInset);
                    const bool editingFadeIn = mode == Mode::FadeIn
                                            && curTrack == t.id && curAudioClipId == clip->id;
                    const bool editingFadeOut = mode == Mode::FadeOut
                                             && curTrack == t.id && curAudioClipId == clip->id;
                    const float pulse = 0.65f + 0.35f * std::sin (
                        static_cast<float> (juce::Time::getMillisecondCounterHiRes() * 0.008));
                    g.setColour (theme::accent().brighter (0.5f).withAlpha (
                        editingFadeIn || editingFadeOut ? pulse : 0.9f));
                    juce::Path fadeInPath;
                    fadeInPath.startNewSubPath (r.getX(), fadeBottom);
                    juce::Path fadeOutPath;
                    fadeOutPath.startNewSubPath (fadeOutX, fadeTop);
                    constexpr int curveSteps = 24;
                    for (int step = 1; step <= curveSteps; ++step)
                    {
                        const float amount = static_cast<float> (step) / curveSteps;
                        const float fadeInGain = static_cast<float> (
                            roll::audioFadeCurveValue (amount, clip->fadeInCurve));
                        const float fadeOutGain = static_cast<float> (
                            roll::audioFadeCurveValue (amount, clip->fadeOutCurve));
                        fadeInPath.lineTo (r.getX() + (fadeInX - r.getX()) * amount,
                                           fadeBottom - (fadeBottom - fadeTop) * fadeInGain);
                        fadeOutPath.lineTo (fadeOutX + (r.getRight() - fadeOutX) * amount,
                                            fadeTop + (fadeBottom - fadeTop) * fadeOutGain);
                    }
                    const bool editingCurveIn = mode == Mode::FadeInCurve
                                             && curTrack == t.id && curAudioClipId == clip->id;
                    const bool editingCurveOut = mode == Mode::FadeOutCurve
                                              && curTrack == t.id && curAudioClipId == clip->id;
                    g.strokePath (fadeInPath, juce::PathStrokeType (
                        editingFadeIn || editingCurveIn ? 2.8f : 2.0f));
                    g.strokePath (fadeOutPath, juce::PathStrokeType (
                        editingFadeOut || editingCurveOut ? 2.8f : 2.0f));
                    const float inRadius = editingFadeIn ? 3.0f + pulse : 3.0f;
                    const float outRadius = editingFadeOut ? 3.0f + pulse : 3.0f;
                    g.fillEllipse (fadeInX - inRadius, fadeTop - inRadius, inRadius * 2.0f, inRadius * 2.0f);
                    g.fillEllipse (fadeOutX - outRadius, fadeTop - outRadius, outRadius * 2.0f, outRadius * 2.0f);
                    constexpr float curveHandleProgress = 0.25f;
                    const float fadeInCurveX = r.getX() + (fadeInX - r.getX()) * curveHandleProgress;
                    const float fadeOutCurveX = fadeOutX + (r.getRight() - fadeOutX) * curveHandleProgress;
                    const float fadeInCurveY = fadeBottom - (fadeBottom - fadeTop) * static_cast<float> (
                        roll::audioFadeCurveValue (curveHandleProgress, clip->fadeInCurve));
                    const float fadeOutCurveY = fadeTop + (fadeBottom - fadeTop) * static_cast<float> (
                        roll::audioFadeCurveValue (curveHandleProgress, clip->fadeOutCurve));
                    g.setColour (theme::accent().brighter (0.4f));
                    if (clip->fadeInBeats > 0.02)
                        g.fillEllipse (fadeInCurveX - 4.0f, fadeInCurveY - 4.0f, 8.0f, 8.0f);
                    if (clip->fadeOutBeats > 0.02)
                        g.fillEllipse (fadeOutCurveX - 4.0f, fadeOutCurveY - 4.0f, 8.0f, 8.0f);
                    if (! clip->enabled)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.62f));
                        g.fillRect (r);
                        g.setColour (juce::Colours::white.withAlpha (0.9f));
                        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                        g.drawText (theme::ui ("Desativado"), r.toNearestInt().reduced (3),
                                    juce::Justification::centred, true);
                    }
                    g.restoreState();
                    if (selected)
                    {
                        g.setColour (juce::Colours::white);
                        g.drawRoundedRectangle (r, 4.0f, 1.6f);
                    }
                }
                continue;
            }

            for (const auto& p : t.clips)
            {
                juce::Rectangle<float> r (static_cast<float> (p->start * ppb), static_cast<float> (i) * trackHeight + 4.0f,
                                          static_cast<float> (p->clip.length() * ppb), static_cast<float> (trackHeight) - 9.0f);
                if (! r.toNearestInt().intersects (cb)) continue;

                const bool inRange = hasRangeSelection && static_cast<int> (i) >= std::min (rangeStartTrack, rangeEndTrack)
                    && static_cast<int> (i) <= std::max (rangeStartTrack, rangeEndTrack)
                    && p->start >= std::min (rangeStartBeat, rangeEndBeat) - 1.0e-9
                    && p->end() <= std::max (rangeStartBeat, rangeEndBeat) + 1.0e-9;
                const bool sel = isClipSelected (t.id, p->id, false) || inRange;
                g.setColour (sel ? base.brighter (0.25f) : base);
                g.fillRoundedRectangle (r, 4.0f);

                g.saveState();
                g.reduceClipRegion (r.toNearestInt());
                g.setColour (juce::Colours::black.withAlpha (0.28f));
                g.fillRect (r.withHeight (14.0f));
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.setFont (juce::FontOptions (12.5f));
                g.drawText (p->name.empty() ? juce::String ("Clip") : juce::String (p->name), r.toNearestInt().withTrimmedLeft (5).withHeight (14),
                            juce::Justification::centredLeft, true);

                const auto& notes = p->clip.notes();
                if (! notes.empty())
                {
                    int lo = 127, hi = 0;
                    for (const auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                    const int range = std::max (hi - lo, 11) + 1;
                    const auto area = r.reduced (2.0f).withTrimmedTop (14.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.7f));
                    for (const auto& n : notes)
                    {
                        const float y = area.getBottom() - (static_cast<float> (n.pitch - lo) + 1.0f) / static_cast<float> (range) * area.getHeight();
                        g.fillRect (r.getX() + static_cast<float> (n.startBeats * ppb), y,
                                    std::max (1.5f, static_cast<float> (n.lengthBeats * ppb)), std::max (1.5f, area.getHeight() / static_cast<float> (range)));
                    }
                }
                if (! p->enabled)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.62f));
                    g.fillRect (r);
                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                    g.drawText (theme::ui ("Desativado"), r.toNearestInt().reduced (3),
                                juce::Justification::centred, true);
                }
                g.restoreState();

                if (sel)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (r, 4.0f, 1.6f);
                }
            }
        }

        if (hasRangeSelection)
        {
            const double startBeat = std::min (rangeStartBeat, rangeEndBeat);
            const double endBeat = std::max (rangeStartBeat, rangeEndBeat);
            const int firstTrack = std::min (rangeStartTrack, rangeEndTrack);
            const int lastTrack = std::max (rangeStartTrack, rangeEndTrack);
            const float x = static_cast<float> (startBeat * ppb);
            const float width = std::max (1.0f, static_cast<float> ((endBeat - startBeat) * ppb));
            const float y = static_cast<float> (firstTrack * trackHeight);
            const float height = static_cast<float> ((lastTrack - firstTrack + 1) * trackHeight);
            const bool selecting = mode == Mode::RangeSelect;
            const float pulse = selecting
                ? 0.45f + 0.2f * std::sin (static_cast<float> (juce::Time::getMillisecondCounterHiRes() * 0.006))
                : 0.45f;
            const auto selectionBounds = juce::Rectangle<float> (x, y, width, height);
            g.setColour (theme::accent().withAlpha (selecting ? 0.14f : 0.09f));
            g.fillRect (selectionBounds);
            g.setColour (theme::accent().withAlpha (pulse));
            g.drawRect (selectionBounds, selecting ? 2.0f : 1.0f);
            g.drawVerticalLine (static_cast<int> (x), y, y + height);
            g.drawVerticalLine (static_cast<int> (x + width), y, y + height);
            if (! selecting)
            {
                const auto hint = juce::Rectangle<int> (static_cast<int> (x) + 6, static_cast<int> (y) + 5, 245, 18);
                g.setColour (theme::panel().withAlpha (0.9f));
                g.fillRoundedRectangle (hint.toFloat(), 3.0f);
                g.setColour (theme::text());
                g.setFont (juce::FontOptions (11.5f));
                g.drawText ("Ctrl+D duplicar | Delete apagar | Ctrl+J consolidar",
                            hint.reduced (4, 0), juce::Justification::centredLeft, true);
            }
        }

        g.setColour (theme::playhead());
        g.drawVerticalLine (static_cast<int> (owner.playhead * ppb), 0.0f, static_cast<float> (getHeight()));

        if (dragTrackIndex >= 0)
        {
            const auto colour = dragDropAllowed ? theme::accent() : juce::Colours::red;
            const int y = dragTrackIndex * trackHeight;
            g.setColour (colour.withAlpha (0.18f));
            g.fillRect (0, y, getWidth(), trackHeight);
            g.setColour (colour);
            g.drawRect (0, y, getWidth(), trackHeight, 2);

            if (dragDropAllowed)
            {
                const int markerX = static_cast<int> (dragBeat * ppb);
                g.drawVerticalLine (markerX, static_cast<float> (y), static_cast<float> (y + trackHeight));
            }

            const bool masterTarget = dragTrackIndex == static_cast<int> (project.tracks.size());
            const juce::String message = dragDropAllowed
                ? (masterTarget ? "Solte efeito para adicionar ao Master"
                                : dragDropIsAudio ? "Solte para adicionar audio" : "Solte para adicionar plugin")
                : (masterTarget ? "Master aceita apenas efeitos"
                                : dragDropIsAudio ? "Audio indisponivel nesta trilha" : "Plugin indisponivel nesta trilha");
            const auto hint = juce::Rectangle<int> (8, y + 6, 220, 22);
            g.setColour (theme::panel().withAlpha (0.95f));
            g.fillRoundedRectangle (hint.toFloat(), 5.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (hint.toFloat(), 5.0f, 1.0f);
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (message, hint.reduced (7, 0), juce::Justification::centredLeft, true);
        }

        drawActionHint (g);
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        // JUCE checks interest before converting localPosition to this target's coordinates.
        const auto description = details.description.toString();
        return description.startsWith ("plugin:") || description.startsWith ("audio:");
    }

    void itemDragEnter (const SourceDetails& details) override
    {
        updateDragTarget (details);
        const auto description = details.description.toString();
        const bool audio = description.startsWith ("audio:");
        notifyAction (audio ? "Arrastando audio para o arrangement" : "Arrastando plugin para o arrangement",
                     audio ? icons::Icon::Waveform : icons::Icon::Mixer, true, 0);
    }
    void itemDragMove (const SourceDetails& details) override  { updateDragTarget (details); }
    void itemDragExit (const SourceDetails&) override
    {
        dragTrackIndex = -1;
        dragDropAllowed = false;
        dragDropIsAudio = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        notifyAction (juce::String(), icons::Icon::Pointer, false, 0);
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        const auto description = details.description.toString();
        const auto trackIndex = trackIndexAt (details.localPosition.y);
        dragTrackIndex = -1;
        dragDropAllowed = false;
        const bool audioDrop = description.startsWith ("audio:");
        dragDropIsAudio = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
        const auto trackId = trackIdAt (trackIndex);
        const auto beat = owner.snapBeat (details.localPosition.x / owner.pixelsPerBeat);
        if (audioDrop)
        {
            const juce::File file (description.substring (6));
            notifyAction ("Audio solto no arrangement", icons::Icon::Waveform, false, 650);
            if (owner.canAcceptAudioDrop && owner.canAcceptAudioDrop (file, trackId) && owner.onAudioDropped)
                owner.onAudioDropped (file, trackId, beat);
            return;
        }

        if (description.startsWith ("plugin:") && owner.canAcceptPluginDrop
            && owner.canAcceptPluginDrop (description.substring (7), trackId) && owner.onPluginDropped)
        {
            notifyAction ("Plugin solto no arrangement", icons::Icon::Mixer, false, 650);
            owner.onPluginDropped (description.substring (7), trackId, beat);
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && isAudioFile (juce::File (files[0]));
    }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        updateFileDragTarget (files, x, y);
        notifyAction ("Arrastando audio para o arrangement", icons::Icon::Waveform, true, 0);
    }
    void fileDragMove  (const juce::StringArray& files, int x, int y) override { updateFileDragTarget (files, x, y); }

    void fileDragExit (const juce::StringArray&) override
    {
        dragTrackIndex = -1;
        dragDropAllowed = false;
        dragDropIsAudio = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        notifyAction (juce::String(), icons::Icon::Pointer, false, 0);
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        const int trackIndex = trackIndexAt (y);
        if (files.size() != 1)
        {
            dragTrackIndex = -1;
            dragDropAllowed = false;
            dragDropIsAudio = false;
            notifyAction ("Solte um arquivo de audio", icons::Icon::Waveform, false, 650);
            repaint();
            return;
        }
        const juce::File file (files[0]);
        const int trackId = trackIdAt (trackIndex);
        notifyAction ("Audio solto no arrangement", icons::Icon::Waveform, false, 650);
        if (owner.canAcceptAudioDrop && owner.canAcceptAudioDrop (file, trackId) && owner.onAudioDropped)
            owner.onAudioDropped (file, trackId, owner.snapBeat (static_cast<double> (x) / owner.pixelsPerBeat));
        dragTrackIndex = -1;
        dragDropAllowed = false;
        dragDropIsAudio = false;
        repaint();
    }

    // ---- mouse ----
    struct Hit
    {
        int trackIndex = -1;
        roll::Placement* clip = nullptr;
        roll::AudioPlacement* audioClip = nullptr;
    };

    Hit hit (juce::Point<float> p) const
    {
        Hit h;
        h.trackIndex = static_cast<int> (std::floor (p.y / trackHeight));
        if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (project.tracks.size())) { h.trackIndex = -1; return h; }
        const auto* track = project.tracks[static_cast<size_t> (h.trackIndex)].get();
        const double beat = p.x / owner.pixelsPerBeat;
        if (track->type == roll::TrackType::Audio)
        {
            for (const auto& clip : track->audioClips)
                if (beat >= clip->start && beat < clip->end()) { h.audioClip = clip.get(); break; }
        }
        else
        {
            for (const auto& clip : track->clips)
                if (beat >= clip->start && beat < clip->end()) { h.clip = clip.get(); break; }
        }
        return h;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        mode = Mode::None; copied = false;

        const int row = static_cast<int> (std::floor (e.position.y / trackHeight));
        if (e.mods.isShiftDown() && e.mods.isLeftButtonDown()
            && row >= 0 && row < static_cast<int> (project.tracks.size()))
        {
            const auto clicked = hit (e.position);
            if (clicked.clip != nullptr || clicked.audioClip != nullptr)
            {
                pendingSelection = { project.tracks[static_cast<std::size_t> (clicked.trackIndex)]->id,
                                     clicked.audioClip != nullptr ? clicked.audioClip->id : clicked.clip->id,
                                     clicked.audioClip != nullptr };
                selectionDownPosition = e.position;
                mode = Mode::ShiftPending;
                notifyInteraction (true);
                return;
            }

            mode = Mode::RangeSelect;
            selectedClips.clear();
            selectionAnchor = {};
            const double rawBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = rangeEndBeat = e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat);
            rangeStartTrack = rangeEndTrack = row;
            hasRangeSelection = true;
            const int trackId = project.tracks[static_cast<std::size_t> (row)]->id;
            project.selectClip (trackId, -1);
            notifyInteraction (true);
            repaint();
            return;
        }
        hasRangeSelection = false;

        if (row == static_cast<int> (project.tracks.size()))
        {
            selectedClips.clear();
            selectionAnchor = {};
            project.selectTrack (0);
            notifyAction ("Barramento selecionado", icons::Icon::Pointer, false, 700);
            return;
        }

        const auto h = hit (e.position);
        if (h.trackIndex < 0) return;
        const int trackId = project.tracks[static_cast<size_t> (h.trackIndex)]->id;

        if (e.mods.isPopupMenu())
        {
            selectedClips.clear();
            selectionAnchor = {};
            if (h.audioClip != nullptr)
            {
                selectedClips.push_back ({ trackId, h.audioClip->id, true });
                selectionAnchor = selectedClips.back();
                project.selectAudioClip (trackId, h.audioClip->id);
            }
            else if (h.clip != nullptr)
            {
                selectedClips.push_back ({ trackId, h.clip->id, false });
                selectionAnchor = selectedClips.back();
                project.selectClip (trackId, h.clip->id);
            }
            else
                project.selectClip (trackId, -1);

            showContextMenu (h, trackId);
            notifyAction ("Menu do clipe", icons::Icon::Pointer, false, 700);
            return;
        }

        if (h.clip == nullptr && h.audioClip == nullptr)
        {
            selectedClips.clear();
            selectionAnchor = {};
            project.selectClip (trackId, -1);
            selectionDownPosition = e.position;
            mode = Mode::EmptyClickPending;
            notifyInteraction (true);
            return;
        }

        if (e.mods.isCtrlDown())
        {
            pendingSelection = { trackId,
                h.audioClip != nullptr ? h.audioClip->id : h.clip->id,
                h.audioClip != nullptr };
            selectionDownPosition = e.position;
            curTrack = trackId;
            if (h.audioClip != nullptr)
            {
                curClip = -1;
                curAudioClipId = h.audioClip->id;
                origStart = h.audioClip->start;
            }
            else
            {
                curClip = h.clip->id;
                curAudioClipId = -1;
                origStart = h.clip->start;
            }
            downBeat = e.position.x / owner.pixelsPerBeat;
            mode = Mode::CtrlPending;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            notifyInteraction (true);
            return;
        }

        selectedClips.clear();
        selectionAnchor = { trackId,
            h.audioClip != nullptr ? h.audioClip->id : h.clip->id,
            h.audioClip != nullptr };
        selectedClips.push_back (selectionAnchor);
        const double rawClickBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
        const double clickBeat = e.mods.isAltDown() ? rawClickBeat : owner.snapBeat (rawClickBeat);

        if (h.audioClip != nullptr)
        {
            project.selectAudioClip (trackId, h.audioClip->id);
            if (owner.onSeek)
                owner.onSeek (std::clamp (clickBeat,
                                          h.audioClip->start, h.audioClip->end()));
            curTrack = trackId; curClip = -1; curAudioClipId = h.audioClip->id;
            origStart = h.audioClip->start; origLen = h.audioClip->lengthBeats;
            origFadeIn = h.audioClip->fadeInBeats; origFadeOut = h.audioClip->fadeOutBeats;
            origFadeInCurve = h.audioClip->fadeInCurve;
            origFadeOutCurve = h.audioClip->fadeOutCurve;
        }
        else
        {
            project.selectClip (trackId, h.clip->id);
            if (owner.onSeek)
                owner.onSeek (std::clamp (clickBeat,
                                          h.clip->start, h.clip->end()));
            curTrack = trackId; curClip = h.clip->id; curAudioClipId = -1;
            origStart = h.clip->start; origLen = h.clip->clip.length();
        }
        downBeat = e.position.x / owner.pixelsPerBeat;
        downY = e.position.y;
        const float right = static_cast<float> ((h.audioClip != nullptr ? h.audioClip->end() : h.clip->end()) * owner.pixelsPerBeat);
        mode = fadeModeAt (h, e.position);
        if (mode == Mode::None)
            mode = (e.position.x > right - 7.0f) ? Mode::Resize : Mode::Move;
        if (mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve)
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor (mode == Mode::Resize || mode == Mode::FadeIn || mode == Mode::FadeOut
                                ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::DraggingHandCursor);
        notifyInteraction (true);
    }

    void showContextMenu (const Hit& h, int trackId)
    {
        juce::PopupMenu menu, gridMenu;
        const bool isAudio = h.audioClip != nullptr;
        const int clipId = isAudio ? h.audioClip->id : (h.clip != nullptr ? h.clip->id : -1);
        menu.addItem (6, theme::ui ("Selecionar tudo (Ctrl+A)"));
        menu.addSeparator();

        if (clipId >= 0)
        {
            menu.addItem (1, theme::ui ("Loopar clipe"));
            const auto* selectedTrack = project.track (trackId);
            const auto* audioClip = isAudio && selectedTrack != nullptr
                ? selectedTrack->findAudioClip (clipId) : nullptr;
            const auto* midiClip = ! isAudio && selectedTrack != nullptr
                ? selectedTrack->find (clipId) : nullptr;
            const bool enabled = isAudio ? (audioClip != nullptr && audioClip->enabled)
                                         : (midiClip != nullptr && midiClip->enabled);
            menu.addItem (4, theme::ui (enabled ? "Desativar clipe (0)" : "Ativar clipe (0)"));
            if (isAudio)
                menu.addItem (5, theme::ui ("Localizar no navegador"));
            menu.addItem (2, theme::ui ("Apagar clipe"));
            const double clipStart = isAudio ? (audioClip != nullptr ? audioClip->start : 0.0)
                                             : (midiClip != nullptr ? midiClip->start : 0.0);
            const double clipEnd = isAudio ? (audioClip != nullptr ? audioClip->end() : 0.0)
                                           : (midiClip != nullptr ? midiClip->end() : 0.0);
            const bool canSplit = owner.playhead > clipStart + 1.0e-9
                               && owner.playhead < clipEnd - 1.0e-9;
            menu.addItem (3, theme::ui ("Dividir no marcador (Ctrl+E)"), canSplit);
            menu.addSeparator();
        }

        static constexpr double gridValues[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                                2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                                1.0 / 12.0, 4.0 };
        static constexpr const char* gridLabels[] { "1/4", "1/8", "1/16", "1/32", "1/64",
                                                     "1/4T", "1/8T", "1/16T", "1/32T", "1 compasso" };
        for (int i = 0; i < 10; ++i)
            gridMenu.addItem (100 + i, theme::ui (gridLabels[i]), true,
                              std::abs (owner.gridBeats() - gridValues[i]) < 1.0e-9);
        gridMenu.addItem (110, theme::ui ("Sem grade"), true, owner.gridBeats() <= 0.0);
        menu.addSubMenu (theme::ui ("Grade de edicao"), gridMenu);

        juce::Component::SafePointer<Lanes> safeThis (this);
        const double splitBeat = owner.playhead;
        const auto* selectedTrack = project.track (trackId);
        const auto* currentAudioClip = isAudio && selectedTrack != nullptr
            ? selectedTrack->findAudioClip (clipId) : nullptr;
        const auto* currentMidiClip = ! isAudio && selectedTrack != nullptr
            ? selectedTrack->find (clipId) : nullptr;
        const bool enableClip = isAudio ? (currentAudioClip != nullptr && ! currentAudioClip->enabled)
                                        : (currentMidiClip != nullptr && ! currentMidiClip->enabled);
        const juce::File audioFile (currentAudioClip != nullptr
            ? juce::String (currentAudioClip->filePath) : juce::String());
        menu.showMenuAsync ({}, [safeThis, trackId, clipId, isAudio, splitBeat, enableClip, audioFile] (int result)
        {
            if (safeThis == nullptr) return;

            if (result >= 100 && result <= 109)
            {
                static constexpr double values[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                                    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                                    1.0 / 12.0, 4.0 };
                safeThis->owner.setGridBeat (values[result - 100]);
                return;
            }
            if (result == 110)
            {
                safeThis->owner.setGridBeat (0.0);
                return;
            }
            if (result == 6)
            {
                safeThis->selectAll();
                return;
            }

            if (clipId < 0) return;
            if (result == 4)
                safeThis->project.setClipEnabled (trackId, clipId, isAudio, enableClip);
            else if (result == 5 && isAudio && safeThis->owner.onLocateAudioFileRequested)
                safeThis->owner.onLocateAudioFileRequested (audioFile);
            else if (result == 1)
            {
                double start = 0.0, end = 0.0;
                if (const auto* track = safeThis->project.track (trackId))
                {
                    if (isAudio)
                    {
                        if (const auto* clip = track->findAudioClip (clipId))
                        {
                            start = clip->start;
                            end = clip->end();
                        }
                    }
                    else if (const auto* clip = track->find (clipId))
                    {
                        start = clip->start;
                        end = clip->end();
                    }
                }

                if (end > start)
                {
                    safeThis->project.setLoop (start, end);
                    if (safeThis->owner.onLoopToggleRequested)
                        safeThis->owner.onLoopToggleRequested (true);
                }
            }
            else if (result == 2)
            {
                if (isAudio)
                    safeThis->project.deleteAudioClip (trackId, clipId);
                else
                    safeThis->project.deleteClip (trackId, clipId);
            }
            else if (result == 3)
            {
                if (isAudio)
                    safeThis->project.splitAudioClip (trackId, clipId, splitBeat);
                else
                    safeThis->project.splitClip (trackId, clipId, splitBeat);
            }
        });
    }

    void mouseMove (const juce::MouseEvent& e) override { updateCursor (e.position); }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (mode == Mode::None)
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == Mode::EmptyClickPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;

            mode = Mode::RangeSelect;
            selectedClips.clear();
            selectionAnchor = {};
            hasRangeSelection = true;
            const double startBeat = std::max (0.0, selectionDownPosition.x / owner.pixelsPerBeat);
            const double endBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = e.mods.isAltDown() ? startBeat : owner.snapBeat (startBeat);
            rangeEndBeat = e.mods.isAltDown() ? endBeat : owner.snapBeat (endBeat);
            rangeStartTrack = std::clamp (
                static_cast<int> (std::floor (selectionDownPosition.y / trackHeight)),
                0, static_cast<int> (project.tracks.size()) - 1);
            rangeEndTrack = std::clamp (static_cast<int> (std::floor (e.position.y / trackHeight)),
                                        0, static_cast<int> (project.tracks.size()) - 1);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }

        if (mode == Mode::ShiftPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;
            mode = Mode::RangeSelect;
            selectedClips.clear();
            selectionAnchor = {};
            hasRangeSelection = true;
            const double startBeat = std::max (0.0, selectionDownPosition.x / owner.pixelsPerBeat);
            const double endBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = e.mods.isAltDown() ? startBeat : owner.snapBeat (startBeat);
            rangeEndBeat = e.mods.isAltDown() ? endBeat : owner.snapBeat (endBeat);
            rangeStartTrack = std::clamp (
                static_cast<int> (std::floor (selectionDownPosition.y / trackHeight)),
                0, static_cast<int> (project.tracks.size()) - 1);
            rangeEndTrack = std::clamp (
                static_cast<int> (std::floor (e.position.y / trackHeight)),
                0, static_cast<int> (project.tracks.size()) - 1);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }
        if (mode == Mode::CtrlPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;
            mode = Mode::Move;
        }
        if (mode == Mode::None) return;
        if (mode != Mode::RangeSelect)
            project.beginEdit();
        const double beat = e.position.x / owner.pixelsPerBeat;
        const bool noSnap = e.mods.isAltDown();

        if (mode == Mode::RangeSelect)
        {
            rangeEndBeat = std::max (0.0, noSnap ? beat : owner.snapBeat (beat));
            rangeEndTrack = std::clamp (static_cast<int> (std::floor (e.position.y / trackHeight)),
                                        0, static_cast<int> (project.tracks.size()) - 1);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }

        if (mode == Mode::Move)
        {
            double newStart = origStart + (beat - downBeat);
            newStart = std::max (0.0, noSnap ? newStart : owner.snapBeat (newStart));

            const int ti = std::clamp (static_cast<int> (std::floor (e.position.y / trackHeight)), 0, static_cast<int> (project.tracks.size()) - 1);
            const int toTrack = project.tracks[static_cast<size_t> (ti)]->id;

            if (curAudioClipId >= 0)
            {
                if (auto* track = project.track (curTrack))
                    if (auto* cur = track->findAudioClip (curAudioClipId);
                        cur != nullptr && (toTrack != curTrack || std::abs (newStart - cur->start) > 1.0e-9))
                    {
                        const int newId = e.mods.isCtrlDown() && ! copied
                            ? project.copyAudioClip (curTrack, curAudioClipId, toTrack, newStart)
                            : project.moveAudioClip (curTrack, curAudioClipId, toTrack, newStart);
                        if (newId >= 0)
                        {
                            curTrack = toTrack;
                            curAudioClipId = newId;
                            copied = copied || e.mods.isCtrlDown();
                            if (copied)
                            {
                                selectedClips = { { toTrack, newId, true } };
                                selectionAnchor = selectedClips.front();
                                hasRangeSelection = false;
                                setProjectSelection (selectionAnchor);
                            }
                        }
                    }
            }
            else if (auto* track = project.track (curTrack))
            {
                if (auto* cur = track->find (curClip);
                    cur != nullptr && (toTrack != curTrack || std::abs (newStart - cur->start) > 1.0e-9))
                {
                    const int nid = e.mods.isCtrlDown() && ! copied
                        ? project.copyClip (curTrack, curClip, toTrack, newStart)
                        : project.moveClip (curTrack, curClip, toTrack, newStart);
                    if (nid >= 0)
                    {
                        curTrack = toTrack; curClip = nid;
                        copied = copied || e.mods.isCtrlDown();
                        if (copied)
                        {
                            selectedClips = { { toTrack, nid, false } };
                            selectionAnchor = selectedClips.front();
                            hasRangeSelection = false;
                            setProjectSelection (selectionAnchor);
                        }
                    }
                }
            }
        }
        else if (mode == Mode::Resize)
        {
            if (curAudioClipId >= 0)
            {
                const double minimumLength = owner.gridBeats() > 0.0 ? owner.gridBeats() : 1.0 / 64.0;
                const double newLen = std::max (minimumLength, noSnap ? origLen + (beat - downBeat)
                                                                       : owner.snapBeat (origLen + (beat - downBeat)));
                if (auto* track = project.track (curTrack))
                    if (auto* clip = track->findAudioClip (curAudioClipId);
                        clip != nullptr && std::abs (newLen - clip->lengthBeats) > 1.0e-9)
                        project.resizeAudioClip (curTrack, curAudioClipId, newLen);
            }
            else if (auto* t = project.track (curTrack))
                if (auto* p = t->find (curClip))
                {
                    double newLen = origLen + (beat - downBeat);
                    newLen = std::max (owner.gridBeats() > 0.0 ? owner.gridBeats() : 1.0 / 64.0,
                                       noSnap ? newLen : owner.snapBeat (newLen));
                    if (std::abs (newLen - p->clip.length()) > 1.0e-9 && t->isFree (p->start, newLen, curClip))
                    {
                        p->clip.setLength (newLen);
                        project.growTo (p->end() + 64.0);
                        project.notifyTrackNotes (curTrack);
                        project.sendChangeMessage();
                    }
                }
        }
        else if (mode == Mode::FadeIn || mode == Mode::FadeOut)
        {
            if (auto* track = project.track (curTrack))
                if (auto* clip = track->findAudioClip (curAudioClipId))
                {
                    const double delta = beat - downBeat;
                    const double requested = mode == Mode::FadeIn ? origFadeIn + delta
                                                                  : origFadeOut - delta;
                    const double otherFade = mode == Mode::FadeIn ? clip->fadeOutBeats
                                                                  : clip->fadeInBeats;
                    const double fadeLength = std::clamp (
                        requested, 0.0, std::max (0.0, clip->lengthBeats - otherFade));
                    const double fadeIn = mode == Mode::FadeIn ? fadeLength : clip->fadeInBeats;
                    const double fadeOut = mode == Mode::FadeOut ? fadeLength : clip->fadeOutBeats;
                    project.setAudioClipFades (curTrack, curAudioClipId, fadeIn, fadeOut);
                }
        }
        else if (mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve)
        {
            const double curveDelta = (downY - e.position.y) / 30.0;
            const double fadeInCurve = mode == Mode::FadeInCurve
                ? std::clamp (origFadeInCurve + curveDelta, -1.0, 1.0) : origFadeInCurve;
            const double fadeOutCurve = mode == Mode::FadeOutCurve
                ? std::clamp (origFadeOutCurve + curveDelta, -1.0, 1.0) : origFadeOutCurve;
            project.setAudioClipFadeCurves (curTrack, curAudioClipId, fadeInCurve, fadeOutCurve);
        }

        notifyInteraction (true, 0, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode == Mode::EmptyClickPending)
        {
            const double rawBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            const double beat = e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat);
            if (owner.onSeek)
                owner.onSeek (beat);

            const int bar = static_cast<int> (std::floor (beat / 4.0)) + 1;
            const double beatInBar = std::fmod (beat, 4.0) + 1.0;
            notifyAction ("Cursor: compasso " + juce::String (bar) + " | beat "
                              + juce::String (beatInBar, 2),
                          icons::Icon::Pointer, false, 900);
            mode = Mode::None;
            updateCursor (e.position);
            repaint();
            return;
        }

        if (mode == Mode::ShiftPending)
        {
            extendClipSelection (pendingSelection);
            notifyAction ("Clipe selecionado", icons::Icon::Pointer, false, 650);
            mode = Mode::None;
            curAudioClipId = -1;
            repaint();
            return;
        }
        if (mode == Mode::CtrlPending)
        {
            toggleClipSelection (pendingSelection);
            notifyAction ("Selecao atualizada", icons::Icon::SelectRegion, false, 650);
            mode = Mode::None;
            curAudioClipId = -1;
            repaint();
            return;
        }
        if (mode != Mode::RangeSelect && e.getDistanceFromDragStart() < 4.0f)
            notifyAction ("Clipe selecionado", icons::Icon::Pointer, false, 650);
        else
            notifyInteraction (false, 700);
        if (mode != Mode::RangeSelect)
            project.endEdit();
        mode = Mode::None;
        updateCursor (e.position);
        curAudioClipId = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto h = hit (e.position);
        if (h.audioClip != nullptr)
        {
            const int trackId = project.tracks[static_cast<size_t> (h.trackIndex)]->id;
            project.selectAudioClip (trackId, h.audioClip->id);
            if (owner.onAudioClipDoubleClicked)
                owner.onAudioClipDoubleClicked (trackId, h.audioClip->id);
            return;
        }
        if (h.trackIndex < 0 || h.clip != nullptr) return;
        const int trackId = project.tracks[static_cast<size_t> (h.trackIndex)]->id;
        const double start = std::floor ((e.position.x / owner.pixelsPerBeat) / 4.0) * 4.0;
        if (project.tracks[static_cast<size_t> (h.trackIndex)]->type == roll::TrackType::Midi)
            project.addClip (trackId, start, 4.0);                     // 1 compasso
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            const auto viewX = owner.viewport.getViewPosition().x;
            owner.zoomAt (w.deltaY > 0 ? 1.15 : 1.0 / 1.15,
                          e.position.x / owner.pixelsPerBeat,
                          static_cast<int> (std::lround (e.position.x - viewX)));
            return;
        }
        Component::mouseWheelMove (e, w);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
        const auto key = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (k.getKeyCode()));
        auto* t = project.selectedTrack();

        if (ctrl && k.getModifiers().isShiftDown() && key == 'M')
        {
            project.addLocator (owner.snapBeat (owner.playhead));
            return true;
        }
        const bool left = k.isKeyCode (juce::KeyPress::leftKey);
        const bool right = k.isKeyCode (juce::KeyPress::rightKey);
        const bool up = k.isKeyCode (juce::KeyPress::upKey);
        const bool down = k.isKeyCode (juce::KeyPress::downKey);
        if (! ctrl && k.getModifiers().isShiftDown() && (left || right))
        {
            prepareRangeSelection();
            const double step = owner.gridBeats() > 0.0 ? owner.gridBeats() : owner.lastGridBeat;
            rangeEndBeat = owner.snapBeat (std::max (0.0, rangeEndBeat + (right ? step : -step)));
            if (owner.loopEnabled)
                project.setLoop (std::min (rangeStartBeat, rangeEndBeat),
                                 std::max (rangeStartBeat, rangeEndBeat));
            repaint();
            return true;
        }
        if (ctrl && (left || right))
        {
            const double step = owner.gridBeats() > 0.0 ? owner.gridBeats() : owner.lastGridBeat;
            const double length = std::max (step, project.loopEnd - project.loopStart
                                                   + (right ? step : -step));
            project.setLoop (project.loopStart, project.loopStart + length);
            return true;
        }
        if (ctrl && (up || down))
        {
            const double length = std::max (owner.gridBeats() > 0.0 ? owner.gridBeats() : owner.lastGridBeat,
                (project.loopEnd - project.loopStart) * (up ? 2.0 : 0.5));
            project.setLoop (project.loopStart, project.loopStart + length);
            return true;
        }
        if (ctrl && key == 'A')
        {
            selectAll();
            return true;
        }
        if (ctrl && key == 'G')
        {
            if (k.getModifiers().isShiftDown())
            {
                if (t != nullptr && t->type == roll::TrackType::Group)
                    project.ungroupTrack (t->id);
            }
            else
                project.groupTracks (project.selectedTrackIds);
            return true;
        }
        if (ctrl && key == '1')
        {
            const double current = owner.gridBeats() > 0.0 ? owner.gridBeats() : owner.lastGridBeat;
            owner.setGridBeat (std::max (1.0 / 64.0, current * 0.5));
            return true;
        }
        if (ctrl && key == '2')
        {
            const double current = owner.gridBeats() > 0.0 ? owner.gridBeats() : owner.lastGridBeat;
            owner.setGridBeat (std::min (4.0, current * 2.0));
            return true;
        }
        if (ctrl && key == '4')
        {
            owner.setGridBeat (owner.gridBeats() > 0.0 ? 0.0 : owner.lastGridBeat);
            return true;
        }

        const auto modifiers = k.getModifiers();
        if (k.getTextCharacter() == '0' && ! modifiers.isCtrlDown() && ! modifiers.isCommandDown()
            && ! modifiers.isAltDown() && ! modifiers.isShiftDown())
        {
            if (t != nullptr && project.selectedAudioClipId >= 0)
                if (auto* clip = t->findAudioClip (project.selectedAudioClipId))
                {
                    project.setClipEnabled (t->id, clip->id, true, ! clip->enabled);
                    return true;
                }

            if (t != nullptr)
                if (auto* clip = project.selectedClip())
                {
                    project.setClipEnabled (t->id, clip->id, false, ! clip->enabled);
                    return true;
                }
            return false;
        }

        if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
        {
            if (! selectedClips.empty())
            {
                deleteSelectedClips();
                return true;
            }
            if (hasRangeSelection)
            {
                deleteRangeSelection();
                return true;
            }
            if (t != nullptr && project.selectedAudioClipId >= 0)
            {
                project.deleteAudioClip (t->id, project.selectedAudioClipId);
                return true;
            }
            if (t != nullptr && t->find (project.selectedClipId) != nullptr) { project.deleteClip (t->id, project.selectedClipId); return true; }
            if (t != nullptr)
            {
                project.removeTrack (t->id);
                return true;
            }
            return false;
        }

        if (ctrl && key == 'D')
        {
            if (hasRangeSelection
                || (! selectedClips.empty()
                    && (project.selectedClipId >= 0 || project.selectedAudioClipId >= 0)))
            {
                duplicateSelectedClips();
                return true;
            }
            if (project.selectedTrackId > 0 && owner.onTrackDuplicateRequested)
            {
                owner.onTrackDuplicateRequested (project.selectedTrackId);
                return true;
            }
            return false;
        }

        if (ctrl && key == 'C')
            return copySelection (false);
        if (ctrl && key == 'X')
            return copySelection (true);
        if (ctrl && key == 'V')
            return pasteClipboard();

        if (ctrl && key == 'J')
        {
            if (! hasRangeSelection || owner.onConsolidateRequested == nullptr)
                return false;
            owner.onConsolidateRequested (std::min (rangeStartBeat, rangeEndBeat),
                                          std::max (rangeStartBeat, rangeEndBeat),
                                          std::min (rangeStartTrack, rangeEndTrack),
                                          std::max (rangeStartTrack, rangeEndTrack));
            return true;
        }

        if (ctrl && key == 'Z' && ! k.getModifiers().isShiftDown())
        {
            project.undo();
            return true;
        }
        if (ctrl && (key == 'Y' || (key == 'Z' && k.getModifiers().isShiftDown())))
        {
            project.redo();
            return true;
        }

        if (ctrl && key == 'L')
        {
            if (selectedClips.empty() && ! hasRangeSelection)
                return false;

            double start = std::min (rangeStartBeat, rangeEndBeat);
            double end = std::max (rangeStartBeat, rangeEndBeat);
            if (! selectedClips.empty())
                getSelectedClipBounds (start, end);
            if (end <= start)
                return true;

            project.setLoop (start, end);
            if (owner.onLoopToggleRequested)
                owner.onLoopToggleRequested (true);
            return true;
        }

        if (ctrl && key == 'E')
        {
            if (t != nullptr && project.selectedAudioClipId >= 0)
                project.splitAudioClip (t->id, project.selectedAudioClipId, owner.playhead);
            else if (t != nullptr && project.selectedClipId >= 0)
                project.splitClip (t->id, project.selectedClipId, owner.playhead);
            return true;
        }

        if (up || down)
        {
            const int idx = project.trackIndex (project.selectedTrackId) + (k.isKeyCode (juce::KeyPress::upKey) ? -1 : 1);
            if (idx >= 0 && idx < static_cast<int> (project.tracks.size())) project.selectTrack (project.tracks[static_cast<size_t> (idx)]->id);
            return true;
        }

        const auto ch = k.getTextCharacter();
        if (! ctrl && (ch == '+' || ch == '=')) { owner.zoom (1.25);       return true; }
        if (! ctrl && (ch == '-' || ch == '_')) { owner.zoom (1.0 / 1.25); return true; }
        if (k.isKeyCode (juce::KeyPress::homeKey)) { if (owner.onSeek) owner.onSeek (0.0); return true; }
        return false;
    }

private:
    void notifyAction (const juce::String& message, icons::Icon icon, bool active, int holdMilliseconds)
    {
        if (owner.onInteractionChanged)
            owner.onInteractionChanged (message, icon, active, holdMilliseconds);
    }

    void notifyInteraction (bool active, int holdMilliseconds = 0, bool dragging = false)
    {
        juce::String message;
        auto icon = icons::Icon::Pointer;
        switch (mode)
        {
            case Mode::Move:          message = active ? (dragging ? "Arrastando clipe" : "Segurando clipe") : "Clipe reposicionado"; icon = icons::Icon::Move; break;
            case Mode::Resize:        message = active ? (dragging ? "Redimensionando clipe" : "Segurando borda do clipe") : "Clipe redimensionado"; icon = icons::Icon::Resize; break;
            case Mode::FadeIn:        message = active ? (dragging ? "Ajustando fade de entrada" : "Segurando fade de entrada") : "Fade de entrada ajustado"; icon = icons::Icon::FadeIn; break;
            case Mode::FadeOut:       message = active ? (dragging ? "Ajustando fade de saida" : "Segurando fade de saida") : "Fade de saida ajustado"; icon = icons::Icon::FadeOut; break;
            case Mode::FadeInCurve:   message = active ? (dragging ? "Ajustando curva do fade" : "Segurando curva do fade") : "Curva do fade ajustada"; icon = icons::Icon::FadeIn; break;
            case Mode::FadeOutCurve:  message = active ? (dragging ? "Ajustando curva do fade" : "Segurando curva do fade") : "Curva do fade ajustada"; icon = icons::Icon::FadeOut; break;
            case Mode::RangeSelect:   message = active ? "Selecionando regiao" : "Regiao selecionada"; icon = icons::Icon::SelectRegion; break;
            case Mode::EmptyClickPending: message = active ? "Posicionando cursor" : "Cursor posicionado"; break;
            case Mode::ShiftPending:  message = active ? "Segurando selecao de clipe" : "Clipe selecionado"; break;
            case Mode::CtrlPending:   message = active ? "Segurando selecao de clipe" : "Selecao atualizada"; break;
            case Mode::None:          return;
        }
        notifyAction (message, icon, active, holdMilliseconds);
    }

    struct ClipSelection
    {
        int trackId = -1;
        int clipId = -1;
        bool audio = false;
    };

    struct ClipboardClip
    {
        int trackOffset = 0;
        double relativeStart = 0.0;
        bool audio = false;
        std::unique_ptr<roll::Placement> midiClip;
        std::unique_ptr<roll::AudioPlacement> audioClip;
    };

    bool isClipSelected (int trackId, int clipId, bool audio) const
    {
        return std::any_of (selectedClips.begin(), selectedClips.end(),
            [=] (const auto& selected)
            {
                return selected.trackId == trackId && selected.clipId == clipId
                    && selected.audio == audio;
            });
    }

    bool getClipBounds (const ClipSelection& selected, double& start, double& end) const
    {
        const auto* track = project.track (selected.trackId);
        if (track == nullptr)
            return false;
        if (selected.audio)
        {
            const auto* clip = track->findAudioClip (selected.clipId);
            if (clip == nullptr) return false;
            start = clip->start;
            end = clip->end();
            return true;
        }
        const auto* clip = track->find (selected.clipId);
        if (clip == nullptr) return false;
        start = clip->start;
        end = clip->end();
        return true;
    }

    bool getSelectedClipBounds (double& start, double& end) const
    {
        start = std::numeric_limits<double>::max();
        end = 0.0;
        bool found = false;
        for (const auto& selected : selectedClips)
        {
            double clipStart = 0.0, clipEnd = 0.0;
            if (! getClipBounds (selected, clipStart, clipEnd))
                continue;
            start = std::min (start, clipStart);
            end = std::max (end, clipEnd);
            found = true;
        }
        return found;
    }

    void setProjectSelection (const ClipSelection& selected)
    {
        if (selected.audio)
            project.selectAudioClip (selected.trackId, selected.clipId);
        else
            project.selectClip (selected.trackId, selected.clipId);
    }

    void updateRangeFromSelection (bool force)
    {
        if (selectedClips.empty() || (! force && selectedClips.size() == 1))
        {
            hasRangeSelection = false;
            return;
        }

        if (! getSelectedClipBounds (rangeStartBeat, rangeEndBeat))
        {
            hasRangeSelection = false;
            return;
        }

        rangeStartTrack = static_cast<int> (project.tracks.size());
        rangeEndTrack = -1;
        for (const auto& selected : selectedClips)
        {
            const int row = project.trackIndex (selected.trackId);
            if (row < 0) continue;
            rangeStartTrack = std::min (rangeStartTrack, row);
            rangeEndTrack = std::max (rangeEndTrack, row);
        }
        hasRangeSelection = rangeEndTrack >= rangeStartTrack;
    }

    void toggleClipSelection (const ClipSelection& selected)
    {
        const auto it = std::find_if (selectedClips.begin(), selectedClips.end(),
            [&selected] (const auto& current)
            {
                return current.trackId == selected.trackId && current.clipId == selected.clipId
                    && current.audio == selected.audio;
            });
        if (it == selectedClips.end())
            selectedClips.push_back (selected);
        else
            selectedClips.erase (it);

        if (selectedClips.empty())
        {
            hasRangeSelection = false;
            project.selectClip (selected.trackId, -1);
        }
        else
        {
            setProjectSelection (selectedClips.back());
            updateRangeFromSelection (false);
        }
        repaint();
    }

    void extendClipSelection (const ClipSelection& target)
    {
        auto anchor = selectionAnchor;
        double anchorStart = 0.0, anchorEnd = 0.0;
        if (anchor.trackId < 0 || ! getClipBounds (anchor, anchorStart, anchorEnd))
            anchor = target;

        double targetStart = 0.0, targetEnd = 0.0;
        double start = 0.0, end = 0.0;
        if (! getClipBounds (anchor, anchorStart, anchorEnd)
            || ! getClipBounds (target, targetStart, targetEnd))
            return;
        start = std::min (anchorStart, targetStart);
        end = std::max (anchorEnd, targetEnd);
        const int anchorRow = project.trackIndex (anchor.trackId);
        const int targetRow = project.trackIndex (target.trackId);
        if (anchorRow < 0 || targetRow < 0)
            return;

        selectedClips.clear();
        const int firstRow = std::min (anchorRow, targetRow);
        const int lastRow = std::max (anchorRow, targetRow);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            const auto& track = *project.tracks[static_cast<std::size_t> (row)];
            if (track.type == roll::TrackType::Audio)
            {
                for (const auto& clip : track.audioClips)
                    if (clip->start <= end && clip->end() >= start)
                        selectedClips.push_back ({ track.id, clip->id, true });
            }
            else
            {
                for (const auto& clip : track.clips)
                    if (clip->start <= end && clip->end() >= start)
                        selectedClips.push_back ({ track.id, clip->id, false });
            }
        }
        if (selectedClips.empty())
            selectedClips.push_back (target);

        setProjectSelection (target);
        updateRangeFromSelection (true);
        repaint();
    }

    void prepareRangeSelection()
    {
        if (hasRangeSelection)
            return;
        if (selectedClips.empty())
        {
            rangeStartBeat = rangeEndBeat = owner.snapBeat (owner.playhead);
            const int row = project.trackIndex (project.selectedTrackId);
            rangeStartTrack = rangeEndTrack = std::max (0, row);
        }
        else
            updateRangeFromSelection (true);
        hasRangeSelection = true;
    }

    void deleteSelectedClips()
    {
        const auto selection = selectedClips;
        project.beginEdit();
        for (const auto& selected : selection)
        {
            if (selected.audio)
                project.deleteAudioClip (selected.trackId, selected.clipId);
            else
                project.deleteClip (selected.trackId, selected.clipId);
        }
        project.endEdit();
        selectedClips.clear();
        selectionAnchor = {};
        hasRangeSelection = false;
        if (! selection.empty())
            project.selectClip (selection.back().trackId, -1);
        repaint();
    }

    void duplicateSelectedClips()
    {
        double start = 0.0, end = 0.0;
        const bool duplicateRange = selectedClips.empty();
        std::vector<ClipSelection> sources = selectedClips;
        std::vector<int> selectedTrackIds;

        if (duplicateRange)
        {
            if (project.tracks.empty())
                return;
            start = std::min (rangeStartBeat, rangeEndBeat);
            end = std::max (rangeStartBeat, rangeEndBeat);
            const int first = std::clamp (std::min (rangeStartTrack, rangeEndTrack), 0,
                                          static_cast<int> (project.tracks.size()) - 1);
            const int last = std::clamp (std::max (rangeStartTrack, rangeEndTrack), first,
                                         static_cast<int> (project.tracks.size()) - 1);
            for (int row = first; row <= last; ++row)
            {
                const auto& track = *project.tracks[static_cast<std::size_t> (row)];
                selectedTrackIds.push_back (track.id);
                if (track.type == roll::TrackType::Audio)
                {
                    for (const auto& clip : track.audioClips)
                        if (clip->start < end - 1.0e-9 && clip->end() > start + 1.0e-9)
                            sources.push_back ({ track.id, clip->id, true });
                }
                else
                {
                    for (const auto& clip : track.clips)
                        if (clip->start < end - 1.0e-9 && clip->end() > start + 1.0e-9)
                            sources.push_back ({ track.id, clip->id, false });
                }
            }
        }
        else
        {
            if (! getSelectedClipBounds (start, end))
                return;
            for (const auto& selected : sources)
                if (std::find (selectedTrackIds.begin(), selectedTrackIds.end(), selected.trackId) == selectedTrackIds.end())
                    selectedTrackIds.push_back (selected.trackId);
        }

        if (end <= start || sources.empty())
            return;

        const double offset = end - start;
        struct CopyAction { ClipSelection selected; double start; };
        std::vector<CopyAction> copyActions;
        std::vector<ClipSelection> copies;
        project.beginEdit();

        const auto splitBoundary = [this] (double beat, const std::vector<int>* tracksToSplit)
        {
            for (const auto& track : project.tracks)
            {
                if (tracksToSplit != nullptr
                    && std::find (tracksToSplit->begin(), tracksToSplit->end(), track->id) == tracksToSplit->end())
                    continue;

                std::vector<int> midiIds, audioIds;
                for (const auto& clip : track->clips)
                    if (clip->start < beat - 1.0e-9 && clip->end() > beat + 1.0e-9)
                        midiIds.push_back (clip->id);
                for (const auto& clip : track->audioClips)
                    if (clip->start < beat - 1.0e-9 && clip->end() > beat + 1.0e-9)
                        audioIds.push_back (clip->id);

                for (const int id : midiIds)
                    if (project.splitClip (track->id, id, beat) < 0)
                        return false;
                for (const int id : audioIds)
                    if (project.splitAudioClip (track->id, id, beat) < 0)
                        return false;
            }
            return true;
        };

        bool succeeded = splitBoundary (end, nullptr);
        if (succeeded && duplicateRange)
            succeeded = splitBoundary (start, &selectedTrackIds);

        if (succeeded)
        {
            if (duplicateRange)
            {
                sources.clear();
                for (const auto& track : project.tracks)
                {
                    if (std::find (selectedTrackIds.begin(), selectedTrackIds.end(), track->id) == selectedTrackIds.end())
                        continue;
                    if (track->type == roll::TrackType::Audio)
                    {
                        for (const auto& clip : track->audioClips)
                            if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                                sources.push_back ({ track->id, clip->id, true });
                    }
                    else
                    {
                        for (const auto& clip : track->clips)
                            if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                                sources.push_back ({ track->id, clip->id, false });
                    }
                }
            }

            for (const auto& source : sources)
            {
                double clipStart = 0.0, clipEnd = 0.0;
                if (getClipBounds (source, clipStart, clipEnd) && clipStart < end - 1.0e-9)
                    copyActions.push_back ({ source, clipStart + offset });
            }
            succeeded = ! copyActions.empty();
        }

        struct ShiftAction { int trackId; int clipId; double start; bool audio; };
        std::vector<ShiftAction> clipsToShift;
        if (succeeded)
        {
            for (const auto& track : project.tracks)
            {
                for (const auto& clip : track->clips)
                    if (clip->start >= end - 1.0e-9)
                        clipsToShift.push_back ({ track->id, clip->id, clip->start, false });
                for (const auto& clip : track->audioClips)
                    if (clip->start >= end - 1.0e-9)
                        clipsToShift.push_back ({ track->id, clip->id, clip->start, true });
            }
            std::sort (clipsToShift.begin(), clipsToShift.end(),
                       [] (const auto& a, const auto& b) { return a.start > b.start; });
            for (const auto& clip : clipsToShift)
            {
                const int movedId = clip.audio
                    ? project.moveAudioClip (clip.trackId, clip.clipId, clip.trackId, clip.start + offset)
                    : project.moveClip (clip.trackId, clip.clipId, clip.trackId, clip.start + offset);
                if (movedId < 0)
                {
                    succeeded = false;
                    break;
                }
            }
        }

        if (succeeded)
        {
            for (const auto& action : copyActions)
            {
                int id = -1;
                if (action.selected.audio)
                {
                    if (const auto* track = project.track (action.selected.trackId))
                        if (const auto* source = track->findAudioClip (action.selected.clipId))
                            id = project.copyAudioClip (*source, action.selected.trackId, action.start);
                }
                else if (const auto* track = project.track (action.selected.trackId))
                    if (const auto* source = track->find (action.selected.clipId))
                        id = project.copyClip (*source, action.selected.trackId, action.start);

                if (id < 0)
                {
                    succeeded = false;
                    break;
                }
                copies.push_back ({ action.selected.trackId, id, action.selected.audio });
            }
        }

        project.endEdit();
        if (! succeeded)
        {
            project.undo();
            notifyAction ("Nao foi possivel duplicar a selecao", icons::Icon::Pointer, false, 900);
            return;
        }

        selectedClips = std::move (copies);
        selectionAnchor = selectedClips.front();
        setProjectSelection (selectedClips.back());
        updateRangeFromSelection (true);
        repaint();
    }

    bool copySelection (bool cut)
    {
        double start = 0.0, end = 0.0;
        const bool rangeSelection = selectedClips.empty() && hasRangeSelection;
        std::vector<ClipSelection> sources = selectedClips;
        if (rangeSelection)
        {
            if (project.tracks.empty())
                return false;
            start = std::min (rangeStartBeat, rangeEndBeat);
            end = std::max (rangeStartBeat, rangeEndBeat);
            const int first = std::clamp (std::min (rangeStartTrack, rangeEndTrack), 0,
                                          static_cast<int> (project.tracks.size()) - 1);
            const int last = std::clamp (std::max (rangeStartTrack, rangeEndTrack), first,
                                         static_cast<int> (project.tracks.size()) - 1);
            for (int row = first; row <= last; ++row)
            {
                const auto& track = *project.tracks[static_cast<std::size_t> (row)];
                if (track.type == roll::TrackType::Audio)
                {
                    for (const auto& clip : track.audioClips)
                        if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                            sources.push_back ({ track.id, clip->id, true });
                }
                else
                {
                    for (const auto& clip : track.clips)
                        if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                            sources.push_back ({ track.id, clip->id, false });
                }
            }
        }
        else if (! getSelectedClipBounds (start, end))
            return false;

        if (sources.empty() || end <= start)
            return false;

        int baseTrackRow = static_cast<int> (project.tracks.size());
        if (rangeSelection)
            baseTrackRow = std::min (rangeStartTrack, rangeEndTrack);
        else
            for (const auto& source : sources)
                baseTrackRow = std::min (baseTrackRow, project.trackIndex (source.trackId));
        if (baseTrackRow < 0 || baseTrackRow >= static_cast<int> (project.tracks.size()))
            return false;

        clipboard.clear();
        for (const auto& source : sources)
        {
            const auto* track = project.track (source.trackId);
            if (track == nullptr)
                continue;
            const int trackOffset = project.trackIndex (source.trackId) - baseTrackRow;
            if (source.audio)
            {
                if (const auto* clip = track->findAudioClip (source.clipId))
                    clipboard.push_back ({ trackOffset, clip->start - start, true, nullptr,
                                           std::make_unique<roll::AudioPlacement> (*clip) });
            }
            else if (const auto* clip = track->find (source.clipId))
                clipboard.push_back ({ trackOffset, clip->start - start, false,
                                       std::make_unique<roll::Placement> (*clip), nullptr });
        }
        if (clipboard.empty())
            return false;

        if (cut)
        {
            if (rangeSelection)
                deleteRangeSelection();
            else
                deleteSelectedClips();
        }
        return true;
    }

    bool pasteClipboard()
    {
        if (clipboard.empty())
            return false;

        const int baseTrackRow = project.trackIndex (project.selectedTrackId);
        if (baseTrackRow < 0)
            return false;

        std::vector<int> targetTrackIds;
        targetTrackIds.reserve (clipboard.size());
        for (const auto& item : clipboard)
        {
            const int targetRow = baseTrackRow + item.trackOffset;
            if (targetRow < 0 || targetRow >= static_cast<int> (project.tracks.size()))
            {
                notifyAction ("Nao ha trilha de destino para todos os clips", icons::Icon::Pointer, false, 900);
                return true;
            }
            const auto& targetTrack = *project.tracks[static_cast<std::size_t> (targetRow)];
            if ((item.audio && targetTrack.type != roll::TrackType::Audio)
                || (! item.audio && targetTrack.type != roll::TrackType::Midi))
            {
                notifyAction ("O tipo da trilha nao corresponde ao clip copiado", icons::Icon::Pointer, false, 900);
                return true;
            }
            targetTrackIds.push_back (targetTrack.id);
        }

        project.beginEdit();
        std::vector<ClipSelection> pasted;
        bool succeeded = true;
        for (std::size_t i = 0; i < clipboard.size(); ++i)
        {
            const auto& item = clipboard[i];
            const int targetTrackId = targetTrackIds[i];
            const double start = owner.playhead + item.relativeStart;
            const int id = item.audio
                ? (item.audioClip != nullptr ? project.copyAudioClip (*item.audioClip, targetTrackId, start) : -1)
                : (item.midiClip != nullptr ? project.copyClip (*item.midiClip, targetTrackId, start) : -1);
            if (id < 0)
            {
                succeeded = false;
                break;
            }
            pasted.push_back ({ targetTrackId, id, item.audio });
        }
        project.endEdit();
        if (! succeeded)
        {
            project.undo();
            notifyAction ("Nao foi possivel colar os clips nesse local", icons::Icon::Pointer, false, 900);
            return true;
        }

        selectedClips = std::move (pasted);
        selectionAnchor = selectedClips.front();
        setProjectSelection (selectedClips.back());
        updateRangeFromSelection (true);
        repaint();
        return true;
    }

    void selectAll()
    {
        selectedClips.clear();
        selectionAnchor = {};
        double start = std::numeric_limits<double>::max();
        double end = 0.0;
        int first = -1, last = -1;
        for (size_t row = 0; row < project.tracks.size(); ++row)
        {
            const auto& track = *project.tracks[row];
            bool rowHasClips = false;
            if (track.type == roll::TrackType::Audio)
            {
                for (const auto& clip : track.audioClips)
                {
                    start = std::min (start, clip->start);
                    end = std::max (end, clip->end());
                    rowHasClips = true;
                }
            }
            else
            {
                for (const auto& clip : track.clips)
                {
                    start = std::min (start, clip->start);
                    end = std::max (end, clip->end());
                    rowHasClips = true;
                }
            }
            if (rowHasClips)
            {
                if (first < 0) first = static_cast<int> (row);
                last = static_cast<int> (row);
            }
        }

        if (first < 0 || end <= start)
            return;

        hasRangeSelection = true;
        rangeStartBeat = start;
        rangeEndBeat = end;
        rangeStartTrack = first;
        rangeEndTrack = last;
        project.selectClip (project.tracks[static_cast<std::size_t> (first)]->id, -1);
        repaint();
    }

    void deleteRangeSelection()
    {
        const double start = std::min (rangeStartBeat, rangeEndBeat);
        const double end = std::max (rangeStartBeat, rangeEndBeat);
        if (end - start <= 1.0e-9)
            return;

        const int first = std::min (rangeStartTrack, rangeEndTrack);
        const int last = std::max (rangeStartTrack, rangeEndTrack);
        struct ClipAction { int trackId; int clipId; bool audio; };
        std::vector<ClipAction> actions;
        for (int row = first; row <= last && row < static_cast<int> (project.tracks.size()); ++row)
        {
            const auto* track = project.tracks[static_cast<std::size_t> (row)].get();
            if (track->type == roll::TrackType::Audio)
            {
                for (const auto& clip : track->audioClips)
                    if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                        actions.push_back ({ track->id, clip->id, true });
            }
            else
            {
                for (const auto& clip : track->clips)
                    if (clip->start >= start - 1.0e-9 && clip->end() <= end + 1.0e-9)
                        actions.push_back ({ track->id, clip->id, false });
            }
        }
        if (actions.empty())
            return;

        project.beginEdit();
        for (const auto& action : actions)
        {
            if (action.audio)
                project.deleteAudioClip (action.trackId, action.clipId);
            else
                project.deleteClip (action.trackId, action.clipId);
        }
        project.endEdit();
        repaint();
    }

    int trackIndexAt (int y) const
    {
        const int masterRow = static_cast<int> (project.tracks.size());
        if (y < 0 || y >= (masterRow + 1) * trackHeight)
            return -1;
        return std::min (y / trackHeight, masterRow);
    }

    int trackIdAt (int row) const
    {
        if (row == static_cast<int> (project.tracks.size()))
            return 0;
        if (row >= 0 && row < static_cast<int> (project.tracks.size()))
            return project.tracks[static_cast<std::size_t> (row)]->id;
        return -1;
    }

    void updateDragTarget (const SourceDetails& details)
    {
        const auto description = details.description.toString();
        const int target = trackIndexAt (details.localPosition.y);
        dragDropIsAudio = description.startsWith ("audio:");
        dragTrackIndex = target;
        dragBeat = owner.snapBeat (details.localPosition.x / owner.pixelsPerBeat);
        dragDropAllowed = false;
        const int trackId = trackIdAt (target);
        if (dragDropIsAudio)
            dragDropAllowed = owner.canAcceptAudioDrop
                           && owner.canAcceptAudioDrop (juce::File (description.substring (6)), trackId);
        else if (description.startsWith ("plugin:"))
            dragDropAllowed = owner.canAcceptPluginDrop
                           && owner.canAcceptPluginDrop (description.substring (7), trackId);
        setMouseCursor (dragDropAllowed ? juce::MouseCursor::CopyingCursor
                                        : juce::MouseCursor::NormalCursor);
        repaint();
    }

    static bool isAudioFile (const juce::File& file)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        return extension == ".wav" || extension == ".wave" || extension == ".aif"
            || extension == ".aiff" || extension == ".flac" || extension == ".ogg";
    }

    void updateFileDragTarget (const juce::StringArray& files, int x, int y)
    {
        dragDropIsAudio = true;
        dragTrackIndex = trackIndexAt (y);
        dragBeat = owner.snapBeat (static_cast<double> (x) / owner.pixelsPerBeat);
        dragDropAllowed = false;
        if (files.size() == 1 && owner.canAcceptAudioDrop)
        {
            const int trackId = trackIdAt (dragTrackIndex);
            dragDropAllowed = owner.canAcceptAudioDrop (juce::File (files[0]), trackId);
        }
        setMouseCursor (dragDropAllowed ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void updateCursor (juce::Point<float> position)
    {
        if (mode == Mode::Resize)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (mode == Mode::FadeIn || mode == Mode::FadeOut)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            return;
        }

        if (mode == Mode::Move)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

        const auto h = hit (position);
        if (fadeModeAt (h, position) != Mode::None)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
        if (h.clip == nullptr && h.audioClip == nullptr)
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        const float right = static_cast<float> ((h.audioClip != nullptr ? h.audioClip->end() : h.clip->end()) * owner.pixelsPerBeat);
        setMouseCursor (position.x > right - 7.0f ? juce::MouseCursor::LeftRightResizeCursor
                                                   : juce::MouseCursor::DraggingHandCursor);
    }

    Mode fadeModeAt (const Hit& h, juce::Point<float> position) const
    {
        if (h.audioClip == nullptr)
            return Mode::None;

        const float rowTop = static_cast<float> (h.trackIndex * trackHeight + 22);
        const float left = static_cast<float> (h.audioClip->start * owner.pixelsPerBeat);
        const float right = static_cast<float> (h.audioClip->end() * owner.pixelsPerBeat);
        const float inset = std::min (3.0f, (right - left) * 0.5f);
        const float fadeInX = std::clamp (left + static_cast<float> (h.audioClip->fadeInBeats
                                                                     * owner.pixelsPerBeat),
                                          left + inset, right - inset);
        const float fadeOutX = std::clamp (right - static_cast<float> (h.audioClip->fadeOutBeats
                                                                       * owner.pixelsPerBeat),
                                           left + inset, right - inset);
        constexpr float curveHandleProgress = 0.25f;
        const float fadeTop = static_cast<float> (h.trackIndex * trackHeight + 22);
        const float fadeBottom = static_cast<float> (h.trackIndex * trackHeight + 70);
        const float inCurveX = left + (fadeInX - left) * curveHandleProgress;
        const float outCurveX = fadeOutX + (right - fadeOutX) * curveHandleProgress;
        const float inCurveY = fadeBottom - (fadeBottom - fadeTop) * static_cast<float> (
            roll::audioFadeCurveValue (curveHandleProgress, h.audioClip->fadeInCurve));
        const float outCurveY = fadeTop + (fadeBottom - fadeTop) * static_cast<float> (
            roll::audioFadeCurveValue (curveHandleProgress, h.audioClip->fadeOutCurve));
        const float inCurveDistance = h.audioClip->fadeInBeats > 0.02
            ? std::hypot (position.x - inCurveX, position.y - inCurveY)
            : std::numeric_limits<float>::max();
        const float outCurveDistance = h.audioClip->fadeOutBeats > 0.02
            ? std::hypot (position.x - outCurveX, position.y - outCurveY)
            : std::numeric_limits<float>::max();
        if (std::min (inCurveDistance, outCurveDistance) <= 9.0f)
            return inCurveDistance <= outCurveDistance ? Mode::FadeInCurve : Mode::FadeOutCurve;

        const float inDistance = std::hypot (position.x - fadeInX, position.y - rowTop);
        const float outDistance = std::hypot (position.x - fadeOutX, position.y - rowTop);
        constexpr float handleRadius = 10.0f;
        if (inDistance > handleRadius && outDistance > handleRadius)
            return Mode::None;
        return inDistance <= outDistance ? Mode::FadeIn : Mode::FadeOut;
    }

    void drawActionHint (juce::Graphics& g)
    {
        if (mode == Mode::None)
            return;

        const auto* track = project.track (curTrack);
        double clipStart = 0.0, clipLength = 0.0;
        if (curAudioClipId >= 0)
        {
            const auto* clip = track != nullptr ? track->findAudioClip (curAudioClipId) : nullptr;
            if (clip == nullptr) return;
            clipStart = clip->start;
            clipLength = clip->lengthBeats;
        }
        else
        {
            const auto* clip = track != nullptr ? track->find (curClip) : nullptr;
            if (clip == nullptr) return;
            clipStart = clip->start;
            clipLength = clip->clip.length();
        }

        juce::String message;
        if (mode == Mode::Resize)
            message = "Tamanho: " + juce::String (clipLength / 4.0, 1) + " compassos";
        else if (mode == Mode::FadeIn || mode == Mode::FadeOut)
        {
            const auto* clip = track->findAudioClip (curAudioClipId);
            if (clip == nullptr) return;
            const double fadeLength = mode == Mode::FadeIn ? clip->fadeInBeats : clip->fadeOutBeats;
            message = (mode == Mode::FadeIn ? "Fade-in: " : "Fade-out: ")
                    + juce::String (fadeLength, 2) + " beats";
        }
        else if (mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve)
        {
            const auto* clip = track->findAudioClip (curAudioClipId);
            if (clip == nullptr) return;
            const double curve = mode == Mode::FadeInCurve ? clip->fadeInCurve : clip->fadeOutCurve;
            message = (mode == Mode::FadeInCurve ? "Fade-in curve: " : "Fade-out curve: ")
                    + juce::String (curve, 2);
        }
        else
            message = "Movendo: compasso " + juce::String (static_cast<int> (std::floor (clipStart / 4.0)) + 1);
        const int x = std::clamp (static_cast<int> ((clipStart + clipLength) * owner.pixelsPerBeat) + 8, 4, std::max (4, getWidth() - 234));
        const int y = static_cast<int> (project.trackIndex (curTrack)) * trackHeight + 8;
        const auto hint = juce::Rectangle<int> (x, y, 230, 24);
        g.setColour (theme::panel().withAlpha (0.96f));
        g.fillRoundedRectangle (hint.toFloat(), 5.0f);
        g.setColour (theme::accent());
        g.drawRoundedRectangle (hint.toFloat(), 5.0f, 1.0f);
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (message, hint.reduced (7, 0), juce::Justification::centredLeft, true);
    }

    ProjectModel& project;
    ArrangementView& owner;
    Mode mode = Mode::None;
    bool copied = false;
    bool dragDropAllowed = false, dragDropIsAudio = false;
    int curTrack = -1, curClip = -1, curAudioClipId = -1;
    int dragTrackIndex = -1;
    double origStart = 0.0, origLen = 4.0, downBeat = 0.0, dragBeat = 0.0;
    double origFadeIn = 0.0, origFadeOut = 0.0;
    double origFadeInCurve = 0.0, origFadeOutCurve = 0.0, downY = 0.0;
    bool hasRangeSelection = false;
    double rangeStartBeat = 0.0, rangeEndBeat = 0.0;
    int rangeStartTrack = 0, rangeEndTrack = 0;
    std::vector<ClipSelection> selectedClips;
    std::vector<ClipboardClip> clipboard;
    ClipSelection selectionAnchor, pendingSelection;
    juce::Point<float> selectionDownPosition;
};

// ============================================================ Ruler (régua + brace de loop)

class ArrangementView::Ruler : public juce::Component, public juce::SettableTooltipClient
{
public:
    Ruler (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setTooltip ("Na régua: arraste verticalmente para ajustar o zoom e horizontalmente para navegar.");
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panelLight());
        const auto cb = g.getClipBounds();
        const double ppb = owner.pixelsPerBeat;

        // brace de loop
        const float lx0 = static_cast<float> (project.loopStart * ppb), lx1 = static_cast<float> (project.loopEnd * ppb);
        const bool loopInteraction = mode == Mode::LoopLeft || mode == Mode::LoopRight || mode == Mode::LoopMove
                                  || hoverMode == Mode::LoopLeft || hoverMode == Mode::LoopRight || hoverMode == Mode::LoopMove;
        const auto braceColour = loopInteraction
                               ? theme::accent().brighter (0.2f)
                               : owner.loopEnabled ? theme::accent() : theme::textDim().withAlpha (0.6f);
        g.setColour (braceColour);
        g.fillRect (lx0, 0.0f, lx1 - lx0, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (lx0, 0.0f, 3.0f, 8.0f);
        g.fillRect (lx1 - 3.0f, 0.0f, 3.0f, 8.0f);

        for (std::size_t i = 0; i < project.locators.size(); ++i)
        {
            const auto& marker = project.locators[i];
            const float x = static_cast<float> (marker.beat * ppb);
            if (x < cb.getX() - 70.0f || x > cb.getRight() + 8.0f)
                continue;

            const bool isSongStart = marker.id == project.songStartLocatorId;
            const bool emphasized = marker.id == project.selectedLocatorId || marker.id == hoveredLocatorId;
            const auto locatorRed = juce::Colour (0xffe64b4b);
            const auto markerColour = marker.id == project.selectedLocatorId ? locatorRed.brighter (0.3f)
                                    : isSongStart ? locatorRed.brighter (0.15f) : locatorRed;

            juce::Path pole;
            pole.startNewSubPath (x, 0.0f);
            pole.lineTo (x, 26.0f);
            g.setColour (markerColour);
            g.strokePath (pole, juce::PathStrokeType (2.0f));

            juce::Path flag;
            flag.startNewSubPath (x + 1.0f, 1.0f);
            flag.lineTo (x + 14.0f, 1.0f);
            flag.lineTo (x + 11.0f, 7.0f);
            flag.lineTo (x + 14.0f, 13.0f);
            flag.lineTo (x + 1.0f, 13.0f);
            flag.closeSubPath();
            g.fillPath (flag);

            const bool hasGeneratedName = marker.name.empty() || marker.name == std::to_string (marker.id);
            const juce::String label = hasGeneratedName ? juce::String (static_cast<int> (i + 1))
                                                        : juce::String (marker.name);
            int labelWidth = std::clamp (label.length() * 7 + 12, 24, 76);
            if (! emphasized && i + 1 < project.locators.size())
            {
                const auto gap = static_cast<int> (
                    (project.locators[i + 1].beat - marker.beat) * ppb) - 18;
                labelWidth = std::min (labelWidth, gap);
            }

            if (labelWidth >= 20)
            {
                int labelX = static_cast<int> (x) + 16;
                if (labelX + labelWidth > getWidth() - 2)
                    labelX = static_cast<int> (x) - labelWidth - 2;
                labelX = std::clamp (labelX, 2, std::max (2, getWidth() - labelWidth - 2));
                const juce::Rectangle<int> labelBounds (labelX, 1, labelWidth, 14);
                g.setColour (theme::panel().withAlpha (0.96f));
                g.fillRoundedRectangle (labelBounds.toFloat(), 3.0f);
                g.setColour (markerColour.withAlpha (emphasized ? 0.95f : 0.65f));
                g.drawRoundedRectangle (labelBounds.toFloat(), 3.0f, emphasized ? 1.3f : 0.8f);
                g.setColour (theme::text());
                g.setFont (juce::FontOptions (emphasized ? 12.5f : 11.5f, juce::Font::bold));
                g.drawText (label, labelBounds.reduced (4, 0), juce::Justification::centredLeft, true);
            }
        }
        if (hoveredInfo.isNotEmpty())
        {
            const auto font = juce::Font (juce::FontOptions (12.0f));
            const int width = std::clamp (hoveredInfo.length() * 7 + 14, 80, 320);
            const juce::Rectangle<int> hint (
                std::clamp (static_cast<int> (lastMouseX) + 10, 2, std::max (2, getWidth() - width - 2)),
                2, width, 20);
            g.setColour (theme::panel().withAlpha (0.97f));
            g.fillRoundedRectangle (hint.toFloat(), 4.0f);
            g.setColour (theme::accent());
            g.drawRoundedRectangle (hint.toFloat(), 4.0f, 1.0f);
            g.setColour (theme::text());
            g.setFont (font);
            g.drawText (hoveredInfo, hint.reduced (6, 0), juce::Justification::centredLeft, true);
        }

        const bool leftActive = mode == Mode::LoopLeft || (mode == Mode::Seek && hoverMode == Mode::LoopLeft);
        const bool rightActive = mode == Mode::LoopRight || (mode == Mode::Seek && hoverMode == Mode::LoopRight);
        if (leftActive || rightActive)
        {
            g.setColour (juce::Colours::white);
            if (leftActive)
                g.fillRoundedRectangle (lx0 - 2.0f, 0.0f, 5.0f, 10.0f, 2.0f);
            if (rightActive)
                g.fillRoundedRectangle (lx1 - 3.0f, 0.0f, 5.0f, 10.0f, 2.0f);
        }

        const int labelStep = ppb < 10.0 ? 16 : ppb < 18.0 ? 8 : 4;          // beats entre rótulos
        g.setFont (juce::FontOptions (12.5f));
        const int firstBeat = std::max (0, static_cast<int> (std::floor (cb.getX() / ppb)) - 1);
        const int lastBeat = static_cast<int> (std::ceil (cb.getRight() / ppb));
        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const bool bar = (b % 4 == 0);
            if (! bar && ppb < 14.0) continue;
            const float x = static_cast<float> (b * ppb);
            g.setColour (bar ? theme::text() : theme::textDim());
            g.drawVerticalLine (static_cast<int> (x), bar ? 12.0f : 20.0f, static_cast<float> (getHeight()));
            if (b % labelStep == 0)
                g.drawText (juce::String (b / 4 + 1), static_cast<int> (x) + 3, 10, 40, getHeight() - 10, juce::Justification::centredLeft);
        }

        g.setColour (theme::playhead());
        juce::Path tri;
        const float px = static_cast<float> (owner.playhead * ppb);
        tri.addTriangle (px - 4.0f, 10.0f, px + 4.0f, 10.0f, px, 18.0f);
        g.fillPath (tri);

        if (mode == Mode::LoopLeft || mode == Mode::LoopRight || mode == Mode::LoopMove)
            drawActionHint (g);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        mode = Mode::Seek;
        navigationPending = false;
        lastMouseX = e.position.x;
        if (e.mods.isPopupMenu())
        {
            showContextMenu (e);
            repaint();
            return;
        }

        if (auto* marker = locatorAt (e.position))
        {
            owner.grabFocus();
            mode = Mode::LocatorMove;
            draggedLocatorId = marker->id;
            grabOffset = e.position.x / owner.pixelsPerBeat - marker->beat;
            project.selectLocator (marker->id);
            if (owner.onSeek)
                owner.onSeek (marker->beat);
            if (owner.onArrangementSeekRequested)
                owner.onArrangementSeekRequested (marker->beat);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            notifyInteraction (true);
            repaint();
            return;
        }

        if (e.y <= 10)
        {
            const double ppb = owner.pixelsPerBeat;
            const float lx0 = static_cast<float> (project.loopStart * ppb), lx1 = static_cast<float> (project.loopEnd * ppb);
            if (std::abs (e.position.x - lx0) < 6.0f)       mode = Mode::LoopLeft;
            else if (std::abs (e.position.x - lx1) < 6.0f)  mode = Mode::LoopRight;
            else if (e.position.x > lx0 && e.position.x < lx1) { mode = Mode::LoopMove; grabOffset = e.position.x / ppb - project.loopStart; }
        }
        hoverMode = mode;
        updateCursor (e.position);
        if (mode == Mode::Seek)
        {
            navigationPending = e.position.y > 10.0f;
            if (navigationPending)
            {
                navigationStartScreenPosition = e.getScreenPosition();
                navigationLastScreenPosition = navigationStartScreenPosition;
                navigationStartViewX = owner.viewport.getViewPositionX();
                navigationAnchorBeat = e.position.x / owner.pixelsPerBeat;
                navigationAnchorScreenX = static_cast<int> (std::lround (e.position.x - navigationStartViewX));
            }
            else
            {
                const double beat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
                if (owner.onSeek) owner.onSeek (beat);
                if (owner.onArrangementSeekRequested)
                    owner.onArrangementSeekRequested (beat);
            }
        }
        notifyInteraction (true);
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        lastMouseX = e.position.x;
        const auto* marker = locatorAt (e.position);
        hoveredLocatorId = marker != nullptr ? marker->id : -1;
        hoveredInfo = marker == nullptr ? juce::String()
                      : marker->info.empty() ? juce::String (marker->name)
                                             : juce::String (marker->name + " | " + marker->info);
        if (mode == Mode::Seek)
        {
            hoverMode = modeAt (e.position);
            updateCursor (e.position);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (mode == Mode::Seek)
        {
            hoverMode = Mode::Seek;
            hoveredLocatorId = -1;
            hoveredInfo.clear();
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        lastMouseX = e.position.x;
        const auto screenPosition = e.getScreenPosition();
        if (mode == Mode::Seek && navigationPending)
        {
            if (e.getDistanceFromDragStart() < 4.0f)
                return;

            const auto delta = screenPosition - navigationStartScreenPosition;
            mode = std::abs (delta.x) > std::abs (delta.y) ? Mode::PanView : Mode::ZoomView;
            updateCursor (e.position);
        }

        const double rawBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
        const double beat = std::max (0.0, e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat));
        switch (mode)
        {
            case Mode::Seek:
            {
                const double seekBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
                if (owner.onSeek) owner.onSeek (seekBeat);
                if (owner.onArrangementSeekRequested)
                    owner.onArrangementSeekRequested (seekBeat);
                break;
            }
            case Mode::PanView:
            {
                const int dx = screenPosition.x - navigationStartScreenPosition.x;
                owner.viewport.setViewPosition (std::max (0, navigationStartViewX + dx),
                                                owner.viewport.getViewPositionY());
                owner.syncPositions();
                break;
            }
            case Mode::ZoomView:
            {
                const int dy = screenPosition.y - navigationLastScreenPosition.y;
                navigationLastScreenPosition = screenPosition;
                if (dy != 0)
                    owner.zoomAt (std::exp (static_cast<double> (dy) * 0.012),
                                  navigationAnchorBeat, navigationAnchorScreenX);
                break;
            }
            case Mode::LoopLeft:  project.setLoop (std::min (beat, project.loopEnd - 1.0), project.loopEnd); break;
            case Mode::LoopRight: project.setLoop (project.loopStart, std::max (beat, project.loopStart + 1.0)); break;
            case Mode::LocatorMove:
            {
                if (! locatorEditStarted)
                {
                    project.beginEdit();
                    locatorEditStarted = true;
                }
                const double position = std::max (0.0, rawBeat - grabOffset);
                project.moveLocator (draggedLocatorId,
                    e.mods.isAltDown() ? position : owner.snapBeat (position));
                break;
            }
            case Mode::LoopMove:
            {
                const double len = project.loopEnd - project.loopStart;
                const double s = std::max (0.0, std::round (e.position.x / owner.pixelsPerBeat - grabOffset));
                project.setLoop (s, s + len);
                break;
            }
        }
        notifyInteraction (true, 0, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode == Mode::Seek && navigationPending)
        {
            const double beat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            if (owner.onSeek) owner.onSeek (beat);
            if (owner.onArrangementSeekRequested)
                owner.onArrangementSeekRequested (beat);
        }
        navigationPending = false;
        if (locatorEditStarted)
        {
            project.endEdit();
            locatorEditStarted = false;
        }
        notifyInteraction (false, 650);
        mode = Mode::Seek;
        hoverMode = modeAt (e.position);
        updateCursor (e.position);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto* marker = locatorAt (e.position))
        {
            project.selectLocator (marker->id);
            if (owner.onLocatorLaunchRequested)
                owner.onLocatorLaunchRequested (marker->beat);
            repaint();
        }
    }

private:
    enum class Mode { Seek, PanView, ZoomView, LoopLeft, LoopRight, LoopMove, LocatorMove };
    void notifyInteraction (bool active, int holdMilliseconds = 0, bool dragging = false)
    {
        if (! owner.onInteractionChanged)
            return;

        juce::String message;
        auto icon = icons::Icon::Pointer;
        switch (mode)
        {
            case Mode::Seek:       message = active ? (dragging ? "Arrastando playhead" : "Segurando playhead") : "Cursor posicionado"; break;
            case Mode::PanView:    message = active ? "Navegando no arranjo" : "Navegacao concluida"; icon = icons::Icon::Move; break;
            case Mode::ZoomView:   message = active ? "Ajustando zoom do arranjo" : "Zoom ajustado"; icon = icons::Icon::Resize; break;
            case Mode::LoopLeft:
            case Mode::LoopRight:  message = active ? (dragging ? "Ajustando loop" : "Segurando limite do loop") : "Loop ajustado"; icon = icons::Icon::SelectRegion; break;
            case Mode::LoopMove:   message = active ? (dragging ? "Movendo loop" : "Segurando loop") : "Loop movido"; icon = icons::Icon::Loop; break;
            case Mode::LocatorMove:message = active ? (dragging ? "Movendo marcador" : "Segurando marcador") : "Marcador movido"; icon = icons::Icon::Move; break;
        }
        owner.onInteractionChanged (message, icon, active, holdMilliseconds);
    }

    ProjectModel& project;
    ArrangementView& owner;
    Mode mode = Mode::Seek;
    Mode hoverMode = Mode::Seek;
    double grabOffset = 0.0;
    float lastMouseX = 0.0f;
    int draggedLocatorId = -1;
    bool locatorEditStarted = false;
    bool navigationPending = false;
    juce::Point<int> navigationStartScreenPosition, navigationLastScreenPosition;
    int navigationStartViewX = 0, navigationAnchorScreenX = 0;
    double navigationAnchorBeat = 0.0;
    int hoveredLocatorId = -1;
    juce::String hoveredInfo;

    ArrangementLocator* locatorAt (juce::Point<float> position) const
    {
        if (position.y >= rulerHeight)
            return nullptr;
        for (auto it = project.locators.rbegin(); it != project.locators.rend(); ++it)
            if (std::abs (position.x - static_cast<float> (it->beat * owner.pixelsPerBeat)) < 16.0f)
                return project.locator (it->id);
        return nullptr;
    }

    void showContextMenu (const juce::MouseEvent& e)
    {
        const double beat = owner.snapBeat (std::max (0.0, e.position.x / owner.pixelsPerBeat));
        const auto* marker = locatorAt (e.position);
        const int markerId = marker != nullptr ? marker->id : -1;
        juce::PopupMenu menu;
        menu.addItem (7, "Previous Locator", locatorInDirection (markerId, beat, false) != nullptr);
        menu.addItem (8, "Next Locator", locatorInDirection (markerId, beat, true) != nullptr);
        menu.addSeparator();
        menu.addItem (1, "Add Locator Here");
        if (marker != nullptr)
        {
            menu.addItem (2, "Rename Locator");
            menu.addItem (3, "Edit Locator Info");
            menu.addItem (4, "Loop to Next Locator", std::any_of (
                project.locators.begin(), project.locators.end(),
                [marker] (const auto& other) { return other.beat >= marker->beat + 1.0 / 64.0; }));
            menu.addItem (5, markerId == project.songStartLocatorId
                                  ? "Clear Song Start" : "Set Song Start Here");
            menu.addItem (6, "Delete Locator");
        }

        juce::Component::SafePointer<Ruler> safeThis (this);
        const auto screen = localPointToGlobal (e.position.toInt());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
            { screen.x, screen.y, 1, 1 }),
            [safeThis, markerId, beat] (int result)
            {
                if (safeThis == nullptr)
                    return;
                auto& model = safeThis->project;
                safeThis->owner.grabFocus();
                if (result == 1)
                    model.addLocator (beat);
                else if (result == 2 || result == 3)
                    safeThis->editLocator (markerId, result == 3);
                else if (result == 4)
                    safeThis->loopToNextLocator (markerId);
                else if (result == 5)
                    model.setSongStartLocator (markerId == model.songStartLocatorId ? -1 : markerId);
                else if (result == 6)
                    model.removeLocator (markerId);
                else if (result == 7 || result == 8)
                    safeThis->navigateLocator (markerId, beat, result == 8);
            });
    }

    const ArrangementLocator* locatorInDirection (int id, double beat, bool forward) const
    {
        auto current = std::find_if (project.locators.begin(), project.locators.end(),
            [id] (const auto& marker) { return id >= 0 && marker.id == id; });
        if (current != project.locators.end())
        {
            if (forward)
                return ++current == project.locators.end() ? nullptr : &*current;
            return current == project.locators.begin() ? nullptr : &*std::prev (current);
        }

        if (forward)
        {
            const auto next = std::upper_bound (project.locators.begin(), project.locators.end(), beat,
                [] (double targetBeat, const auto& marker) { return targetBeat < marker.beat; });
            return next == project.locators.end() ? nullptr : &*next;
        }
        auto previous = std::lower_bound (project.locators.begin(), project.locators.end(), beat,
            [] (const auto& marker, double targetBeat) { return marker.beat < targetBeat; });
        return previous == project.locators.begin() ? nullptr : &*std::prev (previous);
    }

    void navigateLocator (int id, double beat, bool forward)
    {
        if (const auto* marker = locatorInDirection (id, beat, forward))
        {
            project.selectLocator (marker->id);
            if (owner.onSeek)
                owner.onSeek (marker->beat);
            if (owner.onArrangementSeekRequested)
                owner.onArrangementSeekRequested (marker->beat);
        }
    }

    void editLocator (int id, bool editInfo)
    {
        const auto* marker = project.locator (id);
        if (marker == nullptr)
            return;

        auto* alert = new juce::AlertWindow (editInfo ? "Edit Locator Info" : "Rename Locator",
                                             editInfo ? "Enter locator information:" : "Enter locator name:",
                                             juce::AlertWindow::NoIcon);
        alert->addTextEditor ("value", juce::String (editInfo ? marker->info : marker->name));
        alert->addButton ("OK", 1);
        alert->addButton ("Cancel", 0);
        juce::Component::SafePointer<Ruler> safeThis (this);
        alert->enterModalState (true,
            juce::ModalCallbackFunction::create ([safeThis, alert, id, editInfo] (int result)
            {
                if (safeThis != nullptr && result == 1)
                {
                    const auto value = alert->getTextEditorContents ("value").toStdString();
                    if (editInfo)
                        safeThis->project.setLocatorInfo (id, value);
                    else
                        safeThis->project.renameLocator (id, value);
                }
            }), true);
    }

    void loopToNextLocator (int id)
    {
        const auto* marker = project.locator (id);
        if (marker == nullptr)
            return;
        const auto next = std::find_if (project.locators.begin(), project.locators.end(),
            [marker] (const auto& other) { return other.beat >= marker->beat + 1.0 / 64.0; });
        if (next == project.locators.end())
            return;
        project.setLoop (marker->beat, next->beat);
        if (owner.onLoopToggleRequested)
            owner.onLoopToggleRequested (true);
    }

    Mode modeAt (juce::Point<float> position) const
    {
        if (locatorAt (position) != nullptr)
            return Mode::LocatorMove;
        if (position.y > 10.0f)
            return Mode::Seek;

        const float left = static_cast<float> (project.loopStart * owner.pixelsPerBeat);
        const float right = static_cast<float> (project.loopEnd * owner.pixelsPerBeat);
        if (std::abs (position.x - left) < 6.0f)  return Mode::LoopLeft;
        if (std::abs (position.x - right) < 6.0f) return Mode::LoopRight;
        if (position.x > left && position.x < right) return Mode::LoopMove;
        return Mode::Seek;
    }

    void updateCursor (juce::Point<float> position)
    {
        const auto target = mode == Mode::Seek ? modeAt (position) : mode;
        if (target == Mode::LoopLeft || target == Mode::LoopRight)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (target == Mode::LoopMove || target == Mode::LocatorMove)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else if (target == Mode::PanView)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else if (target == Mode::ZoomView
                 || (target == Mode::Seek && position.y > 10.0f && locatorAt (position) == nullptr))
            setMouseCursor (ui::zoomCursor());
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    static juce::String formatPosition (double beat)
    {
        const int wholeBeat = std::max (0, static_cast<int> (std::round (beat)));
        return juce::String (wholeBeat / 4 + 1) + "."
             + juce::String (wholeBeat % 4 + 1) + ".1";
    }

    void drawActionHint (juce::Graphics& g) const
    {
        juce::String message;
        if (mode == Mode::LoopLeft)
            message = "Inicio do loop: " + formatPosition (project.loopStart);
        else if (mode == Mode::LoopRight)
            message = "Fim do loop: " + formatPosition (project.loopEnd);
        else
            message = "Loop: " + formatPosition (project.loopStart) + " - "
                    + formatPosition (project.loopEnd) + "  ("
                    + juce::String ((project.loopEnd - project.loopStart) / 4.0, 1) + " compassos)";

        const auto hint = juce::Rectangle<int> (
            std::clamp (static_cast<int> (lastMouseX) + 12, 4, std::max (4, getWidth() - 274)),
            2, 270, 24);
        g.setColour (theme::panel().withAlpha (0.97f));
        g.fillRoundedRectangle (hint.toFloat(), 5.0f);
        g.setColour (theme::accent());
        g.drawRoundedRectangle (hint.toFloat(), 5.0f, 1.0f);
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (message, hint.reduced (7, 0), juce::Justification::centredLeft, true);
    }
};

// ============================================================ Header (cabeçalho de trilha)

class ArrangementView::Header : public juce::Component
{
public:
    Header (ProjectModel& p, ArrangementView& o, int id) : project (p), owner (o), trackId (id)
    {
        auto* t = project.track (trackId);
        addAndMakeVisible (name);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        addAndMakeVisible (volume);
        addAndMakeVisible (volumeDb);
        addAndMakeVisible (panLabel);
        addAndMakeVisible (pan);
        addAndMakeVisible (panValue);
        addAndMakeVisible (routingButton);

        routingButton.setMouseClickGrabsKeyboardFocus (false);
        updateRoutingButton();
        routingButton.onClick = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                auto next = (tr->outputRouting == roll::TrackOutputRouting::Master)
                                ? roll::TrackOutputRouting::ExtOut
                                : roll::TrackOutputRouting::Master;
                project.setTrackRouting (trackId, next);
                updateRoutingButton();
            }
        };

        name.setText (juce::String (t->name), juce::dontSendNotification);
        name.setEditable (false, true, false);
        name.setColour (juce::Label::textColourId, theme::text());
        name.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        name.addMouseListener (this, false);
        name.onTextChange = [this]
        {
            if (auto* tr = project.track (trackId)) tr->name = name.getText().toStdString();
        };

        for (auto* b : { &mute, &solo })
        {
            b->setClickingTogglesState (true);
            b->setMouseClickGrabsKeyboardFocus (false);
        }
        solo.setColour (juce::TextButton::buttonOnColourId, theme::accent());
        mute.setColour (juce::TextButton::buttonOnColourId, theme::accent());
        mute.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        solo.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        mute.setTooltip (theme::ui ("Ativador da trilha: desativa a saída MIDI/áudio"));
        solo.setTooltip (theme::ui ("Clique para solo exclusivo; Ctrl+clique adiciona/remove solo. "
                                    "Com varias trilhas selecionadas, clique aplica a todas."));
        mute.setToggleState (t->mute, juce::dontSendNotification);
        mute.setButtonText (t->mute ? "OFF" : "ON");
        solo.setToggleState (t->solo, juce::dontSendNotification);
        mute.onClick = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->mute = mute.getToggleState();
                mute.setButtonText (tr->mute ? "OFF" : "ON");
                project.notifyMixer();
            }
        };
        solo.onClick = [this]
        {
            const auto modifiers = juce::ModifierKeys::getCurrentModifiers();
            project.setTrackSolo (trackId, solo.getToggleState(),
                                  modifiers.isCtrlDown() || modifiers.isCommandDown());
        };

        panLabel.setText ("Pan", juce::dontSendNotification);
        panLabel.setFont (juce::FontOptions (11.5f));
        panLabel.setColour (juce::Label::textColourId, theme::textDim());
        panValue.setFont (juce::FontOptions (11.5f));
        panValue.setColour (juce::Label::textColourId, theme::textDim());
        panValue.setJustificationType (juce::Justification::centredRight);
        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setTooltip (theme::ui ("Panorama: esquerda (-100%) / direita (+100%); duplo clique centraliza"));
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setValue (t->pan, juce::dontSendNotification);
        pan.setMouseClickGrabsKeyboardFocus (false);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.onValueChange = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->pan = pan.getValue();
                updatePanText (tr->pan);
                project.notifyMixer();
            }
        };
        pan.setDoubleClickReturnValue (true, 0.0);
        updatePanText (t->pan);

        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setTooltip ("Volume da trilha");
        volume.setRange (0.0, 1.5, 0.001);
        volume.setValue (t->volume, juce::dontSendNotification);
        volume.setMouseClickGrabsKeyboardFocus (false);
        volume.setPopupDisplayEnabled (true, true, nullptr);
        volume.textFromValueFunction = [] (double v)
        {
            const float db = roll::gainToDecibels (static_cast<float> (v));
            if (db <= -59.5f) return juce::String ("-inf dB");
            return (db >= 0.05f ? juce::String ("+") : juce::String()) + juce::String (db, 1) + " dB";
        };
        volumeDb.setFont (juce::FontOptions (11.5f));
        volumeDb.setColour (juce::Label::textColourId, theme::textDim());
        volumeDb.setJustificationType (juce::Justification::centredRight);
        updateVolumeText (t->volume);
        volume.onValueChange = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->volume = volume.getValue();
                updateVolumeText (tr->volume);
                project.notifyMixer();
            }
        };
        volume.setDoubleClickReturnValue (true, 1.0);
    }

    int id() const { return trackId; }
    void setPeak (float p) { if (std::abs (p - peak) > 0.004f) { peak = p; repaint (getWidth() - 16, 0, 16, getHeight()); } }
    void refreshText()
    {
        updateRoutingButton();
        if (const auto* tr = project.track (trackId))
        {
            mute.setToggleState (tr->mute, juce::dontSendNotification);
            mute.setButtonText (tr->mute ? "OFF" : "ON");
            solo.setToggleState (tr->solo, juce::dontSendNotification);
            volume.setValue (tr->volume, juce::dontSendNotification);
            updateVolumeText (tr->volume);
            pan.setValue (tr->pan, juce::dontSendNotification);
            updatePanText (tr->pan);
        }
        repaint();
    }

    void updatePanText (double value)
    {
        const auto amount = juce::roundToInt (std::abs (value) * 100.0);
        const auto text = value < -0.005 ? "E " + juce::String (amount) + "%"
                         : value > 0.005 ? "D " + juce::String (amount) + "%"
                                         : "C";
        panValue.setText (text, juce::dontSendNotification);
    }

    void updateVolumeText (double gain)
    {
        const float db = roll::gainToDecibels (static_cast<float> (gain));
        volumeDb.setText (db <= -59.5f ? "-inf dB"
                                       : (db >= 0.05f ? "+" : "") + juce::String (db, 1) + " dB",
                          juce::dontSendNotification);
    }

    void updateRoutingButton()
    {
        if (auto* tr = project.track (trackId))
        {
            const bool ext = (tr->outputRouting == roll::TrackOutputRouting::ExtOut);
            routingButton.setButtonText (ext ? "Ext. Out" : "Master");
            routingButton.setColour (juce::TextButton::buttonColourId, ext ? juce::Colour (0xffe67e22) : theme::panelLight());
            routingButton.setColour (juce::TextButton::textColourOffId, ext ? juce::Colours::black : theme::textDim());
            routingButton.setTooltip (ext ? theme::ui ("Destino: Ext. Out (Direto - bypassa o Master)")
                                          : theme::ui ("Destino: Master (processado pelo canal Master)"));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const bool sel = project.isTrackSelected (trackId);
        const auto* track = project.track (trackId);
        const bool isGroup = track != nullptr && track->type == roll::TrackType::Group;
        g.setColour (sel ? theme::panelLight().brighter (0.15f)
                         : isGroup ? theme::panelLight().darker (0.12f) : theme::panel());
        g.fillRect (getLocalBounds());
        g.setColour (colourFor (trackId));
        g.fillRect (0, 0, 6, getHeight());
        g.setColour (theme::border());
        g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (12.5f));
        const juce::String summary = owner.deviceSummary ? owner.deviceSummary (trackId) : juce::String();
        g.drawText (summary.isEmpty() ? theme::ui ("sem instrumento") : summary, 14, 26, getWidth() - 30, 16, juce::Justification::centredLeft, true);

        if (const auto* volumeIcon = icons::get (icons::Icon::Volume, theme::textDim(), 16))
            volumeIcon->drawWithin (g, volumeIconBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

        const float h = static_cast<float> (getHeight() - 12);
        const float level = std::min (1.0f, peak);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (static_cast<float> (getWidth() - 12), 6.0f, 6.0f, h);
        g.setColour (level > 0.9f ? theme::warning() : theme::accent());
        g.fillRect (static_cast<float> (getWidth() - 12), 6.0f + h * (1.0f - level), 6.0f, h * level);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (6, 4);
        a.removeFromLeft (8);
        a.removeFromRight (14);
        auto topRow = a.removeFromTop (22);
        routingButton.setBounds (topRow.removeFromRight (58));
        topRow.removeFromRight (4);
        if (const auto* track = project.track (trackId); track != nullptr && track->parentGroupId != 0)
            topRow.removeFromLeft (12);
        name.setBounds (topRow);
        a.removeFromTop (18);
        auto row = a.removeFromTop (24);
        mute.setBounds (row.removeFromLeft (34));
        row.removeFromLeft (4);
        solo.setBounds (row.removeFromLeft (26));
        row.removeFromLeft (6);
        volumeIconBounds = row.removeFromLeft (20);
        const int dbWidth = std::min (48, row.getWidth() / 3);
        volumeDb.setBounds (row.removeFromRight (dbWidth));
        row.removeFromRight (4);
        volume.setBounds (row);
        a.removeFromTop (2);
        auto panRow = a.removeFromTop (20);
        panLabel.setBounds (panRow.removeFromLeft (30));
        panValue.setBounds (panRow.removeFromRight (38));
        pan.setBounds (panRow);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.originalComponent == &name)
        {
            owner.lanes->clearSelection();
            if (e.mods.isPopupMenu())
            {
                const bool toggleSelection = e.mods.isShiftDown() || e.mods.isCtrlDown()
                                          || e.mods.isCommandDown();
                if (! project.isTrackSelected (trackId))
                    project.selectTrack (trackId, toggleSelection);
                showTrackMenu();
                owner.grabFocus();
                return;
            }
            const bool toggleSelection = e.mods.isShiftDown()
                || e.mods.isCtrlDown() || e.mods.isCommandDown();
            project.selectTrack (trackId, toggleSelection);
            owner.grabFocus();
            return;
        }

        if (e.mods.isPopupMenu())
        {
            owner.lanes->clearSelection();
            if (! project.isTrackSelected (trackId))
                project.selectTrack (trackId);
            showTrackMenu();
            owner.grabFocus();
            return;
        }

        const bool toggleSelection = ! e.mods.isPopupMenu()
            && (e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown());
        owner.lanes->clearSelection();
        project.selectTrack (trackId, toggleSelection);
        owner.grabFocus();
    }

    void showTrackMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, theme::ui ("Nova trilha MIDI"));
        m.addItem (3, theme::ui ("Nova trilha de audio"));
        const bool canGroup = project.selectedTrackIds.size() >= 2
            && std::all_of (project.selectedTrackIds.begin(), project.selectedTrackIds.end(),
                [this] (int id)
                {
                    const auto* selected = project.track (id);
                    return selected != nullptr && selected->type != roll::TrackType::Group
                        && selected->parentGroupId == 0;
                });
        m.addItem (4, theme::ui ("Agrupar faixas selecionadas (Ctrl+G)"), canGroup);
        const int tid = trackId;
        const auto* selected = project.track (trackId);
        const bool isGroup = selected != nullptr && selected->type == roll::TrackType::Group;
        if (isGroup)
            m.addItem (5, theme::ui ("Desagrupar faixa (Ctrl+Shift+G)"));
        else
        {
            m.addItem (6, theme::ui ("Duplicar faixa (Ctrl+D)"));
            m.addItem (2, theme::ui ("Apagar trilha"), project.tracks.size() > 1);
        }
        auto* proj = &project;
        auto* arrangement = &owner;
        m.showMenuAsync ({}, [proj, arrangement, tid] (int r)
        {
            if (r == 1) proj->addTrack();
            if (r == 2) proj->removeTrack (tid);
            if (r == 3) proj->addTrack (roll::TrackType::Audio);
            if (r == 4) proj->groupTracks (proj->selectedTrackIds);
            if (r == 5) proj->ungroupTrack (tid);
            if (r == 6 && arrangement->onTrackDuplicateRequested)
                arrangement->onTrackDuplicateRequested (tid);
        });
    }

private:
    ProjectModel& project;
    ArrangementView& owner;
    int trackId;
    juce::Rectangle<int> volumeIconBounds;
    juce::Label name, volumeDb, panLabel, panValue;
    juce::TextButton mute { "ON" }, solo { "S" };
    juce::TextButton routingButton;
    juce::Slider volume, pan;
    float peak = 0.0f;
};

// ============================================================ MasterHeader (trilha Master)

class ArrangementView::MasterHeader : public juce::Component, public juce::DragAndDropTarget
{
public:
    MasterHeader (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        addAndMakeVisible (name);
        addAndMakeVisible (volume);
        addAndMakeVisible (dbLabel);
        addAndMakeVisible (peakDbLabel);

        name.setText ("Master", juce::dontSendNotification);
        name.setTooltip (theme::ui ("Clique para selecionar. Arraste um efeito para adiciona-lo ao Master."));
        name.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, theme::text());

        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setValue (0.0, juce::dontSendNotification);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTooltip (theme::ui ("Volume Master (-inf a +6.0 dB)"));
        volume.setMouseClickGrabsKeyboardFocus (false);

        dbLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        dbLabel.setColour (juce::Label::textColourId, theme::textDim());
        dbLabel.setJustificationType (juce::Justification::centredRight);
        updateDbText (0.0f);

        peakDbLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        peakDbLabel.setColour (juce::Label::textColourId, theme::text());
        peakDbLabel.setJustificationType (juce::Justification::centredRight);
        peakDbLabel.setTooltip (theme::ui ("Pico do Master em dBFS; clique no indicador para zerar"));
        updatePeakDbText (0.0f);

        volume.onValueChange = [this]
        {
            const float db = static_cast<float> (volume.getValue());
            updateDbText (db);
            if (project.masterTrack != nullptr)
            {
                project.masterTrack->volume = roll::decibelsToGain (db);
                project.notifyMixer();
            }
        };
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        const auto description = details.description.toString();
        return description.startsWith ("plugin:")
            && owner.canAcceptPluginDrop
            && owner.canAcceptPluginDrop (description.substring (7), 0);
    }

    void itemDragEnter (const SourceDetails&) override
    {
        pluginDragOver = true;
        repaint();
    }

    void itemDragMove (const SourceDetails&) override {}

    void itemDragExit (const SourceDetails&) override
    {
        pluginDragOver = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        pluginDragOver = false;
        repaint();

        const auto description = details.description.toString();
        if (description.startsWith ("plugin:") && owner.onPluginDropped)
            owner.onPluginDropped (description.substring (7), 0, 0.0);
    }

    void updateDbText (float db)
    {
        if (db <= -59.5f)
            dbLabel.setText ("-inf dB", juce::dontSendNotification);
        else
        {
            juce::String s = (db >= 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
            dbLabel.setText (s, juce::dontSendNotification);
        }
    }

    void setPeak (float p)
    {
        if (p >= 1.0f)
            clipHeld = true;

        updatePeakDbText (p);
        if (std::abs (p - peak) > 0.003f)
        {
            peak = p;
            repaint (getWidth() - 24, 0, 24, getHeight());
        }
    }

    void refreshText()
    {
        if (project.masterTrack != nullptr)
        {
            const float db = roll::gainToDecibels (static_cast<float> (project.masterTrack->volume));
            volume.setValue (db, juce::dontSendNotification);
            updateDbText (db);
        }
        repaint();
    }

    void updatePeakDbText (float level)
    {
        const auto text = level <= 0.001f
            ? juce::String ("-inf dBFS")
            : juce::String (roll::gainToDecibels (level), 1) + " dBFS";
        if (peakDbLabel.getText() != text)
            peakDbLabel.setText (text, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const bool sel = project.isTrackSelected (0);
        g.setColour (sel ? theme::panelLight().brighter (0.15f) : theme::panel());
        g.fillRect (getLocalBounds());

        // Faixa de cor da trilha Master (ouro/laranja)
        g.setColour (juce::Colour (0xffe67e22));
        g.fillRect (0, 0, 6, getHeight());

        g.setColour (theme::border());
        g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

        // Resumo de efeitos na Master
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (12.5f));
        const juce::String summary = owner.deviceSummary ? owner.deviceSummary (0) : juce::String();
        g.drawText (summary.isEmpty() ? theme::ui ("sem efeitos") : summary,
                    14, 26, getWidth() - 112, 16, juce::Justification::centredLeft, true);

        if (const auto* volumeIcon = icons::get (icons::Icon::Volume, theme::textDim(), 16))
            volumeIcon->drawWithin (g, volumeIconBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

        // Medidor de decibeis (dBFS) com indicador de clip
        const int meterX = getWidth() - 18;
        const int meterY = 6;
        const int meterW = 10;
        const int meterH = getHeight() - 12;
        const int clipBoxH = 7;
        const int barH = meterH - clipBoxH - 2;
        const int barY = meterY + clipBoxH + 2;

        // Clip indicator box
        g.setColour (clipHeld ? juce::Colour (0xffff3b30) : juce::Colours::black.withAlpha (0.45f));
        g.fillRect (meterX, meterY, meterW, clipBoxH);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawRect (meterX, meterY, meterW, clipBoxH, 1);

        // Fundo do medidor
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (meterX, barY, meterW, barH);

        // Nível em dB (-48 dB até 0 dB)
        const float db = roll::gainToDecibels (peak);
        const float norm = std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        const int fillH = static_cast<int> (norm * barH);

        if (fillH > 0)
        {
            // Cores: verde ate -12dB (norm < 0.75), amarelo/laranja ate 0dB
            const auto barColour = norm > 0.75f ? (norm >= 0.98f ? theme::warning() : juce::Colour (0xfff39c12))
                                                : theme::accent();
            g.setColour (barColour);
            g.fillRect (meterX, barY + barH - fillH, meterW, fillH);
        }

        // Marcas de -12 dB e 0 dB
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawHorizontalLine (barY, static_cast<float> (meterX - 2), static_cast<float> (meterX + meterW));
        g.drawHorizontalLine (barY + static_cast<int> (barH * 0.25f), static_cast<float> (meterX - 2), static_cast<float> (meterX + meterW));
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! pluginDragOver)
            return;

        g.setColour (theme::accent().withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent());
        g.drawRect (getLocalBounds(), 2);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (theme::ui ("Solte para adicionar efeito ao Master"),
                    getLocalBounds().reduced (8), juce::Justification::centred, true);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (6, 4);
        a.removeFromLeft (8);
        a.removeFromRight (22);
        auto topRow = a.removeFromTop (22);
        name.setBounds (topRow.removeFromLeft (70));
        dbLabel.setBounds (topRow);
        peakDbLabel.setBounds (getWidth() - 98, 26, 72, 16);
        a.removeFromTop (18);
        auto row = a.removeFromTop (24);
        volumeIconBounds = row.removeFromLeft (20);
        row.removeFromLeft (4);
        volume.setBounds (row);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        project.selectTrack (0);
        owner.grabFocus();

        // Se clicou no clip box, reseta o clip
        if (e.x >= getWidth() - 24 && e.y <= 16)
        {
            clipHeld = false;
            peak = 0.0f;
            updatePeakDbText (0.0f);
            if (owner.onMasterPeakReset)
                owner.onMasterPeakReset();
            repaint();
        }
    }

private:
    ProjectModel& project;
    ArrangementView& owner;
    juce::Label name, dbLabel, peakDbLabel;
    juce::Slider volume;
    juce::Rectangle<int> volumeIconBounds;
    float peak = 0.0f;
    bool clipHeld = false, pluginDragOver = false;
};

class ArrangementView::Overview final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Overview (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Arraste na horizontal para navegar e na vertical para ajustar o zoom; duplo clique no contorno para enquadrar o arrangement.");
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        g.fillAll (theme::panel().darker (0.25f));

        const double totalBeats = std::max (1.0, project.totalBeats);
        const float beatsToPixels = static_cast<float> (bounds.getWidth() / totalBeats);
        const int rowCount = std::max (1, static_cast<int> (project.tracks.size()));
        const float rowHeight = static_cast<float> (std::max (1, bounds.getHeight() - 4)) / rowCount;

        for (size_t i = 0; i < project.tracks.size(); ++i)
        {
            const auto& track = *project.tracks[i];
            const float y = 2.0f + static_cast<float> (i) * rowHeight;
            const float clipHeight = std::max (2.0f, std::min (rowHeight - 1.0f, 5.0f));
            const float clipY = y + std::max (0.0f, (rowHeight - clipHeight) * 0.5f);
            const auto clipColour = ArrangementView::colourFor (track.id);

            const auto drawClip = [&] (double start, double length)
            {
                if (length <= 0.0 || start + length < 0.0 || start > totalBeats)
                    return;
                const float x = static_cast<float> (std::max (0.0, start) * beatsToPixels);
                const float width = std::max (1.0f, static_cast<float> (
                    (std::min (totalBeats, start + length) - std::max (0.0, start)) * beatsToPixels));
                g.setColour (clipColour);
                g.fillRoundedRectangle (x, clipY, width, clipHeight, 1.5f);
            };

            if (track.type == roll::TrackType::Audio)
            {
                for (const auto& clip : track.audioClips)
                    drawClip (clip->start, clip->lengthBeats);
            }
            else
            {
                for (const auto& clip : track.clips)
                    drawClip (clip->start, clip->clip.length());
            }
        }

        const auto loopX = static_cast<float> (project.loopStart * beatsToPixels);
        const auto loopWidth = static_cast<float> ((project.loopEnd - project.loopStart) * beatsToPixels);
        g.setColour (theme::accent().withAlpha (0.12f));
        g.fillRect (loopX, 0.0f, loopWidth, static_cast<float> (bounds.getHeight()));

        const auto view = visibleRange();
        g.setColour (theme::accent().withAlpha (0.2f));
        g.fillRect (view);
        g.setColour (theme::accent());
        g.drawRect (view, 1.5f);

        const float playheadX = static_cast<float> (owner.playhead * beatsToPixels);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (static_cast<int> (playheadX), 1.0f,
                            static_cast<float> (bounds.getBottom() - 1));
        g.setColour (theme::border());
        g.drawRect (bounds);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto currentView = visibleRange();
        const double totalBeats = std::max (1.0, project.totalBeats);
        const double clickedBeat = static_cast<double> (e.position.x / std::max (1, getWidth())) * totalBeats;
        const bool clickedInsideView = currentView.contains (e.position);
        if (! clickedInsideView)
        {
            const double visibleBeats = owner.viewport.getMaximumVisibleWidth() / owner.pixelsPerBeat;
            owner.viewport.setViewPosition (
                std::max (0, static_cast<int> (std::lround ((clickedBeat - visibleBeats * 0.5) * owner.pixelsPerBeat))),
                owner.viewport.getViewPositionY());
            owner.syncPositions();
        }

        dragStartX = e.position.x;
        lastZoomY = e.position.y;
        dragStartViewX = owner.viewport.getViewPositionX();
        zoomAnchorBeat = clickedBeat;
        zoomAnchorScreenX = clickedInsideView
            ? static_cast<int> (std::lround (e.position.x))
            : owner.viewport.getMaximumVisibleWidth() / 2;
        gesture = Gesture::Pending;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (gesture == Gesture::Pending)
        {
            if (e.getDistanceFromDragStart() < 4.0f)
                return;
            gesture = std::abs (e.position.x - dragStartX) > std::abs (e.position.y - lastZoomY)
                ? Gesture::Pan : Gesture::Zoom;
        }

        if (gesture == Gesture::Zoom)
        {
            const float deltaY = e.position.y - lastZoomY;
            lastZoomY = e.position.y;
            if (deltaY != 0.0f)
                owner.zoomAt (std::exp (static_cast<double> (deltaY) * 0.012),
                              zoomAnchorBeat, zoomAnchorScreenX);
            return;
        }

        const double totalBeats = std::max (1.0, project.totalBeats);
        const double pixelsPerOverviewPixel = totalBeats * owner.pixelsPerBeat / std::max (1, getWidth());
        const auto deltaX = static_cast<int> (std::lround (
            (e.position.x - dragStartX) * pixelsPerOverviewPixel));
        owner.viewport.setViewPosition (std::max (0, dragStartViewX + deltaX),
                                        owner.viewport.getViewPositionY());
        owner.syncPositions();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        gesture = Gesture::Pending;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! visibleRange().contains (e.position))
            return;

        const double totalBeats = std::max (1.0, project.totalBeats);
        const int viewWidth = std::max (1, owner.viewport.getMaximumVisibleWidth());
        const double fitPixelsPerBeat = std::min (600.0, static_cast<double> (viewWidth) / totalBeats);
        owner.zoomAt (fitPixelsPerBeat / owner.pixelsPerBeat, totalBeats * 0.5, viewWidth / 2);
    }

private:
    enum class Gesture { Pending, Pan, Zoom };

    juce::Rectangle<float> visibleRange() const
    {
        const double totalBeats = std::max (1.0, project.totalBeats);
        const float scale = static_cast<float> (getWidth() / totalBeats);
        const double ppb = std::max (1.0e-6, owner.pixelsPerBeat);
        const float x = static_cast<float> (owner.viewport.getViewPositionX() / ppb) * scale;
        const float width = std::min (static_cast<float> (getWidth()),
            static_cast<float> (owner.viewport.getMaximumVisibleWidth() / ppb) * scale);
        return { std::clamp (x, 0.0f, static_cast<float> (getWidth()) - width),
                 1.0f, width, static_cast<float> (std::max (1, getHeight() - 2)) };
    }

    ProjectModel& project;
    ArrangementView& owner;
    float dragStartX = 0.0f;
    float lastZoomY = 0.0f;
    int dragStartViewX = 0;
    int zoomAnchorScreenX = 0;
    double zoomAnchorBeat = 0.0;
    Gesture gesture = Gesture::Pending;
};

// ============================================================ ArrangementView

ArrangementView::ArrangementView (ProjectModel& p) : project (p)
{
    lanes = std::make_unique<Lanes> (project, *this);
    ruler = std::make_unique<Ruler> (project, *this);
    overview = std::make_unique<Overview> (project, *this);
    addAndMakeVisible (viewport);
    addAndMakeVisible (headerHolder);
    addAndMakeVisible (rulerHolder);
    addAndMakeVisible (*overview);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (addAudioTrackButton);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (locatorPreviousButton);
    addAndMakeVisible (locatorSetButton);
    addAndMakeVisible (locatorNextButton);
    addAndMakeVisible (locatorDeleteButton);
    rulerHolder.addAndMakeVisible (*ruler);

    viewport.setViewedComponent (lanes.get(), false);
    viewport.setScrollBarsShown (true, false);

    addTrackButton.setMouseClickGrabsKeyboardFocus (false);
    addAudioTrackButton.setMouseClickGrabsKeyboardFocus (false);
    addTrackButton.setTooltip ("Adicionar trilha MIDI (Ctrl+Shift+T)");
    addAudioTrackButton.setTooltip ("Adicionar trilha de audio (Ctrl+T)");
    gridBox.setMouseClickGrabsKeyboardFocus (false);
    locatorPreviousButton.setMouseClickGrabsKeyboardFocus (false);
    locatorSetButton.setMouseClickGrabsKeyboardFocus (false);
    locatorNextButton.setMouseClickGrabsKeyboardFocus (false);
    locatorDeleteButton.setMouseClickGrabsKeyboardFocus (false);
    locatorPreviousButton.setTooltip ("Ir para a marcacao anterior");
    locatorSetButton.setTooltip ("Adicionar marcacao no playhead (Ctrl+Shift+M)");
    locatorNextButton.setTooltip ("Ir para a proxima marcacao");
    locatorDeleteButton.setTooltip ("Remover a marcacao selecionada");
    locatorPreviousButton.onClick = [this] { navigateLocator (false); };
    locatorSetButton.onClick = [this] { project.addLocator (snapBeat (playhead)); };
    locatorNextButton.onClick = [this] { navigateLocator (true); };
    locatorDeleteButton.onClick = [this]
    {
        int locatorId = project.locator (project.selectedLocatorId) != nullptr
            ? project.selectedLocatorId : -1;
        if (locatorId < 0 && ! project.locators.empty())
            locatorId = std::max_element (project.locators.begin(), project.locators.end(),
                [] (const auto& a, const auto& b) { return a.id < b.id; })->id;
        if (locatorId >= 0)
            project.removeLocator (locatorId);
    };
    addTrackButton.onClick = [this] { project.addTrack(); };
    addAudioTrackButton.onClick = [this] { project.addTrack (roll::TrackType::Audio); };
    static constexpr double gridValues[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                           2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                           1.0 / 12.0, 4.0 };
    static constexpr const char* gridLabels[] { "1/4", "1/8", "1/16", "1/32", "1/64",
                                                "1/4T", "1/8T", "1/16T", "1/32T", "1 compasso" };
    for (int i = 0; i < 10; ++i)
        gridBox.addItem (gridLabels[i], i + 1);
    gridBox.addItem ("Sem grade", 11);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.onChange = [this]
    {
        const int id = gridBox.getSelectedId();
        if (id >= 1 && id <= 10)
            setGridBeat (gridValues[id - 1]);
        else if (id == 11)
            setGridBeat (0.0);
    };

    project.addChangeListener (this);
    refresh();
    updateLocatorControls();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
    project.removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void ArrangementView::grabFocus() { lanes->grabKeyboardFocus(); }

void ArrangementView::zoom (double factor)
{
    const auto viewX = viewport.getViewPosition().x;
    const auto anchorScreenX = viewport.getWidth() / 2;
    zoomAt (factor, static_cast<double> (viewX + anchorScreenX) / pixelsPerBeat, anchorScreenX);
}

void ArrangementView::zoomAt (double factor, double anchorBeat, int anchorScreenX)
{
    const double fitScale = static_cast<double> (std::max (1, viewport.getMaximumVisibleWidth()))
                          / std::max (1.0, project.totalBeats);
    pixelsPerBeat = std::clamp (pixelsPerBeat * factor, std::min (4.0, fitScale), 600.0);
    updateSizes();
    const auto viewPosition = viewport.getViewPosition();
    viewport.setViewPosition (std::max (0, static_cast<int> (std::lround (anchorBeat * pixelsPerBeat
                                                                          - anchorScreenX))),
                              viewPosition.y);
    syncPositions();
    lanes->repaint();
    ruler->repaint();
}

double ArrangementView::snapBeat (double beat) const
{
    if (gridBeat <= 0.0)
        return std::max (0.0, beat);
    return std::max (0.0, std::round (beat / gridBeat) * gridBeat);
}

void ArrangementView::setGridBeat (double beats)
{
    gridBeat = std::max (0.0, beats);
    if (gridBeat > 0.0)
        lastGridBeat = gridBeat;
    static constexpr double values[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                       2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                       1.0 / 12.0, 4.0 };
    int selectedId = 0;
    for (int i = 0; i < 10; ++i)
        if (std::abs (gridBeat - values[i]) < 1.0e-9)
            selectedId = i + 1;
    if (gridBeat <= 0.0)
        selectedId = 11;
    gridBox.setSelectedId (selectedId, juce::dontSendNotification);
    lanes->repaint();
    if (onGridChanged) onGridChanged();
}

void ArrangementView::setPlayhead (double beats)
{
    const int oldX = static_cast<int> (playhead * pixelsPerBeat);
    lanes->setPlayhead (beats);
    ruler->repaint (oldX - 6, 0, 14, rulerHeight);
    ruler->repaint (static_cast<int> (beats * pixelsPerBeat) - 6, 0, 14, rulerHeight);
    overview->repaint();
}

void ArrangementView::setLoopEnabled (bool on)
{
    loopEnabled = on;
    lanes->repaint();
    ruler->repaint();
}

bool ArrangementView::getTimeSelectionStart (double& startBeat) const
{
    return lanes->getTimeSelectionStart (startBeat);
}

void ArrangementView::refresh()
{
    for (auto& h : headers) headerHolder.removeChildComponent (h.get());
    headers.clear();
    for (const auto& t : project.tracks)
    {
        headers.push_back (std::make_unique<Header> (project, *this, t->id));
        headerHolder.addAndMakeVisible (*headers.back());
    }
    if (masterHeader == nullptr)
    {
        masterHeader = std::make_unique<MasterHeader> (project, *this);
        headerHolder.addAndMakeVisible (*masterHeader);
    }
    updateSizes();
    resized();
}

void ArrangementView::updateSizes()
{
    lanes->updateSize();
    ruler->setSize (std::max (1, lanes->getWidth()), rulerHeight);
    syncPositions();
}

void ArrangementView::syncPositions()
{
    const auto vp = viewport.getViewPosition();
    ruler->setTopLeftPosition (-vp.x, 0);
    overview->repaint();
    headerHolder.setSize (headerWidth, viewport.getMaximumVisibleHeight());
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, static_cast<int> (i) * trackHeight - vp.y, headerWidth, trackHeight);
    if (masterHeader != nullptr)
        masterHeader->setBounds (0, static_cast<int> (headers.size()) * trackHeight - vp.y, headerWidth, trackHeight);
}

void ArrangementView::updateLocatorControls()
{
    const bool anyLocators = ! project.locators.empty();
    locatorPreviousButton.setEnabled (anyLocators);
    locatorNextButton.setEnabled (anyLocators);
    locatorDeleteButton.setEnabled (anyLocators);
}

void ArrangementView::navigateLocator (bool forward)
{
    if (project.locators.empty())
        return;

    auto it = std::find_if (project.locators.begin(), project.locators.end(),
        [this] (const auto& marker) { return marker.id == project.selectedLocatorId; });
    if (it != project.locators.end())
    {
        if (forward)
        {
            if (++it == project.locators.end())
            {
                project.selectLocator (-1);
                if (onSeek) onSeek (project.totalBeats);
                return;
            }
        }
        else
        {
            if (it == project.locators.begin())
            {
                project.selectLocator (-1);
                if (onSeek) onSeek (0.0);
                return;
            }
            --it;
        }
    }
    else if (forward)
    {
        it = std::upper_bound (project.locators.begin(), project.locators.end(), playhead,
            [] (double beat, const auto& marker) { return beat < marker.beat; });
        if (it == project.locators.end())
        {
            if (onSeek) onSeek (project.totalBeats);
            return;
        }
    }
    else
    {
        it = std::lower_bound (project.locators.begin(), project.locators.end(), playhead,
            [] (const auto& marker, double beat) { return marker.beat < beat; });
        if (it == project.locators.begin())
        {
            if (onSeek) onSeek (0.0);
            return;
        }
        --it;
    }

    project.selectLocator (it->id);
    if (onSeek) onSeek (it->beat);
    if (onArrangementSeekRequested) onArrangementSeekRequested (it->beat);
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (headers.size() != project.tracks.size()) { refresh(); }
    else
    {
        updateSizes();
        for (auto& h : headers) h->refreshText();
        if (masterHeader != nullptr) masterHeader->refreshText();
    }
    updateLocatorControls();
    lanes->repaint();
    ruler->repaint();
}

void ArrangementView::timerCallback()
{
    const auto vp = viewport.getViewPosition();
    static juce::Point<int> last;
    if (vp != last) { last = vp; syncPositions(); }

    if (meterProvider)
    {
        for (auto& h : headers) h->setPeak (meterProvider (h->id()));
        if (masterHeader != nullptr) masterHeader->setPeak (meterProvider (0));
    }
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    if (lanes->hasKeyboardFocus (false))
    {
        g.setColour (theme::accent());
        g.drawRect (getLocalBounds(), 2);
    }
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();
    auto overviewArea = area.removeFromTop (overviewHeight);
    overviewArea.removeFromLeft (headerWidth);
    overview->setBounds (overviewArea);
    auto top = area.removeFromTop (rulerHeight);
    auto addTrackArea = top.removeFromLeft (headerWidth).reduced (4);
    addTrackButton.setBounds (addTrackArea.removeFromLeft (56));
    addTrackArea.removeFromLeft (3);
    addAudioTrackButton.setBounds (addTrackArea.removeFromLeft (64));
    addTrackArea.removeFromLeft (3);
    gridBox.setBounds (addTrackArea);
    auto locatorControls = top;
    rulerHolder.setBounds (top);
    int right = locatorControls.getRight() - 4;
    const int buttonY = 4;
    const int buttonHeight = rulerHeight - 8;
    locatorDeleteButton.setBounds (right - 36, buttonY, 34, buttonHeight);
    right -= 38;
    locatorNextButton.setBounds (right - 26, buttonY, 24, buttonHeight);
    right -= 28;
    locatorSetButton.setBounds (right - 26, buttonY, 24, buttonHeight);
    right -= 28;
    locatorPreviousButton.setBounds (right - 26, buttonY, 24, buttonHeight);
    auto left = area.removeFromLeft (headerWidth);
    viewport.setBounds (area);
    headerHolder.setBounds (left.withHeight (viewport.getMaximumVisibleHeight()));
    updateSizes();
}
