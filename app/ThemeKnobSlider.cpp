#include "ThemeKnobSlider.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>

namespace
{
class SelectAllSliderLabel final : public juce::Label
{
public:
    SelectAllSliderLabel() : juce::Label ({}, {}) {}

protected:
    void editorShown (juce::TextEditor* textEditor) override
    {
        if (textEditor != nullptr)
        {
            textEditor->setSelectAllWhenFocused (true);
            textEditor->selectAll();
            juce::Component::SafePointer<juce::TextEditor> safeEditor (textEditor);
            juce::MessageManager::callAsync ([safeEditor]
            {
                if (safeEditor != nullptr)
                    safeEditor->selectAll();
            });
        }
    }
};

void drawThemeKnob (juce::Graphics& g, juce::Rectangle<float> bounds,
                    double minimum, double maximum, double value,
                    double centreValue, bool bipolar, bool enabled, bool highlighted)
{
    const float diameter = std::min (bounds.getWidth(), bounds.getHeight()) - 1.0f;
    if (diameter <= 0.0f || maximum <= minimum)
        return;

    const float radius = diameter * 0.5f;
    const juce::Point<float> centre (bounds.getCentreX(), bounds.getCentreY());
    constexpr float radiansPerDegree = juce::MathConstants<float>::pi / 180.0f;
    constexpr float startAngle = -135.0f * radiansPerDegree;
    constexpr float rotaryRange = 270.0f * radiansPerDegree;
    const float bodyRadius = radius * 0.68f;
    g.setColour (theme::panel());
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);

    value = std::clamp (value, minimum, maximum);
    centreValue = std::clamp (centreValue, minimum, maximum);
    const double proportion = bipolar
        ? value <= centreValue
            ? 0.5 * (value - minimum) / std::max (1.0e-9, centreValue - minimum)
            : 0.5 + 0.5 * (value - centreValue) / std::max (1.0e-9, maximum - centreValue)
        : (value - minimum) / (maximum - minimum);
    const float topAngle = startAngle + static_cast<float> (proportion) * rotaryRange;
    const float arcRadius = radius * 0.857f;
    const float trackThickness = std::max (1.0f, diameter * 0.019f);
    const float activeThickness = std::max (diameter < 32.0f ? 2.0f : 1.5f,
                                            diameter * 0.027f)
                                * (highlighted && enabled ? 1.3f : 1.0f);
    const juce::Colour activeColour = enabled ? theme::accent()
                                               : juce::Colour (0xff8e8e93);

    const auto toPathAngle = [] (float angle) { return angle - juce::MathConstants<float>::halfPi; };

    juce::Path scaleArc;
    scaleArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            toPathAngle (startAngle), toPathAngle (startAngle + rotaryRange), true);
    g.setColour (theme::border());
    g.strokePath (scaleArc, juce::PathStrokeType (
        trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float activeStart = bipolar ? 0.0f : startAngle;
    if (std::abs (topAngle - activeStart) > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                std::min (toPathAngle (activeStart), toPathAngle (topAngle)),
                                std::max (toPathAngle (activeStart), toPathAngle (topAngle)), true);
        g.setColour (activeColour);
        g.strokePath (valueArc, juce::PathStrokeType (
            activeThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float pointRadius = std::max (2.0f, radius * 0.09f);
    const juce::Point<float> point (centre.x + std::sin (topAngle) * radius * 0.46f,
                                    centre.y - std::cos (topAngle) * radius * 0.46f);
    g.setColour (activeColour);
    g.fillEllipse (point.x - pointRadius, point.y - pointRadius,
                   pointRadius * 2.0f, pointRadius * 2.0f);
}
}

juce::Label* ThemeKnobLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = new SelectAllSliderLabel();
    label->setJustificationType (juce::Justification::centred);
    label->setKeyboardType (juce::TextInputTarget::decimalKeyboard);

    const bool isLinearBar = slider.getSliderStyle() == juce::Slider::LinearBar
                          || slider.getSliderStyle() == juce::Slider::LinearBarVertical;
    label->setColour (juce::Label::textColourId,
                      slider.findColour (juce::Slider::textBoxTextColourId));
    label->setColour (juce::Label::backgroundColourId,
                      isLinearBar ? juce::Colours::transparentBlack
                                  : slider.findColour (juce::Slider::textBoxBackgroundColourId));
    label->setColour (juce::Label::outlineColourId,
                      slider.findColour (juce::Slider::textBoxOutlineColourId));
    label->setColour (juce::TextEditor::textColourId,
                      slider.findColour (juce::Slider::textBoxTextColourId));
    label->setColour (juce::TextEditor::backgroundColourId,
                      slider.findColour (juce::Slider::textBoxBackgroundColourId)
                          .withAlpha (isLinearBar ? 0.7f : 1.0f));
    label->setColour (juce::TextEditor::outlineColourId,
                      slider.findColour (juce::Slider::textBoxOutlineColourId));
    label->setColour (juce::TextEditor::highlightColourId,
                      slider.findColour (juce::Slider::textBoxHighlightColourId));
    return label;
}

void ThemeKnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                             int width, int height, float,
                                             float, float, juce::Slider& slider)
{
    const double minimum = slider.getMinimum();
    const double maximum = slider.getMaximum();
    double centreValue = minimum + (maximum - minimum) * 0.5;
    bool bipolar = minimum < 0.0 && maximum > 0.0;
    if (const auto* themed = dynamic_cast<const ThemeKnobSlider*> (&slider))
    {
        bipolar = themed->bipolar;
        if (std::isfinite (themed->centreValue))
            centreValue = themed->centreValue;
        else if (bipolar)
            centreValue = 0.0;
    }

    drawThemeKnob (g, juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height)),
                   minimum, maximum, slider.getValue(), centreValue, bipolar,
                   slider.isEnabled(), slider.isMouseOverOrDragging());
}
