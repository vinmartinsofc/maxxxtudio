#pragma once
#include <juce_events/juce_events.h>
#include "Arrangement.h"
#include "PluginState.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ArrangementLocator
{
    int id = 0;
    double beat = 0.0;
    std::string name, info;
};

enum class MidiMappingTarget : int
{
    TrackVolume = 0,
    TrackPan = 1,
    TrackMute = 2,
    TrackSolo = 3,
    MasterVolume = 4,
    MasterPan = 5
};

enum class MidiMappingMessageType : int
{
    ControlChange = 0,
    Note = 1
};

struct MidiMapping
{
    int trackId = 0;
    MidiMappingTarget target = MidiMappingTarget::TrackVolume;
    MidiMappingMessageType messageType = MidiMappingMessageType::ControlChange;
    int channel = 1;
    int number = 0;
};

// Project state: tracks, clips, selection, and loop region. Lives on the main thread.
// O engine de audio e as views ouvem os callbacks.
class ProjectModel : public juce::ChangeBroadcaster
{
public:
    struct State
    {
        roll::Track master;
        std::vector<roll::Track> tracks;
        std::vector<int> selectedTrackIds;
        int selectedTrackId = -1, selectedClipId = -1, selectedAudioClipId = -1;
        double loopStart = 0.0, loopEnd = 16.0, totalBeats = 256.0;
        std::vector<ArrangementLocator> locators;
        std::vector<MidiMapping> midiMappings;
        int selectedLocatorId = -1, songStartLocatorId = -1;
    };

    ProjectModel();
    ~ProjectModel() override = default;

    std::unique_ptr<roll::Track> masterTrack;
    std::vector<std::unique_ptr<roll::Track>> tracks;
    std::vector<int> selectedTrackIds;
    int selectedTrackId = -1, selectedClipId = -1, selectedAudioClipId = -1;
    double loopStart = 0.0, loopEnd = 16.0;
    double totalBeats = 256.0;                       // 64 visible bars (grows with clips).
    std::vector<ArrangementLocator> locators;
    std::vector<MidiMapping> midiMappings;
    int selectedLocatorId = -1, songStartLocatorId = -1;

    std::function<void (int trackId)> onTrackAdded, onTrackRemoved;   // onTrackRemoved: before deletion.
    std::function<void (int trackId)> onTrackNotes;                   // Track notes changed.
    std::function<void (int trackId)> onAudioClipsChanged;
    std::function<void (int trackId)> onAudioClipAutomationChanged;
    std::function<void()> onMixerChanged;
    std::function<void()> onSelectionChanged;                         // Selected track/clip changed.
    std::function<void()> onLoopChanged;
    std::function<std::vector<PluginState>()> capturePluginHistoryState;
    std::function<void (const std::vector<PluginState>&)> restorePluginHistoryState;

    roll::Track*     track (int id) const;
    roll::Track*     selectedTrack() const { return track (selectedTrackId); }
    bool isTrackSelected (int id) const;
    roll::Placement* selectedClip() const;
    int  trackIndex (int id) const;

    roll::Track* addTrack (roll::TrackType type = roll::TrackType::Midi);
    void removeTrack (int id);
    bool moveTrack (int id, int newIndex);
    int duplicateTrack (int id);
    int groupTracks (const std::vector<int>& trackIds);
    bool ungroupTrack (int groupId);
    void selectTrack (int id, bool toggleSelection = false);
    void selectTrackRange (int anchorTrackId, int targetTrackId, bool additive = false);
    void setTrackSolo (int id, bool solo, bool additive);
    void selectClip (int trackId, int clipId);

