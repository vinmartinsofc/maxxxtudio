#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Arrangement.h"
#include "PluginState.h"
#include "Sequencer.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Dados do bloco atual, escritos pelo grafo e lidos pelas trilhas (mesmo thread de audio).
struct BlockShared
{
    roll::BlockPlan plan;
    bool playing = false;
    bool flush = false;       // soltar todas as notas neste bloco (pausa, stop, seek)
};

// Fonte MIDI de UMA trilha: toca as notas dela e mistura o MIDI de pre-escuta.
class TrackSource : public juce::AudioProcessor
{
public:
    explicit TrackSource (const BlockShared& s);

    void publishNotes (const std::vector<roll::Note>& notes);
    void preview (int pitch, bool on, float velocity = 0.8f);
    void activeMask (std::uint64_t& lo, std::uint64_t& hi) const { lo = maskLo; hi = maskHi; }

    const juce::String getName() const override { return "Track MIDI"; }
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    static constexpr size_t maxNotes = 8192;

private:
    const BlockShared& shared;
    roll::Sequencer sequencer;
    std::vector<roll::MidiEvent> events;
    std::vector<roll::Note> localNotes, pendingNotes;
    juce::SpinLock notesLock;
    unsigned notesVersion = 0, localVersion = 0;
    juce::MidiMessageCollector previewCollector;
    juce::MidiBuffer previewBuffer;
    std::atomic<std::uint64_t> maskLo { 0 }, maskHi { 0 };
};

// Volume / pan / mute da trilha (estereo).
class TrackMixer : public juce::AudioProcessor
{
public:
    explicit TrackMixer (const BlockShared&);
    void set (float gain, float pan, bool muted, bool controlMuted);
    void setAutomation (const std::vector<roll::AutomationEnvelope>&);
    float liveGain() const { return liveGainValue.load (std::memory_order_relaxed); }
    float livePan() const { return livePanValue.load (std::memory_order_relaxed); }
    bool liveMute() const { return liveMuteValue.load (std::memory_order_relaxed); }
    float peak() const { return peakLevel; }
    float peakLeft() const { return peakLeftLevel.load (std::memory_order_relaxed); }
    float peakRight() const { return peakRightLevel.load (std::memory_order_relaxed); }
    float rms() const { return rmsLevel.load (std::memory_order_relaxed); }
    void resetPeak();

    const juce::String getName() const override { return "Track Mixer"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    const BlockShared& shared;
    std::atomic<float> targetGain { 1.0f }, targetPan { 0.0f }, peakLevel { 0.0f },
                        peakLeftLevel { 0.0f }, peakRightLevel { 0.0f }, rmsLevel { 0.0f };
    std::atomic<bool> mute { false }, controlMute { false };
    juce::SpinLock automationLock;
    std::vector<roll::AutomationEnvelope> pendingAutomation, localAutomation;
    std::uint64_t pendingAutomationVersion = 0, localAutomationVersion = 0;
    std::atomic<float> liveGainValue { 1.0f }, livePanValue { 0.0f };
    std::atomic<bool> liveMuteValue { false };
    float currentGain = 1.0f;
};

// Grafo principal: dono do relogio do transporte.
class SequencerGraph : public juce::AudioProcessorGraph
{
public:
    // ---- message thread ----
    void setPlaying (bool p)       { playing = p; }
    bool isPlaying() const         { return playing; }
    void setMetronomeEnabled (bool enabled) { metronomeEnabled = enabled; }
    bool isMetronomeEnabled() const { return metronomeEnabled.load(); }
    void setMetronomeGain (float gain) { metronomeGain.store (std::clamp (gain, 0.0f, 1.0f)); }
    float getMetronomeGain() const { return metronomeGain.load(); }
    void rewind()                  { rewindRequested = true; }
    void seek (double beats)       { seekTarget = beats; seekRequested = true; }
    void setBpm (double b)         { bpm = b; }
    double getBpm() const          { return bpm; }
    void setLoop (bool l)          { loopEnabled = l; }
    bool isLoop() const            { return loopEnabled; }
    void setLoopRegion (double s, double e) { loopStart = s; loopEnd = e; }
    double positionBeats() const   { return positionAtomic; }
    double audioCpuCurrent() const { return audioCpuCurrentValue.load (std::memory_order_relaxed); }
    double audioCpuAverage() const { return audioCpuAverageValue.load (std::memory_order_relaxed); }
    const BlockShared& sharedBlock() const { return shared; }

