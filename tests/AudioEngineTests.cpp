#include <catch2/catch_test_macros.hpp>
#include "AudioEngine.h"
#include "DrumRackEditor.h"
#include "DrumRackProcessor.h"
#include "NativeEffectProcessors.h"
#include "ReferenceMasterWindow.h"
#include "RemotePluginProcessor.h"
#include "Theme.h"
#include "ThemeKnobSlider.h"
#include "WaveformDisplay.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace
{
class TestTransportPlayHead final : public juce::AudioPlayHead
{
public:
    void setSamplePosition (std::int64_t samplePositionIn) { samplePosition = samplePositionIn; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        constexpr double sampleRate = 44100.0;
        constexpr double tempo = 120.0;
        const double seconds = static_cast<double> (samplePosition) / sampleRate;
        const double ppq = seconds * tempo / 60.0;
        PositionInfo position;
        position.setTimeInSamples (samplePosition);
        position.setTimeInSeconds (seconds);
        position.setBpm (tempo);
        position.setTimeSignature (TimeSignature {});
        position.setPpqPosition (ppq);
        position.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        position.setBarCount (static_cast<std::int64_t> (std::floor (ppq / 4.0)));
        position.setIsPlaying (true);
        position.setIsLooping (true);
        LoopPoints loop;
        loop.ppqStart = 0.0;
        loop.ppqEnd = 4.0;
        position.setLoopPoints (loop);
        return position;
    }

private:
    std::int64_t samplePosition = 0;
};

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

bool hasEditableKnobEntry (const juce::Component& component)
{
    if (const auto* slider = dynamic_cast<const juce::Slider*> (&component);
        slider != nullptr
        && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
        && slider->isTextBoxEditable())
        return true;

    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (hasEditableKnobEntry (*component.getChildComponent (index)))
            return true;
    return false;
}
}

TEST_CASE ("Red themes use red accents on their selected dark backgrounds")
{
    const auto previousPalette = theme::selectedPalette();

    theme::selectedPalette() = theme::Palette::GraphiteRed;
    CHECK (theme::accent() == juce::Colour (0xffe53935));
    CHECK (theme::background() == juce::Colour (0xff343638));

    theme::selectedPalette() = theme::Palette::BlackRed;
    CHECK (theme::accent() == juce::Colour (0xffe53935));
    CHECK (theme::background() == juce::Colour (0xff101112));
    CHECK (theme::panel() == juce::Colour (0xff191b1d));

    theme::selectedPalette() = previousPalette;
}

