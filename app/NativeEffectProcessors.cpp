#include "NativeEffectProcessors.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace
{
class EffectParameter final : public juce::AudioProcessorParameterWithID
{
public:
    enum class Kind
    {
        Decibels, Width, Balance, Percent, Rate, Degrees, Toggle, Shape, ChannelMode
    };

    EffectParameter (const juce::String& id, const juce::String& name,
                     float initialValue, float defaultValue, Kind kind)
        : juce::AudioProcessorParameterWithID (juce::ParameterID (id, 1), name),
          value (initialValue), defaultValue (defaultValue), kind (kind)
    {
    }

    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float newValue) override
    {
        newValue = std::clamp (newValue, 0.0f, 1.0f);
        if (kind == Kind::Toggle)
            newValue = newValue >= 0.5f ? 1.0f : 0.0f;
        else if (kind == Kind::ChannelMode)
            newValue = std::round (newValue * 3.0f) / 3.0f;
        value.store (newValue, std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return defaultValue; }
    juce::String getText (float normalized, int) const override
    {
        switch (kind)
        {
            case Kind::Decibels: return juce::String (decibelsFromNormalized (normalized), 1) + " dB";
            case Kind::Width: return juce::String (normalized * 200.0f, 0) + "%";
            case Kind::Balance: return juce::String (normalized * 200.0f - 100.0f, 0);
            case Kind::Percent: return juce::String (normalized * 100.0f, 0) + "%";
            case Kind::Rate:
            {
                const auto rate = UtilityProcessor::rateFromNormalized (normalized);
                return juce::String (rate, rate >= 10.0f ? 1 : 2) + " Hz";
            }
            case Kind::Degrees: return juce::String (normalized * 360.0f, 0) + "\u00b0";
            case Kind::Toggle: return normalized >= 0.5f ? "On" : "Off";
            case Kind::Shape:
                return juce::String (normalized * 100.0f, 0) + "%";
            case Kind::ChannelMode:
            {
                static constexpr std::array<const char*, 4> names { "Stereo", "Left", "Right", "Swap" };
                return names[static_cast<std::size_t> (std::clamp (static_cast<int> (std::round (normalized * 3.0f)),
                                                                    0, 3))];
            }
        }
        jassertfalse;
        return {};
    }
    float getValueForText (const juce::String& text) const override
    {
        const auto number = text.getFloatValue();
        switch (kind)
        {
            case Kind::Decibels: return normalizedFromDecibels (number);
            case Kind::Width: return std::clamp (number / 200.0f, 0.0f, 1.0f);
            case Kind::Balance: return std::clamp ((number + 100.0f) / 200.0f, 0.0f, 1.0f);
            case Kind::Percent: return std::clamp (number / 100.0f, 0.0f, 1.0f);
            case Kind::Rate: return UtilityProcessor::normalizedFromRate (number);
            case Kind::Degrees: return std::clamp (number / 360.0f, 0.0f, 1.0f);
            case Kind::Toggle: return text.equalsIgnoreCase ("On") ? 1.0f : 0.0f;
            case Kind::Shape: return std::clamp (number / 100.0f, 0.0f, 1.0f);
            case Kind::ChannelMode:
                for (int index = 0; index < 4; ++index)
                    if (getText (static_cast<float> (index) / 3.0f, 32)
                            .equalsIgnoreCase (text))
                        return static_cast<float> (index) / 3.0f;
                return 0.0f;
        }
        jassertfalse;
        return 0.0f;
    }

    Kind getKind() const { return kind; }
    static float decibelsFromNormalized (float normalized)
    {
        return normalized * 84.0f - 60.0f;
    }

    static float normalizedFromDecibels (float decibels)
    {
        return std::clamp ((decibels + 60.0f) / 84.0f, 0.0f, 1.0f);
    }

private:
    std::atomic<float> value;
    float defaultValue;
    Kind kind;
};

float autoPanWaveform (float phase, float shape)
{
    const float sine = std::sin (phase);
    return std::copysign (std::pow (std::abs (sine), 1.0f - shape), sine);
}

class ParameterButton final : public juce::TextButton
{
public:
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (onDoubleClick != nullptr)
            onDoubleClick();
        else
            juce::TextButton::mouseDoubleClick (event);
    }
};

