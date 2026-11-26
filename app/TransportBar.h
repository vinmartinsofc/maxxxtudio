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
    void setRecording (bool);
    void cancelCountIn();
    void resumeFromPosition (double beats);
    void setLoopEnabled (bool);
    bool isCountInActive() const noexcept { return countInActive; }
    std::function<void (bool)> onLoopToggled;
    std::function<void (bool)> onBrowserToggled;
    std::function<void (bool)> onFileBrowserToggled;
    std::function<void (bool)> onFavoritesToggled;
    std::function<void (bool)> onPianoRollToggled;
    std::function<void (bool)> onComputerMidiKeyboardToggled;
    std::function<void (bool)> onFollowToggled;
    std::function<void (double beats)> onPosition;   // Called about 30 times per second with the playhead.
    std::function<void()> onGoToStart, onGoToEnd;
    std::function<void (double bpm)> onBpmChanged;
    std::function<void()> onBeforePlay;
    std::function<void()> onBeforePause, onBeforeStop;
    std::function<bool (bool loopWasEnabled)> onRecordStart;
    std::function<void()> onRecordStop;
    void setBrowserVisible (bool);
    void setFileBrowserVisible (bool);
    void setFavoritesVisible (bool);
    void setPianoRollVisible (bool);
    void setComputerMidiKeyboardEnabled (bool);
    void setFollowEnabled (bool);
    juce::Rectangle<int> computerMidiIndicatorBounds() const noexcept { return midiStatusBounds; }
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
    void updateRecordControls();
    void beginCountIn (bool wasPlaying);
    void finishCountIn();
    void restoreCountInMetronome();
    void updateCountInDisplay (double remainingBeats);

    SequencerGraph& graph;
    IconButton playButton { icons::Icon::Play, "Play" };
    IconButton stopButton { icons::Icon::Stop, "Stop" };
    IconToggleButton recordButton { icons::Icon::Record, "Record" };
    juce::ComboBox countInSelector;
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
    IconToggleButton followButton { icons::Icon::Follow, "Follow Arrangement" };
    juce::Label bpmLabel, positionLabel, bpmCaption { {}, "BPM" };
    juce::Rectangle<int> cpuMeterBounds;
    juce::Rectangle<int> midiStatusBounds;
    CpuTooltipButton cpuTooltipArea;
    double displayedCpuAverage = 0.0;
    double lastPosition = -1.0, lastBpm = -1.0;
    bool recording = false, countInActive = false;
    bool countInWasPlaying = false, countInLoopWasEnabled = false;
    bool countInMetronomeWasEnabled = false;
    double countInStartBeat = 0.0, countInRemainingBeats = 0.0;
    double countInLastUpdateSeconds = 0.0;
    int countInBeats = 4, activeCountInBeats = 0, displayedCountInBeat = 0;
};
