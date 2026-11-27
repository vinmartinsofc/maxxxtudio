#include "TransportBar.h"
#include <cmath>

void IconButton::setIcon (icons::Icon i)
{
    if (icon == i)
        return;

    icon = i;
    const auto name = icon == icons::Icon::Pause ? "Pause" : "Play";
    setName (name);
    setTooltip (icon == icons::Icon::Pause
        ? "Pause playback at the current position"
        : "Play from the current position");
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = icon == icons::Icon::Pause;
    if (theme::isHighTech())
    {
        const auto fill = down ? theme::pressed()
                        : active ? theme::panelLight()
                        : over ? theme::hover() : theme::control();
        theme::drawChromeSurface (g, r, 5.0f, fill);
        g.setColour (active || down ? theme::accent()
                    : over ? theme::accentHover() : theme::controlBorder());
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        if (active)
        {
            g.setColour (theme::accent());
            g.fillRoundedRectangle (r.withHeight (2.0f), 1.0f);
        }

        const auto iconColour = ! isEnabled() ? theme::textDim().withAlpha (0.4f)
                                  : active ? theme::accent() : theme::text();
        if (const auto* drawable = icons::get (icon, iconColour, 20))
            drawable->drawWithin (g, r.reduced (9.0f, 7.0f),
                                  juce::RectanglePlacement::centred, 1.0f);
        return;
    }

    const auto fill = active ? (over ? theme::accentHover() : theme::accent())
                             : down ? theme::pressed() : over ? theme::hover() : theme::control();
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (active ? theme::accentHover() : theme::controlBorder());
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const auto iconColour = ! isEnabled() ? theme::textDim().withAlpha (0.4f)
                                         : active ? theme::text()
                                                  : over ? juce::Colours::white : theme::text();
    if (const auto* drawable = icons::get (icon, iconColour, 20))
        drawable->drawWithin (g, r.reduced (9.0f, 7.0f), juce::RectanglePlacement::centred, 1.0f);
    else
    {
        g.setColour (iconColour);
        g.setFont (theme::fontOptions (20.0f, juce::Font::bold));
        g.drawText ("?", r, juce::Justification::centred);
    }
}

void IconToggleButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = getToggleState();
    const bool isRecordControl = icon == icons::Icon::Record;
    if (theme::isHighTech())
    {
        const auto fill = active && isRecordControl ? theme::recording().withAlpha (0.18f)
                        : down ? theme::pressed()
                        : active ? theme::panelLight()
                        : over ? theme::hover() : theme::control();
        theme::drawChromeSurface (g, bounds, 5.0f, fill);
        g.setColour (active && isRecordControl ? theme::recording()
                    : active || down ? theme::accent()
                    : over ? theme::accentHover() : theme::controlBorder());
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
        if (active)
        {
            g.setColour (isRecordControl ? theme::recording() : theme::accent());
            g.fillRoundedRectangle (bounds.withHeight (2.0f), 1.0f);
        }

        const auto iconColour = isEnabled() ? (active ? (isRecordControl ? theme::recording() : theme::accent())
                                                     : theme::textDim())
                                            : theme::textDim().withAlpha (0.4f);
        if (const auto* drawable = icons::get (icon, iconColour, 18))
            drawable->drawWithin (g, bounds.reduced (8.0f, 7.0f),
                                  juce::RectanglePlacement::centred, 1.0f);
        return;
    }

    const auto fill = active && isRecordControl ? theme::recording().withAlpha (0.15f)
                    : down ? theme::pressed() : over ? theme::hover() : theme::control();
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (active && isRecordControl ? theme::recording()
                : active ? theme::accent().withAlpha (0.7f) : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const auto iconColour = isEnabled() ? (active ? (isRecordControl ? theme::recording() : theme::accent())
                                                 : theme::textDim())
                                        : theme::textDim().withAlpha (0.4f);
    if (const auto* drawable = icons::get (icon, iconColour, 18))
        drawable->drawWithin (g, bounds.reduced (8.0f, 7.0f),
                              juce::RectanglePlacement::centred, 1.0f);
}