TEST_CASE ("Ultraviolet waveform colors transition from red to orange to green")
{
    const auto red = ultravioletWaveformColour (0.0f);
    const auto orange = ultravioletWaveformColour (0.5f);
    const auto green = ultravioletWaveformColour (1.0f);

    CHECK (red.getRed() > red.getGreen());
    CHECK (orange.getRed() > orange.getGreen());
    CHECK (orange.getGreen() > orange.getBlue());
    CHECK (green.getGreen() > green.getRed());
    CHECK (ultravioletWaveformColour (-1.0f) == red);
    CHECK (ultravioletWaveformColour (2.0f) == green);
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
    REQUIRE (gain->getText (std::nextafter (5.0f / 7.0f, 0.0f), 16) == "0.0 dB");
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
    REQUIRE (utilityEditor->getNumChildComponents() == 20);
    CHECK_FALSE (hasEditableKnobEntry (*utilityEditor));
    for (int index = 0; index < utilityEditor->getNumChildComponents(); ++index)
        if (const auto* label = dynamic_cast<const juce::Label*> (
                utilityEditor->getChildComponent (index)))
        {
            CHECK (label->getText() != "DC");
            CHECK (label->getText() != "Mute");
            CHECK (label->getText() != "Bass Mono");
        }
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

TEST_CASE ("Slider value entry selects the current value for replacement")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    ThemeKnobLookAndFeel lookAndFeel;
    juce::Slider slider;
    std::unique_ptr<juce::Label> textBox (lookAndFeel.createSliderTextBox (slider));
    textBox->setText ("12.5", juce::dontSendNotification);
    textBox->setEditable (false, true, false);
    textBox->showEditor();

    auto* editor = textBox->getCurrentTextEditor();
    REQUIRE (editor != nullptr);
    REQUIRE (editor->getHighlightedRegion() == juce::Range<int> (0, 4));
    editor->setCaretPosition (2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    REQUIRE (editor->getHighlightedRegion() == juce::Range<int> (0, 4));
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

TEST_CASE ("Comp Master compresses audio and publishes its live scope and meters")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    CompMasterProcessor processor;
    REQUIRE (processor.getName() == "Comp Master");
    REQUIRE (processor.getParameters().size() == 10);
    REQUIRE (std::abs (processor.getParameters()[0]->getValue() - 1.0f) < 0.00001f);
    REQUIRE (std::abs (processor.getParameters()[1]->getValue() - 1.0f / 19.0f) < 0.00001f);
    REQUIRE (std::abs (processor.getParameters()[9]->getValue() - 0.5f) < 0.00001f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (editor != nullptr);
    REQUIRE (editor->getWidth() == 780);
    REQUIRE (editor->getHeight() == 520);
    CHECK_FALSE (hasEditableKnobEntry (*editor));
    juce::Image editorImage (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics editorGraphics (editorImage);
    editor->paintEntireComponent (editorGraphics, true);
    const auto setParameter = [&processor] (const juce::String& id, float value)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                parameter->setValue (value);
    };
    const auto getParameter = [&processor] (const juce::String& id)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                return parameter;
        return static_cast<juce::AudioProcessorParameter*> (nullptr);
    };

    setParameter ("threshold", 0.6f);
    setParameter ("ratio", 1.0f);
    setParameter ("attack", 0.0f);
    setParameter ("release", 0.0f);
    setParameter ("knee", 0.0f);
    setParameter ("mix", 1.0f);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (4, 512);
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 0.8f);
        buffer.setSample (1, sample, 0.6f);
    }
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 511) < 0.2f);
    REQUIRE (processor.gainReductionDb() > 10.0f);
    REQUIRE (processor.outputPeak (0) > 0.0f);
    REQUIRE (processor.outputPeak (1) > 0.0f);
    REQUIRE (processor.outputPeakHold() >= processor.outputPeak (0));
    std::array<CompMasterProcessor::ScopeColumn, 8> scope {};
    REQUIRE (processor.copyScopeSnapshot (scope.data(), static_cast<int> (scope.size())) > 0);
    REQUIRE (std::any_of (scope.begin(), scope.end(),
        [] (const auto& column) { return std::abs (column.maximum) > 0.01f; }));
    REQUIRE (getParameter ("attack")->getText (0.0f, 16).contains ("ms"));
    processor.resetVisualization();
    REQUIRE (processor.outputPeakHold() == 0.0f);
    REQUIRE (processor.copyScopeSnapshot (scope.data(), static_cast<int> (scope.size())) == 0);

    setParameter ("bypass", 1.0f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 1.2f);
        buffer.setSample (1, sample, -1.1f);
    }
    processor.processBlock (buffer, midi);
    REQUIRE (processor.outputPeakHold() >= 1.2f);
    processor.resetVisualization();
    REQUIRE (processor.outputPeakHold() == 0.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    CompMasterProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (std::abs (getParameter ("ratio")->getValue()
                       - restored.getParameters()[1]->getValue()) < 0.00001f);
    REQUIRE (std::abs (getParameter ("mix")->getValue()
                       - restored.getParameters()[9]->getValue()) < 0.00001f);
    processor.setSidechainSourceTrackId (27, false);
    processor.selectABState (1);
    setParameter ("ratio", 0.0f);
    processor.getStateInformation (state);
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.sidechainSourceTrackId() == 27);
    REQUIRE (restored.activeABSlot() == 1);
    REQUIRE (restored.getParameters()[1]->getValue() == 0.0f);
    restored.selectABState (0);
    REQUIRE (std::abs (restored.getParameters()[1]->getValue() - 1.0f) < 0.00001f);
}

