#include "NativeEffectProcessors.h"
#include "ThemeKnobSlider.h"
#include "Theme.h"
#include "WaveformDisplay.h"
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
class EffectParameter final : public juce::AudioProcessorParameterWithID
{
public:
    enum class Kind
    {
        Decibels, Width, Balance, Percent, Rate, Degrees, Toggle, Shape, ChannelMode,
        Threshold, Ratio, Attack, Release, Knee, Makeup, Frequency, EqGain, Quality,
        FilterType, FilterSlope, MidSide
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
        else if (kind == Kind::FilterType)
            newValue = std::round (newValue * 3.0f) / 3.0f;
        else if (kind == Kind::FilterSlope)
            newValue = std::round (newValue * 2.0f) / 2.0f;
        else if (kind == Kind::MidSide)
            newValue = std::round (newValue * 2.0f) / 2.0f;
        value.store (newValue, std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return defaultValue; }
    juce::String getText (float normalized, int) const override
    {
        switch (kind)
        {
            case Kind::Decibels:
            {
                const auto decibels = decibelsFromNormalized (normalized);
                return juce::String (std::abs (decibels) < 0.05f ? 0.0f : decibels, 1) + " dB";
            }
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
            case Kind::Threshold: return juce::String (normalized * 60.0f - 60.0f, 1) + " dB";
            case Kind::Ratio: return juce::String (1.0f + normalized * 19.0f, 1) + ":1";
            case Kind::Attack:
                return juce::String (0.1f * std::pow (2000.0f, normalized), 1) + " ms";
            case Kind::Release:
                return juce::String (1.0f * std::pow (1000.0f, normalized), 1) + " ms";
            case Kind::Knee: return juce::String (normalized * 24.0f, 1) + " dB";
            case Kind::Makeup: return juce::String (normalized * 24.0f, 1) + " dB";
            case Kind::Frequency:
                return juce::String (20.0f * std::pow (1000.0f, normalized), 0) + " Hz";
            case Kind::EqGain: return juce::String (normalized * 36.0f - 18.0f, 1) + " dB";
            case Kind::Quality: return juce::String (0.1f * std::pow (100.0f, normalized), 2);
            case Kind::FilterType:
            {
                static constexpr std::array<const char*, 4> names {
                    "Bell", "Low Pass", "High Pass", "Notch"
                };
                return names[static_cast<std::size_t> (
                    std::clamp (static_cast<int> (std::round (normalized * 3.0f)), 0, 3))];
            }
            case Kind::FilterSlope:
            {
                static constexpr std::array<const char*, 3> names {
                    "12 dB/oct", "24 dB/oct", "48 dB/oct"
                };
                return names[static_cast<std::size_t> (
                    std::clamp (static_cast<int> (std::round (normalized * 2.0f)), 0, 2))];
            }
            case Kind::MidSide:
            {
                static constexpr std::array<const char*, 3> names { "Stereo", "Mid", "Side" };
                return names[static_cast<std::size_t> (
                    std::clamp (static_cast<int> (std::round (normalized * 2.0f)), 0, 2))];
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
            case Kind::Threshold: return std::clamp ((number + 60.0f) / 60.0f, 0.0f, 1.0f);
            case Kind::Ratio: return std::clamp ((number - 1.0f) / 19.0f, 0.0f, 1.0f);
            case Kind::Attack:
                return std::clamp (std::log (std::max (number, 0.1f) / 0.1f)
                                   / std::log (2000.0f), 0.0f, 1.0f);
            case Kind::Release:
                return std::clamp (std::log (std::max (number, 1.0f))
                                   / std::log (1000.0f), 0.0f, 1.0f);
            case Kind::Knee: return std::clamp (number / 24.0f, 0.0f, 1.0f);
            case Kind::Makeup: return std::clamp (number / 24.0f, 0.0f, 1.0f);
            case Kind::Frequency:
                return std::clamp (std::log (std::max (number, 20.0f) / 20.0f)
                                   / std::log (1000.0f), 0.0f, 1.0f);
            case Kind::EqGain: return std::clamp ((number + 18.0f) / 36.0f, 0.0f, 1.0f);
            case Kind::Quality:
                return std::clamp (std::log (std::max (number, 0.1f) / 0.1f)
                                   / std::log (100.0f), 0.0f, 1.0f);
            case Kind::FilterType:
                for (int index = 0; index < 4; ++index)
                    if (getText (static_cast<float> (index) / 3.0f, 32).equalsIgnoreCase (text))
                        return static_cast<float> (index) / 3.0f;
                return 0.0f;
            case Kind::FilterSlope:
                for (int index = 0; index < 3; ++index)
                    if (getText (static_cast<float> (index) * 0.5f, 32).equalsIgnoreCase (text))
                        return static_cast<float> (index) * 0.5f;
                return 0.0f;
            case Kind::MidSide:
                for (int index = 0; index < 3; ++index)
                    if (getText (static_cast<float> (index) * 0.5f, 32).equalsIgnoreCase (text))
                        return static_cast<float> (index) * 0.5f;
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
                const auto* identified = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
                if (identified != nullptr
                    && (identified->paramID == "bass_mono"
                        || identified->paramID == "mute"
                        || identified->paramID == "dc"))
                    continue;

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
                    control->toggle->setColour (juce::TextButton::textColourOnId, theme::text());
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
                    control->slider = std::make_unique<ThemeKnobSlider>();
                    auto& slider = *control->slider;
                    slider.setName (parameter->getName (64));
                    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 76, 24);
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

template <std::size_t ParameterCount>
void captureABState (juce::AudioProcessor& processor,
                     std::array<std::array<float, ParameterCount>, 2>& states, int active)
{
    const auto parameters = processor.getParameters();
    for (std::size_t index = 0; index < ParameterCount && index < parameters.size(); ++index)
        states[static_cast<std::size_t> (active)][index] =
            parameters[static_cast<int> (index)]->getValue();
}

template <std::size_t ParameterCount>
void initializeABState (juce::AudioProcessor& processor,
                        std::array<std::array<float, ParameterCount>, 2>& states)
{
    const auto parameters = processor.getParameters();
    for (std::size_t index = 0; index < ParameterCount && index < parameters.size(); ++index)
        states[0][index] = states[1][index] = parameters[static_cast<int> (index)]->getValue();
}

template <std::size_t ParameterCount>
void selectABState (juce::AudioProcessor& processor,
                    std::array<std::array<float, ParameterCount>, 2>& states,
                    int& active, int slot)
{
    slot = std::clamp (slot, 0, 1);
    if (slot == active)
        return;
    captureABState (processor, states, active);
    active = slot;
    const auto parameters = processor.getParameters();
    for (std::size_t index = 0; index < ParameterCount && index < parameters.size(); ++index)
    {
        auto* parameter = parameters[static_cast<int> (index)];
        parameter->beginChangeGesture();
        parameter->setValue (states[static_cast<std::size_t> (active)][index]);
        parameter->endChangeGesture();
    }
}

template <std::size_t ParameterCount>
void saveABState (juce::XmlElement& xml,
                  const std::array<std::array<float, ParameterCount>, 2>& states, int active)
{
    xml.setAttribute ("abActive", active);
    for (int slot = 0; slot < 2; ++slot)
        for (std::size_t index = 0; index < ParameterCount; ++index)
            xml.setAttribute ("ab" + juce::String (slot) + "_" + juce::String (static_cast<int> (index)),
                              static_cast<double> (states[static_cast<std::size_t> (slot)][index]));
}

template <std::size_t ParameterCount>
void restoreABState (juce::AudioProcessor& processor, const juce::XmlElement& xml,
                     std::array<std::array<float, ParameterCount>, 2>& states, int& active)
{
    const juce::String firstAttribute = "ab0_0";
    if (! xml.hasAttribute (firstAttribute))
    {
        initializeABState (processor, states);
        active = 0;
        return;
    }

    active = std::clamp (xml.getIntAttribute ("abActive", 0), 0, 1);
    for (int slot = 0; slot < 2; ++slot)
        for (std::size_t index = 0; index < ParameterCount; ++index)
        {
            const auto attribute = "ab" + juce::String (slot) + "_"
                                 + juce::String (static_cast<int> (index));
            states[static_cast<std::size_t> (slot)][index] = static_cast<float> (
                xml.getDoubleAttribute (attribute,
                    states[static_cast<std::size_t> (slot)][index]));
        }

    const auto parameters = processor.getParameters();
    for (std::size_t index = 0; index < ParameterCount && index < parameters.size(); ++index)
        parameters[static_cast<int> (index)]->setValue (
            states[static_cast<std::size_t> (active)][index]);
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

namespace
{
class ParameterKnob final : public juce::Component
{
public:
    explicit ParameterKnob (juce::AudioProcessorParameter& parameter)
    {
        label.setColour (juce::Label::textColourId, theme::text());
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, 22);
        slider.setRange (0.0, 1.0, 0.001);
        slider.setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, theme::border());
        slider.setColour (juce::Slider::thumbColourId, theme::text());
        slider.setColour (juce::Slider::textBoxTextColourId, theme::text());
        slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::control());
        slider.setColour (juce::Slider::textBoxOutlineColourId, theme::border());
        slider.textFromValueFunction = [this] (double value)
        {
            return target != nullptr ? target->getText (static_cast<float> (value), 24)
                                     : juce::String();
        };
        slider.valueFromTextFunction = [this] (const juce::String& text)
        {
            return target != nullptr ? target->getValueForText (text) : 0.0;
        };
        slider.onDragStart = [this]
        {
            if (onGestureStarted != nullptr)
                onGestureStarted();
            else if (target != nullptr)
                target->beginChangeGesture();
        };
        slider.onDragEnd = [this]
        {
            if (onGestureEnded != nullptr)
                onGestureEnded();
            else if (target != nullptr)
                target->endChangeGesture();
        };
        slider.onValueChange = [this]
        {
            if (onValueChanged != nullptr)
                onValueChanged (static_cast<float> (slider.getValue()));
            else if (target != nullptr)
                target->setValue (static_cast<float> (slider.getValue()));
        };
        addAndMakeVisible (slider);
        setParameter (parameter);
    }

    void setParameter (juce::AudioProcessorParameter& parameter)
    {
        target = &parameter;
        label.setText (target->getName (48), juce::dontSendNotification);
        slider.setName (target->getName (48));
        slider.setDoubleClickReturnValue (true, target->getDefaultValue());
        refresh();
    }

    void refresh()
    {
        if (target != nullptr)
            slider.setValue (target->getValue(), juce::dontSendNotification);
    }

    std::function<void (float)> onValueChanged;
    std::function<void()> onGestureStarted, onGestureEnded;

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (24));
        slider.setBounds (area);
    }

private:
    juce::AudioProcessorParameter* target = nullptr;
    juce::Label label;
    ThemeKnobSlider slider;
};

struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

BiquadCoefficients makePeakingCoefficients (double frequency, double gainDb,
                                             double quality, double rate)
{
    const double safeRate = std::max (rate, 100.0);
    const double omega = juce::MathConstants<double>::twoPi
                       * std::clamp (frequency, 10.0, safeRate * 0.45) / safeRate;
    const double gain = std::pow (10.0, gainDb / 40.0);
    const double alpha = std::sin (omega) / (2.0 * std::clamp (quality, 0.1, 10.0));
    const double a0 = 1.0 + alpha / gain;
    const double cosine = std::cos (omega);
    return {
        (1.0 + alpha * gain) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha * gain) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha / gain) / a0
    };
}