class ParameterComboBox final : public juce::ComboBox
{
public:
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (onDoubleClick != nullptr)
            onDoubleClick();
        else
            juce::ComboBox::mouseDoubleClick (event);
    }
};

class NativeEffectEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit NativeEffectEditor (UtilityProcessor& processor)
        : juce::AudioProcessorEditor (processor), title ("Volume Master"), utility (processor)
    {
        setSize (700, 400);
        setResizable (false, false);
        for (auto* parameter : processor.getParameters())
            if (parameter != nullptr)
            {
                auto control = std::make_unique<Control>();
                control->parameter = parameter;
                control->definition = dynamic_cast<EffectParameter*> (parameter);
                jassert (control->definition != nullptr);
                control->label.setText (parameter->getName (64), juce::dontSendNotification);
                control->label.setColour (juce::Label::textColourId, theme::text());
                control->label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                control->label.setJustificationType (juce::Justification::centred);
                addAndMakeVisible (control->label);

                if (control->definition->getKind() == EffectParameter::Kind::Toggle)
                {
                    control->toggle = std::make_unique<ParameterButton>();
                    control->toggle->setClickingTogglesState (true);
                    control->toggle->setToggleState (parameter->getValue() >= 0.5f,
                                                     juce::dontSendNotification);
                    control->toggle->setButtonText (parameter->getValue() >= 0.5f ? "On" : "Off");
                    control->toggle->setColour (juce::TextButton::buttonColourId, theme::control());
                    control->toggle->setColour (juce::TextButton::buttonOnColourId, theme::accent());
                    control->toggle->setColour (juce::TextButton::textColourOffId, theme::text());
                    control->toggle->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                    control->toggle->onClick = [parameter, button = control->toggle.get()]
                    {
                        parameter->beginChangeGesture();
                        parameter->setValue (button->getToggleState() ? 1.0f : 0.0f);
                        parameter->endChangeGesture();
                    };
                    control->toggle->onDoubleClick = [this, parameter, button = control->toggle.get()]
                    {
                        resetParameter (*parameter);
                        button->setToggleState (parameter->getValue() >= 0.5f, juce::dontSendNotification);
                        button->setButtonText (parameter->getValue() >= 0.5f ? "On" : "Off");
                    };
                    addAndMakeVisible (*control->toggle);
                }
                else if (control->definition->getKind() == EffectParameter::Kind::ChannelMode)
                {
                    control->choice = std::make_unique<ParameterComboBox>();
                    constexpr int count = 4;
                    for (int index = 0; index < count; ++index)
                        control->choice->addItem (
                            parameter->getText (static_cast<float> (index) / static_cast<float> (count - 1), 32),
                            index + 1);
                    control->choice->setSelectedId (
                        static_cast<int> (std::round (parameter->getValue() * static_cast<float> (count - 1))) + 1,
                        juce::dontSendNotification);
                    control->choice->setColour (juce::ComboBox::backgroundColourId, theme::control());
                    control->choice->setColour (juce::ComboBox::textColourId, theme::text());
                    control->choice->setColour (juce::ComboBox::outlineColourId, theme::border());
                    control->choice->onChange = [parameter, choice = control->choice.get(), count]
                    {
                        parameter->beginChangeGesture();
                        parameter->setValue (static_cast<float> (choice->getSelectedId() - 1)
                                             / static_cast<float> (count - 1));
                        parameter->endChangeGesture();
                    };
                    control->choice->onDoubleClick = [this, parameter, choice = control->choice.get(), count]
                    {
                        resetParameter (*parameter);
                        choice->setSelectedId (
                            static_cast<int> (std::round (parameter->getValue()
                                                          * 3.0f)) + 1,
                            juce::dontSendNotification);
                    };
                    addAndMakeVisible (*control->choice);
                }
                else
                {
                    control->slider = std::make_unique<juce::Slider>();
                    auto& slider = *control->slider;
                    slider.setName (parameter->getName (64));
                    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 24);
                    slider.setRange (0.0, 1.0, 0.001);
                    slider.setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
                    slider.setColour (juce::Slider::rotarySliderOutlineColourId, theme::border());
                    slider.setColour (juce::Slider::thumbColourId, theme::text());
                    slider.setColour (juce::Slider::textBoxTextColourId, theme::text());
                    slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::control());
                    slider.setColour (juce::Slider::textBoxOutlineColourId, theme::border());
                    slider.textFromValueFunction = [parameter] (double value)
                    {
                        return parameter->getText (static_cast<float> (value), 24);
                    };
                    slider.valueFromTextFunction = [parameter] (const juce::String& text)
                    {
                        return parameter->getValueForText (text);
                    };
                    slider.setValue (parameter->getValue(), juce::dontSendNotification);
                    slider.setDoubleClickReturnValue (true, parameter->getDefaultValue());
                    slider.onDragStart = [parameter] { parameter->beginChangeGesture(); };
                    slider.onDragEnd = [parameter] { parameter->endChangeGesture(); };
                    slider.onValueChange = [parameter, sliderPointer = &slider]
                    {
                        parameter->setValue (static_cast<float> (sliderPointer->getValue()));
                    };
                    addAndMakeVisible (slider);
                }
                if (control->slider != nullptr)
                    valueControls.push_back (control.get());
                else
                    modeControls.push_back (control.get());
                controls.push_back (std::move (control));
            }
        resized();
        startTimerHz (60);
    }

    ~NativeEffectEditor() override
    {
        stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel());
        g.setColour (theme::text());
        g.setFont (18.0f);
        g.drawText (title, getLocalBounds().removeFromTop (34),
                    juce::Justification::centred, true);

        auto visual = getLocalBounds().reduced (16);
        visual.removeFromTop (38);
        visual.setHeight (100);
        g.setColour (theme::control());
        g.fillRoundedRectangle (visual.toFloat(), 6.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (visual.toFloat(), 6.0f, 1.0f);
        drawPanVisualization (g, visual.reduced (12, 8));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (38);
        area.removeFromTop (100);
        area.removeFromTop (8);
        auto valueArea = area.removeFromTop (170);
        const int valueCellWidth = valueArea.getWidth()
                                 / std::max (1, static_cast<int> (valueControls.size()));
        for (std::size_t index = 0; index < valueControls.size(); ++index)
        {
            auto cell = cellForIndex (valueArea, index, valueCellWidth, valueControls.size());
            auto& control = *valueControls[index];
            control.label.setBounds (cell.removeFromTop (38));
            control.slider->setBounds (cell.reduced (2, 0));
        }

        area.removeFromTop (4);
        const int modeCellWidth = area.getWidth() / std::max (1, static_cast<int> (modeControls.size()));
        for (std::size_t index = 0; index < modeControls.size(); ++index)
        {
            auto cell = cellForIndex (area, index, modeCellWidth, modeControls.size());
            auto& control = *modeControls[index];
            control.label.setBounds (cell.removeFromTop (22));
            auto widget = cell.reduced (5, 2);
            if (control.toggle != nullptr)
                control.toggle->setBounds (widget);
            else if (control.choice != nullptr)
                control.choice->setBounds (widget);
        }
    }