TransportBar::TransportBar (SequencerGraph& g) : graph (g)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (countInSelector);
    addAndMakeVisible (startButton);
    addAndMakeVisible (endButton);
    addAndMakeVisible (projectButton);
    addAndMakeVisible (optionsButton);
    addAndMakeVisible (metronomeButton);
    addAndMakeVisible (metronomeVolumeSlider);
    for (auto* b : std::initializer_list<juce::Component*> {
             &playButton, &stopButton, &recordButton, &startButton, &endButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    metronomeButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setMouseClickGrabsKeyboardFocus (false);
    optionsButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setTooltip ("Project: open, create, save, or export the session");
    projectButton.onClick = [this] { if (onProjectMenu) onProjectMenu(); };
    optionsButton.setTooltip ("Preferences: audio, plugins, interface, and other settings (Ctrl+,)");
    optionsButton.onClick = [this] { if (onOptionsMenu) onOptionsMenu(); };
    metronomeButton.setTooltip ("Metronome: toggle and adjust click volume");
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (graph.isMetronomeEnabled(), juce::dontSendNotification);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    metronomeButton.setColour (juce::TextButton::textColourOnId, theme::text());
    metronomeButton.onClick = [this] { graph.setMetronomeEnabled (metronomeButton.getToggleState()); };
    metronomeVolumeSlider.setName ("Metronome Volume");
    metronomeVolumeSlider.setTooltip ("Metronome Volume");
    metronomeVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    metronomeVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    metronomeVolumeSlider.setRange (0.0, 1.0, 0.01);
    metronomeVolumeSlider.setValue (graph.getMetronomeGain(), juce::dontSendNotification);
    metronomeVolumeSlider.setColour (juce::Slider::thumbColourId, theme::accent());
    metronomeVolumeSlider.setColour (juce::Slider::trackColourId, theme::border());
    metronomeVolumeSlider.onValueChange = [this]
    {
        graph.setMetronomeGain (static_cast<float> (metronomeVolumeSlider.getValue()));
    };
    cpuTooltipArea.setTooltip ("Average CPU usage: project audio process");
    addAndMakeVisible (cpuTooltipArea);
    loopButton.setMouseClickGrabsKeyboardFocus (false);
    browserButton.setMouseClickGrabsKeyboardFocus (false);
    fileBrowserButton.setMouseClickGrabsKeyboardFocus (false);
    favoritesButton.setMouseClickGrabsKeyboardFocus (false);
    pianoRollButton.setMouseClickGrabsKeyboardFocus (false);
    computerKeyboardButton.setMouseClickGrabsKeyboardFocus (false);
    followButton.setMouseClickGrabsKeyboardFocus (false);
    startButton.setTooltip ("Return to the start of the timeline");
    startButton.onClick = [this] { if (onGoToStart) onGoToStart(); };
    endButton.setTooltip ("Go to the end of the timeline");
    endButton.onClick = [this] { if (onGoToEnd) onGoToEnd(); };
    addAndMakeVisible (loopButton);
    addAndMakeVisible (browserButton);
    addAndMakeVisible (fileBrowserButton);
    addAndMakeVisible (favoritesButton);
    addAndMakeVisible (pianoRollButton);
    addAndMakeVisible (computerKeyboardButton);
    addAndMakeVisible (followButton);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmCaption);
    addAndMakeVisible (positionLabel);

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { stop(); };
    recordButton.setClickingTogglesState (false);
    recordButton.setTooltip ("Record audio/MIDI input, or automation in Automation Mode");
    countInSelector.addItem ("Off", 1);
    countInSelector.addItem ("1 beat", 2);
    countInSelector.addItem ("2 beats", 3);
    countInSelector.addItem ("3 beats", 4);
    countInSelector.addItem ("4 beats", 5);
    countInSelector.addItem ("8 beats", 6);
    countInSelector.setSelectedId (5, juce::dontSendNotification);
    countInSelector.setTooltip ("Count-in before recording: choose 0, 1, 2, 3, 4, or 8 beats");
    countInSelector.setColour (juce::ComboBox::backgroundColourId, theme::control());
    countInSelector.setColour (juce::ComboBox::textColourId, theme::text());
    countInSelector.setColour (juce::ComboBox::outlineColourId, theme::controlBorder());
    countInSelector.onChange = [this]
    {
        const int selectedId = countInSelector.getSelectedId();
        countInBeats = selectedId == 1 ? 0 : selectedId == 6 ? 8 : selectedId - 1;
    };
    recordButton.onClick = [this]
    {
        if (recording)
        {
            if (onRecordStop)
                onRecordStop();
            setRecording (false);
            return;
        }

        if (countInActive)
        {
            const bool shouldRestorePosition = ! countInWasPlaying;
            const double startBeat = countInStartBeat;
            cancelCountIn();
            if (shouldRestorePosition)
            {
                graph.setPlaying (false);
                graph.seek (startBeat);
            }
            updateButtons();
            return;
        }

        const bool wasPlaying = graph.isPlaying();
        if (! wasPlaying && onBeforePlay)
            onBeforePlay();
        if (onRecordStart)
        {
            if (countInBeats > 0)
                beginCountIn (wasPlaying);
            else if (onRecordStart (graph.isLoop()))
            {
                setRecording (true);
                if (! wasPlaying)
                    graph.setPlaying (true);
                updateButtons();
            }
        }
    };
    loopButton.setTooltip ("Loop: toggle repeat for the marked region");

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (graph.isLoop(), juce::dontSendNotification);
    loopButton.onClick = [this] { setLoopEnabled (loopButton.getToggleState()); };

    browserButton.setTooltip ("VSTs and instruments: show or hide the plugin browser (Ctrl+B)");
    browserButton.setClickingTogglesState (true);
    browserButton.setToggleState (true, juce::dontSendNotification);
    browserButton.onClick = [this]
    {
        if (onBrowserToggled)
            onBrowserToggled (browserButton.getToggleState());
    };

    fileBrowserButton.setTooltip ("Audio Files: search, preview, and drag samples");
    fileBrowserButton.setClickingTogglesState (true);
    fileBrowserButton.setToggleState (false, juce::dontSendNotification);
    fileBrowserButton.onClick = [this]
    {
        if (onFileBrowserToggled)
            onFileBrowserToggled (fileBrowserButton.getToggleState());
    };

    favoritesButton.setTooltip ("Favorites: color-coded collections of plugins and files");
    favoritesButton.setClickingTogglesState (true);
    favoritesButton.setToggleState (false, juce::dontSendNotification);
    favoritesButton.onClick = [this]
    {
        if (onFavoritesToggled)
            onFavoritesToggled (favoritesButton.getToggleState());
    };

    pianoRollButton.setTooltip ("Piano Roll and devices: show or hide the bottom panel (Ctrl+')");
    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setToggleState (true, juce::dontSendNotification);
    pianoRollButton.onClick = [this]
    {
        if (onPianoRollToggled)
            onPianoRollToggled (pianoRollButton.getToggleState());
    };

    computerKeyboardButton.setTooltip ("Computer MIDI Keyboard: play notes using the computer (M; Z/X change octave)");
    computerKeyboardButton.setClickingTogglesState (true);
    computerKeyboardButton.onClick = [this]
    {
        if (onComputerMidiKeyboardToggled)
            onComputerMidiKeyboardToggled (computerKeyboardButton.getToggleState());
    };

    followButton.setTooltip ("Follow Arrangement: keep the playhead in view (Ctrl+Shift+F)");
    followButton.setClickingTogglesState (true);
    followButton.setToggleState (false, juce::dontSendNotification);
    followButton.onClick = [this]
    {
        if (onFollowToggled)
            onFollowToggled (followButton.getToggleState());
    };

    bpmLabel.setEditable (false, true, false);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setColour (juce::Label::backgroundColourId, theme::control());
    bpmLabel.setColour (juce::Label::textColourId, theme::text());
    bpmLabel.onEditorShow = [this]
    {
        if (auto* editor = bpmLabel.getCurrentTextEditor())
        {
            editor->setSelectAllWhenFocused (true);
            editor->selectAll();
            juce::Component::SafePointer<juce::TextEditor> safeEditor (editor);
            juce::MessageManager::callAsync ([safeEditor]
            {
                if (safeEditor != nullptr)
                    safeEditor->selectAll();
            });
        }
    };
    bpmLabel.setText (juce::String (graph.getBpm(), 2), juce::dontSendNotification);
    bpmLabel.onTextChange = [this]
    {
        const double v = std::clamp (bpmLabel.getText().getDoubleValue(), 20.0, 999.0);
        setBpm (v);
    };

    bpmCaption.setColour (juce::Label::textColourId, theme::textDim());
    bpmCaption.setFont (theme::fontOptions (12.5f));

    positionLabel.setName ("Current Project Time");
    positionLabel.setTooltip ("Current position in project time (hours:minutes:seconds:frames)");
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::backgroundColourId, theme::control());
    positionLabel.setColour (juce::Label::textColourId, theme::text());
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));

    startTimerHz (30);
    timerCallback();
}