BiquadCoefficients makeLowPassCoefficients (double frequency, double quality, double rate)
{
    const double safeRate = std::max (rate, 100.0);
    const double omega = juce::MathConstants<double>::twoPi
                       * std::clamp (frequency, 10.0, safeRate * 0.45) / safeRate;
    const double cosine = std::cos (omega);
    const double alpha = std::sin (omega) / (2.0 * std::clamp (quality, 0.1, 10.0));
    const double a0 = 1.0 + alpha;
    return { (1.0 - cosine) * 0.5 / a0, (1.0 - cosine) / a0,
             (1.0 - cosine) * 0.5 / a0, -2.0 * cosine / a0,
             (1.0 - alpha) / a0 };
}

BiquadCoefficients makeHighPassCoefficients (double frequency, double quality, double rate)
{
    const double safeRate = std::max (rate, 100.0);
    const double omega = juce::MathConstants<double>::twoPi
                       * std::clamp (frequency, 10.0, safeRate * 0.45) / safeRate;
    const double cosine = std::cos (omega);
    const double alpha = std::sin (omega) / (2.0 * std::clamp (quality, 0.1, 10.0));
    const double a0 = 1.0 + alpha;
    return { (1.0 + cosine) * 0.5 / a0, -(1.0 + cosine) / a0,
             (1.0 + cosine) * 0.5 / a0, -2.0 * cosine / a0,
             (1.0 - alpha) / a0 };
}

BiquadCoefficients makeNotchCoefficients (double frequency, double quality, double rate)
{
    const double safeRate = std::max (rate, 100.0);
    const double omega = juce::MathConstants<double>::twoPi
                       * std::clamp (frequency, 10.0, safeRate * 0.45) / safeRate;
    const double cosine = std::cos (omega);
    const double alpha = std::sin (omega) / (2.0 * std::clamp (quality, 0.1, 10.0));
    const double a0 = 1.0 + alpha;
    return { 1.0 / a0, -2.0 * cosine / a0, 1.0 / a0,
             -2.0 * cosine / a0, (1.0 - alpha) / a0 };
}

std::array<double, 4> butterworthStageQualities (int slope, int& count)
{
    static constexpr std::array<double, 1> twelveDb { 0.70710678 };
    static constexpr std::array<double, 2> twentyFourDb { 0.54119610, 1.30656296 };
    static constexpr std::array<double, 4> fortyEightDb {
        0.50979558, 0.60134489, 0.89997622, 2.56291545
    };
    std::array<double, 4> result {};
    if (slope <= 0)
    {
        count = 1;
        std::copy (twelveDb.begin(), twelveDb.end(), result.begin());
    }
    else if (slope == 1)
    {
        count = 2;
        std::copy (twentyFourDb.begin(), twentyFourDb.end(), result.begin());
    }
    else
    {
        count = 4;
        std::copy (fortyEightDb.begin(), fortyEightDb.end(), result.begin());
    }
    return result;
}

std::array<BiquadCoefficients, 4> makeBandCoefficients (int type, int slope,
    double frequency, double gain, double quality, double rate, int& stageCount)
{
    std::array<BiquadCoefficients, 4> result {};
    if (type == 0)
    {
        stageCount = 1;
        result[0] = makePeakingCoefficients (frequency, gain, quality, rate);
    }
    else if (type == 3)
    {
        stageCount = 1;
        result[0] = makeNotchCoefficients (frequency, quality, rate);
    }
    else
    {
        const auto qualities = butterworthStageQualities (slope, stageCount);
        for (int stage = 0; stage < stageCount; ++stage)
            result[static_cast<std::size_t> (stage)] = type == 1
                ? makeLowPassCoefficients (frequency, qualities[static_cast<std::size_t> (stage)], rate)
                : makeHighPassCoefficients (frequency, qualities[static_cast<std::size_t> (stage)], rate);
    }
    return result;
}

double responseDb (const BiquadCoefficients& c, double frequency, double rate)
{
    const double omega = juce::MathConstants<double>::twoPi * frequency / std::max (rate, 100.0);
    const double cosine = std::cos (omega);
    const double sine = std::sin (omega);
    const double cosine2 = std::cos (omega * 2.0);
    const double sine2 = std::sin (omega * 2.0);
    const double numeratorReal = c.b0 + c.b1 * cosine + c.b2 * cosine2;
    const double numeratorImaginary = -c.b1 * sine - c.b2 * sine2;
    const double denominatorReal = 1.0 + c.a1 * cosine + c.a2 * cosine2;
    const double denominatorImaginary = -c.a1 * sine - c.a2 * sine2;
    const double magnitude = std::hypot (numeratorReal, numeratorImaginary)
                           / std::max (std::hypot (denominatorReal, denominatorImaginary), 1.0e-12);
    return 20.0 * std::log10 (std::max (magnitude, 1.0e-12));
}

juce::AudioProcessorParameter* parameterWithId (juce::AudioProcessor& processor,
                                                 const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            identified != nullptr && identified->paramID == id)
            return parameter;
    return nullptr;
}

class CompMasterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CompMasterEditor (CompMasterProcessor& processor)
        : juce::AudioProcessorEditor (processor), compressor (processor)
    {
        setSize (780, 520);
        setResizable (false, false);
        for (int index = 0; index < 6; ++index)
        {
            auto control = std::make_unique<ParameterKnob> (*processor.getParameters()[index]);
            addAndMakeVisible (*control);
            controls.push_back (std::move (control));
        }
        for (int index = 6; index < 8; ++index)
        {
            auto control = std::make_unique<ParameterKnob> (*processor.getParameters()[index]);
            addAndMakeVisible (*control);
            sidechainControls.push_back (std::move (control));
        }
        auto mixControl = std::make_unique<ParameterKnob> (*processor.getParameters()[9]);
        addAndMakeVisible (*mixControl);
        controls.push_back (std::move (mixControl));
        refreshSourceItems();
        sourceBox.setSelectedId (selectedSourceItem(), juce::dontSendNotification);
        sourceBox.onChange = [this]
        {
            const int index = sourceBox.getSelectedId() - 1;
            const auto& sources = compressor.getSidechainSources();
            if (index >= 0 && index < static_cast<int> (sources.size()))
                compressor.setSidechainSourceTrackId (
                    sources[static_cast<std::size_t> (index)].trackId);
            sourceBox.setSelectedId (selectedSourceItem(), juce::dontSendNotification);
        };
        sourceBox.setColour (juce::ComboBox::backgroundColourId, theme::control());
        sourceBox.setColour (juce::ComboBox::textColourId, theme::text());
        sourceBox.setColour (juce::ComboBox::outlineColourId, theme::border());
        addAndMakeVisible (sourceBox);

        zoomOut.setButtonText ("-");
        zoomIn.setButtonText ("+");
        for (auto* button : { &zoomOut, &zoomIn })
        {
            button->setColour (juce::TextButton::buttonColourId, theme::control());
            button->setColour (juce::TextButton::textColourOffId, theme::text());
            addAndMakeVisible (*button);
        }
        zoomOut.onClick = [this] { zoomFactor = std::max (1.0, zoomFactor / 2.0); repaint(); };
        zoomIn.onClick = [this] { zoomFactor = std::min (32.0, zoomFactor * 2.0); repaint(); };

        bypass.setVisible (false);
        ab.setVisible (false);
        resetButton.setButtonText ("Reset");
        resetButton.setTooltip ("Reset the waveform history and peak hold");
        resetButton.setColour (juce::TextButton::buttonColourId, theme::control());
        resetButton.setColour (juce::TextButton::textColourOffId, theme::text());
        resetButton.onClick = [this]
        {
            compressor.resetVisualization();
            waveform.clear();
        };
        addAndMakeVisible (resetButton);
        resized();
        startTimerHz (30);
    }

    ~CompMasterEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel());
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("COMP MASTER", getLocalBounds().removeFromTop (36).reduced (16, 0),
                    juce::Justification::centredLeft, true);

        auto area = getLocalBounds().reduced (16);
        area.removeFromTop (38);
        auto sourceRow = area.removeFromTop (32);
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("SIDECHAIN SOURCE", sourceRow.removeFromLeft (126),
                    juce::Justification::centredLeft, true);
        auto scope = scopeBounds().toFloat();
        g.setColour (juce::Colour (0xff161616));
        g.fillRoundedRectangle (scope, 5.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (scope, 5.0f, 1.0f);
        drawScope (g, scope);

        const bool clipping = compressor.outputPeakHold() >= 1.0f;
        auto peakLabel = scopeBounds().withY (scopeBounds().getBottom())
                                      .withHeight (24).toFloat();
        g.setColour (clipping ? theme::recording() : theme::textDim());
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("Max DB: " + peakHoldText(), peakLabel, juce::Justification::centred, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto header = area.removeFromTop (38);
        bypass.setVisible (false);
        ab.setVisible (false);
        auto sourceRow = area.removeFromTop (32);
        sourceRow.removeFromLeft (126);
        sourceBox.setBounds (sourceRow.removeFromLeft (280).reduced (2, 2));
        sourceRow.removeFromLeft (8);
        zoomOut.setBounds (sourceRow.removeFromRight (30).reduced (2, 2));
        zoomIn.setBounds (sourceRow.removeFromRight (30).reduced (2, 2));
        area.removeFromTop (200);
        auto footer = area.removeFromTop (24);
        resetButton.setBounds (footer.removeFromRight (80).reduced (1));
        area.removeFromTop (4);
        auto controlsRow = area.removeFromTop (130);
        const int sidechainWidth = 80;
        const int gap = 8;
        const int count = static_cast<int> (controls.size());
        const int controlWidth = (controlsRow.getWidth() - sidechainWidth * 2
                                  - gap * (count + 1)) / count;
        for (int index = 0; index < count; ++index)
        {
            auto cell = controlsRow.removeFromLeft (controlWidth);
            controls[static_cast<std::size_t> (index)]->setBounds (
                cell.withSizeKeepingCentre (72, controlsRow.getHeight()));
            if (index + 1 < count)
                controlsRow.removeFromLeft (gap);
        }
        controlsRow.removeFromLeft (gap);
        for (auto& control : sidechainControls)
        {
            auto cell = controlsRow.removeFromLeft (sidechainWidth);
            control->setBounds (cell.withSizeKeepingCentre (72, controlsRow.getHeight()));
            if (control != sidechainControls.back())
                controlsRow.removeFromLeft (gap);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isCtrlDown() && scopeBounds().toFloat().contains (event.position))
        {
            zoomFactor = juce::jlimit (1.0, 32.0,
                zoomFactor * std::pow (1.2, static_cast<double> (wheel.deltaY) * 4.0));
            repaint();
            return;
        }

        juce::AudioProcessorEditor::mouseWheelMove (event, wheel);
    }

private:
    void refreshSourceItems()
    {
        sourceBox.clear (juce::dontSendNotification);
        const auto& sources = compressor.getSidechainSources();
        for (std::size_t index = 0; index < sources.size(); ++index)
            sourceBox.addItem (sources[index].name, static_cast<int> (index) + 1);
        sourceOptionsCache = sources;
        sourceBox.setSelectedId (selectedSourceItem(), juce::dontSendNotification);
    }

    bool sourceOptionsChanged() const
    {
        const auto& sources = compressor.getSidechainSources();
        if (sources.size() != sourceOptionsCache.size())
            return true;
        for (std::size_t index = 0; index < sources.size(); ++index)
            if (sources[index].trackId != sourceOptionsCache[index].trackId
                || sources[index].name != sourceOptionsCache[index].name)
                return true;
        return false;
    }

    int selectedSourceItem() const
    {
        const auto& sources = compressor.getSidechainSources();
        for (std::size_t index = 0; index < sources.size(); ++index)
            if (sources[index].trackId == compressor.sidechainSourceTrackId())
                return static_cast<int> (index) + 1;
        return sources.empty() ? 0 : 1;
    }

    juce::String peakHoldText() const
    {
        const float peak = compressor.outputPeakHold();
        return peak > 0.000001f
            ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS"
            : "-inf dBFS";
    }

    void drawScope (juce::Graphics& g, juce::Rectangle<float> scope)
    {
        auto content = scope.reduced (9.0f, 8.0f);
        auto meters = content.removeFromRight (108.0f);
        auto graph = content.reduced (30.0f, 0.0f);
        const float centreY = graph.getCentreY();
        const float halfHeight = graph.getHeight() * 0.5f;
        static constexpr std::array<float, 6> dbMarks {
            0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -60.0f
        };
        g.setFont (juce::FontOptions (8.5f));
        for (const float db : dbMarks)
        {
            const float amplitude = juce::Decibels::decibelsToGain (db);
            const float offset = amplitude * halfHeight;
            g.setColour (db == 0.0f ? theme::textDim().withAlpha (0.72f)
                                     : theme::border().withAlpha (0.5f));
            g.drawHorizontalLine (juce::roundToInt (centreY - offset), graph.getX(), graph.getRight());
            g.drawHorizontalLine (juce::roundToInt (centreY + offset), graph.getX(), graph.getRight());
            g.setColour (theme::textDim());
            const auto label = db <= -60.0f ? "-inf" : juce::String (static_cast<int> (db));
            g.drawText (label,
                        juce::Rectangle<float> (scope.getX() + 2.0f, centreY - offset - 6.0f,
                                                25.0f, 12.0f),
                        juce::Justification::centredLeft);
            if (db > -60.0f)
                g.drawText (label,
                            juce::Rectangle<float> (scope.getX() + 2.0f, centreY + offset - 6.0f,
                                                    25.0f, 12.0f),
                            juce::Justification::centredLeft);
        }

        const int count = static_cast<int> (waveform.size());
        const int visibleCount = count > 0
            ? std::max (1, juce::roundToInt (static_cast<double> (count) / zoomFactor)) : 0;
        const int start = std::max (0, count - visibleCount);
        juce::Path reductionLine;
        if (visibleCount > 0)
        {
            const int pixelWidth = std::max (1, juce::roundToInt (graph.getWidth()));
            for (int pixel = 0; pixel < pixelWidth; ++pixel)
            {
                const int first = start + pixel * visibleCount / pixelWidth;
                const int last = std::max (first + 1, start + (pixel + 1) * visibleCount / pixelWidth);
                float minimum = 0.0f, maximum = 0.0f, reduction = 0.0f;
                for (int index = first; index < std::min (last, count); ++index)
                {
                    const auto& column = waveform[static_cast<std::size_t> (index)];
                    minimum = std::min (minimum, column.minimum);
                    maximum = std::max (maximum, column.maximum);
                    reduction = std::max (reduction, column.gainReductionDb);
                }
                const float x = graph.getX() + static_cast<float> (pixel);
                const float top = centreY - juce::jlimit (0.0f, 1.0f, maximum) * halfHeight;
                const float bottom = centreY - juce::jlimit (-1.0f, 0.0f, minimum) * halfHeight;
                const float amplitude = juce::jlimit (0.0f, 1.0f,
                    std::max (std::abs (minimum), std::abs (maximum)));
                const auto waveformColour = juce::Colour (0xffa45bff)
                    .brighter (amplitude * 0.25f);
                g.setColour (waveformColour.withAlpha (0.24f));
                g.fillRect (x - 0.5f, top - 1.0f, 2.5f, std::max (2.0f, bottom - top + 2.0f));
                g.setColour (waveformColour);
                g.fillRect (x, top, 1.5f, std::max (1.0f, bottom - top));
                const float grY = centreY + juce::jlimit (0.0f, 1.0f, reduction / 12.0f) * halfHeight;
                if (pixel == 0)
                    reductionLine.startNewSubPath (x, grY);
                else
                    reductionLine.lineTo (x, grY);
            }
            g.setColour (theme::recording());
            g.strokePath (reductionLine, juce::PathStrokeType (1.5f));
        }

        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("IN", meters.removeFromLeft (32.0f), juce::Justification::centredTop);
        g.drawText ("GR", meters.removeFromLeft (32.0f), juce::Justification::centredTop);
        g.drawText ("OUT", meters, juce::Justification::centredTop);
        meters = scope.reduced (9.0f, 8.0f).removeFromRight (108.0f);
        meters.removeFromTop (15.0f);
        drawLevelMeter (g, meters.removeFromLeft (32.0f), compressor.inputPeak (0),
                        compressor.inputPeak (1));
        const float reduction = compressor.gainReductionDb();
        auto grArea = meters.removeFromLeft (32.0f);
        const float meterY = grArea.getY() + 5.0f;
        const float meterHeight = grArea.getHeight() - 10.0f;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (grArea.getX() + 8.0f, meterY, 14.0f, meterHeight);
        g.setColour (theme::recording());
        g.fillRect (grArea.getX() + 8.0f, meterY, 14.0f,
                    juce::jlimit (0.0f, meterHeight, reduction / 12.0f * meterHeight));
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String (reduction, 1), grArea.withY (grArea.getBottom() - 14.0f)
                    .withHeight (12.0f), juce::Justification::centred);
        drawLevelMeter (g, meters, compressor.outputPeak (0), compressor.outputPeak (1),
                        compressor.outputPeakHold());

    }

    static void drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> area,
                                float left, float right, float peakHold = -1.0f)
    {
        const float top = area.getY() + 5.0f;
        const float height = area.getHeight() - 23.0f;
        for (int channel = 0; channel < 2; ++channel)
        {
            const float x = area.getX() + 5.0f + channel * 11.0f;
            const float level = channel == 0 ? left : right;
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (x, top, 7.0f, height);
            const float db = level > 0.000001f
                ? juce::Decibels::gainToDecibels (level) : -48.0f;
            const float fill = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * height;
            g.setColour (db > -6.0f ? theme::warning() : theme::accent());
            g.fillRect (x, top + height - fill, 7.0f, fill);
            if (peakHold > 0.000001f)
            {
                const float heldDb = juce::Decibels::gainToDecibels (peakHold);
                const float heldY = top + height
                    - juce::jlimit (0.0f, 1.0f, (heldDb + 48.0f) / 48.0f) * height;
                g.setColour (heldDb >= 0.0f ? theme::recording() : theme::text());
                g.drawHorizontalLine (juce::roundToInt (heldY), x - 1.0f, x + 8.0f);
            }
        }
    }

    juce::Rectangle<int> scopeBounds() const
    {
        auto area = getLocalBounds().reduced (16);
        area.removeFromTop (38 + 32);
        return area.removeFromTop (200);
    }

    void timerCallback() override
    {
        if (sourceOptionsChanged())
            refreshSourceItems();
        for (auto& control : controls)
            control->refresh();
        for (auto& control : sidechainControls)
            control->refresh();
        bypass.setVisible (false);
        ab.setVisible (false);
        sourceBox.setSelectedId (selectedSourceItem(), juce::dontSendNotification);
        waveform.resize (static_cast<std::size_t> (CompMasterProcessor::scopeBufferSize));
        const int count = compressor.copyScopeSnapshot (
            waveform.data(), static_cast<int> (waveform.size()));
        waveform.resize (static_cast<std::size_t> (count));
        repaint();
    }

    CompMasterProcessor& compressor;
    std::vector<std::unique_ptr<ParameterKnob>> controls;
    std::vector<std::unique_ptr<ParameterKnob>> sidechainControls;
    std::vector<CompMasterProcessor::ScopeColumn> waveform;
    std::vector<CompMasterProcessor::SidechainSource> sourceOptionsCache;
    juce::ComboBox sourceBox;
    juce::TextButton zoomIn, zoomOut, bypass, ab, resetButton;
    double zoomFactor = 1.0;
};

class EqMasterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EqMasterEditor (EqMasterProcessor& processor)
        : juce::AudioProcessorEditor (processor), equalizer (processor), fft (11)
    {
        setSize (760, 480);
        setResizable (false, false);
        selectedBands[0] = true;

        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            auto& button = bandButtons[static_cast<std::size_t> (band)];
            button.setButtonText (juce::String (band + 1));
            button.setColour (juce::TextButton::buttonColourId, theme::control());
            button.setColour (juce::TextButton::buttonOnColourId, theme::accent());
            button.setColour (juce::TextButton::textColourOffId, theme::text());
            button.setColour (juce::TextButton::textColourOnId, theme::text());
            button.setClickingTogglesState (true);
            button.setRadioGroupId (9814);
            button.setToggleState (band == 0, juce::dontSendNotification);
            button.onClick = [this, band]
            {
                selectedBand = band;
                selectedBands.fill (false);
                selectedBands[static_cast<std::size_t> (band)] = true;
                bandButtons[static_cast<std::size_t> (band)].setToggleState (
                    true, juce::dontSendNotification);
                refreshSelectedBand();
                repaint();
            };
            button.onDoubleClick = [this, band] { resetBand (band); };
            addAndMakeVisible (button);
        }

        enabled.setButtonText ("Band enabled");
        enabled.setColour (juce::ToggleButton::textColourId, theme::text());
        enabled.setColour (juce::ToggleButton::tickColourId, theme::accent());
        enabled.onClick = [this]
        {
            if (auto* parameter = selectedParameter (0))
            {
                parameter->beginChangeGesture();
                const bool bandEnabled = enabled.getToggleState();
                parameter->setValue (bandEnabled ? 1.0f : 0.0f);
                parameter->endChangeGesture();
                selectedBands[static_cast<std::size_t> (selectedBand)] = bandEnabled;
                if (! bandEnabled)
                {
                    for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
                        if (selectedBands[static_cast<std::size_t> (band)]
                            && isBandEnabled (band))
                        {
                            selectedBand = band;
                            break;
                        }
                }
                if (selectedBand < EqMasterProcessor::bandCount)
                    bandButtons[static_cast<std::size_t> (selectedBand)].setToggleState (
                        true, juce::dontSendNotification);
                refreshSelectedBand();
                repaint();
            }
        };
        addAndMakeVisible (enabled);

        type.setTextWhenNothingSelected ("Filter");
        type.addItem ("Bell", 1);
        type.addItem ("Low Pass", 2);
        type.addItem ("High Pass", 3);
        type.addItem ("Notch", 4);
        type.onChange = [this] { setSelectedParameter (4, (type.getSelectedId() - 1) / 3.0f); };
        type.onDoubleClick = [this] { resetSelectedParameter (4); };
        addAndMakeVisible (type);

        slope.setTextWhenNothingSelected ("Slope");
        slope.addItem ("12 dB/oct", 1);
        slope.addItem ("24 dB/oct", 2);
        slope.addItem ("48 dB/oct", 3);
        slope.onChange = [this] { setSelectedParameter (5, (slope.getSelectedId() - 1) * 0.5f); };
        slope.onDoubleClick = [this] { resetSelectedParameter (5); };
        addAndMakeVisible (slope);

        channelMode.addItem ("Stereo", 1);
        channelMode.addItem ("Mid", 2);
        channelMode.addItem ("Side", 3);
        channelMode.onChange = [this]
        {
            auto* parameter = parameterWithId (equalizer, "mode");
            if (parameter != nullptr)
            {
                parameter->beginChangeGesture();
                parameter->setValue (static_cast<float> (channelMode.getSelectedId() - 1) * 0.5f);
                parameter->endChangeGesture();
            }
        };
        channelMode.setColour (juce::ComboBox::backgroundColourId, theme::control());
        channelMode.setColour (juce::ComboBox::textColourId, theme::text());
        channelMode.setColour (juce::ComboBox::outlineColourId, theme::border());
        addAndMakeVisible (channelMode);

        analyzerMode.addItem ("Analyzer: Pre", 1);
        analyzerMode.addItem ("Analyzer: Post", 2);
        analyzerMode.onChange = [this]
        {
            if (auto* parameter = parameterWithId (equalizer, "analyzer_mode"))
            {
                parameter->beginChangeGesture();
                parameter->setValue (static_cast<float> (analyzerMode.getSelectedId() - 1));
                parameter->endChangeGesture();
            }
        };
        analyzerMode.setColour (juce::ComboBox::backgroundColourId, theme::control());
        analyzerMode.setColour (juce::ComboBox::textColourId, theme::text());
        analyzerMode.setColour (juce::ComboBox::outlineColourId, theme::border());
        addAndMakeVisible (analyzerMode);

        resetAnalyzerButton.setButtonText ("Reset");
        resetAnalyzerButton.setTooltip ("Reset the spectrum peak hold");
        resetAnalyzerButton.setColour (juce::TextButton::buttonColourId, theme::control());
        resetAnalyzerButton.setColour (juce::TextButton::textColourOffId, theme::text());
        resetAnalyzerButton.onClick = [this]
        {
            equalizer.resetAnalyzer();
            analyzerPeakMagnitudes.fill (0.0f);
            repaint();
        };
        addAndMakeVisible (resetAnalyzerButton);

        bypass.setVisible (false);
        ab.setVisible (false);

        for (int offset = 1; offset <= 3; ++offset)
        {
            auto control = std::make_unique<ParameterKnob> (*selectedParameter (offset));
            addAndMakeVisible (*control);
            controls.push_back (std::move (control));
        }
        controls.front()->onGestureStarted = [this]
        {
            const bool hasSelection = hasEnabledSelection();
            for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
                if (isBandEnabled (band)
                    && (selectedBands[static_cast<std::size_t> (band)]
                        || (! hasSelection && band == selectedBand)))
                    if (auto* frequency = bandParameter (band, 1))
                        frequency->beginChangeGesture();
        };
        controls.front()->onGestureEnded = [this]
        {
            const bool hasSelection = hasEnabledSelection();
            for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
                if (isBandEnabled (band)
                    && (selectedBands[static_cast<std::size_t> (band)]
                        || (! hasSelection && band == selectedBand)))
                    if (auto* frequency = bandParameter (band, 1))
                        frequency->endChangeGesture();
        };
        controls.front()->onValueChanged = [this] (float value)
        {
            setSelectedFrequenciesFromKnob (value);
        };
        refreshSelectedBand();
        resized();
        startTimerHz (30);
    }

    ~EqMasterEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel());
        g.setColour (theme::text());
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("EQ MASTER", getLocalBounds().removeFromTop (36).reduced (18, 0),
                    juce::Justification::centredLeft, true);

        auto bounds = getLocalBounds().reduced (18);
        bounds.removeFromTop (42);
        auto graph = bounds.removeFromTop (220).toFloat().reduced (10.0f, 8.0f);
        drawResponseGraph (g, graph);
    }

    void resized() override
    {
        auto header = getLocalBounds().reduced (18).removeFromTop (36);
        channelMode.setBounds (header.removeFromRight (125).reduced (2, 4));
        analyzerMode.setBounds (header.removeFromRight (145).reduced (2, 4));
        resetAnalyzerButton.setBounds (header.removeFromRight (76).reduced (2, 4));

        auto area = getLocalBounds().reduced (18);
        area.removeFromTop (42);
        area.removeFromTop (220);
        area.removeFromTop (6);
        auto selectors = area.removeFromTop (30);
        const int buttonWidth = selectors.getWidth() / EqMasterProcessor::bandCount;
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            const int x = selectors.getX() + band * buttonWidth;
            bandButtons[static_cast<std::size_t> (band)].setBounds (
                x, selectors.getY(), bandWidth (selectors, band, buttonWidth), selectors.getHeight());
        }
        area.removeFromTop (6);
        auto controlsRow = area.removeFromTop (96);
        enabled.setBounds (controlsRow.removeFromLeft (112).removeFromTop (26));
        controlsRow.removeFromLeft (4);
        const int controlWidth = 78;
        for (auto& control : controls)
        {
            control->setBounds (controlsRow.removeFromLeft (controlWidth));
            controlsRow.removeFromLeft (2);
        }
        const int comboWidth = std::max (140, controlsRow.getWidth() / 2);
        type.setBounds (controlsRow.removeFromLeft (comboWidth).removeFromTop (28));
        slope.setBounds (controlsRow.removeFromLeft (controlsRow.getWidth()).removeFromTop (28));
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto graph = responseGraphBounds();
        if (! graph.contains (event.position))
            return;

        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
            if (! isBandEnabled (band))
                selectedBands[static_cast<std::size_t> (band)] = false;

        const auto point = event.position;
        float nearestDistance = 18.0f;
        int nearestBand = -1;
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            if (! isBandEnabled (band))
                continue;
            const auto frequency = parameterPhysicalValue (band, 1);
            const auto typeIndex = std::clamp (static_cast<int> (std::round (
                parameterWithId (equalizer, "band" + juce::String (band + 1)
                                 + "_type")->getValue() * 3.0f)), 0, 3);
            const auto gain = typeIndex == 0 ? parameterPhysicalValue (band, 2) : 0.0;
            const auto x = graphXForFrequency (graph, frequency);
            const auto y = graphYForGain (graph, gain);
            const float distance = point.getDistanceFrom (juce::Point<float> (x, y));
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestBand = band;
            }
        }
        if (nearestBand < 0)
        {
            marqueeFrom = point;
            marqueeTo = point;
            selectingMarquee = true;
            if (! event.mods.isShiftDown())
                selectedBands.fill (false);
            return;
        }

        if (event.mods.isShiftDown())
        {
            selectedBands[static_cast<std::size_t> (nearestBand)] =
                ! selectedBands[static_cast<std::size_t> (nearestBand)];
        }
        else if (! selectedBands[static_cast<std::size_t> (nearestBand)])
        {
            selectedBands.fill (false);
            selectedBands[static_cast<std::size_t> (nearestBand)] = true;
        }

        selectedBand = nearestBand;
        int selectedCount = 0;
        for (const bool selected : selectedBands)
            if (selected)
                ++selectedCount;
        if (selectedCount == 1)
            bandButtons[static_cast<std::size_t> (selectedBand)].setToggleState (
                true, juce::dontSendNotification);
        refreshSelectedBand();
        draggingNode = true;
        dragStart = point;
        dragStartFrequency.fill (0.0f);
        dragStartGain.fill (0.0f);
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
            if (selectedBands[static_cast<std::size_t> (band)])
            {
                if (! isBandEnabled (band))
                    continue;
                dragStartFrequency[static_cast<std::size_t> (band)] =
                    static_cast<float> (parameterPhysicalValue (band, 1));
                dragStartGain[static_cast<std::size_t> (band)] =
                    static_cast<float> (parameterPhysicalValue (band, 2));
                if (auto* frequency = bandParameter (band, 1))
                    frequency->beginChangeGesture();
                if (auto* gain = bandParameter (band, 2))
                    gain->beginChangeGesture();
            }
        updateSelectedNodes (point, graph);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        const auto graph = responseGraphBounds();
        if (selectingMarquee)
        {
            marqueeTo = event.position;
            updateMarqueeSelection (graph);
            repaint();
        }
        else if (draggingNode)
            updateSelectedNodes (event.position, graph);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingNode)
        {
            for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
                if (selectedBands[static_cast<std::size_t> (band)])
                {
                    if (! isBandEnabled (band))
                        continue;
                    if (auto* frequency = bandParameter (band, 1))
                        frequency->endChangeGesture();
                    if (auto* gain = bandParameter (band, 2))
                        gain->endChangeGesture();
                }
        }
        draggingNode = false;
        selectingMarquee = false;
        marqueeFrom = {};
        marqueeTo = {};
        repaint();
    }

