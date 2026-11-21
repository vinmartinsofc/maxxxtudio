#include <catch2/catch_test_macros.hpp>
#include "AudioEngine.h"
#include "DrumRackEditor.h"
#include "DrumRackProcessor.h"
#include "NativeEffectProcessors.h"
#include "ReferenceMasterWindow.h"
#include "RemotePluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace
{
struct TemporaryWavFile
{
    TemporaryWavFile()
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("maxxx_tudio_master_render", ".wav", false))
    {
    }

    ~TemporaryWavFile() { file.deleteFile(); }

    juce::File file;
};

struct TemporarySampleFile
{
    TemporarySampleFile()
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("maxxx_tudio_drum_rack_sample", ".wav", false))
    {
        std::ofstream output (file.getFullPathName().toStdString(), std::ios::binary);
        const auto write16 = [&output] (std::uint16_t value)
        {
            const char bytes[] { static_cast<char> (value & 0xff), static_cast<char> (value >> 8) };
            output.write (bytes, sizeof (bytes));
        };
        const auto write32 = [&output] (std::uint32_t value)
        {
            const char bytes[] { static_cast<char> (value & 0xff),
                                 static_cast<char> ((value >> 8) & 0xff),
                                 static_cast<char> ((value >> 16) & 0xff),
                                 static_cast<char> ((value >> 24) & 0xff) };
            output.write (bytes, sizeof (bytes));
        };
        constexpr std::uint32_t sampleCount = 1024;
        constexpr std::uint32_t dataSize = sampleCount * 2;
        output.write ("RIFF", 4);
        write32 (36 + dataSize);
        output.write ("WAVEfmt ", 8);
        write32 (16);
        write16 (1);
        write16 (1);
        write32 (44100);
        write32 (88200);
        write16 (2);
        write16 (16);
        output.write ("data", 4);
        write32 (dataSize);
        for (std::uint32_t index = 0; index < sampleCount; ++index)
            write16 (16384);
    }

    ~TemporarySampleFile() { file.deleteFile(); }

    juce::File file;
};

struct TemporaryReferenceFile
{
    explicit TemporaryReferenceFile (double mainFrequency = 440.0,
                                     double harmonicFrequency = 0.0,
                                     double harmonicAmplitude = 0.0)
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("maxxx_tudio_reference_analysis", ".wav", false))
    {
        std::ofstream output (file.getFullPathName().toStdString(), std::ios::binary);
        const auto write16 = [&output] (std::uint16_t value)
        {
            const char bytes[] { static_cast<char> (value & 0xff),
                                 static_cast<char> ((value >> 8) & 0xff) };
            output.write (bytes, sizeof (bytes));
        };
        const auto write32 = [&output] (std::uint32_t value)
        {
            const char bytes[] { static_cast<char> (value & 0xff),
                                 static_cast<char> ((value >> 8) & 0xff),
                                 static_cast<char> ((value >> 16) & 0xff),
                                 static_cast<char> ((value >> 24) & 0xff) };
            output.write (bytes, sizeof (bytes));
        };
        constexpr std::uint32_t sampleRate = 44100;
        constexpr std::uint32_t sampleCount = sampleRate;
        constexpr std::uint32_t dataSize = sampleCount * 2;
        output.write ("RIFF", 4);
        write32 (36 + dataSize);
        output.write ("WAVEfmt ", 8);
        write32 (16);
        write16 (1);
        write16 (1);
        write32 (sampleRate);
        write32 (sampleRate * 2);
        write16 (2);
        write16 (16);
        output.write ("data", 4);
        write32 (dataSize);
        for (std::uint32_t index = 0; index < sampleCount; ++index)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi * mainFrequency
                               * index / sampleRate;
            const double harmonicPhase = 2.0 * juce::MathConstants<double>::pi
                * harmonicFrequency * index / sampleRate;
            const auto value = static_cast<std::int16_t> (
                (std::sin (phase) + harmonicAmplitude * std::sin (harmonicPhase)) * 16000.0);
            write16 (static_cast<std::uint16_t> (value));
        }
    }

    ~TemporaryReferenceFile() { file.deleteFile(); }

    juce::File file;
};

class TestPadGainEffect final : public juce::AudioProcessor
{
public:
    explicit TestPadGainEffect (float g)
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          gain (g)
    {
    }

    const juce::String getName() const override { return "Test Pad Gain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain);
    }
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
    float gain = 1.0f;
};

class TestPadSidechainEffect final : public juce::AudioProcessor
{
public:
    TestPadSidechainEffect()
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                   .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "Test Pad Sidechain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (0.5f);
    }
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
};
}

TEST_CASE ("Track mixer keeps separate left and right peak levels")
{
    BlockShared shared;
    TrackMixer mixer (shared);
    mixer.set (1.0f, 0.0f, false, false);

    juce::AudioBuffer<float> buffer (2, 4);
    buffer.clear();
    buffer.setSample (0, 0, 0.25f);
    buffer.setSample (1, 0, 0.75f);
    juce::MidiBuffer midi;
    mixer.processBlock (buffer, midi);

    REQUIRE (std::abs (mixer.peakLeft() - 0.25f) < 0.001f);
    REQUIRE (std::abs (mixer.peakRight() - 0.75f) < 0.001f);
}