TEST_CASE ("Comp Master mix blends dry audio to preserve transient weight")
{
    CompMasterProcessor processor;
    processor.getParameters()[0]->setValue (0.6f);
    processor.getParameters()[1]->setValue (1.0f);
    processor.getParameters()[2]->setValue (0.0f);
    processor.getParameters()[3]->setValue (0.0f);
    processor.getParameters()[4]->setValue (0.0f);
    processor.getParameters()[9]->setValue (0.5f);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (4, 512);
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample (0, sample, 0.8f);
        buffer.setSample (1, sample, 0.8f);
    }

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 511) > 0.4f);
    REQUIRE (buffer.getSample (0, 511) < 0.5f);
}

TEST_CASE ("Comp Master visualization keeps only the newest scope columns")
{
    CompMasterProcessor processor;
    processor.getParameters()[8]->setValue (1.0f);
    processor.prepareToPlay (120.0, 1);

    juce::AudioBuffer<float> buffer (4, 1);
    juce::MidiBuffer midi;
    for (int column = 0; column <= CompMasterProcessor::scopeBufferSize; ++column)
    {
        buffer.clear();
        const float input = column == 0 ? 0.9f : 0.1f;
        buffer.setSample (0, 0, input);
        buffer.setSample (1, 0, input);
        processor.processBlock (buffer, midi);
    }

    std::vector<CompMasterProcessor::ScopeColumn> scope (
        static_cast<std::size_t> (CompMasterProcessor::scopeBufferSize + 1));
    const int count = processor.copyScopeSnapshot (
        scope.data(), static_cast<int> (scope.size()));

    REQUIRE (count == CompMasterProcessor::scopeBufferSize);
    REQUIRE (std::none_of (scope.begin(), scope.begin() + count,
        [] (const auto& column) { return column.maximum > 0.2f; }));
}

TEST_CASE ("EQ Master applies its eight parametric bands and restores their state")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    EqMasterProcessor processor;
    REQUIRE (processor.getName() == "EQ Master");
    REQUIRE (processor.getParameters().size() == EqMasterProcessor::bandCount * 6 + 3);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (editor != nullptr);
    REQUIRE (editor->getWidth() == 760);
    REQUIRE (editor->getHeight() == 480);
    CHECK_FALSE (hasEditableKnobEntry (*editor));
    juce::Image editorImage (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics editorGraphics (editorImage);
    editor->paintEntireComponent (editorGraphics, true);
    const auto getParameter = [&processor] (const juce::String& id)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                return parameter;
        return static_cast<juce::AudioProcessorParameter*> (nullptr);
    };
    for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        REQUIRE (getParameter ("band" + juce::String (band + 1) + "_enabled")->getValue() == 0.0f);
    auto* frequency = getParameter ("band1_frequency");
    auto* gain = getParameter ("band1_gain");
    REQUIRE (frequency != nullptr);
    REQUIRE (gain != nullptr);
    getParameter ("band1_enabled")->setValue (1.0f);
    frequency->setValue (frequency->getValueForText ("1000 Hz"));
    gain->setValue (gain->getValueForText ("18 dB"));
    processor.prepareToPlay (48000.0, 4096);

    juce::AudioBuffer<float> buffer (2, 4096);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float value = 0.1f * std::sin (juce::MathConstants<float>::twoPi
                                             * 1000.0f * static_cast<float> (sample) / 48000.0f);
        buffer.setSample (0, sample, value);
        buffer.setSample (1, sample, value);
    }
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    double outputRms = 0.0;
    for (int sample = 2048; sample < buffer.getNumSamples(); ++sample)
        outputRms += std::pow (buffer.getSample (0, sample), 2.0);
    outputRms = std::sqrt (outputRms / 2048.0);
    REQUIRE (outputRms > 0.3);
    std::array<float, EqMasterProcessor::analyzerBufferSize> preAnalyzer {}, postAnalyzer {};
    REQUIRE (processor.copyAnalyzerSnapshot (false, preAnalyzer.data(),
                static_cast<int> (preAnalyzer.size())) == EqMasterProcessor::analyzerBufferSize);
    REQUIRE (processor.copyAnalyzerSnapshot (true, postAnalyzer.data(),
                static_cast<int> (postAnalyzer.size())) == EqMasterProcessor::analyzerBufferSize);
    const auto analyzerRms = [] (const auto& samples)
    {
        double sum = 0.0;
        for (const auto sample : samples)
            sum += static_cast<double> (sample) * sample;
        return std::sqrt (sum / samples.size());
    };
    REQUIRE (analyzerRms (postAnalyzer) > analyzerRms (preAnalyzer) * 3.0);
    processor.resetAnalyzer();
    REQUIRE (processor.copyAnalyzerSnapshot (true, postAnalyzer.data(),
                static_cast<int> (postAnalyzer.size())) == EqMasterProcessor::analyzerBufferSize);
    REQUIRE (analyzerRms (postAnalyzer) == 0.0);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    EqMasterProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (std::abs (getParameter ("band1_frequency")->getValue()
                       - restored.getParameters()[1]->getValue()) < 0.00001f);
    processor.selectABState (1);
    gain->setValue (0.5f);
    processor.getStateInformation (state);
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (restored.activeABSlot() == 1);
    restored.selectABState (0);
    REQUIRE (restored.getParameters()[2]->getValue() > 0.99f);

    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);
    const int compId = engine.addNativeEffect (1, AudioEngine::compMasterPluginIdentifier);
    const int eqId = engine.addNativeEffect (1, AudioEngine::eqMasterPluginIdentifier);
    REQUIRE (compId > 0);
    REQUIRE (eqId > 0);
    const auto devices = engine.devices (1);
    REQUIRE (devices.size() == 2);
    REQUIRE (devices[0].name == "Comp Master");
    REQUIRE (devices[0].automatableParameters.size() == 10);
    REQUIRE (devices[1].name == "EQ Master");
    REQUIRE (devices[1].automatableParameters.size() == EqMasterProcessor::bandCount * 6 + 3);
    const auto states = engine.getPluginStates();
    REQUIRE (states.size() == 2);
    REQUIRE (states[0].identifier == AudioEngine::compMasterPluginIdentifier);
    REQUIRE (states[1].identifier == AudioEngine::eqMasterPluginIdentifier);
    REQUIRE (states[0].state.getSize() > 0);
    REQUIRE (states[1].state.getSize() > 0);
}