private:
    juce::Rectangle<float> marqueeRectangle() const
    {
        return juce::Rectangle<float>::leftTopRightBottom (
            std::min (marqueeFrom.x, marqueeTo.x),
            std::min (marqueeFrom.y, marqueeTo.y),
            std::max (marqueeFrom.x, marqueeTo.x),
            std::max (marqueeFrom.y, marqueeTo.y));
    }

    void updateMarqueeSelection (juce::Rectangle<float> graph)
    {
        const auto rectangle = marqueeRectangle();
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            if (! isBandEnabled (band))
            {
                selectedBands[static_cast<std::size_t> (band)] = false;
                continue;
            }
            const auto frequency = parameterPhysicalValue (band, 1);
            const auto typeIndex = std::clamp (static_cast<int> (std::round (
                parameterWithId (equalizer, "band" + juce::String (band + 1)
                                 + "_type")->getValue() * 3.0f)), 0, 3);
            const auto gain = typeIndex == 0 ? parameterPhysicalValue (band, 2) : 0.0;
            const juce::Point<float> point (
                graphXForFrequency (graph, frequency), graphYForGain (graph, gain));
            selectedBands[static_cast<std::size_t> (band)] = rectangle.contains (point);
        }
        int firstSelected = -1;
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
            if (selectedBands[static_cast<std::size_t> (band)])
            {
                firstSelected = band;
                break;
            }
        if (firstSelected >= 0)
        {
            selectedBand = firstSelected;
            refreshSelectedBand();
        }
    }

    static int bandWidth (juce::Rectangle<int> area, int band, int nominalWidth)
    {
        return band + 1 == EqMasterProcessor::bandCount
            ? area.getRight() - (area.getX() + band * nominalWidth) : nominalWidth;
    }

    juce::AudioProcessorParameter* selectedParameter (int offset) const
    {
        return bandParameter (selectedBand, offset);
    }

    bool isBandEnabled (int band) const
    {
        const auto* parameter = bandParameter (band, 0);
        return parameter != nullptr && parameter->getValue() >= 0.5f;
    }

    bool hasEnabledSelection() const
    {
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
            if (selectedBands[static_cast<std::size_t> (band)] && isBandEnabled (band))
                return true;
        return false;
    }

    double parameterPhysicalValue (int band, int offset) const
    {
        const auto id = "band" + juce::String (band + 1) + "_"
                      + (offset == 1 ? "frequency" : offset == 2 ? "gain" : "quality");
        if (auto* parameter = parameterWithId (equalizer, id))
        {
            const auto value = parameter->getValue();
            if (offset == 1)
                return 20.0 * std::pow (1000.0, value);
            if (offset == 2)
                return value * 36.0 - 18.0;
            return 0.1 * std::pow (100.0, value);
        }
        return 0.0;
    }

    void refreshSelectedBand()
    {
        enabled.setToggleState (selectedParameter (0)->getValue() >= 0.5f,
                                juce::dontSendNotification);
        const int filterType = currentFilterType();
        for (std::size_t index = 0; index < controls.size(); ++index)
        {
            if (auto* parameter = selectedParameter (static_cast<int> (index) + 1))
                controls[index]->setParameter (*parameter);
            controls[index]->setVisible (index != 1 || filterType == 0);
            controls[index]->refresh();
        }
        type.setSelectedId (filterType + 1, juce::dontSendNotification);
        slope.setSelectedId (static_cast<int> (std::round (
            selectedParameter (5)->getValue() * 2.0f)) + 1, juce::dontSendNotification);
        slope.setEnabled (filterType == 1 || filterType == 2);
    }

    void setSelectedFrequenciesFromKnob (float normalizedFrequency)
    {
        const double targetFrequency = 20.0 * std::pow (1000.0,
            static_cast<double> (normalizedFrequency));
        const double currentFrequency = parameterPhysicalValue (selectedBand, 1);
        if (currentFrequency <= 0.0)
            return;

        const double ratio = targetFrequency / currentFrequency;
        const bool hasEnabledBands = hasEnabledSelection();
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            if (! isBandEnabled (band)
                || (! selectedBands[static_cast<std::size_t> (band)]
                    && (hasEnabledBands || band != selectedBand)))
                continue;
            const double frequency = juce::jlimit (
                20.0, 20000.0, parameterPhysicalValue (band, 1) * ratio);
            if (auto* parameter = bandParameter (band, 1))
                parameter->setValue (static_cast<float> (
                    std::log (frequency / 20.0) / std::log (1000.0)));
        }
        refreshSelectedBand();
        repaint();
    }

    int currentFilterType() const
    {
        return std::clamp (static_cast<int> (std::round (
            selectedParameter (4)->getValue() * 3.0f)), 0, 3);
    }

    void setSelectedParameter (int offset, float value)
    {
        if (auto* parameter = selectedParameter (offset))
        {
            parameter->beginChangeGesture();
            parameter->setValue (value);
            parameter->endChangeGesture();
            refreshSelectedBand();
            repaint();
        }
    }

    void resetSelectedParameter (int offset)
    {
        if (auto* parameter = selectedParameter (offset))
            setSelectedParameter (offset, parameter->getDefaultValue());
    }

    void resetBand (int band)
    {
        for (int offset = 0; offset < 6; ++offset)
            if (auto* parameter = bandParameter (band, offset))
            {
                parameter->beginChangeGesture();
                parameter->setValue (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        if (selectedBand == band)
            refreshSelectedBand();
        repaint();
    }

    juce::AudioProcessorParameter* bandParameter (int band, int offset) const
    {
        static constexpr std::array<const char*, 6> suffixes {
            "enabled", "frequency", "gain", "quality", "type", "slope"
        };
        return parameterWithId (equalizer,
            "band" + juce::String (band + 1) + "_" + suffixes[static_cast<std::size_t> (offset)]);
    }

    juce::Rectangle<float> responseGraphBounds() const
    {
        auto bounds = getLocalBounds().reduced (18);
        bounds.removeFromTop (42);
        return bounds.removeFromTop (220).toFloat().reduced (10.0f, 8.0f);
    }

    static float graphXForFrequency (juce::Rectangle<float> graph, double frequency)
    {
        const double normalized = std::log (std::clamp (frequency, 20.0, 20000.0) / 20.0)
                                / std::log (1000.0);
        return graph.getX() + static_cast<float> (normalized) * graph.getWidth();
    }

    static float graphYForGain (juce::Rectangle<float> graph, double gain)
    {
        return graph.getCentreY() - static_cast<float> (gain / 18.0) * graph.getHeight() * 0.5f;
    }

    void updateSelectedNode (juce::Point<float> point, juce::Rectangle<float> graph)
    {
        const float x = juce::jlimit (graph.getX(), graph.getRight(), point.x);
        const float y = juce::jlimit (graph.getY(), graph.getBottom(), point.y);
        const float normalizedFrequency = (x - graph.getX()) / graph.getWidth();
        const float frequency = 20.0f * std::pow (1000.0f, normalizedFrequency);
        if (auto* parameter = selectedParameter (1))
            parameter->setValue (static_cast<float> (
                std::log (frequency / 20.0f) / std::log (1000.0f)));
        const int filterType = std::clamp (static_cast<int> (std::round (
            selectedParameter (4)->getValue() * 3.0f)), 0, 3);
        if (filterType == 0)
        {
            const float gain = (graph.getCentreY() - y) / (graph.getHeight() * 0.5f) * 18.0f;
            if (auto* parameter = selectedParameter (2))
                parameter->setValue ((gain + 18.0f) / 36.0f);
        }
        for (auto& control : controls)
            control->refresh();
        repaint();
    }

    void updateSelectedNodes (juce::Point<float> point, juce::Rectangle<float> graph)
    {
        const float deltaX = (point.x - dragStart.x) / graph.getWidth();
        const float deltaY = (point.y - dragStart.y) / (graph.getHeight() * 0.5f);
        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            if (! selectedBands[static_cast<std::size_t> (band)] || ! isBandEnabled (band))
                continue;

            const double initialFrequency = juce::jlimit (
                20.0, 20000.0,
                static_cast<double> (dragStartFrequency[static_cast<std::size_t> (band)]));
            const double frequency = juce::jlimit (
                20.0, 20000.0,
                initialFrequency * std::pow (1000.0, deltaX));
            if (auto* frequencyParameter = bandParameter (band, 1))
                frequencyParameter->setValue (static_cast<float> (
                    std::log (frequency / 20.0) / std::log (1000.0)));

            if (currentFilterType() == 0)
            {
                const float gain = juce::jlimit (
                    -18.0f, 18.0f,
                    dragStartGain[static_cast<std::size_t> (band)] - deltaY * 18.0f);
                if (auto* gainParameter = bandParameter (band, 2))
                    gainParameter->setValue ((gain + 18.0f) / 36.0f);
            }
        }
        refreshSelectedBand();
        for (auto& control : controls)
            control->refresh();
        repaint();
    }

    void drawResponseGraph (juce::Graphics& g, juce::Rectangle<float> graph)
    {
        g.setColour (juce::Colour (0xff161616));
        g.fillRoundedRectangle (graph, 5.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (graph, 5.0f, 1.0f);

        constexpr std::array<double, 10> frequencies {
            20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0
        };
        for (const auto frequency : frequencies)
        {
            const auto x = graphXForFrequency (graph, frequency);
            g.setColour (theme::border().withAlpha (0.42f));
            g.drawVerticalLine (juce::roundToInt (x), graph.getY(), graph.getBottom());
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (9.5f));
            const auto label = frequency >= 1000.0
                ? juce::String (frequency / 1000.0, frequency >= 10000.0 ? 0 : 1) + "k"
                : juce::String (static_cast<int> (frequency));
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, graph.getBottom() - 15.0f,
                                                       40.0f, 12.0f),
                        juce::Justification::centred);
        }
        for (int db = -18; db <= 18; db += 6)
        {
            const auto y = graphYForGain (graph, db);
            g.setColour (db == 0 ? theme::textDim().withAlpha (0.8f)
                                 : theme::border().withAlpha (0.42f));
            g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (juce::String (db),
                        juce::Rectangle<int> (juce::roundToInt (graph.getX() + 2.0f),
                                               juce::roundToInt (y - 7.0f), 28, 14),
                        juce::Justification::centredLeft);
        }

        juce::Path response;
        constexpr int points = 240;
        for (int point = 0; point <= points; ++point)
        {
            const double frequency = 20.0 * std::pow (1000.0, static_cast<double> (point) / points);
            double totalGain = 0.0;
            for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
            {
                auto* enabledParameter = parameterWithId (
                    equalizer, "band" + juce::String (band + 1) + "_enabled");
                if (enabledParameter == nullptr || enabledParameter->getValue() < 0.5f)
                    continue;
                const double centre = parameterPhysicalValue (band, 1);
                const double gain = parameterPhysicalValue (band, 2);
                const double quality = parameterPhysicalValue (band, 3);
                const int typeIndex = std::clamp (static_cast<int> (std::round (
                    parameterWithId (equalizer, "band" + juce::String (band + 1)
                                     + "_type")->getValue() * 3.0f)), 0, 3);
                const int slopeIndex = std::clamp (static_cast<int> (std::round (
                    parameterWithId (equalizer, "band" + juce::String (band + 1)
                                     + "_slope")->getValue() * 2.0f)), 0, 2);
                if (typeIndex == 0 && std::abs (gain) < 0.001)
                    continue;
                int stageCount = 0;
                const auto stages = makeBandCoefficients (typeIndex, slopeIndex, centre, gain,
                    quality, equalizer.getSampleRate(), stageCount);
                for (int stage = 0; stage < stageCount; ++stage)
                    totalGain += responseDb (stages[static_cast<std::size_t> (stage)], frequency,
                                             equalizer.getSampleRate());
            }
            const float x = graph.getX() + graph.getWidth()
                          * static_cast<float> (point) / static_cast<float> (points);
            const float y = graphYForGain (graph, juce::jlimit (-18.0, 18.0, totalGain));
            if (point == 0)
                response.startNewSubPath (x, y);
            else
                response.lineTo (x, y);
        }
        g.setColour (theme::accent());
        drawAnalyzer (g, graph);
        g.strokePath (response, juce::PathStrokeType (2.0f));

        for (int band = 0; band < EqMasterProcessor::bandCount; ++band)
        {
            if (! isBandEnabled (band))
                continue;
            const auto frequency = parameterPhysicalValue (band, 1);
            const auto* filterType = parameterWithId (
                equalizer, "band" + juce::String (band + 1) + "_type");
            const auto gain = filterType != nullptr && filterType->getValue() < 0.1667f
                ? parameterPhysicalValue (band, 2) : 0.0;
            const auto centre = juce::Point<float> (
                graphXForFrequency (graph, frequency), graphYForGain (graph, gain));
            const bool isSelected = selectedBands[static_cast<std::size_t> (band)];
            g.setColour (theme::accent());
            g.fillEllipse (juce::Rectangle<float> (centre.x - (isSelected ? 6.0f : 4.0f),
                                                   centre.y - (isSelected ? 6.0f : 4.0f),
                                                   isSelected ? 12.0f : 8.0f,
                                                   isSelected ? 12.0f : 8.0f));
            if (isSelected)
            {
                g.setColour (theme::text().withAlpha (0.85f));
                g.drawEllipse (juce::Rectangle<float> (centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f), 1.5f);
            }
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (band + 1),
                        juce::Rectangle<int> (juce::roundToInt (centre.x + 5.0f),
                                               juce::roundToInt (centre.y - 17.0f), 18, 14),
                        juce::Justification::centred);
        }
        if (selectingMarquee)
        {
            g.setColour (theme::accent().withAlpha (0.12f));
            g.fillRect (marqueeRectangle());
            g.setColour (theme::accent());
            g.drawRect (marqueeRectangle(), 1.0f);
        }
    }

    void timerCallback() override
    {
        refreshSelectedBand();
        if (auto* mode = parameterWithId (equalizer, "mode"))
            channelMode.setSelectedId (static_cast<int> (std::round (mode->getValue() * 2.0f)) + 1,
                                       juce::dontSendNotification);
        if (auto* analyzer = parameterWithId (equalizer, "analyzer_mode"))
            analyzerMode.setSelectedId (static_cast<int> (std::round (analyzer->getValue())) + 1,
                                        juce::dontSendNotification);
        bypass.setVisible (false);
        ab.setVisible (false);
        updateAnalyzer();
        repaint();
    }

    void updateAnalyzer()
    {
        const bool postEq = parameterWithId (equalizer, "analyzer_mode")->getValue() >= 0.5f;
        const int count = equalizer.copyAnalyzerSnapshot (
            postEq, analyzerSamples.data(), static_cast<int> (analyzerSamples.size()));
        fftData.fill (0.0f);
        const int start = static_cast<int> (analyzerSamples.size()) - count;
        for (int index = 0; index < count; ++index)
        {
            const int position = start + index;
            const float window = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * static_cast<float> (position)
                / static_cast<float> (EqMasterProcessor::analyzerBufferSize - 1));
            fftData[static_cast<std::size_t> (position)] =
                analyzerSamples[static_cast<std::size_t> (index)] * window;
        }
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (int bin = 0; bin <= EqMasterProcessor::analyzerBufferSize / 2; ++bin)
        {
            const auto index = static_cast<std::size_t> (bin);
            analyzerMagnitudes[index] =
                fftData[static_cast<std::size_t> (bin)]
                / static_cast<float> (EqMasterProcessor::analyzerBufferSize / 2);
            analyzerPeakMagnitudes[index] = std::max (
                analyzerMagnitudes[index], analyzerPeakMagnitudes[index] * 0.995f);
        }
    }

    void drawAnalyzer (juce::Graphics& g, juce::Rectangle<float> graph)
    {
        auto* mode = parameterWithId (equalizer, "mode");
        const int modeIndex = mode != nullptr
            ? std::clamp (static_cast<int> (std::round (mode->getValue() * 2.0f)), 0, 2)
            : 0;
        const juce::Colour analyzerColour = modeIndex == 1
            ? juce::Colour (0xff45d7ff)
            : modeIndex == 2
                ? juce::Colour (0xffff63c7)
                : theme::accent();
        juce::Path path;
        juce::Path peakPath;
        constexpr int points = 240;
        const double rate = std::max (equalizer.getSampleRate(), 1.0);
        for (int point = 0; point <= points; ++point)
        {
            const double frequency = 20.0 * std::pow (
                1000.0, static_cast<double> (point) / points);
            const int bin = std::clamp (juce::roundToInt (
                frequency * EqMasterProcessor::analyzerBufferSize / rate),
                0, EqMasterProcessor::analyzerBufferSize / 2);
            const float magnitude = analyzerMagnitudes[static_cast<std::size_t> (bin)];
            const float db = juce::Decibels::gainToDecibels (magnitude, -90.0f);
            const float normalized = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f);
            const float x = graph.getX() + graph.getWidth()
                          * static_cast<float> (point) / static_cast<float> (points);
            const float y = graph.getBottom() - normalized * graph.getHeight();
            if (point == 0)
            {
                path.startNewSubPath (x, y);
                peakPath.startNewSubPath (x, y);
            }
            else
            {
                path.lineTo (x, y);
                const int peakBin = std::clamp (juce::roundToInt (
                    frequency * EqMasterProcessor::analyzerBufferSize / rate),
                    0, EqMasterProcessor::analyzerBufferSize / 2);
                const float peakDb = juce::Decibels::gainToDecibels (
                    analyzerPeakMagnitudes[static_cast<std::size_t> (peakBin)], -90.0f);
                const float peakNormalized = juce::jlimit (0.0f, 1.0f, (peakDb + 90.0f) / 90.0f);
                peakPath.lineTo (x, graph.getBottom() - peakNormalized * graph.getHeight());
            }
        }
        g.setColour (analyzerColour.withAlpha (0.18f));
        auto filled = path;
        filled.lineTo (graph.getRight(), graph.getBottom());
        filled.lineTo (graph.getX(), graph.getBottom());
        filled.closeSubPath();
        g.fillPath (filled);
        g.setColour (analyzerColour.withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
        g.setColour (theme::warning().withAlpha (0.7f));
        g.strokePath (peakPath, juce::PathStrokeType (1.0f));
    }

    EqMasterProcessor& equalizer;
    std::array<ParameterButton, EqMasterProcessor::bandCount> bandButtons;
    juce::ToggleButton enabled;
    ParameterComboBox type, slope;
    juce::ComboBox channelMode, analyzerMode;
    juce::TextButton bypass, ab, resetAnalyzerButton;
    juce::dsp::FFT fft;
    std::array<float, EqMasterProcessor::analyzerBufferSize * 2> fftData {};
    std::array<float, EqMasterProcessor::analyzerBufferSize> analyzerSamples {};
    std::array<float, EqMasterProcessor::analyzerBufferSize / 2 + 1> analyzerMagnitudes {};
    std::array<float, EqMasterProcessor::analyzerBufferSize / 2 + 1> analyzerPeakMagnitudes {};
    std::vector<std::unique_ptr<ParameterKnob>> controls;
    int selectedBand = 0;
    bool draggingNode = false;
    bool selectingMarquee = false;
    juce::Point<float> marqueeFrom, marqueeTo, dragStart;
    std::array<bool, EqMasterProcessor::bandCount> selectedBands {};
    std::array<float, EqMasterProcessor::bandCount> dragStartFrequency {};
    std::array<float, EqMasterProcessor::bandCount> dragStartGain {};
};
}

