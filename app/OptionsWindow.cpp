#include "OptionsWindow.h"
#include "Theme.h"
#include <utility>

namespace
{
class AudioPreferencesPage final : public juce::Component
{
public:
    explicit AudioPreferencesPage (juce::AudioDeviceManager& manager)
        : selector (manager, 0, 2, 1, 2, true, false, false, false)
    {
        description.setColour (juce::Label::textColourId, theme::textDim());
        description.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (description);
        selector.setItemHeight (26);
        addAndMakeVisible (selector);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        description.setBounds (area.removeFromTop (34));
        selector.setBounds (area);
    }

private:
    juce::Label description { {}, "Configure audio and enable the MIDI inputs used by MIDI Learn." };
    juce::AudioDeviceSelectorComponent selector;
};

class PluginPreferencesPage final : public juce::Component, private juce::Timer
{
public:
    explicit PluginPreferencesPage (PluginScanner& pluginScanner) : scanner (pluginScanner)
    {
        title.setFont (theme::fontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (title);

        description.setColour (juce::Label::textColourId, theme::textDim());
        addAndMakeVisible (description);

        paths.setTextWhenNothingSelected ("No custom folders");
        addAndMakeVisible (paths);
        addAndMakeVisible (addButton);
        addAndMakeVisible (removeButton);
        addAndMakeVisible (scanButton);

        status.setColour (juce::Label::textColourId, theme::textDim());
        addAndMakeVisible (status);

        addButton.onClick = [this] { chooseSearchPath(); };
        removeButton.onClick = [this] { removeSelectedPath(); };
        scanButton.onClick = [this]
        {
            scanner.startScan();
            status.setText ("Scan started...", juce::dontSendNotification);
        };
        paths.onChange = [this] { removeButton.setEnabled (paths.getSelectedId() > 0); };
        refreshSearchPaths();
        startTimerHz (3);
    }

    ~PluginPreferencesPage() override { stopTimer(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        title.setBounds (area.removeFromTop (26));
        description.setBounds (area.removeFromTop (42));
        area.removeFromTop (8);
        paths.setBounds (area.removeFromTop (30));
        area.removeFromTop (12);
        auto buttons = area.removeFromTop (30);
        addButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (8);
        removeButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (8);
        scanButton.setBounds (buttons.removeFromLeft (150));
        status.setBounds (area.removeFromTop (32));
    }

private:
    void timerCallback() override
    {
        const bool scanning = scanner.isScanning();
        if (scanning)
        {
            scanWasActive = true;
            status.setText ("Plugin scan in progress...", juce::dontSendNotification);
        }
        else if (scanWasActive)
        {
            scanWasActive = false;
            status.setText ("Scan complete. Check the browser for plugins.",
                            juce::dontSendNotification);
        }
    }

    void refreshSearchPaths()
    {
        customPaths = scanner.customSearchPaths ("VST3");
        paths.clear (juce::dontSendNotification);
        for (std::size_t i = 0; i < customPaths.size(); ++i)
            paths.addItem (customPaths[i].getFullPathName(), static_cast<int> (i) + 1);
        paths.setSelectedId (customPaths.empty() ? 0 : 1, juce::dontSendNotification);
        removeButton.setEnabled (! customPaths.empty());
    }

    void chooseSearchPath()
    {
        if (fileChooser != nullptr)
            return;

        fileChooser = std::make_unique<juce::FileChooser> (
            "Add VST3 Plugin Folder",
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory));
        const juce::Component::SafePointer<PluginPreferencesPage> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                const auto directory = chooser.getResult();
                safeThis->fileChooser.reset();
                if (directory == juce::File())
                    return;

                const auto result = safeThis->scanner.addCustomSearchPath ("VST3", directory);
                if (result.failed())
                {
                    safeThis->status.setText (result.getErrorMessage(), juce::dontSendNotification);
                    return;
                }

                safeThis->refreshSearchPaths();
                safeThis->status.setText ("Folder added; starting a new scan...",
                                          juce::dontSendNotification);
                safeThis->scanner.startScan();
            });
    }

