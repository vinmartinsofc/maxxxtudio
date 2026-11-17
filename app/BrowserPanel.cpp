#include "BrowserPanel.h"
#include "DrumRackProcessor.h"
#include "PluginFolderGrouping.h"
#include <algorithm>

class BrowserPanel::TreeItem : public juce::TreeViewItem
{
public:
    TreeItem (BrowserPanel& p, juce::String label, int itemCount, juce::String key)
        : owner (p), text (std::move (label)), uniqueKey (std::move (key)),
          count (itemCount), isFolder (true) {}

    TreeItem (BrowserPanel& p, const juce::PluginDescription& d)
        : owner (p), description (d), text (d.name) {}

    bool mightContainSubItems() override { return isFolder; }

    juce::String getUniqueName() const override
    {
        return isFolder ? "folder:" + uniqueKey
                        : "plugin:" + description.createIdentifierString();
    }

    juce::String getTooltip() override
    {
        return isFolder ? text + " (" + juce::String (count) + ")"
                        : description.name + " — " + description.manufacturerName
                            + " (" + description.pluginFormatName + ")";
    }

    juce::var getDragSourceDescription() override
    {
        return isFolder ? juce::var()
                        : juce::var ("plugin:" + description.createIdentifierString());
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
            g.fillAll (theme::accent().withAlpha (0.25f));

        g.setColour (theme::text());
        g.setFont (juce::FontOptions (isFolder && getParentItem() == nullptr ? 13.0f : 12.0f,
                                      isFolder ? juce::Font::bold : juce::Font::plain));

        if (isFolder)
        {
            g.drawText (text + " (" + juce::String (count) + ")", 4, 0, width - 8, height,
                        juce::Justification::centredLeft, true);
            return;
        }

        g.drawText (description.name, 4, 0, std::max (0, width - 150), height,
                    juce::Justification::centredLeft, true);
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (description.manufacturerName, std::max (0, width - 140), 0, 84, height,
                    juce::Justification::centredRight, true);
        g.drawText (description.pluginFormatName, std::max (0, width - 48), 0, 42, height,
                    juce::Justification::centredRight, true);

        if (! description.isInstrument)
        {
            g.setColour (juce::Colour (0xff8c7ae6));
            g.fillRoundedRectangle (static_cast<float> (width - 7), 4.0f, 3.0f,
                                    static_cast<float> (height - 8), 1.5f);
        }
    }

    void itemDoubleClicked (const juce::MouseEvent& event) override
    {
        if (isFolder)
            juce::TreeViewItem::itemDoubleClicked (event);
        else
            owner.load (description);
    }

    void itemOpennessChanged (bool) override
    {
        if (isFolder)
            owner.saveTreeState();
    }

    bool isPlugin() const { return ! isFolder; }
    const juce::PluginDescription& plugin() const { return description; }

private:
    BrowserPanel& owner;
    juce::PluginDescription description;
    juce::String text, uniqueKey;
    int count = 0;
    bool isFolder = false;
};

class BrowserPanel::PluginTree : public juce::TreeView
{
public:
    explicit PluginTree (BrowserPanel& p) : owner (p) {}

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey && owner.loadSelectedPlugin())
            return true;

        return juce::TreeView::keyPressed (key);
    }

private:
    BrowserPanel& owner;
};

BrowserPanel::NativeToolCard::NativeToolCard (const juce::String& name,
                                               const juce::String& description,
                                               const juce::String& sourceDescription)
    : juce::TextButton (name), detail (description), dragDescription (sourceDescription)
{
    setMouseClickGrabsKeyboardFocus (false);
    setTooltip ("Clique para adicionar ao canal selecionado ou arraste para um canal MIDI.");
}

void BrowserPanel::NativeToolCard::mouseDown (const juce::MouseEvent& event)
{
    dragStarted = false;
    juce::TextButton::mouseDown (event);
}

void BrowserPanel::NativeToolCard::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragStarted && event.mods.isLeftButtonDown() && event.getDistanceFromDragStart() >= 6)
    {
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            const auto image = juce::ScaledImage (createComponentSnapshot (getLocalBounds()));
            container->startDragging (dragDescription, this, image, false, nullptr, &event.source);
            dragStarted = true;
            setState (juce::Button::buttonNormal);
            return;
        }
    }

    if (! dragStarted)
        juce::TextButton::mouseDrag (event);
}