void TransportBar::togglePlay()
{
    if (countInActive)
        cancelCountIn();
    const bool wasPlaying = graph.isPlaying();
    if (wasPlaying)
    {
        if (recording)
        {
            if (onRecordStop)
                onRecordStop();
            setRecording (false);
        }
        if (onBeforePause)
            onBeforePause();
    }
    else if (onBeforePlay)
        onBeforePlay();

    graph.setPlaying (! wasPlaying);
    updateButtons();
}

void TransportBar::setLoopEnabled (bool on)
{
    graph.setLoop (on);
    loopButton.setToggleState (on, juce::dontSendNotification);
    if (onLoopToggled)
        onLoopToggled (on);
}

void TransportBar::stop()
{
    if (countInActive)
        cancelCountIn();
    if (recording)
    {
        if (onRecordStop)
            onRecordStop();
        setRecording (false);
    }
    if (onBeforeStop)
        onBeforeStop();
    graph.setPlaying (false);
    graph.rewind();
    updateButtons();
}

void TransportBar::setRecording (bool shouldRecord)
{
    recording = shouldRecord;
    updateRecordControls();
}

void TransportBar::cancelCountIn()
{
    if (! countInActive)
        return;

    countInActive = false;
    restoreCountInMetronome();
    if (countInLoopWasEnabled)
        setLoopEnabled (true);
    graph.setPlaying (countInWasPlaying);
    positionLabel.setColour (juce::Label::textColourId, theme::text());
    lastPosition = -1.0;
    updateRecordControls();
    updateButtons();
}

