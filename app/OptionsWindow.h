#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginScanner.h"
#include "Theme.h"
#include <functional>

class OptionsWindow final : public juce::DocumentWindow
{
public:
    OptionsWindow (juce::AudioDeviceManager&, PluginScanner&, bool createEdgeFades,
                   std::function<void (bool)> onCreateEdgeFadesChanged,
                   std::function<void (theme::Palette)> onThemeChanged);

    void closeButtonPressed() override;
};