TEST_CASE ("Volume Master exposes unified stereo and modulation controls")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    UtilityProcessor utility;
    REQUIRE (utility.getName() == "Volume Master");
    REQUIRE (utility.getParameters().size() == 13);
    REQUIRE (std::any_of (utility.getParameters().begin(), utility.getParameters().end(),
        [] (const auto* parameter)
        {
            auto* identified = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
            return identified != nullptr && identified->paramID == "gain"
                && parameter->isAutomatable();
        }));

    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);
    REQUIRE (engine.addUtility (1) > 0);
    const auto utilityDevices = engine.devices (1);
    REQUIRE (utilityDevices.size() == 1);
    REQUIRE (utilityDevices.front().name == "Volume Master");
    REQUIRE (utilityDevices.front().automatableParameters.size() == 13);
    const auto findParameter = [&utility] (const juce::String& id)
    {
        for (auto* parameter : utility.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                return parameter;
        return static_cast<juce::AudioProcessorParameter*> (nullptr);
    };
    const auto* gain = findParameter ("gain");
    REQUIRE (gain != nullptr);
    REQUIRE (gain->getText (0.0f, 16) == "-60.0 dB");
    REQUIRE (gain->getText (5.0f / 7.0f, 16) == "0.0 dB");
    REQUIRE (gain->getText (1.0f, 16) == "24.0 dB");
    REQUIRE (gain->getValueForText ("-60 dB") == 0.0f);
    REQUIRE (std::abs (gain->getValueForText ("0 dB") - 5.0f / 7.0f) < 0.00001f);
    REQUIRE (gain->getValueForText ("+24 dB") == 1.0f);
    auto* rate = findParameter ("pan_rate");
    REQUIRE (rate != nullptr);
    REQUIRE (rate->getText (0.0f, 16) == "0.01 Hz");
    REQUIRE (rate->getText (0.5f, 16) == "1.00 Hz");
    REQUIRE (rate->getText (1.0f, 16) == "100.0 Hz");
    REQUIRE (std::abs (rate->getValueForText ("90 Hz")
                       - UtilityProcessor::normalizedFromRate (90.0f)) < 0.00001f);
    REQUIRE (findParameter ("pan_phase")->getText (0.5f, 16) == "180\u00b0");
    auto* shape = findParameter ("pan_shape");
    REQUIRE (shape != nullptr);
    REQUIRE (shape->getText (0.57f, 16) == "57%");
    shape->setValue (shape->getValueForText ("57%"));
    REQUIRE (std::abs (shape->getValue() - 0.57f) < 0.00001f);
    REQUIRE (findParameter ("bass_mono_frequency") == nullptr);
    REQUIRE (findParameter ("bass_mono") != nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> utilityEditor (utility.createEditor());
    REQUIRE (utilityEditor != nullptr);
    REQUIRE (utilityEditor->getWidth() == 700);
    REQUIRE (utilityEditor->getHeight() == 400);
    REQUIRE_FALSE (utilityEditor->isResizable());
    REQUIRE (utilityEditor->getNumChildComponents() == 26);
    for (int index = 0; index < utilityEditor->getNumChildComponents(); ++index)
    {
        const auto* child = utilityEditor->getChildComponent (index);
        const auto bounds = child->getBounds();
        REQUIRE (bounds.getWidth() > 0);
        REQUIRE (bounds.getHeight() > 0);
        REQUIRE (utilityEditor->getLocalBounds().contains (bounds));
        if (const auto* slider = dynamic_cast<const juce::Slider*> (child))
        {
            const auto parameter = std::find_if (
                utility.getParameters().begin(), utility.getParameters().end(),
                [slider] (const auto* candidate) { return candidate->getName (64) == slider->getName(); });
            REQUIRE (parameter != utility.getParameters().end());
            REQUIRE (slider->getDoubleClickReturnValue() == (*parameter)->getDefaultValue());
        }
    }

    AutoPanProcessor pan;
    REQUIRE (pan.getName() == "Pan Master");
    REQUIRE (pan.getParameters().size() == 3);
    pan.prepareToPlay (44100.0, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    buffer.clear();
    buffer.getWritePointer (0)[0] = 1.0f;
    buffer.getWritePointer (1)[0] = 1.0f;
    juce::MidiBuffer midi;
    pan.processBlock (buffer, midi);
    REQUIRE (buffer.getNumChannels() == 2);

    juce::XmlElement oldPanState ("NativeEffect");
    auto* oldGain = oldPanState.createNewChildElement ("Parameter");
    oldGain->setAttribute ("id", "gain");
    oldGain->setAttribute ("value", 0.25);
    for (const auto& [id, value] : { std::pair<const char*, double> { "rate", 0.1 },
                                     { "depth", 0.75 }, { "phase", 0.4 } })
    {
        auto* parameter = oldPanState.createNewChildElement ("Parameter");
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    juce::MemoryOutputStream stateData;
    oldPanState.writeTo (stateData);
    utility.setStateInformation (stateData.getData(), static_cast<int> (stateData.getDataSize()));
    const auto findParameterValue = [] (UtilityProcessor& processor, const juce::String& id)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                return parameter->getValue();
        return -1.0f;
    };
    REQUIRE (std::abs (findParameterValue (utility, "pan_rate")
                       - UtilityProcessor::migrateLegacyRate (0.1f)) < 0.00001f);
    REQUIRE (std::abs (findParameterValue (utility, "pan_amount") - 0.75f) < 0.00001f);
    REQUIRE (std::abs (findParameterValue (utility, "pan_phase") - 0.4f) < 0.00001f);
    REQUIRE (std::abs (findParameterValue (utility, "gain") - 9.0f / 14.0f) < 0.00001f);

    juce::MemoryBlock currentState;
    utility.getStateInformation (currentState);
    const auto currentStateXml = juce::XmlDocument::parse (
        juce::String::fromUTF8 (static_cast<const char*> (currentState.getData()),
                                static_cast<int> (currentState.getSize())));
    REQUIRE (currentStateXml != nullptr);
    REQUIRE (currentStateXml->getIntAttribute ("gainRangeVersion") == 3);
    REQUIRE (currentStateXml->getIntAttribute ("rateRangeVersion") == 1);
    UtilityProcessor restoredUtility;
    restoredUtility.setStateInformation (currentState.getData(), static_cast<int> (currentState.getSize()));
    REQUIRE (std::abs (findParameterValue (restoredUtility, "gain") - 9.0f / 14.0f) < 0.00001f);

    juce::XmlElement priorRangeState ("NativeEffect");
    priorRangeState.setAttribute ("gainRangeVersion", 2);
    auto* priorGain = priorRangeState.createNewChildElement ("Parameter");
    priorGain->setAttribute ("id", "gain");
    priorGain->setAttribute ("value", 0.25);
    juce::MemoryOutputStream priorRangeData;
    priorRangeState.writeTo (priorRangeData);
    UtilityProcessor priorRangeUtility;
    priorRangeUtility.setStateInformation (priorRangeData.getData(),
                                           static_cast<int> (priorRangeData.getDataSize()));
    REQUIRE (std::abs (findParameterValue (priorRangeUtility, "gain") - 3.0f / 14.0f) < 0.00001f);
}

TEST_CASE ("Volume Master Rate reaches the high-frequency range")
{
    UtilityProcessor processor;
    processor.prepareToPlay (1000.0, 10);
    for (auto* parameter : processor.getParameters())
    {
        auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        if (identified != nullptr && identified->paramID == "pan_rate")
            parameter->setValue (UtilityProcessor::normalizedFromRate (90.0f));
        else if (identified != nullptr && identified->paramID == "pan_amount")
            parameter->setValue (1.0f);
    }

    juce::AudioBuffer<float> buffer (2, 10);
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 1.0f);
        buffer.setSample (1, sample, 1.0f);
    }
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 1) > 0.7f);
    REQUIRE (std::abs (processor.modulationPhase()
                       - juce::MathConstants<double>::pi * 1.8) < 0.001);
}