TEST_CASE ("EQ Master supports filter slopes and global Mid/Side processing")
{
    EqMasterProcessor lowPass;
    const auto setParameter = [] (juce::AudioProcessor& processor, const juce::String& id,
                                  const juce::String& text)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                parameter->setValue (parameter->getValueForText (text));
    };

    setParameter (lowPass, "band1_enabled", "On");
    setParameter (lowPass, "band1_type", "Low Pass");
    setParameter (lowPass, "band1_slope", "48 dB/oct");
    setParameter (lowPass, "band1_frequency", "1000 Hz");
    lowPass.prepareToPlay (48000.0, 4096);
    juce::AudioBuffer<float> highTone (2, 4096);
    for (int sample = 0; sample < highTone.getNumSamples(); ++sample)
    {
        const float value = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                             * 8000.0f * static_cast<float> (sample) / 48000.0f);
        highTone.setSample (0, sample, value);
        highTone.setSample (1, sample, value);
    }
    juce::MidiBuffer midi;
    lowPass.processBlock (highTone, midi);
    double filteredRms = 0.0;
    for (int sample = 2048; sample < highTone.getNumSamples(); ++sample)
        filteredRms += std::pow (highTone.getSample (0, sample), 2.0);
    filteredRms = std::sqrt (filteredRms / 2048.0);
    REQUIRE (filteredRms < 0.01);

    EqMasterProcessor sideEq;
    setParameter (sideEq, "band1_enabled", "On");
    setParameter (sideEq, "band1_frequency", "1000 Hz");
    setParameter (sideEq, "band1_gain", "18 dB");
    setParameter (sideEq, "mode", "Side");
    sideEq.prepareToPlay (48000.0, 4096);
    juce::AudioBuffer<float> sideTone (2, 4096);
    for (int sample = 0; sample < sideTone.getNumSamples(); ++sample)
    {
        const float value = 0.1f * std::sin (juce::MathConstants<float>::twoPi
                                              * 1000.0f * static_cast<float> (sample) / 48000.0f);
        sideTone.setSample (0, sample, value);
        sideTone.setSample (1, sample, -value);
    }
    sideEq.processBlock (sideTone, midi);
    double sideRms = 0.0;
    for (int sample = 2048; sample < sideTone.getNumSamples(); ++sample)
        sideRms += std::pow (sideTone.getSample (0, sample), 2.0);
    sideRms = std::sqrt (sideRms / 2048.0);
    REQUIRE (sideRms > 0.4);
    REQUIRE (std::abs (sideTone.getSample (0, 4095) + sideTone.getSample (1, 4095)) < 0.001f);
}

