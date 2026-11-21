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
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("?", r, juce::Justification::centred);
    }
}

void IconToggleButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = getToggleState();
    const auto fill = down ? theme::pressed() : over ? theme::hover() : theme::control();
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (active ? theme::accent().withAlpha (0.7f) : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const auto iconColour = isEnabled() ? (active ? theme::accent() : theme::textDim())
                                        : theme::textDim().withAlpha (0.4f);
    if (const auto* drawable = icons::get (icon, iconColour, 18))
        drawable->drawWithin (g, bounds.reduced (8.0f, 7.0f),
                              juce::RectanglePlacement::centred, 1.0f);
}

TransportBar::TransportBar (SequencerGraph& g) : graph (g)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (startButton);
    addAndMakeVisible (endButton);
    addAndMakeVisible (projectButton);
    addAndMakeVisible (optionsButton);
    addAndMakeVisible (metronomeButton);
    addAndMakeVisible (metronomeVolumeSlider);
    for (auto* b : std::initializer_list<juce::Component*> {
             &playButton, &stopButton, &startButton, &endButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    metronomeButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setMouseClickGrabsKeyboardFocus (false);
    optionsButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setTooltip ("Project: open, create, save, or export the session");
    projectButton.onClick = [this] { if (onProjectMenu) onProjectMenu(); };
    optionsButton.setTooltip ("Preferences: audio, plugins, themes, and other settings (Ctrl+,)");
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
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmCaption);
    addAndMakeVisible (positionLabel);

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { stop(); };
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

    pianoRollButton.setTooltip ("Piano Roll and devices: show or hide the bottom panel");
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

    bpmLabel.setEditable (false, true, false);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setColour (juce::Label::backgroundColourId, theme::control());
    bpmLabel.setColour (juce::Label::textColourId, theme::text());
    bpmLabel.setText (juce::String (graph.getBpm(), 2), juce::dontSendNotification);
    bpmLabel.onTextChange = [this]
    {
        const double v = std::clamp (bpmLabel.getText().getDoubleValue(), 20.0, 999.0);
        setBpm (v);
    };

    bpmCaption.setColour (juce::Label::textColourId, theme::textDim());
    bpmCaption.setFont (juce::FontOptions (12.5f));

    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::backgroundColourId, theme::control());
    positionLabel.setColour (juce::Label::textColourId, theme::text());
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));

    startTimerHz (30);
    timerCallback();
}

void TransportBar::togglePlay()
{
    const bool wasPlaying = graph.isPlaying();
    if (wasPlaying)
    {
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
    if (onBeforeStop)
        onBeforeStop();
    graph.setPlaying (false);
    graph.rewind();
    updateButtons();
}

void TransportBar::resumeFromPosition (double beats)
{
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
    if (std::abs (pos - lastPosition) < 1.0e-9) return;
    lastPosition = pos;

    const int bar   = static_cast<int> (std::floor (pos / 4.0)) + 1;
    const int beat  = static_cast<int> (std::floor (std::fmod (pos, 4.0))) + 1;
    const int sixt  = static_cast<int> (std::floor (std::fmod (pos, 1.0) * 4.0)) + 1;
    positionLabel.setText (juce::String (bar) + " . " + juce::String (beat) + " . " + juce::String (sixt), juce::dontSendNotification);

    if (onPosition) onPosition (pos);
}

void TransportBar::paint (juce::Graphics& g)
{
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
        g.setFont (juce::FontOptions (11.5f));
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
    projectButton.setBounds (area.removeFromLeft (72));
    area.removeFromLeft (8);
    optionsButton.setBounds (area.removeFromLeft (72));
    area.removeFromLeft (8);
    bpmCaption.setBounds (area.removeFromLeft (34));
    bpmLabel.setBounds (area.removeFromLeft (76));
    area.removeFromLeft (16);
    startButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (6);
    playButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (6);
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
    positionLabel.setBounds (area.removeFromLeft (120));
    area.removeFromLeft (12);
    const int rightControlsWidth = compact ? 312 : 360;
    const int cpuWidth = std::min (150, std::max (0, area.getWidth() - rightControlsWidth - 12));
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
    browserButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    fileBrowserButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    favoritesButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    pianoRollButton.setBounds (area.removeFromRight (40));
    area.removeFromRight (compact ? 4 : 6);
    computerKeyboardButton.setBounds (area.removeFromRight (40));
}
