#include "GrooveBrowser.h"
#include "Theme.h"
#include <algorithm>

class GrooveBrowserWindow::Content final : public juce::Component
{
    class CategoryTabs final : public juce::TabbedComponent
    {
    public:
        explicit CategoryTabs (Content& ownerToUse)
            : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop), owner (ownerToUse)
        {
        }

        void currentTabChanged (int, const juce::String&) override { owner.tabsChanged(); }

    private:
        Content& owner;
    };

public:
    explicit Content (GrooveBrowserWindow& ownerToUse)
        : owner (ownerToUse), tabs (*this)
    {
        addAndMakeVisible (tabs);
        addAndMakeVisible (importButton);
        addAndMakeVisible (searchEditor);
        addAndMakeVisible (autoPreviewButton);
        addAndMakeVisible (previewButton);
        addAndMakeVisible (stopButton);
        addAndMakeVisible (applyButton);
        addAndMakeVisible (clearButton);
        addAndMakeVisible (amountLabel);
        addAndMakeVisible (amountSlider);
        addAndMakeVisible (appliedGrooveLabel);
        addAndMakeVisible (statusLabel);

        importButton.setButtonText ("Importar .agr");
        searchEditor.setTextToShowWhenEmpty ("Pesquisar grooves...", theme::textDim());
        searchEditor.setColour (juce::TextEditor::backgroundColourId, theme::panel());
        searchEditor.setColour (juce::TextEditor::outlineColourId, theme::border());
        searchEditor.setColour (juce::TextEditor::textColourId, theme::text());
        searchEditor.onTextChange = [this] { updateSearchResults(); };
        autoPreviewButton.setButtonText ("Auto preview");
        autoPreviewButton.setTooltip ("Tocar automaticamente o groove selecionado.");
        juce::PropertiesFile::Options options;
        options.applicationName = "MaXXXtudioGrooveBrowser";
        options.filenameSuffix = ".settings";
        options.folderName = "MaXXXtudio";
        grooveProperties.setStorageParameters (options);
        grooveSettings = grooveProperties.getUserSettings();
        autoPreviewButton.setToggleState (
            grooveSettings != nullptr && grooveSettings->getBoolValue ("autoPreview", false),
            juce::dontSendNotification);
        previewButton.setButtonText ("Preview");
        stopButton.setButtonText ("Parar");
        applyButton.setButtonText ("Aplicar ao clip");
        clearButton.setButtonText ("Remover groove");
        amountLabel.setText ("Intensidade", juce::dontSendNotification);
        amountLabel.setColour (juce::Label::textColourId, theme::textDim());
        statusLabel.setColour (juce::Label::textColourId, theme::textDim());
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        appliedGrooveLabel.setColour (juce::Label::textColourId, theme::accent());
        appliedGrooveLabel.setJustificationType (juce::Justification::centredLeft);

        amountSlider.setRange (0.0, 100.0, 1.0);
        amountSlider.setValue (100.0, juce::dontSendNotification);
        amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        amountSlider.setColour (juce::Slider::textBoxTextColourId, theme::text());
        amountSlider.setColour (juce::Slider::textBoxBackgroundColourId, theme::panel());
        amountSlider.setColour (juce::Slider::thumbColourId, theme::accent());
        amountSlider.setColour (juce::Slider::trackColourId, theme::border());
        amountSlider.setMouseDragSensitivity (180);

        const auto directory = GrooveLibrary::defaultGroovesDirectory();
        juce::String skippedFiles;
        const std::vector<juce::String> includedCategories {
            "Utility", "Swing / Basic", "Style", "Percussion"
        };
        categories = GrooveLibrary::loadCategories (directory, &skippedFiles, includedCategories);
        std::sort (categories.begin(), categories.end(), [&includedCategories] (const auto& first, const auto& second)
        {
            const auto firstOrder = std::find (includedCategories.begin(), includedCategories.end(), first.name);
            const auto secondOrder = std::find (includedCategories.begin(), includedCategories.end(), second.name);
            return firstOrder < secondOrder;
        });
        int loadedGrooves = 0;
        for (std::size_t index = 0; index < categories.size(); ++index)
        {
            auto page = std::make_unique<CategoryPage> (*this, index);
            loadedGrooves += static_cast<int> (categories[index].entries.size());
            tabs.addTab (categories[index].name, theme::panel(), page.get(), false);
            pages.push_back (std::move (page));
        }

        tabs.setTabBarDepth (28);
        tabs.setColour (juce::TabbedComponent::backgroundColourId, theme::background());
        tabs.setColour (juce::TabbedComponent::outlineColourId, theme::border());

        importButton.onClick = [this] { importGrooves(); };
        autoPreviewButton.onClick = [this]
        {
            if (grooveSettings != nullptr)
            {
                grooveSettings->setValue ("autoPreview", autoPreviewButton.getToggleState());
                grooveSettings->saveIfNeeded();
            }
            if (autoPreviewButton.getToggleState())
                previewCurrentSelection();
            else if (owner.onStopPreview)
                owner.onStopPreview();
        };
        previewButton.onClick = [this]
        {
            if (const auto* groove = selectedGroove(); groove != nullptr && owner.onPreview)
                owner.onPreview (withAmount (*groove));
        };
        stopButton.onClick = [this]
        {
            if (owner.onStopPreview)
                owner.onStopPreview();
            setStatusText ("Preview parado.");
        };
        applyButton.onClick = [this]
        {
            if (const auto* groove = selectedGroove(); groove != nullptr && owner.onApply)
            {
                const auto applied = withAmount (*groove);
                setStatusText (owner.onApply (applied));
                setAppliedName (applied.name);
            }
        };
        clearButton.onClick = [this]
        {
            if (owner.onClear)
            {
                setStatusText (owner.onClear());
                setAppliedName ({});
            }
        };

        if (categories.empty())
            setStatusText ("Nenhum groove compativel encontrado em: " + directory.getFullPathName());
        else
        {
            setStatusText (juce::String (loadedGrooves) + " grooves em "
                           + juce::String (static_cast<int> (categories.size())) + " abas."
                           + (skippedFiles.isNotEmpty() ? " Alguns arquivos nao puderam ser lidos." : ""));
            statusLabel.setTooltip (skippedFiles);
        }
        updateButtons();
    }

    void setTargetAvailable (bool available)
    {
        targetAvailable = available;
        updateButtons();
    }

    void setAutoPreviewEnabled (bool enabled)
    {
        autoPreviewButton.setToggleState (enabled, juce::dontSendNotification);
        if (grooveSettings != nullptr)
        {
            grooveSettings->setValue ("autoPreview", enabled);
            grooveSettings->saveIfNeeded();
        }

        if (enabled)
            previewCurrentSelection();
        else if (owner.onStopPreview)
            owner.onStopPreview();
    }

    void setAppliedName (const juce::String& name)
    {
        appliedName = name;
        clearButton.setEnabled (appliedName.isNotEmpty());
        appliedGrooveLabel.setText (appliedName.isNotEmpty()
            ? "Groove aplicado: " + appliedName : "Nenhum groove aplicado",
            juce::dontSendNotification);
        appliedGrooveLabel.setTooltip (appliedName);
    }

    void setStatusText (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto row = area.removeFromTop (28);
        importButton.setBounds (row.removeFromLeft (108));
        row.removeFromLeft (8);
        autoPreviewButton.setBounds (row.removeFromRight (124));
        row.removeFromRight (8);
        searchEditor.setBounds (row);
        area.removeFromTop (8);
        tabs.setBounds (area.removeFromTop (std::max (100, area.getHeight() - 112)));
        area.removeFromTop (8);
        row = area.removeFromTop (28);
        previewButton.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (5);
        stopButton.setBounds (row.removeFromLeft (66));
        row.removeFromLeft (5);
        applyButton.setBounds (row.removeFromLeft (112));
        row.removeFromLeft (5);
        clearButton.setBounds (row);
        area.removeFromTop (6);
        row = area.removeFromTop (24);
        amountLabel.setBounds (row.removeFromLeft (76));
        amountSlider.setBounds (row);
        area.removeFromTop (3);
        appliedGrooveLabel.setBounds (area.removeFromTop (22));
        statusLabel.setBounds (area.removeFromTop (24));
    }

