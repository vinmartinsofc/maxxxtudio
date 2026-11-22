#include "ArrangementView.h"
#include "AppModalDialog.h"
#include "AudioEngine.h"
#include "Icons.h"
#include "ThemeKnobSlider.h"
#include "WaveformDisplay.h"
#include "ZoomCursor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

juce::Colour ArrangementView::colourFor (int)
{
    return theme::accent();
}

namespace
{
constexpr std::array<std::uint32_t, 9> trackColours {
    0xfff44336, 0xffff9800, 0xffffeb3b, 0xff4caf50, 0xff00bcd4,
    0xff2196f3, 0xff3f51b5, 0xff9c27b0, 0xff9e9e9e
};
constexpr std::array<const char*, trackColours.size()> trackColourNames {
    "Red", "Orange", "Yellow", "Green", "Cyan",
    "Blue", "Indigo", "Purple", "Gray"
};

juce::Colour spectrumWaveformColour (float frequency, float flatness)
{
    constexpr std::array<float, 7> frequencies {
        40.0f, 100.0f, 220.0f, 600.0f, 1500.0f, 5000.0f, 16000.0f
    };
    constexpr std::array<std::uint32_t, 7> colours {
        0xffff3030, 0xffff8618, 0xfff4e900, 0xff16d34b,
        0xff00d9e8, 0xff416dff, 0xffb23cff
    };
    if (frequency <= 0.0f)
        return juce::Colour (0xffff3030);

    frequency = juce::jlimit (frequencies.front(), frequencies.back(), frequency);
    juce::Colour colour (colours.back());
    for (std::size_t i = 1; i < frequencies.size(); ++i)
        if (frequency <= frequencies[i])
        {
            const float proportion = std::log (frequency / frequencies[i - 1])
                / std::log (frequencies[i] / frequencies[i - 1]);
            colour = juce::Colour (colours[i - 1]).interpolatedWith (
                juce::Colour (colours[i]), proportion);
            break;
        }

    const auto noiseTint = juce::Colour (0xffddd4ed);
    return colour.interpolatedWith (noiseTint, juce::jlimit (0.0f, 0.9f, flatness * 0.9f));
}

void drawSpectrumWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                           const roll::AudioPlacement& clip,
                           const std::vector<WaveformColumn>& spectrum,
                           double sourceDurationSeconds, float halfHeight)
{
    if (clip.waveform.empty() || spectrum.empty() || sourceDurationSeconds <= 0.0
        || area.getWidth() < 1.0f || area.getHeight() < 1.0f)
        return;

    constexpr int columnStep = 2;
    constexpr float barWidth = 1.5f;
    const int columns = std::max (1, static_cast<int> (std::ceil (area.getWidth() / columnStep)));
    const float centre = area.getCentreY();

    for (int column = 0; column < columns; ++column)
    {
        const double proportion = (static_cast<double> (column) + 0.5) / columns;
        const double beat = proportion * clip.lengthBeats;
        double sourceSeconds = roll::audioSourceSecondsAtBeat (clip, beat);
        if (clip.reverse)
            sourceSeconds = clip.sourceOffsetSeconds + clip.durationSeconds
                          - (sourceSeconds - clip.sourceOffsetSeconds);
        const auto spectrumIndex = static_cast<std::size_t> (std::clamp (
            sourceSeconds / sourceDurationSeconds * spectrum.size(), 0.0,
            static_cast<double> (spectrum.size() - 1)));
        const auto& analysis = spectrum[spectrumIndex];

        const double firstProportion = static_cast<double> (column) / columns;
        const auto first = std::min (clip.waveform.size() - 1,
            static_cast<std::size_t> (firstProportion * clip.waveform.size()));
        const auto last = std::max (first + 1, std::min (clip.waveform.size(),
            static_cast<std::size_t> ((static_cast<double> (column + 1) / columns)
                                      * clip.waveform.size())));
        float peak = 0.0f;
        for (auto bin = first; bin < last; ++bin)
            peak = std::max (peak, std::max (std::abs (clip.waveform[bin].minimum),
                                             std::abs (clip.waveform[bin].maximum)));

        const float amplitude = std::pow (std::clamp (peak, 0.0f, 1.0f), 0.72f);
        const float x = area.getX() + static_cast<float> (column * columnStep);
        const float width = std::min (barWidth, area.getRight() - x);
        const float height = std::max (0.75f, amplitude * halfHeight);
        const auto colour = amplitude < 0.004f || analysis.centroid < 40.0f
            ? juce::Colour (0xffff3030)
            : spectrumWaveformColour (analysis.centroid, analysis.flatness);
        g.setColour (colour);
        g.fillRect (x, centre - height, width, height * 2.0f);
    }
}

juce::String formatTimelineTime (double seconds)
{
    const int totalSeconds = std::max (0, juce::roundToInt (seconds));
    return juce::String (totalSeconds / 60).paddedLeft ('0', 2)
         + ":" + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
}

int arrangementBarStride (double pixelsPerBeat)
{
    int bars = 1;
    while (bars < (1 << 20) && bars * 4.0 * pixelsPerBeat < 64.0)
        bars *= 2;
    return bars;
}

void showMidiMappingMenu (juce::Component& component, const juce::String& mappingInfo,
                          std::function<void()> onLearn, std::function<void()> onClear)
{
    juce::PopupMenu menu;
    if (mappingInfo.isNotEmpty())
        menu.addSectionHeader ("MIDI: " + mappingInfo);
    menu.addItem (1, theme::ui ("Learn MIDI Control..."));
    menu.addItem (2, theme::ui ("Remove MIDI Mapping"), mappingInfo.isNotEmpty());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&component),
                        [onLearn = std::move (onLearn), onClear = std::move (onClear)] (int result)
                        {
                            if (result == 1 && onLearn)
                                onLearn();
                            else if (result == 2 && onClear)
                                onClear();
                        });
}

juce::String midiMappingInfo (const ProjectModel& project, int trackId, MidiMappingTarget target)
{
    const auto mapping = std::find_if (project.midiMappings.begin(), project.midiMappings.end(),
        [trackId, target] (const MidiMapping& candidate)
        {
            return candidate.trackId == trackId && candidate.target == target;
        });
    if (mapping == project.midiMappings.end())
        return {};

    return "Ch " + juce::String (mapping->channel) + " / "
         + (mapping->messageType == MidiMappingMessageType::ControlChange ? "CC " : "Note ")
         + juce::String (mapping->number);
}

class MidiLearnButton final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::function<void()> onMidiLearn, onMidiClear;
    juce::String mappingInfo;

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            showMidiMappingMenu (*this, mappingInfo, onMidiLearn, onMidiClear);
            return;
        }

        juce::TextButton::mouseDown (event);
    }
};

class MixerKnobSlider final : public ThemeKnobSlider
{
public:
    std::function<void()> onDoubleClick, onMidiLearn, onMidiClear;
    juce::String mappingInfo;

    MixerKnobSlider()
    {
        setScrollWheelEnabled (true);
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (onDoubleClick)
            onDoubleClick();
        else
            juce::Slider::mouseDoubleClick (event);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            showMidiMappingMenu (*this, mappingInfo, onMidiLearn, onMidiClear);
            return;
        }

        if (event.mods.isLeftButtonDown())
            wheelControlEnabled = true;
        juce::Slider::mouseDown (event);
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheelControlEnabled)
            juce::Slider::mouseWheelMove (event, wheel);
        else
            juce::Component::mouseWheelMove (event, wheel);
    }

private:
    bool wheelControlEnabled = false;
};
}

juce::Colour ArrangementView::colourFor (const roll::Track& track)
{
    return track.colour == 0 ? colourFor (track.id)
                             : juce::Colour (static_cast<juce::uint32> (track.colour));
}

// ============================================================ Lanes (clips)