    // ---- audio thread ----
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessorGraph::processBlock;

private:
    roll::TransportClock clock;
    BlockShared shared;
    bool wasPlaying = false;
    std::atomic<bool>   playing { false }, rewindRequested { false }, seekRequested { false }, loopEnabled { true };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<float> metronomeGain { 1.0f };
    std::atomic<double> bpm { 120.0 }, loopStart { 0.0 }, loopEnd { 16.0 }, seekTarget { 0.0 }, positionAtomic { 0.0 };
    std::atomic<double> audioCpuCurrentValue { 0.0 }, audioCpuAverageValue { 0.0 };
    int clickSamplesRemaining = 0;
    double clickPhase = 0.0, clickPhaseStep = 0.0;
    double freeMetronomeBeatPhase = 1.0;
    std::int64_t freeMetronomeBeatNumber = 0;
    float clickEnvelope = 0.0f, clickEnvelopeDecay = 0.0f;
};

struct DeviceInfo
{
    int  id = 0;
    juce::String name;
    juce::String pluginIdentifier;
    bool isInstrument = false;
    bool isBypassed = false;
    int groupId = 0;
    struct Parameter
    {
        int index = -1;
        juce::String id, name;
        float value = 0.0f;
    };
    std::vector<Parameter> automatableParameters;
};

struct PluginParameterGesture
{
    int trackId = 0;
    int deviceId = 0;
    int parameterIndex = -1;
    bool gestureIsStarting = false;
};

struct WaveformColumn
{
    float peak = 0.0f;
    float centroid = 0.0f;
    float flatness = 0.0f;
};

struct AudioFileData
{
    juce::File file;
    std::shared_ptr<juce::AudioBuffer<float>> samples;
    std::vector<roll::WaveformRange> waveform;
    std::vector<float> waveformRms;
    std::vector<WaveformColumn> waveformColumns;
    std::vector<float> spectrum;
    int spectrumColumns = 0;
    int spectrumRows = 0;
    std::vector<double> transientSeconds;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    double estimatedBpm = 0.0;
    juce::String estimatedKey;
};

struct AudioClipPlayback
{
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    double sourceBpm = 120.0;
    double sourceOffsetSeconds = 0.0;
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    double fadeInCurve = 0.0;
    double fadeOutCurve = 0.0;
    double pitchSemitones = 0.0;
    double gainDb = 0.0;
    int warpMode = 0;
    std::shared_ptr<const AudioFileData> source;
    bool warpEnabled = true;
    bool reverse = false;
    std::vector<roll::AutomationEnvelope> automation;
    double durationSeconds = 0.0;
    std::vector<roll::AudioWarpMarker> warpMarkers;
};

class TrackAudioSource : public juce::AudioProcessor
{
public:
    explicit TrackAudioSource (const BlockShared&);
    void publishClips (const std::vector<AudioClipPlayback>&);
    const juce::String getName() const override { return "Track Audio"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    const BlockShared& shared;
    juce::SpinLock clipsLock;
    std::vector<AudioClipPlayback> pendingClips, localClips;
    std::uint64_t pendingVersion = 0, localVersion = 0;
};

class AudioPreviewSource : public juce::AudioProcessor
{
public:
    AudioPreviewSource();
    void play (std::shared_ptr<const AudioFileData>, double startPositionSeconds = 0.0);
    void pause();
    void resume();
    void seek (double positionSeconds);
    void stop();
    double getPositionSeconds() const
    {
        return positionRequestVersion.load() != appliedPositionRequestVersion.load()
            ? requestedPositionSeconds.load() : playheadSeconds.load();
    }
    bool isPlaying() const { return playing.load(); }
    void setGain (float gain) { targetGain.store (std::clamp (gain, 0.0f, 1.0f)); }
    const juce::String getName() const override { return "Audio Preview"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::SpinLock sourceLock;
    std::shared_ptr<const AudioFileData> pendingSource, localSource;
    std::uint64_t pendingVersion = 0, localVersion = 0;
    std::atomic<bool> playing { false };
    std::atomic<bool> hasSource { false };
    std::atomic<double> requestedPositionSeconds { 0.0 }, playheadSeconds { 0.0 };
    std::atomic<std::uint64_t> positionRequestVersion { 0 }, appliedPositionRequestVersion { 0 };
    std::uint64_t localPositionRequestVersion = 0;
    std::atomic<float> targetGain { 0.5f };
    float currentGain = 0.5f;
    double positionSamples = 0.0;
};

class AudioEngine : private juce::ChangeListener
{
public:
    enum class IsolatedEditorResult { notIsolated, opened, noEditor, failed };
    static double estimateAudioBpm (const juce::AudioBuffer<float>&, double sampleRate);
    std::function<void (int trackId, int deviceId, const juce::String& pluginName)> onPluginCrashed;

