#include "AudioClipEditor.h"
#include "WaveformDisplay.h"
#include "ZoomCursor.h"
#include <algorithm>
#include <cmath>

AudioClipEditor::AudioClipEditor()
{
    addAndMakeVisible (fileName);
    addAndMakeVisible (tempoInfo);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (sourceBpm);
    addAndMakeVisible (warpButton);
    addAndMakeVisible (reverseButton);
    addAndMakeVisible (warpModeBox);
    addAndMakeVisible (pitchSlider);
    addAndMakeVisible (centsSlider);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (doubleBpmButton);
    addAndMakeVisible (halveBpmButton);
    addAndMakeVisible (splitButton);
    addAndMakeVisible (joinButton);
    addAndMakeVisible (fadeInLabel);
    addAndMakeVisible (fadeOutLabel);
    addAndMakeVisible (fadeIn);
    addAndMakeVisible (fadeOut);
    addAndMakeVisible (closeButton);

    fileName.setColour (juce::Label::textColourId, theme::text());
    fileName.setFont (theme::fontOptions (14.5f, juce::Font::bold));
    tempoInfo.setColour (juce::Label::textColourId, theme::textDim());
    bpmLabel.setColour (juce::Label::textColourId, theme::textDim());
    sourceBpm.setRange (20.0, 300.0, 0.1);
    sourceBpm.setDoubleClickReturnValue (true, 120.0);
    sourceBpm.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sourceBpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 74, 24);
    sourceBpm.setName ("BPM");
    sourceBpm.setTooltip ("Source BPM: enter a value in the field or adjust it with the knob.");
    sourceBpm.textFromValueFunction = [] (double value)
    {
        return juce::String (value, 1) + " BPM";
    };
    sourceBpm.valueFromTextFunction = [] (const juce::String& text)
    {
        return text.trim().upToFirstOccurrenceOf (" ", false, false)
                   .replaceCharacter (',', '.').getDoubleValue();
    };
    sourceBpm.setColour (juce::Slider::textBoxTextColourId, theme::text());
    sourceBpm.setColour (juce::Slider::textBoxBackgroundColourId, theme::panelLight());
    sourceBpm.setColour (juce::Slider::textBoxOutlineColourId, theme::border());
    closeButton.setMouseClickGrabsKeyboardFocus (false);
    splitButton.setMouseClickGrabsKeyboardFocus (false);
    joinButton.setMouseClickGrabsKeyboardFocus (false);
    reverseButton.setMouseClickGrabsKeyboardFocus (false);
    doubleBpmButton.setMouseClickGrabsKeyboardFocus (false);
    halveBpmButton.setMouseClickGrabsKeyboardFocus (false);
    doubleBpmButton.setTooltip ("Double the original BPM");
    halveBpmButton.setTooltip ("Halve the original BPM");
    doubleBpmButton.onClick = [this]
    {
        if (clip == nullptr)
            return;
        const double bpm = std::clamp (clip->sourceBpm * 2.0, 20.0, 300.0);
        sourceBpm.setValue (bpm, juce::sendNotification);
    };
    halveBpmButton.onClick = [this]
    {
        if (clip == nullptr)
            return;
        const double bpm = std::clamp (clip->sourceBpm * 0.5, 20.0, 300.0);
        sourceBpm.setValue (bpm, juce::sendNotification);
    };
    warpButton.setTooltip ("Sync this clip to the project BPM; changing speed also changes pitch.");
    warpButton.onClick = [this]
    {
        if (clip != nullptr && onWarpChanged)
            onWarpChanged (warpButton.getToggleState());
    };
    warpModeBox.addItem ("Beats", 1);
    warpModeBox.addItem ("Tones", 2);
    warpModeBox.addItem ("Texture", 3);
    warpModeBox.addItem ("Re-Pitch", 4);
    warpModeBox.addItem ("Complex", 5);
    warpModeBox.addItem ("Complex Pro", 6);
    warpModeBox.setSelectedId (1, juce::dontSendNotification);
    warpModeBox.setTooltip ("Clip Warp Mode");
    warpModeBox.onChange = [this]
    {
        applyPlaybackProperties();
    };
    pitchSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pitchSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.setRange (-24.0, 24.0, 1.0);
    pitchSlider.setName ("Semitones");
    pitchSlider.textFromValueFunction = [] (double value)
    {
        return (value > 0.0 ? juce::String ("+") : juce::String())
             + juce::String (value, 0) + " semitones";
    };
    pitchSlider.setTooltip ("Transpose in semitones");
    pitchSlider.bipolar = true;
    centsSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    centsSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    centsSlider.setRange (-100.0, 100.0, 1.0);
    centsSlider.setName ("Cents");
    centsSlider.textFromValueFunction = [] (double value)
    {
        return (value > 0.0 ? juce::String ("+") : juce::String())
             + juce::String (value, 0) + " cents";
    };
    centsSlider.setTooltip ("Fine transpose in cents");
    centsSlider.bipolar = true;
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setName ("Gain");
    gainSlider.textFromValueFunction = [] (double value)
    {
        return (value > 0.0 ? juce::String ("+") : juce::String())
             + juce::String (value, 1) + " dB";
    };
    gainSlider.setTooltip ("Clip Gain in dB");
    gainSlider.bipolar = true;
    for (auto* slider : std::initializer_list<juce::Slider*> {
             &pitchSlider, &centsSlider, &gainSlider })
    {
        slider->setColour (juce::Slider::thumbColourId, theme::accent());
        slider->setColour (juce::Slider::trackColourId, theme::border());
        slider->setColour (juce::Slider::textBoxTextColourId, theme::text());
        slider->setColour (juce::Slider::textBoxBackgroundColourId, theme::panelLight());
        slider->setMouseClickGrabsKeyboardFocus (false);
        slider->setPopupDisplayEnabled (true, true, nullptr);
        slider->setDoubleClickReturnValue (true, 0.0);
        slider->onValueChange = [this] { applyPlaybackProperties(); };
        slider->onDragStart = [this]
        {
            if (onAutomationGestureStarted)
                onAutomationGestureStarted();
        };
        slider->onDragEnd = [this]
        {
            if (onAutomationGestureEnded)
                onAutomationGestureEnded();
        };
    }
    pitchSlider.onValueChange = [this]
    {
        applyPlaybackProperties();
        if (onAutomationParameterChanged)
            onAutomationParameterChanged ("Pitch",
                pitchSlider.getValue() + centsSlider.getValue() / 100.0);
    };
    centsSlider.onValueChange = [this]
    {
        applyPlaybackProperties();
        if (onAutomationParameterChanged)
            onAutomationParameterChanged ("Pitch",
                pitchSlider.getValue() + centsSlider.getValue() / 100.0);
    };
    gainSlider.onValueChange = [this]
    {
        applyPlaybackProperties();
        if (onAutomationParameterChanged)
            onAutomationParameterChanged ("Gain", gainSlider.getValue());
    };
    reverseButton.setTooltip ("Play audio in reverse");
    reverseButton.onClick = [this] { applyPlaybackProperties(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    sourceBpm.onValueChange = [this]
    {
        if (clip == nullptr || ! onSourceBpmChanged)
            return;
        const double value = sourceBpm.getValue();
        if (! onSourceBpmChanged (value))
        {
            sourceBpm.setValue (clip->sourceBpm, juce::dontSendNotification);
            tempoInfo.setText ("BPM not applied: the clips overlap.", juce::dontSendNotification);
        }
    };
    splitButton.setTooltip ("Split the selected clip at the playhead");
    splitButton.onClick = [this]
    {
        if (clip != nullptr && onSplitAtPlayhead)
            onSplitAtPlayhead();
    };
    joinButton.setTooltip ("Merge this clip with the adjacent segment created by splitting");
    joinButton.onClick = [this]
    {
        if (clip != nullptr && onJoinWithNext)
            onJoinWithNext();
    };

    for (auto* label : std::initializer_list<juce::Label*> {
             &fadeInLabel, &fadeOutLabel })
    {
        label->setColour (juce::Label::textColourId, theme::textDim());
        label->setFont (theme::fontOptions (11.0f));
    }

    const auto configureSlider = [] (AudioClipKnobSlider& slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setNumDecimalPlacesToDisplay (2);
        slider.setColour (juce::Slider::thumbColourId, theme::accent());
        slider.setColour (juce::Slider::trackColourId, theme::border());
        slider.setColour (juce::Slider::textBoxTextColourId, theme::text());
        slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::panelLight());
        slider.setMouseClickGrabsKeyboardFocus (false);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setPopupDisplayEnabled (true, true, nullptr);
    };
    for (auto* slider : std::initializer_list<AudioClipKnobSlider*> {
             &fadeIn, &fadeOut })
    {
        configureSlider (*slider);
        slider->onDragStart = [this]
        {
            if (onAutomationGestureStarted)
                onAutomationGestureStarted();
        };
        slider->onDragEnd = [this]
        {
            if (onAutomationGestureEnded)
                onAutomationGestureEnded();
        };
    }

    fadeIn.setRange (0.0, 64.0, 0.01);
    fadeOut.setRange (0.0, 64.0, 0.01);
    fadeIn.textFromValueFunction = [] (double value)
    {
        return juce::String (value, 2) + " beats";
    };
    fadeOut.textFromValueFunction = fadeIn.textFromValueFunction;
    fadeIn.setTooltip ("Fade-in duration in beats");
    fadeOut.setTooltip ("Fade-out duration in beats");
    sourceBpm.setPopupDisplayEnabled (true, true, nullptr);

    fadeIn.onValueChange = [this]
    {
        if (onAutomationParameterChanged)
            onAutomationParameterChanged ("Fade In", fadeIn.getValue());
        if (clip == nullptr || ! onFadesChanged)
            return;
        if (! onFadesChanged (fadeIn.getValue(), fadeOut.getValue()))
            fadeIn.setValue (clip->fadeInBeats, juce::dontSendNotification);
    };
    fadeOut.onValueChange = [this]
    {
        if (onAutomationParameterChanged)
            onAutomationParameterChanged ("Fade Out", fadeOut.getValue());
        if (clip == nullptr || ! onFadesChanged)
            return;
        if (! onFadesChanged (fadeIn.getValue(), fadeOut.getValue()))
            fadeOut.setValue (clip->fadeOutBeats, juce::dontSendNotification);
    };
}