TEST_CASE ("Volume Master applies stereo width, shared phase inversion, and pan modulation")
{
    UtilityProcessor processor;
    processor.prepareToPlay (44100.0, 8);
    const auto setParameter = [&processor] (const juce::String& id, float value)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                parameter->setValue (value);
    };

    juce::AudioBuffer<float> buffer (2, 8);
    juce::MidiBuffer midi;
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, -1.0f);
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 0) - 1.0f) < 0.0001f);
    REQUIRE (std::abs (buffer.getSample (1, 0) + 1.0f) < 0.0001f);

    setParameter ("invert_phase", 1.0f);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, -1.0f);
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 0) + 1.0f) < 0.0001f);
    REQUIRE (std::abs (buffer.getSample (1, 0) - 1.0f) < 0.0001f);

    setParameter ("invert_phase", 0.0f);
    setParameter ("pan_amount", 1.0f);
    setParameter ("balance", 1.0f);
    setParameter ("pan_phase", 0.25f);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 0)) < 0.0001f);
    REQUIRE (buffer.getSample (1, 0) > 1.4f);

    setParameter ("pan_amount", 0.0f);
    setParameter ("balance", 0.5f);
    setParameter ("gain", 0.0f);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 0) - 0.001f) < 0.00001f);
    REQUIRE (std::abs (buffer.getSample (1, 0) - 0.001f) < 0.00001f);

    processor.prepareToPlay (4.0, 4);
    setParameter ("gain", 5.0f / 7.0f);
    setParameter ("pan_amount", 1.0f);
    setParameter ("pan_phase", 0.0f);
    setParameter ("pan_shape", 0.0f);
    setParameter ("balance", 0.5f);
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 1.0f);
        buffer.setSample (1, sample, 1.0f);
    }
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 1) - 1.0f) < 0.0001f);
    REQUIRE (std::abs (buffer.getSample (1, 1) - 1.0f) < 0.0001f);

    processor.prepareToPlay (4.0, 4);
    setParameter ("pan_phase", 0.5f);
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 1.0f);
        buffer.setSample (1, sample, 1.0f);
    }
    processor.processBlock (buffer, midi);
    REQUIRE (std::abs (buffer.getSample (0, 1) - 1.0f) < 0.0001f);
    REQUIRE (std::abs (buffer.getSample (1, 1)) < 0.0001f);
}

TEST_CASE ("Drum Rack exposes stable automatable parameters for every pad")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    REQUIRE (processor.getParameters().size()
             == DrumRackProcessor::numPads * DrumRackProcessor::automatablePadParameterCount);

    juce::AudioProcessorParameter* volume = nullptr;
    for (auto* parameter : processor.getParameters())
        if (parameter != nullptr && parameter->getName (64) == "Pad 1 Volume")
            volume = parameter;
    REQUIRE (volume != nullptr);
    REQUIRE (volume->isAutomatable());
    const auto* identifiedVolume = dynamic_cast<const juce::AudioProcessorParameterWithID*> (volume);
    REQUIRE (identifiedVolume != nullptr);
    REQUIRE (identifiedVolume->paramID == "pad_1_volume");
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    volume->setValue (0.5f);
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    processor.processBlock (output, midi);
    REQUIRE (output.getSample (0, 0) > 0.9f);

    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Midi);
    REQUIRE (engine.addDrumRack (1) > 0);
    const auto devices = engine.devices (1);
    REQUIRE (devices.size() == 1);
    REQUIRE (std::any_of (devices.front().automatableParameters.begin(),
                          devices.front().automatableParameters.end(),
        [] (const auto& parameter)
        {
            return parameter.id == "pad_1_volume" && parameter.name == "Pad 1 Volume";
        }));
}

TEST_CASE ("Drum Rack triggers loaded samples from its MIDI pad notes")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 5);
    processor.processBlock (output, midi);

    REQUIRE (processor.consumeTriggeredPads() == 1u);
    REQUIRE (std::abs (output.getSample (0, 4)) < 0.0001f);
    REQUIRE (output.getSample (0, 5) > 0.4f);
    REQUIRE (output.getSample (1, 5) > 0.4f);
    REQUIRE (midi.isEmpty());
    REQUIRE (processor.padName (0).contains ("maxxx_tudio_drum_rack_sample"));
}

TEST_CASE ("Drum Rack editor initializes its pad controls before resizing")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    DrumRackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    REQUIRE (editor != nullptr);
    REQUIRE (DrumRackProcessor::visiblePadCount == 15);
    REQUIRE (editor->getWidth() == 900);
    REQUIRE (editor->getHeight() == 560);
}

TEST_CASE ("Drum Rack effects process only their assigned pad")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    REQUIRE (processor.loadPadFromFile (1, sample.file, error));

    juce::PluginDescription description;
    description.name = "Test Pad Gain";
    description.fileOrIdentifier = "Test Pad Gain";
    REQUIRE (processor.addPadEffect (0, description,
                                     std::make_unique<TestPadGainEffect> (0.25f), error));
    REQUIRE (processor.padEffects (0).size() == 1);
    REQUIRE (processor.padEffects (1).empty());
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote + 1, 1.0f), 0);
    processor.processBlock (output, midi);

    REQUIRE (output.getSample (0, 0) == 0.625f);
    REQUIRE (output.getSample (1, 0) == 0.625f);

    auto state = juce::XmlDocument::parse (processor.stateXml());
    REQUIRE (state != nullptr);
    auto* padState = state->getChildByName ("Pad");
    REQUIRE (padState != nullptr);
    REQUIRE (padState->getNumChildElements() == 1);

    REQUIRE (processor.setPadEffectBypassed (0, 0, true));
    REQUIRE (processor.padEffects (0).front().bypassed);
    midi.clear();
    for (int block = 0; block < 7; ++block)
        processor.processBlock (output, midi);
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    processor.processBlock (output, midi);
    REQUIRE (output.getSample (0, 0) == 0.5f);
    REQUIRE (output.getSample (1, 0) == 0.5f);
    REQUIRE (processor.removePadEffect (0, 0));
    REQUIRE (processor.padEffects (0).empty());
}