TEST_CASE ("Comp Master sidechain routes reject feedback and restore valid sources")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);
    engine.addTrack (2, roll::TrackType::Audio);
    engine.addTrack (3, roll::TrackType::Audio);
    const int compId = engine.addNativeEffect (1, AudioEngine::compMasterPluginIdentifier);
    REQUIRE (compId > 0);
    REQUIRE (engine.canUseCompMasterSidechainSource (1, 2));
    REQUIRE_FALSE (engine.canUseCompMasterSidechainSource (1, 1));
    REQUIRE (engine.canUseCompMasterSidechainSource (1, 0));

    CompMasterProcessor stateSource;
    stateSource.setSidechainSourceTrackId (2, false);
    juce::MemoryBlock state;
    stateSource.getStateInformation (state);
    const auto connectionCount = engine.graph().getConnections().size();
    REQUIRE (engine.restorePluginState (1, compId, state));
    REQUIRE (engine.graph().getConnections().size() == connectionCount + 2);

    stateSource.setSidechainSourceTrackId (0, false);
    stateSource.getStateInformation (state);
    const auto masterConnectionCount = engine.graph().getConnections().size();
    REQUIRE (engine.restorePluginState (1, compId, state));
    REQUIRE (engine.graph().getConnections().size() == masterConnectionCount + 2);
    const auto savedStates = engine.getPluginStates();
    REQUIRE (savedStates.size() == 1);
    REQUIRE (juce::String::fromUTF8 (static_cast<const char*> (savedStates[0].state.getData()),
                                    static_cast<int> (savedStates[0].state.getSize()))
                 .contains ("sidechainSourceTrackId=\"0\""));

    CompMasterProcessor sidechainCompressor;
    sidechainCompressor.prepareToPlay (48000.0, 4096);
    sidechainCompressor.setSidechainSourceTrackId (2, false);
    auto* threshold = sidechainCompressor.getParameters()[0];
    auto* ratio = sidechainCompressor.getParameters()[1];
    threshold->setValue (0.7f);
    ratio->setValue (1.0f);
    sidechainCompressor.getParameters()[9]->setValue (1.0f);
    juce::AudioBuffer<float> detectorSignal (4, 4096);
    for (int sample = 0; sample < detectorSignal.getNumSamples(); ++sample)
    {
        detectorSignal.setSample (0, sample, 0.1f);
        detectorSignal.setSample (1, sample, 0.1f);
        const float detector = 0.8f * std::sin (juce::MathConstants<float>::twoPi
            * 1000.0f * static_cast<float> (sample) / 48000.0f);
        detectorSignal.setSample (2, sample, detector);
        detectorSignal.setSample (3, sample, detector);
    }
    juce::MidiBuffer midi;
    sidechainCompressor.processBlock (detectorSignal, midi);
    REQUIRE (sidechainCompressor.gainReductionDb() > 5.0f);
    REQUIRE (std::abs (detectorSignal.getSample (0, 4095)) < 0.05f);
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

TEST_CASE ("Drum Master exposes stable automatable parameters for every pad")
{
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    REQUIRE (processor.getName() == "Drum Master");
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
    CHECK_FALSE (hasEditableKnobEntry (*editor));
}

