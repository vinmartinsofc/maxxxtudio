#include "ThemeKnobSlider.h"
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr std::array<std::pair<double, double>, 7> mixerFaderCurve {{
    { 0.00, -60.0 }, { 0.08, -48.0 }, { 0.25, -24.0 }, { 0.45, -12.0 },
    { 0.60, -6.0 }, { 0.75, 0.0 }, { 1.00, 6.0 }
}};

double faderPositionForDecibels (double decibels)
{
    decibels = std::clamp (decibels, mixerFaderCurve.front().second,
                           mixerFaderCurve.back().second);
    for (std::size_t i = 1; i < mixerFaderCurve.size(); ++i)
    {
        const auto [rightPosition, rightDb] = mixerFaderCurve[i];
        if (decibels <= rightDb)
        {
            const auto [leftPosition, leftDb] = mixerFaderCurve[i - 1];
            const double proportion = (decibels - leftDb) / (rightDb - leftDb);
            return leftPosition + proportion * (rightPosition - leftPosition);
        }
    }

    return 1.0;
}

double decibelsForFaderPosition (double position)
{
    position = std::clamp (position, mixerFaderCurve.front().first,
                           mixerFaderCurve.back().first);
    for (std::size_t i = 1; i < mixerFaderCurve.size(); ++i)
    {
        const auto [rightPosition, rightDb] = mixerFaderCurve[i];
        if (position <= rightPosition)
        {
            const auto [leftPosition, leftDb] = mixerFaderCurve[i - 1];
            const double proportion = (position - leftPosition)
                                    / (rightPosition - leftPosition);
            return leftDb + proportion * (rightDb - leftDb);
        }
    }

    return 6.0;
}

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
        if (highlighted && enabled)
        {
            g.setColour (activeColour.withAlpha (0.05f));
            g.strokePath (valueArc, juce::PathStrokeType (activeThickness + 5.0f));
            g.setColour (activeColour.withAlpha (0.10f));
            g.strokePath (valueArc, juce::PathStrokeType (activeThickness + 3.0f));
            g.setColour (activeColour.withAlpha (0.20f));
            g.strokePath (valueArc, juce::PathStrokeType (activeThickness + 1.5f));
        }
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

MixerFaderSlider::MixerFaderSlider()
    : trackColour (theme::accent())
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
}

void MixerPanSlider::setTrackColour (juce::Colour colour)
{
    if (trackColour == colour)
        return;

    trackColour = colour;
    repaint();
}

void MixerFaderSlider::setTrackColour (juce::Colour colour)
{
    if (trackColour == colour)
        return;

    trackColour = colour;
    scaleCache = {};
    repaint();
}

float MixerFaderSlider::getCapHeight() const noexcept
{
    return std::max (1.0f, std::min (16.0f, static_cast<float> (getHeight()) * 0.08f));
}

double MixerFaderSlider::normalisedPositionForDecibels (double decibels)
{
    return faderPositionForDecibels (decibels);
}

double MixerFaderSlider::decibelsForNormalisedPosition (double position)
{
    return decibelsForFaderPosition (position);
}

juce::NormalisableRange<double> MixerFaderSlider::decibelRange (double maximumDb)
{
    maximumDb = std::clamp (maximumDb, -59.9, 6.0);
    // Keep each channel's existing gain limit; channels below +6 dB simply stop before the scale's top.
    return {
        -60.0, maximumDb,
        [] (double, double, double position)
        {
            return decibelsForNormalisedPosition (position);
        },
        [] (double, double, double decibels)
        {
            return normalisedPositionForDecibels (decibels);
        },
        [] (double minimum, double maximum, double decibels)
        {
            return juce::jlimit (minimum, maximum, std::round (decibels * 10.0) / 10.0);
        }
    };
}

void MixerFaderSlider::drawCachedScale (juce::Graphics& g)
{
    updateScaleCache();
    g.drawImageWithin (scaleCache, 0, 0, getWidth(), getHeight(),
                       juce::RectanglePlacement::stretchToFit);
}

