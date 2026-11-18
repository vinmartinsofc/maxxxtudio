#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Icons.h"
#include "ProjectModel.h"
#include "Theme.h"
#include <functional>
#include <memory>
#include <vector>

// Vista de arranjo: cabecalhos de trilha, regua com o loop, e os clips na linha do tempo.
class ArrangementView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    struct AutomationParameterChoice
    {
        roll::AutomationTarget target;
        juce::String label;
    };

    struct LiveMixerValues
    {
        float gain = 1.0f;
        float pan = 0.0f;
        bool muted = false;
    };

    explicit ArrangementView (ProjectModel&);
    ~ArrangementView() override;

    std::function<void (double beats)>  onSeek;
    std::function<float (int trackId)>  meterProvider;
    std::function<bool()> playbackStateProvider;
    std::function<bool()> computerMidiKeyboardEnabledProvider;
    std::function<void()> onMasterPeakReset;
    std::function<juce::String (int)>   deviceSummary;
    std::function<bool (const juce::String& pluginIdentifier, int trackId)> canAcceptPluginDrop;
    std::function<void (const juce::String& pluginIdentifier, int trackId, double beats)> onPluginDropped;
    std::function<bool (const juce::File&, int trackId)> canAcceptAudioDrop;
    std::function<void (const juce::File&, int trackId, double beats)> onAudioDropped;
    std::function<void (const juce::File&)> onLocateAudioFileRequested;
    std::function<void (int trackId, int clipId)> onAudioClipDoubleClicked;
    std::function<void (int trackId)> onTrackDuplicateRequested;
    std::function<LiveMixerValues (int trackId)> liveMixerProvider;
    std::function<std::vector<AutomationParameterChoice> (int trackId)> automationParametersProvider;
    std::function<void (double startBeat, double endBeat, int firstTrack, int lastTrack)> onConsolidateRequested;
    std::function<void()> onGridChanged;
    std::function<void (bool)> onLoopToggleRequested;
    std::function<void (double beats)> onLocatorLaunchRequested;
    std::function<void (double beats)> onArrangementSeekRequested;
    std::function<void (const juce::String&, icons::Icon, bool active, int holdMilliseconds)> onInteractionChanged;

    void setPlayhead (double beats);
    void scrollToBeat (double beats);
    void setBpm (double beatsPerMinute);
    void setLoopEnabled (bool on);
    void setGridBeat (double beats);
    bool getTimeSelectionStart (double& startBeat) const;
    bool hasSelectionToDuplicate() const;
    void duplicateSelection();
    void refresh();                       // recria os cabecalhos
    void grabFocus();
    void toggleAutomationMode();
    void toggleAutomationModeFromKey();
    void zoom (double factor);
    double gridBeats() const { return gridBeat; }
    double snapBeat (double beat) const;
    void setAdaptiveGrid (bool enabled);
    void navigateLocator (bool forward);
    void addLocatorAtPlayhead();
    void deleteSelectedLocator();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerWidth = 210, overviewHeight = 20, rulerHeight = 28, timeRulerHeight = 24,
                         trackHeight = 94, collapsedTrackHeight = 24;

private:
    class Lanes;
    class Ruler;
    class TimeRuler;
    class Overview;
    class Header;
    class MasterHeader;
    friend class Lanes;
    friend class Ruler;
    friend class TimeRuler;
    friend class Overview;
    friend class Header;
    friend class MasterHeader;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncPositions();
    void updateSizes();
    void updateLocatorControls();
    void zoomAt (double factor, double anchorBeat, int anchorScreenX);
    void updateAdaptiveGrid();
    void updateGridSelection();
    void setAutomationMode (bool enabled);
    void updateAutomationStatus();
    void showAutomationTargetMenu();
    void updateAutomationTargetButton();
    void selectTrackFromHeader (int trackId, juce::ModifierKeys modifiers);
    int rowTop (int row) const;
    int rowHeight (int row) const;
    int rowAt (int y) const;
    int rowInsertionIndexAt (int y) const;
    void toggleTrackCollapsed (int trackId);
    static juce::Colour colourFor (int trackId);
    static juce::Colour colourFor (const roll::Track&);

    ProjectModel& project;
    double pixelsPerBeat = 22.0;          // pixels por beat
    double playhead = 0.0;
    double bpm = 120.0;
    bool loopEnabled = true;
    double gridBeat = 0.25;
    double lastGridBeat = 0.25;
    bool adaptiveGridEnabled = false;

    std::unique_ptr<Lanes> lanes;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<TimeRuler> timeRuler;
    std::unique_ptr<Overview> overview;
    std::vector<std::unique_ptr<Header>> headers;
    std::unique_ptr<MasterHeader> masterHeader;
    juce::Viewport viewport;
    juce::Component headerHolder, rulerHolder, timeRulerHolder;
    juce::Label timeReadout;
    juce::TextButton addTrackButton { "+ MIDI" }, addAudioTrackButton { "+ Audio" };
    juce::TextButton locatorPreviousButton { "<" }, locatorSetButton { "+" }, locatorNextButton { ">" },
                     locatorDeleteButton { "Del" };
    juce::ComboBox gridBox;
    juce::TextButton automationModeButton { "A" }, automationTargetButton { "Volume" };
    juce::Label automationStatusLabel;
    roll::AutomationTarget automationTarget;
    bool automationMode = false;
    bool automationModeKeyDown = false;
    int trackReorderSourceId = -1;
    int trackReorderDropIndex = -1;
    int trackSelectionAnchorId = -1;
};