void BrowserPanel::NativeToolCard::mouseUp (const juce::MouseEvent& event)
{
    if (dragStarted)
        setState (juce::Button::buttonNormal);
    else
        juce::TextButton::mouseUp (event);
    dragStarted = false;
}

void BrowserPanel::NativeToolCard::paintButton (juce::Graphics& graphics,
                                                bool isMouseOverButton,
                                                bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient background (theme::control(), bounds.getX(), bounds.getY(),
                                    theme::panelLight(), bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill (background);
    graphics.fillRoundedRectangle (bounds, 4.0f);

    juce::Path ribbon;
    ribbon.startNewSubPath (bounds.getRight() * 0.62f, bounds.getY());
    ribbon.cubicTo (bounds.getRight() * 0.78f, bounds.getY() + 8.0f,
                    bounds.getRight() * 0.58f, bounds.getBottom() - 8.0f,
                    bounds.getRight() * 0.78f, bounds.getBottom());
    graphics.setColour (theme::accent().withAlpha (isButtonDown ? 0.95f : 0.75f));
    graphics.strokePath (ribbon, juce::PathStrokeType (5.0f));
    graphics.setColour (theme::textDim().withAlpha (0.7f));
    graphics.drawLine (bounds.getRight() * 0.81f, bounds.getY() + 5.0f,
                       bounds.getRight() * 0.98f, bounds.getBottom() - 5.0f, 2.0f);

    if (isMouseOverButton)
    {
        graphics.setColour (juce::Colours::white.withAlpha (0.08f));
        graphics.fillRoundedRectangle (bounds, 4.0f);
    }

    auto textArea = getLocalBounds().reduced (14, 8);
    graphics.setColour (theme::text());
    graphics.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    graphics.drawText (getButtonText(), textArea.removeFromTop (28),
                       juce::Justification::centredLeft, true);
    graphics.setColour (theme::textDim());
    graphics.setFont (juce::FontOptions (11.5f));
    graphics.drawText (detail, textArea, juce::Justification::centredLeft, true);
}

BrowserPanel::BrowserPanel (PluginScanner& s)
    : scanner (s), tree (std::make_unique<PluginTree> (*this)),
      drumRackCard ("Drum Rack", "Arraste para o canal MIDI desejado",
                    "plugin:" + juce::String (DrumRackProcessor::pluginIdentifier))
{
    title.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (title);

    for (auto* tab : { &vstTab, &projectFreedomTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (4710);
        tab->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        tab->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (tab);
    }
    vstTab.setToggleState (true, juce::dontSendNotification);
    vstTab.onClick = [this] { setProjectFreedomTab (false); };
    projectFreedomTab.onClick = [this] { setProjectFreedomTab (true); };

    search.setTextToShowWhenEmpty ("Buscar (nome ou fabricante)", theme::textDim());
    search.onTextChange = [this] { refresh(); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    tree->setColour (juce::TreeView::backgroundColourId, theme::panel());
    tree->setRootItemVisible (true);
    tree->addKeyListener (this);
    addAndMakeVisible (*tree);

    for (auto* b : { &allButton, &instrumentsButton, &effectsButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (b);
    }
    allButton.setToggleState (true, juce::dontSendNotification);
    allButton.onClick         = [this] { setFilter (Filter::All); };
    instrumentsButton.onClick = [this] { setFilter (Filter::Instruments); };
    effectsButton.onClick     = [this] { setFilter (Filter::Effects); };

    status.setFont (juce::FontOptions (12.5f));
    status.setColour (juce::Label::textColourId, theme::textDim());
    addAndMakeVisible (status);

    projectFreedomDescription.setText ("Instrumentos e efeitos nativos.", juce::dontSendNotification);
    projectFreedomDescription.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    projectFreedomDescription.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (projectFreedomDescription);
    drumRackCard.onClick = [this]
    {
        if (onAddDrumRackRequested)
            setStatus (onAddDrumRackRequested());
    };
    addAndMakeVisible (drumRackCard);
    setProjectFreedomTab (false);

    scanner.list().addChangeListener (this);
    refresh();
}

BrowserPanel::~BrowserPanel()
{
    saveTreeState();
    scanner.list().removeChangeListener (this);
}

void BrowserPanel::grabFocus()
{
    if (showingProjectFreedomTools)
        drumRackCard.grabKeyboardFocus();
    else
        tree->grabKeyboardFocus();
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! showingProjectFreedomTools && key.getModifiers().isCommandDown()
        && juce::CharacterFunctions::toLowerCase (key.getKeyCode()) == 'f')
    {
        search.grabKeyboardFocus();
        return true;
    }
    return false;
}

void BrowserPanel::setFilter (Filter f)
{
    filter = f;
    refresh();
}

void BrowserPanel::setProjectFreedomTab (bool showTools)
{
    showingProjectFreedomTools = showTools;
    vstTab.setToggleState (! showTools, juce::dontSendNotification);
    projectFreedomTab.setToggleState (showTools, juce::dontSendNotification);
    title.setText (showTools ? "Ferramentas do Project Freedom" : "Plugins VST",
                   juce::dontSendNotification);
    search.setVisible (! showTools);
    allButton.setVisible (! showTools);
    instrumentsButton.setVisible (! showTools);
    effectsButton.setVisible (! showTools);
    tree->setVisible (! showTools);
    projectFreedomDescription.setVisible (showTools);
    drumRackCard.setVisible (showTools);
    resized();
}

juce::File BrowserPanel::treeStateFile() const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("ProjectFreedom");
    directory.createDirectory();
    return directory.getChildFile ("plugin-tree-state.xml");
}

void BrowserPanel::saveTreeState()
{
    if (searchActive || rebuildingTree || tree == nullptr || tree->getRootItem() == nullptr)
        return;

    if (auto state = tree->getOpennessState (true))
        state->writeTo (treeStateFile());
}

void BrowserPanel::refresh()
{
    const auto query = search.getText().trim();
    const bool searching = query.isNotEmpty();
    const bool wasSearching = searchActive;

    if (searching && ! wasSearching)
        opennessBeforeSearch = tree->getOpennessState (true);
    else if (! searching && ! wasSearching)
        saveTreeState();

    searchActive = searching;
    rebuildingTree = true;

    struct FolderGroup
    {
        juce::String name;
        std::vector<juce::PluginDescription> plugins;
    };

    struct FormatGroup
    {
        juce::String name;
        std::vector<juce::File> roots;
        std::vector<juce::PluginDescription> loosePlugins;
        std::vector<FolderGroup> folders;
    };
    std::vector<FormatGroup> formats;

    for (const auto& description : scanner.list().getTypes())
    {
        if (! scanner.supportsPluginFormat (description.pluginFormatName))
            continue;

        const bool kindMatches = filter == Filter::All
                              || (filter == Filter::Instruments) == description.isInstrument;
        const bool textMatches = query.isEmpty()
                              || description.name.containsIgnoreCase (query)
                              || description.manufacturerName.containsIgnoreCase (query);
        if (! kindMatches || ! textMatches)
            continue;

        const auto formatName = description.pluginFormatName;
        const auto displayFormat = formatName;
        auto format = std::find_if (formats.begin(), formats.end(), [&displayFormat] (const auto& group)
        {
            return group.name == displayFormat;
        });
        if (format == formats.end())
        {
            formats.push_back ({ displayFormat, scanner.searchRoots (formatName), {}, {} });
            format = std::prev (formats.end());
        }

        juce::String folder;
        const roll::PluginPath path { description.name.toStdString(),
                                      description.fileOrIdentifier.toStdString() };
        for (const auto& root : format->roots)
        {
            const auto grouped = roll::groupPluginsByFolder ({ path }, root.getFullPathName().toStdString());
            if (! grouped.empty() && ! grouped.front().folder.empty())
            {
                folder = juce::String::fromUTF8 (grouped.front().folder.c_str());
                break;
            }
        }

        if (folder.isEmpty())
        {
            folder = description.manufacturerName.trim();
            if (folder.isEmpty())
            {
                format->loosePlugins.push_back (description);
                continue;
            }
        }

        auto it = std::find_if (format->folders.begin(), format->folders.end(), [&folder] (const auto& group)
        {
            return group.name.compareIgnoreCase (folder) == 0;
        });
        if (it == format->folders.end())
        {
            format->folders.push_back ({ folder, {} });
            it = std::prev (format->folders.end());
        }
        it->plugins.push_back (description);
    }

    const auto byPluginName = [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    };
    std::sort (formats.begin(), formats.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    int totalPluginCount = 0;
    for (const auto& format : formats)
    {
        totalPluginCount += static_cast<int> (format.loosePlugins.size());
        for (const auto& group : format.folders)
            totalPluginCount += static_cast<int> (group.plugins.size());
    }

    auto root = std::make_unique<TreeItem> (*this, "Plugins", totalPluginCount, "root");
    auto* rootPointer = root.get();
    for (auto& format : formats)
    {
        std::sort (format.loosePlugins.begin(), format.loosePlugins.end(), byPluginName);
        int formatPluginCount = static_cast<int> (format.loosePlugins.size());
        for (auto& group : format.folders)
        {
            std::sort (group.plugins.begin(), group.plugins.end(), byPluginName);
            formatPluginCount += static_cast<int> (group.plugins.size());
        }
        std::sort (format.folders.begin(), format.folders.end(), [] (const auto& a, const auto& b)
        {
            return a.name.compareIgnoreCase (b.name) < 0;
        });

        auto formatItem = std::make_unique<TreeItem> (
            *this, format.name, formatPluginCount, "format:" + format.name);
        auto* formatPointer = formatItem.get();
        for (const auto& description : format.loosePlugins)
            formatItem->addSubItem (new TreeItem (*this, description));

        for (auto& group : format.folders)
        {
            auto folder = std::make_unique<TreeItem> (
                *this, group.name, static_cast<int> (group.plugins.size()),
                "folder:" + format.name + ":" + group.name);
            auto* folderPointer = folder.get();
            for (const auto& description : group.plugins)
                folder->addSubItem (new TreeItem (*this, description));
            formatItem->addSubItem (folder.release());
            if (searching)
                folderPointer->setOpen (true);
        }

        root->addSubItem (formatItem.release());
        formatPointer->setOpen (true);
    }

    tree->setRootItem (root.release());
    tree->setRootItemVisible (true);
    if (searching)
    {
        rootPointer->setOpen (true);
    }
    else if (wasSearching && opennessBeforeSearch != nullptr)
    {
        tree->restoreOpennessState (*opennessBeforeSearch, false);
        opennessBeforeSearch.reset();
    }
    else if (auto savedState = juce::XmlDocument::parse (treeStateFile()))
    {
        tree->restoreOpennessState (*savedState, false);
    }
    else
    {
        rootPointer->setOpen (true);
    }

    rebuildingTree = false;
    tree->repaint();
}

bool BrowserPanel::loadSelectedPlugin()
{
    auto* item = dynamic_cast<TreeItem*> (tree->getSelectedItem (0));
    if (item == nullptr || ! item->isPlugin())
        return false;
    load (item->plugin());
    return true;
}

void BrowserPanel::load (const juce::PluginDescription& description)
{
    if (onLoadRequested)
        onLoadRequested (description);
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    g.setColour (theme::border());
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto head = area.removeFromTop (24);
    title.setBounds (head);
    area.removeFromTop (6);
    auto tabs = area.removeFromTop (26);
    vstTab.setBounds (tabs.removeFromLeft (62));
    tabs.removeFromLeft (4);
    projectFreedomTab.setBounds (tabs.removeFromLeft (144));

    if (showingProjectFreedomTools)
    {
        area.removeFromTop (12);
        projectFreedomDescription.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        drumRackCard.setBounds (area.removeFromTop (78));
    }
    else
    {
        area.removeFromTop (6);
        search.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);
        tabs = area.removeFromTop (24);
        allButton.setBounds (tabs.removeFromLeft (52));
        tabs.removeFromLeft (3);
        instrumentsButton.setBounds (tabs.removeFromLeft (92));
        tabs.removeFromLeft (3);
        effectsButton.setBounds (tabs);
    }

    area.removeFromTop (6);
    status.setBounds (area.removeFromBottom (20));
    tree->setBounds (showingProjectFreedomTools ? juce::Rectangle<int>() : area);
}