    roll::Placement* addClip (int trackId, double start, double length);
    int copyClip (int fromTrack, int clipId, int toTrack, double start);
    int copyClip (const roll::Placement& source, int toTrack, double start);
    int copyClipReplacingRange (const roll::Placement& source, int toTrack, double start);
    int duplicateClip (int trackId, int clipId);
    void deleteClip (int trackId, int clipId);
    int splitClip (int trackId, int clipId, double beat);
    roll::AudioPlacement* addAudioClip (int trackId, double start, double length,
                                        const std::string& filePath, const std::string& name,
                                        const std::vector<roll::WaveformRange>& waveform,
                                        double durationSeconds = 0.0, double sourceBpm = 120.0);
    roll::AudioPlacement* addAudioClipReplacingRange (
        int trackId, double start, double length, const std::string& filePath, const std::string& name,
        const std::vector<roll::WaveformRange>& waveform,
        double durationSeconds = 0.0, double sourceBpm = 120.0,
        double edgeFadeBeats = 0.0);
    void deleteAudioClip (int trackId, int clipId);
    int copyAudioClip (int fromTrack, int clipId, int toTrack, double start);
    int copyAudioClip (const roll::AudioPlacement& source, int toTrack, double start);
    int copyAudioClipReplacingRange (const roll::AudioPlacement& source, int toTrack, double start);
    int duplicateAudioClip (int trackId, int clipId);
    int splitAudioClip (int trackId, int clipId, double beat);
    bool joinAudioClips (int trackId, int leftClipId, int rightClipId);
    bool resizeAudioClip (int trackId, int clipId, double length);
    bool resizeAudioClipStart (int trackId, int clipId, double start, double length);
    bool resizeClipStart (int trackId, int clipId, double start, double length);
    bool joinMidiClips (int trackId, int leftClipId, int rightClipId);
    bool setAudioClipFades (int trackId, int clipId, double fadeInBeats, double fadeOutBeats);
    bool setAudioClipFadeCurves (int trackId, int clipId, double fadeInCurve, double fadeOutCurve);
    bool setAudioClipSourceBpm (int trackId, int clipId, double sourceBpm);
    bool setAudioClipWarpMarkers (int trackId, int clipId,
                                  const std::vector<roll::AudioWarpMarker>& markers);
    bool setAudioClipWarpEnabled (int trackId, int clipId, bool enabled);
    bool setAudioClipPlayback (
        int trackId, int clipId, double pitchSemitones, double pitchCents,
        double gainDb, int warpMode, bool reverse);
    bool setClipEnabled (int trackId, int clipId, bool audio, bool enabled);
    bool deleteRange (int trackId, double start, double end);
    bool cropRange (int trackId, double start, double end);
    bool setAutomationEnvelope (int trackId, roll::AutomationEnvelope);
    bool removeAutomationEnvelope (int trackId, const roll::AutomationTarget&);
    // Move to another track/position, replacing only the overlapping section.
    // Returns the new clip ID (or -1 if the arguments are invalid).
    int  moveClip (int fromTrack, int clipId, int toTrack, double newStart);
    int  moveAudioClip (int fromTrack, int clipId, int toTrack, double newStart);
    void selectAudioClip (int trackId, int clipId);
    bool setTrackColour (int trackId, std::uint32_t colour);
    void setTrackRouting (int trackId, roll::TrackOutputRouting routing);
    void notifyTrackNotes (int trackId);
    void notifyMixer()   { if (onMixerChanged) onMixerChanged(); sendChangeMessage(); }
    void setLoop (double s, double e);
    int addLocator (double beat);
    bool moveLocator (int id, double beat);
    bool renameLocator (int id, std::string name);
    bool setLocatorInfo (int id, std::string info);
    bool removeLocator (int id);
    void selectLocator (int id);
    void setSongStartLocator (int id);
    ArrangementLocator* locator (int id);
    const ArrangementLocator* locator (int id) const;
    void growTo (double beats);
    void beginEdit();
    void beginExternalEdit();
    void endEdit();
    void cancelEdit();
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool retainsAudioFile (const std::string& path) const;
    State createState() const;
    void replaceState (State);

private:
    struct Snapshot
    {
        roll::Track master;
        std::vector<roll::Track> tracks;
        std::vector<int> selectedTrackIds;
        int selectedTrackId = -1, selectedClipId = -1, selectedAudioClipId = -1;
        int nextTrackId = 1;
        double loopStart = 0.0, loopEnd = 16.0, totalBeats = 256.0;
        std::vector<ArrangementLocator> locators;
        std::vector<MidiMapping> midiMappings;
        int selectedLocatorId = -1, songStartLocatorId = -1;
        int nextLocatorId = 1;
        std::vector<PluginState> plugins;
        bool includesPluginState = false;
    };

    Snapshot makeSnapshot (bool includePluginState = false) const;
    void restoreSnapshot (Snapshot&&);
    void recordUndo();

    int nextTrackId = 1;
    int nextLocatorId = 1;
    std::vector<Snapshot> undoStack, redoStack;
    std::vector<Snapshot> redoStackBeforeEdit;
    bool editInProgress = false;
};