private:
    struct Control
    {
        juce::AudioProcessorParameter* parameter = nullptr;
        EffectParameter* definition = nullptr;
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<ParameterButton> toggle;
        std::unique_ptr<ParameterComboBox> choice;
    };

    static juce::Rectangle<int> cellForIndex (juce::Rectangle<int> area, std::size_t index,
                                              int cellWidth, std::size_t count)
    {
        const int x = area.getX() + static_cast<int> (index) * cellWidth;
        const int width = index + 1 == count ? area.getRight() - x : cellWidth;
        return area.withX (x).withWidth (width);
    }

    void resetParameter (juce::AudioProcessorParameter& parameter)
    {
        parameter.beginChangeGesture();
        parameter.setValue (parameter.getDefaultValue());
        parameter.endChangeGesture();
    }

    void timerCallback() override
    {
        for (auto& control : controls)
        {
            const auto value = control->parameter->getValue();
            if (control->slider != nullptr)
                control->slider->setValue (value, juce::dontSendNotification);
            if (control->toggle != nullptr)
            {
                control->toggle->setToggleState (value >= 0.5f, juce::dontSendNotification);
                control->toggle->setButtonText (value >= 0.5f ? "On" : "Off");
            }
            if (control->choice != nullptr)
            {
                control->choice->setSelectedId (
                    static_cast<int> (std::round (value * 3.0f)) + 1,
                    juce::dontSendNotification);
            }
        }
        repaint();
    }

    float parameterValue (const juce::String& identifier) const
    {
        for (auto* parameter : utility.getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == identifier)
                return parameter->getValue();
        return 0.0f;
    }

    void drawPanVisualization (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto waveform = bounds.removeFromTop (64);
        waveform.reduce (28, 0);
        const float centreY = static_cast<float> (waveform.getCentreY());
        const float amplitude = std::max (2.0f, static_cast<float> (waveform.getHeight()) * 0.44f);
        g.setColour (theme::border());
        g.drawLine (static_cast<float> (waveform.getX()), centreY,
                    static_cast<float> (waveform.getRight()), centreY, 1.0f);

        juce::Path leftPath, rightPath;
        const float amount = parameterValue ("pan_amount");
        const float phaseOffset = parameterValue ("pan_phase")
                                * juce::MathConstants<float>::twoPi;
        const float shape = parameterValue ("pan_shape");
        constexpr int points = 180;
        for (int index = 0; index <= points; ++index)
        {
            const float proportion = static_cast<float> (index) / static_cast<float> (points);
            const float phase = proportion * juce::MathConstants<float>::twoPi
                              + static_cast<float> (utility.modulationPhase());
            const float leftModulation = autoPanWaveform (phase, shape);
            const float rightModulation = autoPanWaveform (phase + phaseOffset, shape);
            const auto x = static_cast<float> (waveform.getX())
                         + proportion * static_cast<float> (waveform.getWidth());
            const auto leftY = centreY - leftModulation * amount * amplitude;
            const auto rightY = centreY - rightModulation * amount * amplitude;
            if (index == 0)
            {
                leftPath.startNewSubPath (x, leftY);
                rightPath.startNewSubPath (x, rightY);
            }
            else
            {
                leftPath.lineTo (x, leftY);
                rightPath.lineTo (x, rightY);
            }
        }
        g.setColour (theme::accent());
        g.strokePath (leftPath, juce::PathStrokeType (2.0f));
        g.setColour (theme::warning());
        g.strokePath (rightPath, juce::PathStrokeType (2.0f));

        const auto directionLabels = bounds.removeFromBottom (20);
        const auto panField = directionLabels.reduced (26, 0);
        const auto panCentreY = static_cast<float> (panField.getCentreY());
        g.setColour (theme::border());
        g.drawLine (static_cast<float> (panField.getX()), panCentreY,
                    static_cast<float> (panField.getRight()), panCentreY, 2.0f);
        g.drawLine (static_cast<float> (panField.getCentreX()), panCentreY - 4.0f,
                    static_cast<float> (panField.getCentreX()), panCentreY + 4.0f, 1.0f);

        const float balance = parameterValue ("balance") * 2.0f - 1.0f;
        const float balanceAngle = (balance + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float leftBalanceGain = std::cos (balanceAngle) * juce::MathConstants<float>::sqrt2;
        const float rightBalanceGain = std::sin (balanceAngle) * juce::MathConstants<float>::sqrt2;
        const float currentPhase = static_cast<float> (utility.modulationPhase());
        const float leftModulation = autoPanWaveform (currentPhase, shape);
        const float rightModulation = autoPanWaveform (currentPhase + phaseOffset, shape);
        const float leftGain = leftBalanceGain * (1.0f - amount * 0.5f * (1.0f - leftModulation));
        const float rightGain = rightBalanceGain * (1.0f - amount * 0.5f * (1.0f - rightModulation));
        const float totalGain = leftGain + rightGain;
        const float panPosition = totalGain > 0.000001f
                                ? juce::jlimit (-1.0f, 1.0f, (rightGain - leftGain) / totalGain)
                                : 0.0f;
        const float markerX = static_cast<float> (panField.getCentreX())
                            + panPosition * static_cast<float> (panField.getWidth()) * 0.5f;
        g.setColour (theme::accent());
        g.fillEllipse (juce::Rectangle<float> (markerX - 4.0f, panCentreY - 4.0f, 8.0f, 8.0f));

        g.setColour (theme::textDim());
        g.drawText ("L", directionLabels.withWidth (20), juce::Justification::centredLeft);
        g.drawText ("R", directionLabels.withX (directionLabels.getRight() - 20).withWidth (20),
                    juce::Justification::centredRight);
    }

    juce::String title;
    UtilityProcessor& utility;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Control*> valueControls, modeControls;
};