private:
    class CategoryPage final : public juce::Component, private juce::ListBoxModel
    {
    public:
        CategoryPage (Content& ownerToUse, std::size_t categoryIndexToUse)
            : owner (ownerToUse), categoryIndex (categoryIndexToUse), listBox ("Grooves", this)
        {
            addAndMakeVisible (listBox);
            listBox.setRowHeight (24);
            listBox.setColour (juce::ListBox::backgroundColourId, theme::panel());
            listBox.setColour (juce::ListBox::outlineColourId, theme::border());
            setSearchQuery ({});
        }

        const roll::GrooveTemplate* selectedGroove() const
        {
            const int row = listBox.getSelectedRow();
            return juce::isPositiveAndBelow (row, static_cast<int> (filteredRows.size()))
                ? &owner.categories[categoryIndex].entries[filteredRows[static_cast<std::size_t> (row)]].groove
                : nullptr;
        }

        void setSearchQuery (const juce::String& query)
        {
            const auto& entries = owner.categories[categoryIndex].entries;
            filteredRows.clear();
            for (std::size_t index = 0; index < entries.size(); ++index)
                if (query.isEmpty() || juce::String (entries[index].groove.name).containsIgnoreCase (query))
                    filteredRows.push_back (index);
            listBox.updateContent();
            listBox.selectRow (filteredRows.empty() ? -1 : 0, juce::dontSendNotification);
        }

        int numMatches() const { return static_cast<int> (filteredRows.size()); }

        void resized() override { listBox.setBounds (getLocalBounds()); }

    private:
        int getNumRows() override
        {
            return static_cast<int> (filteredRows.size());
        }

        void paintListBoxItem (int row, juce::Graphics& graphics, int width, int height,
                               bool selected) override
        {
            const auto& entries = owner.categories[categoryIndex].entries;
            if (! juce::isPositiveAndBelow (row, static_cast<int> (filteredRows.size())))
                return;
            const auto& groove = entries[filteredRows[static_cast<std::size_t> (row)]].groove;

            const auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);
            if (selected)
                graphics.setColour (theme::accent().withAlpha (0.7f));
            else if ((row & 1) != 0)
                graphics.setColour (theme::panel().brighter (0.035f));
            else
                graphics.setColour (theme::panel());
            graphics.fillRoundedRectangle (bounds.toFloat(), 3.0f);
            graphics.setColour (theme::text());
            graphics.setFont (juce::FontOptions (13.0f));
            graphics.drawText (juce::String (groove.name),
                               bounds.reduced (8, 0), juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged (int) override { owner.selectionChanged(); }

        Content& owner;
        std::size_t categoryIndex;
        juce::ListBox listBox;
        std::vector<std::size_t> filteredRows;
    };

    void updateSearchResults()
    {
        const auto query = searchEditor.getText().trim();
        for (auto& page : pages)
            page->setSearchQuery (query);

        if (const auto* page = currentPage(); page != nullptr && page->numMatches() == 0)
        {
            const auto firstMatch = std::find_if (pages.begin(), pages.end(),
                [] (const auto& candidate) { return candidate->numMatches() > 0; });
            if (firstMatch != pages.end())
                tabs.setCurrentTabIndex (static_cast<int> (std::distance (pages.begin(), firstMatch)));
        }

        if (query.isNotEmpty())
        {
            int matches = 0;
            for (const auto& page : pages)
                matches += page->numMatches();
            setStatusText (matches == 0 ? "Nenhum groove encontrado."
                                        : juce::String (matches) + " groove(s) encontrado(s).");
        }
        selectionChanged();
    }

    void tabsChanged()
    {
        previewCurrentSelection();
        updateButtons();
    }

    void selectionChanged()
    {
        previewCurrentSelection();
        updateButtons();
    }

    void previewCurrentSelection()
    {
        if (owner.onStopPreview)
            owner.onStopPreview();
        if (autoPreviewButton.getToggleState())
            if (const auto* groove = selectedGroove(); groove != nullptr && owner.onPreview)
                owner.onPreview (withAmount (*groove));
    }

    CategoryPage* currentPage() const
    {
        const int index = tabs.getCurrentTabIndex();
        return juce::isPositiveAndBelow (index, static_cast<int> (pages.size()))
            ? pages[static_cast<std::size_t> (index)].get() : nullptr;
    }

    void importGrooves()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Importar grooves", GrooveLibrary::defaultGroovesDirectory(), "*.agr");
        const juce::Component::SafePointer<Content> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                auto* page = safeThis->currentPage();
                if (page == nullptr)
                {
                    safeThis->setStatusText ("Selecione uma aba antes de importar grooves.");
                    safeThis->fileChooser.reset();
                    return;
                }

                auto& entries = safeThis->categories[static_cast<std::size_t> (
                    safeThis->tabs.getCurrentTabIndex())].entries;
                int imported = 0;
                juce::String firstError;
                for (const auto& file : chooser.getResults())
                {
                    roll::GrooveTemplate groove;
                    const auto result = GrooveLibrary::loadFile (file, groove);
                    if (result.failed())
                    {
                        if (firstError.isEmpty())
                            firstError = file.getFileName() + ": " + result.getErrorMessage();
                        continue;
                    }

                    const auto existing = std::find_if (entries.begin(), entries.end(),
                        [&file] (const auto& entry) { return entry.file == file; });
                    if (existing == entries.end())
                    {
                        entries.push_back ({ file, std::move (groove) });
                        ++imported;
                    }
                }

                std::sort (entries.begin(), entries.end(),
                    [] (const auto& first, const auto& second)
                    { return first.groove.name < second.groove.name; });
                page->setSearchQuery (safeThis->searchEditor.getText().trim());
                safeThis->setStatusText (imported > 0
                    ? juce::String (imported) + " groove(s) importado(s) na aba "
                        + safeThis->tabs.getCurrentTabName() + "."
                    : firstError.isNotEmpty() ? firstError : "Nenhum groove novo selecionado.");
                safeThis->fileChooser.reset();
                safeThis->updateButtons();
            });
    }

    const roll::GrooveTemplate* selectedGroove() const
    {
        const auto* page = currentPage();
        return page != nullptr ? page->selectedGroove() : nullptr;
    }

    roll::GrooveTemplate withAmount (roll::GrooveTemplate groove) const
    {
        const double amount = amountSlider.getValue() / 100.0;
        groove.timingAmount = amount;
        groove.velocityAmount = amount;
        return groove;
    }

    void updateButtons()
    {
        const bool hasSelection = selectedGroove() != nullptr;
        previewButton.setEnabled (hasSelection);
        applyButton.setEnabled (hasSelection && targetAvailable);
        clearButton.setEnabled (appliedName.isNotEmpty() && targetAvailable);
        amountSlider.setEnabled (hasSelection);
    }

    GrooveBrowserWindow& owner;
    std::vector<GrooveLibraryCategory> categories;
    std::vector<std::unique_ptr<CategoryPage>> pages;
    CategoryTabs tabs;
    juce::TextButton importButton, previewButton, stopButton, applyButton, clearButton;
    juce::TextEditor searchEditor;
    juce::ToggleButton autoPreviewButton;
    juce::Label amountLabel, statusLabel;
    juce::Slider amountSlider;
    juce::Label appliedGrooveLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ApplicationProperties grooveProperties;
    juce::PropertiesFile* grooveSettings = nullptr;
    juce::String appliedName;
    bool targetAvailable = false;
};