void MixerFaderSlider::updateScaleCache()
{
    const auto surfaceColour = theme::control();
    const auto borderColour = theme::border();
    const auto textColour = theme::textDim();
    const float scale = std::max (0.5f, juce::Component::getApproximateScaleFactorForComponent (this));
    const int imageWidth = std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * scale));
    const int imageHeight = std::max (1, juce::roundToInt (static_cast<float> (getHeight()) * scale));

    if (scaleCache.isValid() && scaleCache.getWidth() == imageWidth
        && scaleCache.getHeight() == imageHeight && cachedTrackColour == trackColour
        && cachedSurfaceColour == surfaceColour && cachedBorderColour == borderColour
        && cachedTextColour == textColour)
        return;

    scaleCache = juce::Image (juce::Image::ARGB, imageWidth, imageHeight, true);
    juce::Graphics cacheGraphics (scaleCache);
    cacheGraphics.addTransform (juce::AffineTransform::scale (scale));

    const float width = static_cast<float> (getWidth());
    const float height = static_cast<float> (getHeight());
    const float capHeight = getCapHeight();
    const float capWidth = std::min (width * 0.42f, capHeight * 1.9f);
    const float centreX = width * 0.68f;
    const float railWidth = std::min (6.0f, std::max (4.0f, width * 0.085f));
    const float top = capHeight * 0.5f;
    const float bottom = height - top;
    const juce::Rectangle<float> rail (centreX - railWidth * 0.5f, top,
                                       railWidth, std::max (0.0f, bottom - top));

    cacheGraphics.setColour (surfaceColour);
    cacheGraphics.fillRoundedRectangle (rail, railWidth * 0.5f);
    cacheGraphics.setColour (borderColour);
    cacheGraphics.drawRoundedRectangle (rail, railWidth * 0.5f, 0.8f);

    constexpr std::array<std::pair<double, const char*>, 5> ticks {{
        { -48.0, "-48" }, { -24.0, "-24" }, { -12.0, "-12" },
        { -6.0, "-6" }, { 0.0, "0" }
    }};
    const float labelRight = centreX - capWidth * 0.5f - 2.0f;
    const float labelWidth = std::max (0.0f, labelRight);
    const float tickStart = centreX - railWidth * 0.5f - 6.0f;
    const float tickEnd = centreX - railWidth * 0.5f - 1.5f;
    cacheGraphics.setFont (theme::fontOptions (11.0f));
    cacheGraphics.setColour (textColour);
    for (const auto& [decibels, label] : ticks)
    {
        if (decibels > getMaximum())
            continue;

        const double position = normalisedPositionForDecibels (decibels);
        const float y = bottom - static_cast<float> (position) * (bottom - top);
        cacheGraphics.setColour (borderColour.brighter (0.18f));
        cacheGraphics.drawHorizontalLine (juce::roundToInt (y), tickStart, tickEnd);
        cacheGraphics.setColour (textColour);
        cacheGraphics.drawText (label,
            juce::Rectangle<float> (0.0f, y - 7.0f, labelWidth, 14.0f),
            juce::Justification::centredRight, false);
    }

    cachedTrackColour = trackColour;
    cachedSurfaceColour = surfaceColour;
    cachedBorderColour = borderColour;
    cachedTextColour = textColour;
}

juce::Slider::SliderLayout ThemeKnobLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);
    if (auto* fader = dynamic_cast<MixerFaderSlider*> (&slider))
    {
        const int inset = std::min (layout.sliderBounds.getHeight() / 2,
                                    juce::roundToInt (fader->getCapHeight() * 0.5f));
        layout.sliderBounds = layout.sliderBounds.withTrimmedTop (inset)
                                                   .withTrimmedBottom (inset);
    }
    return layout;
}