juce::AudioProcessorEditor* makeNativeEditor (UtilityProcessor& processor)
{
    return new NativeEffectEditor (processor);
}
}

UtilityProcessor::UtilityProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (new EffectParameter ("gain", "Gain", 5.0f / 7.0f, 5.0f / 7.0f,
                                       EffectParameter::Kind::Decibels));
    addParameter (new EffectParameter ("width", "Width", 0.5f, 0.5f,
                                       EffectParameter::Kind::Width));
    addParameter (new EffectParameter ("balance", "Balance", 0.5f, 0.5f,
                                       EffectParameter::Kind::Balance));
    addParameter (new EffectParameter ("channel_mode", "Input", 0.0f, 0.0f,
                                       EffectParameter::Kind::ChannelMode));
    addParameter (new EffectParameter ("mono", "Mono", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("invert_phase", "Invert Phase", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("pan_amount", "Amount", 0.0f, 0.0f,
                                       EffectParameter::Kind::Percent));
    addParameter (new EffectParameter ("pan_rate", "Rate", 0.5f, 0.5f,
                                       EffectParameter::Kind::Rate));
    addParameter (new EffectParameter ("pan_phase", "Phase", 0.5f, 0.5f,
                                       EffectParameter::Kind::Degrees));
    addParameter (new EffectParameter ("pan_shape", "Shape", 0.0f, 0.0f,
                                       EffectParameter::Kind::Shape));
    addParameter (new EffectParameter ("bass_mono", "Bass Mono", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("mute", "Mute", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("dc", "DC", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
}

void UtilityProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    std::fill (std::begin (dcPreviousInput), std::end (dcPreviousInput), 0.0f);
    std::fill (std::begin (dcPreviousOutput), std::end (dcPreviousOutput), 0.0f);
    std::fill (std::begin (bassLowpass), std::end (bassLowpass), 0.0f);
    currentLfoPhase.store (0.0, std::memory_order_relaxed);
}

void UtilityProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2)
        return;

    const auto parameterValue = [this] (const juce::String& id)
    {
        for (auto* parameter : getParameters())
            if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                identified != nullptr && identified->paramID == id)
                return parameter->getValue();
        return 0.0f;
    };

    const float gain = std::pow (10.0f, EffectParameter::decibelsFromNormalized (
                                     parameterValue ("gain")) / 20.0f);
    const float width = parameterValue ("width") * 2.0f;
    const float balance = parameterValue ("balance") * 2.0f - 1.0f;
    const int channelMode = static_cast<int> (std::round (parameterValue ("channel_mode") * 3.0f));
    const bool mono = parameterValue ("mono") >= 0.5f;
    const float phaseSign = parameterValue ("invert_phase") >= 0.5f ? -1.0f : 1.0f;
    const float panAmount = parameterValue ("pan_amount");
    const float panRate = rateFromNormalized (parameterValue ("pan_rate"));
    const float panPhase = parameterValue ("pan_phase") * juce::MathConstants<float>::twoPi;
    const float panShape = parameterValue ("pan_shape");
    const bool bassMonoEnabled = parameterValue ("bass_mono") >= 0.5f;
    const bool muted = parameterValue ("mute") >= 0.5f;
    const bool removeDc = parameterValue ("dc") >= 0.5f;
    const float balanceAngle = (balance + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float leftBalanceGain = std::cos (balanceAngle) * juce::MathConstants<float>::sqrt2;
    const float rightBalanceGain = std::sin (balanceAngle) * juce::MathConstants<float>::sqrt2;
    const float dcCoefficient = std::exp (-juce::MathConstants<float>::twoPi * 10.0f
                                          / static_cast<float> (sampleRate));
    const float bassCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                   * 100.0f / static_cast<float> (sampleRate));
    double phase = currentLfoPhase.load (std::memory_order_relaxed);
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float l = left[sample];
        float r = right[sample];
        if (channelMode == 1)
            r = l;
        else if (channelMode == 2)
            l = r;
        else if (channelMode == 3)
            std::swap (l, r);

        const float filteredLeft = l - dcPreviousInput[0] + dcCoefficient * dcPreviousOutput[0];
        const float filteredRight = r - dcPreviousInput[1] + dcCoefficient * dcPreviousOutput[1];
        dcPreviousInput[0] = l;
        dcPreviousInput[1] = r;
        dcPreviousOutput[0] = filteredLeft;
        dcPreviousOutput[1] = filteredRight;
        if (removeDc)
        {
            l = filteredLeft;
            r = filteredRight;
        }

        bassLowpass[0] += bassCoefficient * (l - bassLowpass[0]);
        bassLowpass[1] += bassCoefficient * (r - bassLowpass[1]);
        if (bassMonoEnabled)
        {
            const float sharedBass = (bassLowpass[0] + bassLowpass[1]) * 0.5f;
            l += sharedBass - bassLowpass[0];
            r += sharedBass - bassLowpass[1];
        }

        const float mid = (l + r) * 0.5f;
        const float side = (l - r) * 0.5f * width;
        l = mid + side;
        r = mid - side;
        if (mono)
            l = r = mid;

        const float phaseAtSample = static_cast<float> (phase);
        const float leftModulation = autoPanWaveform (phaseAtSample, panShape);
        const float rightModulation = autoPanWaveform (phaseAtSample + panPhase, panShape);
        const float leftAutoPanGain = 1.0f - panAmount * 0.5f * (1.0f - leftModulation);
        const float rightAutoPanGain = 1.0f - panAmount * 0.5f * (1.0f - rightModulation);

        left[sample] = muted ? 0.0f : l * leftBalanceGain * leftAutoPanGain * gain * phaseSign;
        right[sample] = muted ? 0.0f : r * rightBalanceGain * rightAutoPanGain * gain * phaseSign;

        phase += juce::MathConstants<double>::twoPi * panRate / sampleRate;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase = std::fmod (phase, juce::MathConstants<double>::twoPi);
    }
    currentLfoPhase.store (phase, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* UtilityProcessor::createEditor() { return makeNativeEditor (*this); }

AutoPanProcessor::AutoPanProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (new EffectParameter ("rate", "Rate", 0.5f, 0.5f,
                                       EffectParameter::Kind::Rate));
    addParameter (new EffectParameter ("depth", "Depth", 0.5f, 0.5f,
                                       EffectParameter::Kind::Percent));
    addParameter (new EffectParameter ("phase", "Phase", 0.0f, 0.0f,
                                       EffectParameter::Kind::Degrees));
}

void AutoPanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2)
        return;
    float rate = 0.05f;
    float depth = 0.5f;
    float offset = 0.0f;
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            if (identified->paramID == "rate")
                rate = UtilityProcessor::rateFromNormalized (parameter->getValue());
            else if (identified->paramID == "depth")
                depth = parameter->getValue();
            else if (identified->paramID == "phase")
                offset = parameter->getValue() * juce::MathConstants<float>::twoPi;
        }
    const double advance = juce::MathConstants<double>::twoPi * rate / std::max (sampleRate, 1.0);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float pan = std::sin (static_cast<float> (phase) + static_cast<float> (offset)) * depth;
        buffer.applyGain (0, sample, 1, juce::jlimit (0.0f, 1.0f, 1.0f - pan * 0.5f));
        buffer.applyGain (1, sample, 1, juce::jlimit (0.0f, 1.0f, 1.0f + pan * 0.5f));
        phase += advance;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
}