void TransportBar::updateRecordControls()
{
    recordButton.setToggleState (recording || countInActive, juce::dontSendNotification);
    loopButton.setEnabled (! recording && ! countInActive);
    countInSelector.setEnabled (! recording && ! countInActive);
    metronomeButton.setEnabled (! countInActive);
    metronomeVolumeSlider.setEnabled (! countInActive);
    recordButton.setTooltip (recording ? "Stop recording"
        : countInActive ? "Cancel the recording count-in"
                        : "Record audio/MIDI input, or automation in Automation Mode");
}

void TransportBar::beginCountIn (bool wasPlaying)
{
    countInWasPlaying = wasPlaying;
    countInStartBeat = graph.positionBeats();
    if (! std::isfinite (countInStartBeat))
        countInStartBeat = 0.0;
    countInLoopWasEnabled = graph.isLoop();
    countInMetronomeWasEnabled = graph.isMetronomeEnabled();
    activeCountInBeats = countInBeats;
    countInRemainingBeats = activeCountInBeats;
    countInLastUpdateSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    displayedCountInBeat = 0;
    countInActive = true;

    if (countInLoopWasEnabled)
        setLoopEnabled (false);
    graph.setMetronomeEnabled (true);
    metronomeButton.setToggleState (true, juce::dontSendNotification);
    positionLabel.setColour (juce::Label::textColourId, theme::recording());
    updateRecordControls();
    updateCountInDisplay (countInRemainingBeats);
    graph.setPlaying (false);
    updateButtons();
}

void TransportBar::restoreCountInMetronome()
{
    graph.setMetronomeEnabled (countInMetronomeWasEnabled);
    metronomeButton.setToggleState (countInMetronomeWasEnabled, juce::dontSendNotification);
}

void TransportBar::finishCountIn()
{
    const bool loopWasEnabled = countInLoopWasEnabled;
    const bool wasPlaying = countInWasPlaying;
    const double startBeat = countInStartBeat;
    countInActive = false;
    restoreCountInMetronome();
    positionLabel.setColour (juce::Label::textColourId, theme::text());
    updateRecordControls();

    if (onRecordStart && onRecordStart (loopWasEnabled))
    {
        setRecording (true);
        if (! graph.isPlaying())
            graph.setPlaying (true);
    }
    else
    {
        if (loopWasEnabled)
            setLoopEnabled (true);
        if (! wasPlaying)
            graph.seek (startBeat);
        graph.setPlaying (wasPlaying);
    }
    lastPosition = -1.0;
    updateButtons();
}

