#pragma once
#include <juce_events/juce_events.h>
#include "Arrangement.h"
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

// Estado do projeto: trilhas, clips, selecao e regiao de loop. Vive na thread principal.
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
        int selectedLocatorId = -1, songStartLocatorId = -1;
    };

    ProjectModel();
    ~ProjectModel() override = default;

    std::unique_ptr<roll::Track> masterTrack;
    std::vector<std::unique_ptr<roll::Track>> tracks;
    std::vector<int> selectedTrackIds;
    int selectedTrackId = -1, selectedClipId = -1, selectedAudioClipId = -1;
    double loopStart = 0.0, loopEnd = 16.0;
    double totalBeats = 256.0;                       // 64 compassos visiveis (cresce com os clips)
    std::vector<ArrangementLocator> locators;
    int selectedLocatorId = -1, songStartLocatorId = -1;

    std::function<void (int trackId)> onTrackAdded, onTrackRemoved;   // removed: antes de apagar
    std::function<void (int trackId)> onTrackNotes;                   // notas da trilha mudaram
    std::function<void (int trackId)> onAudioClipsChanged;
    std::function<void()> onMixerChanged;
    std::function<void()> onSelectionChanged;                         // trilha/clip selecionados mudaram
    std::function<void()> onLoopChanged;

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
    bool resizeAudioClip (int trackId, int clipId, double length);
    bool setAudioClipFades (int trackId, int clipId, double fadeInBeats, double fadeOutBeats);
    bool setAudioClipFadeCurves (int trackId, int clipId, double fadeInCurve, double fadeOutCurve);
    bool setAudioClipSourceBpm (int trackId, int clipId, double sourceBpm);
    bool setAudioClipWarpEnabled (int trackId, int clipId, bool enabled);
    bool setClipEnabled (int trackId, int clipId, bool audio, bool enabled);
    bool setAutomationEnvelope (int trackId, roll::AutomationEnvelope);
    bool removeAutomationEnvelope (int trackId, const roll::AutomationTarget&);
    // Move para outra trilha/posicao, substituindo apenas o trecho sobreposto.
    // Devolve o novo id do clip (ou -1 se os argumentos forem invalidos).
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
    void endEdit();
    bool undo();
    bool redo();
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
        int selectedLocatorId = -1, songStartLocatorId = -1;
        int nextLocatorId = 1;
    };

    Snapshot makeSnapshot() const;
    void restoreSnapshot (Snapshot&&);
    void recordUndo();

    int nextTrackId = 1;
    int nextLocatorId = 1;
    std::vector<Snapshot> undoStack, redoStack;
    bool editInProgress = false;
};
