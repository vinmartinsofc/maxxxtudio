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

void drawHighTechTickBox (juce::Graphics& g, juce::Rectangle<float> bounds,
                          bool ticked, bool enabled, bool highlighted, bool down)
{
    g.setColour (down ? theme::pressed() : highlighted ? theme::hover() : theme::control());
    g.fillRect (bounds);
    g.setColour (ticked ? theme::accent() : theme::controlBorder());
    g.drawRect (bounds, 1.0f);

    if (ticked)
    {
        juce::Path check;
        check.startNewSubPath (bounds.getX() + bounds.getWidth() * 0.20f,
                               bounds.getY() + bounds.getHeight() * 0.52f);
        check.lineTo (bounds.getX() + bounds.getWidth() * 0.43f,
                      bounds.getY() + bounds.getHeight() * 0.74f);
        check.lineTo (bounds.getX() + bounds.getWidth() * 0.82f,
                      bounds.getY() + bounds.getHeight() * 0.25f);
        g.setColour (enabled ? theme::accent() : theme::textDim());
        g.strokePath (check, juce::PathStrokeType (1.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
}

void ThemeKnobLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool highlighted, bool down)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawButtonBackground (g, button, backgroundColour,
                                                     highlighted, down);
        return;
    }

    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool active = button.getToggleState();
    const auto fill = ! button.isEnabled() ? theme::control()
                    : down ? theme::pressed()
                    : active ? theme::panelLight()
                    : highlighted ? theme::hover() : theme::control();
    theme::drawChromeSurface (g, bounds, 5.0f, fill);
    g.setColour (active || down ? theme::accent()
                : highlighted ? theme::accentHover() : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    if (active)
    {
        g.setColour (theme::accent());
        g.fillRoundedRectangle (bounds.withHeight (2.0f), 1.0f);
    }
}

void ThemeKnobLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool highlighted, bool down)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawToggleButton (g, button, highlighted, down);
        return;
    }

    const float boxSize = std::min (15.0f, static_cast<float> (button.getHeight() - 4));
    if (boxSize <= 0.0f)
        return;
    const auto box = juce::Rectangle<float> (1.0f,
        (static_cast<float> (button.getHeight()) - boxSize) * 0.5f, boxSize, boxSize);
    drawHighTechTickBox (g, box, button.getToggleState(), button.isEnabled(), highlighted, down);
    g.setColour (button.isEnabled() ? theme::text() : theme::textDim());
    g.setFont (theme::fontOptions (13.0f));
    g.drawFittedText (button.getButtonText(),
        juce::Rectangle<int> (static_cast<int> (box.getRight() + 7.0f), 0,
                              std::max (0, button.getWidth() - static_cast<int> (box.getRight()) - 9),
                              button.getHeight()),
        juce::Justification::centredLeft, 1);
}

void ThemeKnobLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                       float x, float y, float width, float height,
                                       bool ticked, bool enabled, bool highlighted, bool down)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawTickBox (g, component, x, y, width, height,
                                           ticked, enabled, highlighted, down);
        return;
    }

    drawHighTechTickBox (g, { x, y, width, height }, ticked, enabled && component.isEnabled(),
                         highlighted, down);
}

void ThemeKnobLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down,
                                        int buttonX, int buttonY, int buttonWidth, int buttonHeight,
                                        juce::ComboBox& box)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawComboBox (g, width, height, down,
                                            buttonX, buttonY, buttonWidth, buttonHeight, box);
        return;
    }

    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
        static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f);
    theme::drawChromeSurface (g, bounds, 4.0f, down ? theme::pressed() : theme::control());
    g.setColour (box.hasKeyboardFocus (false) || down ? theme::accent() : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const float centreX = static_cast<float> (buttonX) + static_cast<float> (buttonWidth) * 0.5f;
    const float centreY = static_cast<float> (buttonY) + static_cast<float> (buttonHeight) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (centreX - 4.0f, centreY - 1.5f);
    chevron.lineTo (centreX, centreY + 2.5f);
    chevron.lineTo (centreX + 4.0f, centreY - 1.5f);
    g.setColour (box.isEnabled() ? theme::accent() : theme::textDim());
    g.strokePath (chevron, juce::PathStrokeType (1.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ThemeKnobLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPosition, float minSliderPosition,
                                            float maxSliderPosition,
                                            juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPosition,
                                                 minSliderPosition, maxSliderPosition, style, slider);
        return;
    }

    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;
    const bool bar = style == juce::Slider::LinearBar
                  || style == juce::Slider::LinearBarVertical;
    if (style != juce::Slider::LinearHorizontal && ! vertical
        && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPosition,
                                                 minSliderPosition, maxSliderPosition, style, slider);
        return;
    }

    const float thickness = bar ? 4.0f : 2.5f;
    juce::Rectangle<float> track;
    juce::Rectangle<float> active;
    if (vertical)
    {
        const float centreX = static_cast<float> (x) + static_cast<float> (width) * 0.5f;
        const float top = static_cast<float> (y) + (bar ? 1.0f : 5.0f);
        const float bottom = static_cast<float> (y + height) - (bar ? 1.0f : 5.0f);
        track = { centreX - thickness * 0.5f, top, thickness, std::max (0.0f, bottom - top) };
        active = { track.getX(), sliderPosition, thickness,
                   std::max (0.0f, bottom - sliderPosition) };
    }
    else
    {
        const float left = static_cast<float> (x) + (bar ? 1.0f : 5.0f);
        const float right = static_cast<float> (x + width) - (bar ? 1.0f : 5.0f);
        const float centreY = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
        track = { left, centreY - thickness * 0.5f, std::max (0.0f, right - left), thickness };
        active = { left, centreY - thickness * 0.5f,
                   std::clamp (sliderPosition - left, 0.0f, track.getWidth()), thickness };
    }

    g.setColour (theme::border());
    g.fillRoundedRectangle (track, thickness * 0.5f);
    g.setColour (slider.isEnabled() ? theme::accent() : theme::textDim());
    g.fillRoundedRectangle (active, thickness * 0.5f);
    if (! bar)
    {
        const float thumbSize = 9.0f;
        const auto thumb = vertical
            ? juce::Rectangle<float> (track.getCentreX() - thumbSize * 0.5f,
                                      sliderPosition - thumbSize * 0.5f, thumbSize, thumbSize)
            : juce::Rectangle<float> (sliderPosition - thumbSize * 0.5f,
                                      track.getCentreY() - thumbSize * 0.5f, thumbSize, thumbSize);
        theme::drawChromeSurface (g, thumb, thumbSize * 0.5f,
                                  slider.isEnabled() ? theme::panelLight() : theme::control());
        g.setColour (theme::accent());
        g.drawEllipse (thumb, 1.0f);
    }
}

void ThemeKnobLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                                 juce::TextEditor& editor)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
        return;
    }

    g.setColour (editor.hasKeyboardFocus (true) ? theme::accent() : theme::controlBorder());
    g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f,
        static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f), 4.0f, 1.0f);
}

void ThemeKnobLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                                         int x, int y, int width, int height, bool vertical,
                                         int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    if (! theme::isHighTech())
    {
        juce::LookAndFeel_V4::drawScrollbar (g, scrollbar, x, y, width, height, vertical,
                                             thumbStart, thumbSize, isMouseOver, isMouseDown);
        return;
    }

    const float thickness = isMouseOver || isMouseDown ? 7.0f : 5.0f;
    const auto thumb = vertical
        ? juce::Rectangle<float> (static_cast<float> (x) + (static_cast<float> (width) - thickness) * 0.5f,
                                  static_cast<float> (thumbStart), thickness,
                                  static_cast<float> (thumbSize))
        : juce::Rectangle<float> (static_cast<float> (thumbStart),
                                  static_cast<float> (y) + (static_cast<float> (height) - thickness) * 0.5f,
                                  static_cast<float> (thumbSize), thickness);
    g.setColour ((isMouseOver || isMouseDown ? theme::accent() : theme::controlBorder())
                     .withAlpha (isMouseDown ? 1.0f : isMouseOver ? 0.85f : 0.75f));
    g.fillRoundedRectangle (thumb, thickness * 0.5f);
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
