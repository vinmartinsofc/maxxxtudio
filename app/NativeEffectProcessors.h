#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

class UtilityProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* pluginIdentifier = "MaXXXtudio.Utility";
    static constexpr float minimumRateHz = 0.01f;
    static constexpr float maximumRateHz = 100.0f;

    UtilityProcessor();
    static float rateFromNormalized (float normalized)
    {
        return minimumRateHz * std::pow (maximumRateHz / minimumRateHz,
                                         std::clamp (normalized, 0.0f, 1.0f));
    }
    static float normalizedFromRate (float rate)
    {
        const auto clampedRate = std::clamp (rate, minimumRateHz, maximumRateHz);
        return std::log (clampedRate / minimumRateHz)
             / std::log (maximumRateHz / minimumRateHz);
    }
    static float migrateLegacyRate (float normalized)
    {
        const auto oldRate = 0.05f + std::clamp (normalized, 0.0f, 1.0f) * 9.95f;
        return normalizedFromRate (oldRate);
    }
    double modulationPhase() const { return currentLfoPhase.load (std::memory_order_relaxed); }
    const juce::String getName() const override { return "Master Volume"; }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    double sampleRate = 44100.0;
    float dcPreviousInput[2] {};
    float dcPreviousOutput[2] {};
    float bassLowpass[2] {};
    std::atomic<double> currentLfoPhase { 0.0 };
};

class AutoPanProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* pluginIdentifier = "MaXXXtudio.AutoPan";

    AutoPanProcessor();
    const juce::String getName() const override { return "Pan Master"; }
    void prepareToPlay (double newSampleRate, int) override { sampleRate = newSampleRate; phase = 0.0f; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    class Parameter;
    double sampleRate = 44100.0;
    double phase = 0.0;
};

class CompMasterProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* pluginIdentifier = "MaXXXtudio.CompMaster";
    static constexpr int scopeBufferSize = 131072;
    struct ScopeColumn
    {
        float minimum = 0.0f, maximum = 0.0f, gainReductionDb = 0.0f;
    };
    struct SidechainSource { int trackId = -1; juce::String name; };

    CompMasterProcessor();
    const juce::String getName() const override { return "Comp Master"; }
    void prepareToPlay (double newSampleRate, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    int copyScopeSnapshot (ScopeColumn* destination, int capacity) const;
    float inputPeak (int channel) const;
    float outputPeak (int channel) const;
    float gainReductionDb() const { return gainReduction.load (std::memory_order_relaxed); }
    int sidechainSourceTrackId() const { return sidechainSource.load (std::memory_order_relaxed); }
    void setSidechainSourceTrackId (int trackId, bool notify = true);
    void setSidechainSources (std::vector<SidechainSource> sources);
    const std::vector<SidechainSource>& getSidechainSources() const { return sidechainSources; }
    void selectABState (int slot);
    int activeABSlot() const { return activeAB; }
    std::function<bool (int)> onSidechainSourceChanged;

private:
    struct BiquadState { double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; };
    double sampleRate = 44100.0;
    int scopeDivider = 1;
    int scopeCounter = 0;
    std::atomic<int> scopeWritePosition { 0 };
    std::atomic<int> scopeAvailable { 0 };
    float envelope = 0.0f;
    float scopeMinimum = 0.0f, scopeMaximum = 0.0f;
    float scopeGainReduction = 0.0f;
    std::unique_ptr<std::atomic<float>[]> scopeMinimums, scopeMaximums;
    std::unique_ptr<std::atomic<float>[]> scopeGainReductions;
    std::array<std::atomic<float>, 2> inputPeaks {}, outputPeaks {};
    std::atomic<float> gainReduction { 0.0f };
    std::atomic<int> sidechainSource { -1 };
    int lastSidechainSourceId = -1;
    std::vector<SidechainSource> sidechainSources;
    std::array<std::array<BiquadState, 2>, 2> sidechainFilterStates {};
    std::array<std::array<float, 9>, 2> abState {};
    int activeAB = 0;
};

class EqMasterProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* pluginIdentifier = "MaXXXtudio.EqMaster";
    static constexpr int bandCount = 8;
    static constexpr int analyzerBufferSize = 2048;

    EqMasterProcessor();
    const juce::String getName() const override { return "EQ Master"; }
    void prepareToPlay (double newSampleRate, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void selectABState (int slot);
    int activeABSlot() const { return activeAB; }
    int copyAnalyzerSnapshot (bool postEq, float* destination, int capacity) const;

private:
    struct BiquadState { double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; };

    double sampleRate = 44100.0;
    std::array<std::array<std::array<BiquadState, 2>, 4>, bandCount> states {};
    std::array<int, bandCount> lastFilterTypes {};
    std::array<int, bandCount> lastFilterSlopes {};
    std::array<std::array<float, bandCount * 6 + 2>, 2> abState {};
    int activeAB = 0;
    std::array<std::atomic<float>, analyzerBufferSize> analyzerPre {}, analyzerPost {};
    std::atomic<int> analyzerWritePosition { 0 };
};
