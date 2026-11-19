#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Icons.h"
#include "Theme.h"
#include <functional>

// Botao vetorial com estados visuais e nome acessivel.
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

// Barra de transporte: play/pause, stop, loop, BPM e posicao (compasso.beat.16avo).
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
    std::function<void (double beats)> onPosition;   // chamado ~30x/s com o playhead
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
    IconButton playButton { icons::Icon::Play, "Reproduzir" };
    IconButton stopButton { icons::Icon::Stop, "Parar" };
    IconButton startButton { icons::Icon::Rewind, "Ir para o inicio" };
    IconButton endButton { icons::Icon::FastForward, "Ir para o final" };
    IconButton projectButton { icons::Icon::Folder, "Projeto" };
    IconButton optionsButton { icons::Icon::Settings, "Opcoes" };
    IconToggleButton metronomeButton { icons::Icon::Metronome, "Metronomo" };
    juce::Slider metronomeVolumeSlider;
    IconToggleButton loopButton { icons::Icon::Loop, "Loop" };
    IconToggleButton browserButton { icons::Icon::Vst, "Plugins VST" };
    IconToggleButton fileBrowserButton { icons::Icon::AudioFile, "Audio" };
    IconToggleButton favoritesButton { icons::Icon::Favorites, "Favoritos" };
    IconToggleButton pianoRollButton { icons::Icon::PianoRoll, "Piano Roll" },
                     computerKeyboardButton { icons::Icon::ComputerKeyboard, "Teclado MIDI do computador" };
    juce::Label bpmLabel, positionLabel, bpmCaption { {}, "BPM" };
    juce::Rectangle<int> cpuMeterBounds;
    CpuTooltipButton cpuTooltipArea;
    double displayedCpuAverage = 0.0;
    double lastPosition = -1.0;
};