CompMasterProcessor::CompMasterProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , scopeMinimums (std::make_unique<std::atomic<float>[]> (scopeBufferSize))
    , scopeMaximums (std::make_unique<std::atomic<float>[]> (scopeBufferSize))
    , scopeGainReductions (std::make_unique<std::atomic<float>[]> (scopeBufferSize))
{
    addParameter (new EffectParameter ("threshold", "Threshold", 1.0f, 1.0f,
                                       EffectParameter::Kind::Threshold));
    addParameter (new EffectParameter ("ratio", "Ratio", 1.0f / 19.0f, 1.0f / 19.0f,
                                       EffectParameter::Kind::Ratio));
    addParameter (new EffectParameter ("attack", "Attack",
        std::log (10.0f / 0.1f) / std::log (2000.0f),
        std::log (10.0f / 0.1f) / std::log (2000.0f), EffectParameter::Kind::Attack));
    addParameter (new EffectParameter ("release", "Release",
        std::log (100.0f) / std::log (1000.0f),
        std::log (100.0f) / std::log (1000.0f), EffectParameter::Kind::Release));
    addParameter (new EffectParameter ("knee", "Knee", 0.25f, 0.25f,
                                       EffectParameter::Kind::Knee));
    addParameter (new EffectParameter ("makeup", "Makeup", 0.0f, 0.0f,
                                       EffectParameter::Kind::Makeup));
    addParameter (new EffectParameter ("sidechain_low", "Sidechain Low Cut", 0.0f, 0.0f,
                                       EffectParameter::Kind::Frequency));
    addParameter (new EffectParameter ("sidechain_high", "Sidechain High Cut", 1.0f, 1.0f,
                                       EffectParameter::Kind::Frequency));
    addParameter (new EffectParameter ("bypass", "Bypass", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("mix", "Mix", 0.5f, 0.5f,
                                       EffectParameter::Kind::Percent));
    for (int index = 0; index < scopeBufferSize; ++index)
    {
        scopeMinimums[static_cast<std::size_t> (index)].store (0.0f);
        scopeMaximums[static_cast<std::size_t> (index)].store (0.0f);
        scopeGainReductions[static_cast<std::size_t> (index)].store (0.0f);
    }
    initializeABState (*this, abState);
}

void CompMasterProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    scopeDivider = std::max (1, static_cast<int> (sampleRate / 120.0));
    scopeCounter = 0;
    scopeWritePosition.store (0, std::memory_order_relaxed);
    scopeAvailable.store (0, std::memory_order_relaxed);
    envelope = 0.0f;
    scopeMinimum = std::numeric_limits<float>::max();
    scopeMaximum = std::numeric_limits<float>::lowest();
    scopeGainReduction = 0.0f;
    gainReduction.store (0.0f, std::memory_order_relaxed);
    outputPeakHoldValue.store (0.0f, std::memory_order_relaxed);
    sidechainFilterStates = {};
    lastSidechainSourceId = sidechainSource.load (std::memory_order_relaxed);
    for (auto& peak : outputPeaks)
        peak.store (0.0f, std::memory_order_relaxed);
    for (auto& peak : inputPeaks)
        peak.store (0.0f, std::memory_order_relaxed);
}

void CompMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto input = getBusBuffer (buffer, true, 0);
    auto sidechain = getBusBuffer (buffer, true, 1);
    auto output = getBusBuffer (buffer, false, 0);
    if (input.getNumChannels() == 0 || output.getNumChannels() == 0)
        return;

    const auto parameters = getParameters();
    const float thresholdDb = parameters[0]->getValue() * 60.0f - 60.0f;
    const float ratio = 1.0f + parameters[1]->getValue() * 19.0f;
    const float attackMs = 0.1f * std::pow (2000.0f, parameters[2]->getValue());
    const float releaseMs = std::pow (1000.0f, parameters[3]->getValue());
    const float kneeDb = parameters[4]->getValue() * 24.0f;
    const float makeupDb = parameters[5]->getValue() * 24.0f;
    const float lowCut = 20.0f * std::pow (1000.0f, parameters[6]->getValue());
    const float highCut = 20.0f * std::pow (1000.0f, parameters[7]->getValue());
    const bool bypass = parameters[8]->getValue() >= 0.5f;
    const float mix = parameters[9]->getValue();
    const int sourceTrackId = sidechainSource.load (std::memory_order_relaxed);
    if (sourceTrackId != lastSidechainSourceId)
    {
        sidechainFilterStates = {};
        lastSidechainSourceId = sourceTrackId;
    }
    const bool useSidechain = sourceTrackId >= 0;
    const double safeHighCut = std::min (
        std::max (static_cast<double> (highCut), static_cast<double> (lowCut) * 1.01),
        sampleRate * 0.45);
    const auto highPass = makeHighPassCoefficients (lowCut, 0.70710678, sampleRate);
    const auto lowPass = makeLowPassCoefficients (safeHighCut, 0.70710678, sampleRate);
    const float attackCoefficient = std::exp (-1.0f / (static_cast<float> (sampleRate)
                                                    * attackMs * 0.001f));
    const float releaseCoefficient = std::exp (-1.0f / (static_cast<float> (sampleRate)
                                                     * releaseMs * 0.001f));
    float blockPeakLeft = 0.0f, blockPeakRight = 0.0f, blockReduction = 0.0f;
    const auto* inputLeft = input.getReadPointer (0);
    const auto* inputRight = input.getNumChannels() > 1 ? input.getReadPointer (1) : inputLeft;
    auto* outputLeft = output.getWritePointer (0);
    auto* outputRight = output.getNumChannels() > 1 ? output.getWritePointer (1) : outputLeft;
    const auto* sidechainLeft = sidechain.getNumChannels() > 0 ? sidechain.getReadPointer (0) : nullptr;
    const auto* sidechainRight = sidechain.getNumChannels() > 1 ? sidechain.getReadPointer (1)
                                                                 : sidechainLeft;
    const bool captureScope = std::max (input.getMagnitude (0, 0, input.getNumSamples()),
        input.getNumChannels() > 1 ? input.getMagnitude (1, 0, input.getNumSamples()) : 0.0f) > 1.0e-6f;
    float blockInputPeakLeft = 0.0f, blockInputPeakRight = 0.0f;

    for (int sample = 0; sample < input.getNumSamples(); ++sample)
    {
        const float dryLeft = inputLeft[sample];
        const float dryRight = inputRight[sample];
        blockInputPeakLeft = std::max (blockInputPeakLeft, std::abs (dryLeft));
        blockInputPeakRight = std::max (blockInputPeakRight, std::abs (dryRight));
        float detectorLeft = dryLeft;
        float detectorRight = dryRight;
        if (useSidechain)
        {
            detectorLeft = sidechainLeft != nullptr ? sidechainLeft[sample] : 0.0f;
            detectorRight = sidechainRight != nullptr ? sidechainRight[sample] : detectorLeft;
            const auto filterDetector = [&] (float value, int channel)
            {
                for (int filter = 0; filter < 2; ++filter)
                {
                    const auto& c = filter == 0 ? highPass : lowPass;
                    auto& state = sidechainFilterStates[static_cast<std::size_t> (filter)]
                                                       [static_cast<std::size_t> (channel)];
                    const double filtered = c.b0 * value + c.b1 * state.x1 + c.b2 * state.x2
                                          - c.a1 * state.y1 - c.a2 * state.y2;
                    state.x2 = state.x1;
                    state.x1 = value;
                    state.y2 = state.y1;
                    state.y1 = filtered;
                    value = static_cast<float> (filtered);
                }
                return value;
            };
            detectorLeft = filterDetector (detectorLeft, 0);
            detectorRight = filterDetector (detectorRight, 1);
        }
        const float detector = std::max (std::abs (detectorLeft), std::abs (detectorRight));
        const float coefficient = detector > envelope ? attackCoefficient : releaseCoefficient;
        envelope = coefficient * envelope + (1.0f - coefficient) * detector;
        const float levelDb = juce::Decibels::gainToDecibels (std::max (envelope, 1.0e-9f));
        float reductionDb = 0.0f;
        if (kneeDb > 0.0f && levelDb > thresholdDb - kneeDb * 0.5f
            && levelDb < thresholdDb + kneeDb * 0.5f)
        {
            const float distance = levelDb - thresholdDb + kneeDb * 0.5f;
            reductionDb = (1.0f / ratio - 1.0f) * distance * distance / (2.0f * kneeDb);
        }
        else if (levelDb >= thresholdDb + kneeDb * 0.5f)
        {
            reductionDb = (thresholdDb - levelDb) * (1.0f - 1.0f / ratio);
        }
        blockReduction = std::max (blockReduction, -reductionDb);
        const float gain = bypass ? 1.0f
            : juce::Decibels::decibelsToGain (reductionDb + makeupDb);
        const float blendedGain = 1.0f + (gain - 1.0f) * mix;
        outputLeft[sample] = dryLeft * blendedGain;
        if (output.getNumChannels() > 1)
            outputRight[sample] = dryRight * blendedGain;

        blockPeakLeft = std::max (blockPeakLeft, std::abs (outputLeft[sample]));
        blockPeakRight = std::max (blockPeakRight, std::abs (output.getNumChannels() > 1
                                                              ? outputRight[sample] : outputLeft[sample]));
        if (captureScope)
        {
            scopeMinimum = std::min (scopeMinimum, std::min (outputLeft[sample], outputRight[sample]));
            scopeMaximum = std::max (scopeMaximum, std::max (outputLeft[sample], outputRight[sample]));
            scopeGainReduction = std::max (scopeGainReduction, -reductionDb);
            if (++scopeCounter >= scopeDivider)
            {
                scopeCounter = 0;
                const int position = scopeWritePosition.load (std::memory_order_relaxed);
                scopeMinimums[static_cast<std::size_t> (position)].store (scopeMinimum,
                                                                           std::memory_order_relaxed);
                scopeMaximums[static_cast<std::size_t> (position)].store (scopeMaximum,
                                                                           std::memory_order_relaxed);
                scopeGainReductions[static_cast<std::size_t> (position)].store (scopeGainReduction,
                                                                                 std::memory_order_relaxed);
                const int next = (position + 1) % scopeBufferSize;
                scopeWritePosition.store (next, std::memory_order_release);
                scopeAvailable.store (std::min (scopeBufferSize,
                    scopeAvailable.load (std::memory_order_relaxed) + 1), std::memory_order_release);
                scopeMinimum = std::numeric_limits<float>::max();
                scopeMaximum = std::numeric_limits<float>::lowest();
                scopeGainReduction = 0.0f;
            }
        }
    }

    inputPeaks[0].store (std::max (blockInputPeakLeft,
        inputPeaks[0].load (std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    inputPeaks[1].store (std::max (blockInputPeakRight,
        inputPeaks[1].load (std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    outputPeaks[0].store (std::max (blockPeakLeft,
        outputPeaks[0].load (std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    outputPeaks[1].store (std::max (blockPeakRight,
        outputPeaks[1].load (std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    const float blockPeak = std::max (blockPeakLeft, blockPeakRight);
    float peakHold = outputPeakHoldValue.load (std::memory_order_relaxed);
    while (blockPeak > peakHold
           && ! outputPeakHoldValue.compare_exchange_weak (
               peakHold, blockPeak, std::memory_order_relaxed))
    {
    }
    gainReduction.store (blockReduction, std::memory_order_relaxed);
}

void CompMasterProcessor::resetVisualization()
{
    scopeAvailable.store (0, std::memory_order_relaxed);
    scopeWritePosition.store (0, std::memory_order_release);
    outputPeakHoldValue.store (0.0f, std::memory_order_relaxed);
}

int CompMasterProcessor::copyScopeSnapshot (ScopeColumn* destination, int capacity) const
{
    if (destination == nullptr || capacity <= 0)
        return 0;
    const int count = std::min (capacity, scopeAvailable.load (std::memory_order_acquire));
    const int writePosition = scopeWritePosition.load (std::memory_order_acquire);
    const int start = (writePosition - count + scopeBufferSize) % scopeBufferSize;
    for (int index = 0; index < count; ++index)
    {
        const int position = (start + index) % scopeBufferSize;
        destination[index] = {
            scopeMinimums[static_cast<std::size_t> (position)].load (std::memory_order_relaxed),
            scopeMaximums[static_cast<std::size_t> (position)].load (std::memory_order_relaxed),
            scopeGainReductions[static_cast<std::size_t> (position)].load (std::memory_order_relaxed)
        };
    }
    return count;
}

float CompMasterProcessor::inputPeak (int channel) const
{
    return inputPeaks[static_cast<std::size_t> (std::clamp (channel, 0, 1))]
        .load (std::memory_order_relaxed);
}

float CompMasterProcessor::outputPeak (int channel) const
{
    return outputPeaks[static_cast<std::size_t> (std::clamp (channel, 0, 1))]
        .load (std::memory_order_relaxed);
}

juce::AudioProcessorEditor* CompMasterProcessor::createEditor()
{
    return new CompMasterEditor (*this);
}

void CompMasterProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    captureABState (*this, abState, activeAB);
    storeNativeParameterState (*this, destination);
    auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
        static_cast<const char*> (destination.getData()), static_cast<int> (destination.getSize())));
    if (xml != nullptr)
    {
        xml->setAttribute ("sidechainSourceTrackId", sidechainSourceTrackId());
        saveABState (*xml, abState, activeAB);
        juce::MemoryOutputStream stream;
        xml->writeTo (stream);
        destination.replaceAll (stream.getData(), stream.getDataSize());
    }
}

void CompMasterProcessor::setStateInformation (const void* data, int size)
{
    restoreNativeParameterState (*this, data, size);
    if (data != nullptr && size > 0)
    {
        auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
            static_cast<const char*> (data), size));
        if (xml != nullptr)
        {
            restoreABState (*this, *xml, abState, activeAB);
            if (xml->hasAttribute ("sidechainSourceTrackId"))
                setSidechainSourceTrackId (xml->getIntAttribute ("sidechainSourceTrackId"));
        }
    }
}

void CompMasterProcessor::selectABState (int slot)
{
    ::selectABState (*this, abState, activeAB, slot);
}

void CompMasterProcessor::setSidechainSourceTrackId (int trackId, bool notify)
{
    if (notify && onSidechainSourceChanged != nullptr
        && ! onSidechainSourceChanged (trackId))
        return;
    sidechainSource.store (trackId, std::memory_order_relaxed);
}

void CompMasterProcessor::setSidechainSources (std::vector<SidechainSource> sources)
{
    sidechainSources = std::move (sources);
}

EqMasterProcessor::EqMasterProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    constexpr std::array<float, bandCount> initialFrequencies {
        60.0f, 150.0f, 350.0f, 800.0f, 1800.0f, 4000.0f, 8000.0f, 14000.0f
    };
    for (int band = 0; band < bandCount; ++band)
    {
        const auto prefix = "band" + juce::String (band + 1) + "_";
        const float normalizedFrequency = std::log (initialFrequencies[static_cast<std::size_t> (band)] / 20.0f)
                                        / std::log (1000.0f);
        addParameter (new EffectParameter (prefix + "enabled", "Band Enabled", 0.0f, 0.0f,
                                           EffectParameter::Kind::Toggle));
        addParameter (new EffectParameter (prefix + "frequency", "Frequency",
                                           normalizedFrequency, normalizedFrequency,
                                           EffectParameter::Kind::Frequency));
        addParameter (new EffectParameter (prefix + "gain", "Gain", 0.5f, 0.5f,
                                           EffectParameter::Kind::EqGain));
        addParameter (new EffectParameter (prefix + "quality", "Q", 0.5f, 0.5f,
                                           EffectParameter::Kind::Quality));
        addParameter (new EffectParameter (prefix + "type", "Filter Type", 0.0f, 0.0f,
                                           EffectParameter::Kind::FilterType));
        addParameter (new EffectParameter (prefix + "slope", "Slope", 0.0f, 0.0f,
                                           EffectParameter::Kind::FilterSlope));
    }
    addParameter (new EffectParameter ("mode", "Processing Mode", 0.0f, 0.0f,
                                       EffectParameter::Kind::MidSide));
    addParameter (new EffectParameter ("bypass", "Bypass", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    addParameter (new EffectParameter ("analyzer_mode", "Analyzer", 0.0f, 0.0f,
                                       EffectParameter::Kind::Toggle));
    initializeABState (*this, abState);
}

void EqMasterProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    states = {};
    lastFilterTypes.fill (-1);
    lastFilterSlopes.fill (-1);
    analyzerWritePosition.store (0, std::memory_order_relaxed);
    for (auto& sample : analyzerPre)
        sample.store (0.0f, std::memory_order_relaxed);
    for (auto& sample : analyzerPost)
        sample.store (0.0f, std::memory_order_relaxed);
}

void EqMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    std::array<std::array<BiquadCoefficients, 4>, bandCount> coefficients {};
    std::array<int, bandCount> stageCounts {};
    std::array<bool, bandCount> enabledBands {};
    const auto parameters = getParameters();
    const int mode = std::clamp (static_cast<int> (std::round (
        parameters[bandCount * 6]->getValue() * 2.0f)), 0, 2);
    const bool bypass = parameters[bandCount * 6 + 1]->getValue() >= 0.5f;
    for (int band = 0; band < bandCount; ++band)
    {
        const int offset = band * 6;
        enabledBands[static_cast<std::size_t> (band)] = parameters[offset]->getValue() >= 0.5f;
        const double frequency = 20.0 * std::pow (1000.0, parameters[offset + 1]->getValue());
        const double gain = parameters[offset + 2]->getValue() * 36.0 - 18.0;
        const double quality = 0.1 * std::pow (100.0, parameters[offset + 3]->getValue());
        const int type = std::clamp (static_cast<int> (std::round (
            parameters[offset + 4]->getValue() * 3.0f)), 0, 3);
        const int slope = std::clamp (static_cast<int> (std::round (
            parameters[offset + 5]->getValue() * 2.0f)), 0, 2);
        if (type != lastFilterTypes[static_cast<std::size_t> (band)]
            || slope != lastFilterSlopes[static_cast<std::size_t> (band)])
        {
            states[static_cast<std::size_t> (band)] = {};
            lastFilterTypes[static_cast<std::size_t> (band)] = type;
            lastFilterSlopes[static_cast<std::size_t> (band)] = slope;
        }
        coefficients[static_cast<std::size_t> (band)] = makeBandCoefficients (
            type, slope, frequency, gain, quality, sampleRate,
            stageCounts[static_cast<std::size_t> (band)]);
        enabledBands[static_cast<std::size_t> (band)] = enabledBands[static_cast<std::size_t> (band)]
            && (type != 0 || std::abs (gain) > 0.0001);
    }

    if (buffer.getNumChannels() == 0)
        return;

    auto processChannel = [&] (double value, int channel)
    {
        for (int band = 0; band < bandCount; ++band)
        {
            if (! enabledBands[static_cast<std::size_t> (band)])
                continue;
            for (int stage = 0; stage < stageCounts[static_cast<std::size_t> (band)]; ++stage)
            {
                const auto& c = coefficients[static_cast<std::size_t> (band)]
                                              [static_cast<std::size_t> (stage)];
                auto& state = states[static_cast<std::size_t> (band)]
                                     [static_cast<std::size_t> (stage)]
                                     [static_cast<std::size_t> (channel)];
                const double output = c.b0 * value + c.b1 * state.x1 + c.b2 * state.x2
                                    - c.a1 * state.y1 - c.a2 * state.y2;
                state.x2 = state.x1;
                state.x1 = value;
                state.y2 = state.y1;
                state.y1 = output;
                value = output;
            }
        }
        return value;
    };

    const int channels = std::min (2, buffer.getNumChannels());
    auto* left = buffer.getWritePointer (0);
    auto* right = channels > 1 ? buffer.getWritePointer (1) : left;
    int writePosition = analyzerWritePosition.load (std::memory_order_relaxed);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const double inputLeft = left[sample];
        const double inputRight = channels > 1 ? right[sample] : inputLeft;
        const float preEq = static_cast<float> (mode == 0 ? inputLeft
            : mode == 1 ? (inputLeft + inputRight) * 0.5
                        : (inputLeft - inputRight) * 0.5);
        double outputLeft = inputLeft;
        double outputRight = inputRight;
        if (! bypass)
        {
            if (mode == 0)
            {
                outputLeft = processChannel (inputLeft, 0);
                outputRight = channels > 1 ? processChannel (inputRight, 1) : outputLeft;
            }
            else
            {
                double mid = (inputLeft + inputRight) * 0.5;
                double side = (inputLeft - inputRight) * 0.5;
                if (mode == 1)
                    mid = processChannel (mid, 0);
                else
                    side = processChannel (side, 1);
                outputLeft = mid + side;
                outputRight = mid - side;
            }
        }
        left[sample] = static_cast<float> (outputLeft);
        if (channels > 1)
            right[sample] = static_cast<float> (outputRight);
        analyzerPre[static_cast<std::size_t> (writePosition)].store (preEq,
                                                                     std::memory_order_relaxed);
        analyzerPost[static_cast<std::size_t> (writePosition)].store (
            static_cast<float> (mode == 0 ? outputLeft
                : mode == 1 ? (outputLeft + outputRight) * 0.5
                            : (outputLeft - outputRight) * 0.5),
            std::memory_order_relaxed);
        writePosition = (writePosition + 1) % analyzerBufferSize;
    }
    analyzerWritePosition.store (writePosition, std::memory_order_release);
}

int EqMasterProcessor::copyAnalyzerSnapshot (bool postEq, float* destination, int capacity) const
{
    if (destination == nullptr || capacity <= 0)
        return 0;
    const int count = std::min (capacity, analyzerBufferSize);
    const int writePosition = analyzerWritePosition.load (std::memory_order_acquire);
    const int start = (writePosition - count + analyzerBufferSize) % analyzerBufferSize;
    const auto& source = postEq ? analyzerPost : analyzerPre;
    for (int index = 0; index < count; ++index)
        destination[index] = source[static_cast<std::size_t> (
            (start + index) % analyzerBufferSize)].load (std::memory_order_relaxed);
    return count;
}

void EqMasterProcessor::resetAnalyzer()
{
    analyzerWritePosition.store (0, std::memory_order_release);
    for (auto& sample : analyzerPre)
        sample.store (0.0f, std::memory_order_relaxed);
    for (auto& sample : analyzerPost)
        sample.store (0.0f, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* EqMasterProcessor::createEditor()
{
    return new EqMasterEditor (*this);
}

void EqMasterProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    captureABState (*this, abState, activeAB);
    storeNativeParameterState (*this, destination);
    auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
        static_cast<const char*> (destination.getData()), static_cast<int> (destination.getSize())));
    if (xml != nullptr)
    {
        saveABState (*xml, abState, activeAB);
        juce::MemoryOutputStream stream;
        xml->writeTo (stream);
        destination.replaceAll (stream.getData(), stream.getDataSize());
    }
}

void EqMasterProcessor::setStateInformation (const void* data, int size)
{
    restoreNativeParameterState (*this, data, size);
    if (data != nullptr && size > 0)
    {
        auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
            static_cast<const char*> (data), size));
        if (xml != nullptr)
            restoreABState (*this, *xml, abState, activeAB);
    }
}

void EqMasterProcessor::selectABState (int slot)
{
    ::selectABState (*this, abState, activeAB, slot);
}