TEST_CASE ("Drum Rack pad effects can be reordered without removing them")
{
    DrumRackProcessor processor;
    juce::String error;
    juce::PluginDescription firstDescription;
    firstDescription.name = "First effect";
    firstDescription.fileOrIdentifier = "First effect";
    juce::PluginDescription secondDescription;
    secondDescription.name = "Second effect";
    secondDescription.fileOrIdentifier = "Second effect";

    REQUIRE (processor.addPadEffect (0, firstDescription,
                                     std::make_unique<TestPadGainEffect> (0.25f), error));
    REQUIRE (processor.addPadEffect (0, secondDescription,
                                     std::make_unique<TestPadGainEffect> (0.5f), error));
    REQUIRE_FALSE (processor.movePadEffect (0, 0, -1));
    REQUIRE (processor.movePadEffect (0, 1, -1));
    const auto effects = processor.padEffects (0);
    REQUIRE (effects.size() == 2);
    REQUIRE (effects[0].name == "Second effect");
    REQUIRE (effects[1].name == "First effect");
    REQUIRE_FALSE (processor.movePadEffect (0, 2, -1));
}

TEST_CASE ("Drum Rack effects with a stereo sidechain process on their pad")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));

    juce::PluginDescription description;
    description.name = "Test Pad Sidechain";
    description.fileOrIdentifier = "Test Pad Sidechain";
    REQUIRE (processor.addPadEffect (0, description,
                                     std::make_unique<TestPadSidechainEffect>(), error));
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    processor.processBlock (output, midi);
    REQUIRE (output.getSample (0, 0) == 0.25f);
    REQUIRE (output.getSample (1, 0) == 0.25f);
}

TEST_CASE ("Drum Rack pads accept samples dragged from the audio browser")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* target = dynamic_cast<juce::DragAndDropTarget*> (editor.get());
    REQUIRE (target != nullptr);

    juce::DragAndDropTarget::SourceDetails details (
        "audio:" + sample.file.getFullPathName(), nullptr, { 30, 330 });
    REQUIRE (target->isInterestedInDragSource (details));
    target->itemDropped (details);

    REQUIRE (processor.padName (0) == sample.file.getFileName());

    auto* rackEditor = dynamic_cast<DrumRackEditor*> (editor.get());
    REQUIRE (rackEditor != nullptr);
    int requestedPad = -1;
    juce::String requestedPlugin;
    rackEditor->onPadEffectRequested = [&] (int pad, const juce::String& identifier)
    {
        requestedPad = pad;
        requestedPlugin = identifier;
    };
    juce::DragAndDropTarget::SourceDetails effectDetails ("plugin:test.effect", nullptr, { 30, 330 });
    REQUIRE (target->isInterestedInDragSource (effectDetails));
    target->itemDropped (effectDetails);
    REQUIRE (requestedPad == 0);
    REQUIRE (requestedPlugin == "test.effect");
}

TEST_CASE ("Drum Rack pads accept samples dropped from the file system")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (editor.get());
    REQUIRE (target != nullptr);

    juce::StringArray files { sample.file.getFullPathName() };
    REQUIRE (target->isInterestedInFileDrag (files));
    target->filesDropped (files, 30, 330);

    REQUIRE (processor.padName (0) == sample.file.getFileName());
}

TEST_CASE ("Drum Rack maps the computer keyboard's fifteen chromatic notes")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (DrumRackProcessor::numPads == 15);
    REQUIRE (DrumRackProcessor::computerMidiKeyboardMap.size() == 15);
    REQUIRE (processor.loadPadFromFile (14, sample.file, error));
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (
        1, DrumRackProcessor::firstMidiNote + 14, 1.0f), 0);
    processor.processBlock (output, midi);

    REQUIRE (output.getSample (0, 0) > 0.4f);
    REQUIRE (processor.padName (14).contains ("maxxx_tudio_drum_rack_sample"));
}

TEST_CASE ("Drum Rack applies per-pad gain and sample trim")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    auto settings = processor.padSettings (0);
    settings.gain = 0.5f;
    settings.startSample = 4;
    settings.endSample = 8;
    processor.setPadSettings (0, settings);
    processor.prepareToPlay (44100.0, 128);

    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    processor.processBlock (output, midi);

    REQUIRE (output.getSample (0, 0) == 0.25f);
    REQUIRE (output.getSample (0, 3) == 0.25f);
    REQUIRE (std::abs (output.getSample (0, 4)) < 0.0001f);
}

TEST_CASE ("Drum Rack persists per-pad sample playback settings")
{
    TemporarySampleFile sample;
    DrumRackProcessor source;
    juce::String error;
    REQUIRE (source.loadPadFromFile (0, sample.file, error));

    auto settings = source.padSettings (0);
    settings.gain = 0.5f;
    settings.pitchSemitones = -7;
    settings.startSample = 16;
    settings.endSample = 512;
    settings.reverse = true;
    settings.loop = true;
    settings.mode = DrumRackProcessor::PlaybackMode::Classic;
    source.setPadSettings (0, settings);

    const auto xml = juce::XmlDocument::parse (source.stateXml());
    REQUIRE (xml != nullptr);
    DrumRackProcessor restored;
    REQUIRE (restored.restoreStateXml (*xml));
    const auto restoredSettings = restored.padSettings (0);
    REQUIRE (std::abs (restoredSettings.gain - 0.5f) < 0.0001f);
    REQUIRE (restoredSettings.pitchSemitones == -7);
    REQUIRE (restoredSettings.startSample == 16);
    REQUIRE (restoredSettings.endSample == 512);
    REQUIRE (restoredSettings.reverse);
    REQUIRE (restoredSettings.loop);
    REQUIRE (restoredSettings.mode == DrumRackProcessor::PlaybackMode::Classic);
}

