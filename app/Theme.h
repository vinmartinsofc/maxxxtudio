#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// NEONWAVE's black interface with selectable neon accents.
namespace theme
{
// Convert interface text from UTF-8 consistently across compilers.
inline juce::String ui (const char* utf8) { return juce::String::fromUTF8 (utf8); }

enum class Palette
{
    GraphiteOrange,
    BlackOrange,
    WhiteOrange,
    GraphiteBlue,
    BlackBlue,
    GraphiteRed,
    BlackRed,
    MatrixGraphite,
    MatrixBlack,
    HighTech,
    HighTechAmber,
    BlackTexturedBlue
};

enum class WaveformStyle
{
    Classic,
    Ultraviolet,
    Spectrum
};

enum class AccentStyle
{
    CyberpunkYellow,
    SynthwavePurple,
    NeonCyan,
    NeonMagenta,
    NeonGreen,
    NeonRed,
    NeonBlue
};

inline juce::FontOptions fontOptions (float size, int style = juce::Font::plain)
{
    return juce::FontOptions ("Aptos", size, style);
}

inline Palette& selectedPalette()
{
    static Palette palette = Palette::BlackTexturedBlue;
    return palette;
}

inline AccentStyle& selectedAccentStyle()
{
    static AccentStyle selected = AccentStyle::NeonCyan;
    return selected;
}

inline WaveformStyle& selectedWaveformStyle()
{
    static WaveformStyle selected = WaveformStyle::Classic;
    return selected;
}

inline juce::File preferencesFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MaXXXtudio/preferences.xml");
}

inline std::unique_ptr<juce::XmlElement> readPreferences()
{
    auto xml = juce::XmlDocument::parse (preferencesFile());
    if (xml == nullptr || ! xml->hasTagName ("MAXXXTUDIO_PREFERENCES"))
        return std::make_unique<juce::XmlElement> ("MAXXXTUDIO_PREFERENCES");
    return xml;
}

inline void writePreferences (const juce::XmlElement& xml)
{
    const auto file = preferencesFile();
    file.getParentDirectory().createDirectory();
    if (! xml.writeTo (file))
        juce::Logger::writeToLog ("Theme: Could not save preferences.");
}

inline Palette current()
{
    return selectedPalette();
}

inline WaveformStyle waveformStyle()
{
    return selectedWaveformStyle();
}

inline void setWaveformStyle (WaveformStyle style)
{
    selectedWaveformStyle() = style;
    auto xml = readPreferences();
    const char* value = style == WaveformStyle::Ultraviolet ? "ultraviolet"
                      : style == WaveformStyle::Spectrum ? "spectrum" : "classic";
    xml->setAttribute ("arrangementWaveform", value);
    writePreferences (*xml);
}

inline AccentStyle accentStyle()
{
    return selectedAccentStyle();
}

inline void setAccentStyle (AccentStyle style)
{
    selectedAccentStyle() = style;
    auto xml = readPreferences();
    const char* value = "neon-cyan";
    switch (style)
    {
        case AccentStyle::CyberpunkYellow: value = "cyberpunk-yellow"; break;
        case AccentStyle::SynthwavePurple: value = "synthwave-purple"; break;
        case AccentStyle::NeonCyan:        value = "neon-cyan"; break;
        case AccentStyle::NeonMagenta:     value = "neon-magenta"; break;
        case AccentStyle::NeonGreen:       value = "neon-green"; break;
        case AccentStyle::NeonRed:         value = "neon-red"; break;
        case AccentStyle::NeonBlue:        value = "neon-blue"; break;
    }
    xml->setAttribute ("interfaceAccent", value);
    writePreferences (*xml);
}

inline bool isLight()
{
    return current() == Palette::WhiteOrange;
}

inline bool isHighTech()
{
    return current() == Palette::HighTech || current() == Palette::HighTechAmber
        || current() == Palette::BlackTexturedBlue;
}

inline bool isHighTechAmber()
{
    return current() == Palette::HighTechAmber;
}

inline bool isBlackTexturedBlue()
{
    return current() == Palette::BlackTexturedBlue;
}