class ArrangementView::Lanes : public juce::Component, public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    enum class Mode { None, Move, Resize, ResizeLeft, AutomationEdit, FadeIn, FadeOut, FadeInCurve, FadeOutCurve,
                      RangeSelect, EmptyClickPending, ClipTimeSelectPending, ShiftPending, CtrlPending };

    struct ClipSelection
    {
        int trackId = -1;
        int clipId = -1;
        bool audio = false;
    };

    struct MultiDragSource
    {
        ClipSelection selection;
        int trackIndex = -1;
        double start = 0.0, length = 0.0;
        std::unique_ptr<roll::Placement> midiClip;
        std::unique_ptr<roll::AudioPlacement> audioClip;
    };

    struct MovePreviewItem
    {
        int trackIndex = -1;
        double start = 0.0, length = 0.0;
        bool replacesSource = false;
    };

    Lanes (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    void timerCallback() override
    {
        if (dragActive && (mode == Mode::Move || mode == Mode::RangeSelect))
            autoScrollDuringDrag();

        if (mode == Mode::FadeIn || mode == Mode::FadeOut
            || mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve
            || mode == Mode::RangeSelect || mode == Mode::AutomationEdit)
            repaint();
    }

    void autoScrollDuringDrag()
    {
        const auto viewportBounds = owner.viewport.getScreenBounds();
        const auto mouse = juce::Desktop::getMousePosition();
        const int pointerX = mouse.x - viewportBounds.getX();
        const int visibleWidth = owner.viewport.getMaximumVisibleWidth();
        constexpr int edgeSize = 36;
        int delta = 0;
        if (pointerX < edgeSize)
            delta = -std::clamp ((edgeSize - pointerX) / 2, 2, 24);
        else if (pointerX > visibleWidth - edgeSize)
            delta = std::clamp ((pointerX - (visibleWidth - edgeSize)) / 2, 2, 24);
        else
            return;

        int viewX = owner.viewport.getViewPositionX();
        int maxViewX = std::max (0, getWidth() - visibleWidth);
        if (delta > 0 && viewX >= maxViewX)
        {
            const double visibleBeats = visibleWidth / owner.pixelsPerBeat;
            project.growTo (project.totalBeats + std::max (16.0, visibleBeats));
            owner.updateSizes();
            viewX = owner.viewport.getViewPositionX();
            maxViewX = std::max (0, getWidth() - visibleWidth);
        }

        const int nextViewX = std::clamp (viewX + delta, 0, maxViewX);
        if (nextViewX == viewX)
            return;

        owner.viewport.setViewPosition (nextViewX, owner.viewport.getViewPositionY());
        owner.syncPositions();
        const auto currentView = owner.viewport.getViewPosition();
        const juce::Point<float> pointerInLanes (
            static_cast<float> (pointerX + currentView.x),
            static_cast<float> (mouse.y - viewportBounds.getY() + currentView.y));
        const auto modifiers = juce::ModifierKeys::getCurrentModifiers();
        if (mode == Mode::Move)
            updateMovePreview (pointerInLanes, modifiers);
        else
            updateRangeSelection (pointerInLanes, modifiers);
        repaint();
    }

    void updateRangeSelection (juce::Point<float> position, juce::ModifierKeys modifiers)
    {
        const double beat = position.x / owner.pixelsPerBeat;
        rangeEndBeat = std::max (0.0, modifiers.isAltDown() ? beat : owner.snapBeat (beat));
        rangeEndTrack = std::clamp (owner.rowAt (juce::roundToInt (position.y)),
                                    0, static_cast<int> (project.tracks.size()) - 1);
    }

    double snapClipBeat (double beat) const
    {
        const double grid = std::max (1.0 / 64.0,
                                      owner.gridBeats() > 0.0 ? owner.gridBeats()
                                                             : owner.lastGridBeat);
        return std::max (0.0, std::round (beat / grid) * grid);
    }

    double moveClipBeat (double beat, juce::ModifierKeys modifiers) const
    {
        if (owner.gridBeats() > 0.0 && modifiers.isAltDown())
            return std::max (0.0, beat);
        return snapClipBeat (beat);
    }

    void updateMovePreview (juce::Point<float> position, juce::ModifierKeys modifiers)
    {
        const double beat = position.x / owner.pixelsPerBeat;
        const double requestedStart = origStart + (beat - downBeat);
        const double newStart = moveClipBeat (requestedStart, modifiers);
        const int ti = std::clamp (owner.rowAt (juce::roundToInt (position.y)),
                                   0, static_cast<int> (project.tracks.size()) - 1);
        const auto* targetTrack = project.tracks[static_cast<std::size_t> (ti)].get();
        movePreviewActive = true;
        movePreviewStart = newStart;
        movePreviewTrackIndex = ti;
        movePreviewIsCopy = movePreviewIsCopy || modifiers.isCtrlDown();
        multiMovePreviewItems.clear();
        if (multiDragSources.size() > 1)
        {
            const int trackOffset = ti - multiDragAnchorTrackIndex;
            const double beatOffset = newStart - origStart;
            movePreviewAllowed = true;
            movePreviewReplacesSource = false;
            for (const auto& source : multiDragSources)
            {
                const int targetIndex = source.trackIndex + trackOffset;
                const double targetStart = source.start + beatOffset;
                if (targetIndex < 0 || targetIndex >= static_cast<int> (project.tracks.size())
                    || targetStart < 0.0)
                {
                    movePreviewAllowed = false;
                    continue;
                }

                const auto* target = project.tracks[static_cast<std::size_t> (targetIndex)].get();
                const auto* sourceTrack = project.track (source.selection.trackId);
                if (target == nullptr || sourceTrack == nullptr || target->type != sourceTrack->type)
                {
                    movePreviewAllowed = false;
                    continue;
                }

                const bool replacesSource = std::any_of (multiDragSources.begin(), multiDragSources.end(),
                    [&source, target, targetStart] (const auto& other)
                    {
                        if (other.selection.trackId != target->id)
                            return false;
                        return targetStart < other.start + other.length - 1.0e-9
                            && targetStart + source.length > other.start + 1.0e-9;
                    });
                movePreviewReplacesSource = movePreviewReplacesSource || replacesSource;
                multiMovePreviewItems.push_back ({ targetIndex, targetStart, source.length, replacesSource });
            }
            movePreviewAllowed = movePreviewAllowed
                && multiMovePreviewItems.size() == multiDragSources.size();
            setMouseCursor (movePreviewIsCopy ? juce::MouseCursor::CopyingCursor
                                              : juce::MouseCursor::DraggingHandCursor);
            return;
        }

        const auto* sourceTrack = project.track (curTrack);
        movePreviewReplacesSource = movePreviewIsCopy && targetTrack != nullptr
            && curTrack == targetTrack->id
            && newStart < origStart + origLen - 1.0e-9
            && newStart + origLen > origStart + 1.0e-9;
        movePreviewAllowed = targetTrack != nullptr && sourceTrack != nullptr
            && targetTrack->type == sourceTrack->type;
        setMouseCursor (movePreviewIsCopy ? juce::MouseCursor::CopyingCursor
                                          : juce::MouseCursor::DraggingHandCursor);
    }

    void updateSize()
    {
        setSize (std::max (1, static_cast<int> (project.totalBeats * owner.pixelsPerBeat)),
                 std::max (1, owner.rowTop (static_cast<int> (project.tracks.size() + 1))));
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
        selectedFrozenTrackId = -1;
        hasRangeSelection = false;
        shiftRangeAnchorBeat = -1.0;
        shiftRangeAnchorTrack = -1;
        repaint();
    }

    bool getTimeSelectionStart (double& startBeat) const
    {
        if (! hasRangeSelection || std::abs (rangeEndBeat - rangeStartBeat) < 1.0e-9)
            return false;

        startBeat = std::min (rangeStartBeat, rangeEndBeat);
        return true;
    }

    bool hasSelectionToDuplicate() const
    {
        return hasRangeSelection
            || (! selectedClips.empty()
                && (project.selectedClipId >= 0 || project.selectedAudioClipId >= 0));
    }

    void duplicateSelection()
    {
        if (hasSelectionToDuplicate())
            duplicateSelectedClips();
    }

    void paint (juce::Graphics& g) override
    {
        const auto cb = g.getClipBounds();
        const double ppb = owner.pixelsPerBeat;

        const size_t totalRows = project.tracks.size() + 1;
        for (size_t i = 0; i < totalRows; ++i)
        {
            const int row = static_cast<int> (i);
            const int y = owner.rowTop (row);
            const int height = owner.rowHeight (row);
            if (height <= 0)
                continue;

            if (i == project.tracks.size())
            {
                g.setColour (theme::panel().darker (0.2f));
                g.fillRect (cb.getX(), y, cb.getWidth(), height);
                g.setColour (theme::textDim().withAlpha (0.45f));
                g.setFont (juce::FontOptions (12.5f));
                g.drawText (theme::ui ("Master Bus (Stereo Output 1/2)"), cb.getX() + 12, y + 4, 300, 20, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour ((i % 2 == 0) ? theme::rowWhite() : theme::rowBlack());
                g.fillRect (cb.getX(), y, cb.getWidth(), height);
                if (project.isTrackSelected (project.tracks[i]->id))
                {
                    g.setColour (theme::accent().withAlpha (0.10f));
                    g.fillRect (cb.getX(), y, cb.getWidth(), height);
                }
            }
            g.setColour (theme::border());
            g.drawHorizontalLine (y + height - 1, static_cast<float> (cb.getX()), static_cast<float> (cb.getRight()));
        }

        // Loop region.
        const float lx0 = static_cast<float> (project.loopStart * ppb), lx1 = static_cast<float> (project.loopEnd * ppb);
        g.setColour (theme::accent().withAlpha (owner.loopEnabled ? 0.07f : 0.03f));
        g.fillRect (lx0, 0.0f, lx1 - lx0, static_cast<float> (getHeight()));

        // linhas de compasso/beat
        const int firstBeat = std::max (0, static_cast<int> (std::floor (cb.getX() / ppb)));
        const int lastBeat = static_cast<int> (std::ceil (cb.getRight() / ppb));
        const int barsPerMajorLine = arrangementBarStride (ppb);
        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const bool bar = (b % 4 == 0);
            if (! bar && ppb < 14.0) continue;
            if (bar && (b / 4) % barsPerMajorLine != 0) continue;
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
            if (owner.rowHeight (static_cast<int> (i)) <= 0)
                continue;

            const auto base = colourFor (t);
            if (t.frozen)
            {
                if (t.frozenAudioClip == nullptr)
                    continue;
                const auto& clip = *t.frozenAudioClip;
                const int rowTop = owner.rowTop (static_cast<int> (i));
                const int rowHeight = owner.rowHeight (static_cast<int> (i));
                const juce::Rectangle<float> r (
                    static_cast<float> (clip.start * ppb),
                    static_cast<float> (rowTop + (t.collapsed ? 3 : 4)),
                    static_cast<float> (clip.lengthBeats * ppb),
                    static_cast<float> (rowHeight - (t.collapsed ? 6 : 9)));
                if (r.toNearestInt().intersects (cb))
                {
                    const bool selected = selectedFrozenTrackId == t.id;
                    g.setColour (selected ? base.brighter (0.25f) : base.darker (0.25f));
                    g.fillRoundedRectangle (r, 4.0f);
                    g.setColour (selected ? juce::Colours::white : theme::accent().withAlpha (0.9f));
                    g.drawRoundedRectangle (r, 4.0f, selected ? 1.6f : 1.5f);
                    if (r.getWidth() > 10.0f)
                    {
                        const auto waveformArea = r.reduced (3.0f, 4.0f);
                        g.setColour (base.brighter (0.7f));
                        drawWaveformEnvelope (g, waveformArea, clip.waveform,
                                              base.brighter (0.7f), r.getHeight() * 0.4f);
                        g.setColour (theme::text());
                        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                        g.drawText (clip.name, r.toNearestInt().reduced (5, 1),
                                    juce::Justification::centredLeft, true);
                    }
                }
            }
            else if (t.type == roll::TrackType::Audio)
            {
                for (const auto& clip : t.audioClips)
                {
                    const int rowTop = owner.rowTop (static_cast<int> (i));
                    const int rowHeight = owner.rowHeight (static_cast<int> (i));
                    juce::Rectangle<float> r (static_cast<float> (clip->start * ppb),
                                              static_cast<float> (rowTop + (t.collapsed ? 3 : 4)),
                                              static_cast<float> (clip->lengthBeats * ppb),
                                              static_cast<float> (rowHeight - (t.collapsed ? 6 : 9)));
                    if (! r.toNearestInt().intersects (cb)) continue;

                    const bool selected = isClipSelected (t.id, clip->id, true);
                    g.setColour (selected ? base.brighter (0.25f) : base);
                    g.fillRoundedRectangle (r, 4.0f);
                    if (t.collapsed)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.8f));
                        g.drawText (juce::String::fromUTF8 (clip->name.c_str()), r.toNearestInt().reduced (5),
                                    juce::Justification::centredLeft, true);
                        if (! clip->enabled)
                        {
                            g.setColour (juce::Colours::black.withAlpha (0.62f));
                            g.fillRoundedRectangle (r, 4.0f);
                        }
                        if (selected)
                        {
                            g.setColour (juce::Colours::white);
                            g.drawRoundedRectangle (r, 4.0f, 1.6f);
                        }
                        continue;
                    }
                    g.saveState();
                    g.reduceClipRegion (r.toNearestInt());
                    g.setColour (juce::Colours::black.withAlpha (0.28f));
                    g.fillRect (r.withHeight (14.0f));
                    g.setColour (juce::Colours::black.withAlpha (0.8f));
                    g.setFont (juce::FontOptions (12.5f));
                    g.drawText (clip->name, r.toNearestInt().withTrimmedLeft (5).withHeight (14),
                                juce::Justification::centredLeft, true);

                    const auto waveformArea = r.withY (r.getY() + 15.0f).withHeight (r.getHeight() - 15.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.8f));
                    g.fillRect (waveformArea);
                    const float halfHeight = 24.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.24f));
                    g.drawHorizontalLine (static_cast<int> (waveformArea.getCentreY()),
                                          waveformArea.getX(), waveformArea.getRight());
                    const auto style = theme::waveformStyle();
                    if (style == theme::WaveformStyle::Ultraviolet)
                        drawReferenceWaveform (g, waveformArea, clip->waveform, halfHeight);
                    else if (style == theme::WaveformStyle::Spectrum && owner.audioDataProvider)
                    {
                        const auto* audioData = owner.audioDataProvider (clip->filePath);
                        if (audioData != nullptr && ! audioData->waveformColumns.empty())
                            drawSpectrumWaveform (g, waveformArea, *clip,
                                                  audioData->waveformColumns,
                                                  audioData->durationSeconds, halfHeight);
                        else
                            drawWaveformEnvelope (g, waveformArea, clip->waveform,
                                                  base.brighter (0.7f), halfHeight);
                    }
                    else
                        drawWaveformEnvelope (g, waveformArea, clip->waveform,
                                              base.brighter (0.7f), halfHeight);
                    if (clip->sourceOffsetSeconds > 1.0e-6)
                    {
                        g.setColour (theme::accent().withAlpha (0.95f));
                        g.fillRect (juce::Rectangle<float> (
                            r.getX() + 1.0f, r.getY() + 15.0f, 3.0f,
                            std::max (4.0f, r.getHeight() - 15.0f)));
                        if (r.getWidth() >= 96.0f)
                        {
                            g.setColour (juce::Colours::white.withAlpha (0.85f));
                            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                            g.drawText (
                                juce::String ("Source ") + juce::String (clip->sourceOffsetSeconds, 2) + " s",
                                r.toNearestInt().withTrimmedLeft (7).withTrimmedTop (15).withHeight (14),
                                juce::Justification::centredLeft, true);
                        }
                    }

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
                        g.drawText (theme::ui ("Disabled"), r.toNearestInt().reduced (3),
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
                const int rowTop = owner.rowTop (static_cast<int> (i));
                const int rowHeight = owner.rowHeight (static_cast<int> (i));
                juce::Rectangle<float> r (static_cast<float> (p->start * ppb),
                                          static_cast<float> (rowTop + (t.collapsed ? 3 : 4)),
                                          static_cast<float> (p->clip.length() * ppb),
                                          static_cast<float> (rowHeight - (t.collapsed ? 6 : 9)));
                if (! r.toNearestInt().intersects (cb)) continue;

                const bool sel = isClipSelected (t.id, p->id, false);
                g.setColour (sel ? base.brighter (0.25f) : base);
                g.fillRoundedRectangle (r, 4.0f);
                if (t.collapsed)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.3f));
                    const auto name = p->name.empty() ? juce::String ("Clip")
                                                       : juce::String::fromUTF8 (p->name.c_str());
                    g.drawText (name, r.toNearestInt().reduced (5), juce::Justification::centredLeft, true);
                    if (! p->enabled)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.62f));
                        g.fillRoundedRectangle (r, 4.0f);
                    }
                    if (sel)
                    {
                        g.setColour (juce::Colours::white);
                        g.drawRoundedRectangle (r, 4.0f, 1.6f);
                    }
                    continue;
                }

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
                    g.drawText (theme::ui ("Disabled"), r.toNearestInt().reduced (3),
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

        if (owner.automationMode)
        {
            for (size_t i = 0; i < project.tracks.size(); ++i)
            {
                const auto& track = *project.tracks[i];
                if (owner.rowHeight (static_cast<int> (i)) <= 0)
                    continue;
                if (! track.automation.empty())
                {
                    const float top = static_cast<float> (owner.rowTop (static_cast<int> (i)) + 4);
                    const float height = static_cast<float> (std::max (10, owner.rowHeight (static_cast<int> (i)) - 8));
                    g.setColour (theme::panel().withAlpha (0.72f));
                    g.fillRect (0.0f, top, static_cast<float> (getWidth()), height);
                }
                for (const auto& envelope : track.automation)
                {
                    const bool selected = envelope.target == owner.automationTarget;
                    if (automationEditTrackId == track.id
                        && automationEditEnvelope.target == envelope.target)
                        drawAutomationEnvelope (g, static_cast<int> (i), track,
                                               automationEditEnvelope, selected);
                    else
                        drawAutomationEnvelope (g, static_cast<int> (i), track, envelope, selected);
                }
                if (automationEditTrackId == track.id
                    && findEnvelope (track, owner.automationTarget) == nullptr
                    && ! automationEditEnvelope.points.empty())
                    drawAutomationEnvelope (g, static_cast<int> (i), track,
                                           automationEditEnvelope, true);
            }
            if (project.masterTrack != nullptr)
            {
                const int row = static_cast<int> (project.tracks.size());
                const auto& track = *project.masterTrack;
                if (owner.rowHeight (row) > 0 && ! track.automation.empty())
                {
                    const float top = static_cast<float> (owner.rowTop (row) + 4);
                    const float height = static_cast<float> (std::max (10, owner.rowHeight (row) - 8));
                    g.setColour (theme::panel().withAlpha (0.72f));
                    g.fillRect (0.0f, top, static_cast<float> (getWidth()), height);
                }
                if (owner.rowHeight (row) > 0)
                    for (const auto& envelope : track.automation)
                    {
                        const bool selected = envelope.target == owner.automationTarget;
                        if (automationEditTrackId == track.id
                            && automationEditEnvelope.target == envelope.target)
                            drawAutomationEnvelope (g, row, track, automationEditEnvelope, selected);
                        else
                            drawAutomationEnvelope (g, row, track, envelope, selected);
                    }
                if (owner.rowHeight (row) > 0 && automationEditTrackId == track.id
                    && findEnvelope (track, owner.automationTarget) == nullptr
                    && ! automationEditEnvelope.points.empty())
                    drawAutomationEnvelope (g, row, track, automationEditEnvelope, true);
            }

            if (owner.automationTarget.type == roll::AutomationParameterType::PluginParameter)
            {
                const int targetTrackId = owner.automationTargetTrackId >= 0
                    ? owner.automationTargetTrackId : project.selectedTrackId;
                const roll::Track* track = targetTrackId == 0 ? project.masterTrack.get()
                                                               : project.track (targetTrackId);
                int row = targetTrackId == 0 ? static_cast<int> (project.tracks.size()) : -1;
                if (row < 0)
                    for (std::size_t i = 0; i < project.tracks.size(); ++i)
                        if (project.tracks[i]->id == targetTrackId)
                        {
                            row = static_cast<int> (i);
                            break;
                        }

                if (track != nullptr && row >= 0 && owner.rowHeight (row) > 0
                    && automationEditTrackId != targetTrackId
                    && findEnvelope (*track, owner.automationTarget) == nullptr)
                {
                    const auto [minimum, maximum] =
                        roll::automationValueRange (owner.automationTarget.type);
                    const float top = static_cast<float> (owner.rowTop (row) + 7);
                    const float height = static_cast<float> (
                        std::max (10, owner.rowHeight (row) - 14));
                    const double value = std::clamp (owner.automationTarget.currentValue,
                                                     minimum, maximum);
                    const float y = top + height * static_cast<float> (
                        1.0 - (value - minimum) / (maximum - minimum));
                    g.setColour (theme::panel().withAlpha (0.32f));
                    g.fillRect (0.0f, top, static_cast<float> (getWidth()), height);
                    g.setColour (theme::accent().withAlpha (0.7f));
                    g.drawHorizontalLine (juce::roundToInt (y), 0.0f,
                                          static_cast<float> (getWidth()));
                    g.fillEllipse (static_cast<float> (owner.playhead * owner.pixelsPerBeat) - 3.0f,
                                   y - 3.0f, 6.0f, 6.0f);
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
            const float y = static_cast<float> (owner.rowTop (firstTrack));
            const float height = static_cast<float> (owner.rowTop (lastTrack + 1) - owner.rowTop (firstTrack));
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
            if (height >= 30.0f && width >= 120.0f)
            {
                const auto label = juce::Rectangle<int> (static_cast<int> (x) + 6,
                    static_cast<int> (y) + 5, 146, 18);
                g.setColour (theme::panel().withAlpha (0.9f));
                g.fillRoundedRectangle (label.toFloat(), 3.0f);
                g.setColour (theme::text());
                g.setFont (juce::FontOptions (11.5f));
                g.drawText (theme::ui ("Time Selection"),
                            label.reduced (4, 0), juce::Justification::centredLeft, true);
            }
            if (! selecting && height >= 48.0f && width >= 245.0f)
            {
                const auto hint = juce::Rectangle<int> (static_cast<int> (x) + 6,
                    static_cast<int> (y) + 25, 245, 18);
                g.setColour (theme::panel().withAlpha (0.9f));
                g.fillRoundedRectangle (hint.toFloat(), 3.0f);
                g.setColour (theme::text());
                g.setFont (juce::FontOptions (11.5f));
                g.drawText ("Ctrl+D duplicate | Delete remove | Ctrl+J consolidate",
                            hint.reduced (4, 0), juce::Justification::centredLeft, true);
            }
        }

        g.setColour (theme::playhead());
        g.drawVerticalLine (static_cast<int> (owner.playhead * ppb), 0.0f, static_cast<float> (getHeight()));

        drawMovePreview (g, ppb);

        if (dragTrackIndex >= 0)
        {
            const auto colour = dragDropAllowed ? theme::accent() : juce::Colours::red;
            const int y = owner.rowTop (dragTrackIndex);
            const int height = owner.rowHeight (dragTrackIndex);
            g.setColour (colour.withAlpha (0.18f));
            g.fillRect (0, y, getWidth(), height);
            g.setColour (colour);
            g.drawRect (0, y, getWidth(), height, 2);

            if (dragDropAllowed)
            {
                const int markerX = static_cast<int> (dragBeat * ppb);
                g.drawVerticalLine (markerX, static_cast<float> (y), static_cast<float> (y + height));
            }

            const bool masterTarget = dragTrackIndex == static_cast<int> (project.tracks.size());
            const juce::String message = dragDropAllowed
                ? (masterTarget ? "Drop an effect to add it to Master"
                                : dragDropIsAudio ? "Drop audio to add it"
                                : "Drop content to add it")
                : (masterTarget ? "The Master track only accepts effects"
                                : dragDropIsAudio ? "Audio unavailable on this track" : "Plugin unavailable on this track");
            const auto hint = juce::Rectangle<int> (8, y + 6, 220, 22);
            g.setColour (theme::panel().withAlpha (0.95f));
            g.fillRoundedRectangle (hint.toFloat(), 5.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (hint.toFloat(), 5.0f, 1.0f);
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (message, hint.reduced (7, 0), juce::Justification::centredLeft, true);
        }

        if (owner.trackReorderSourceId >= 0 && owner.trackReorderDropIndex >= 0)
        {
            const int y = owner.rowTop (owner.trackReorderDropIndex);
            g.setColour (theme::accent());
            g.fillRect (0, y - 2, getWidth(), 4);
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
        notifyAction (audio ? "Dragging audio into the Arrangement" : "Dragging a plugin into the Arrangement",
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
        const bool midiDrop = description.startsWith ("browser-item:")
            && juce::File (description.substring (13)).hasFileExtension (".mid;.midi");
        dragDropIsAudio = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
        const auto trackId = trackIdAt (trackIndex);
        const auto beat = snapClipBeat (details.localPosition.x / owner.pixelsPerBeat);
        if (audioDrop)
        {
            const juce::File file (description.substring (6));
            notifyAction ("Audio dropped in the Arrangement", icons::Icon::Waveform, false, 650);
            if (owner.canAcceptAudioDrop && owner.canAcceptAudioDrop (file, trackId) && owner.onAudioDropped)
                owner.onAudioDropped (file, trackId, beat);
            return;
        }

        if (midiDrop)
        {
            juce::File file (description.substring (13));
            notifyAction ("MIDI dropped in the Arrangement", icons::Icon::PianoRoll, false, 650);
            if (owner.onMidiDropped)
                owner.onMidiDropped (file, trackId, beat);
            return;
        }

        if (description.startsWith ("plugin:") && owner.canAcceptPluginDrop
            && owner.canAcceptPluginDrop (description.substring (7), trackId) && owner.onPluginDropped)
        {
            notifyAction ("Plugin dropped in the Arrangement", icons::Icon::Mixer, false, 650);
            owner.onPluginDropped (description.substring (7), trackId, beat);
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        const juce::File file (files.size() == 1 ? files[0] : juce::String());
        return files.size() == 1
            && (isAudioFile (file) || file.hasFileExtension (".mid;.midi"));
    }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        updateFileDragTarget (files, x, y);
        const juce::File file (files.size() == 1 ? files[0] : juce::String());
        notifyAction (file.hasFileExtension (".mid;.midi")
            ? "Dragging MIDI into the Arrangement"
            : "Dragging audio into the Arrangement",
            file.hasFileExtension (".mid;.midi") ? icons::Icon::PianoRoll : icons::Icon::Waveform,
            true, 0);
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
            notifyAction ("Drop an audio file", icons::Icon::Waveform, false, 650);
            repaint();
            return;
        }
        const juce::File file (files[0]);
        const int trackId = trackIdAt (trackIndex);
        if (file.hasFileExtension (".mid;.midi"))
        {
            notifyAction ("MIDI dropped in the Arrangement", icons::Icon::PianoRoll, false, 650);
            if (owner.onMidiDropped)
                owner.onMidiDropped (file, trackId, snapClipBeat (static_cast<double> (x) / owner.pixelsPerBeat));
        }
        else if (owner.canAcceptAudioDrop && owner.canAcceptAudioDrop (file, trackId) && owner.onAudioDropped)
        {
            notifyAction ("Audio dropped in the Arrangement", icons::Icon::Waveform, false, 650);
            owner.onAudioDropped (file, trackId, snapClipBeat (static_cast<double> (x) / owner.pixelsPerBeat));
        }
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
        roll::AudioPlacement* frozenClip = nullptr;
    };

    Hit hit (juce::Point<float> p) const
    {
        Hit h;
        h.trackIndex = owner.rowAt (juce::roundToInt (p.y));
        if (h.trackIndex < 0 || h.trackIndex >= static_cast<int> (project.tracks.size())) { h.trackIndex = -1; return h; }
        const auto* track = project.tracks[static_cast<size_t> (h.trackIndex)].get();
        const double beat = p.x / owner.pixelsPerBeat;
        if (track->frozen)
        {
            if (track->frozenAudioClip != nullptr && beat >= track->frozenAudioClip->start
                && beat < track->frozenAudioClip->end())
                h.frozenClip = track->frozenAudioClip.get();
            return h;
        }
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
        mode = Mode::None;
        dragActive = false;
        movePreviewActive = movePreviewIsCopy = false;
        movePreviewAllowed = true;
        movePreviewReplacesSource = false;
        multiDragSources.clear();
        multiMovePreviewItems.clear();
        multiDragAnchorTrackIndex = -1;
        selectedFrozenTrackId = -1;

        const int row = owner.rowAt (juce::roundToInt (e.position.y));
        if (owner.automationMode && e.mods.isLeftButtonDown()
            && row >= 0 && row <= static_cast<int> (project.tracks.size()))
        {
            beginAutomationEdit (e, row);
            return;
        }

        hasRangeSelection = false;

        if (row == static_cast<int> (project.tracks.size()))
        {
            selectedClips.clear();
            selectionAnchor = {};
            project.selectTrack (0);
            notifyAction ("Selected Bus", icons::Icon::Pointer, false, 700);
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
            notifyAction ("Clip Menu", icons::Icon::Pointer, false, 700);
            return;
        }

        if (h.frozenClip != nullptr)
        {
            selectedFrozenTrackId = trackId;
            project.selectClip (trackId, -1);
            if (owner.onSeek)
                owner.onSeek (h.frozenClip->start);
            notifyAction ("Clip selected", icons::Icon::Pointer, false, 650);
            repaint();
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

        if (e.mods.isShiftDown())
        {
            pendingSelection = { trackId,
                h.audioClip != nullptr ? h.audioClip->id : h.clip->id,
                h.audioClip != nullptr };
            selectionDownPosition = e.position;
            mode = Mode::ShiftPending;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
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
                origLen = h.audioClip->lengthBeats;
            }
            else
            {
                curClip = h.clip->id;
                curAudioClipId = -1;
                origStart = h.clip->start;
                origLen = h.clip->clip.length();
            }
            downBeat = e.position.x / owner.pixelsPerBeat;
            captureMultiDragSources (pendingSelection);
            mode = Mode::CtrlPending;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            notifyInteraction (true);
            return;
        }

        const ClipSelection clickedClip { trackId,
            h.audioClip != nullptr ? h.audioClip->id : h.clip->id,
            h.audioClip != nullptr };
        mode = fadeModeAt (h, e.position);
        const bool focusesClipTime = mode == Mode::None && isClipTimeSelectionArea (h, e.position);
        const bool dragExistingSelection = ! focusesClipTime && selectedClips.size() > 1
            && isClipSelected (clickedClip.trackId, clickedClip.clipId, clickedClip.audio);
        if (focusesClipTime)
        {
            selectedClips.clear();
            selectionAnchor = {};
        }
        else if (! dragExistingSelection)
        {
            selectedClips.clear();
            selectionAnchor = clickedClip;
            selectedClips.push_back (selectionAnchor);
        }
        const double rawBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
        const double seekBeat = focusesClipTime && ! e.mods.isAltDown()
            ? owner.snapBeat (rawBeat) : rawBeat;
        if (h.audioClip != nullptr)
        {
            project.selectAudioClip (trackId, h.audioClip->id);
            if (owner.onSeek)
                owner.onSeek (focusesClipTime ? seekBeat : h.audioClip->start);
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
                owner.onSeek (focusesClipTime ? seekBeat : h.clip->start);
            curTrack = trackId; curClip = h.clip->id; curAudioClipId = -1;
            origStart = h.clip->start; origLen = h.clip->clip.length();
        }
        downBeat = e.position.x / owner.pixelsPerBeat;
        downY = e.position.y;
        const float left = static_cast<float> (
            (h.audioClip != nullptr ? h.audioClip->start : h.clip->start) * owner.pixelsPerBeat);
        const float right = static_cast<float> ((h.audioClip != nullptr ? h.audioClip->end() : h.clip->end()) * owner.pixelsPerBeat);
        if (focusesClipTime)
        {
            mode = Mode::ClipTimeSelectPending;
            selectionDownPosition = e.position;
            pendingRangeTrack = h.trackIndex;
        }
        else if (mode == Mode::None)
            mode = e.position.x > right - 7.0f
                ? Mode::Resize
                : e.position.x < left + 7.0f
                    ? Mode::ResizeLeft
                    : Mode::Move;
        if (mode == Mode::Move && dragExistingSelection)
            captureMultiDragSources (clickedClip);
        if (mode == Mode::FadeInCurve || mode == Mode::FadeOutCurve)
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor (mode == Mode::Resize || mode == Mode::ResizeLeft || mode == Mode::FadeIn || mode == Mode::FadeOut
                                ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::DraggingHandCursor);
        notifyInteraction (true);
        repaint();
    }

    void showContextMenu (const Hit& h, int trackId)
    {
        juce::PopupMenu menu, gridMenu;
        const bool isAudio = h.audioClip != nullptr;
        const int clipId = isAudio ? h.audioClip->id : (h.clip != nullptr ? h.clip->id : -1);
        menu.addItem (6, theme::ui ("Select All (Ctrl+A)"));
        menu.addSeparator();

        if (clipId >= 0)
        {
            menu.addItem (1, theme::ui ("Toggle Clip Loop"));
            const auto* selectedTrack = project.track (trackId);
            const auto* audioClip = isAudio && selectedTrack != nullptr
                ? selectedTrack->findAudioClip (clipId) : nullptr;
            const auto* midiClip = ! isAudio && selectedTrack != nullptr
                ? selectedTrack->find (clipId) : nullptr;
            const bool enabled = isAudio ? (audioClip != nullptr && audioClip->enabled)
                                         : (midiClip != nullptr && midiClip->enabled);
            menu.addItem (4, theme::ui (enabled ? "Disable Clip (0)" : "Enable Clip (0)"));
            if (isAudio)
                menu.addItem (5, theme::ui ("Locate in Browser"));
            menu.addItem (2, theme::ui ("Delete Clip"));
            const double clipStart = isAudio ? (audioClip != nullptr ? audioClip->start : 0.0)
                                             : (midiClip != nullptr ? midiClip->start : 0.0);
            const double clipEnd = isAudio ? (audioClip != nullptr ? audioClip->end() : 0.0)
                                           : (midiClip != nullptr ? midiClip->end() : 0.0);
            const bool canSplit = owner.playhead > clipStart + 1.0e-9
                               && owner.playhead < clipEnd - 1.0e-9;
            menu.addItem (3, theme::ui ("Split at Locator (Ctrl+E)"), canSplit);
            menu.addSeparator();
        }

        const auto* contextTrack = project.track (trackId);
        if (contextTrack != nullptr && contextTrack->type != roll::TrackType::Group)
        {
            if (contextTrack->frozen)
            {
                menu.addItem (8, theme::ui ("Unfreeze Track"), contextTrack->freezeReversible);
                menu.addItem (9, theme::ui ("Flatten Track"), contextTrack->freezeReversible);
            }
            else
                menu.addItem (7, theme::ui ("Freeze Track"));
            menu.addSeparator();
        }

        static constexpr double gridValues[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                                2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                                1.0 / 12.0, 4.0 };
        static constexpr const char* gridLabels[] { "1/4", "1/8", "1/16", "1/32", "1/64",
                                                     "1/4T", "1/8T", "1/16T", "1/32T", "1 bar" };
        for (int i = 0; i < 10; ++i)
            gridMenu.addItem (100 + i, theme::ui (gridLabels[i]), true,
                              std::abs (owner.gridBeats() - gridValues[i]) < 1.0e-9);
        gridMenu.addItem (110, theme::ui ("No Grid"), true, owner.gridBeats() <= 0.0);
        menu.addSubMenu (theme::ui ("Editing Grid"), gridMenu);

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
            if (result == 7 && safeThis->owner.onFreezeTrackRequested)
            {
                safeThis->owner.onFreezeTrackRequested (trackId);
                return;
            }
            if (result == 8 && safeThis->owner.onUnfreezeTrackRequested)
            {
                safeThis->owner.onUnfreezeTrackRequested (trackId);
                return;
            }
            if (result == 9 && safeThis->owner.onFlattenTrackRequested)
            {
                safeThis->owner.onFlattenTrackRequested (trackId);
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
        if (mode == Mode::AutomationEdit)
        {
            if (e.getDistanceFromDragStart() >= 2.0f)
            {
                automationWasDragged = true;
                updateAutomationPoint (e);
            }
            return;
        }

        if (mode == Mode::EmptyClickPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;

            mode = Mode::RangeSelect;
            dragActive = true;
            selectedClips.clear();
            selectionAnchor = {};
            hasRangeSelection = true;
            const double startBeat = std::max (0.0, selectionDownPosition.x / owner.pixelsPerBeat);
            const double endBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = e.mods.isAltDown() ? startBeat : owner.snapBeat (startBeat);
            rangeEndBeat = e.mods.isAltDown() ? endBeat : owner.snapBeat (endBeat);
            rangeStartTrack = std::clamp (
                owner.rowAt (juce::roundToInt (selectionDownPosition.y)),
                0, static_cast<int> (project.tracks.size()) - 1);
            rangeEndTrack = std::clamp (owner.rowAt (juce::roundToInt (e.position.y)),
                                        0, static_cast<int> (project.tracks.size()) - 1);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }

        if (mode == Mode::ClipTimeSelectPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;

            mode = Mode::RangeSelect;
            dragActive = true;
            selectedClips.clear();
            selectionAnchor = {};
            hasRangeSelection = true;
            const double rawBeat = std::max (0.0, selectionDownPosition.x / owner.pixelsPerBeat);
            const double endBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat);
            rangeEndBeat = e.mods.isAltDown() ? endBeat : owner.snapBeat (endBeat);
            rangeStartTrack = pendingRangeTrack;
            rangeEndTrack = std::clamp (owner.rowAt (juce::roundToInt (e.position.y)),
                                        0, static_cast<int> (project.tracks.size()) - 1);
            const int trackId = project.tracks[static_cast<std::size_t> (pendingRangeTrack)]->id;
            project.selectClip (trackId, -1);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }

        if (mode == Mode::ShiftPending)
        {
            if (e.position.getDistanceFrom (selectionDownPosition) < 4.0f)
                return;
            mode = Mode::RangeSelect;
            dragActive = true;
            selectedClips.clear();
            selectionAnchor = {};
            hasRangeSelection = true;
            const double startBeat = std::max (0.0, selectionDownPosition.x / owner.pixelsPerBeat);
            const double endBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            rangeStartBeat = e.mods.isAltDown() ? startBeat : owner.snapBeat (startBeat);
            rangeEndBeat = e.mods.isAltDown() ? endBeat : owner.snapBeat (endBeat);
            rangeStartTrack = std::clamp (
                owner.rowAt (juce::roundToInt (selectionDownPosition.y)),
                0, static_cast<int> (project.tracks.size()) - 1);
            rangeEndTrack = std::clamp (
                owner.rowAt (juce::roundToInt (e.position.y)),
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
            movePreviewIsCopy = true;
        }
        if (mode == Mode::None) return;
        dragActive = true;
        if (mode != Mode::RangeSelect)
            project.beginEdit();
        const bool noSnap = e.mods.isAltDown();

        if (mode == Mode::RangeSelect)
        {
            updateRangeSelection (e.position, e.mods);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }

        if (mode == Mode::Move)
        {
            updateMovePreview (e.position, e.mods);
            notifyInteraction (true, 0, true);
            repaint();
            return;
        }
        else if (mode == Mode::Resize)
        {
            const double beat = e.position.x / owner.pixelsPerBeat;
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
        else if (mode == Mode::ResizeLeft)
        {
            const double beat = e.position.x / owner.pixelsPerBeat;
            const double requestedStart = noSnap ? beat : owner.snapBeat (beat);
            if (curAudioClipId >= 0)
            {
                if (auto* track = project.track (curTrack))
                    if (auto* clip = track->findAudioClip (curAudioClipId))
                    {
                        const double minimumLength = owner.gridBeats() > 0.0 ? owner.gridBeats() : 1.0 / 64.0;
                        const double endBeat = clip->end();
                        const double newStart = std::clamp (
                            requestedStart, 0.0, endBeat - minimumLength);
                        const double newLength = endBeat - newStart;
                        project.resizeAudioClipStart (curTrack, curAudioClipId, newStart, newLength);
                    }
            }
            else if (curClip >= 0)
            {
                if (auto* track = project.track (curTrack))
                    if (auto* clip = track->find (curClip))
                    {
                        const double minimumLength = owner.gridBeats() > 0.0 ? owner.gridBeats() : 1.0 / 64.0;
                        const double endBeat = clip->end();
                        const double newStart = std::clamp (
                            requestedStart, 0.0, endBeat - minimumLength);
                        const double newLength = endBeat - newStart;
                        project.resizeClipStart (curTrack, curClip, newStart, newLength);
                    }
            }
        }
        else if (mode == Mode::FadeIn || mode == Mode::FadeOut)
        {
            const double beat = e.position.x / owner.pixelsPerBeat;
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
        dragActive = false;
        if (mode == Mode::AutomationEdit)
        {
            finishAutomationEdit();
            mode = Mode::None;
            repaint();
            return;
        }

        if (mode == Mode::EmptyClickPending)
        {
            const double rawBeat = std::max (0.0, e.position.x / owner.pixelsPerBeat);
            const double beat = e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat);
            if (owner.onSeek)
                owner.onSeek (beat);

            const int bar = static_cast<int> (std::floor (beat / 4.0)) + 1;
            const double beatInBar = std::fmod (beat, 4.0) + 1.0;
            notifyAction ("Cursor: bar " + juce::String (bar) + " | beat "
                              + juce::String (beatInBar, 2),
                          icons::Icon::Pointer, false, 900);
            mode = Mode::None;
            movePreviewActive = false;
            updateCursor (e.position);
            repaint();
            return;
        }

        if (mode == Mode::ClipTimeSelectPending)
        {
            notifyAction ("Playback position set", icons::Icon::Pointer, false, 650);
            mode = Mode::None;
            movePreviewActive = false;
            curAudioClipId = -1;
            updateCursor (e.position);
            repaint();
            return;
        }

        if (mode == Mode::ShiftPending)
        {
            extendClipSelection (pendingSelection);
            notifyAction ("Clip selected", icons::Icon::Pointer, false, 650);
            mode = Mode::None;
            movePreviewActive = false;
            curAudioClipId = -1;
            repaint();
            return;
        }
        if (mode == Mode::CtrlPending)
        {
            toggleClipSelection (pendingSelection);
            notifyAction ("Selection updated", icons::Icon::SelectRegion, false, 650);
            mode = Mode::None;
            movePreviewActive = false;
            multiDragSources.clear();
            multiMovePreviewItems.clear();
            multiDragAnchorTrackIndex = -1;
            curAudioClipId = -1;
            repaint();
            return;
        }

        if ((mode == Mode::Resize || mode == Mode::ResizeLeft)
            && e.getDistanceFromDragStart() >= 4.0f)
        {
            auto* track = project.track (curTrack);
            if (track != nullptr)
            {
                if (curAudioClipId >= 0)
                {
                    auto* clip = track->findAudioClip (curAudioClipId);
                    if (clip != nullptr)
                    {
                        if (mode == Mode::Resize)
                        {
                            for (const auto& adjacent : track->audioClips)
                                if (adjacent->id != curAudioClipId
                                    && std::abs (adjacent->start - clip->end()) < 1.0e-7)
                                {
                                    project.joinAudioClips (curTrack, curAudioClipId, adjacent->id);
                                    break;
                                }
                        }
                        else
                        {
                            for (const auto& adjacent : track->audioClips)
                                if (adjacent->id != curAudioClipId
                                    && std::abs (adjacent->end() - clip->start) < 1.0e-7)
                                {
                                    project.joinAudioClips (curTrack, adjacent->id, curAudioClipId);
                                    break;
                                }
                        }
                    }
                }
                else if (curClip >= 0)
                {
                    auto* clip = track->find (curClip);
                    if (clip != nullptr)
                    {
                        if (mode == Mode::Resize)
                        {
                            for (const auto& adjacent : track->clips)
                                if (adjacent->id != curClip
                                    && std::abs (adjacent->start - clip->end()) < 1.0e-7)
                                {
                                    project.joinMidiClips (curTrack, curClip, adjacent->id);
                                    break;
                                }
                        }
                        else
                        {
                            for (const auto& adjacent : track->clips)
                                if (adjacent->id != curClip
                                    && std::abs (adjacent->end() - clip->start) < 1.0e-7)
                                {
                                    project.joinMidiClips (curTrack, adjacent->id, curClip);
                                    break;
                                }
                        }
                    }
                }
            }
        }

        bool moveFailed = false;
        bool editEndedAfterRollback = false;
        if (mode == Mode::Move && e.getDistanceFromDragStart() >= 4.0f
            && ! project.tracks.empty())
        {
            updateMovePreview (e.position, e.mods);
            if (multiDragSources.size() > 1)
            {
                if (! movePreviewAllowed || multiMovePreviewItems.size() != multiDragSources.size())
                    moveFailed = true;
                else
                {
                    bool unchangedPlacement = true;
                    for (std::size_t i = 0; i < multiDragSources.size() && unchangedPlacement; ++i)
                        unchangedPlacement = multiMovePreviewItems[i].trackIndex == multiDragSources[i].trackIndex
                            && std::abs (multiMovePreviewItems[i].start - multiDragSources[i].start) <= 1.0e-9;
                    if (! unchangedPlacement)
                    {
                        bool changed = false;
                        if (! movePreviewIsCopy)
                        {
                            for (const auto& source : multiDragSources)
                            {
                                const auto* track = project.track (source.selection.trackId);
                                const bool exists = track != nullptr && (source.selection.audio
                                    ? track->findAudioClip (source.selection.clipId) != nullptr
                                    : track->find (source.selection.clipId) != nullptr);
                                if (! exists)
                                    continue;

                                if (source.selection.audio)
                                    project.deleteAudioClip (source.selection.trackId, source.selection.clipId);
                                else
                                    project.deleteClip (source.selection.trackId, source.selection.clipId);
                                changed = true;
                            }
                        }

                        std::vector<ClipSelection> copies;
                        for (std::size_t i = 0; i < multiDragSources.size() && ! moveFailed; ++i)
                        {
                            const auto& source = multiDragSources[i];
                            const auto& preview = multiMovePreviewItems[i];
                            const int targetTrackId = project.tracks[static_cast<std::size_t> (preview.trackIndex)]->id;
                            const int newId = source.selection.audio
                                ? project.copyAudioClipReplacingRange (*source.audioClip, targetTrackId, preview.start)
                                : project.copyClipReplacingRange (*source.midiClip, targetTrackId, preview.start);
                            if (newId < 0)
                            {
                                moveFailed = true;
                                break;
                            }
                            copies.push_back ({ targetTrackId, newId, source.selection.audio });
                            changed = true;
                        }

                        if (moveFailed && changed)
                        {
                            project.endEdit();
                            project.undo();
                            editEndedAfterRollback = true;
                        }
                        else if (! moveFailed)
                        {
                            selectedClips = std::move (copies);
                            selectionAnchor = selectedClips.front();
                            hasRangeSelection = false;
                            setProjectSelection (selectedClips.back());
                            updateRangeFromSelection (true);
                        }
                    }
                }
            }
            else
            {
                const double beat = e.position.x / owner.pixelsPerBeat;
                const double requestedStart = origStart + (beat - downBeat);
                const double start = moveClipBeat (requestedStart, e.mods);
                movePreviewTrackIndex = std::clamp (
                    owner.rowAt (juce::roundToInt (e.position.y)), 0,
                    static_cast<int> (project.tracks.size()) - 1);
                movePreviewStart = start;
                movePreviewIsCopy = movePreviewIsCopy || e.mods.isCtrlDown();
                const auto* targetTrack = project.tracks[static_cast<std::size_t> (movePreviewTrackIndex)].get();
                const auto* sourceTrack = project.track (curTrack);
                movePreviewReplacesSource = movePreviewIsCopy && curTrack == targetTrack->id
                    && start < origStart + origLen - 1.0e-9
                    && start + origLen > origStart + 1.0e-9;
                movePreviewAllowed = targetTrack != nullptr && sourceTrack != nullptr
                    && targetTrack->type == sourceTrack->type;
                const int trackId = targetTrack->id;
                const bool copy = movePreviewIsCopy;
                int newId = -1;
                if (curAudioClipId >= 0)
                    newId = copy ? project.copyAudioClip (curTrack, curAudioClipId, trackId, start)
                                 : project.moveAudioClip (curTrack, curAudioClipId, trackId, start);
                else
                    newId = copy ? project.copyClip (curTrack, curClip, trackId, start)
                                 : project.moveClip (curTrack, curClip, trackId, start);

                if (newId >= 0)
                {
                    curTrack = trackId;
                    if (curAudioClipId >= 0)
                        curAudioClipId = newId;
                    else
                        curClip = newId;
                    selectedClips = { { trackId, newId, curAudioClipId >= 0 } };
                    selectionAnchor = selectedClips.front();
                    hasRangeSelection = false;
                    setProjectSelection (selectionAnchor);
                    movePreviewStart = start;
                    movePreviewIsCopy = copy;
                }
                else
                {
                    moveFailed = true;
                }
            }
        }

        if (mode != Mode::RangeSelect && e.getDistanceFromDragStart() < 4.0f)
            notifyAction ("Clip selected", icons::Icon::Pointer, false, 650);
        else if (moveFailed)
            notifyAction ("Could not drop the clip on this track.",
                          icons::Icon::Pointer, false, 900);
        else
            notifyInteraction (false, 700);
        if (mode != Mode::RangeSelect && ! editEndedAfterRollback)
            project.endEdit();
        mode = Mode::None;
        movePreviewActive = false;
        multiDragSources.clear();
        multiMovePreviewItems.clear();
        multiDragAnchorTrackIndex = -1;
        updateCursor (e.position);
        curAudioClipId = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (owner.automationMode)
            return;

        const auto h = hit (e.position);
        if (h.frozenClip != nullptr)
            return;
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
        if ((! owner.computerMidiKeyboardEnabledProvider
             || ! owner.computerMidiKeyboardEnabledProvider())
            && ! ctrl && ! k.getModifiers().isAltDown() && ! k.getModifiers().isShiftDown()
            && key == 'A')
        {
            owner.toggleAutomationModeFromKey();
            return true;
        }
        if (owner.automationMode
            && (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey)))
        {
            const int trackId = owner.automationTargetTrackId >= 0
                ? owner.automationTargetTrackId : project.selectedTrackId;
            if (trackId >= 0)
                project.removeAutomationEnvelope (trackId, owner.automationTarget);
            return true;
        }

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
        if (ctrl && key == '5')
        {
            owner.setAdaptiveGrid (! owner.adaptiveGridEnabled);
            return true;
        }
        if (ctrl && key == '4')
        {
            owner.setGridBeat (owner.gridBeats() > 0.0 ? 0.0 : owner.lastGridBeat);
            return true;
        }

        const auto modifiers = k.getModifiers();
        if (! ctrl && ! modifiers.isAltDown() && ! modifiers.isShiftDown()
            && key == 'S'
            && (! owner.computerMidiKeyboardEnabledProvider
                || ! owner.computerMidiKeyboardEnabledProvider())
            && t != nullptr)
        {
            project.setTrackSolo (t->id, ! t->solo, false);
            return true;
        }

        if (k.getTextCharacter() == '0' && ! modifiers.isCtrlDown() && ! modifiers.isCommandDown()
            && ! modifiers.isAltDown() && ! modifiers.isShiftDown())
        {
            if (! selectedClips.empty())
            {
                bool hasClip = false;
                bool shouldEnable = false;
                for (const auto& selected : selectedClips)
                {
                    const auto* track = project.track (selected.trackId);
                    if (track == nullptr)
                        continue;
                    if (selected.audio)
                    {
                        const auto* clip = track->findAudioClip (selected.clipId);
                        if (clip == nullptr)
                            continue;
                        hasClip = true;
                        shouldEnable |= ! clip->enabled;
                    }
                    else
                    {
                        const auto* clip = track->find (selected.clipId);
                        if (clip == nullptr)
                            continue;
                        hasClip = true;
                        shouldEnable |= ! clip->enabled;
                    }
                }

                if (hasClip)
                {
                    project.beginEdit();
                    for (const auto& selected : selectedClips)
                        project.setClipEnabled (selected.trackId, selected.clipId,
                                                selected.audio, shouldEnable);
                    project.endEdit();
                    return true;
                }
            }

            if (project.selectedTrackIds.size() > 1)
            {
                std::vector<roll::Track*> selectedTracks;
                for (const int trackId : project.selectedTrackIds)
                    if (auto* selectedTrack = project.track (trackId))
                        selectedTracks.push_back (selectedTrack);
                if (! selectedTracks.empty())
                {
                    const bool shouldMute = std::any_of (selectedTracks.begin(), selectedTracks.end(),
                        [] (const auto* selectedTrack) { return ! selectedTrack->mute; });
                    project.beginEdit();
                    for (auto* selectedTrack : selectedTracks)
                        selectedTrack->mute = shouldMute;
                    project.notifyMixer();
                    project.endEdit();
                    return true;
                }
            }

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
            if (t != nullptr)
            {
                project.beginEdit();
                t->mute = ! t->mute;
                project.notifyMixer();
                project.endEdit();
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
            if (project.selectedTrackIds.size() > 1)
            {
                auto tracksToDelete = project.selectedTrackIds;
                std::stable_sort (tracksToDelete.begin(), tracksToDelete.end(),
                    [this] (int a, int b) { return project.trackIndex (a) > project.trackIndex (b); });
                for (const int trackId : tracksToDelete)
                    if (project.tracks.size() > 1 && project.track (trackId) != nullptr)
                        project.removeTrack (trackId);

                if (std::any_of (tracksToDelete.begin(), tracksToDelete.end(),
                                 [this] (int trackId) { return project.track (trackId) != nullptr; }))
                    notifyAction ("The project must contain at least one track.", icons::Icon::Pointer, false, 1200);
                return true;
            }
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

        if (ctrl && k.getModifiers().isShiftDown() && key == 'K')
        {
            if (! hasRangeSelection)
                return false;
            const double start = std::min (rangeStartBeat, rangeEndBeat);
            const double end = std::max (rangeStartBeat, rangeEndBeat);
            const int first = std::min (rangeStartTrack, rangeEndTrack);
            const int last = std::max (rangeStartTrack, rangeEndTrack);
            project.beginEdit();
            bool changed = false;
            for (int row = first; row <= last && row < static_cast<int> (project.tracks.size()); ++row)
            {
                const auto* track = project.tracks[static_cast<std::size_t> (row)].get();
                if (project.cropRange (track->id, start, end))
                    changed = true;
            }
            project.endEdit();
            if (changed)
            {
                notifyAction ("Selection cut", icons::Icon::Cut, false, 700);
                return true;
            }
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
    static const roll::AutomationEnvelope* findEnvelope (
        const roll::Track& track, const roll::AutomationTarget& target)
    {
        const auto found = std::find_if (track.automation.begin(), track.automation.end(),
            [&target] (const auto& envelope) { return envelope.target == target; });
        return found == track.automation.end() ? nullptr : &*found;
    }

    void drawAutomationEnvelope (juce::Graphics& g, int row, const roll::Track& track,
                                 const roll::AutomationEnvelope& envelope, bool selected)
    {
        if (envelope.points.empty())
            return;

        const auto [minimum, maximum] = roll::automationValueRange (envelope.target.type);
        if (maximum <= minimum)
            return;
        const float top = static_cast<float> (owner.rowTop (row) + 7);
        const float height = static_cast<float> (std::max (10, owner.rowHeight (row) - 14));
        const auto valueY = [=] (double value)
        {
            const double proportion = (value - minimum) / (maximum - minimum);
            return top + height * static_cast<float> (1.0 - std::clamp (proportion, 0.0, 1.0));
        };

        juce::Path path;
        const auto& points = envelope.points;
        if (points.size() == 1)
        {
            const float y = valueY (points.front().value);
            path.startNewSubPath (0.0f, y);
            path.lineTo (static_cast<float> (getWidth()), y);
        }
        else
        {
            path.startNewSubPath (static_cast<float> (points.front().beat * owner.pixelsPerBeat),
                                  valueY (points.front().value));
        }
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const float x = static_cast<float> (points[i].beat * owner.pixelsPerBeat);
            if (envelope.target.type == roll::AutomationParameterType::Mute)
                path.lineTo (x, valueY (points[i - 1].value));
            path.lineTo (x, valueY (points[i].value));
        }

        g.setColour (selected ? theme::accent() : colourFor (track).brighter (0.65f).withAlpha (0.58f));
        g.strokePath (path, juce::PathStrokeType (selected ? 2.8f : 1.5f));
        g.setColour (selected ? juce::Colours::white : theme::text().withAlpha (0.72f));
        for (const auto& point : points)
        {
            const float x = static_cast<float> (point.beat * owner.pixelsPerBeat);
            const float y = valueY (point.value);
            const float size = selected ? 8.0f : 5.0f;
            g.fillEllipse (x - size * 0.5f, y - size * 0.5f, size, size);
        }
        juce::String label;
        switch (envelope.target.type)
        {
            case roll::AutomationParameterType::Volume: label = "Volume"; break;
            case roll::AutomationParameterType::Pan: label = "Pan"; break;
            case roll::AutomationParameterType::Mute: label = "Mute"; break;
            case roll::AutomationParameterType::PluginParameter:
                label = juce::String::fromUTF8 (envelope.target.parameterName.c_str());
                break;
            case roll::AutomationParameterType::AudioClipGain: label = "Clip Gain"; break;
            case roll::AutomationParameterType::AudioClipPitch: label = "Clip Pitch"; break;
            case roll::AutomationParameterType::AudioClipFadeIn: label = "Fade In"; break;
            case roll::AutomationParameterType::AudioClipFadeOut: label = "Fade Out"; break;
        }
        if (label.isNotEmpty())
        {
            const float x = static_cast<float> (points.front().beat * owner.pixelsPerBeat) + 7.0f;
            const float y = valueY (points.front().value) - 16.0f;
            const auto labelBounds = juce::Rectangle<float> (x, y, 108.0f, 14.0f);
            g.setColour (theme::panel().withAlpha (0.9f));
            g.fillRoundedRectangle (labelBounds, 3.0f);
            g.setColour (selected ? theme::accent() : theme::text().withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (label, labelBounds.reduced (3.0f, 0.0f),
                        juce::Justification::centredLeft, true);
        }

        if (mode == Mode::AutomationEdit && automationEditTrackId == track.id
            && automationEditEnvelope.target == envelope.target
            && automationPointIndex >= 0
            && automationPointIndex < static_cast<int> (points.size())
            && automationPointValueText.isNotEmpty())
        {
            const auto& point = points[static_cast<std::size_t> (automationPointIndex)];
            const float width = std::clamp (
                static_cast<float> (automationPointValueText.length()) * 6.5f + 12.0f,
                58.0f, 150.0f);
            const float valueTooltipHeight = 18.0f;
            const float x = std::clamp (
                static_cast<float> (point.beat * owner.pixelsPerBeat) + 9.0f,
                2.0f, std::max (2.0f, static_cast<float> (getWidth()) - width - 2.0f));
            const float y = std::clamp (
                valueY (point.value) - valueTooltipHeight - 5.0f,
                static_cast<float> (owner.rowTop (row) + 2),
                static_cast<float> (owner.rowTop (row) + owner.rowHeight (row))
                    - valueTooltipHeight - 2.0f);
            const auto valueBounds = juce::Rectangle<float> (
                x, y, width, valueTooltipHeight);
            g.setColour (theme::panel().withAlpha (0.96f));
            g.fillRoundedRectangle (valueBounds, 4.0f);
            g.setColour (theme::accent());
            g.drawRoundedRectangle (valueBounds, 4.0f, 1.0f);
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (automationPointValueText, valueBounds.reduced (5.0f, 0.0f),
                        juce::Justification::centred, true);
        }
    }

    double automationBeatAt (const juce::MouseEvent& e) const
    {
        const double rawBeat = std::max (0.0, static_cast<double> (e.position.x) / owner.pixelsPerBeat);
        return e.mods.isAltDown() || owner.gridBeats() <= 0.0 ? rawBeat : owner.snapBeat (rawBeat);
    }

    double automationValueAtY (int row, float y) const
    {
        const auto [minimum, maximum] = roll::automationValueRange (owner.automationTarget.type);
        const float top = static_cast<float> (owner.rowTop (row) + 7);
        const float height = static_cast<float> (std::max (10, owner.rowHeight (row) - 14));
        const double proportion = std::clamp (1.0 - (static_cast<double> (y) - top) / height, 0.0, 1.0);
        double value = minimum + proportion * (maximum - minimum);
        if (owner.automationTarget.type == roll::AutomationParameterType::Mute)
            value = value >= 0.5 ? 1.0 : 0.0;
        return value;
    }

    void beginAutomationEdit (const juce::MouseEvent& e, int row)
    {
        automationEditRow = row;
        automationEditTrackId = row == static_cast<int> (project.tracks.size())
            ? 0 : project.tracks[static_cast<std::size_t> (row)]->id;
        auto* track = automationEditTrackId == 0 ? project.masterTrack.get()
                                                  : project.track (automationEditTrackId);
        if (track == nullptr)
        {
            automationEditTrackId = -1;
            return;
        }

        if (owner.automationTarget.type == roll::AutomationParameterType::PluginParameter)
        {
            const auto choices = owner.automationParametersProvider
                ? owner.automationParametersProvider (automationEditTrackId)
                : std::vector<AutomationParameterChoice>();
            const auto match = std::find_if (choices.begin(), choices.end(),
                [this] (const auto& choice) { return choice.target == owner.automationTarget; });
            if (match == choices.end())
            {
                automationEditTrackId = -1;
                notifyAction ("Parameter unavailable on this track",
                              icons::Icon::Automation, false, 1000);
                return;
            }
            owner.automationTarget.currentValue = match->target.currentValue;
        }

        project.selectTrack (automationEditTrackId);
        owner.automationTargetTrackId = automationEditTrackId;
        if (const auto* existing = findEnvelope (*track, owner.automationTarget))
            automationEditEnvelope = *existing;
        else
        {
            automationEditEnvelope = {};
            automationEditEnvelope.target = owner.automationTarget;
            if (owner.automationTarget.type == roll::AutomationParameterType::Volume)
                automationEditEnvelope.target.currentValue = track->volume;
            else if (owner.automationTarget.type == roll::AutomationParameterType::Pan)
                automationEditEnvelope.target.currentValue = track->pan;
            else if (owner.automationTarget.type == roll::AutomationParameterType::Mute)
                automationEditEnvelope.target.currentValue = track->mute ? 1.0 : 0.0;
        }

        const double beat = automationBeatAt (e);
        const float x = static_cast<float> (beat * owner.pixelsPerBeat);
        const int rowTop = owner.rowTop (row);
        const float rowValueY = static_cast<float> (e.position.y);
        automationPointIndex = -1;
        double nearestDistance = 9.0;
        for (std::size_t i = 0; i < automationEditEnvelope.points.size(); ++i)
        {
            const auto& point = automationEditEnvelope.points[i];
            const auto [minimum, maximum] = roll::automationValueRange (owner.automationTarget.type);
            const float pointY = static_cast<float> (rowTop + 7)
                + static_cast<float> (std::max (10, owner.rowHeight (row) - 14))
                    * static_cast<float> (1.0 - std::clamp ((point.value - minimum) / (maximum - minimum), 0.0, 1.0));
            const double distance = std::hypot (static_cast<double> (x - point.beat * owner.pixelsPerBeat),
                                                static_cast<double> (rowValueY - pointY));
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                automationPointIndex = static_cast<int> (i);
            }
        }

        automationPointWasExisting = automationPointIndex >= 0;
        automationDoubleClick = e.getNumberOfClicks() > 1;
        automationWasDragged = false;
        if (! automationPointWasExisting)
        {
            automationEditEnvelope.points.push_back ({ beat, automationValueAtY (row, e.position.y) });
            normalizeAutomationPoints (static_cast<int> (automationEditEnvelope.points.size()) - 1);
        }
        updateAutomationPointValueText();
        mode = Mode::AutomationEdit;
        repaint();
    }

    void normalizeAutomationPoints (int pointIndex)
    {
        auto& points = automationEditEnvelope.points;
        if (pointIndex < 0 || pointIndex >= static_cast<int> (points.size()))
            return;
        const auto movedPoint = points[static_cast<std::size_t> (pointIndex)];
        for (int i = static_cast<int> (points.size()) - 1; i >= 0; --i)
            if (i != pointIndex
                && std::abs (points[static_cast<std::size_t> (i)].beat - movedPoint.beat) < 1.0e-9)
                points.erase (points.begin() + i);
        std::sort (points.begin(), points.end(),
            [] (const auto& a, const auto& b) { return a.beat < b.beat; });
        const auto found = std::find_if (points.begin(), points.end(),
            [&movedPoint] (const auto& point)
            {
                return point.beat == movedPoint.beat && point.value == movedPoint.value;
            });
        automationPointIndex = found == points.end() ? -1 : static_cast<int> (found - points.begin());
    }

    void updateAutomationPoint (const juce::MouseEvent& e)
    {
        if (automationPointIndex < 0
            || automationPointIndex >= static_cast<int> (automationEditEnvelope.points.size()))
            return;
        auto& point = automationEditEnvelope.points[static_cast<std::size_t> (automationPointIndex)];
        point.beat = automationBeatAt (e);
        point.value = automationValueAtY (automationEditRow, e.position.y);
        normalizeAutomationPoints (automationPointIndex);
        updateAutomationPointValueText();
        repaint();
    }

    void updateAutomationPointValueText()
    {
        automationPointValueText.clear();
        if (automationPointIndex < 0
            || automationPointIndex >= static_cast<int> (automationEditEnvelope.points.size()))
            return;

        const auto value = automationEditEnvelope.points[static_cast<std::size_t> (
            automationPointIndex)].value;
        switch (automationEditEnvelope.target.type)
        {
            case roll::AutomationParameterType::Volume:
                automationPointValueText = juce::String (
                    roll::gainToDecibels (static_cast<float> (value), -100.0f), 1) + " dB";
                break;
            case roll::AutomationParameterType::Pan:
                automationPointValueText = value < -0.01
                    ? juce::String ("L ") + juce::String (-value * 100.0, 0) + "%"
                    : value > 0.01
                        ? juce::String ("R ") + juce::String (value * 100.0, 0) + "%"
                        : "Center";
                break;
            case roll::AutomationParameterType::Mute:
                automationPointValueText = value >= 0.5 ? "On" : "Off";
                break;
            case roll::AutomationParameterType::PluginParameter:
                if (owner.automationParameterValueTextProvider)
                    automationPointValueText = owner.automationParameterValueTextProvider (
                        automationEditTrackId, automationEditEnvelope.target, value);
                if (automationPointValueText.isEmpty())
                    automationPointValueText = juce::String (value * 100.0, 1) + "%";
                break;
            case roll::AutomationParameterType::AudioClipGain:
                automationPointValueText = juce::String (value, 1) + " dB";
                break;
            case roll::AutomationParameterType::AudioClipPitch:
                automationPointValueText = juce::String (value, 1) + " st";
                break;
            case roll::AutomationParameterType::AudioClipFadeIn:
            case roll::AutomationParameterType::AudioClipFadeOut:
                automationPointValueText = juce::String (value, 2) + " beats";
                break;
        }
    }

    void finishAutomationEdit()
    {
        if (automationEditTrackId < 0)
            return;
        if (automationPointWasExisting && ! automationWasDragged && ! automationDoubleClick)
        {
            if (automationPointIndex >= 0
                && automationPointIndex < static_cast<int> (automationEditEnvelope.points.size()))
                automationEditEnvelope.points.erase (
                    automationEditEnvelope.points.begin() + automationPointIndex);
        }

        if (automationEditEnvelope.points.empty())
            project.removeAutomationEnvelope (automationEditTrackId, owner.automationTarget);
        else if (! project.setAutomationEnvelope (automationEditTrackId, automationEditEnvelope))
            notifyAction ("Could not save automation.", icons::Icon::Automation, false, 1000);
        automationEditTrackId = -1;
        automationPointIndex = -1;
        automationPointValueText.clear();
    }

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
            case Mode::Move:          message = active ? (dragging ? "Dragging clip" : "Moving clip")
                                                       : (movePreviewIsCopy ? "Clip copied" : "Clip moved");
                                          icon = icons::Icon::Move; break;
            case Mode::Resize:        message = active ? (dragging ? "Resizing clip" : "Dragging clip edge") : "Clip resized"; icon = icons::Icon::Resize; break;
            case Mode::FadeIn:        message = active ? (dragging ? "Adjusting fade-in" : "Holding fade-in") : "Fade-in adjusted"; icon = icons::Icon::FadeIn; break;
            case Mode::FadeOut:       message = active ? (dragging ? "Adjusting fade-out" : "Holding fade-out") : "Fade-out adjusted"; icon = icons::Icon::FadeOut; break;
            case Mode::FadeInCurve:   message = active ? (dragging ? "Adjusting fade curve" : "Dragging fade curve") : "Fade curve adjusted"; icon = icons::Icon::FadeIn; break;
            case Mode::FadeOutCurve:  message = active ? (dragging ? "Adjusting fade curve" : "Dragging fade curve") : "Fade curve adjusted"; icon = icons::Icon::FadeOut; break;
            case Mode::AutomationEdit: message = active ? "Editing automation" : "Automation edited"; icon = icons::Icon::Automation; break;
            case Mode::RangeSelect:   message = active ? "Selecting region" : "Region selected"; icon = icons::Icon::SelectRegion; break;
            case Mode::EmptyClickPending: message = active ? "Positioning cursor" : "Cursor positioned"; break;
            case Mode::ClipTimeSelectPending: message = "Focusing clip region"; icon = icons::Icon::SelectRegion; break;
            case Mode::ShiftPending:  message = active ? "Dragging clip selection" : "Clip selected"; break;
            case Mode::CtrlPending:   message = active ? "Dragging clip selection" : "Selection updated"; break;
            case Mode::None:          return;
        }
        notifyAction (message, icon, active, holdMilliseconds);
    }

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

    void captureMultiDragSources (const ClipSelection& anchor)
    {
        multiDragSources.clear();
        multiMovePreviewItems.clear();
        if (selectedClips.size() <= 1 || ! isClipSelected (anchor.trackId, anchor.clipId, anchor.audio))
            return;

        multiDragAnchorTrackIndex = project.trackIndex (anchor.trackId);
        for (const auto& selected : selectedClips)
        {
            MultiDragSource source;
            source.selection = selected;
            source.trackIndex = project.trackIndex (selected.trackId);
            double end = 0.0;
            if (source.trackIndex < 0 || ! getClipBounds (selected, source.start, end))
            {
                multiDragSources.clear();
                multiDragAnchorTrackIndex = -1;
                return;
            }
            source.length = end - source.start;

            const auto* track = project.track (selected.trackId);
            if (selected.audio)
            {
                const auto* clip = track != nullptr ? track->findAudioClip (selected.clipId) : nullptr;
                if (clip == nullptr)
                {
                    multiDragSources.clear();
                    multiDragAnchorTrackIndex = -1;
                    return;
                }
                source.audioClip = std::make_unique<roll::AudioPlacement> (*clip);
            }
            else
            {
                const auto* clip = track != nullptr ? track->find (selected.clipId) : nullptr;
                if (clip == nullptr)
                {
                    multiDragSources.clear();
                    multiDragAnchorTrackIndex = -1;
                    return;
                }
                source.midiClip = std::make_unique<roll::Placement> (*clip);
            }
            multiDragSources.push_back (std::move (source));
        }

        if (multiDragSources.size() != selectedClips.size())
        {
            multiDragSources.clear();
            multiDragAnchorTrackIndex = -1;
        }
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
        {
            selectedClips.push_back (selected);
            selectionAnchor = selected;
        }
        else
        {
            selectedClips.erase (it);
            if (selectionAnchor.trackId == selected.trackId
                && selectionAnchor.clipId == selected.clipId
                && selectionAnchor.audio == selected.audio)
                selectionAnchor = selectedClips.empty() ? ClipSelection {}
                                                        : selectedClips.back();
        }

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
            if (track.frozen)
                continue;
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
                if (track.frozen)
                    continue;
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
                if (track->frozen)
                    continue;
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

        bool succeeded = splitBoundary (end, &selectedTrackIds);
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
                    if (track->frozen)
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

        if (succeeded)
        {
            const double destinationEnd = end + offset;
            succeeded = splitBoundary (destinationEnd, &selectedTrackIds);
            if (succeeded)
            {
                for (const int trackId : selectedTrackIds)
                {
                    const auto* track = project.track (trackId);
                    if (track == nullptr)
                        continue;

                    std::vector<int> midiIds, audioIds;
                    for (const auto& clip : track->clips)
                        if (clip->start >= end - 1.0e-9
                            && clip->end() <= destinationEnd + 1.0e-9)
                            midiIds.push_back (clip->id);
                    for (const auto& clip : track->audioClips)
                        if (clip->start >= end - 1.0e-9
                            && clip->end() <= destinationEnd + 1.0e-9)
                            audioIds.push_back (clip->id);

                    for (const int id : midiIds)
                        project.deleteClip (trackId, id);
                    for (const int id : audioIds)
                        project.deleteAudioClip (trackId, id);
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
            notifyAction ("Could not duplicate the selection.", icons::Icon::Pointer, false, 900);
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
                if (track.frozen)
                    continue;
                if (track.type == roll::TrackType::Audio)
                {
                    for (const auto& clip : track.audioClips)
                        if (clip->start < end - 1.0e-9 && clip->end() > start + 1.0e-9)
                            sources.push_back ({ track.id, clip->id, true });
                }
                else
                {
                    for (const auto& clip : track.clips)
                        if (clip->start < end - 1.0e-9 && clip->end() >= start + 1.0e-9)
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
            if (track == nullptr || track->frozen)
                continue;
            const int trackOffset = project.trackIndex (source.trackId) - baseTrackRow;
            if (source.audio)
            {
                if (const auto* clip = track->findAudioClip (source.clipId))
                {
                    roll::AudioPlacement snapshot (*clip);
                    const double clippedStart = std::max (snapshot.start, start);
                    const double clippedEnd = std::min (snapshot.end(), end);
                    const double overlap = clippedEnd - clippedStart;
                    if (overlap <= 1.0e-9)
                        continue;
                    const double leftBeats = clippedStart - snapshot.start;
                    snapshot.start = 0.0;
                    snapshot.lengthBeats = overlap;
                    snapshot.durationSeconds = overlap * 60.0 / std::max (snapshot.sourceBpm, 20.0);
                    snapshot.sourceOffsetSeconds += leftBeats * 60.0 / std::max (snapshot.sourceBpm, 20.0);
                    snapshot.fadeInBeats = std::min (snapshot.fadeInBeats, overlap);
                    snapshot.fadeOutBeats = std::min (snapshot.fadeOutBeats, overlap);
                    clipboard.push_back ({ trackOffset, clippedStart - start, true, nullptr,
                                           std::make_unique<roll::AudioPlacement> (std::move (snapshot)) });
                }
            }
            else if (const auto* clip = track->find (source.clipId))
            {
                roll::Placement snapshot (*clip);
                const double clippedStart = std::max (snapshot.start, start);
                const double clippedEnd = std::min (snapshot.end(), end);
                const double overlap = clippedEnd - clippedStart;
                if (overlap <= 1.0e-9)
                    continue;
                std::vector<roll::Note> clippedNotes;
                for (const auto& note : snapshot.clip.notes())
                {
                    const double absoluteStart = snapshot.start + note.startBeats;
                    const double absoluteEnd = absoluteStart + note.lengthBeats;
                    if (absoluteEnd <= clippedStart + 1.0e-9 || absoluteStart >= clippedEnd - 1.0e-9)
                        continue;
                    roll::Note clipped = note;
                    const double relativeStart = std::max (absoluteStart, clippedStart) - clippedStart;
                    const double relativeEnd = std::min (absoluteEnd, clippedEnd) - clippedStart;
                    clipped.startBeats = relativeStart;
                    clipped.lengthBeats = std::max (1.0e-9, relativeEnd - relativeStart);
                    clippedNotes.push_back (clipped);
                }
                snapshot.start = 0.0;
                snapshot.clip.setLength (overlap);
                snapshot.clip.setNotes (std::move (clippedNotes));
                clipboard.push_back ({ trackOffset, clippedStart - start, false,
                                       std::make_unique<roll::Placement> (std::move (snapshot)), nullptr });
            }
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
                notifyAction ("No destination track is available for all clips.", icons::Icon::Pointer, false, 900);
                return true;
            }
            const auto& targetTrack = *project.tracks[static_cast<std::size_t> (targetRow)];
            if (targetTrack.frozen
                || (item.audio && targetTrack.type != roll::TrackType::Audio)
                || (! item.audio && targetTrack.type != roll::TrackType::Midi))
            {
                notifyAction ("The track type does not match the copied clip.", icons::Icon::Pointer, false, 900);
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
            notifyAction ("Could not paste the clips here.", icons::Icon::Pointer, false, 900);
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
            if (track.frozen)
                continue;
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
        project.beginEdit();
        bool changed = false;
        for (int row = first; row <= last && row < static_cast<int> (project.tracks.size()); ++row)
        {
            const auto* track = project.tracks[static_cast<std::size_t> (row)].get();
            if (project.deleteRange (track->id, start, end))
                changed = true;
        }
        project.endEdit();
        if (changed)
            notifyAction ("Track removed", icons::Icon::Eraser, false, 700);
        repaint();
    }

    int trackIndexAt (int y) const
    {
        return owner.rowAt (y);
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
        const bool midiDrop = description.startsWith ("browser-item:")
            && juce::File (description.substring (13)).hasFileExtension (".mid;.midi");
        dragTrackIndex = target;
        dragBeat = snapClipBeat (details.localPosition.x / owner.pixelsPerBeat);
        dragDropAllowed = false;
        const int trackId = trackIdAt (target);
        if (dragDropIsAudio)
            dragDropAllowed = owner.canAcceptAudioDrop
                           && owner.canAcceptAudioDrop (juce::File (description.substring (6)), trackId);
        else if (midiDrop)
        {
            const auto* track = project.track (trackId);
            dragDropAllowed = track != nullptr
                && (track->type == roll::TrackType::Midi || trackId == 0);
        }
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
        const juce::File file (files.size() == 1 ? files[0] : juce::String());
        dragDropIsAudio = isAudioFile (file);
        dragTrackIndex = trackIndexAt (y);
        dragBeat = snapClipBeat (static_cast<double> (x) / owner.pixelsPerBeat);
        dragDropAllowed = false;
        if (files.size() == 1 && dragDropIsAudio && owner.canAcceptAudioDrop)
        {
            const int trackId = trackIdAt (dragTrackIndex);
            dragDropAllowed = owner.canAcceptAudioDrop (file, trackId);
        }
        else if (files.size() == 1 && file.hasFileExtension (".mid;.midi"))
            dragDropAllowed = true;
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
        if (h.frozenClip != nullptr)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }
        if (fadeModeAt (h, position) != Mode::None)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
        if (isClipTimeSelectionArea (h, position))
        {
            setMouseCursor (juce::MouseCursor::IBeamCursor);
            return;
        }
        if (h.clip == nullptr && h.audioClip == nullptr)
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        const float right = static_cast<float> ((h.audioClip != nullptr ? h.audioClip->end() : h.clip->end()) * owner.pixelsPerBeat);
        const float left = static_cast<float> (
            (h.audioClip != nullptr ? h.audioClip->start : h.clip->start) * owner.pixelsPerBeat);
        setMouseCursor (position.x > right - 7.0f || position.x < left + 7.0f
                            ? juce::MouseCursor::LeftRightResizeCursor
                            : juce::MouseCursor::DraggingHandCursor);
    }

    Mode fadeModeAt (const Hit& h, juce::Point<float> position) const
    {
        if (h.audioClip == nullptr)
            return Mode::None;

        if (owner.rowHeight (h.trackIndex) <= collapsedTrackHeight)
            return Mode::None;

        const float rowTop = static_cast<float> (owner.rowTop (h.trackIndex) + 22);
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
        const float fadeTop = static_cast<float> (owner.rowTop (h.trackIndex) + 22);
        const float fadeBottom = static_cast<float> (owner.rowTop (h.trackIndex) + 70);
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

    bool isClipTimeSelectionArea (const Hit& h, juce::Point<float> position) const
    {
        if ((h.clip == nullptr && h.audioClip == nullptr) || h.trackIndex < 0)
            return false;

        const int rowHeight = owner.rowHeight (h.trackIndex);
        if (rowHeight <= collapsedTrackHeight)
            return false;

        const float clipTop = static_cast<float> (owner.rowTop (h.trackIndex) + 4);
        const float clipBottom = static_cast<float> (owner.rowTop (h.trackIndex) + rowHeight - 5);
        const float right = static_cast<float> (
            (h.audioClip != nullptr ? h.audioClip->end() : h.clip->end()) * owner.pixelsPerBeat);
        constexpr float titleHeight = 14.0f;
        return position.y >= clipTop + titleHeight && position.y < clipBottom
            && position.x <= right - 7.0f;
    }

    void drawMovePreview (juce::Graphics& g, double ppb)
    {
        if (! movePreviewActive || mode != Mode::Move || movePreviewTrackIndex < 0
            || movePreviewTrackIndex >= static_cast<int> (project.tracks.size()))
            return;

        if (movePreviewIsCopy && multiDragSources.size() > 1)
        {
            for (const auto& item : multiMovePreviewItems)
            {
                const int rowTop = owner.rowTop (item.trackIndex);
                const int rowHeight = owner.rowHeight (item.trackIndex);
                if (rowHeight <= 0 || item.length <= 0.0)
                    continue;

                const juce::Rectangle<float> preview (
                    static_cast<float> (item.start * ppb), static_cast<float> (rowTop + 3),
                    static_cast<float> (item.length * ppb),
                    static_cast<float> (std::max (1, rowHeight - 6)));
                const auto colour = movePreviewAllowed ? theme::accent() : juce::Colours::red;
                g.setColour (colour.withAlpha (0.32f));
                g.fillRoundedRectangle (preview, 4.0f);
                g.setColour (colour);
                g.drawRoundedRectangle (preview, 4.0f, 2.0f);
                if (preview.getWidth() >= 42.0f)
                {
                    g.setColour (juce::Colours::white);
                    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                    g.drawText (movePreviewAllowed
                                    ? (movePreviewIsCopy ? (item.replacesSource ? "REPLACE" : "COPY")
                                                         : "MOVE")
                                    : "INCOMPATIBLE TYPE",
                                preview.toNearestInt().reduced (5, 0),
                                juce::Justification::centredLeft, true);
                }
            }
            return;
        }

        const auto* track = project.track (curTrack);
        double length = origLen;
        if (track != nullptr)
        {
            if (curAudioClipId >= 0)
            {
                if (const auto* clip = track->findAudioClip (curAudioClipId))
                    length = clip->lengthBeats;
            }
            else if (const auto* clip = track->find (curClip))
            {
                length = clip->clip.length();
            }
        }

        const int rowTop = owner.rowTop (movePreviewTrackIndex);
        const int rowHeight = owner.rowHeight (movePreviewTrackIndex);
        if (rowHeight <= 0 || length <= 0.0)
            return;

        const juce::Rectangle<float> preview (
            static_cast<float> (movePreviewStart * ppb),
            static_cast<float> (rowTop + 3),
            static_cast<float> (length * ppb),
            static_cast<float> (std::max (1, rowHeight - 6)));
        const auto colour = movePreviewAllowed ? theme::accent() : juce::Colours::red;
        g.setColour (colour.withAlpha (0.32f));
        g.fillRoundedRectangle (preview, 4.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (preview, 4.0f, 2.0f);
        if (preview.getWidth() >= 42.0f)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (movePreviewAllowed
                            ? (movePreviewReplacesSource ? "REPLACE" : movePreviewIsCopy ? "COPY" : "MOVE")
                            : "INCOMPATIBLE TYPE",
                        preview.toNearestInt().reduced (5, 0),
                        juce::Justification::centredLeft, true);
        }
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
            message = "Clip: " + juce::String (clipLength / 4.0, 1)
                    + " bars; loop: "
                    + juce::String ((project.loopEnd - project.loopStart) / 4.0, 1);
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
        else if (movePreviewActive)
        {
            clipStart = movePreviewStart;
            message = juce::String (movePreviewReplacesSource ? "Replacing"
                                    : movePreviewIsCopy ? "Copying" : "Moving")
                    + ": bar "
                    + juce::String (static_cast<int> (std::floor (clipStart / 4.0)) + 1);
        }
        else
            message = "Moving: bar " + juce::String (static_cast<int> (std::floor (clipStart / 4.0)) + 1);
        const int targetRow = movePreviewActive ? movePreviewTrackIndex : project.trackIndex (curTrack);
        const int hintWidth = mode == Mode::Resize ? 310 : 230;
        const int x = std::clamp (static_cast<int> ((clipStart + clipLength) * owner.pixelsPerBeat) + 8,
                                  4, std::max (4, getWidth() - hintWidth - 4));
        const int y = owner.rowTop (targetRow) + 8;
        const auto hint = juce::Rectangle<int> (x, y, hintWidth, 24);
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
    roll::AutomationEnvelope automationEditEnvelope;
    juce::String automationPointValueText;
    int automationEditTrackId = -1, automationPointIndex = -1, automationEditRow = -1;
    bool automationPointWasExisting = false, automationWasDragged = false, automationDoubleClick = false;
    Mode mode = Mode::None;
    bool dragActive = false, movePreviewActive = false, movePreviewIsCopy = false, movePreviewAllowed = true;
    bool movePreviewReplacesSource = false;
    int movePreviewTrackIndex = -1;
    double movePreviewStart = 0.0;
    bool dragDropAllowed = false, dragDropIsAudio = false;
    int curTrack = -1, curClip = -1, curAudioClipId = -1;
    int dragTrackIndex = -1;
    double origStart = 0.0, origLen = 4.0, downBeat = 0.0, dragBeat = 0.0;
    double origFadeIn = 0.0, origFadeOut = 0.0;
    double origFadeInCurve = 0.0, origFadeOutCurve = 0.0, downY = 0.0;
    bool hasRangeSelection = false;
    double rangeStartBeat = 0.0, rangeEndBeat = 0.0;
    int rangeStartTrack = 0, rangeEndTrack = 0;
    int selectedFrozenTrackId = -1;
    double shiftRangeAnchorBeat = -1.0;
    int shiftRangeAnchorTrack = -1;
    int multiDragAnchorTrackIndex = -1;
    int pendingRangeTrack = -1;
    std::vector<ClipSelection> selectedClips;
    std::vector<MultiDragSource> multiDragSources;
    std::vector<MovePreviewItem> multiMovePreviewItems;
    std::vector<ClipboardClip> clipboard;
    ClipSelection selectionAnchor, pendingSelection;
    juce::Point<float> selectionDownPosition;
};

// ============================================================ Ruler (ruler + loop bracket)

class ArrangementView::Ruler : public juce::Component, public juce::SettableTooltipClient
{
public:
    Ruler (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setTooltip ("On the ruler: drag vertically to zoom and horizontally to scroll.");
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panelLight());
        juce::Component* const toolbarControls[] {
            &owner.locatorDeleteButton, &owner.locatorNextButton, &owner.locatorSetButton,
            &owner.locatorPreviousButton, &owner.automationTargetButton,
            &owner.automationModeButton, &owner.automationStatusLabel
        };
        const int scrollX = owner.viewport.getViewPositionX();
        for (auto* control : toolbarControls)
        {
            if (! control->isVisible())
                continue;
            auto bounds = control->getBounds();
            bounds.translate (-headerWidth + scrollX, -bounds.getY());
            bounds.setHeight (getHeight());
            g.excludeClipRegion (bounds);
        }

        const auto cb = g.getClipBounds();
        const double ppb = owner.pixelsPerBeat;

        // Loop bracket.
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
            const auto locatorRed = theme::accent();
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
                const juce::Rectangle<int> labelBounds (labelX, 1, labelWidth, 13);
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

        g.setFont (juce::FontOptions (11.5f));
        const int firstBeat = std::max (0, static_cast<int> (std::floor (cb.getX() / ppb)) - 1);
        const int lastBeat = static_cast<int> (std::ceil (cb.getRight() / ppb));
        const int barsPerLabel = arrangementBarStride (ppb);
        for (int b = firstBeat; b <= lastBeat; ++b)
        {
            const bool bar = (b % 4 == 0);
            if (! bar && ppb < 14.0) continue;
            if (bar && (b / 4) % barsPerLabel != 0) continue;
            const float x = static_cast<float> (b * ppb);
            g.setColour (bar ? theme::text() : theme::textDim());
            g.drawVerticalLine (static_cast<int> (x), bar ? 15.0f : 23.0f,
                                static_cast<float> (getHeight()));
            if (bar)
                g.drawText (juce::String (b / 4 + 1), static_cast<int> (x) + 3, 15,
                            std::clamp (juce::roundToInt (barsPerLabel * 4.0 * ppb) - 6, 32, 64),
                            getHeight() - 15, juce::Justification::centredLeft, false);
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
                navigationLastScreenPosition = e.getScreenPosition();
                navigationAnchorBeat = e.position.x / owner.pixelsPerBeat;
                navigationAnchorScreenX = static_cast<int> (std::lround (
                    e.position.x - owner.viewport.getViewPositionX()));
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

            mode = Mode::PanZoomView;
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
            case Mode::PanZoomView:
            {
                const auto delta = screenPosition - navigationLastScreenPosition;
                navigationLastScreenPosition = screenPosition;
                if (delta.y != 0)
                    owner.zoomAt (std::exp (static_cast<double> (delta.y) * 0.012),
                                  navigationAnchorBeat, navigationAnchorScreenX);

                if (delta.x != 0)
                {
                    const int oldViewX = owner.viewport.getViewPositionX();
                    owner.viewport.setViewPosition (std::max (0, oldViewX + delta.x),
                                                    owner.viewport.getViewPositionY());
                    const int actualDelta = owner.viewport.getViewPositionX() - oldViewX;
                    if (actualDelta != 0)
                    {
                        navigationAnchorBeat += static_cast<double> (actualDelta) / owner.pixelsPerBeat;
                        owner.syncPositions();
                    }
                }
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
    enum class Mode { Seek, PanZoomView, LoopLeft, LoopRight, LoopMove, LocatorMove };
    void notifyInteraction (bool active, int holdMilliseconds = 0, bool dragging = false)
    {
        if (! owner.onInteractionChanged)
            return;

        juce::String message;
        auto icon = icons::Icon::Pointer;
        switch (mode)
        {
            case Mode::Seek:       message = active ? (dragging ? "Dragging playhead" : "Dragging playhead") : "Cursor positioned"; break;
            case Mode::PanZoomView:message = active ? "Adjusting zoom and scrolling" : "Zoom and scrolling adjusted"; icon = icons::Icon::Resize; break;
            case Mode::LoopLeft:
            case Mode::LoopRight:  message = active ? (dragging ? "Adjusting loop" : "Dragging loop boundary") : "Loop adjusted"; icon = icons::Icon::SelectRegion; break;
            case Mode::LoopMove:   message = active ? (dragging ? "Moving loop" : "Dragging loop") : "Loop moved"; icon = icons::Icon::Loop; break;
            case Mode::LocatorMove:message = active ? (dragging ? "Moving locator" : "Dragging locator") : "Locator moved"; icon = icons::Icon::Move; break;
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
    juce::Point<int> navigationLastScreenPosition;
    int navigationAnchorScreenX = 0;
    double navigationAnchorBeat = 0.0;
    int hoveredLocatorId = -1;
    juce::String hoveredInfo;

    ArrangementLocator* locatorAt (juce::Point<float> position) const
    {
        if (position.y >= rulerHeight)
            return nullptr;
        for (std::size_t reverseIndex = project.locators.size(); reverseIndex > 0; --reverseIndex)
        {
            const auto index = reverseIndex - 1;
            const auto& marker = project.locators[index];
            const float x = static_cast<float> (marker.beat * owner.pixelsPerBeat);
            const bool onPole = std::abs (position.x - x) < 5.0f && position.y <= 26.0f;
            const bool onFlag = position.x >= x && position.x <= x + 14.0f
                             && position.y <= 14.0f;
            if (onPole || onFlag)
                return project.locator (marker.id);

            const bool emphasized = marker.id == project.selectedLocatorId || marker.id == hoveredLocatorId;
            const bool hasGeneratedName = marker.name.empty() || marker.name == std::to_string (marker.id);
            const juce::String label = hasGeneratedName ? juce::String (static_cast<int> (index + 1))
                                                        : juce::String (marker.name);
            int labelWidth = std::clamp (label.length() * 7 + 12, 24, 76);
            if (! emphasized && index + 1 < project.locators.size())
            {
                const auto gap = static_cast<int> (
                    (project.locators[index + 1].beat - marker.beat) * owner.pixelsPerBeat) - 18;
                labelWidth = std::min (labelWidth, gap);
            }
            if (labelWidth < 20)
                continue;

            int labelX = static_cast<int> (x) + 16;
            if (labelX + labelWidth > getWidth() - 2)
                labelX = static_cast<int> (x) - labelWidth - 2;
            labelX = std::clamp (labelX, 2, std::max (2, getWidth() - labelWidth - 2));
            if (juce::Rectangle<float> (static_cast<float> (labelX), 1.0f,
                                        static_cast<float> (labelWidth), 14.0f).contains (position))
                return project.locator (marker.id);
        }
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

        juce::Component::SafePointer<Ruler> safeThis (this);
        AppModalDialog::showInput (
            *this, editInfo ? "Edit Locator Information" : "Rename Locator",
            editInfo ? "Locator Information:" : "Locator Name:",
            juce::String (editInfo ? marker->info : marker->name), {},
            { { "OK", 1, AppModalDialog::ButtonStyle::Primary },
              { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
            [safeThis, id, editInfo] (int result, juce::String value)
            {
                if (safeThis != nullptr && result == 1)
                {
                    const auto text = value.toStdString();
                    if (editInfo)
                        safeThis->project.setLocatorInfo (id, text);
                    else
                        safeThis->project.renameLocator (id, text);
                }
            });
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
        else if (target == Mode::PanZoomView
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
            message = "Loop start: " + formatPosition (project.loopStart);
        else if (mode == Mode::LoopRight)
            message = "Loop end: " + formatPosition (project.loopEnd);
        else
            message = "Loop: " + formatPosition (project.loopStart) + " - "
                    + formatPosition (project.loopEnd) + "  ("
                    + juce::String ((project.loopEnd - project.loopStart) / 4.0, 1) + " bars)";

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

class ArrangementView::TimeRuler final : public juce::Component
{
public:
    explicit TimeRuler (ArrangementView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panelLight().darker (0.12f));
        g.setColour (theme::border());
        g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

        const double pixelsPerSecond = owner.pixelsPerBeat * owner.bpm / 60.0;
        if (pixelsPerSecond <= 0.0)
            return;

        static constexpr double intervals[] { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
                                                60.0, 120.0, 300.0, 600.0,
                                                1200.0, 1800.0, 3600.0 };
        const auto interval = *std::find_if (std::begin (intervals), std::end (intervals),
            [pixelsPerSecond] (double seconds) { return seconds * pixelsPerSecond >= 100.0; });
        const auto clip = g.getClipBounds();
        const int firstTick = std::max (0, static_cast<int> (std::floor (
            clip.getX() / (interval * pixelsPerSecond))));
        const int lastTick = static_cast<int> (std::ceil (
            clip.getRight() / (interval * pixelsPerSecond)));

        g.setFont (juce::FontOptions (11.5f));
        for (int tick = firstTick; tick <= lastTick; ++tick)
        {
            const double seconds = tick * interval;
            const int x = juce::roundToInt (seconds * pixelsPerSecond);
            g.setColour (theme::border().brighter (0.25f));
            g.drawVerticalLine (x, 5.0f, static_cast<float> (getHeight() - 1));
            g.setColour (theme::textDim());
            g.drawText (formatTimelineTime (seconds), x + 5, 3, 52, getHeight() - 6,
                        juce::Justification::centredLeft, false);
        }

        const int playheadX = juce::roundToInt (owner.playhead * owner.pixelsPerBeat);
        g.setColour (theme::playhead().withAlpha (0.8f));
        g.drawVerticalLine (playheadX, 2.0f, static_cast<float> (getHeight() - 1));
    }

private:
    ArrangementView& owner;
};

// ============================================================ Header (track header)

class ArrangementView::Header : public juce::Component
{
public:
    Header (ProjectModel& p, ArrangementView& o, int id) : project (p), owner (o), trackId (id)
    {
        auto* t = project.track (trackId);
        addAndMakeVisible (name);
        addAndMakeVisible (frozenBadge);
        addAndMakeVisible (foldButton);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        addAndMakeVisible (volume);
        addAndMakeVisible (volumeDb);
        addAndMakeVisible (volumeLabel);
        addAndMakeVisible (panLabel);
        addAndMakeVisible (pan);
        addAndMakeVisible (panValue);
        addAndMakeVisible (routingButton);

        foldButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.onClick = [this] { owner.toggleTrackCollapsed (trackId); };
        updateFoldButton();

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
        name.setTooltip (theme::ui ("Drag the track name to reorder it; double-click to rename."));
        frozenBadge.setText ("FROZEN", juce::dontSendNotification);
        frozenBadge.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        frozenBadge.setColour (juce::Label::textColourId, theme::accent());
        frozenBadge.setJustificationType (juce::Justification::centredRight);
        frozenBadge.setTooltip (theme::ui ("This track is frozen. Right-click the track to unfreeze or flatten it."));
        frozenBadge.setVisible (t != nullptr && t->frozen);
        name.addMouseListener (this, false);
        name.onTextChange = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->name = name.getText().toStdString();
                project.sendChangeMessage();
            }
        };

        for (auto* b : { &mute, &solo })
        {
            b->setClickingTogglesState (true);
            b->setMouseClickGrabsKeyboardFocus (false);
        }
        const juce::Component::SafePointer<Header> safeThis (this);
        const auto configureMidiControl = [this, safeThis] (auto& control, MidiMappingTarget target)
        {
            control.mappingInfo = midiMappingInfo (project, trackId, target);
            control.onMidiLearn = [safeThis, target]
            {
                if (safeThis != nullptr && safeThis->owner.onMidiLearnRequested)
                    safeThis->owner.onMidiLearnRequested (safeThis->trackId, target);
            };
            control.onMidiClear = [safeThis, target]
            {
                if (safeThis != nullptr && safeThis->owner.onMidiMappingClearRequested)
                    safeThis->owner.onMidiMappingClearRequested (safeThis->trackId, target);
            };
        };
        configureMidiControl (volume, MidiMappingTarget::TrackVolume);
        configureMidiControl (pan, MidiMappingTarget::TrackPan);
        configureMidiControl (mute, MidiMappingTarget::TrackMute);
        configureMidiControl (solo, MidiMappingTarget::TrackSolo);

        solo.setColour (juce::TextButton::buttonOnColourId, theme::accent());
        mute.setColour (juce::TextButton::buttonOnColourId, theme::accent());
        mute.setColour (juce::TextButton::textColourOnId, theme::text());
        solo.setColour (juce::TextButton::textColourOnId, theme::text());
        mute.setTooltip (theme::ui ("Track enable: disables MIDI/audio output."));
        solo.setTooltip (theme::ui ("Click for exclusive solo; Ctrl-click to add or remove solo. "
                                    "With multiple tracks selected, click to apply to all."));
        mute.setToggleState (t->mute, juce::dontSendNotification);
        mute.setButtonText (t->mute ? "OFF" : "ON");
        solo.setToggleState (t->solo, juce::dontSendNotification);
        mute.onClick = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                project.beginEdit();
                tr->mute = mute.getToggleState();
                mute.setButtonText (tr->mute ? "OFF" : "ON");
                owner.selectMixerAutomationParameter (
                    trackId, roll::AutomationParameterType::Mute);
                project.notifyMixer();
                project.endEdit();
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
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setTooltip (theme::ui ("Pan: left (-100%) / right (+100%); double-click to center; click to enable the mouse wheel."));
        pan.setRange (-1.0, 1.0, 0.01);
        pan.centreValue = 0.0;
        pan.bipolar = true;
        pan.setValue (t->pan, juce::dontSendNotification);
        pan.setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
        pan.setColour (juce::Slider::rotarySliderOutlineColourId, theme::border().brighter (0.45f));
        pan.setMouseClickGrabsKeyboardFocus (false);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.onValueChange = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->pan = pan.getValue();
                updatePanText (tr->pan);
                owner.selectMixerAutomationParameter (
                    trackId, roll::AutomationParameterType::Pan);
                project.notifyMixer();
            }
        };
        pan.setDoubleClickReturnValue (true, 0.0);
        updatePanText (t->pan);

        volume.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setTooltip (theme::ui ("Track volume; click to enable the mouse wheel."));
        volume.setRange (0.0, 1.5, 0.001);
        volume.bipolar = false;
        volume.setValue (t->volume, juce::dontSendNotification);
        volume.setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
        volume.setColour (juce::Slider::rotarySliderOutlineColourId, theme::border().brighter (0.45f));
        volume.setMouseClickGrabsKeyboardFocus (false);
        volume.setPopupDisplayEnabled (true, true, nullptr);
        volume.textFromValueFunction = [] (double v)
        {
            const float db = roll::gainToDecibels (static_cast<float> (v));
            if (db <= -59.5f) return juce::String ("-inf dB");
            return (db >= 0.05f ? juce::String ("+") : juce::String()) + juce::String (db, 1) + " dB";
        };
        volumeLabel.setText ("Vol", juce::dontSendNotification);
        volumeLabel.setFont (juce::FontOptions (11.5f));
        volumeLabel.setColour (juce::Label::textColourId, theme::text());
        volumeDb.setFont (juce::FontOptions (11.5f));
        volumeDb.setColour (juce::Label::textColourId, theme::text());
        volumeDb.setJustificationType (juce::Justification::centredRight);
        volume.setColour (juce::Slider::textBoxTextColourId, theme::text());
        volume.setColour (juce::Slider::textBoxBackgroundColourId, theme::panel());
        volume.setColour (juce::Slider::textBoxOutlineColourId, theme::border());
        updateVolumeText (t->volume);
        volume.onValueChange = [this]
        {
            if (auto* tr = project.track (trackId))
            {
                tr->volume = volume.getValue();
                updateVolumeText (tr->volume);
                owner.selectMixerAutomationParameter (
                    trackId, roll::AutomationParameterType::Volume);
                project.notifyMixer();
            }
        };
        volume.setDoubleClickReturnValue (true, 1.0);
        volume.onDragStart = [this]
        {
            project.beginEdit();
            owner.selectMixerAutomationParameter (
                trackId, roll::AutomationParameterType::Volume);
        };
        volume.onDragEnd = [this] { project.endEdit(); };
        pan.onDragStart = [this]
        {
            project.beginEdit();
            owner.selectMixerAutomationParameter (
                trackId, roll::AutomationParameterType::Pan);
        };
        pan.onDragEnd = [this] { project.endEdit(); };

        name.onEditorShow = [this] { project.beginEdit(); };
        name.onEditorHide = [this] { project.endEdit(); };
    }

    int id() const { return trackId; }
    void setPeak (float p) { if (std::abs (p - peak) > 0.004f) { peak = p; repaint (getWidth() - 16, 0, 16, getHeight()); } }
    void refreshText()
    {
        refreshMidiMappingInfo();
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
        resized();
        repaint();
    }

    void refreshMidiMappingInfo()
    {
        volume.mappingInfo = midiMappingInfo (project, trackId, MidiMappingTarget::TrackVolume);
        pan.mappingInfo = midiMappingInfo (project, trackId, MidiMappingTarget::TrackPan);
        mute.mappingInfo = midiMappingInfo (project, trackId, MidiMappingTarget::TrackMute);
        solo.mappingInfo = midiMappingInfo (project, trackId, MidiMappingTarget::TrackSolo);
    }

    void refreshLiveMixerValues (ArrangementView::LiveMixerValues values)
    {
        if (! volume.isMouseButtonDown())
        {
            volume.setValue (values.gain, juce::dontSendNotification);
            updateVolumeText (values.gain);
        }
        if (! pan.isMouseButtonDown())
        {
            pan.setValue (values.pan, juce::dontSendNotification);
            updatePanText (values.pan);
        }
        if (! mute.isMouseButtonDown())
        {
            mute.setToggleState (values.muted, juce::dontSendNotification);
            mute.setButtonText (values.muted ? "OFF" : "ON");
        }
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
            routingButton.setColour (juce::TextButton::buttonColourId, ext ? theme::accent() : theme::panelLight());
            routingButton.setColour (juce::TextButton::textColourOffId, ext ? theme::text() : theme::textDim());
            routingButton.setTooltip (ext ? theme::ui ("Destination: Ext. Out (Direct - bypasses Master)")
                                          : theme::ui ("Destination: Master (processed by the Master channel)"));
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
        g.setColour (track != nullptr ? colourFor (*track) : colourFor (trackId));
        g.fillRect (0, 0, 6, getHeight());
        if (track != nullptr && track->parentGroupId != 0)
        {
            const int index = project.trackIndex (trackId);
            const bool hasPreviousSibling = index > 0
                && project.tracks[static_cast<std::size_t> (index - 1)]->parentGroupId == track->parentGroupId;
            const bool hasNextSibling = index + 1 < static_cast<int> (project.tracks.size())
                && project.tracks[static_cast<std::size_t> (index + 1)]->parentGroupId == track->parentGroupId;
            const float connectorX = 42.0f;
            const float middleY = static_cast<float> (getHeight()) * 0.5f;
            const auto* parent = project.track (track->parentGroupId);
            g.setColour ((parent != nullptr ? colourFor (*parent) : colourFor (track->parentGroupId))
                         .brighter (0.25f));
            if (hasPreviousSibling)
                g.drawLine (connectorX, 0.0f, connectorX, middleY, 2.0f);
            if (hasNextSibling)
                g.drawLine (connectorX, middleY, connectorX, static_cast<float> (getHeight()), 2.0f);
            g.drawLine (connectorX, middleY, connectorX + 6.0f, middleY, 2.0f);
            g.fillEllipse (connectorX + 4.0f, middleY - 2.0f, 4.0f, 4.0f);
        }
        g.setColour (theme::border());
        g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

        if (track == nullptr || ! track->collapsed)
        {
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (12.5f));
            const juce::String summary = owner.deviceSummary ? owner.deviceSummary (trackId) : juce::String();
            g.drawText (summary.isEmpty() ? theme::ui ("No instrument") : summary,
                        14, 26, getWidth() - 30, 14, juce::Justification::centredLeft, true);
        }

        const float h = static_cast<float> (getHeight() - 12);
        const float level = std::min (1.0f, peak);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (static_cast<float> (getWidth() - 12), 6.0f, 6.0f, h);
        g.setColour (level > 0.9f ? theme::warning() : theme::accent());
        g.fillRect (static_cast<float> (getWidth() - 12), 6.0f + h * (1.0f - level), 6.0f, h * level);

        if (owner.trackReorderSourceId >= 0 && owner.trackReorderDropIndex >= 0)
        {
            const int index = project.trackIndex (trackId);
            if (owner.trackReorderDropIndex == index)
                g.fillAll (theme::accent().withAlpha (0.08f));
            if (owner.trackReorderDropIndex == index
                || owner.trackReorderDropIndex == index + 1)
            {
                g.setColour (theme::accent());
                g.fillRect (0, owner.trackReorderDropIndex == index ? 0 : getHeight() - 3,
                            getWidth(), 3);
            }
        }
    }

    void resized() override
    {
        const auto* track = project.track (trackId);
        const bool collapsed = track != nullptr && track->collapsed;
        updateFoldButton();
        routingButton.setVisible (! collapsed);
        frozenBadge.setVisible (track != nullptr && track->frozen);
        mute.setVisible (! collapsed);
        solo.setVisible (! collapsed);
        volume.setVisible (! collapsed);
        volumeDb.setVisible (! collapsed);
        volumeLabel.setVisible (! collapsed);
        panLabel.setVisible (! collapsed);
        pan.setVisible (! collapsed);
        panValue.setVisible (! collapsed);

        if (collapsed)
        {
            auto row = getLocalBounds().reduced (6, 2);
            row.removeFromLeft (8);
            row.removeFromRight (14);
            foldButton.setBounds (row.removeFromLeft (18));
            row.removeFromLeft (4);
            if (track != nullptr && track->parentGroupId != 0)
                row.removeFromLeft (12);
            if (track != nullptr && track->frozen)
            {
                frozenBadge.setBounds (row.removeFromRight (52));
                row.removeFromRight (4);
            }
            name.setBounds (row);
            return;
        }

        auto a = getLocalBounds().reduced (6, 4);
        a.removeFromLeft (8);
        a.removeFromRight (14);
        auto topRow = a.removeFromTop (22);
        foldButton.setBounds (topRow.removeFromLeft (18));
        topRow.removeFromLeft (4);
        routingButton.setBounds (topRow.removeFromRight (58));
        topRow.removeFromRight (4);
        if (track != nullptr && track->frozen)
        {
            frozenBadge.setBounds (topRow.removeFromRight (52));
            topRow.removeFromRight (4);
        }
        if (track != nullptr && track->parentGroupId != 0)
            topRow.removeFromLeft (12);
        name.setBounds (topRow);
        a.removeFromTop (14);
        auto row = a.removeFromTop (24);
        mute.setBounds (row.removeFromLeft (34));
        row.removeFromLeft (4);
        solo.setBounds (row.removeFromLeft (26));
        row.removeFromLeft (4);
        volumeLabel.setBounds (row.removeFromLeft (26));
        row.removeFromLeft (4);
        volume.setBounds (row.removeFromLeft (24));
        row.removeFromRight (4);
        volumeDb.setBounds (row.removeFromRight (48));
        auto panRow = a.removeFromTop (26);
        panLabel.setBounds (panRow.removeFromLeft (30));
        panRow.removeFromLeft (4);
        pan.setBounds (panRow.removeFromLeft (26));
        panRow.removeFromLeft (4);
        panValue.setBounds (panRow.removeFromLeft (44));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        reorderPending = e.originalComponent == &name && ! e.mods.isPopupMenu();
        reorderDragging = false;

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
            owner.selectTrackFromHeader (trackId, e.mods);
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

        owner.lanes->clearSelection();
        owner.selectTrackFromHeader (trackId, e.mods);
        owner.grabFocus();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! reorderPending || e.originalComponent != &name)
            return;
        if (! reorderDragging && e.getDistanceFromDragStart() < 5.0f)
            return;

        reorderDragging = true;
        owner.trackReorderSourceId = trackId;
        updateReorderDropIndex (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! reorderPending || e.originalComponent != &name)
            return;

        reorderPending = false;
        if (! reorderDragging)
            return;

        updateReorderDropIndex (e);
        const int sourceIndex = project.trackIndex (trackId);
        int targetIndex = owner.trackReorderDropIndex;
        owner.trackReorderSourceId = -1;
        owner.trackReorderDropIndex = -1;
        owner.headerHolder.repaint();
        owner.lanes->repaint();
        reorderDragging = false;

        if (sourceIndex < 0 || targetIndex < 0)
            return;
        if (targetIndex > sourceIndex)
            --targetIndex;
        project.moveTrack (trackId, std::clamp (targetIndex, 0,
                                                static_cast<int> (project.tracks.size()) - 1));
    }

    void showTrackMenu()
    {
        juce::PopupMenu m;
        juce::PopupMenu colourMenu;
        const auto* selected = project.track (trackId);
        colourMenu.addItem (10, theme::ui ("Automatic"), true,
                            selected != nullptr && selected->colour == 0);
        for (std::size_t i = 0; i < trackColourNames.size(); ++i)
            colourMenu.addItem (11 + static_cast<int> (i), theme::ui (trackColourNames[i]), true,
                selected != nullptr && selected->colour == trackColours[i]);
        m.addSubMenu (theme::ui ("Track Color"), colourMenu, selected != nullptr);
        m.addItem (1, theme::ui ("New MIDI Track"));
        m.addItem (3, theme::ui ("New Audio Track"));
        const bool canGroup = project.selectedTrackIds.size() >= 2
            && std::all_of (project.selectedTrackIds.begin(), project.selectedTrackIds.end(),
                [this] (int id)
                {
                    const auto* selected = project.track (id);
                    return selected != nullptr && selected->type != roll::TrackType::Group
                        && selected->parentGroupId == 0;
                });
        m.addItem (4, theme::ui ("Group Selected Tracks (Ctrl+G)"), canGroup);
        const int tid = trackId;
        const bool isGroup = selected != nullptr && selected->type == roll::TrackType::Group;
        const bool isFrozen = selected != nullptr && selected->frozen;
        if (isGroup)
            m.addItem (5, theme::ui ("Ungroup Track (Ctrl+Shift+G)"));
        else
        {
            m.addItem (6, theme::ui ("Duplicate Track (Ctrl+D)"));
            m.addItem (2, theme::ui ("Delete Track"), project.tracks.size() > 1);
            m.addSeparator();
            if (isFrozen)
            {
                m.addItem (8, theme::ui ("Unfreeze Track"), selected->freezeReversible);
                m.addItem (9, theme::ui ("Flatten Track"), selected->freezeReversible);
            }
            else
                m.addItem (7, theme::ui ("Freeze Track"));
        }
        auto* proj = &project;
        auto* arrangement = &owner;
        m.showMenuAsync ({}, [proj, arrangement, tid] (int r)
        {
            if (r == 10)
                proj->setTrackColour (tid, 0);
            else if (r >= 11 && r < 11 + static_cast<int> (trackColours.size()))
                proj->setTrackColour (tid, trackColours[static_cast<std::size_t> (r - 11)]);
            if (r == 1) proj->addTrack();
            if (r == 2) proj->removeTrack (tid);
            if (r == 3) proj->addTrack (roll::TrackType::Audio);
            if (r == 4) proj->groupTracks (proj->selectedTrackIds);
            if (r == 5) proj->ungroupTrack (tid);
            if (r == 6 && arrangement->onTrackDuplicateRequested)
                arrangement->onTrackDuplicateRequested (tid);
            if (r == 7 && arrangement->onFreezeTrackRequested)
                arrangement->onFreezeTrackRequested (tid);
            if (r == 8 && arrangement->onUnfreezeTrackRequested)
                arrangement->onUnfreezeTrackRequested (tid);
            if (r == 9 && arrangement->onFlattenTrackRequested)
                arrangement->onFlattenTrackRequested (tid);
        });
    }

private:
    void updateFoldButton()
    {
        const auto* track = project.track (trackId);
        const bool collapsed = track != nullptr && track->collapsed;
        foldButton.setButtonText (collapsed ? ">" : "v");
        foldButton.setTooltip (theme::ui (collapsed ? "Expand Track" : "Collapse Track"));
    }

    void updateReorderDropIndex (const juce::MouseEvent& e)
    {
        const auto position = e.getEventRelativeTo (&owner.headerHolder).position;
        const auto contentY = position.y + static_cast<float> (owner.viewport.getViewPositionY());
        owner.trackReorderDropIndex = owner.rowInsertionIndexAt (juce::roundToInt (contentY));
        owner.headerHolder.repaint();
        owner.lanes->repaint();
    }

    ProjectModel& project;
    ArrangementView& owner;
    int trackId;
    bool reorderPending = false, reorderDragging = false;
    juce::Label name, frozenBadge, volumeDb, volumeLabel, panLabel, panValue;
    juce::TextButton foldButton;
    MidiLearnButton mute { "ON" }, solo { "S" };
    juce::TextButton routingButton;
    MixerKnobSlider volume, pan;
    float peak = 0.0f;
};

// ============================================================ MasterHeader (Master track)

class ArrangementView::MasterHeader : public juce::Component, public juce::DragAndDropTarget
{
public:
    MasterHeader (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        addAndMakeVisible (name);
        addAndMakeVisible (foldButton);
        addAndMakeVisible (volume);
        addAndMakeVisible (masterVolumeDbLabel);
        addAndMakeVisible (peakDbLabel);
        addAndMakeVisible (panLabel);
        addAndMakeVisible (pan);
        addAndMakeVisible (panValue);

        const juce::Component::SafePointer<MasterHeader> safeThis (this);
        volume.onMidiLearn = [safeThis]
        {
            if (safeThis != nullptr && safeThis->owner.onMidiLearnRequested)
                safeThis->owner.onMidiLearnRequested (0, MidiMappingTarget::MasterVolume);
        };
        volume.onMidiClear = [safeThis]
        {
            if (safeThis != nullptr && safeThis->owner.onMidiMappingClearRequested)
                safeThis->owner.onMidiMappingClearRequested (0, MidiMappingTarget::MasterVolume);
        };
        pan.onMidiLearn = [safeThis]
        {
            if (safeThis != nullptr && safeThis->owner.onMidiLearnRequested)
                safeThis->owner.onMidiLearnRequested (0, MidiMappingTarget::MasterPan);
        };
        pan.onMidiClear = [safeThis]
        {
            if (safeThis != nullptr && safeThis->owner.onMidiMappingClearRequested)
                safeThis->owner.onMidiMappingClearRequested (0, MidiMappingTarget::MasterPan);
        };
        refreshMidiMappingInfo();

        name.setText ("Master", juce::dontSendNotification);
        name.setTooltip (theme::ui ("Click to select; right-click to choose a color; drag an effect here to add it to Master."));
        name.setInterceptsMouseClicks (false, false);
        name.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, theme::text());
        foldButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.onClick = [this] { owner.toggleTrackCollapsed (0); };
        updateFoldButton();

        volume.bipolar = false;
        volume.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.textFromValueFunction = [] (double db)
        {
            if (db <= -59.5) return juce::String ("-inf dB");
            return (db >= 0.05 ? juce::String ("+") : juce::String())
                   + juce::String (db, 1) + " dB";
        };
        volume.setValue (0.0, juce::dontSendNotification);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTooltip (theme::ui ("Master volume (-inf to +6.0 dB); double-click to reset to 0 dB; click to enable the mouse wheel."));
        volume.setMouseClickGrabsKeyboardFocus (false);
        volume.setPopupDisplayEnabled (true, true, nullptr);
        volume.setColour (juce::Slider::textBoxTextColourId, theme::text());
        volume.setColour (juce::Slider::textBoxBackgroundColourId, theme::panel());
        volume.setColour (juce::Slider::textBoxOutlineColourId, theme::border());
        masterVolumeDbLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        masterVolumeDbLabel.setColour (juce::Label::textColourId, theme::text());
        masterVolumeDbLabel.setJustificationType (juce::Justification::centredRight);
        updateMasterVolumeDbText (0.0f);
        volume.onDragStart = [this]
        {
            project.beginEdit();
            owner.selectMixerAutomationParameter (
                0, roll::AutomationParameterType::Volume);
        };
        volume.onDragEnd = [this] { project.endEdit(); };

        panLabel.setText ("Pan", juce::dontSendNotification);
        panLabel.setFont (juce::FontOptions (11.5f));
        panLabel.setColour (juce::Label::textColourId, theme::textDim());
        panValue.setFont (juce::FontOptions (11.5f));
        panValue.setColour (juce::Label::textColourId, theme::textDim());
        panValue.setJustificationType (juce::Justification::centredRight);
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.centreValue = 0.0;
        pan.bipolar = true;
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setValue (project.masterTrack != nullptr ? project.masterTrack->pan : 0.0,
                      juce::dontSendNotification);
        pan.setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
        pan.setColour (juce::Slider::rotarySliderOutlineColourId, theme::border().brighter (0.35f));
        pan.setColour (juce::Slider::thumbColourId, theme::text());
        pan.setMouseClickGrabsKeyboardFocus (false);
        pan.setTooltip (theme::ui ("Master pan: left (-100%) / right (+100%); double-click to center; click to enable the mouse wheel."));
        pan.setPopupDisplayEnabled (true, true, nullptr);
        updatePanText (pan.getValue());
        pan.onDragStart = [this]
        {
            project.beginEdit();
            owner.selectMixerAutomationParameter (
                0, roll::AutomationParameterType::Pan);
        };
        pan.onDragEnd = [this] { project.endEdit(); };
        pan.onValueChange = [this]
        {
            if (project.masterTrack != nullptr)
            {
                project.masterTrack->pan = pan.getValue();
                updatePanText (pan.getValue());
                owner.selectMixerAutomationParameter (
                    0, roll::AutomationParameterType::Pan);
                project.notifyMixer();
            }
        };
        pan.onDoubleClick = [this]
        {
            if (project.masterTrack == nullptr || std::abs (project.masterTrack->pan) < 0.005)
                return;
            project.beginEdit();
            pan.setValue (0.0, juce::sendNotificationSync);
            project.endEdit();
        };

        peakDbLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        peakDbLabel.setColour (juce::Label::textColourId, theme::text());
        peakDbLabel.setJustificationType (juce::Justification::centredRight);
        peakDbLabel.setTooltip (theme::ui ("Master peak; click the meter to reset it."));
        updatePeakDbText (0.0f);

        volume.onValueChange = [this]
        {
            const float db = static_cast<float> (volume.getValue());
            updateMasterVolumeDbText (db);
            if (project.masterTrack != nullptr)
            {
                project.masterTrack->volume = roll::decibelsToGain (db);
                owner.selectMixerAutomationParameter (
                    0, roll::AutomationParameterType::Volume);
                project.notifyMixer();
            }
        };
    }

    void refreshMidiMappingInfo()
    {
        volume.mappingInfo = midiMappingInfo (project, 0, MidiMappingTarget::MasterVolume);
        pan.mappingInfo = midiMappingInfo (project, 0, MidiMappingTarget::MasterPan);
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

    void setPeak (float p)
    {
        if (p >= 1.0f)
            clipHeld = true;

        const float previousPeak = peak;
        const float previousHold = peakHold;
        peak = p;
        peakHold = std::max (peakHold, p);
        updatePeakDbText (peakHold);
        if (std::abs (p - previousPeak) > 0.003f || peakHold != previousHold)
        {
            repaint (getWidth() - 30, 0, 30, getHeight());
        }
    }

    void setStereoPeaks (float left, float right)
    {
        const bool changed = std::abs (left - leftPeak) > 0.003f
                          || std::abs (right - rightPeak) > 0.003f
                          || left > leftPeakHold || right > rightPeakHold;
        leftPeak = left;
        rightPeak = right;
        leftPeakHold = std::max (leftPeakHold, left);
        rightPeakHold = std::max (rightPeakHold, right);
        if (changed)
            repaint (getWidth() - 30, 0, 30, getHeight());
    }

    void refreshText()
    {
        updateFoldButton();
        refreshMidiMappingInfo();
        if (project.masterTrack != nullptr)
        {
            const float db = roll::gainToDecibels (static_cast<float> (project.masterTrack->volume));
            volume.setValue (db, juce::dontSendNotification);
            updateMasterVolumeDbText (db);
            pan.setValue (project.masterTrack->pan, juce::dontSendNotification);
            updatePanText (project.masterTrack->pan);
        }
        repaint();
    }

    void refreshLiveMixerValues (ArrangementView::LiveMixerValues values)
    {
        if (! volume.isMouseButtonDown())
        {
            const float db = roll::gainToDecibels (values.gain);
            volume.setValue (db, juce::dontSendNotification);
            updateMasterVolumeDbText (db);
        }
        if (! pan.isMouseButtonDown())
        {
            pan.setValue (values.pan, juce::dontSendNotification);
            updatePanText (values.pan);
        }
    }

    void updatePanText (double value)
    {
        const auto amount = juce::roundToInt (std::abs (value) * 100.0);
        const auto text = value < -0.005 ? juce::String ("L ") + juce::String (amount) + "%"
                         : value > 0.005 ? juce::String ("R ") + juce::String (amount) + "%"
                                         : juce::String();
        panValue.setText (text, juce::dontSendNotification);
    }

    void updatePeakDbText (float level)
    {
        const auto text = level <= 0.001f
            ? juce::String ("Peak -inf")
            : juce::String ("Peak ") + juce::String (roll::gainToDecibels (level), 1);
        if (peakDbLabel.getText() != text)
            peakDbLabel.setText (text, juce::dontSendNotification);
    }

    void updateMasterVolumeDbText (float db)
    {
        const juce::String text = db <= -59.5f
            ? juce::String ("-inf dB")
            : (db >= 0.05f ? juce::String ("+") : juce::String()) + juce::String (db, 1) + " dB";
        if (masterVolumeDbLabel.getText() != text)
            masterVolumeDbLabel.setText (text, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const bool sel = project.isTrackSelected (0);
        g.setColour (sel ? theme::panelLight().brighter (0.15f) : theme::panel());
        g.fillRect (getLocalBounds());

        // Master track color band (gold/orange).
        g.setColour (project.masterTrack != nullptr && project.masterTrack->colour != 0
                         ? ArrangementView::colourFor (*project.masterTrack)
                         : theme::accent());
        g.fillRect (0, 0, 6, getHeight());

        g.setColour (theme::border());
        g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

        const bool collapsed = project.masterTrack != nullptr && project.masterTrack->collapsed;
        if (! collapsed)
        {
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (12.5f));
            const juce::String summary = owner.deviceSummary ? owner.deviceSummary (0) : juce::String();
            g.drawText (summary.isEmpty() ? theme::ui ("No effects") : summary,
                        14, 26, getWidth() - 122, 14, juce::Justification::centredLeft, true);

            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (theme::ui ("Vol"), volumeLabelBounds, juce::Justification::centredLeft, true);
        }

        const int meterX = getWidth() - 26;
        const int meterY = collapsed ? 3 : 6;
        const int meterW = 20;
        const int meterH = std::max (1, getHeight() - meterY - (collapsed ? 3 : 6));
        const int clipBoxH = std::min (7, std::max (3, meterH / 4));
        const int labelY = meterY + clipBoxH + 1;
        const int barY = labelY + 9;
        const int barH = std::max (1, meterY + meterH - barY);
        const int leftBarX = meterX + 1, rightBarX = meterX + 11, barWidth = 7;

        // Clip indicator box
        g.setColour (clipHeld ? theme::recording() : juce::Colours::black.withAlpha (0.45f));
        g.fillRect (meterX, meterY, meterW, clipBoxH);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawRect (meterX, meterY, meterW, clipBoxH, 1);

        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("L", leftBarX, labelY, barWidth, 9, juce::Justification::centred, true);
        g.drawText ("R", rightBarX, labelY, barWidth, 9, juce::Justification::centred, true);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (leftBarX, barY, barWidth, barH);
        g.fillRect (rightBarX, barY, barWidth, barH);

        const auto levelToY = [barY, barH] (float level)
        {
            const float db = roll::gainToDecibels (level);
            return barY + barH - static_cast<int> (std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f) * barH);
        };
        const auto levelColour = [] (float level)
        {
            return roll::gainToDecibels (level) > -12.0f ? theme::warning() : theme::accent();
        };
        const int leftY = levelToY (leftPeak), rightY = levelToY (rightPeak);
        if (leftY < barY + barH)
        {
            g.setColour (levelColour (leftPeak));
            g.fillRect (leftBarX, leftY, barWidth, barY + barH - leftY);
        }
        if (rightY < barY + barH)
        {
            g.setColour (levelColour (rightPeak));
            g.fillRect (rightBarX, rightY, barWidth, barY + barH - rightY);
        }

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawHorizontalLine (levelToY (leftPeakHold), static_cast<float> (leftBarX),
                              static_cast<float> (leftBarX + barWidth));
        g.drawHorizontalLine (levelToY (rightPeakHold), static_cast<float> (rightBarX),
                              static_cast<float> (rightBarX + barWidth));
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        for (const float mark : { 0.0f, 0.5f, 0.75f, 1.0f })
        {
            const int y = barY + barH - static_cast<int> (mark * barH);
            g.drawHorizontalLine (y, static_cast<float> (meterX), static_cast<float> (meterX + meterW));
        }
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
        g.drawText (theme::ui ("Drop an effect here to add it to Master."),
                    getLocalBounds().reduced (8), juce::Justification::centred, true);
    }

    void resized() override
    {
        const bool collapsed = project.masterTrack != nullptr && project.masterTrack->collapsed;
        if (collapsed)
        {
            foldButton.setBounds (8, 2, 20, 20);
            name.setBounds (34, 2, 70, 20);
            peakDbLabel.setBounds (getWidth() - 138, 2, 108, 20);
            masterVolumeDbLabel.setBounds ({});
            volumeLabelBounds = {};
            volume.setBounds ({});
            panLabel.setVisible (false);
            pan.setVisible (false);
            panValue.setVisible (false);
            return;
        }

        panLabel.setVisible (true);
        pan.setVisible (true);
        panValue.setVisible (true);
        auto a = getLocalBounds().reduced (6, 4);
        a.removeFromLeft (8);
        a.removeFromRight (28);
        auto topRow = a.removeFromTop (22);
        foldButton.setBounds (topRow.removeFromLeft (18));
        topRow.removeFromLeft (4);
        name.setBounds (topRow.removeFromLeft (66));
        peakDbLabel.setBounds (getWidth() - 138, 26, 108, 16);
        masterVolumeDbLabel.setBounds (getWidth() - 138, 44, 108, 18);
        a.removeFromTop (14);
        auto row = a.removeFromTop (24);
        volumeLabelBounds = row.removeFromLeft (30);
        row.removeFromLeft (4);
        volume.setBounds (row.removeFromLeft (26));
        auto panRow = a.removeFromTop (26);
        panLabel.setBounds (panRow.removeFromLeft (30));
        panRow.removeFromLeft (4);
        pan.setBounds (panRow.removeFromLeft (26));
        panRow.removeFromLeft (4);
        panValue.setBounds (panRow.removeFromLeft (40));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            project.selectTrack (0);
            showTrackMenu();
            owner.grabFocus();
            return;
        }

        project.selectTrack (0);
        owner.grabFocus();

        // If the clip box was clicked, reset the clip.
        if (e.x >= getWidth() - 24 && e.y <= 16)
        {
            clipHeld = false;
            peak = 0.0f;
            peakHold = 0.0f;
            leftPeakHold = 0.0f;
            rightPeakHold = 0.0f;
            updatePeakDbText (0.0f);
            if (owner.onMasterPeakReset)
                owner.onMasterPeakReset();
            repaint();
        }
    }

private:
    void updateFoldButton()
    {
        const bool collapsed = project.masterTrack != nullptr && project.masterTrack->collapsed;
        foldButton.setButtonText (collapsed ? ">" : "v");
        foldButton.setTooltip (theme::ui (collapsed ?         "Expand Master" : "Collapse Master"));
    }

    void showTrackMenu()
    {
        juce::PopupMenu colourMenu;
        const auto* selected = project.masterTrack.get();
        colourMenu.addItem (10, theme::ui ("Automatic"), true,
                            selected != nullptr && selected->colour == 0);
        for (std::size_t i = 0; i < trackColourNames.size(); ++i)
            colourMenu.addItem (11 + static_cast<int> (i), theme::ui (trackColourNames[i]), true,
                selected != nullptr && selected->colour == trackColours[i]);

        juce::PopupMenu menu;
        menu.addSubMenu (theme::ui ("Track Color"), colourMenu, selected != nullptr);
        auto* proj = &project;
        menu.showMenuAsync ({}, [proj] (int result)
        {
            if (result == 10)
                proj->setTrackColour (0, 0);
            else if (result >= 11 && result < 11 + static_cast<int> (trackColours.size()))
                proj->setTrackColour (0, trackColours[static_cast<std::size_t> (result - 11)]);
        });
    }

    ProjectModel& project;
    ArrangementView& owner;
    juce::Label name, peakDbLabel, masterVolumeDbLabel, panLabel, panValue;
    juce::TextButton foldButton { "v" };
    MixerKnobSlider volume;
    MixerKnobSlider pan;
    juce::Rectangle<int> volumeLabelBounds;
    float peak = 0.0f, peakHold = 0.0f;
    float leftPeak = 0.0f, rightPeak = 0.0f, leftPeakHold = 0.0f, rightPeakHold = 0.0f;
    bool clipHeld = false, pluginDragOver = false;
};

class ArrangementView::Overview final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Overview (ProjectModel& p, ArrangementView& o) : project (p), owner (o)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Drag horizontally to scroll and vertically to zoom; double-click the ruler to fit the arrangement.");
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
            const auto clipColour = ArrangementView::colourFor (track);

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

            if (track.frozen)
            {
                if (track.frozenAudioClip != nullptr)
                    drawClip (track.frozenAudioClip->start, track.frozenAudioClip->lengthBeats);
            }
            else if (track.type == roll::TrackType::Audio)
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

        lastZoomY = e.position.y;
        lastPanX = e.position.x;
        zoomAnchorBeat = clickedBeat;
        zoomAnchorScreenX = clickedInsideView
            ? static_cast<int> (std::lround (clickedBeat * owner.pixelsPerBeat
                                             - owner.viewport.getViewPositionX()))
            : owner.viewport.getMaximumVisibleWidth() / 2;
        gesture = Gesture::Pending;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (gesture == Gesture::Pending)
        {
            if (e.getDistanceFromDragStart() < 4.0f)
                return;
            gesture = Gesture::PanZoom;
        }

        const float deltaY = e.position.y - lastZoomY;
        if (deltaY != 0.0f)
        {
            lastZoomY = e.position.y;
            owner.zoomAt (std::exp (static_cast<double> (deltaY) * 0.012),
                          zoomAnchorBeat, zoomAnchorScreenX);
        }

        const float deltaX = e.position.x - lastPanX;
        lastPanX = e.position.x;
        if (deltaX != 0.0f)
        {
            const double totalBeats = std::max (1.0, project.totalBeats);
            const double pixelsPerOverviewPixel = totalBeats * owner.pixelsPerBeat
                                                / std::max (1, getWidth());
            const int oldViewX = owner.viewport.getViewPositionX();
            const int newViewX = std::max (0, oldViewX + static_cast<int> (std::lround (
                deltaX * pixelsPerOverviewPixel)));
            if (newViewX != oldViewX)
            {
                owner.viewport.setViewPosition (newViewX, owner.viewport.getViewPositionY());
                zoomAnchorBeat += static_cast<double> (owner.viewport.getViewPositionX() - oldViewX)
                                / owner.pixelsPerBeat;
                owner.syncPositions();
            }
        }
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
    enum class Gesture { Pending, PanZoom };

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
    float lastPanX = 0.0f;
    float lastZoomY = 0.0f;
    int zoomAnchorScreenX = 0;
    double zoomAnchorBeat = 0.0;
    Gesture gesture = Gesture::Pending;
};

// ============================================================ ArrangementView

ArrangementView::ArrangementView (ProjectModel& p) : project (p)
{
    lastSelectedTrackId = automationTargetTrackId = project.selectedTrackId;
    lanes = std::make_unique<Lanes> (project, *this);
    ruler = std::make_unique<Ruler> (project, *this);
    timeRuler = std::make_unique<TimeRuler> (*this);
    overview = std::make_unique<Overview> (project, *this);
    addAndMakeVisible (viewport);
    addAndMakeVisible (headerHolder);
    addAndMakeVisible (rulerHolder);
    addAndMakeVisible (*overview);
    addAndMakeVisible (timeRulerHolder);
    addAndMakeVisible (timeReadout);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (addAudioTrackButton);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (locatorPreviousButton);
    addAndMakeVisible (locatorSetButton);
    addAndMakeVisible (locatorNextButton);
    addAndMakeVisible (locatorDeleteButton);
    addAndMakeVisible (locatorOverflowButton);
    addAndMakeVisible (automationModeButton);
    addAndMakeVisible (automationTargetButton);
    rulerHolder.addAndMakeVisible (*ruler);

    viewport.setViewedComponent (lanes.get(), false);
    viewport.setScrollBarsShown (true, false);

    addTrackButton.setMouseClickGrabsKeyboardFocus (false);
    addAudioTrackButton.setMouseClickGrabsKeyboardFocus (false);
    addTrackButton.setTooltip ("Add MIDI Track (Ctrl+Shift+T)");
    addAudioTrackButton.setTooltip ("Add Audio Track (Ctrl+T)");
    gridBox.setMouseClickGrabsKeyboardFocus (false);
    locatorPreviousButton.setMouseClickGrabsKeyboardFocus (false);
    locatorSetButton.setMouseClickGrabsKeyboardFocus (false);
    locatorNextButton.setMouseClickGrabsKeyboardFocus (false);
    locatorDeleteButton.setMouseClickGrabsKeyboardFocus (false);
    locatorOverflowButton.setMouseClickGrabsKeyboardFocus (false);
    automationModeButton.setClickingTogglesState (true);
    automationModeButton.setMouseClickGrabsKeyboardFocus (false);
    automationTargetButton.setMouseClickGrabsKeyboardFocus (false);
    automationStatusLabel.setColour (juce::Label::backgroundColourId, theme::panelLight().darker (0.18f));
    automationStatusLabel.setColour (juce::Label::textColourId, theme::text());
    automationStatusLabel.setFont (juce::FontOptions (10.5f));
    automationStatusLabel.setJustificationType (juce::Justification::centredLeft);
    automationStatusLabel.setMinimumHorizontalScale (0.7f);
    automationStatusLabel.setVisible (false);
    addAndMakeVisible (automationStatusLabel);
    locatorPreviousButton.setTooltip ("Go to Previous Locator");
    locatorSetButton.setTooltip ("Add a locator at the playhead (Ctrl+Shift+M)");
    locatorNextButton.setTooltip ("Go to Next Locator");
    locatorDeleteButton.setTooltip ("Remove the selected locator");
    locatorOverflowButton.setTooltip ("Locators e automacao");
    automationModeButton.setTooltip ("Toggle Automation Mode (A)");
    automationTargetButton.setTooltip ("Select a parameter to view and edit");
    locatorPreviousButton.onClick = [this] { navigateLocator (false); };
    locatorSetButton.onClick = [this] { addLocatorAtPlayhead(); };
    locatorNextButton.onClick = [this] { navigateLocator (true); };
    locatorDeleteButton.onClick = [this] { deleteSelectedLocator(); };
    locatorOverflowButton.onClick = [this] { showLocatorAutomationMenu(); };
    automationModeButton.onClick = [this]
    {
        setAutomationMode (automationModeButton.getToggleState());
    };
    automationTargetButton.onClick = [this] { showAutomationTargetMenu(); };
    timeRulerHolder.addAndMakeVisible (*timeRuler);
    timeReadout.setColour (juce::Label::backgroundColourId, theme::panelLight());
    timeReadout.setColour (juce::Label::textColourId, theme::text());
    timeReadout.setFont (juce::FontOptions (11.5f));
    timeReadout.setJustificationType (juce::Justification::centred);
    timeReadout.setTooltip ("Current project position");
    addTrackButton.onClick = [this] { project.addTrack(); };
    addAudioTrackButton.onClick = [this] { project.addTrack (roll::TrackType::Audio); };
    static constexpr double gridValues[] { 1.0, 0.5, 0.25, 0.125, 0.0625,
                                           2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                           1.0 / 12.0, 4.0 };
    static constexpr const char* gridLabels[] { "1/4", "1/8", "1/16", "1/32", "1/64",
                                                "1/4T", "1/8T", "1/16T", "1/32T", "1 bar" };
    for (int i = 0; i < 10; ++i)
        gridBox.addItem (gridLabels[i], i + 1);
    gridBox.addItem ("No Grid", 11);
    gridBox.addItem ("Adaptive", 12);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.setTooltip ("Ctrl+1 Narrow (finer grid), Ctrl+2 Widen (coarser grid), "
                        "Ctrl+4 toggles snap and Ctrl+5 toggles the adaptive grid");
    gridBox.onChange = [this]
    {
        const int id = gridBox.getSelectedId();
        if (id >= 1 && id <= 10)
            setGridBeat (gridValues[id - 1]);
        else if (id == 11)
            setGridBeat (0.0);
        else if (id == 12)
            setAdaptiveGrid (true);
    };

    project.addChangeListener (this);
    setAdaptiveGrid (true);
    refresh();
    updateLocatorControls();
    updateAutomationTargetButton();
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
    if (adaptiveGridEnabled)
        updateAdaptiveGrid();
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

int ArrangementView::rowTop (int row) const
{
    const int rowCount = static_cast<int> (project.tracks.size()) + 1;
    const int endRow = std::clamp (row, 0, rowCount);
    int top = 0;
    for (int i = 0; i < endRow; ++i)
        top += rowHeight (i);
    return top;
}

int ArrangementView::rowHeight (int row) const
{
    if (row >= 0 && row < static_cast<int> (project.tracks.size()))
    {
        const auto* track = project.tracks[static_cast<std::size_t> (row)].get();
        if (track->parentGroupId != 0)
        {
            const auto* parent = project.track (track->parentGroupId);
            if (parent != nullptr && parent->type == roll::TrackType::Group && parent->collapsed)
                return 0;
        }
        if (track->collapsed)
            return collapsedTrackHeight;
    }
    if (row == static_cast<int> (project.tracks.size())
        && project.masterTrack != nullptr && project.masterTrack->collapsed)
        return collapsedTrackHeight;
    return trackHeight;
}

int ArrangementView::rowAt (int y) const
{
    if (y < 0)
        return -1;

    const int rowCount = static_cast<int> (project.tracks.size()) + 1;
    for (int row = 0; row < rowCount; ++row)
    {
        const int top = rowTop (row);
        if (y < top + rowHeight (row))
            return row;
    }
    return -1;
}

int ArrangementView::rowInsertionIndexAt (int y) const
{
    const int trackCount = static_cast<int> (project.tracks.size());
    for (int row = 0; row < trackCount; ++row)
        if (y < rowTop (row) + rowHeight (row) / 2)
            return row;
    return trackCount;
}

void ArrangementView::toggleTrackCollapsed (int trackId)
{
    if (auto* track = project.track (trackId))
    {
        project.beginEdit();
        track->collapsed = ! track->collapsed;
        project.endEdit();
        updateSizes();
        lanes->repaint();
        headerHolder.repaint();
        project.sendChangeMessage();
    }
}

void ArrangementView::selectTrackFromHeader (int trackId, juce::ModifierKeys modifiers)
{
    const bool additive = modifiers.isCtrlDown() || modifiers.isCommandDown();
    if (modifiers.isShiftDown())
    {
        int anchor = project.track (trackSelectionAnchorId) != nullptr
            ? trackSelectionAnchorId : project.selectedTrackId;
        if (project.track (anchor) == nullptr)
            anchor = trackId;
        project.selectTrackRange (anchor, trackId, additive);
        if (project.track (trackSelectionAnchorId) == nullptr)
            trackSelectionAnchorId = anchor;
        return;
    }

    project.selectTrack (trackId, additive);
    trackSelectionAnchorId = trackId;
}

void ArrangementView::setAdaptiveGrid (bool enabled)
{
    adaptiveGridEnabled = enabled;
    if (adaptiveGridEnabled)
    {
        updateAdaptiveGrid();
        return;
    }

    updateGridSelection();
    lanes->repaint();
}

void ArrangementView::toggleAutomationMode()
{
    setAutomationMode (! automationMode);
}

void ArrangementView::toggleAutomationModeFromKey()
{
    if (automationModeKeyDown)
        return;
    automationModeKeyDown = true;
    toggleAutomationMode();
}

void ArrangementView::setAutomationMode (bool enabled)
{
    automationMode = enabled;
    automationModeButton.setToggleState (enabled, juce::dontSendNotification);
    automationModeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    automationModeButton.setColour (juce::TextButton::textColourOnId, theme::text());
    if (enabled && automationTarget.type == roll::AutomationParameterType::PluginParameter)
    {
        selectPluginAutomationParameter (
            automationTargetTrackId >= 0 ? automationTargetTrackId : project.selectedTrackId,
            automationTarget);
        return;
    }
    updateAutomationStatus();
    lanes->repaint();
}

void ArrangementView::selectMixerAutomationParameter (
    int trackId, roll::AutomationParameterType type)
{
    if (! automationMode || type == roll::AutomationParameterType::PluginParameter)
        return;

    const auto* track = trackId == 0 ? project.masterTrack.get() : project.track (trackId);
    if (track == nullptr)
        return;

    roll::AutomationTarget target;
    target.type = type;
    switch (type)
    {
        case roll::AutomationParameterType::Volume: target.currentValue = track->volume; break;
        case roll::AutomationParameterType::Pan: target.currentValue = track->pan; break;
        case roll::AutomationParameterType::Mute: target.currentValue = track->mute ? 1.0 : 0.0; break;
        case roll::AutomationParameterType::PluginParameter: return;
    }

    const bool targetChanged = automationTargetTrackId != trackId || !(automationTarget == target);
    automationTargetTrackId = trackId;
    automationTarget = target;

    bool envelopeCreated = false;
    const auto existing = std::find_if (track->automation.begin(), track->automation.end(),
        [&target] (const auto& envelope) { return envelope.target == target; });
    if (existing == track->automation.end())
    {
        roll::AutomationEnvelope envelope;
        envelope.target = target;
        envelope.points.push_back ({ std::max (0.0, playhead), target.currentValue });
        envelopeCreated = project.setAutomationEnvelope (trackId, std::move (envelope));
        if (! envelopeCreated)
            juce::Logger::writeToLog (
                "ArrangementView: Could not initialize mixer automation.");
    }

    if (targetChanged || envelopeCreated)
    {
        updateAutomationTargetButton();
        updateAutomationStatus();
        lanes->repaint();
    }
}

void ArrangementView::selectAudioClipAutomationParameter (
    int trackId, int clipId, roll::AutomationParameterType type, double value)
{
    roll::AutomationTarget target;
    target.type = type;
    target.clipId = clipId;
    target.currentValue = value;
    if (trackId != 0)
        project.selectTrack (trackId);
    automationTargetTrackId = trackId;
    automationTarget = target;
    if (automationMode)
    {
        const auto* track = trackId == 0 ? project.masterTrack.get() : project.track (trackId);
        const auto* clip = track != nullptr ? track->findAudioClip (clipId) : nullptr;
        if (clip != nullptr)
        {
            const auto existing = std::find_if (track->automation.begin(), track->automation.end(),
                [this] (const auto& envelope) { return envelope.target == automationTarget; });
            roll::AutomationEnvelope envelope;
            if (existing != track->automation.end())
                envelope = *existing;
            else
            {
                envelope.target = automationTarget;
                envelope.points = { { clip->start, value }, { clip->end(), value } };
            }

            const double beat = std::clamp (playhead, clip->start, clip->end());
            const auto point = std::lower_bound (envelope.points.begin(), envelope.points.end(), beat,
                [] (const roll::AutomationPoint& candidate, double position)
                {
                    return candidate.beat < position;
                });
            if (point != envelope.points.end() && std::abs (point->beat - beat) < 1.0e-9)
                point->value = value;
            else
                envelope.points.insert (point, { beat, value });
            project.setAutomationEnvelope (trackId, std::move (envelope));
        }
    }
    updateAutomationTargetButton();
    updateAutomationStatus();
    lanes->repaint();
}

void ArrangementView::selectPluginAutomationParameter (
    int trackId, const roll::AutomationTarget& target)
{
    if (target.type != roll::AutomationParameterType::PluginParameter)
        return;
    if (trackId != 0)
        project.selectTrack (trackId);
    automationTargetTrackId = trackId;
    automationTarget = target;
    updateAutomationTargetButton();
    updateAutomationStatus();
    lanes->repaint();
}

void ArrangementView::showAutomationTargetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (theme::ui ("Mixer"));
    std::vector<AutomationParameterChoice> choices;
    const int trackId = automationTargetTrackId >= 0
        ? automationTargetTrackId : project.selectedTrackId;
    const auto* track = trackId == 0 ? project.masterTrack.get() : project.track (trackId);
    const auto addMixerChoice = [&] (int id, roll::AutomationParameterType type,
                                     const juce::String& label, double value)
    {
        AutomationParameterChoice choice;
        choice.target.type = type;
        choice.target.currentValue = value;
        choice.label = label;
        const bool selected = automationTarget == choice.target;
        menu.addItem (id, label, true, selected);
        choices.push_back (std::move (choice));
    };
    addMixerChoice (1, roll::AutomationParameterType::Volume,
                    theme::ui ("Volume"), track != nullptr ? track->volume : 1.0);
    addMixerChoice (2, roll::AutomationParameterType::Pan,
                    theme::ui ("Pan"), track != nullptr ? track->pan : 0.0);
    addMixerChoice (3, roll::AutomationParameterType::Mute,
                    theme::ui ("Mute"), track != nullptr && track->mute ? 1.0 : 0.0);

    std::vector<std::pair<int, roll::AutomationTarget>> targets;
    if (track != nullptr && automationParametersProvider)
    {
        menu.addSeparator();
        menu.addSectionHeader (theme::ui ("Plugins and Effects"));
        int nextId = 100;
        for (auto& choice : automationParametersProvider (trackId))
        {
            const auto existing = std::find_if (targets.begin(), targets.end(),
                [&choice] (const auto& current) { return current.second == choice.target; });
            if (existing != targets.end())
                continue;
            const int id = nextId++;
            menu.addItem (id, choice.label,
                          true, automationTarget == choice.target);
            targets.emplace_back (id, choice.target);
        }
    }

    juce::Component::SafePointer<ArrangementView> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automationTargetButton),
        [safeThis, trackId, choices = std::move (choices),
         targets = std::move (targets)] (int result)
        {
            if (safeThis == nullptr)
                return;
            auto selected = std::find_if (choices.begin(), choices.end(),
                [result] (const auto& choice)
                {
                    if (result == 1) return choice.target.type == roll::AutomationParameterType::Volume;
                    if (result == 2) return choice.target.type == roll::AutomationParameterType::Pan;
                    if (result == 3) return choice.target.type == roll::AutomationParameterType::Mute;
                    return false;
                });
            if (selected != choices.end())
            {
                safeThis->automationTargetTrackId = safeThis->project.selectedTrackId;
                safeThis->automationTarget = selected->target;
            }
            else
            {
                const auto pluginTarget = std::find_if (targets.begin(), targets.end(),
                    [result] (const auto& target) { return target.first == result; });
                if (pluginTarget == targets.end())
                    return;
                safeThis->selectPluginAutomationParameter (trackId, pluginTarget->second);
                return;
            }
            safeThis->updateAutomationTargetButton();
            safeThis->updateAutomationStatus();
            safeThis->lanes->repaint();
        });
}

void ArrangementView::updateAutomationTargetButton()
{
    juce::String label;
    switch (automationTarget.type)
    {
        case roll::AutomationParameterType::Volume: label = theme::ui ("Volume"); break;
        case roll::AutomationParameterType::Pan: label = theme::ui ("Pan"); break;
        case roll::AutomationParameterType::Mute: label = theme::ui ("Mute"); break;
        case roll::AutomationParameterType::PluginParameter:
            label = juce::String::fromUTF8 (automationTarget.parameterName.c_str());
            if (label.isEmpty())
                label = theme::ui ("Parametro");
            break;
    }
    automationTargetButton.setButtonText (label);
    automationTargetButton.setTooltip (
        "Active parameter. Adjusting a plugin control selects it automatically; "
        "click here to choose manually.");
}

void ArrangementView::updateAutomationStatus()
{
    automationStatusLabel.setVisible (automationMode && automationStatusLabel.getWidth() > 80);
    if (! automationMode)
        return;

    const int trackId = automationTargetTrackId >= 0
        ? automationTargetTrackId : project.selectedTrackId;
    const auto* track = trackId == 0 ? project.masterTrack.get() : project.track (trackId);
    const auto choices = track != nullptr && automationParametersProvider
        ? automationParametersProvider (trackId)
        : std::vector<AutomationParameterChoice>();
    juce::String status = "A";

    juce::String targetName;
    double value = automationTarget.currentValue;
    if (automationTarget.type == roll::AutomationParameterType::PluginParameter)
    {
        targetName = juce::String::fromUTF8 (automationTarget.parameterName.c_str());
        const auto choice = std::find_if (choices.begin(), choices.end(),
            [this] (const auto& item) { return item.target == automationTarget; });
        if (choice != choices.end())
            value = choice->target.currentValue;
    }
    else if (track != nullptr)
    {
        switch (automationTarget.type)
        {
            case roll::AutomationParameterType::Volume:
                targetName = "Volume";
                value = track->volume;
                break;
            case roll::AutomationParameterType::Pan:
                targetName = "Pan";
                value = track->pan;
                break;
            case roll::AutomationParameterType::Mute:
                targetName = "Mute";
                value = track->mute ? 1.0 : 0.0;
                break;
            case roll::AutomationParameterType::PluginParameter:
                break;
        }
    }

    const roll::AutomationEnvelope* activeEnvelope = nullptr;
    if (track != nullptr)
        if (const auto envelope = std::find_if (track->automation.begin(), track->automation.end(),
                [this] (const auto& item) { return item.target == automationTarget; });
            envelope != track->automation.end())
            activeEnvelope = &*envelope;
    if (activeEnvelope != nullptr && automationTarget.type != roll::AutomationParameterType::PluginParameter
        && playbackStateProvider && playbackStateProvider())
    {
        value = roll::automationValueAt (*activeEnvelope, playhead, value);
    }

    juce::String valueText;
    switch (automationTarget.type)
    {
        case roll::AutomationParameterType::Volume:
            valueText = juce::String (
                roll::gainToDecibels (static_cast<float> (std::max (0.0, value)), -100.0f), 1)
                + " dB";
            break;
        case roll::AutomationParameterType::Pan:
            valueText = value < -0.01 ? juce::String ("L ") + juce::String (-value * 100.0, 0) + "%"
                : value > 0.01 ? juce::String ("R ") + juce::String (value * 100.0, 0) + "%"
                               : "Center";
            break;
        case roll::AutomationParameterType::Mute:
            valueText = value >= 0.5 ? "On" : "Off";
            break;
        case roll::AutomationParameterType::PluginParameter:
            if (automationParameterValueTextProvider)
                valueText = automationParameterValueTextProvider (trackId, automationTarget, value);
            if (valueText.isEmpty())
                valueText = juce::String (value * 100.0, 1) + "%";
            break;
    }
    if (targetName.isEmpty())
        targetName = "Parameter";
    status += " | " + targetName + (activeEnvelope != nullptr ? " [Automated]: " : " [Available]: ")
            + valueText;
    automationStatusLabel.setText (status, juce::dontSendNotification);
    automationStatusLabel.setTooltip (status);
}

void ArrangementView::updateAdaptiveGrid()
{
    const double previousGrid = gridBeat;
    gridBeat = roll::adaptiveGridBeat (pixelsPerBeat);
    lastGridBeat = gridBeat;
    updateGridSelection();
    if (std::abs (previousGrid - gridBeat) > 1.0e-9)
    {
        lanes->repaint();
        if (onGridChanged)
            onGridChanged();
    }
}

void ArrangementView::updateGridSelection()
{
    if (adaptiveGridEnabled)
    {
        gridBox.setSelectedId (12, juce::dontSendNotification);
        return;
    }

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
}

void ArrangementView::setGridBeat (double beats)
{
    adaptiveGridEnabled = false;
    gridBeat = std::max (0.0, beats);
    if (gridBeat > 0.0)
        lastGridBeat = gridBeat;
    updateGridSelection();
    lanes->repaint();
    if (onGridChanged) onGridChanged();
}

void ArrangementView::setPlayhead (double beats)
{
    const int oldX = static_cast<int> (playhead * pixelsPerBeat);
    lanes->setPlayhead (beats);
    ruler->repaint (oldX - 6, 0, 14, rulerHeight);
    ruler->repaint (static_cast<int> (beats * pixelsPerBeat) - 6, 0, 14, rulerHeight);
    timeReadout.setText (formatTimelineTime (playhead * 60.0 / bpm),
                         juce::dontSendNotification);
    timeRuler->repaint (oldX - 2, 0, 5, timeRulerHeight);
    timeRuler->repaint (static_cast<int> (beats * pixelsPerBeat) - 2, 0, 5, timeRulerHeight);
    overview->repaint();
    updateAutomationStatus();
}

void ArrangementView::scrollToBeat (double beats)
{
    const int beatX = static_cast<int> (std::lround (std::max (0.0, beats) * pixelsPerBeat));
    const int viewX = viewport.getViewPositionX();
    const int visibleWidth = viewport.getMaximumVisibleWidth();
    constexpr int margin = 24;
    int targetX = viewX;

    if (beatX < viewX)
        targetX = std::max (0, beatX - margin);
    else if (beatX >= viewX + visibleWidth)
        targetX = beatX - visibleWidth + margin;

    if (targetX != viewX)
        viewport.setViewPosition (targetX, viewport.getViewPositionY());
}

void ArrangementView::setBpm (double beatsPerMinute)
{
    if (! std::isfinite (beatsPerMinute) || beatsPerMinute <= 0.0)
        return;

    bpm = beatsPerMinute;
    timeReadout.setText (formatTimelineTime (playhead * 60.0 / bpm),
                         juce::dontSendNotification);
    timeRuler->repaint();
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

bool ArrangementView::hasSelectionToDuplicate() const
{
    return lanes != nullptr && lanes->hasSelectionToDuplicate();
}

void ArrangementView::duplicateSelection()
{
    if (lanes != nullptr)
        lanes->duplicateSelection();
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
    else
    {
        masterHeader->refreshMidiMappingInfo();
    }
    updateSizes();
    resized();
}

void ArrangementView::refreshWaveformAppearance()
{
    if (lanes != nullptr)
        lanes->repaint();
}

void ArrangementView::setAudioDataProvider (
    std::function<const AudioFileData* (const std::string&)> provider)
{
    audioDataProvider = std::move (provider);
    refreshWaveformAppearance();
}

void ArrangementView::refreshMidiMappingInfo()
{
    for (auto& header : headers)
        header->refreshMidiMappingInfo();
    if (masterHeader != nullptr)
        masterHeader->refreshMidiMappingInfo();
}

void ArrangementView::updateSizes()
{
    lanes->updateSize();
    ruler->setSize (std::max (1, lanes->getWidth()), rulerHeight);
    timeRuler->setSize (std::max (1, lanes->getWidth()), timeRulerHeight);
    timeRuler->repaint();
    setBpm (bpm);
    syncPositions();
}

void ArrangementView::syncPositions()
{
    const auto vp = viewport.getViewPosition();
    ruler->setTopLeftPosition (-vp.x, 0);
    timeRuler->setTopLeftPosition (-vp.x, 0);
    overview->repaint();
    headerHolder.setSize (headerWidth, viewport.getMaximumVisibleHeight());
    for (size_t i = 0; i < headers.size(); ++i)
    {
        const int height = rowHeight (static_cast<int> (i));
        headers[i]->setVisible (height > 0);
        headers[i]->setBounds (0, rowTop (static_cast<int> (i)) - vp.y,
                               headerWidth, height);
    }
    if (masterHeader != nullptr)
    {
        const int height = rowHeight (static_cast<int> (headers.size()));
        masterHeader->setVisible (height > 0);
        masterHeader->setBounds (0, rowTop (static_cast<int> (headers.size())) - vp.y,
                                 headerWidth, height);
    }
}

void ArrangementView::updateLocatorControls()
{
    const bool anyLocators = ! project.locators.empty();
    locatorPreviousButton.setEnabled (anyLocators);
    locatorNextButton.setEnabled (anyLocators);
    locatorDeleteButton.setEnabled (anyLocators);
}

void ArrangementView::addLocatorAtPlayhead()
{
    project.addLocator (snapBeat (playhead));
}

void ArrangementView::deleteSelectedLocator()
{
    int locatorId = project.locator (project.selectedLocatorId) != nullptr
        ? project.selectedLocatorId : -1;
    if (locatorId < 0 && ! project.locators.empty())
        locatorId = std::max_element (project.locators.begin(), project.locators.end(),
            [] (const auto& a, const auto& b) { return a.id < b.id; })->id;
    if (locatorId >= 0)
        project.removeLocator (locatorId);
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
    if (project.selectedTrackId != lastSelectedTrackId)
    {
        lastSelectedTrackId = project.selectedTrackId;
        automationTargetTrackId = lastSelectedTrackId;
    }

    bool orderChanged = headers.size() != project.tracks.size();
    for (size_t i = 0; ! orderChanged && i < headers.size(); ++i)
        orderChanged = headers[i]->id() != project.tracks[i]->id;

    if (orderChanged) { refresh(); }
    else
    {
        updateSizes();
        for (auto& h : headers) h->refreshText();
        if (masterHeader != nullptr) masterHeader->refreshText();
    }
    updateLocatorControls();
    updateAutomationTargetButton();
    updateAutomationStatus();
    lanes->repaint();
    ruler->repaint();
}

void ArrangementView::timerCallback()
{
    if (! juce::KeyPress::isKeyCurrentlyDown ('A'))
        automationModeKeyDown = false;

    const auto vp = viewport.getViewPosition();
    static juce::Point<int> last;
    if (vp != last) { last = vp; syncPositions(); }

    if (meterProvider)
    {
        for (auto& h : headers) h->setPeak (meterProvider (h->id()));
        if (masterHeader != nullptr) masterHeader->setPeak (meterProvider (0));
    }
    if (masterStereoPeakProvider && masterHeader != nullptr)
    {
        const auto [left, right] = masterStereoPeakProvider();
        masterHeader->setStereoPeaks (left, right);
    }
    if (liveMixerProvider)
    {
        for (auto& h : headers)
            h->refreshLiveMixerValues (liveMixerProvider (h->id()));
        if (masterHeader != nullptr)
            masterHeader->refreshLiveMixerValues (liveMixerProvider (0));
    }
    updateAutomationStatus();
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
    const int buttonY = top.getY() + 4;
    const int buttonHeight = rulerHeight - 8;
    locatorOverflowButton.setVisible (false);
    locatorDeleteButton.setVisible (true);
    locatorNextButton.setVisible (true);
    locatorSetButton.setVisible (true);
    locatorPreviousButton.setVisible (true);
    automationTargetButton.setVisible (true);
    automationModeButton.setVisible (true);

    const bool compactToolbar = top.getWidth() - headerWidth < 300;
    if (compactToolbar)
    {
        locatorDeleteButton.setBounds (right - 30, buttonY, 28, buttonHeight);
        right -= 32;
        locatorNextButton.setBounds (right - 20, buttonY, 20, buttonHeight);
        right -= 24;
        locatorSetButton.setBounds (right - 20, buttonY, 20, buttonHeight);
        right -= 24;
        locatorPreviousButton.setBounds (right - 20, buttonY, 20, buttonHeight);
        right -= 28;
        automationTargetButton.setBounds (right - 76, buttonY, 76, buttonHeight);
        right -= 80;
        automationModeButton.setBounds (right - 24, buttonY, 24, buttonHeight);
        automationStatusLabel.setBounds ({});
    }
    else
    {
        locatorDeleteButton.setBounds (right - 36, buttonY, 34, buttonHeight);
        right -= 38;
        locatorNextButton.setBounds (right - 26, buttonY, 24, buttonHeight);
        right -= 28;
        locatorSetButton.setBounds (right - 26, buttonY, 24, buttonHeight);
        right -= 28;
        locatorPreviousButton.setBounds (right - 26, buttonY, 24, buttonHeight);
        right -= 40;
        automationTargetButton.setBounds (right - 110, buttonY, 108, buttonHeight);
        right -= 120;
        automationModeButton.setBounds (right - 28, buttonY, 26, buttonHeight);
        automationStatusLabel.setBounds (headerWidth + 4, buttonY,
            std::max (0, automationModeButton.getX() - headerWidth - 8), buttonHeight);
    }
    updateAutomationStatus();
    auto timeArea = area.removeFromBottom (timeRulerHeight);
    auto left = area.removeFromLeft (headerWidth);
    timeArea.removeFromLeft (headerWidth);
    viewport.setBounds (area);
    headerHolder.setBounds (left.withHeight (viewport.getMaximumVisibleHeight()));
    timeReadout.setBounds (left.getX(), timeArea.getY(), headerWidth, timeRulerHeight);
    timeRulerHolder.setBounds (timeArea);
    updateSizes();
}

void ArrangementView::showLocatorAutomationMenu()
{
    juce::PopupMenu menu;
    const bool hasLocators = ! project.locators.empty();
    menu.addItem (1, "Go to Previous Locator", hasLocators);
    menu.addItem (2, "Add Locator at Playhead");
    menu.addItem (3, "Go to Next Locator", hasLocators);
    menu.addItem (4, "Remove Selected Locator", hasLocators);
    menu.addSeparator();
    menu.addItem (5, "Automation Mode", true, automationMode);
    menu.addItem (6, "Select Automation Parameter");

    juce::Component::SafePointer<ArrangementView> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&locatorOverflowButton),
        [safeThis] (int result)
        {
            if (safeThis == nullptr)
                return;
            switch (result)
            {
                case 1: safeThis->navigateLocator (false); break;
                case 2: safeThis->addLocatorAtPlayhead(); break;
                case 3: safeThis->navigateLocator (true); break;
                case 4: safeThis->deleteSelectedLocator(); break;
                case 5: safeThis->setAutomationMode (! safeThis->automationMode); break;
                case 6: safeThis->showAutomationTargetMenu(); break;
                default: break;
            }
        });
}