void AudioClipEditor::applyPlaybackProperties()
{
    if (clip == nullptr || onPlaybackChanged == nullptr)
        return;
    if (! onPlaybackChanged (pitchSlider.getValue(), centsSlider.getValue(),
                             gainSlider.getValue(),
                             std::clamp (warpModeBox.getSelectedId() - 1, 0, 5),
                             reverseButton.getToggleState()))
    {
        reverseButton.setToggleState (clip->reverse, juce::dontSendNotification);
        return;
    }
    repaint();
}

void AudioClipEditor::setClip (const roll::AudioPlacement* selected,
                               std::shared_ptr<const AudioFileData> audio,
                               double bpm, double beatGrid)
{
    const bool changedClip = selected != nullptr
        && (clip == nullptr || selected->id != clip->id || selected->filePath != clip->filePath);
    clip = selected;
    source = std::move (audio);
    projectBpm = bpm;
    gridBeats = beatGrid;
    if (changedClip || selected == nullptr)
    {
        waveformZoom = 1.0;
        waveformViewStartBeat = 0.0;
        draggedWarpMarker = -1;
        navigationPending = false;
        navigationDragging = false;
    }
    if (selected != nullptr)
        waveformViewStartBeat = std::clamp (
            waveformViewStartBeat, 0.0,
            std::max (0.0, selected->lengthBeats - selected->lengthBeats / waveformZoom));
    fileName.setText (clip != nullptr ? juce::String (clip->name) : "No audio selected",
                      juce::dontSendNotification);
    const double defaultBpm = source != nullptr && source->estimatedBpm > 0.0
        ? std::clamp (source->estimatedBpm, 20.0, 300.0)
        : 120.0;
    sourceBpm.setDoubleClickReturnValue (true, defaultBpm);
    sourceBpm.setValue (clip != nullptr ? clip->sourceBpm : 120.0, juce::dontSendNotification);
    sourceBpm.setEnabled (clip != nullptr && clip->durationSeconds > 0.0);
    warpButton.setToggleState (clip != nullptr && clip->warpEnabled, juce::dontSendNotification);
    warpButton.setEnabled (clip != nullptr);
    reverseButton.setToggleState (clip != nullptr && clip->reverse, juce::dontSendNotification);
    reverseButton.setEnabled (clip != nullptr);
    warpModeBox.setSelectedId (clip != nullptr
        ? std::clamp (clip->warpMode + 1, 1, 6) : 1,
        juce::dontSendNotification);
    warpModeBox.setEnabled (clip != nullptr);
    pitchSlider.setValue (clip != nullptr ? clip->pitchSemitones : 0.0,
                          juce::dontSendNotification);
    centsSlider.setValue (clip != nullptr ? clip->pitchCents : 0.0,
                          juce::dontSendNotification);
    gainSlider.setValue (clip != nullptr ? clip->gainDb : 0.0,
                         juce::dontSendNotification);
    pitchSlider.setEnabled (clip != nullptr);
    centsSlider.setEnabled (clip != nullptr);
    gainSlider.setEnabled (clip != nullptr);
    doubleBpmButton.setEnabled (clip != nullptr);
    halveBpmButton.setEnabled (clip != nullptr);
    splitButton.setEnabled (clip != nullptr);
    joinButton.setEnabled (clip != nullptr);
    fadeIn.setEnabled (clip != nullptr);
    fadeOut.setEnabled (clip != nullptr);
    const double length = clip != nullptr ? clip->lengthBeats : 0.0;
    fadeIn.setRange (0.0, length, 0.01);
    fadeOut.setRange (0.0, length, 0.01);
    fadeIn.setValue (clip != nullptr ? clip->fadeInBeats : 0.0, juce::dontSendNotification);
    fadeOut.setValue (clip != nullptr ? clip->fadeOutBeats : 0.0, juce::dontSendNotification);
    tempoInfo.setText (clip != nullptr
        ? "Project: " + juce::String (projectBpm, 1) + " BPM    |    Duration: "
            + juce::String (clip->durationSeconds, 2) + " s"
        : juce::String(), juce::dontSendNotification);
    repaint();
}