TEST_CASE ("Drum Rack undo restores cleared pads and the full rack")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    REQUIRE (processor.loadPadFromFile (1, sample.file, error));

    processor.clearPad (0);
    REQUIRE (processor.padSampleLength (0) == 0);
    REQUIRE (processor.undo());
    REQUIRE (processor.padSampleLength (0) == 1024);
    REQUIRE (processor.padSampleLength (1) == 1024);

    processor.clearPads();
    REQUIRE (processor.padSampleLength (0) == 0);
    REQUIRE (processor.padSampleLength (1) == 0);
    REQUIRE (processor.undo());
    REQUIRE (processor.padSampleLength (0) == 1024);
    REQUIRE (processor.padSampleLength (1) == 1024);
    REQUIRE (processor.redo());
    REQUIRE (processor.padSampleLength (0) == 0);
    REQUIRE (processor.padSampleLength (1) == 0);
}

TEST_CASE ("Drum Rack Delete key clears only the selected pad and can be undone")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    REQUIRE (processor.loadPadFromFile (1, sample.file, error));
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (editor != nullptr);

    REQUIRE (editor->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (processor.padSampleLength (0) == 0);
    REQUIRE (processor.padSampleLength (1) == 1024);
    REQUIRE (editor->keyPressed (juce::KeyPress (
        'Z', juce::ModifierKeys::ctrlModifier, 'Z')));
    REQUIRE (processor.padSampleLength (0) == 1024);
}

TEST_CASE ("Drum Rack migrates legacy sixteen-pad state to its MIDI note bank")
{
    TemporarySampleFile sample;
    juce::XmlElement legacy ("DrumRack");
    legacy.setAttribute ("version", 2);
    auto* pad = legacy.createNewChildElement ("Pad");
    pad->setAttribute ("index", 0);
    pad->setAttribute ("file", sample.file.getFullPathName());
    pad->setAttribute ("startSample", 0);
    pad->setAttribute ("endSample", 1023);
    pad->setAttribute ("loop", true);

    DrumRackProcessor processor;
    REQUIRE (processor.restoreStateXml (legacy));
    REQUIRE (processor.padSampleLength (0) == 1024);
    REQUIRE (processor.padName (0).contains ("maxxx_tudio_drum_rack_sample"));
    REQUIRE (processor.padName (1).contains ("vazio"));
    REQUIRE (processor.padSettings (0).endSample == 1024);
    REQUIRE (processor.padSettings (0).mode == DrumRackProcessor::PlaybackMode::Classic);
}

TEST_CASE ("Drum Rack maps the previous default MIDI bank into its keyboard slots")
{
    TemporarySampleFile sample;
    juce::XmlElement legacy ("DrumRack");
    legacy.setAttribute ("version", 3);
    auto* pad = legacy.createNewChildElement ("Pad");
    pad->setAttribute ("index", 36);
    pad->setAttribute ("file", sample.file.getFullPathName());
    pad->setAttribute ("endSample", 1024);

    DrumRackProcessor processor;
    REQUIRE (processor.restoreStateXml (legacy));
    REQUIRE (processor.padSampleLength (0) == 1024);
    REQUIRE (processor.padName (0).contains ("maxxx_tudio_drum_rack_sample"));
}

TEST_CASE ("Classic Drum Rack pads stop looping after MIDI note-off")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    auto settings = processor.padSettings (0);
    settings.mode = DrumRackProcessor::PlaybackMode::Classic;
    settings.loop = true;
    processor.setPadSettings (0, settings);
    processor.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> output (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, DrumRackProcessor::firstMidiNote, 1.0f), 0);
    processor.processBlock (output, midi);
    REQUIRE (output.getSample (0, 500) > 0.4f);

    midi.addEvent (juce::MidiMessage::noteOff (1, DrumRackProcessor::firstMidiNote), 0);
    processor.processBlock (output, midi);
    REQUIRE (output.getSample (0, 0) > 0.4f);
    REQUIRE (std::abs (output.getSample (0, 300)) < 0.0001f);
}

TEST_CASE ("Offline WAV export renders the master bus")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineRender (error));

    TemporaryWavFile output;
    const auto result = engine.renderMasterToWav (output.file, 1.1, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (output.file.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (output.file));
    REQUIRE (reader != nullptr);
    REQUIRE (reader->numChannels == 2);
    REQUIRE (reader->bitsPerSample == 24);
    REQUIRE (reader->lengthInSamples > reader->sampleRate);

    juce::AudioBuffer<float> firstFrames (2, 1024);
    REQUIRE (reader->read (&firstFrames, 0, firstFrames.getNumSamples(), 0, true, true));
    REQUIRE (firstFrames.getMagnitude (0, firstFrames.getNumSamples()) > 0.1f);
    reader.reset();
}

TEST_CASE ("Audio engine registers only VST3")
{
    AudioEngine engine;
    bool hasVst2 = false;
    bool hasVst3 = false;

    for (int index = 0; index < engine.formats().getNumFormats(); ++index)
    {
        const auto name = engine.formats().getFormat (index)->getName();
        hasVst2 = hasVst2 || name == "VST";
        hasVst3 = hasVst3 || name == "VST3";
    }

    REQUIRE (hasVst3);
    REQUIRE_FALSE (hasVst2);
}

TEST_CASE ("Audio loop tempo analysis resists off-beat transients")
{
    constexpr double sampleRate = 44100.0;
    constexpr double durationSeconds = 15.24;
    const int sampleCount = static_cast<int> (std::lround (sampleRate * durationSeconds));
    juce::AudioBuffer<float> samples (1, sampleCount);
    samples.clear();

    const int kickPeriod = static_cast<int> (std::lround (sampleRate * 60.0 / 126.0));
    for (int start = 0; start < sampleCount; start += kickPeriod)
        for (int i = 0; i < 2500 && start + i < sampleCount; ++i)
        {
            const double seconds = static_cast<double> (i) / sampleRate;
            samples.addSample (0, start + i,
                static_cast<float> (0.8 * std::exp (-seconds * 45.0)
                    * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * seconds)));
        }

    const int transientPeriod = static_cast<int> (std::lround (sampleRate * 60.0 / 170.0));
    for (int start = 1000; start < sampleCount; start += transientPeriod)
        for (int i = 0; i < 256 && start + i < sampleCount; ++i)
        {
            const double seconds = static_cast<double> (i) / sampleRate;
            samples.addSample (0, start + i,
                static_cast<float> (0.35 * std::exp (-seconds * 900.0)
                    * std::sin (2.0 * juce::MathConstants<double>::pi * 8000.0 * seconds)));
        }

    const double bpm = AudioEngine::estimateAudioBpm (samples, sampleRate);
    CHECK (std::abs (bpm - 126.0) < 1.0);
}