TEST_CASE ("Drum Master knob double-click restores each pad default")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporarySampleFile sample;
    DrumRackProcessor processor;
    juce::String error;
    REQUIRE (processor.loadPadFromFile (0, sample.file, error));
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (editor != nullptr);

    std::array<juce::Slider*, 4> knobs {};
    int knobCount = 0;
    for (int index = 0; index < editor->getNumChildComponents(); ++index)
        if (auto* slider = dynamic_cast<juce::Slider*> (editor->getChildComponent (index));
            slider != nullptr
            && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
        {
            REQUIRE (knobCount < static_cast<int> (knobs.size()));
            knobs[static_cast<std::size_t> (knobCount++)] = slider;
        }
    REQUIRE (knobCount == static_cast<int> (knobs.size()));
    REQUIRE (knobs[0]->isDoubleClickReturnEnabled());
    REQUIRE (knobs[1]->isDoubleClickReturnEnabled());
    REQUIRE (knobs[2]->isDoubleClickReturnEnabled());
    REQUIRE (knobs[3]->isDoubleClickReturnEnabled());

    knobs[0]->setValue (2.0, juce::sendNotificationSync);
    knobs[1]->setValue (12.0, juce::sendNotificationSync);
    knobs[2]->setValue (200.0, juce::sendNotificationSync);
    knobs[3]->setValue (800.0, juce::sendNotificationSync);
    const auto now = juce::Time::getCurrentTime();
    for (auto* slider : knobs)
    {
        const auto point = slider->getLocalBounds().getCentre().toFloat();
        const juce::MouseEvent doubleClick (
            juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            slider, slider, now, point, now, 2, false);
        slider->mouseDoubleClick (doubleClick);
        CHECK (slider->getValue() == slider->getDoubleClickReturnValue());
    }

    const auto settings = processor.padSettings (0);
    CHECK (settings.gain == 1.0f);
    CHECK (settings.pitchSemitones == 0);
    CHECK (settings.startSample == 0);
    CHECK (settings.endSample == processor.padSampleLength (0));
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
    REQUIRE (processor.padName (1).contains ("Empty"));
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

    const auto load = [&] (const juce::File& file, bool analyzeReference,
                           bool analyzeWaveform = false)
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
            engine.loadAudioFileAsync (file, callback, analyzeWaveform);

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

    const auto arrangementAudio = load (audioFile.file, false, true);
    CHECK (arrangementAudio->estimatedKey.isEmpty());
    CHECK (arrangementAudio->spectrum.empty());
    CHECK_FALSE (arrangementAudio->waveformColumns.empty());

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

    juce::AudioBuffer<float> rendered (2, juce::roundToInt (engine.offlineRenderSampleRate()));
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
    REQUIRE (rendered.getMagnitude (1, rendered.getNumSamples()) > 0.1f);
}

TEST_CASE ("Frozen MIDI track audio is routed directly to its track output")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Midi);
    engine.setTrackFrozen (1, true);

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
    juce::AudioBuffer<float> rendered (2, juce::roundToInt (engine.offlineRenderSampleRate()));
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    CHECK (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
    CHECK (rendered.getMagnitude (1, rendered.getNumSamples()) > 0.1f);

    engine.setTrackType (1, roll::TrackType::Audio);
    engine.setTrackFrozen (1, false);
    engine.publishAudioClips (1, { clip });
    REQUIRE (engine.beginOfflineTrackRender (AudioEngine::masterTrackId, error));
    rendered.clear();
    const auto flattenedResult = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();
    REQUIRE (flattenedResult.wasOk());
    CHECK (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
    CHECK (rendered.getMagnitude (1, rendered.getNumSamples()) > 0.1f);
}

TEST_CASE ("Offline rendering rejects MIDI tracks without an instrument")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Midi);

    juce::String error;
    CHECK_FALSE (engine.beginOfflineTrackRender (1, error));
    CHECK (error.contains ("no instrument"));
}

