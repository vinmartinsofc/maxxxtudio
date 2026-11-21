#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Icons.h"
#include "Theme.h"
#include <functional>

// Vector button with visual states and an accessible name.
class IconButton : public juce::Button
{
public:
    IconButton (icons::Icon i, const juce::String& name) : juce::Button (name), icon (i)
    {
        setTooltip (name);
    }
    void setIcon (icons::Icon i);
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    icons::Icon icon;
};

class IconToggleButton : public juce::Button
{
public:
    IconToggleButton (icons::Icon i, const juce::String& name) : juce::Button (name), icon (i)
    {
        setTooltip (name);
    }

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    icons::Icon icon;
};

// Transport bar: play/pause, stop, loop, BPM, and position (bar.beat.sixteenth).
class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar (SequencerGraph&);
    ~TransportBar() override { stopTimer(); }

    void togglePlay();
    void stop();
    void resumeFromPosition (double beats);
    void setLoopEnabled (bool);
    std::function<void (bool)> onLoopToggled;
    std::function<void (bool)> onBrowserToggled;
    std::function<void (bool)> onFileBrowserToggled;
    std::function<void (bool)> onFavoritesToggled;
    std::function<void (bool)> onPianoRollToggled;
    std::function<void (bool)> onComputerMidiKeyboardToggled;
    std::function<void (double beats)> onPosition;   // Called about 30 times per second with the playhead.
    std::function<void()> onGoToStart, onGoToEnd;
    std::function<void (double bpm)> onBpmChanged;
    std::function<void()> onBeforePlay;
    std::function<void()> onBeforePause, onBeforeStop;
    void setBrowserVisible (bool);
    void setFileBrowserVisible (bool);
    void setFavoritesVisible (bool);
    void setPianoRollVisible (bool);
    void setComputerMidiKeyboardEnabled (bool);
    void setBpm (double);
    void setMetronomeState (bool enabled, float gain);

    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onProjectMenu;
    std::function<void()> onOptionsMenu;

private:
    class CpuTooltipButton final : public juce::TextButton
    {
    public:
        CpuTooltipButton () : juce::TextButton ("") {}
        void paintButton (juce::Graphics&, bool, bool) override {}
    };

    void timerCallback() override;
    void updateButtons();

    SequencerGraph& graph;
    IconButton playButton { icons::Icon::Play, "Play" };
    IconButton stopButton { icons::Icon::Stop, "Stop" };
    IconButton startButton { icons::Icon::Rewind, "Go to Start" };
    IconButton endButton { icons::Icon::FastForward, "Go to End" };
    IconButton projectButton { icons::Icon::Folder, "Project" };
    IconButton optionsButton { icons::Icon::Settings, "Options" };
    IconToggleButton metronomeButton { icons::Icon::Metronome, "Metronome" };
    juce::Slider metronomeVolumeSlider;
    IconToggleButton loopButton { icons::Icon::Loop, "Loop" };
    IconToggleButton browserButton { icons::Icon::Vst, "VST Plugins" };
    IconToggleButton fileBrowserButton { icons::Icon::AudioFile, "Audio" };
    IconToggleButton favoritesButton { icons::Icon::Favorites, "Favorites" };
    IconToggleButton pianoRollButton { icons::Icon::PianoRoll, "Piano Roll" },
                     computerKeyboardButton { icons::Icon::ComputerKeyboard, "Computer MIDI Keyboard" };
    juce::Label bpmLabel, positionLabel, bpmCaption { {}, "BPM" };
    juce::Rectangle<int> cpuMeterBounds;
    CpuTooltipButton cpuTooltipArea;
    double displayedCpuAverage = 0.0;
    double lastPosition = -1.0;
};