juce::MouseCursor ThemeKnobLookAndFeel::getMouseCursorFor (juce::Component& component)
{
    if (dynamic_cast<juce::Button*> (&component) != nullptr
        || dynamic_cast<juce::ComboBox*> (&component) != nullptr)
        return juce::MouseCursor::PointingHandCursor;

    if (auto* slider = dynamic_cast<juce::Slider*> (&component))
    {
        switch (slider->getSliderStyle())
        {
            case juce::Slider::Rotary:
            case juce::Slider::RotaryHorizontalDrag:
            case juce::Slider::RotaryVerticalDrag:
            case juce::Slider::RotaryHorizontalVerticalDrag:
                return juce::MouseCursor::DraggingHandCursor;
            case juce::Slider::LinearVertical:
            case juce::Slider::LinearBarVertical:
                return juce::MouseCursor::UpDownResizeCursor;
            case juce::Slider::LinearHorizontal:
            case juce::Slider::LinearBar:
                return juce::MouseCursor::LeftRightResizeCursor;
            default:
                break;
        }
    }

    if (dynamic_cast<juce::TextEditor*> (&component) != nullptr)
        return juce::MouseCursor::IBeamCursor;

    if (dynamic_cast<juce::ScrollBar*> (&component) != nullptr)
        return juce::MouseCursor::DraggingHandCursor;

    return juce::LookAndFeel_V4::getMouseCursorFor (component);
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
    if (highlighted && ! active && ! down)
    {
        g.setColour (theme::accent().withAlpha (0.06f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), 6.0f, 4.0f);
        g.setColour (theme::accent().withAlpha (0.12f));
        g.drawRoundedRectangle (bounds.expanded (0.5f), 5.5f, 2.0f);
    }
    if (active || down)
    {
        g.setColour (theme::accent().withAlpha (0.05f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 4.0f);
        g.setColour (theme::accent().withAlpha (0.10f));
        g.drawRoundedRectangle (bounds.reduced (1.25f), 4.0f, 2.5f);
        g.setColour (theme::accent().withAlpha (0.20f));
        g.drawRoundedRectangle (bounds.reduced (2.0f), 3.0f, 1.5f);
    }
    g.setColour (active || down ? theme::accent()
                : highlighted ? theme::accentHover() : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 5.0f, 1.5f);
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
    const bool highlighted = box.isMouseOverOrDragging();
    g.setColour (box.hasKeyboardFocus (false) || down ? theme::accent()
                : highlighted ? theme::accentHover() : theme::controlBorder());
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    if (highlighted && ! down)
    {
        g.setColour (theme::accent().withAlpha (0.12f));
        g.drawRoundedRectangle (bounds.expanded (0.5f), 4.5f, 2.0f);
    }

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
    if (auto* fader = dynamic_cast<MixerFaderSlider*> (&slider))
    {
        fader->drawCachedScale (g);
        const float capHeight = fader->getCapHeight();
        const float capWidth = std::min (static_cast<float> (fader->getWidth()) * 0.42f,
                                         capHeight * 1.9f);
        const float centreX = static_cast<float> (fader->getWidth()) * 0.68f;
        const float railWidth = std::min (6.0f, std::max (4.0f,
            static_cast<float> (fader->getWidth()) * 0.085f));
        const float bottom = static_cast<float> (y + height);
        const float fillTop = juce::jlimit (static_cast<float> (y), bottom, sliderPosition);
        const juce::Rectangle<float> rail (centreX - railWidth * 0.5f, fillTop,
                                           railWidth, std::max (0.0f, bottom - fillTop));
        g.setColour (fader->getTrackColour().withAlpha (0.8f));
        g.fillRoundedRectangle (rail, railWidth * 0.5f);

        const juce::Rectangle<float> cap (centreX - capWidth * 0.5f,
                                          sliderPosition - capHeight * 0.5f,
                                          capWidth, capHeight);
        g.setColour (theme::background().withAlpha (0.4f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);

        const bool highlighted = slider.isMouseOverOrDragging();
        const auto capFill = highlighted
            ? theme::textDim().interpolatedWith (theme::text(), 0.55f)
            : theme::textDim().interpolatedWith (theme::panelLight(), 0.45f);
        g.setColour (capFill);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (cap, 3.0f, 0.8f);

        g.setColour (theme::textDim().withAlpha (0.75f));
        for (int line = -1; line <= 1; ++line)
        {
            const float lineY = cap.getCentreY() + static_cast<float> (line) * capHeight * 0.2f;
            g.drawHorizontalLine (juce::roundToInt (lineY),
                                  cap.getX() + capWidth * 0.32f,
                                  cap.getRight() - capWidth * 0.32f);
        }
        g.setColour (fader->getTrackColour());
        g.fillRect (cap.getX(), cap.getCentreY() - capHeight * 0.18f,
                    std::max (1.0f, capHeight * 0.12f), capHeight * 0.36f);
        if (slider.isMouseButtonDown())
        {
            g.setColour (fader->getTrackColour());
            g.drawRoundedRectangle (cap.expanded (0.5f), 3.5f, 1.2f);
        }
        return;
    }

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

    const float thickness = bar ? 4.0f : vertical ? 4.0f : 2.5f;
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

    const bool highlighted = slider.isMouseOverOrDragging();
    g.setColour (theme::border());
    g.fillRoundedRectangle (vertical ? track.expanded (1.5f, 0.0f) : track,
                            thickness * 0.5f);
    if (vertical && ! bar)
    {
        for (int tick = 0; tick <= 10; ++tick)
        {
            const float tickY = track.getY() + track.getHeight() * static_cast<float> (tick) / 10.0f;
            const float tickWidth = tick % 5 == 0 ? 4.0f : 2.0f;
            g.setColour (theme::textDim().withAlpha (tick % 5 == 0 ? 0.55f : 0.28f));
            g.drawHorizontalLine (juce::roundToInt (tickY),
                                  track.getCentreX() - tickWidth - 3.0f,
                                  track.getCentreX() - 3.0f);
        }
    }

    const auto activeColour = slider.isEnabled() ? theme::accent() : theme::textDim();
    g.setColour (activeColour);
    g.fillRoundedRectangle (active, thickness * 0.5f);
    if (! bar)
    {
        if (vertical)
        {
            const float thumbWidth = std::max (
                1.0f, std::min (24.0f, static_cast<float> (width) - 2.0f));
            const auto thumb = juce::Rectangle<float> (
                track.getCentreX() - thumbWidth * 0.5f, sliderPosition - 4.0f,
                thumbWidth, 8.0f);
            if (highlighted && slider.isEnabled())
            {
                g.setColour (activeColour.withAlpha (0.08f));
                g.fillRoundedRectangle (thumb.expanded (5.0f, 4.0f), 5.0f);
                g.setColour (activeColour.withAlpha (0.18f));
                g.drawRoundedRectangle (thumb.expanded (2.0f, 1.5f), 4.0f, 2.0f);
            }
            theme::drawChromeSurface (g, thumb, 3.0f,
                                      slider.isEnabled() ? theme::panelLight() : theme::control());
            g.setColour (highlighted ? theme::accentHover() : activeColour);
            g.drawRoundedRectangle (thumb, 3.0f, 1.2f);
            g.setColour (theme::text().withAlpha (0.8f));
            g.drawHorizontalLine (juce::roundToInt (thumb.getCentreY()),
                                  thumb.getX() + 4.0f, thumb.getRight() - 4.0f);
        }
        else
        {
            const float thumbSize = 9.0f;
            const auto thumb = juce::Rectangle<float> (
                sliderPosition - thumbSize * 0.5f, track.getCentreY() - thumbSize * 0.5f,
                thumbSize, thumbSize);
            theme::drawChromeSurface (g, thumb, thumbSize * 0.5f,
                                      slider.isEnabled() ? theme::panelLight() : theme::control());
            g.setColour (highlighted ? theme::accentHover() : activeColour);
            g.drawEllipse (thumb, 1.0f);
        }
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

    const bool hovered = editor.isMouseOverOrDragging();
    g.setColour (editor.hasKeyboardFocus (true) ? theme::accent()
                : hovered ? theme::accentHover() : theme::controlBorder());
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
    if (auto* pan = dynamic_cast<MixerPanSlider*> (&slider))
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height));
        const juce::Point<float> centre (bounds.getCentreX(), bounds.getCentreY());
        const float radius = std::max (0.0f, std::min (bounds.getWidth(), bounds.getHeight()) * 0.43f);
        const float bodyRadius = std::min (7.0f, radius * 0.55f);
        constexpr float degreesToRadians = juce::MathConstants<float>::pi / 180.0f;
        constexpr float startAngle = -135.0f * degreesToRadians;
        constexpr float endAngle = 135.0f * degreesToRadians;
        const auto toPathAngle = [] (float angle)
        {
            return angle - juce::MathConstants<float>::halfPi;
        };

        juce::Path backgroundArc;
        backgroundArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                     toPathAngle (startAngle), toPathAngle (endAngle), true);
        g.setColour (theme::border());
        g.strokePath (backgroundArc, juce::PathStrokeType (
            3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float panPosition = juce::jlimit (-1.0f, 1.0f,
            static_cast<float> (pan->getValue()));
        const float valueAngle = panPosition * 135.0f * degreesToRadians;
        if (std::abs (valueAngle) > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                std::min (toPathAngle (0.0f), toPathAngle (valueAngle)),
                std::max (toPathAngle (0.0f), toPathAngle (valueAngle)), true);
            const auto activeColour = slider.isMouseOverOrDragging()
                ? pan->getTrackColour().brighter (0.15f) : pan->getTrackColour();
            g.setColour (activeColour);
            g.strokePath (valueArc, juce::PathStrokeType (
                3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const juce::Rectangle<float> body (centre.x - bodyRadius, centre.y - bodyRadius,
                                           bodyRadius * 2.0f, bodyRadius * 2.0f);
        g.setColour (theme::control());
        g.fillEllipse (body);
        g.setColour (theme::border());
        g.drawEllipse (body, 1.0f);
        g.setColour (theme::textDim());
        g.drawVerticalLine (juce::roundToInt (centre.x),
                            centre.y - bodyRadius * 0.4f, centre.y + bodyRadius * 0.4f);
        return;
    }

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