void AudioClipEditor::mouseWheelMove (const juce::MouseEvent& event,
                                      const juce::MouseWheelDetails& wheel)
{
    const auto bounds = waveformBounds();
    if (clip == nullptr || bounds.isEmpty())
        return;
    const double factor = wheel.deltaY > 0.0 ? 1.25 : 0.8;
    const double oldRange = clip->lengthBeats / waveformZoom;
    const double mouseProportion = std::clamp (
        (event.position.x - bounds.getX()) / static_cast<double> (bounds.getWidth()), 0.0, 1.0);
    const double anchorBeat = waveformViewStartBeat + mouseProportion * oldRange;
    waveformZoom = std::clamp (waveformZoom * factor, 1.0, 64.0);
    const double newRange = clip->lengthBeats / waveformZoom;
    waveformViewStartBeat = anchorBeat - mouseProportion * newRange;
    waveformViewStartBeat = std::clamp (waveformViewStartBeat, 0.0,
                                        std::max (0.0, clip->lengthBeats - newRange));
    repaint();
}

void AudioClipEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto bounds = waveformBounds();
    if (clip == nullptr || ! bounds.contains (event.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    auto markers = clip->warpMarkers;
    if (markers.empty())
        markers = { { clip->sourceOffsetSeconds, 0.0 },
                    { clip->sourceOffsetSeconds + clip->durationSeconds, clip->lengthBeats } };
    const bool overWarpMarker = event.position.y <= bounds.getY() + 12.0f
        && markers.size() > 2
        && std::any_of (markers.begin() + 1, markers.end() - 1,
            [this, &event] (const roll::AudioWarpMarker& marker)
            {
                return std::abs (event.position.x - waveformXAtBeat (marker.beat)) <= 9.0f;
            });
    setMouseCursor (overWarpMarker ? juce::MouseCursor::DraggingHandCursor : ui::zoomCursor());
}

void AudioClipEditor::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

juce::Rectangle<int> AudioClipEditor::waveformBounds() const
{
    return getLocalBounds().reduced (8).withTrimmedTop (116);
}

double AudioClipEditor::beatAtWaveformX (float x) const
{
    const auto bounds = waveformBounds();
    if (clip == nullptr || bounds.isEmpty() || clip->lengthBeats <= 0.0)
        return 0.0;
    const double visibleLength = clip->lengthBeats / waveformZoom;
    const double proportion = std::clamp (
        (x - bounds.getX()) / static_cast<double> (bounds.getWidth()), 0.0, 1.0);
    return std::clamp (waveformViewStartBeat + proportion * visibleLength,
                       0.0, clip->lengthBeats);
}

float AudioClipEditor::waveformXAtBeat (double beat) const
{
    const auto bounds = waveformBounds();
    if (clip == nullptr || bounds.isEmpty() || clip->lengthBeats <= 0.0)
        return static_cast<float> (bounds.getX());
    const double visibleLength = clip->lengthBeats / waveformZoom;
    return static_cast<float> (bounds.getX()
        + (beat - waveformViewStartBeat) / visibleLength * bounds.getWidth());
}

void AudioClipEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = waveformBounds();
    if (clip == nullptr || ! bounds.contains (event.getPosition()))
        return;

    auto markers = clip->warpMarkers;
    if (markers.empty())
        markers = { { clip->sourceOffsetSeconds, 0.0 },
                    { clip->sourceOffsetSeconds + clip->durationSeconds, clip->lengthBeats } };
    if (event.position.y <= bounds.getY() + 12.0f)
        for (std::size_t i = 1; i + 1 < markers.size(); ++i)
            if (std::abs (event.position.x - waveformXAtBeat (markers[i].beat)) <= 9.0f)
            {
                draggedWarpMarker = static_cast<int> (i);
                dragStartX = event.position.x;
                dragStartSourceSeconds = markers[i].sourceSeconds;
                if (onAutomationGestureStarted)
                    onAutomationGestureStarted();
                return;
            }

    navigationPending = true;
    navigationDragging = false;
    navigationLastScreenPosition = event.getScreenPosition();
    navigationAnchorProportion = std::clamp (
        (event.position.x - bounds.getX()) / static_cast<double> (bounds.getWidth()), 0.0, 1.0);
    navigationAnchorBeat = beatAtWaveformX (event.position.x);
}

void AudioClipEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedWarpMarker < 1)
    {
        if (! navigationPending || clip == nullptr)
            return;
        if (! navigationDragging && event.getDistanceFromDragStart() < 4.0f)
            return;

        navigationDragging = true;
        const auto screenPosition = event.getScreenPosition();
        const auto delta = screenPosition - navigationLastScreenPosition;
        navigationLastScreenPosition = screenPosition;
        const auto bounds = waveformBounds();
        if (bounds.isEmpty())
            return;

        if (delta.y != 0)
        {
            waveformZoom = std::clamp (
                waveformZoom * std::exp (static_cast<double> (delta.y) * 0.012), 1.0, 64.0);
            const double newRange = clip->lengthBeats / waveformZoom;
            waveformViewStartBeat = navigationAnchorBeat - navigationAnchorProportion * newRange;
            waveformViewStartBeat = std::clamp (
                waveformViewStartBeat, 0.0, std::max (0.0, clip->lengthBeats - newRange));
        }

        if (delta.x != 0)
        {
            const double visibleRange = clip->lengthBeats / waveformZoom;
            const double oldStartBeat = waveformViewStartBeat;
            waveformViewStartBeat = std::clamp (
                waveformViewStartBeat + delta.x * visibleRange / bounds.getWidth(),
                0.0, std::max (0.0, clip->lengthBeats - visibleRange));
            navigationAnchorBeat += waveformViewStartBeat - oldStartBeat;
        }
        repaint();
        return;
    }

    if (clip == nullptr || ! onWarpMarkersChanged)
        return;

    auto markers = clip->warpMarkers;
    if (markers.empty())
        markers = { { clip->sourceOffsetSeconds, 0.0 },
                    { clip->sourceOffsetSeconds + clip->durationSeconds, clip->lengthBeats } };
    if (draggedWarpMarker + 1 >= static_cast<int> (markers.size()))
        return;

    auto& marker = markers[static_cast<std::size_t> (draggedWarpMarker)];
    const auto& previous = markers[static_cast<std::size_t> (draggedWarpMarker - 1)];
    const auto& next = markers[static_cast<std::size_t> (draggedWarpMarker + 1)];
    if (next.sourceSeconds - previous.sourceSeconds <= 2.0e-6)
        return;
    if (event.mods.isShiftDown())
    {
        const double viewEndBeat = std::min (
            clip->lengthBeats, waveformViewStartBeat + clip->lengthBeats / waveformZoom);
        const double viewStartSeconds = roll::audioSourceSecondsAtBeat (
            *clip, waveformViewStartBeat);
        const double viewEndSeconds = roll::audioSourceSecondsAtBeat (*clip, viewEndBeat);
        const double secondsPerPixel = (viewEndSeconds - viewStartSeconds)
            / std::max (1, waveformBounds().getWidth());
        marker.sourceSeconds = std::clamp (
            dragStartSourceSeconds + (event.position.x - dragStartX)
                * (clip->reverse ? -secondsPerPixel : secondsPerPixel),
            previous.sourceSeconds + 1.0e-6, next.sourceSeconds - 1.0e-6);
    }
    else
    {
        const double minimumSpacing = std::min (
            std::max (1.0e-4, gridBeats * 0.1), (next.beat - previous.beat) * 0.49);
        double beat = beatAtWaveformX (event.position.x);
        if (gridBeats > 0.0)
            beat = std::round (beat / gridBeats) * gridBeats;
        marker.beat = std::clamp (beat, previous.beat + minimumSpacing,
                                  next.beat - minimumSpacing);
    }

    if (onWarpMarkersChanged (markers))
        repaint();
}

void AudioClipEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggedWarpMarker >= 0 && onAutomationGestureEnded)
        onAutomationGestureEnded();
    draggedWarpMarker = -1;
    navigationPending = false;
    navigationDragging = false;
}

void AudioClipEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (clip == nullptr)
        return;
    if (waveformBounds().contains (event.getPosition()))
    {
        const auto bounds = waveformBounds();
        if (event.position.y > bounds.getY() + bounds.getHeight() * 0.5f
            || clip->durationSeconds <= 0.0 || ! onWarpMarkersChanged)
            return;

        auto markers = clip->warpMarkers;
        if (markers.empty())
            markers = { { clip->sourceOffsetSeconds, 0.0 },
                        { clip->sourceOffsetSeconds + clip->durationSeconds, clip->lengthBeats } };
        const float x = event.position.x;
        for (std::size_t i = 1; i + 1 < markers.size(); ++i)
            if (std::abs (x - waveformXAtBeat (markers[i].beat)) <= 9.0f)
            {
                markers.erase (markers.begin() + static_cast<std::ptrdiff_t> (i));
                if (onWarpMarkersChanged (markers))
                    repaint();
                return;
            }

        const double unsnappedBeat = beatAtWaveformX (x);
        const double sourceSeconds = roll::audioSourceSecondsAtBeat (*clip, unsnappedBeat);
        double beat = unsnappedBeat;
        if (gridBeats > 0.0)
            beat = std::round (beat / gridBeats) * gridBeats;
        const auto next = std::upper_bound (markers.begin(), markers.end(), beat,
            [] (double position, const roll::AudioWarpMarker& marker) { return position < marker.beat; });
        if (next == markers.begin() || next == markers.end())
            return;
        const auto previous = next - 1;
        if (beat - previous->beat <= 1.0e-4 || next->beat - beat <= 1.0e-4
            || sourceSeconds - previous->sourceSeconds <= 1.0e-6
            || next->sourceSeconds - sourceSeconds <= 1.0e-6)
            return;
        markers.insert (next, { sourceSeconds, beat });
        if (onWarpMarkersChanged (markers))
            repaint();
        return;
    }
    const double defaultBpm = source != nullptr && source->estimatedBpm > 0.0
        ? std::clamp (source->estimatedBpm, 20.0, 300.0) : 120.0;
    sourceBpm.setValue (defaultBpm, juce::sendNotification);
}