    static constexpr int masterTrackId = 0;
    static constexpr const char* drumRackPluginIdentifier = "MaXXXtudio.DrumRack";
    static constexpr const char* utilityPluginIdentifier = "MaXXXtudio.Utility";
    static constexpr const char* autoPanPluginIdentifier = "MaXXXtudio.AutoPan";

    AudioEngine();
    ~AudioEngine();

    SequencerGraph& graph()                    { return seqGraph; }
    juce::AudioPluginFormatManager& formats()  { return formatManager; }
    juce::AudioDeviceManager& audioDeviceManager() { return deviceManager; }
    const juce::AudioFormatManager& audioFormats() const { return audioFormatManager; }

    // trilhas
    void addTrack (int trackId, roll::TrackType type = roll::TrackType::Midi);
    void removeTrack (int trackId);
    void publishNotes (int trackId, const std::vector<roll::Note>& notes);
    void publishAudioClips (int trackId, const std::vector<AudioClipPlayback>& clips);
    void setMixer (int trackId, float gain, float pan, bool muted);
    void setMixer (int trackId, float gain, float pan, bool muted, bool controlMuted);
    void setTrackAutomation (int trackId, const std::vector<roll::AutomationEnvelope>&);
    void setPluginAutomationOverride (int trackId, int deviceId, int parameterIndex, bool enabled);
    void setTrackRouting (int trackId, roll::TrackOutputRouting routing, int parentGroupId = 0);
    void setMasterMixer (float gain, float pan, bool muted = false);
    void resetPeak (int trackId);
    void previewNote (int trackId, int pitch, bool on, float velocity = 0.8f);
    void activeMask (int trackId, std::uint64_t& lo, std::uint64_t& hi) const;
    float mixerGain (int trackId) const;
    float mixerPan (int trackId) const;
    bool mixerMuted (int trackId) const;
    float peak (int trackId) const;
    float rms (int trackId) const;
    float masterPeak() const;
    std::pair<float, float> masterPeakStereo() const;