TEST_CASE ("Reference analysis is isolated from previews and produces a spectrum")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporaryReferenceFile audioFile;
    AudioEngine engine;
    auto* messageManager = juce::MessageManager::getInstance();
    REQUIRE (messageManager != nullptr);

    struct AsyncResult
    {
        bool complete = false;
        juce::String error;
        std::shared_ptr<const AudioFileData> data;
    };

    const auto load = [&] (const juce::File& file, bool analyzeReference)
    {
        auto result = std::make_shared<AsyncResult>();
        const auto callback = [result] (const juce::String& error,
                                        std::shared_ptr<const AudioFileData> data)
        {
            result->error = error;
            result->data = std::move (data);
            result->complete = true;
        };
        if (analyzeReference)
            engine.analyzeAudioFileAsync (file, callback);
        else
            engine.loadAudioFileAsync (file, callback);

        const auto startTime = juce::Time::getMillisecondCounter();
        while (! result->complete
               && juce::Time::getMillisecondCounter() - startTime < 10000)
            messageManager->runDispatchLoopUntil (10);

        if (! result->complete)
            FAIL ("Audio analysis callback timed out");
        if (result->error.isNotEmpty())
            FAIL (result->error.toStdString());
        if (result->data == nullptr)
            FAIL ("Audio analysis returned no data");
        return result->data;
    };

    const auto preview = load (audioFile.file, false);
    CHECK (preview->estimatedKey.isEmpty());
    CHECK (preview->spectrum.empty());
    CHECK (preview->waveformColumns.empty());

    const auto analysis = load (audioFile.file, true);
    CHECK (analysis->estimatedKey.isNotEmpty());
    CHECK (analysis->spectrumColumns == 4096);
    CHECK (analysis->spectrumRows == 72);
    CHECK (analysis->spectrum.size() == 4096u * 72u);
    REQUIRE (analysis->waveformColumns.size() == 345u);
    CHECK (analysis->waveformColumns.front().peak > 0.45f);
    CHECK (analysis->waveformColumns.front().centroid > 400.0f);
    CHECK (analysis->waveformColumns.front().centroid < 480.0f);
    CHECK (analysis->waveformColumns.front().flatness < 0.2f);
    CHECK (analysis->waveformRms.size() == analysis->waveform.size());

    TemporaryReferenceFile bassWithHarmonics (60.0, 5000.0, 0.25);
    const auto bassAnalysis = load (bassWithHarmonics.file, true);
    REQUIRE_FALSE (bassAnalysis->waveformColumns.empty());
    CHECK (bassAnalysis->waveformColumns.front().centroid < 250.0f);
}