void TransportBar::updateCountInDisplay (double remainingBeats)
{
    const int beat = juce::jlimit (1, activeCountInBeats,
        static_cast<int> (std::ceil (remainingBeats - 1.0e-9)));
    if (beat == displayedCountInBeat)
        return;

    displayedCountInBeat = beat;
    const auto text = "COUNT-IN " + juce::String (beat);
    positionLabel.setText (text, juce::dontSendNotification);
    recordButton.setTooltip ("Cancel count-in (" + juce::String (beat) + " beats remaining)");
}

void TransportBar::resumeFromPosition (double beats)
{
    if (countInActive)
        cancelCountIn();
    if (recording)
    {
        if (onRecordStop)
            onRecordStop();
        setRecording (false);
    }
    graph.seek (beats);
    graph.setPlaying (true);
    updateButtons();
}

void TransportBar::setBrowserVisible (bool visible)
{
    browserButton.setToggleState (visible, juce::dontSendNotification);
}

void TransportBar::setFileBrowserVisible (bool visible)
{
    fileBrowserButton.setToggleState (visible, juce::dontSendNotification);
}

void TransportBar::setFavoritesVisible (bool visible)
{
    favoritesButton.setToggleState (visible, juce::dontSendNotification);
}

void TransportBar::setPianoRollVisible (bool visible)
{
    pianoRollButton.setToggleState (visible, juce::dontSendNotification);
}

void TransportBar::setComputerMidiKeyboardEnabled (bool enabled)
{
    computerKeyboardButton.setToggleState (enabled, juce::dontSendNotification);
}

void TransportBar::setFollowEnabled (bool enabled)
{
    followButton.setToggleState (enabled, juce::dontSendNotification);
}

void TransportBar::setBpm (double bpm)
{
    const double previousBpm = graph.getBpm();
    graph.setBpm (std::clamp (bpm, 20.0, 999.0));
    bpmLabel.setText (juce::String (graph.getBpm(), 2), juce::dontSendNotification);
    if (std::abs (graph.getBpm() - previousBpm) > 1.0e-6 && onBpmChanged)
        onBpmChanged (graph.getBpm());
}

void TransportBar::setMetronomeState (bool enabled, float gain)
{
    graph.setMetronomeEnabled (enabled);
    graph.setMetronomeGain (gain);
    metronomeButton.setToggleState (enabled, juce::dontSendNotification);
    metronomeVolumeSlider.setValue (gain, juce::dontSendNotification);
}

void TransportBar::updateButtons()
{
    playButton.setIcon (graph.isPlaying() ? icons::Icon::Pause : icons::Icon::Play);
}

void TransportBar::timerCallback()
{
    updateButtons();
    displayedCpuAverage = graph.audioCpuAverage();
    repaint (cpuMeterBounds);

    const double pos = graph.positionBeats();
    const double bpm = graph.getBpm();
    if (countInActive)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double elapsed = std::max (0.0, now - countInLastUpdateSeconds);
        countInLastUpdateSeconds = now;
        countInRemainingBeats -= elapsed * std::clamp (bpm, 20.0, 999.0) / 60.0;
        if (countInRemainingBeats <= 0.0)
            finishCountIn();
        else
            updateCountInDisplay (countInRemainingBeats);
    }

    const bool positionChanged = std::abs (pos - lastPosition) >= 1.0e-9;
    const bool tempoChanged = std::abs (bpm - lastBpm) >= 1.0e-9;
    if (! positionChanged && ! tempoChanged)
        return;
    lastPosition = pos;
    lastBpm = bpm;

    const double safeBpm = std::isfinite (bpm) && bpm > 0.0 ? bpm : 120.0;
    const double seconds = std::max (0.0, pos) * 60.0 / safeBpm;
    const auto totalFrames = static_cast<juce::int64> (std::floor (seconds * 30.0));
    const auto totalSeconds = totalFrames / 30;
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto displaySeconds = totalSeconds % 60;
    const auto frames = totalFrames % 30;
    const auto timecode = juce::String (hours).paddedLeft ('0', 2) + ":"
        + juce::String (minutes).paddedLeft ('0', 2) + ":"
        + juce::String (displaySeconds).paddedLeft ('0', 2) + ":"
        + juce::String (frames).paddedLeft ('0', 2);
    if (! countInActive)
        positionLabel.setText (timecode, juce::dontSendNotification);

    if (positionChanged && onPosition)
        onPosition (pos);
}

