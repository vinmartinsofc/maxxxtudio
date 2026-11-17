#include "OptionsWindow.h"
#include "Theme.h"
#include <utility>

namespace
{
class AudioPreferencesPage final : public juce::Component
{
public:
    explicit AudioPreferencesPage (juce::AudioDeviceManager& manager)
        : selector (manager, 0, 2, 1, 2, false, false, false, false)
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
    juce::Label description { {}, "Configure dispositivo de entrada/saida, canais, taxa de amostragem e buffer." };
    juce::AudioDeviceSelectorComponent selector;
};

class PluginPreferencesPage final : public juce::Component, private juce::Timer
{
public:
    explicit PluginPreferencesPage (PluginScanner& pluginScanner) : scanner (pluginScanner)
    {
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (title);

        description.setColour (juce::Label::textColourId, theme::textDim());
        addAndMakeVisible (description);

        paths.setTextWhenNothingSelected ("Nenhuma pasta customizada");
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
            status.setText ("Varredura iniciada...", juce::dontSendNotification);
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
            status.setText ("Varredura de plugins em andamento...", juce::dontSendNotification);
        }
        else if (scanWasActive)
        {
            scanWasActive = false;
            status.setText ("Varredura concluida. Confira os plugins no navegador.",
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
            "Adicionar pasta de plugins VST3",
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
                safeThis->status.setText ("Pasta adicionada; iniciando nova varredura...",
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
        status.setText ("Pasta removida; iniciando nova varredura...", juce::dontSendNotification);
        scanner.startScan();
    }

    PluginScanner& scanner;
    juce::Label title { {}, "Plugins VST3" };
    juce::Label description { {}, "Adicione pastas personalizadas ou execute novamente a busca de plugins." };
    juce::ComboBox paths;
    juce::TextButton addButton { "Adicionar pasta..." }, removeButton { "Remover pasta" },
                     scanButton { "Escanear plugins" };
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
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        title.setText ("Record / Warp / Launch", juce::dontSendNotification);
        addAndMakeVisible (title);

        edgeFades.setButtonText ("Criar fades curtos nas bordas do audio importado (4 ms)");
        edgeFades.setToggleState (enabled, juce::dontSendNotification);
        edgeFades.onClick = [this]
        {
            if (changed)
                changed (edgeFades.getToggleState());
        };
        addAndMakeVisible (edgeFades);

        description.setText (
            "Quando ativado, novos clips de audio recebem fades curtos de entrada e saida para reduzir cliques. "
            "Os fades podem ser ajustados no Arrangement; clips existentes nao sao alterados.",
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
}

OptionsWindow::OptionsWindow (juce::AudioDeviceManager& audioDeviceManager,
                              PluginScanner& scanner,
                              bool createEdgeFades,
                              std::function<void (bool)> onCreateEdgeFadesChanged)
    : juce::DocumentWindow (theme::ui ("Preferencias"), theme::panel(),
                            juce::DocumentWindow::closeButton)
{
    auto tabs = std::make_unique<juce::TabbedComponent> (juce::TabbedButtonBar::TabsAtTop);
    tabs->addTab (theme::ui ("Audio"), theme::panel(), new AudioPreferencesPage (audioDeviceManager), true);
    tabs->addTab (theme::ui ("Plugins"), theme::panel(), new PluginPreferencesPage (scanner), true);
    tabs->addTab (theme::ui ("Record/Warp/Launch"), theme::panel(),
                  new FadePreferencesPage (createEdgeFades, std::move (onCreateEdgeFadesChanged)), true);

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
