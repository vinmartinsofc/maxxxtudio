#include "ThemeBackground.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr std::uint32_t backgroundTop = 0xff14161a;       // Blue-black gradient top.
constexpr std::uint32_t backgroundBottom = 0xff08090a;    // Blue-black gradient base.
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
    juce::ColourGradient base (juce::Colour (backgroundTop), 0.0f, 0.0f,
                               juce::Colour (backgroundBottom), 0.0f, bounds.getHeight(), false);
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
    juce::ColourGradient radial (juce::Colour (spotlightCentre),
                                 static_cast<float> (sourceRadius), 0.0f,
                                 juce::Colour (spotlightEdge),
                                 static_cast<float> (sourceRadius),
                                 static_cast<float> (sourceRadius), true);
    spotlightGraphics.setGradientFill (radial);
    spotlightGraphics.fillRect (spotlight.getBounds());

    const juce::Rectangle<float> spotlightBounds (
        bounds.getCentreX() - radiusX, 0.0f, radiusX * 2.0f, radiusY);
    g.drawImage (spotlight, spotlightBounds,
                 juce::RectanglePlacement::stretchToFit, false);
}
