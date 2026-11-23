#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

class ThemeKnobSlider : public juce::Slider
{
public:
    bool bipolar = false;
    double centreValue = std::numeric_limits<double>::quiet_NaN();
};

class MixerFaderSlider final : public juce::Slider
{
public:
    MixerFaderSlider();

    void setTrackColour (juce::Colour);
    juce::Colour getTrackColour() const noexcept { return trackColour; }
    float getCapHeight() const noexcept;
    void drawCachedScale (juce::Graphics&);
    static juce::NormalisableRange<double> decibelRange (double maximumDb);
    static double normalisedPositionForDecibels (double decibels);
    static double decibelsForNormalisedPosition (double position);

private:
    void updateScaleCache();

    juce::Colour trackColour;
    juce::Image scaleCache;
    juce::Colour cachedTrackColour, cachedSurfaceColour, cachedBorderColour, cachedTextColour;
};

class MixerPanSlider final : public juce::Slider
{
public:
    void setTrackColour (juce::Colour colour);
    juce::Colour getTrackColour() const noexcept { return trackColour; }

private:
    juce::Colour trackColour;
};

class ThemeKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::MouseCursor getMouseCursorFor (juce::Component&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float width, float height,
                      bool ticked, bool enabled, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down,
                       int buttonX, int buttonY, int buttonWidth, int buttonHeight,
                       juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosition, float minSliderPosition, float maxSliderPosition,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height,
                                juce::TextEditor&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool vertical, int thumbStart, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosition, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};
