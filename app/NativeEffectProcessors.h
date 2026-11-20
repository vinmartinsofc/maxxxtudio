#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <atomic>
#include <cmath>

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
    const juce::String getName() const override { return "Volume Master"; }
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
