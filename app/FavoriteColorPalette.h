#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace favoriteColorPalette
{
inline const std::array<juce::Colour, 7> colors {
    juce::Colour (0xfff44336), juce::Colour (0xffff9800), juce::Colour (0xffffeb3b),
    juce::Colour (0xff4caf50), juce::Colour (0xff2196f3), juce::Colour (0xff9c27b0),
    juce::Colour (0xff9e9e9e)
};

inline constexpr std::array<const char*, 7> names {
    "Vermelho", "Laranja", "Amarelo", "Verde", "Azul", "Roxo", "Cinza"
};
}