inline void load()
{
    const auto xml = readPreferences();
    const auto waveformStyle = xml->getStringAttribute ("arrangementWaveform", "classic");
    selectedWaveformStyle() = waveformStyle == "ultraviolet" || waveformStyle == "reference"
        ? WaveformStyle::Ultraviolet
        : waveformStyle == "spectrum" ? WaveformStyle::Spectrum : WaveformStyle::Classic;
    const auto accent = xml->getStringAttribute ("interfaceAccent", "neon-cyan").toLowerCase();
    selectedAccentStyle() = accent == "cyberpunk-yellow" ? AccentStyle::CyberpunkYellow
        : accent == "synthwave-purple" ? AccentStyle::SynthwavePurple
        : accent == "neon-magenta" ? AccentStyle::NeonMagenta
        : accent == "neon-green" ? AccentStyle::NeonGreen
        : accent == "neon-red" ? AccentStyle::NeonRed
        : accent == "neon-blue" ? AccentStyle::NeonBlue
        : AccentStyle::NeonCyan;
    selectedPalette() = Palette::BlackTexturedBlue;
}

inline juce::Colour background()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff08090a); // Blue-black gradient base.
    if (isHighTech()) return juce::Colour (0xff060a10);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xfff2f2f2);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff101112);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff101112);
    if (current() == Palette::BlackRed) return juce::Colour (0xff101112);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff101112);
    return juce::Colour (0xff343638);
}
inline juce::Colour panel()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff111315); // Main panel surface.
    if (isHighTech()) return juce::Colour (0xff0a111b);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffffffff);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff191b1d);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff191b1d);
    if (current() == Palette::BlackRed) return juce::Colour (0xff191b1d);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff191b1d);
    return juce::Colour (0xff292b2d);
}
inline juce::Colour panelLight()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff191c20); // Raised panel surface.
    if (isHighTech()) return juce::Colour (0xff101a27);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xfffafafa);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff202224);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff202224);
    if (current() == Palette::BlackRed) return juce::Colour (0xff202224);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff202224);
    return juce::Colour (0xff303234);
}
inline juce::Colour control()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff0c0e11); // Recessed controls.
    if (isHighTech()) return juce::Colour (0xff070c13);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffebebeb);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff090a0b);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff090a0b);
    if (current() == Palette::BlackRed) return juce::Colour (0xff090a0b);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff090a0b);
    return juce::Colour (0xff242628);
}
inline juce::Colour hover()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff181b20); // Hovered control surface.
    if (isHighTech()) return juce::Colour (0xff142333);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffdedede);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff252729);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff252729);
    if (current() == Palette::BlackRed) return juce::Colour (0xff252729);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff252729);
    return juce::Colour (0xff2c2e31);
}
inline juce::Colour pressed()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff090a0c); // Pressed control surface.
    if (isHighTech()) return juce::Colour (0xff05080d);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffd2d2d2);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff050606);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff050606);
    if (current() == Palette::BlackRed) return juce::Colour (0xff050606);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff050606);
    return juce::Colour (0xff1d1f21);
}
inline juce::Colour border()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff1d2023); // Subtle panel outline.
    if (isHighTech()) return juce::Colour (0xff1b4c73);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffb8b8b8);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff34373a);
    if (current() == Palette::BlackBlue) return juce::Colour (0xff34373a);
    if (current() == Palette::BlackRed) return juce::Colour (0xff34373a);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xff34373a);
    return juce::Colour (0xff45484a);
}
inline juce::Colour gridLine()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff1d2023); // Low-contrast grid line.
    return isHighTech() ? juce::Colour (0xff102031) : border();
}
inline juce::Colour controlBorder() { return border(); }
inline juce::Colour text()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xffeef3f7);
    if (isHighTech()) return juce::Colour (0xffedf4fb);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xff202124);
    return juce::Colour (0xfff1f1f1);
}
inline juce::Colour textDim()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff9a9ca8);
    if (isHighTech()) return juce::Colour (0xff9aacc0);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xff56585b);
    if (current() == Palette::BlackOrange) return juce::Colour (0xffb8bbbe);
    if (current() == Palette::BlackBlue) return juce::Colour (0xffb8bbbe);
    if (current() == Palette::BlackRed) return juce::Colour (0xffb8bbbe);
    if (current() == Palette::MatrixBlack) return juce::Colour (0xffb8bbbe);
    return juce::Colour (0xffa5a7a8);
}
inline juce::Colour accent()
{
    if (isBlackTexturedBlue())
    {
        switch (accentStyle())
        {
            case AccentStyle::CyberpunkYellow: return juce::Colour (0xffffd52a); // Legacy neon yellow.
            case AccentStyle::SynthwavePurple: return juce::Colour (0xffc35cff); // Legacy synthwave purple.
            case AccentStyle::NeonCyan:        return juce::Colour (0xff00e5ff); // Neon cyan accent.
            case AccentStyle::NeonMagenta:     return juce::Colour (0xffff2bd6); // Neon magenta accent.
            case AccentStyle::NeonGreen:       return juce::Colour (0xff39ff88); // Neon green accent.
            case AccentStyle::NeonRed:         return juce::Colour (0xffff3b4f); // Neon red accent.
            case AccentStyle::NeonBlue:        return juce::Colour (0xff398bff); // Neon blue accent.
        }
    }
    if (isHighTechAmber()) return juce::Colour (0xffff8500);
    if (isHighTech()) return juce::Colour (0xff25c2e6);
    if (current() == Palette::GraphiteBlue || current() == Palette::BlackBlue)
        return juce::Colour (0xff1685ff);
    if (current() == Palette::GraphiteRed || current() == Palette::BlackRed)
        return juce::Colour (0xffe53935);
    if (current() == Palette::MatrixGraphite || current() == Palette::MatrixBlack)
        return juce::Colour (0xff27c96b);
    return juce::Colour (0xffff6b00);
}
inline juce::Colour accentHover()
{
    if (isBlackTexturedBlue())
    {
        switch (accentStyle())
        {
            case AccentStyle::CyberpunkYellow: return juce::Colour (0xffffec75);
            case AccentStyle::SynthwavePurple: return juce::Colour (0xffe58aff);
            case AccentStyle::NeonCyan:        return juce::Colour (0xff75f4ff); // Cyan hover.
            case AccentStyle::NeonMagenta:     return juce::Colour (0xffff7de7); // Magenta hover.
            case AccentStyle::NeonGreen:       return juce::Colour (0xff82ffb6); // Green hover.
            case AccentStyle::NeonRed:         return juce::Colour (0xffff7b89); // Red hover.
            case AccentStyle::NeonBlue:        return juce::Colour (0xff79b2ff); // Blue hover.
        }
    }
    if (isHighTechAmber()) return juce::Colour (0xffffab32);
    if (isHighTech()) return juce::Colour (0xff398dff);
    if (current() == Palette::GraphiteBlue || current() == Palette::BlackBlue)
        return juce::Colour (0xff3d9cff);
    if (current() == Palette::GraphiteRed || current() == Palette::BlackRed)
        return juce::Colour (0xffff625c);
    if (current() == Palette::MatrixGraphite || current() == Palette::MatrixBlack)
        return juce::Colour (0xff4adf88);
    return juce::Colour (0xffff8524);
}
inline juce::Colour taskActive()   { return accent(); }
inline juce::Colour recording()    { return juce::Colour (0xffff4058); }
inline juce::Colour warning()
{
    return isHighTech() ? juce::Colour (0xffffa23b) : accentHover();
}
inline juce::Colour cyan()         { return juce::Colour (0xff00cce8); }
inline juce::Colour violet()       { return juce::Colour (0xffa675ff); }
inline juce::Colour magenta()      { return juce::Colour (0xffff4fa3); }
inline juce::Colour amber()        { return juce::Colour (0xffffa23b); }
inline juce::Colour emerald()      { return juce::Colour (0xff38d99b); }
inline juce::Colour noteFill()     { return accent(); }
inline juce::Colour noteSelected() { return accentHover(); }
inline juce::Colour rowWhite()     { return panel(); }
inline juce::Colour rowBlack()     { return control(); }
inline juce::Colour rowInScale()
{
    if (isBlackTexturedBlue()) return juce::Colour (0xff101116);
    if (isHighTech()) return juce::Colour (0xff142333);
    if (current() == Palette::WhiteOrange) return juce::Colour (0xffe6e6e6);
    if (current() == Palette::BlackOrange) return juce::Colour (0xff202224);
    if (current() == Palette::BlackRed) return juce::Colour (0xff202224);
    return juce::Colour (0xff303234);
}
inline void drawChromeSurface (juce::Graphics& g, juce::Rectangle<float> bounds,
                               float cornerRadius, juce::Colour fill)
{
    if (bounds.isEmpty())
        return;

    const float radius = juce::jmax (0.0f, juce::jmin (cornerRadius, bounds.getHeight() * 0.5f));
    juce::Path surface;
    surface.addRoundedRectangle (bounds, radius);
    if (isBlackTexturedBlue())
    {
        g.setColour (fill);
    }
    else
    {
        juce::ColourGradient gradient (fill.brighter (0.12f), bounds.getX(), bounds.getY(),
                                       fill.darker (0.12f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (gradient);
    }
    g.fillPath (surface);

    if (! isBlackTexturedBlue())
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (surface);
        for (float y = bounds.getY() + 2.0f; y < bounds.getBottom(); y += 4.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.drawLine (bounds.getX() + radius * 0.5f, y,
                        bounds.getRight() - radius * 0.5f, y, 0.5f);
        }
    }

    if (! isBlackTexturedBlue())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (bounds.getX() + radius, bounds.getY() + 0.5f,
                    bounds.getRight() - radius, bounds.getY() + 0.5f, 0.7f);
    }
    g.setColour (border());
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

inline float meterAmount (float gain)
{
    const float db = gain > 0.000001f
        ? 20.0f * std::log10 (gain) : -100.0f;
    return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

inline juce::Colour meterColour (float amount)
{
    static constexpr std::array<std::uint32_t, 6> colours {{
        0xff2878ff, 0xff12c8e8, 0xff20c982, 0xffd5ec55, 0xffff9838, 0xffff4058
    }};
    const float scaled = juce::jlimit (0.0f, 1.0f, amount) * static_cast<float> (colours.size() - 1);
    const auto index = std::min (static_cast<std::size_t> (scaled), colours.size() - 2);
    return juce::Colour (colours[index]).interpolatedWith (
        juce::Colour (colours[index + 1]), scaled - static_cast<float> (index));
}

inline void drawSegmentedMeter (juce::Graphics& g, juce::Rectangle<float> bounds,
                                float amount, bool vertical = false, int segmentCount = 24)
{
    if (bounds.isEmpty() || segmentCount <= 0)
        return;

    amount = juce::jlimit (0.0f, 1.0f, amount);
    const float gap = 1.0f;
    const float totalLength = vertical ? bounds.getHeight() : bounds.getWidth();
    const float segmentLength = std::max (
        0.0f, (totalLength - gap * static_cast<float> (segmentCount - 1)) / segmentCount);
    for (int i = 0; i < segmentCount; ++i)
    {
        const float position = static_cast<float> (i) / static_cast<float> (segmentCount);
        auto segment = vertical
            ? juce::Rectangle<float> (bounds.getX(),
                bounds.getBottom() - (static_cast<float> (i + 1) * (segmentLength + gap)),
                bounds.getWidth(), segmentLength)
            : juce::Rectangle<float> (bounds.getX() + static_cast<float> (i) * (segmentLength + gap),
                bounds.getY(), segmentLength, bounds.getHeight());
        const bool active = position < amount;
        g.setColour (active ? meterColour (position) : control().brighter (0.12f));
        g.fillRoundedRectangle (segment, juce::jmin (1.5f, segment.getHeight() * 0.5f));
    }

    g.setColour (border().withAlpha (0.8f));
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
}

inline juce::Colour playhead()     { return text(); }
} // namespace theme