void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    auto waveformArea = waveformBounds();
    if (waveformArea.isEmpty())
        return;

    g.setColour (theme::panelLight());
    g.fillRoundedRectangle (waveformArea.toFloat(), 4.0f);
    g.saveState();
    g.reduceClipRegion (waveformArea);

    const double beats = clip != nullptr ? std::max (clip->lengthBeats, 0.0) : 0.0;
    if (beats > 0.0 && gridBeats > 0.0)
    {
        const double visibleEnd = std::min (beats, waveformViewStartBeat + beats / waveformZoom);
        const int firstSubdivision = std::max (0, static_cast<int> (
            std::ceil (waveformViewStartBeat / gridBeats)));
        const int lastSubdivision = std::min (4096, static_cast<int> (
            std::floor (visibleEnd / gridBeats)));
        for (int i = firstSubdivision; i <= lastSubdivision; ++i)
        {
            const double beat = i * gridBeats;
            const bool bar = std::abs (std::remainder (beat, 4.0)) < 1.0e-6;
            const bool wholeBeat = std::abs (std::remainder (beat, 1.0)) < 1.0e-6;
            g.setColour (theme::border().withAlpha (bar ? 0.9f : wholeBeat ? 0.65f : 0.35f));
            const int x = juce::roundToInt (waveformXAtBeat (beat));
            g.drawVerticalLine (x, static_cast<float> (waveformArea.getY()),
                                static_cast<float> (waveformArea.getBottom()));
        }
    }

    const float halfHeight = static_cast<float> (waveformArea.getHeight()) * 0.42f;
    g.setColour (theme::accent().brighter (0.25f));
    g.drawHorizontalLine (waveformArea.getCentreY(), static_cast<float> (waveformArea.getX()),
                          static_cast<float> (waveformArea.getRight()));
    if (source != nullptr && ! source->waveform.empty())
    {
        std::vector<roll::WaveformRange> visibleWaveform;
        visibleWaveform.reserve (static_cast<std::size_t> (waveformArea.getWidth()));
        const double gainFactor = clip != nullptr
            ? std::pow (10.0, clip->gainDb / 20.0) : 1.0;
        for (int x = 0; x < waveformArea.getWidth(); ++x)
        {
            const double visibleLength = clip != nullptr
                ? clip->lengthBeats / waveformZoom : 0.0;
            const double beatStart = waveformViewStartBeat
                + static_cast<double> (x) / waveformArea.getWidth() * visibleLength;
            const double beatEnd = waveformViewStartBeat
                + static_cast<double> (x + 1) / waveformArea.getWidth() * visibleLength;
            double sourceStart = clip != nullptr
                ? roll::audioSourceSecondsAtBeat (*clip, beatStart) : 0.0;
            double sourceEnd = clip != nullptr
                ? roll::audioSourceSecondsAtBeat (*clip, beatEnd) : sourceStart;
            if (clip != nullptr && clip->reverse)
            {
                const double sourceEndClip = clip->sourceOffsetSeconds + clip->durationSeconds;
                sourceStart = sourceEndClip - (sourceStart - clip->sourceOffsetSeconds);
                sourceEnd = sourceEndClip - (sourceEnd - clip->sourceOffsetSeconds);
            }
            const double duration = source != nullptr && source->durationSeconds > 0.0
                ? source->durationSeconds : 1.0;
            auto firstIndex = static_cast<std::size_t> (std::clamp (
                std::min (sourceStart, sourceEnd) / duration * source->waveform.size(),
                0.0, static_cast<double> (source->waveform.size() - 1)));
            auto lastIndex = static_cast<std::size_t> (std::clamp (
                std::max (sourceStart, sourceEnd) / duration * source->waveform.size(),
                0.0, static_cast<double> (source->waveform.size() - 1)));
            if (lastIndex <= firstIndex)
                lastIndex = std::min (source->waveform.size() - 1, firstIndex + 1);
            float minimum = 0.0f, maximum = 0.0f;
            for (std::size_t i = firstIndex; i <= lastIndex; ++i)
            {
                minimum = std::min (minimum, source->waveform[i].minimum);
                maximum = std::max (maximum, source->waveform[i].maximum);
            }
            visibleWaveform.push_back ({
                static_cast<float> (std::clamp (minimum * gainFactor, -1.0, 1.0)),
                static_cast<float> (std::clamp (maximum * gainFactor, -1.0, 1.0))
            });
        }
        drawWaveformEnvelope (g, waveformArea.toFloat(), visibleWaveform,
                              theme::accent().brighter (0.25f), halfHeight);
    }
    if (source != nullptr && source->durationSeconds > 0.0)
    {
        g.setColour (theme::text().withAlpha (0.55f));
        for (const double seconds : source->transientSeconds)
        {
            if (clip == nullptr || seconds < clip->sourceOffsetSeconds
                || seconds > clip->sourceOffsetSeconds + clip->durationSeconds)
                continue;
            double sourceSeconds = seconds;
            if (clip->reverse)
                sourceSeconds = clip->sourceOffsetSeconds + clip->durationSeconds
                              - (seconds - clip->sourceOffsetSeconds);
            double beat = clip->lengthBeats
                * (sourceSeconds - clip->sourceOffsetSeconds)
                / std::max (1.0e-9, clip->durationSeconds);
            if (clip->warpMarkers.size() >= 2)
            {
                const auto& markers = clip->warpMarkers;
                const auto next = std::upper_bound (markers.begin(), markers.end(), sourceSeconds,
                    [] (double position, const roll::AudioWarpMarker& marker)
                    {
                        return position < marker.sourceSeconds;
                    });
                if (next == markers.begin())
                    beat = markers.front().beat;
                else if (next == markers.end())
                    beat = markers.back().beat;
                else
                {
                    const auto previous = next - 1;
                    const double proportion = (sourceSeconds - previous->sourceSeconds)
                        / (next->sourceSeconds - previous->sourceSeconds);
                    beat = previous->beat + (next->beat - previous->beat) * proportion;
                }
            }
            const float x = waveformXAtBeat (beat);
            if (x < waveformArea.getX() || x > waveformArea.getRight())
                continue;
            g.drawVerticalLine (juce::roundToInt (x),
                                static_cast<float> (waveformArea.getY()),
                                static_cast<float> (waveformArea.getY() + 5.0f));
        }
    }
    if (clip != nullptr && clip->lengthBeats > 0.0)
    {
        const std::vector<roll::AudioWarpMarker> markers = clip->warpMarkers.empty()
            ? std::vector<roll::AudioWarpMarker> {
                { clip->sourceOffsetSeconds, 0.0 },
                { clip->sourceOffsetSeconds + clip->durationSeconds, clip->lengthBeats } }
            : clip->warpMarkers;
        for (std::size_t i = 0; i < markers.size(); ++i)
        {
            const float x = waveformXAtBeat (markers[i].beat);
            if (x < waveformArea.getX() - 8.0f || x > waveformArea.getRight() + 8.0f)
                continue;
            const bool endpoint = i == 0 || i + 1 == markers.size();
            g.setColour (endpoint ? theme::text().withAlpha (0.7f) : theme::accent());
            g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (waveformArea.getY()),
                                static_cast<float> (waveformArea.getBottom()));
            juce::Path handle;
            handle.addTriangle (x, static_cast<float> (waveformArea.getY() + 1),
                                x - 5.0f, static_cast<float> (waveformArea.getY() + 10),
                                x + 5.0f, static_cast<float> (waveformArea.getY() + 10));
            g.fillPath (handle);
        }
    }
    g.restoreState();
}

