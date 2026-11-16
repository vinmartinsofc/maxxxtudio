#include "BrowserPanel.h"
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

BrowserPanel::BrowserPanel (PluginScanner& s) : scanner (s), tree (std::make_unique<PluginTree> (*this))
{
    title.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (title);

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

    rescanButton.onClick = [this]
    {
        setStatus ("Procurando plugins...");
        scanner.startScan();
    };
    addAndMakeVisible (rescanButton);

    foldersButton.setTooltip ("Adicionar ou remover pastas de busca de plugins");
    foldersButton.onClick = [this] { showSearchPathsMenu(); };
    addAndMakeVisible (foldersButton);

    status.setFont (juce::FontOptions (12.5f));
    status.setColour (juce::Label::textColourId, theme::textDim());
    addAndMakeVisible (status);

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
    tree->grabKeyboardFocus();
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key.getModifiers().isCommandDown()
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

juce::File BrowserPanel::treeStateFile() const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("ProjectFreedom");
    directory.createDirectory();
    return directory.getChildFile ("plugin-tree-state.xml");
}

void BrowserPanel::showSearchPathsMenu()
{
    constexpr int addVst3 = 1;
    struct PathAction
    {
        juce::String formatName;
        juce::File directory;
    };

    juce::PopupMenu menu;
    menu.addItem (addVst3, "Adicionar pasta VST3...");

    std::vector<PathAction> removals;
    const auto paths = scanner.customSearchPaths ("VST3");
    if (! paths.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Pastas VST3");
        for (const auto& path : paths)
        {
            const int id = 100 + static_cast<int> (removals.size());
            menu.addItem (id, "Remover " + path.getFullPathName());
            removals.push_back ({ "VST3", path });
        }
    }
    if (removals.empty())
    {
        menu.addSeparator();
        menu.addItem (0, "Nenhuma pasta customizada", false);
    }

    juce::Component::SafePointer<BrowserPanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safeThis, removals = std::move (removals)] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result == addVst3)
            {
                safeThis->chooseSearchPath ("VST3");
                return;
            }
            const auto index = result - 100;
            if (index < 0 || index >= static_cast<int> (removals.size()))
                return;

            const auto& action = removals[static_cast<std::size_t> (index)];
            const auto pathResult = safeThis->scanner.removeCustomSearchPath (action.formatName, action.directory);
            if (pathResult.wasOk())
            {
                safeThis->setStatus ("Pasta removida; atualizando plugins...");
                safeThis->scanner.startScan();
            }
            else
                safeThis->setStatus (pathResult.getErrorMessage());
        });
}

void BrowserPanel::chooseSearchPath (const juce::String& formatName)
{
    if (pathChooser != nullptr)
        return;

    pathChooser = std::make_unique<juce::FileChooser> (
        "Adicionar pasta de plugins " + formatName,
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory));
    juce::Component::SafePointer<BrowserPanel> safeThis (this);
    pathChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis, formatName] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto directory = chooser.getResult();
            safeThis->pathChooser.reset();
            if (directory == juce::File())
                return;

            const auto result = safeThis->scanner.addCustomSearchPath (formatName, directory);
            if (result.failed())
            {
                safeThis->setStatus (result.getErrorMessage());
                return;
            }

            safeThis->setStatus ("Pasta adicionada; atualizando plugins...");
            safeThis->scanner.startScan();
        });
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
    rescanButton.setBounds (head.removeFromRight (64));
    foldersButton.setBounds (head.removeFromRight (58));
    title.setBounds (head);
    area.removeFromTop (6);
    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    auto tabs = area.removeFromTop (24);
    allButton.setBounds (tabs.removeFromLeft (52));
    tabs.removeFromLeft (3);
    instrumentsButton.setBounds (tabs.removeFromLeft (92));
    tabs.removeFromLeft (3);
    effectsButton.setBounds (tabs);
    area.removeFromTop (6);
    status.setBounds (area.removeFromBottom (20));
    tree->setBounds (area);
}
