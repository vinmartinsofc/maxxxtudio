#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ThemeBackground final : public juce::Component
{
public:
    ThemeBackground();
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void rebuildImage();

    juce::Image backgroundImage;
};