TEST_CASE ("Frozen tracks reject plugin insertion until unfrozen")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Midi);
    juce::PluginDescription instrument;
    instrument.isInstrument = true;

    CHECK (engine.canAcceptPlugin (1, instrument));
    engine.setTrackFrozen (1, true);
    CHECK_FALSE (engine.canAcceptPlugin (1, instrument));
    engine.setTrackFrozen (1, false);
    CHECK (engine.canAcceptPlugin (1, instrument));
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

    const int sampleRate = juce::roundToInt (engine.offlineRenderSampleRate());
    juce::AudioBuffer<float> rendered (2, juce::roundToInt (0.1 * sampleRate));
    const auto result = engine.renderTrackRange (0.0, 0.2, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (std::abs (rendered.getSample (0, 0)) < 0.0001f);
    const int earlySample = juce::roundToInt (80.0 * sampleRate / 44100.0);
    const int settledSample = juce::roundToInt (400.0 * sampleRate / 44100.0);
    REQUIRE (rendered.getSample (0, earlySample) > 0.05f);
    REQUIRE (rendered.getSample (0, earlySample) < 0.5f);
    REQUIRE (rendered.getSample (0, settledSample) > 0.49f);
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
    const int sampleRate = juce::roundToInt (engine.offlineRenderSampleRate());
    juce::AudioBuffer<float> rendered (2, sampleRate);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (rendered.getSample (0, juce::roundToInt (1000.0 * sampleRate / 44100.0))
             > rendered.getSample (0, juce::roundToInt (30000.0 * sampleRate / 44100.0)));
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
    const int sampleRate = juce::roundToInt (engine.offlineRenderSampleRate());
    juce::AudioBuffer<float> rendered (2, 2 * sampleRate);
    const auto result = engine.renderTrackRange (0.0, 4.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    CHECK (std::abs (rendered.getSample (0, sampleRate) - 0.75f) < 0.02f);
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
    const int sampleRate = juce::roundToInt (engine.offlineRenderSampleRate());
    juce::AudioBuffer<float> rendered (2, sampleRate);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    const float lowerGain = rendered.getSample (0, juce::roundToInt (10000.0 * sampleRate / 44100.0));
    const float higherGain = rendered.getSample (0, juce::roundToInt (40000.0 * sampleRate / 44100.0));
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
    juce::AudioBuffer<float> rendered (2, juce::roundToInt (engine.offlineRenderSampleRate()));
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

TEST_CASE ("Sequencer graph publishes transport timing to its plugin nodes")
{
    SequencerGraph graph;
    graph.setBpm (120.0);
    graph.setLoop (false);
    graph.setPlaying (true);
    graph.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    graph.processBlock (buffer, midi);

    auto position = graph.getPlayHead()->getPosition();
    REQUIRE (position.hasValue());
    REQUIRE (position->getBpm().hasValue());
    CHECK (*position->getBpm() == 120.0);
    CHECK (position->getIsPlaying());
    REQUIRE (position->getPpqPosition().hasValue());
    CHECK (*position->getPpqPosition() == 0.0);

    graph.processBlock (buffer, midi);
    position = graph.getPlayHead()->getPosition();
    REQUIRE (position.hasValue());
    REQUIRE (position->getPpqPosition().hasValue());
    CHECK (*position->getPpqPosition() > 0.0);
    graph.releaseResources();
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

TEST_CASE ("A saved VST3 preset can be restored by a plugin worker",
           "[integration][plugin-worker][plugin-state]")
{
    const auto projectPath = juce::SystemStats::getEnvironmentVariable (
        "MAXXXTUDIO_TEST_PROJECT", {});
    const auto pluginName = juce::SystemStats::getEnvironmentVariable (
        "MAXXXTUDIO_TEST_PLUGIN", {});
    if (projectPath.isEmpty() || pluginName.isEmpty())
    {
        SUCCEED ("Set MAXXXTUDIO_TEST_PROJECT and MAXXXTUDIO_TEST_PLUGIN to test a saved plugin state.");
        return;
    }

    const auto projectXml = juce::XmlDocument::parse (juce::File (projectPath));
    REQUIRE (projectXml != nullptr);
    auto* pluginsNode = projectXml->getChildByName ("Plugins");
    REQUIRE (pluginsNode != nullptr);

    juce::MemoryBlock savedState;
    for (auto* node = pluginsNode->getFirstChildElement(); node != nullptr; node = node->getNextElement())
        if (node->hasTagName ("Plugin")
            && node->getStringAttribute ("identifier").containsIgnoreCase (pluginName))
        {
            REQUIRE (savedState.fromBase64Encoding (node->getStringAttribute ("state")));
            break;
        }
    REQUIRE (savedState.getSize() > 0);

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

    juce::String error;
    auto defaultProcessor = RemotePluginProcessor::create (*description, 44100.0, 512, error);
    INFO ("Plugin creation failed: " << error);
    REQUIRE (defaultProcessor != nullptr);
    std::vector<float> defaultParameters;
    for (auto* parameter : defaultProcessor->getParameters())
        defaultParameters.push_back (parameter != nullptr ? parameter->getValue() : 0.0f);

    auto processor = RemotePluginProcessor::create (*description, 44100.0, 512, error);
    INFO ("Plugin creation failed: " << error);
    REQUIRE (processor != nullptr);
    processor->setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));
    REQUIRE_FALSE (processor->hasFailed());
    std::size_t changedParameterCount = 0;
    for (int i = 0; i < std::min (processor->getParameters().size(),
                                  static_cast<int> (defaultParameters.size())); ++i)
        if (std::abs (processor->getParameters().getUnchecked (i)->getValue()
                      - defaultParameters[static_cast<std::size_t> (i)]) > 1.0e-5f)
            ++changedParameterCount;
    INFO ("Parameters changed from plugin defaults: " << changedParameterCount);
    if (pluginName.containsIgnoreCase ("Nexus"))
        CHECK (changedParameterCount > 0);

    juce::MemoryBlock restoredState;
    processor->getStateInformation (restoredState);
    REQUIRE_FALSE (processor->hasFailed());
    REQUIRE (restoredState.getSize() > 0);

    auto repeatedState = restoredState;
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        auto repeatedProcessor = RemotePluginProcessor::create (*description, 44100.0, 512, error);
        INFO ("Plugin creation failed: " << error);
        REQUIRE (repeatedProcessor != nullptr);
        repeatedProcessor->setStateInformation (
            repeatedState.getData(), static_cast<int> (repeatedState.getSize()));
        REQUIRE_FALSE (repeatedProcessor->hasFailed());
        juce::MemoryBlock nextState;
        repeatedProcessor->getStateInformation (nextState);
        REQUIRE_FALSE (repeatedProcessor->hasFailed());
        REQUIRE (nextState.getSize() > 0);
        repeatedState = std::move (nextState);
    }
    INFO ("Saved / first restore / repeated restore bytes: "
          << savedState.getSize() << " / " << restoredState.getSize()
          << " / " << repeatedState.getSize());
    CHECK (repeatedState.getSize() > 0);

    if (pluginName.containsIgnoreCase ("Effectrix") || pluginName.containsIgnoreCase ("Glitch"))
    {
        auto effectProcessor = RemotePluginProcessor::create (*description, 44100.0, 512, error);
        INFO ("Plugin creation failed: " << error);
        REQUIRE (effectProcessor != nullptr);
        TestTransportPlayHead transport;
        effectProcessor->setPlayHead (&transport);
        effectProcessor->setStateInformation (
            savedState.getData(), static_cast<int> (savedState.getSize()));
        REQUIRE_FALSE (effectProcessor->hasFailed());

        constexpr int blockSize = 512;
        constexpr int blocksToProcess = 400;
        constexpr double twoPi = 6.283185307179586;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double maximumDifference = 0.0;
        for (int block = 0; block < blocksToProcess; ++block)
        {
            transport.setSamplePosition (static_cast<std::int64_t> (block) * blockSize);
            std::array<float, blockSize> inputSamples {};
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const double position = static_cast<double> (block * blockSize + sample);
                const float input = static_cast<float> (0.5 * std::sin (twoPi * 440.0 * position / 44100.0));
                inputSamples[static_cast<std::size_t> (sample)] = input;
                buffer.setSample (0, sample, input);
                buffer.setSample (1, sample, input);
            }
            effectProcessor->processBlock (buffer, midi);
            REQUIRE_FALSE (effectProcessor->hasFailed());
            for (int sample = 0; sample < blockSize; ++sample)
                maximumDifference = std::max (maximumDifference, std::abs (
                    static_cast<double> (buffer.getSample (0, sample)
                                         - inputSamples[static_cast<std::size_t> (sample)])));
        }
        INFO ("Maximum processed-signal difference: " << maximumDifference);
        CHECK (maximumDifference > 1.0e-5);
    }
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
