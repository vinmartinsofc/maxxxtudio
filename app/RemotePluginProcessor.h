#pragma once

#include "PluginWorkerProtocol.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class RemotePluginProcessor final : public juce::AudioProcessor
{
public:
    static std::unique_ptr<RemotePluginProcessor> create (const juce::PluginDescription&,
                                                           double sampleRate, int blockSize,
                                                           juce::String& error);
    ~RemotePluginProcessor() override;

    bool hasFailed() const noexcept { return failed.load (std::memory_order_acquire); }
    bool isInstrument() const noexcept { return instrument; }
    static bool isPluginEditorForeground();
    static std::vector<juce::Rectangle<int>> pluginEditorWindowBounds();
    static void movePluginEditorWindows (int deltaX);
    bool showEditor (int x = -1, int y = -1);
    void setFailureCallback (std::function<void (const juce::String&)> callback)
    {
        onFailure = std::move (callback);
    }

    const juce::String getName() const override { return pluginName; }
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return acceptsMidiFlag; }
    bool producesMidi() const override { return producesMidiFlag; }
    bool isMidiEffect() const override { return midiEffectFlag; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return hasEditorFlag; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    class RemoteEditor;
    friend class RemoteParameter;

    RemotePluginProcessor (const juce::PluginDescription&, double, int);
    bool startWorker (const juce::PluginDescription&, juce::String& error);
    bool sendCommand (pluginworker::Command, int timeoutMs, int editorX = -1, int editorY = -1);
    void failWorker();
    void updateParameter (int index, float value) noexcept;
    float readParameter (int index) const noexcept;

    juce::String pluginName, sessionId;
    bool instrument = false, hasEditorFlag = false, midiEffectFlag = false;
    bool acceptsMidiFlag = false, producesMidiFlag = false;
    int sampleBlockSize = 512;
    double sampleRate = 44100.0;
    bool resourcesPrepared = true;
    std::atomic<bool> failed { false };
    juce::CriticalSection requestLock;
    juce::CriticalSection stateLock;
    juce::MemoryBlock cachedState;
    std::function<void (const juce::String&)> onFailure;
    bool processRequestPending = false;
    std::uint32_t processRequestStartTime = 0;
    HANDLE mapping = nullptr, requestEvent = nullptr, responseEvent = nullptr, workerProcess = nullptr;
    DWORD workerProcessId = 0;
    pluginworker::SharedData* shared = nullptr;
};