    // dispositivos (plugins) da trilha: o instrumento vem sempre primeiro, efeitos em seguida
    void addDevice (int trackId, const juce::PluginDescription&,
                    std::function<void (const juce::String& error, int deviceId)> onDone,
                    int insertionIndex = -1);
    int addDrumRack (int trackId);
    int addUtility (int trackId);
    void addPadEffect (int trackId, int rackDeviceId, int pad,
                       const juce::PluginDescription&, std::function<void (const juce::String&)> onDone);
    std::unique_ptr<juce::AudioProcessorEditor> createPadEffectEditor (int trackId, int rackDeviceId,
                                                                       int pad, int effectIndex);
    void loadAudioFileAsync (
        const juce::File&,
        std::function<void (const juce::String&, std::shared_ptr<const AudioFileData>)>,
        bool includeWaveformAnalysis = false);
    void analyzeAudioFileAsync (const juce::File&, std::function<void (const juce::String&, std::shared_ptr<const AudioFileData>)>);
    void previewAudio (std::shared_ptr<const AudioFileData> source,
                       double startPositionSeconds = 0.0);
    void pauseAudioPreview();
    void resumeAudioPreview();
    void seekAudioPreview (double positionSeconds);
    void stopAudioPreview();
    double getAudioPreviewPositionSeconds() const;
    bool isAudioPreviewPlaying() const;
    void setAudioPreviewGain (float gain);
    bool beginOfflineRender (juce::String& error);
    bool beginOfflineTrackRender (int trackId, juce::String& error);
    juce::Result renderMasterToWav (const juce::File&,
                                   double durationSeconds,
                                   std::function<void (double)> onProgress,
                                   std::function<bool()> shouldCancel);
    juce::Result renderTrackRange (double startBeat,
                                   double lengthBeats,
                                   juce::AudioBuffer<float>& destination,
                                   std::function<void (double)> onProgress,
                                   std::function<bool()> shouldCancel);
    double offlineRenderSampleRate() const { return offlineRenderRate; }
    void finishOfflineRender();
    bool canAcceptPlugin (int trackId, const juce::PluginDescription&) const;
    void removeDevice (int trackId, int deviceId);
    bool moveDevice (int trackId, int deviceId, int direction);
    bool reorderDevice (int trackId, int deviceId, int targetDeviceId, bool afterTarget);
    bool groupDevices (int trackId, const std::vector<int>& deviceIds);
    bool ungroupDevices (int trackId, const std::vector<int>& deviceIds);
    void setDeviceBypassed (int trackId, int deviceId, bool bypassed);
    std::vector<DeviceInfo> devices (int trackId) const;
    juce::String pluginParameterValueText (int trackId, const roll::AutomationTarget&,
                                           double normalizedValue) const;
    std::vector<PluginParameterGesture> consumePluginParameterGestures();
    std::vector<PluginState> getPluginStates();
    bool restorePluginState (int trackId, int deviceId, const juce::MemoryBlock&);
    IsolatedEditorResult showIsolatedDeviceEditor (int trackId, int deviceId,
                                                    int editorX = -1, int editorY = -1);
    IsolatedEditorResult showIsolatedPadEffectEditor (int trackId, int rackDeviceId,
                                                       int pad, int effectIndex,
                                                       int editorX = -1, int editorY = -1);
    std::unique_ptr<juce::AudioProcessorEditor> createDeviceEditor (int trackId, int deviceId);
    void saveAudioDeviceSettings();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Device
    {
        int id;
        juce::AudioProcessorGraph::Node::Ptr node;
        juce::String name, pluginIdentifier;
        bool instrument;
        int groupId = 0;
    };
    struct TrackNodes
    {
        juce::AudioProcessorGraph::Node::Ptr source, mixer;
        juce::AudioProcessorGraph::Node::Ptr automationNode;
        TrackSource* sourcePtr = nullptr;
        TrackAudioSource* audioSourcePtr = nullptr;
        TrackMixer*  mixerPtr = nullptr;
        juce::AudioProcessor* automationProcessor = nullptr;
        std::vector<roll::AutomationEnvelope> automation;
        bool audioTrack = false;
        bool isMaster = false;
        bool isGroup = false;
        int parentGroupId = 0;
        roll::TrackOutputRouting routing = roll::TrackOutputRouting::Master;
        std::vector<Device> devices;
    };

    TrackNodes* find (int trackId);
    const TrackNodes* find (int trackId) const;
    bool beginOfflineRenderInternal (juce::String& error, int captureTrackId);
    void rewire (TrackNodes&);
    void rewireMaster();
    void clearConnectionsOf (juce::AudioProcessorGraph::NodeID);
    void clearOutputConnectionsOf (juce::AudioProcessorGraph::NodeID);

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::AudioFormatManager audioFormatManager;
    juce::ThreadPool audioLoadPool { 1 };
    SequencerGraph seqGraph;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr previewNode;
    juce::AudioProcessorGraph::Node::Ptr offlineCaptureNode;
    juce::AudioProcessor* offlineCaptureProcessor = nullptr;
    AudioPreviewSource* previewSourcePtr = nullptr;
    std::map<int, TrackNodes> tracks;
    std::set<std::pair<int, std::string>> pendingPluginLoads;
    int nextDeviceId = 1, nextDeviceGroupId = 1;
    bool offlineRenderActive = false, previousLoopEnabled = true, previousMetronomeEnabled = false;
    double previousPosition = 0.0;
    float previousMetronomeGain = 1.0f;
    double offlineRenderRate = 44100.0;
    int offlineRenderBlockSize = 512;
};
