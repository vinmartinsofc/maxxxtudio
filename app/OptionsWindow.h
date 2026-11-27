#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginScanner.h"
#include "Theme.h"
#include <functional>
#include <utility>

class OptionsWindow final : public juce::DocumentWindow
{
public:
    OptionsWindow (juce::AudioDeviceManager&, PluginScanner&, bool createEdgeFades,
                   std::function<void (bool)> onCreateEdgeFadesChanged,
    std::function<void (theme::Palette)> onPaletteChanged,
                   std::function<void (theme::AccentStyle)> onAccentStyleChanged,
                   theme::WaveformStyle waveformStyle,
                   std::function<void (theme::WaveformStyle)> onWaveformStyleChanged,
                   std::function<std::pair<float, float>()> audioInputLevelProvider);

    void closeButtonPressed() override;
    void startThemeTransition();
};