    void removeSelectedPath()
    {
        const int selected = paths.getSelectedId() - 1;
        if (selected < 0 || selected >= static_cast<int> (customPaths.size()))
            return;

        const auto result = scanner.removeCustomSearchPath (
            "VST3", customPaths[static_cast<std::size_t> (selected)]);
        if (result.failed())
        {
            status.setText (result.getErrorMessage(), juce::dontSendNotification);
            return;
        }

        refreshSearchPaths();
        status.setText ("Folder removed; starting a new scan...", juce::dontSendNotification);
        scanner.startScan();
    }

    PluginScanner& scanner;
    juce::Label title { {}, "VST3 Plugins" };
    juce::Label description { {}, "Add custom folders or run the plugin scan again." };
    juce::ComboBox paths;
    juce::TextButton addButton { "Add Folder..." }, removeButton { "Remove Folder" },
                     scanButton { "Scan Plugins" };
    juce::Label status;
    std::vector<juce::File> customPaths;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool scanWasActive = false;
};

class FadePreferencesPage final : public juce::Component
{
public:
    FadePreferencesPage (bool enabled, std::function<void (bool)> onChanged)
        : changed (std::move (onChanged))
    {
        title.setFont (theme::fontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        title.setText ("Record / Warp / Launch", juce::dontSendNotification);
        addAndMakeVisible (title);

        edgeFades.setButtonText ("Create short fades at the edges of imported audio (4 ms)");
        edgeFades.setToggleState (enabled, juce::dontSendNotification);
        edgeFades.onClick = [this]
        {
            if (changed)
                changed (edgeFades.getToggleState());
        };
        addAndMakeVisible (edgeFades);

        description.setText (
            "When enabled, new audio clips get short fade-ins and fade-outs to reduce clicks. "
            "Fades can be adjusted in the Arrangement; existing clips are not changed.",
            juce::dontSendNotification);
        description.setColour (juce::Label::textColourId, theme::textDim());
        description.setJustificationType (juce::Justification::topLeft);
        description.setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (description);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        title.setBounds (area.removeFromTop (28));
        area.removeFromTop (16);
        edgeFades.setBounds (area.removeFromTop (30));
        area.removeFromTop (8);
        description.setBounds (area.removeFromTop (64));
    }

private:
    std::function<void (bool)> changed;
    juce::Label title, description;
    juce::ToggleButton edgeFades;
};

class AppearancePreferencesPage final : public juce::Component
{
public:
    explicit AppearancePreferencesPage (std::function<void (theme::AccentStyle)> onChanged)
        : changed (std::move (onChanged))
    {
        title.setText ("NEONWAVE Interface", juce::dontSendNotification);
        title.setFont (theme::fontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (title);

        description.setText ("Choose the neon accent for the blue-black interface.",
                             juce::dontSendNotification);
        description.setColour (juce::Label::textColourId, theme::textDim());
        description.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (description);

        accentBox.addItem ("Neon Cyan", 1);
        accentBox.addItem ("Neon Green", 2);
        accentBox.addItem ("Neon Red", 3);
        accentBox.addItem ("Neon Blue", 4);
        accentBox.addItem ("Neon Magenta", 5);
        accentBox.addItem ("Synthwave Purple", 6);
        accentBox.addItem ("Cyberpunk Yellow", 7);
        const auto selectedId = theme::accentStyle() == theme::AccentStyle::NeonGreen ? 2
            : theme::accentStyle() == theme::AccentStyle::NeonRed ? 3
            : theme::accentStyle() == theme::AccentStyle::NeonBlue ? 4
            : theme::accentStyle() == theme::AccentStyle::NeonMagenta ? 5
            : theme::accentStyle() == theme::AccentStyle::SynthwavePurple ? 6
            : theme::accentStyle() == theme::AccentStyle::CyberpunkYellow ? 7 : 1;
        accentBox.setSelectedId (selectedId, juce::dontSendNotification);
        accentBox.onChange = [this]
        {
            if (changed)
            {
                const auto style = accentBox.getSelectedId() == 2 ? theme::AccentStyle::NeonGreen
                    : accentBox.getSelectedId() == 3 ? theme::AccentStyle::NeonRed
                    : accentBox.getSelectedId() == 4 ? theme::AccentStyle::NeonBlue
                    : accentBox.getSelectedId() == 5 ? theme::AccentStyle::NeonMagenta
                    : accentBox.getSelectedId() == 6 ? theme::AccentStyle::SynthwavePurple
                    : accentBox.getSelectedId() == 7 ? theme::AccentStyle::CyberpunkYellow
                                                     : theme::AccentStyle::NeonCyan;
                changed (style);
            }
        };
        addAndMakeVisible (accentBox);

    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        title.setBounds (area.removeFromTop (28));
        description.setBounds (area.removeFromTop (58));
        area.removeFromTop (12);
        accentBox.setBounds (area.removeFromTop (34).withWidth (260));
    }

private:
    std::function<void (theme::AccentStyle)> changed;
    juce::Label title, description;
    juce::ComboBox accentBox;
};

class WaveformPreferencesPage final : public juce::Component
{
public:
    WaveformPreferencesPage (theme::WaveformStyle currentStyle,
                             std::function<void (theme::WaveformStyle)> onChanged)
        : changed (std::move (onChanged))
    {
        title.setText ("Waveform Style", juce::dontSendNotification);
        title.setFont (theme::fontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (title);

        description.setText (
            "Choose how clip waveforms appear in the Arrangement.",
            juce::dontSendNotification);
        description.setColour (juce::Label::textColourId, theme::textDim());
        description.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (description);

        styleBox.addItem ("Classic (Default)", 1);
        styleBox.addItem ("Ultraviolet", 2);
        styleBox.addItem ("Spectrum", 3);
        styleBox.setSelectedId (currentStyle == theme::WaveformStyle::Ultraviolet ? 2
                               : currentStyle == theme::WaveformStyle::Spectrum ? 3 : 1,
                               juce::dontSendNotification);
        styleBox.onChange = [this]
        {
            const auto style = styleBox.getSelectedId() == 2 ? theme::WaveformStyle::Ultraviolet
                              : styleBox.getSelectedId() == 3 ? theme::WaveformStyle::Spectrum
                                                              : theme::WaveformStyle::Classic;
            if (changed)
                changed (style);
        };
        addAndMakeVisible (styleBox);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        title.setBounds (area.removeFromTop (28));
        description.setBounds (area.removeFromTop (42));
        area.removeFromTop (12);
        styleBox.setBounds (area.removeFromTop (34).withWidth (260));
    }

private:
    std::function<void (theme::WaveformStyle)> changed;
    juce::Label title, description;
    juce::ComboBox styleBox;
};
}

OptionsWindow::OptionsWindow (juce::AudioDeviceManager& audioDeviceManager,
                              PluginScanner& scanner,
                              bool createEdgeFades,
                              std::function<void (bool)> onCreateEdgeFadesChanged,
                              std::function<void (theme::AccentStyle)> onAccentStyleChanged,
                              theme::WaveformStyle waveformStyle,
                              std::function<void (theme::WaveformStyle)> onWaveformStyleChanged)
    : juce::DocumentWindow (theme::ui ("Preferences"), theme::panel(),
                            juce::DocumentWindow::closeButton)
{
    auto tabs = std::make_unique<juce::TabbedComponent> (juce::TabbedButtonBar::TabsAtTop);
    tabs->addTab (theme::ui ("Audio"), theme::panel(), new AudioPreferencesPage (audioDeviceManager), true);
    tabs->addTab (theme::ui ("Plugins"), theme::panel(), new PluginPreferencesPage (scanner), true);
    tabs->addTab (theme::ui ("Record/Warp/Launch"), theme::panel(),
                  new FadePreferencesPage (createEdgeFades, std::move (onCreateEdgeFadesChanged)), true);
    tabs->addTab (theme::ui ("Interface"), theme::panel(),
                  new AppearancePreferencesPage (std::move (onAccentStyleChanged)), true);
    tabs->addTab (theme::ui ("Waveform"), theme::panel(),
                  new WaveformPreferencesPage (waveformStyle,
                                               std::move (onWaveformStyleChanged)), true);

    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setContentOwned (tabs.release(), true);
    setColour (juce::DocumentWindow::backgroundColourId, theme::panel());
    centreWithSize (760, 600);
    setVisible (true);
}

void OptionsWindow::closeButtonPressed()
{
    setVisible (false);
}
