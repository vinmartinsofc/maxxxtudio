#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Theme.h"
#include "ThemeKnobSlider.h"
#include <functional>

class AudioClipKnobSlider final : public ThemeKnobSlider {};

class AudioClipEditor : public juce::Component
{
public:
    AudioClipEditor();
    void setClip (const roll::AudioPlacement*, std::shared_ptr<const AudioFileData>,
                  double projectBpm, double gridBeats);

    std::function<bool (double sourceBpm)> onSourceBpmChanged;
    std::function<void (bool)> onWarpChanged;
    std::function<bool (const std::vector<roll::AudioWarpMarker>&)> onWarpMarkersChanged;
    std::function<void()> onSplitAtPlayhead;
    std::function<bool()> onJoinWithNext;
    std::function<bool (double fadeInBeats, double fadeOutBeats)> onFadesChanged;
    std::function<bool (double pitchSemitones, double pitchCents,
                        double gainDb, int warpMode, bool reverse)> onPlaybackChanged;
    std::function<void (juce::String parameterName, double value)> onAutomationParameterChanged;
    std::function<void()> onAutomationGestureStarted, onAutomationGestureEnded;
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void applyPlaybackProperties();
    juce::Rectangle<int> waveformBounds() const;
    double beatAtWaveformX (float x) const;
    float waveformXAtBeat (double beat) const;
    const roll::AudioPlacement* clip = nullptr;
    std::shared_ptr<const AudioFileData> source;
    double projectBpm = 120.0, gridBeats = 0.25;
    double waveformZoom = 1.0;
    double waveformViewStartBeat = 0.0;
    int draggedWarpMarker = -1;
    float dragStartX = 0.0f;
    double dragStartSourceSeconds = 0.0;
    bool navigationPending = false, navigationDragging = false;
    juce::Point<int> navigationLastScreenPosition;
    double navigationAnchorBeat = 0.0, navigationAnchorProportion = 0.0;
    juce::Label fileName, tempoInfo, bpmLabel { {}, "Source BPM" };
    AudioClipKnobSlider sourceBpm;
    juce::ToggleButton warpButton { "Warp" };
    juce::ToggleButton reverseButton { "Reverse" };
    juce::ComboBox warpModeBox;
    AudioClipKnobSlider pitchSlider, centsSlider, gainSlider;
    juce::TextButton autoWarpButton { "Auto-Warp" };
    juce::TextButton doubleBpmButton { "x2" }, halveBpmButton { "/2" };
    juce::TextButton splitButton { "Split" }, joinButton { "Merge" };
    juce::Label fadeInLabel { {}, "Fade In" }, fadeOutLabel { {}, "Fade Out" };
    AudioClipKnobSlider fadeIn, fadeOut;
    juce::TextButton closeButton { "Close Editor" };
};
