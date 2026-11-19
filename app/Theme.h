#pragma once
#include <juce_graphics/juce_graphics.h>

// Grafite e laranja do Project Freedom.
namespace theme
{
// Texto da interface em UTF-8 (acentos corretos em qualquer compilador).
inline juce::String ui (const char* utf8) { return juce::String::fromUTF8 (utf8); }

enum class Palette
{
    GraphiteOrange,
    BlackOrange,
    WhiteOrange
};

inline Palette& selectedPalette()
{
    static Palette palette = Palette::BlackOrange;
    return palette;
}

inline Palette current()
{
    return selectedPalette();
}

inline bool isLight()
{
    return current() == Palette::WhiteOrange;
}

inline void setCurrent (Palette palette)
{
    selectedPalette() = palette;

    const auto xmlFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("ProjectFreedom/preferences.xml");
    xmlFile.getParentDirectory().createDirectory();

    auto xml = juce::XmlDocument::parse (xmlFile);
    if (xml == nullptr || ! xml->hasTagName ("PROJECT_FREEDOM_PREFERENCES"))
        xml = std::make_unique<juce::XmlElement> ("PROJECT_FREEDOM_PREFERENCES");

    const char* value = "graphite-orange";
    if (palette == Palette::BlackOrange)
        value = "black-orange";
    else if (palette == Palette::WhiteOrange)
        value = "white-orange";

    xml->setAttribute ("theme", value);
    if (! xml->writeTo (xmlFile))
        juce::Logger::writeToLog ("Theme: nao foi possivel salvar a preferencia de tema.");
}

inline void load()
{
    const auto xmlFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("ProjectFreedom/preferences.xml");
    const auto xml = juce::XmlDocument::parse (xmlFile);
    if (xml == nullptr || ! xml->hasTagName ("PROJECT_FREEDOM_PREFERENCES"))
    {
        selectedPalette() = Palette::BlackOrange;
        return;
    }

    const auto value = xml->getStringAttribute ("theme", "black-orange").toLowerCase();
    if (value == "graphite-orange")
        selectedPalette() = Palette::GraphiteOrange;
    else if (value == "white-orange")
        selectedPalette() = Palette::WhiteOrange;
    else
        selectedPalette() = Palette::BlackOrange;
}

inline juce::Colour background()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xfff2f2f2);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff101112);
    return juce::Colour (0xff343638);
}
inline juce::Colour panel()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffffffff);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff191b1d);
    return juce::Colour (0xff292b2d);
}
inline juce::Colour panelLight()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xfffafafa);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff202224);
    return juce::Colour (0xff303234);
}
inline juce::Colour control()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffebebeb);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff090a0b);
    return juce::Colour (0xff242628);
}
inline juce::Colour hover()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffdedede);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff252729);
    return juce::Colour (0xff2c2e31);
}
inline juce::Colour pressed()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffd2d2d2);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff050606);
    return juce::Colour (0xff1d1f21);
}
inline juce::Colour border()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffb8b8b8);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff34373a);
    return juce::Colour (0xff45484a);
}
inline juce::Colour controlBorder() { return border(); }
inline juce::Colour text()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xff202124);
    return juce::Colour (0xfff1f1f1);
}
inline juce::Colour textDim()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xff56585b);
    if (current() == Palette::BlackOrange) return juce::Colour (0xffb8bbbe);
    return juce::Colour (0xffa5a7a8);
}
inline juce::Colour accent()       { return juce::Colour (0xffff6b00); }
inline juce::Colour accentHover()  { return juce::Colour (0xffff8524); }
inline juce::Colour taskActive()   { return accent(); }
inline juce::Colour recording()    { return juce::Colour (0xffe85b5b); }
inline juce::Colour warning()      { return accentHover(); }
inline juce::Colour noteFill()     { return accent(); }
inline juce::Colour noteSelected() { return accentHover(); }
inline juce::Colour rowWhite()     { return panel(); }
inline juce::Colour rowBlack()     { return control(); }
inline juce::Colour rowInScale()
{
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffe6e6e6);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff202224);
    return juce::Colour (0xff303234);
}
inline juce::Colour playhead()     { return text(); }
} // namespace theme
