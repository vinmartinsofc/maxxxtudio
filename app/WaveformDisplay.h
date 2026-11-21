#pragma once

#include "Arrangement.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <juce_graphics/juce_graphics.h>

inline void drawWaveformEnvelope (juce::Graphics& g, juce::Rectangle<float> area,
                                 const std::vector<roll::WaveformRange>& waveform,
                                 juce::Colour colour, float halfHeight)
{
    if (waveform.empty() || area.getWidth() < 1.0f || area.getHeight() < 1.0f)
        return;

    const int columns = std::max (1, static_cast<int> (std::ceil (area.getWidth())));
    const auto center = area.getCentreY();
    std::vector<float> lowerEdges (static_cast<std::size_t> (columns));
    juce::Path envelope;

    const auto displayAmplitude = [] (float value)
    {
        const auto clamped = std::clamp (value, -1.0f, 1.0f);
        return std::copysign (std::pow (std::abs (clamped), 0.72f), clamped);
    };

    for (int column = 0; column < columns; ++column)
    {
        const auto first = std::min (waveform.size() - 1,
            static_cast<std::size_t> (static_cast<double> (column) / columns * waveform.size()));
        const auto last = std::max (first + 1, std::min (waveform.size(),
            static_cast<std::size_t> (static_cast<double> (column + 1) / columns * waveform.size())));
        float minimum = 1.0f;
        float maximum = -1.0f;
        for (auto bin = first; bin < last; ++bin)
        {
            minimum = std::min (minimum, waveform[bin].minimum);
            maximum = std::max (maximum, waveform[bin].maximum);
        }

        const float x = area.getX() + (static_cast<float> (column) + 0.5f)
                                      * area.getWidth() / static_cast<float> (columns);
        const float top = center - displayAmplitude (maximum) * halfHeight;
        const float bottom = center - displayAmplitude (minimum) * halfHeight;
        if (column == 0)
            envelope.startNewSubPath (x, top);
        else
            envelope.lineTo (x, top);
        lowerEdges[static_cast<std::size_t> (column)] = bottom;
    }

    for (int column = columns - 1; column >= 0; --column)
    {
        const float x = area.getX() + (static_cast<float> (column) + 0.5f)
                                      * area.getWidth() / static_cast<float> (columns);
        envelope.lineTo (x, lowerEdges[static_cast<std::size_t> (column)]);
    }
    envelope.closeSubPath();

    g.setColour (colour.withAlpha (0.32f));
    g.fillPath (envelope);
    g.setColour (colour.withAlpha (0.95f));
    g.strokePath (envelope, juce::PathStrokeType (0.8f));
}

inline juce::Colour ultravioletWaveformColour (float amplitude)
{
    const float clampedAmplitude = std::clamp (amplitude, 0.0f, 1.0f);
    const float hue = clampedAmplitude <= 0.5f
        ? juce::jmap (clampedAmplitude, 0.0f, 0.5f, 0.0f, 0.08f)
        : juce::jmap (clampedAmplitude, 0.5f, 1.0f, 0.08f, 0.33f);
    const float lightness = juce::jmap (clampedAmplitude, 0.38f, 0.66f);
    return juce::Colour::fromHSL (hue, 0.9f, lightness, 1.0f);
}

inline void drawReferenceWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                                   const std::vector<roll::WaveformRange>& waveform,
                                   float halfHeight)
{
    if (waveform.empty() || area.getWidth() < 1.0f || area.getHeight() < 1.0f)
        return;

    constexpr int columnStep = 2;
    constexpr float barWidth = 1.5f;
    const int columns = std::max (1, static_cast<int> (std::ceil (
        area.getWidth() / columnStep)));
    const float centre = area.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.24f));
    g.drawHorizontalLine (juce::roundToInt (centre), area.getX(), area.getRight());

    for (int column = 0; column < columns; ++column)
    {
        const auto first = std::min (waveform.size() - 1,
            static_cast<std::size_t> (static_cast<double> (column) / columns * waveform.size()));
        const auto last = std::max (first + 1, std::min (waveform.size(),
            static_cast<std::size_t> (static_cast<double> (column + 1) / columns
                                      * waveform.size())));
        float peak = 0.0f;
        for (auto bin = first; bin < last; ++bin)
            peak = std::max (peak, std::max (std::abs (waveform[bin].minimum),
                                             std::abs (waveform[bin].maximum)));

        const float amplitude = std::pow (std::clamp (peak, 0.0f, 1.0f), 0.72f);
        const float x = area.getX() + static_cast<float> (column * columnStep);
        const float width = std::min (barWidth, area.getRight() - x);
        g.setColour (ultravioletWaveformColour (amplitude));
        const float height = std::max (0.75f, amplitude * halfHeight);
        g.fillRect (x, centre - height, width, height * 2.0f);
    }
}