GrooveBrowserWindow::GrooveBrowserWindow()
    : juce::DocumentWindow ("Grooves", theme::background(), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    content = new Content (*this);
    addTransportKeyListenerRecursively (*content);
    addKeyListener (this);
    setContentOwned (content, true);
    centreWithSize (620, 480);
}

GrooveBrowserWindow::~GrooveBrowserWindow()
{
    if (content != nullptr)
        removeTransportKeyListenerRecursively (*content);
    removeKeyListener (this);
}

void GrooveBrowserWindow::addTransportKeyListenerRecursively (juce::Component& component)
{
    component.addKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            addTransportKeyListenerRecursively (*child);
}

void GrooveBrowserWindow::removeTransportKeyListenerRecursively (juce::Component& component)
{
    component.removeKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            removeTransportKeyListenerRecursively (*child);
}

bool GrooveBrowserWindow::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return onTransportKeyPress && onTransportKeyPress (key);
}

void GrooveBrowserWindow::setTargetAvailable (bool available)
{
    if (content != nullptr)
        content->setTargetAvailable (available);
}

void GrooveBrowserWindow::setAppliedGrooveName (const juce::String& name)
{
    if (content != nullptr)
        content->setAppliedName (name);
}

void GrooveBrowserWindow::setStatus (const juce::String& message)
{
    if (content != nullptr)
        content->setStatusText (message);
}

void GrooveBrowserWindow::setAutoPreviewEnabled (bool enabled)
{
    if (content != nullptr)
        content->setAutoPreviewEnabled (enabled);
}

void GrooveBrowserWindow::activeWindowStatusChanged()
{
    juce::DocumentWindow::activeWindowStatusChanged();
}

void GrooveBrowserWindow::closeButtonPressed()
{
    if (onStopPreview)
        onStopPreview();
    setVisible (false);
}