TEST_CASE ("Audio preview deck preserves playback position across pause and seek")
{
    AudioPreviewSource preview;
    preview.setRateAndBufferSizeDetails (44100.0, 10);
    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (1, 44100);
    for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
        source->samples->setSample (0, sample,
                                    static_cast<float> (sample) / source->samples->getNumSamples());
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;
    preview.play (source);

    juce::AudioBuffer<float> output (2, 10);
    juce::MidiBuffer midi;
    preview.processBlock (output, midi);
    CHECK (std::abs (preview.getPositionSeconds() - 10.0 / 44100.0) < 1.0e-8);

    preview.seek (0.5);
    preview.processBlock (output, midi);
    CHECK (preview.isPlaying());
    CHECK (std::abs (preview.getPositionSeconds() - (0.5 + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (std::abs (output.getSample (0, 0) - 0.25f) < 0.001f);

    preview.seek (0.8);
    preview.seek (0.6);
    preview.processBlock (output, midi);
    CHECK (preview.isPlaying());
    CHECK (std::abs (preview.getPositionSeconds() - (0.6 + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (std::abs (output.getSample (0, 0) - 0.3f) < 0.001f);

    preview.play (source, 0.4);
    preview.processBlock (output, midi);
    CHECK (preview.isPlaying());
    CHECK (std::abs (preview.getPositionSeconds() - (0.4 + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (std::abs (output.getSample (0, 0) - 0.2f) < 0.001f);

    preview.pause();
    const double pausedPosition = preview.getPositionSeconds();
    preview.processBlock (output, midi);
    CHECK (std::abs (preview.getPositionSeconds() - pausedPosition) < 1.0e-8);
    CHECK (output.getMagnitude (0, output.getNumSamples()) == 0.0f);

    preview.seek (0.25);
    preview.processBlock (output, midi);
    CHECK (std::abs (preview.getPositionSeconds() - 0.25) < 1.0e-8);
    preview.resume();
    preview.processBlock (output, midi);
    CHECK (std::abs (preview.getPositionSeconds() - (0.25 + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (output.getMagnitude (0, output.getNumSamples()) > 0.1f);

    preview.stop();
    CHECK (preview.getPositionSeconds() == 0.0);
    CHECK_FALSE (preview.isPlaying());

    preview.play (source, 0.75);
    preview.processBlock (output, midi);
    CHECK (preview.isPlaying());
    CHECK (std::abs (preview.getPositionSeconds() - (0.75 + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (std::abs (output.getSample (0, 0) - 0.375f) < 0.001f);
}

TEST_CASE ("Reference Master keeps playback active after a waveform click")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    ReferenceMasterWindow window;
    auto analysis = std::make_shared<AudioFileData>();
    analysis->samples = std::make_shared<juce::AudioBuffer<float>> (1, 44100);
    for (int sample = 0; sample < analysis->samples->getNumSamples(); ++sample)
        analysis->samples->setSample (0, sample,
            static_cast<float> (sample) / analysis->samples->getNumSamples());
    analysis->sampleRate = 44100.0;
    analysis->durationSeconds = 1.0;
    analysis->waveform.resize (128, { -0.5f, 0.5f });

    AudioPreviewSource preview;
    preview.setRateAndBufferSizeDetails (44100.0, 10);
    preview.play (analysis);
    juce::AudioBuffer<float> output (2, 10);
    juce::MidiBuffer midi;
    preview.processBlock (output, midi);

    int playRequests = 0, resumeRequests = 0, seekRequests = 0;
    double requestedPosition = 0.0;
    window.onIsPlayingRequested = [&] { return preview.isPlaying(); };
    window.onPlayheadPositionRequested = [&] { return preview.getPositionSeconds(); };
    window.onSeekRequested = [&] (double seconds)
    {
        ++seekRequests;
        requestedPosition = seconds;
        preview.seek (seconds);
    };
    window.onResumeRequested = [&] { ++resumeRequests; preview.resume(); };
    window.onPlayRequested = [&] (std::shared_ptr<const AudioFileData>, double seconds)
    {
        ++playRequests;
        requestedPosition = seconds;
        preview.play (analysis, seconds);
    };
    window.setAnalysis (juce::File ("reference.wav"), analysis);

    auto* content = window.getContentComponent();
    REQUIRE (content != nullptr);
    REQUIRE (content->getNumChildComponents() > 0);
    auto* deck = content->getChildComponent (content->getNumChildComponents() - 1);
    REQUIRE (deck != nullptr);
    REQUIRE (deck->getNumChildComponents() == 2);
    REQUIRE (deck->getHeight() > 0);
    juce::Component* seekOverlay = nullptr;
    juce::Component* visualizerShield = nullptr;
    for (int i = 0; i < deck->getNumChildComponents(); ++i)
    {
        auto* child = deck->getChildComponent (i);
        if (child->getY() < deck->getHeight() / 2)
            seekOverlay = child;
        else
            visualizerShield = child;
    }
    REQUIRE (seekOverlay != nullptr);
    REQUIRE (seekOverlay->getWidth() > 0);
    REQUIRE (visualizerShield != nullptr);
    REQUIRE (visualizerShield->getHeight() > 0);

    const auto point = juce::Point<float> (seekOverlay->getWidth() * 0.6f,
                                           seekOverlay->getHeight() * 0.5f);
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent click (
        juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        seekOverlay, seekOverlay, now, point, now, 1, false);
    seekOverlay->mouseDown (click);
    seekOverlay->mouseUp (click);

    CHECK (seekRequests >= 1);
    CHECK (resumeRequests == 1);
    CHECK (playRequests == 0);
    CHECK (preview.isPlaying());
    CHECK (requestedPosition > 0.55);
    CHECK (requestedPosition < 0.7);

    const auto visualizerPoint = juce::Point<float> (
        visualizerShield->getWidth() * 0.5f, visualizerShield->getHeight() * 0.5f);
    CHECK (deck->getComponentAt (juce::roundToInt (visualizerShield->getX()
                                                  + visualizerPoint.x),
                                 juce::roundToInt (visualizerShield->getY()
                                                  + visualizerPoint.y))
           == visualizerShield);
    const int seeksBeforeVisualizerClick = seekRequests;
    const juce::MouseEvent visualizerClick (
        juce::Desktop::getInstance().getMainMouseSource(), visualizerPoint,
        juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        visualizerShield, visualizerShield, now, visualizerPoint, now, 1, false);
    visualizerShield->mouseDown (visualizerClick);
    visualizerShield->mouseUp (visualizerClick);
    CHECK (seekRequests == seeksBeforeVisualizerClick);
    CHECK (preview.isPlaying());

    preview.processBlock (output, midi);
    CHECK (preview.isPlaying());
    CHECK (std::abs (preview.getPositionSeconds()
                     - (requestedPosition + 10.0 / 44100.0)) < 1.0e-8);
    CHECK (std::abs (output.getSample (0, 0) - 0.3f) < 0.01f);
}

TEST_CASE ("Offline track rendering captures audio before the master bus")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));

    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
    REQUIRE (rendered.getMagnitude (1, rendered.getNumSamples()) > 0.1f);
}

TEST_CASE ("Audio clips receive a short safety fade at their start")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.5f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));

    juce::AudioBuffer<float> rendered (2, 4410);
    const auto result = engine.renderTrackRange (0.0, 0.2, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (std::abs (rendered.getSample (0, 0)) < 0.0001f);
    REQUIRE (rendered.getSample (0, 80) > 0.05f);
    REQUIRE (rendered.getSample (0, 80) < 0.5f);
    REQUIRE (rendered.getSample (0, 400) > 0.49f);
}

TEST_CASE ("Audio clip reverse reads the selected source region backwards")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample,
                0.1f + 0.4f * static_cast<float> (sample) / source->samples->getNumSamples());
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    clip.reverse = true;
    clip.durationSeconds = 1.0;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));
    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (rendered.getSample (0, 1000) > rendered.getSample (0, 30000));
}

TEST_CASE ("Manual warp markers control audio source timing")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 88200);
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample,
                static_cast<float> (sample) / source->samples->getNumSamples());
    source->sampleRate = 44100.0;
    source->durationSeconds = 2.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 4.0;
    clip.sourceBpm = 120.0;
    clip.durationSeconds = 2.0;
    clip.source = std::move (source);
    clip.warpMarkers = { { 0.0, 0.0 }, { 1.5, 2.0 }, { 2.0, 4.0 } };
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));
    juce::AudioBuffer<float> rendered (2, 88200);
    const auto result = engine.renderTrackRange (0.0, 4.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    CHECK (std::abs (rendered.getSample (0, 44100) - 0.75f) < 0.02f);
}

TEST_CASE ("Audio clip gain automation affects playback and applies base gain once")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.1f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    clip.durationSeconds = 1.0;
    clip.automation.push_back ({
        { roll::AutomationParameterType::AudioClipGain, {}, {}, {}, -1, -1, 0.0, 1 },
        { { 0.0, 0.0 }, { 2.0, 6.0 } }
    });
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));
    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    const float lowerGain = rendered.getSample (0, 10000);
    const float higherGain = rendered.getSample (0, 40000);
    REQUIRE (higherGain > lowerGain * 1.4f);
    REQUIRE (higherGain < lowerGain * 1.8f);
}

