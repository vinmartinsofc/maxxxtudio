#include "TransportBar.h"
#include <cmath>

void IconButton::setIcon (icons::Icon i)
{
    if (icon == i)
        return;

    icon = i;
    const auto name = icon == icons::Icon::Pause ? "Pausar" : "Reproduzir";
    setName (name);
    setTooltip (name);
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = icon == icons::Icon::Pause;
    g.setColour (down ? theme::pressed() : over ? theme::hover() : theme::control());
    g.fillRoundedRectangle (r, 4.0f);

    if (active)
    {
        g.setColour (theme::accent());
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    const auto iconColour = ! isEnabled() ? theme::textDim().withAlpha (0.4f)
                                         : active ? theme::accent()
                                                  : over ? juce::Colours::white : theme::text();
    if (const auto* drawable = icons::get (icon, iconColour, 24))
        drawable->drawWithin (g, r.reduced (8.0f, 6.0f), juce::RectanglePlacement::centred, 1.0f);
    else
    {
        g.setColour (iconColour);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("?", r, juce::Justification::centred);
    }
}

TransportBar::TransportBar (SequencerGraph& g) : graph (g)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (rewindButton);
    addAndMakeVisible (projectButton);
    addAndMakeVisible (metronomeButton);
    addAndMakeVisible (metronomeVolumeSlider);
    for (auto* b : std::initializer_list<juce::Component*> { &playButton, &stopButton, &rewindButton }) b->setMouseClickGrabsKeyboardFocus (false);
    metronomeButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setMouseClickGrabsKeyboardFocus (false);
    projectButton.setTooltip ("Abrir, salvar ou criar um projeto");
    projectButton.onClick = [this] { if (onProjectMenu) onProjectMenu(); };
    metronomeButton.setTooltip ("Ativar ou desativar o metronomo");
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (graph.isMetronomeEnabled(), juce::dontSendNotification);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    metronomeButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    metronomeButton.onClick = [this] { graph.setMetronomeEnabled (metronomeButton.getToggleState()); };
    metronomeVolumeSlider.setName ("Volume do metronomo");
    metronomeVolumeSlider.setTooltip ("Volume do metronomo");
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
    loopButton.setMouseClickGrabsKeyboardFocus (false);
    browserButton.setMouseClickGrabsKeyboardFocus (false);
    fileBrowserButton.setMouseClickGrabsKeyboardFocus (false);
    favoritesButton.setMouseClickGrabsKeyboardFocus (false);
    pianoRollButton.setMouseClickGrabsKeyboardFocus (false);
    computerKeyboardButton.setMouseClickGrabsKeyboardFocus (false);
    rewindButton.onClick = [this] { rewind(); };
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
    loopButton.setTooltip ("Ativar ou desativar o loop");

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (graph.isLoop(), juce::dontSendNotification);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    loopButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    loopButton.onClick = [this] { setLoopEnabled (loopButton.getToggleState()); };

    browserButton.setTooltip ("Mostrar ou ocultar o navegador de plugins (Ctrl+B)");
    browserButton.setClickingTogglesState (true);
    browserButton.setToggleState (true, juce::dontSendNotification);
    browserButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    browserButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    browserButton.onClick = [this]
    {
        if (onBrowserToggled)
            onBrowserToggled (browserButton.getToggleState());
    };

    fileBrowserButton.setTooltip ("Mostrar ou ocultar o navegador de arquivos de audio");
    fileBrowserButton.setClickingTogglesState (true);
    fileBrowserButton.setToggleState (false, juce::dontSendNotification);
    fileBrowserButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    fileBrowserButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    fileBrowserButton.onClick = [this]
    {
        if (onFileBrowserToggled)
            onFileBrowserToggled (fileBrowserButton.getToggleState());
    };

    favoritesButton.setTooltip ("Mostrar colecoes de favoritos por cor");
    favoritesButton.setClickingTogglesState (true);
    favoritesButton.setToggleState (false, juce::dontSendNotification);
    favoritesButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    favoritesButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    favoritesButton.onClick = [this]
    {
        if (onFavoritesToggled)
            onFavoritesToggled (favoritesButton.getToggleState());
    };

    pianoRollButton.setTooltip ("Mostrar ou ocultar o painel inferior do piano roll e dispositivos");
    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setToggleState (true, juce::dontSendNotification);
    pianoRollButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    pianoRollButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    pianoRollButton.onClick = [this]
    {
        if (onPianoRollToggled)
            onPianoRollToggled (pianoRollButton.getToggleState());
    };

    computerKeyboardButton.setTooltip ("Teclado MIDI do computador (M; Z/X oitava; C/V velocidade)");
    computerKeyboardButton.setClickingTogglesState (true);
    computerKeyboardButton.setColour (juce::TextButton::buttonOnColourId, theme::accent());
    computerKeyboardButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
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
        graph.setBpm (v);
        bpmLabel.setText (juce::String (v, 2), juce::dontSendNotification);
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

void TransportBar::rewind() { graph.rewind(); }

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
    graph.setBpm (std::clamp (bpm, 20.0, 999.0));
    bpmLabel.setText (juce::String (graph.getBpm(), 2), juce::dontSendNotification);
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
    displayedCpuCurrent = graph.audioCpuCurrent();
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
        const auto loadColour = displayedCpuCurrent >= 1.0 ? juce::Colour (0xffe34b4b)
                               : displayedCpuCurrent >= 0.75 ? juce::Colour (0xffe6a845)
                                                             : theme::accent();

        g.setColour (theme::control());
        g.fillRoundedRectangle (meter, 4.0f);
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("CPU " + juce::String (static_cast<int> (std::lround (displayedCpuCurrent * 100.0)))
                    + "%  med " + juce::String (static_cast<int> (std::lround (displayedCpuAverage * 100.0))) + "%",
                    meter.toNearestInt().withTrimmedBottom (5), juce::Justification::centred, true);
        g.setColour (theme::border());
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (loadColour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * static_cast<float> (std::min (displayedCpuCurrent, 1.0))),
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
    bpmCaption.setBounds (area.removeFromLeft (34));
    bpmLabel.setBounds (area.removeFromLeft (76));
    area.removeFromLeft (16);
    rewindButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (6);
    playButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (6);
    stopButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (12);
    loopButton.setBounds (area.removeFromLeft (56));
    area.removeFromLeft (6);
    metronomeButton.setBounds (area.removeFromLeft (88));
    area.removeFromLeft (6);
    metronomeVolumeSlider.setBounds (area.removeFromLeft (68).reduced (0, 8));
    area.removeFromLeft (12);
    positionLabel.setBounds (area.removeFromLeft (120));
    area.removeFromLeft (12);
    const int cpuWidth = std::min (150, std::max (0, area.getWidth() - (compact ? 424 : 438)));
    if (cpuWidth >= 100)
    {
        cpuMeterBounds = area.removeFromLeft (cpuWidth);
        area.removeFromLeft (12);
    }
    else
    {
        cpuMeterBounds = {};
    }
    browserButton.setBounds (area.removeFromRight (compact ? 74 : 88));
    area.removeFromRight (compact ? 4 : 6);
    fileBrowserButton.setBounds (area.removeFromRight (compact ? 58 : 72));
    area.removeFromRight (compact ? 4 : 6);
    favoritesButton.setBounds (area.removeFromRight (compact ? 88 : 100));
    area.removeFromRight (compact ? 4 : 6);
    pianoRollButton.setBounds (area.removeFromRight (compact ? 94 : 104));
    area.removeFromRight (compact ? 4 : 6);
    computerKeyboardButton.setBounds (area.removeFromRight (38));
}
