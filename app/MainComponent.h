#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "AudioClipEditor.h"
#include "AudioFileBrowser.h"
#include "ArrangementView.h"
#include "BrowserPanel.h"
#include "DeviceView.h"
#include "PianoRollEditor.h"
#include "PluginScanner.h"
#include "ProjectFile.h"
#include "ProjectModel.h"
#include "RollModel.h"
#include "TaskRegistry.h"
#include "TaskStatusBar.h"
#include "TransportBar.h"
#include <array>
#include <map>
#include <thread>

class MainComponent : public juce::Component, public juce::DragAndDropContainer,
                      private juce::ChangeListener, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void setMainWindow (juce::TopLevelWindow*);
    void updatePluginWindowZOrder();
    void openProjectFromFile (const juce::File&);
    void offerRecoveryBeforeOpening (std::function<void()> continueStartup);
    void requestClose (std::function<void()> onCloseApproved);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    class ActionIndicator;
    class ComputerMidiIndicator;
    class PluginWindow;

    void startSimulatedTask();
    void loadDevice (const juce::PluginDescription&, int trackId = -1, int insertionIndex = -1);
    void duplicateSelectedTrack (int trackId);
    void openDeviceEditor (int trackId, int deviceId);
    void updateActionIndicator();
    void setBrowserVisible (bool);
    void setAudioBrowserVisible (bool);
    void setFavoritesVisible (bool);
    void setPianoRollVisible (bool);
    void loadAudioFile (const juce::File&, int trackId, double startBeat);
    void consolidateAudioRange (double startBeat, double endBeat, int firstTrack, int lastTrack);
    void openAudioClipEditor (int trackId, int clipId);
    void refreshAudioClipEditor();
    void publishAudioTrack (int trackId);
    bool selectedTrackSpan (double& start, double& end) const;
    void playSelectedTrack();
    void resumeFromPausedPosition();
    bool handleTransportKeyPress (const juce::KeyPress&);
    void loopSelectedTrack();
    void closeWindowsOfTrack (int trackId);
    void showProjectMenu();
    void showOptionsWindow();
    void chooseProjectFile (bool open, std::function<void (bool)> onSaveComplete = {});
    void chooseWavExportFile();
    void showWavRenderOptions (const juce::File&);
    void startWavRender (const juce::File&, double tailSeconds);
    void setAudioRenderUiEnabled (bool);
    void saveProject (const juce::File&, std::function<void (bool)> onComplete = {});
    void saveRecoverySnapshot();
    void loadProject (const juce::File&, bool recovering = false);
    void applyLoadedProject (const juce::File&, ProjectFileData, const std::shared_ptr<TaskScope>&,
                             bool recovering);
    void advanceProjectLoadTask();
    void finishProjectLoadTask();
    void restoreNextProjectPlugin();
    void loadProjectAudio (const ProjectFileData&);
    void setProjectStatus (const juce::String&);
    void refreshDeviceView();
    void applyMixers();
    void applyScale();
    void applyLength();
    void onSelectionChanged();
    void showDetail (bool devices);
    void cycleFocus();
    void updateComputerMidiKeyboard();
    void setComputerMidiKeyboardEnabled (bool);
    void showComputerMidiStatus (const juce::String&, const juce::String&, bool noteHeld);
    void hideComputerMidiStatus();
    void releaseComputerMidiNotes();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    juce::String deviceSummary (int trackId) const;

    TaskRegistry taskRegistry;
    juce::ThreadPool projectFilePool { 1 };
    AudioEngine engine;
    PluginScanner scanner { engine.formats(), taskRegistry };
    ProjectModel project;
    RollModel model;

    TransportBar transport { engine.graph() };
    TaskStatusBar taskStatusBar { taskRegistry };
    BrowserPanel browser { scanner };
    AudioFileBrowser audioBrowser;
    ArrangementView arrangement { project };
    PianoRollEditor editor { model };
    AudioClipEditor audioClipEditor;
    DeviceView deviceView { taskRegistry };

    // painel do clip
    juce::TextButton clipTab { "Clip" }, deviceTab { "Dispositivos" };
    juce::Label scaleCaption { {}, "Escala" }, lengthCaption { {}, "Tamanho" }, gridCaption { {}, "Grade" }, zoomCaption { {}, "Zoom" };
    juce::ComboBox rootBox, modeBox, lengthBox;
    juce::TextButton scaleButton { "Scale" }, highlightButton { "Destacar (K)" }, foldButton { "Fold" }, foldScaleButton { "Fold escala" },
                     drawButton { "Draw (B)" }, snapButton { "Snap" }, previewButton { "Preview" },
                     zoomHMinus { "H -" }, zoomHPlus { "H +" }, zoomVMinus { "V -" }, zoomVPlus { "V +" };
    juce::Label gridLabel;
    std::vector<juce::Component*> clipControls;

    juce::TopLevelWindow* mainWindow = nullptr;
    std::map<int, std::unique_ptr<PluginWindow>> windows;
    std::map<std::string, std::shared_ptr<const AudioFileData>> audioAssets;
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    std::unique_ptr<juce::FileChooser> audioExportChooser;
    std::unique_ptr<class OptionsWindow> optionsWindow;
    juce::File currentProjectFile;
    double nextRecoverySaveTime = 0.0;
    std::shared_ptr<TaskScope> projectLoadTask;
    std::vector<PluginState> pendingProjectPlugins;
    std::vector<std::pair<PluginState, int>> loadedProjectPlugins;
    size_t nextProjectPlugin = 0;
    int projectLoadToken = 0, projectGeneration = 0;
    int missingProjectPlugins = 0;
    bool waitingForProjectPluginScan = false, restoringProjectPlugins = false;
    std::shared_ptr<std::atomic<bool>> lifetimeToken { std::make_shared<std::atomic<bool>> (true) };
    std::unique_ptr<ActionIndicator> actionIndicator;
    std::unique_ptr<ComputerMidiIndicator> computerMidiIndicator;
    std::jthread taskSimulationThread;
    int nextPluginCascadeIndex = 0;
    int previewLoadToken = 0;
    int pendingAudioLoads = 0, pendingPreviewLoads = 0;
    bool scanningPlugins = false;
    bool projectFileOperationInProgress = false;
    bool recoverySaveInProgress = false;
    bool trackDuplicationInProgress = false;
    bool audioRenderInProgress = false;
    size_t projectLoadWorkTotal = 0, projectLoadWorkDone = 0;
    juce::String scanMessage;

    bool showingDevices = false;
    bool createFadesOnClipEdges = false;
    bool audioClipEditorOpen = false;
    bool pianoRollVisible = true;
    bool suppressSongStartOnce = false;
    bool pendingArrangementSeek = false;
    double pendingArrangementSeekBeat = 0.0;
    double arrangementRatio = 0.42;
    double transportPosition = 0.0;
    double pausedPosition = 0.0;
    double displayedBpm = 120.0;
    juce::Rectangle<int> splitterRect;
    int rightTop = 0, rightHeight = 1;
    bool draggingSplitter = false, draggingBrowserSplitter = false;
    std::uint64_t lastLo = 0, lastHi = 0;
    std::array<bool, 15> computerMidiKeyDown {}, computerMidiNoteActive {};
    std::array<int, 15> computerMidiKeyPitch {}, computerMidiTrackIds {};
    std::array<bool, 4> computerMidiControlKeyDown {};
    juce::String lastComputerMidiNote;
    bool computerMidiModeKeyDown = false, computerMidiKeyboardEnabled = false;
    int computerMidiOctaveOffset = 0, computerMidiVelocity = 100;
    int browserWidth = 300;
    juce::Rectangle<int> browserSplitterRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