TEST_CASE ("Audio from a track passes through its group bus before the master")
{
    AudioEngine engine;
    engine.addTrack (10, roll::TrackType::Group);
    engine.addTrack (1, roll::TrackType::Audio);
    engine.setTrackRouting (1, roll::TrackOutputRouting::Master, 10);
    engine.setMixer (10, 0.5f, 0.0f, false);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (AudioEngine::masterTrackId, error));
    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    const auto settledLevel = std::abs (rendered.getSample (0, rendered.getNumSamples() - 1));
    REQUIRE (settledLevel > 0.12f);
    REQUIRE (settledLevel < 0.13f);
}

TEST_CASE ("Mixer automation changes the live audio level during playback")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);
    engine.setTrackAutomation (1, {
        { { roll::AutomationParameterType::Volume }, { { 0.0, 0.25 }, { 1.0, 1.0 } } },
        { { roll::AutomationParameterType::Pan }, { { 0.0, -0.5 }, { 1.0, 0.5 } } },
        { { roll::AutomationParameterType::Mute }, { { 0.0, 0.0 }, { 1.69, 0.0 }, { 1.7, 1.0 } } }
    });

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineRender (error));
    TemporaryWavFile output;
    const auto result = engine.renderMasterToWav (output.file, 0.9, {}, {});
    const float liveAutomatedGain = engine.mixerGain (1);
    const float liveAutomatedPan = engine.mixerPan (1);
    const bool liveAutomatedMute = engine.mixerMuted (1);
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (output.file));
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> rendered (2, 39690);
    REQUIRE (reader->read (&rendered, 0, rendered.getNumSamples(), 0, true, true));

    const float initialLevel = rendered.getMagnitude (0, 2048);
    const float automatedLevel = rendered.getMagnitude (30000, 2048);
    REQUIRE (initialLevel > 0.02f);
    REQUIRE (automatedLevel > initialLevel * 2.0f);
    REQUIRE (liveAutomatedGain > 0.9f);
    REQUIRE (liveAutomatedPan > 0.4f);
    REQUIRE (liveAutomatedMute);
}

TEST_CASE ("Cancelling an offline WAV export removes the partial file")
{
    AudioEngine engine;
    juce::String error;
    REQUIRE (engine.beginOfflineRender (error));

    TemporaryWavFile output;
    const auto result = engine.renderMasterToWav (output.file, 0.1, {}, [] { return true; });
    engine.finishOfflineRender();

    REQUIRE (result.failed());
    REQUIRE_FALSE (output.file.existsAsFile());
}

#if defined(_WIN32)
TEST_CASE ("Remote VST3 processing runs outside the host process", "[integration][plugin-worker]")
{
    const auto pluginName = juce::SystemStats::getEnvironmentVariable ("MAXXXTUDIO_TEST_PLUGIN", {});
    if (pluginName.isEmpty())
    {
        SUCCEED ("Set MAXXXTUDIO_TEST_PLUGIN to a cached VST3 name to run this integration test.");
        return;
    }

    const auto pluginListFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("MaXXXtudio").getChildFile ("plugins.xml");
    const auto pluginListXml = juce::XmlDocument::parse (pluginListFile);
    REQUIRE (pluginListXml != nullptr);

    juce::KnownPluginList knownPlugins;
    knownPlugins.recreateFromXml (*pluginListXml);
    const auto descriptions = knownPlugins.getTypes();
    const auto description = std::find_if (descriptions.begin(), descriptions.end(),
        [&pluginName] (const auto& candidate) { return candidate.name.equalsIgnoreCase (pluginName); });
    REQUIRE (description != descriptions.end());
    REQUIRE (description->pluginFormatName == "VST3");

    juce::String error;
    auto processor = RemotePluginProcessor::create (*description, 44100.0, 256, error);
    INFO ("Plugin creation failed: " << error);
    REQUIRE (processor != nullptr);

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    juce::MidiBuffer midi;
    processor->prepareToPlay (44100.0, 256);
    processor->processBlock (buffer, midi);
    REQUIRE_FALSE (processor->hasFailed());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE (std::isfinite (buffer.getSample (channel, sample)));

    juce::MemoryBlock state;
    processor->getStateInformation (state);
    REQUIRE_FALSE (processor->hasFailed());
}

TEST_CASE ("Plugin scanning runs in the isolated worker", "[integration][plugin-worker]")
{
    const auto sessionId = juce::Uuid().toString().removeCharacters ("{}");
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto prefix = "maxxx-tudio-scan-" + sessionId + "-";
    const auto configFile = temp.getChildFile (prefix + "input.xml");
    const auto outputFile = temp.getChildFile (prefix + "output.xml");
    const auto progressFile = temp.getChildFile (prefix + "progress.txt");
    const auto emptyPath = temp.getChildFile ("maxxx-tudio-empty-plugin-scan-" + sessionId);
    struct Cleanup
    {
        juce::File config, output, progress, directory;
        ~Cleanup()
        {
            config.deleteFile();
            output.deleteFile();
            progress.deleteFile();
            directory.deleteRecursively();
        }
    } cleanup { configFile, outputFile, progressFile, emptyPath };
    REQUIRE (emptyPath.createDirectory().wasOk());

    juce::XmlElement config ("PLUGIN_SCAN");
    juce::KnownPluginList knownPlugins;
    config.addChildElement (knownPlugins.createXml().release());
    auto* path = config.createNewChildElement ("PATH");
    path->setAttribute ("format", "VST3");
    path->setAttribute ("directory", emptyPath.getFullPathName());
    REQUIRE (config.writeTo (configFile));

    juce::ChildProcess child;
    juce::StringArray arguments;
    arguments.add (MAXXXTUDIO_PLUGIN_WORKER_PATH);
    arguments.add ("--plugin-scan-worker");
    arguments.add (sessionId);
    arguments.add (juce::String (GetCurrentProcessId()));
    REQUIRE (child.start (arguments, 0));
    const bool finished = child.waitForProcessToFinish (30000);
    if (! finished)
        child.kill();
    REQUIRE (finished);
    REQUIRE (child.getExitCode() == 0);
    const auto outputXml = juce::XmlDocument::parse (outputFile);
    REQUIRE (outputXml != nullptr);
    REQUIRE (outputXml->hasTagName ("KNOWNPLUGINS"));

}
#endif
