#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

class ThemeKnobSlider : public juce::Slider
{
public:
    bool bipolar = false;
    double centreValue = std::numeric_limits<double>::quiet_NaN();
};

class ThemeKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosition, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};
