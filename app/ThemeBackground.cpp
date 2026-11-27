#include "ThemeBackground.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr std::uint32_t spotlightCentre = 0xe61b2027;     // Soft top-centred spotlight.
constexpr std::uint32_t spotlightEdge = 0x001b2027;       // Fully transparent spotlight edge.
}

ThemeBackground::ThemeBackground()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void ThemeBackground::paint (juce::Graphics& g)
{
    if (backgroundImage.isNull())
    {
        g.fillAll (theme::background());
        return;
    }

    g.drawImage (backgroundImage, getLocalBounds().toFloat(),
                 juce::RectanglePlacement::stretchToFit, false);
}

void ThemeBackground::resized()
{
    rebuildImage();
}

void ThemeBackground::refresh()
{
    rebuildImage();
    repaint();
}

void ThemeBackground::rebuildImage()
{
    const auto bounds = getLocalBounds().toFloat();
    const int width = juce::roundToInt (bounds.getWidth());
    const int height = juce::roundToInt (bounds.getHeight());
    if (width <= 0 || height <= 0)
    {
        backgroundImage = juce::Image();
        return;
    }

    backgroundImage = juce::Image (juce::Image::RGB, width, height, true);
    juce::Graphics g (backgroundImage);
    const auto baseBottom = theme::background();
    const auto baseTop = theme::isBlackTexturedBlue()
        ? theme::isAmberBlue() ? juce::Colour (0xff112c49)
                               : juce::Colour (0xff14161a)
        : baseBottom.brighter (0.08f);
    juce::ColourGradient base (baseTop, 0.0f, 0.0f,
                               baseBottom, 0.0f, bounds.getHeight(), false);
    g.setGradientFill (base);
    g.fillRect (bounds);

    if (! theme::isBlackTexturedBlue())
        return;

    const float radiusX = bounds.getWidth() * 0.65f;
    const float radiusY = bounds.getHeight() * 0.65f;
    const int sourceRadius = std::max (1, juce::roundToInt (radiusY));
    const int sourceWidth = sourceRadius * 2;
    juce::Image spotlight (juce::Image::ARGB, sourceWidth, sourceRadius, true);
    juce::Graphics spotlightGraphics (spotlight);
    const auto spotlightColour = theme::isAmberBlue()
        ? juce::Colour (0xe62b5480) : juce::Colour (spotlightCentre);
    const auto spotlightEdgeColour = theme::isAmberBlue()
        ? juce::Colour (0x002b5480) : juce::Colour (spotlightEdge);
    juce::ColourGradient radial (spotlightColour,
                                 static_cast<float> (sourceRadius), 0.0f,
                                 spotlightEdgeColour,
                                 static_cast<float> (sourceRadius),
                                 static_cast<float> (sourceRadius), true);
    spotlightGraphics.setGradientFill (radial);
    spotlightGraphics.fillRect (spotlight.getBounds());

    const juce::Rectangle<float> spotlightBounds (
        bounds.getCentreX() - radiusX, 0.0f, radiusX * 2.0f, radiusY);
    g.drawImage (spotlight, spotlightBounds,
                 juce::RectanglePlacement::stretchToFit, false);
}