void TransportBar::paint (juce::Graphics& g)
{
    if (theme::isHighTech())
        theme::drawChromeSurface (g, getLocalBounds().toFloat(), 0.0f, theme::panel());
    else
        g.fillAll (theme::panel());
    if (! cpuMeterBounds.isEmpty())
    {
        const auto meter = cpuMeterBounds.toFloat().reduced (1.0f);
        const auto bar = meter.withTrimmedLeft (3.0f).withTrimmedRight (3.0f)
                             .withY (meter.getBottom() - 5.0f).withHeight (3.0f);
        const auto loadColour = displayedCpuAverage >= 1.0 ? theme::recording()
                               : displayedCpuAverage >= 0.75 ? theme::warning()
                                                             : theme::accent();

        g.setColour (theme::control());
        g.fillRoundedRectangle (meter, 4.0f);
        if (const auto* cpuIcon = icons::get (icons::Icon::Cpu, theme::textDim(), 15))
        {
            const auto iconBounds = juce::Rectangle<float> (meter.getX() + 5.0f,
                                                             meter.getY() + 4.0f, 15.0f, 15.0f);
            cpuIcon->drawWithin (g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        }
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (11.5f));
        const auto cpuText = juce::String (
            static_cast<int> (std::lround (displayedCpuAverage * 100.0))) + "%";
        g.drawText (cpuText,
                    meter.toNearestInt().withTrimmedLeft (18).withTrimmedBottom (5),
                    juce::Justification::centred, true);
        g.setColour (theme::border());
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (loadColour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * static_cast<float> (std::min (displayedCpuAverage, 1.0))),
                                1.5f);
    }
    g.setColour (theme::border());
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (8, 4);
    const bool compact = getWidth() < 1120;
    bpmCaption.setBounds (area.removeFromLeft (34));
    bpmLabel.setBounds (area.removeFromLeft (76));
    area.removeFromLeft (16);
    startButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (6);
    playButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (6);
    recordButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (4);
    countInSelector.setBounds (area.removeFromLeft (78).reduced (0, 7));
    area.removeFromLeft (4);
    stopButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (4);
    endButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (12);
    loopButton.setBounds (area.removeFromLeft (56));
    area.removeFromLeft (6);
    metronomeButton.setBounds (area.removeFromLeft (88));
    area.removeFromLeft (6);
    metronomeVolumeSlider.setBounds (area.removeFromLeft (68).reduced (0, 8));
    area.removeFromLeft (12);
    const int spacing = compact ? 4 : 6;
    const int rightControlsWidth = 8 * 40 + 7 * spacing;
    const int availableWidth = std::max (0, area.getWidth() - rightControlsWidth);
    int cpuWidth = std::min (150, std::max (0, availableWidth - 270));
    if (cpuWidth < 72)
        cpuWidth = 0;
    if (cpuWidth >= 72)
    {
        cpuMeterBounds = area.removeFromLeft (cpuWidth);
        cpuTooltipArea.setBounds (cpuMeterBounds);
        cpuTooltipArea.setVisible (true);
        area.removeFromLeft (12);
    }
    else
    {
        cpuMeterBounds = {};
        cpuTooltipArea.setBounds ({});
        cpuTooltipArea.setVisible (false);
    }
    const int clockAreaWidth = std::max (0, area.getWidth() - rightControlsWidth);
    constexpr int statusWidth = 128;
    constexpr int statusGap = 6;
    constexpr int minimumClockWidth = 120;
    const int reservedStatusWidth = clockAreaWidth >= statusWidth + statusGap + minimumClockWidth
        ? statusWidth + statusGap : 0;
    const int availableClockWidth = clockAreaWidth - reservedStatusWidth;
    const int clockWidth = std::min (190, availableClockWidth);
    midiStatusBounds = reservedStatusWidth > 0
        ? juce::Rectangle<int> (area.getX(), 4, statusWidth, getHeight() - 8)
        : juce::Rectangle<int>();
    positionLabel.setVisible (clockWidth >= 96);
    positionLabel.setBounds (clockWidth >= 96
        ? juce::Rectangle<int> (area.getX() + reservedStatusWidth
                                  + (availableClockWidth - clockWidth) / 2,
                                3, clockWidth, getHeight() - 6)
        : juce::Rectangle<int>());
    browserButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    fileBrowserButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    favoritesButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    pianoRollButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    computerKeyboardButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    followButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    optionsButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    projectButton.setBounds (area.removeFromRight (40));
}
