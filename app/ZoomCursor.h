#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace ui
{
inline const juce::MouseCursor& zoomCursor()
{
    static const juce::MouseCursor cursor = []
    {
        juce::Image image (juce::Image::ARGB, 32, 32, true);
        juce::Graphics g (image);

        g.setColour (juce::Colours::black);
        g.drawEllipse (2.0f, 2.0f, 17.0f, 17.0f, 4.0f);
        g.drawLine (16.0f, 16.0f, 28.0f, 28.0f, 4.0f);

        g.setColour (juce::Colours::white);
        g.drawEllipse (3.0f, 3.0f, 15.0f, 15.0f, 2.0f);
        g.drawLine (16.0f, 16.0f, 27.0f, 27.0f, 2.0f);

        g.setColour (theme::accent());
        g.drawEllipse (5.0f, 5.0f, 11.0f, 11.0f, 1.5f);
        return juce::MouseCursor (image, 5, 5);
    }();

    return cursor;
}
}