juce::AudioProcessorEditor* AutoPanProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

namespace
{
void storeNativeParameterState (juce::AudioProcessor& processor, juce::MemoryBlock& destination)
{
    juce::XmlElement xml ("NativeEffect");
    xml.setAttribute ("gainRangeVersion", 3);
    xml.setAttribute ("rateRangeVersion", 1);
    for (int index = 0; index < static_cast<int> (processor.getParameters().size()); ++index)
    {
        auto* parameter = processor.getParameters()[static_cast<std::size_t> (index)];
        if (parameter == nullptr)
            continue;
        auto* node = xml.createNewChildElement ("Parameter");
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            node->setAttribute ("id", identified->paramID);
        node->setAttribute ("value", static_cast<double> (parameter->getValue()));
    }
    juce::MemoryOutputStream stream;
    xml.writeTo (stream);
    destination.replaceAll (stream.getData(), stream.getDataSize());
}

void restoreNativeParameterState (juce::AudioProcessor& processor, const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), size);
    auto xml = juce::XmlDocument::parse (text);
    if (xml == nullptr || ! xml->hasTagName ("NativeEffect"))
        return;
    const int gainRangeVersion = xml->getIntAttribute ("gainRangeVersion", 0);
    bool containsUnifiedAutoPanParameters = false;
    for (auto* node = xml->getFirstChildElement(); node != nullptr; node = node->getNextElement())
        containsUnifiedAutoPanParameters = containsUnifiedAutoPanParameters
                                        || node->getStringAttribute ("id") == "pan_rate";

    for (auto* node = xml->getFirstChildElement(); node != nullptr; node = node->getNextElement())
    {
        auto id = node->getStringAttribute ("id");
        float value = static_cast<float> (node->getDoubleAttribute ("value", 0.0));
        if (id == "gain")
        {
            if (gainRangeVersion < 2)
                value = (2.0f + value) / 3.0f;
            if (gainRangeVersion < 3)
                value *= 6.0f / 7.0f;
        }
        else if (id == "pan_rate" || id == "rate")
        {
            if (xml->getIntAttribute ("rateRangeVersion", 0) < 1)
                value = UtilityProcessor::migrateLegacyRate (value);
            if (id == "rate" && dynamic_cast<UtilityProcessor*> (&processor) != nullptr)
                id = "pan_rate";
        }
        else if (id == "invert_left" || id == "invert_right")
        {
            id = "invert_phase";
            for (auto* candidate : processor.getParameters())
                if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (candidate);
                    identified != nullptr && identified->paramID == id)
                    value = std::max (value, candidate->getValue());
        }
        else if (id == "width" && ! containsUnifiedAutoPanParameters)
            value *= 0.5f;
        else if (id == "rate")
            id = "pan_rate";
        else if (id == "depth")
        {
            id = "pan_amount";
        }
        else if (id == "phase")
            id = "pan_phase";
        for (int index = 0; index < static_cast<int> (processor.getParameters().size()); ++index)
        {
            auto* parameter = processor.getParameters()[static_cast<std::size_t> (index)];
            auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            if (identified != nullptr && identified->paramID == id)
            {
                parameter->setValue (value);
                break;
            }
        }
    }
}
}

void UtilityProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    storeNativeParameterState (*this, destination);
}

void UtilityProcessor::setStateInformation (const void* data, int size)
{
    restoreNativeParameterState (*this, data, size);
}

void AutoPanProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    storeNativeParameterState (*this, destination);
}

void AutoPanProcessor::setStateInformation (const void* data, int size)
{
    restoreNativeParameterState (*this, data, size);
}
