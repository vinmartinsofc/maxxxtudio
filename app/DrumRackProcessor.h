#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

class DrumRackEditor;

class DrumRackProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int firstMidiNote = 60;
    static constexpr int numPads = 15;
    static constexpr int automatablePadParameterCount = 7;
    static constexpr int visiblePadCount = numPads;
    static constexpr const char* pluginIdentifier = "MaXXXtudio.DrumRack";
    static constexpr std::array<std::pair<int, int>, numPads> computerMidiKeyboardMap {{
        { 'A', 0 }, { 'W', 1 }, { 'S', 2 }, { 'E', 3 }, { 'D', 4 },
        { 'F', 5 }, { 'T', 6 }, { 'G', 7 }, { 'Y', 8 }, { 'H', 9 },
        { 'U', 10 }, { 'J', 11 }, { 'K', 12 }, { 'O', 13 }, { 'L', 14 }
    }};

    enum class PlaybackMode
    {
        OneShot,
        Classic
    };

    DrumRackProcessor();
    ~DrumRackProcessor() override = default;

    bool loadPadFromFile (int pad, const juce::File& file, juce::String& error);
    void clearPad (int pad);
    void clearPads();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    std::uint32_t consumeTriggeredPads() { return triggeredPadMask.exchange (0, std::memory_order_relaxed); }
    juce::String padName (int pad) const;
    int padSampleLength (int pad) const;
    struct PadEffectInfo
    {
        juce::String name, identifier;
        bool bypassed = false;
    };
    std::vector<PadEffectInfo> padEffects (int pad) const;
    bool addPadEffect (int pad, const juce::PluginDescription&,
                       std::unique_ptr<juce::AudioProcessor>, juce::String& error);
    bool removePadEffect (int pad, int effectIndex);
    bool movePadEffect (int pad, int effectIndex, int direction);
    bool setPadEffectBypassed (int pad, int effectIndex, bool bypassed);
    std::shared_ptr<juce::AudioProcessor> padEffectProcessor (int pad, int effectIndex) const;
    void collectRetiredPadEffects();
    juce::String stateXml() const;
    bool restoreStateXml (const juce::XmlElement& xml);
    bool stateRestoreSucceeded() const { return stateRestoreSucceededFlag; }

    const juce::String getName() const override { return "Drum Master"; }
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    struct PadSettings
    {
        float gain = 1.0f;
        int pitchSemitones = 0;
        int startSample = 0;
        int endSample = -1;
        bool reverse = false;
        bool loop = false;
        PlaybackMode mode = PlaybackMode::OneShot;
    };

    void setPadSettings (int pad, const PadSettings& settings);
    PadSettings padSettings (int pad) const;

private:
    class PadAutomationParameter;
    enum class PadParameterKind { Gain, Pitch, Start, End, Reverse, Loop, ClassicMode };

    struct Pad
    {
        juce::File file;
        std::shared_ptr<const juce::AudioBuffer<float>> samples;
        double sampleRate = 0.0;
        PadSettings settings;
    };

    using PadSnapshot = std::array<Pad, numPads>;

    struct Voice
    {
        std::shared_ptr<const juce::AudioBuffer<float>> samples;
        double sampleRate = 0.0;
        double position = 0.0;
        double pitchRatio = 1.0;
        float gain = 0.0f;
        int startOffset = 0;
        int endOffset = 0;
        bool reverse = false;
        bool loop = false;
        bool classicMode = false;
        bool active = false;
        int triggerSample = 0;
        int midiNote = -1;
        int releaseSample = 0;
        int releaseSamplesRemaining = 0;
        bool releasePending = false;
        bool releasing = false;
        int pad = -1;
    };

    struct PadEffect
    {
        juce::PluginDescription description;
        std::shared_ptr<juce::AudioProcessor> processor;
        juce::AudioBuffer<float> scratch;
        std::atomic<bool> bypassed { false };
    };

    struct PadEffectChain
    {
        std::uint64_t version = 0;
        std::vector<std::shared_ptr<PadEffect>> effects;
    };

    struct RetiredPadEffectChain
    {
        int pad = -1;
        std::shared_ptr<const PadEffectChain> chain;
    };

    bool loadPadFromFileInternal (int pad, const juce::File& file, juce::String& error,
                                  bool recordUndo);
    void setPadSettingsInternal (int pad, const PadSettings& settings, bool recordUndo);
    void setPad (int pad, Pad sample, bool recordUndo = true);
    void pushUndoStateLocked();
    void syncPadsForAudioThread();
    void syncPadAutomationParameters (int pad, const Pad& state);
    void publishPadEffectChain (int pad, std::shared_ptr<PadEffectChain> chain);
    bool preparePadEffect (const juce::PluginDescription&, std::unique_ptr<juce::AudioProcessor>,
                           std::shared_ptr<PadEffect>& result, juce::String& error);

    juce::AudioFormatManager formats;
    juce::SpinLock padsLock;
    juce::SpinLock effectsLock;
    std::array<Pad, numPads> pendingPads;
    std::array<std::array<PadAutomationParameter*, automatablePadParameterCount>, numPads>
        padAutomationParameters {};
    std::vector<PadSnapshot> undoStack, redoStack;
    std::array<std::shared_ptr<const juce::AudioBuffer<float>>, numPads> audioSamples;
    std::array<double, numPads> audioSampleRates {};
    std::array<PadSettings, numPads> audioPadSettings {};
    std::uint64_t pendingVersion = 0, audioVersion = 0;
    std::array<std::atomic<std::shared_ptr<const PadEffectChain>>, numPads> pendingPadEffects;
    std::array<std::shared_ptr<const PadEffectChain>, numPads> audioPadEffects;
    std::array<std::atomic<const PadEffectChain*>, numPads> audioPadEffectPointers {};
    std::vector<RetiredPadEffectChain> retiredPadEffectChains;
    std::array<juce::AudioBuffer<float>, numPads> padBuffers;
    std::array<Voice, 32> voices;
    std::atomic<std::uint32_t> triggeredPadMask { 0 };
    double outputSampleRate = 44100.0;
    int maximumBlockSize = 512;
    bool stateRestoreSucceededFlag = true;
};