void AudioClipEditor::resized()
{
    auto header = getLocalBounds().reduced (8).removeFromTop (34);
    closeButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (8);
    warpButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (6);
    reverseButton.setBounds (header.removeFromRight (82));
    header.removeFromRight (6);
    warpModeBox.setBounds (header.removeFromRight (120));
    header.removeFromRight (6);
    sourceBpm.setBounds (header.removeFromRight (126));
    header.removeFromRight (6);
    halveBpmButton.setBounds (header.removeFromRight (42));
    header.removeFromRight (4);
    doubleBpmButton.setBounds (header.removeFromRight (42));
    header.removeFromRight (6);
    bpmLabel.setBounds (header.removeFromRight (90));
    tempoInfo.setBounds (header.removeFromRight (205));
    fileName.setBounds (header);

    auto controls = getLocalBounds().reduced (8).withTop (42).removeFromTop (34);
    splitButton.setBounds (controls.removeFromLeft (84));
    controls.removeFromLeft (8);
    joinButton.setBounds (controls.removeFromLeft (84));
    controls.removeFromLeft (12);
    constexpr int fadeKnobWidth = 56;
    const int fadeControlsX = controls.getX();
    fadeInLabel.setBounds (controls.removeFromLeft (fadeKnobWidth));
    fadeInLabel.setVisible (false);
    fadeIn.setBounds (fadeInLabel.getBounds());
    controls.removeFromLeft (4);
    fadeOutLabel.setBounds (controls.removeFromLeft (fadeKnobWidth));
    fadeOutLabel.setVisible (false);
    fadeOut.setBounds (fadeOutLabel.getBounds());

    auto playbackRow = getLocalBounds().reduced (8).withTop (76).removeFromTop (32);
    playbackRow.removeFromLeft (fadeControlsX - playbackRow.getX());
    pitchSlider.setBounds (playbackRow.removeFromLeft (fadeKnobWidth));
    playbackRow.removeFromLeft (4);
    centsSlider.setBounds (playbackRow.removeFromLeft (fadeKnobWidth));
    playbackRow.removeFromLeft (4);
    gainSlider.setBounds (playbackRow.removeFromLeft (fadeKnobWidth));
}
