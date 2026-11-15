#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ProjectModel.h"
#include "Theme.h"
#include <functional>
#include <memory>
#include <vector>

// Vista de arranjo: cabeçalhos de trilha, régua com o loop, e os clips na linha do tempo.
class ArrangementView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ArrangementView (ProjectModel&);
    ~ArrangementView() override;

    std::function<void (double beats)>  onSeek;
    std::function<float (int trackId)>  meterProvider;
    std::function<void()> onMasterPeakReset;
    std::function<juce::String (int)>   deviceSummary;
    std::function<bool (const juce::String& pluginIdentifier, int trackId)> canAcceptPluginDrop;
    std::function<void (const juce::String& pluginIdentifier, int trackId, double beats)> onPluginDropped;
    std::function<bool (const juce::File&, int trackId)> canAcceptAudioDrop;
    std::function<void (const juce::File&, int trackId, double beats)> onAudioDropped;
    std::function<void (const juce::File&)> onLocateAudioFileRequested;
    std::function<void (int trackId, int clipId)> onAudioClipDoubleClicked;
    std::function<void (double startBeat, double endBeat, int firstTrack, int lastTrack)> onConsolidateRequested;
    std::function<void()> onGridChanged;
    std::function<void (bool)> onLoopToggleRequested;
    std::function<void (double beats)> onLocatorLaunchRequested;
    std::function<void (double beats)> onArrangementSeekRequested;

    void setPlayhead (double beats);
    void setLoopEnabled (bool on);
    void setGridBeat (double beats);
    void refresh();                       // recria os cabeçalhos
    void grabFocus();
    void zoom (double factor);
    double gridBeats() const { return gridBeat; }
    double snapBeat (double beat) const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerWidth = 210, rulerHeight = 28, trackHeight = 94;

private:
    class Lanes;
    class Ruler;
    class Header;
    class MasterHeader;
    friend class Lanes;
    friend class Ruler;
    friend class Header;
    friend class MasterHeader;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncPositions();
    void updateSizes();
    void updateLocatorControls();
    void navigateLocator (bool forward);
    void zoomAt (double factor, double anchorBeat, int anchorScreenX);
    static juce::Colour colourFor (int trackId);

    ProjectModel& project;
    double pixelsPerBeat = 22.0;          // pixels por beat
    double playhead = 0.0;
    bool loopEnabled = true;
    double gridBeat = 0.25;
    double lastGridBeat = 0.25;

    std::unique_ptr<Lanes> lanes;
    std::unique_ptr<Ruler> ruler;
    std::vector<std::unique_ptr<Header>> headers;
    std::unique_ptr<MasterHeader> masterHeader;
    juce::Viewport viewport;
    juce::Component headerHolder, rulerHolder;
    juce::TextButton addTrackButton { "+ MIDI" }, addAudioTrackButton { "+ Audio" };
    juce::TextButton locatorPreviousButton { "<" }, locatorSetButton { "+" },
                     locatorNextButton { ">" }, locatorDeleteButton { "Del" };
    juce::ComboBox gridBox;
};
