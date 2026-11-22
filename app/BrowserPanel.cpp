#include "BrowserPanel.h"
#include "DrumRackProcessor.h"
#include "FavoriteColorPalette.h"
#include "PluginFolderGrouping.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

class BrowserPanel::TreeItem : public juce::TreeViewItem
{
public:
    TreeItem (BrowserPanel& p, juce::String label, int itemCount, juce::String key,
              int color = -1)
        : owner (p), text (std::move (label)), uniqueKey (std::move (key)),
          count (itemCount), favoriteColor (color), isFolder (true) {}

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
                        : description.name + " - " + description.manufacturerName
                            + " (" + description.pluginFormatName + "); right-click to add to Favorites";
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
            const int textX = favoriteColor >= 0 ? 22 : 4;
            if (favoriteColor >= 0)
            {
                g.setColour (favoriteColorPalette::colors[static_cast<std::size_t> (favoriteColor)]);
                g.fillRoundedRectangle (5.0f, static_cast<float> (height / 2 - 5),
                                        10.0f, 10.0f, 2.0f);
            }
            g.drawText (text + " (" + juce::String (count) + ")", textX, 0,
                        std::max (0, width - textX - 5), height,
                        juce::Justification::centredLeft, true);
            return;
        }

        const int pluginColor = owner.pluginFavoriteColor (description);
        const int nameX = pluginColor >= 0 ? 20 : 4;
        if (pluginColor >= 0)
        {
            g.setColour (favoriteColorPalette::colors[static_cast<std::size_t> (pluginColor)]);
            g.fillRoundedRectangle (5.0f, static_cast<float> (height / 2 - 5),
                                    10.0f, 10.0f, 2.0f);
        }
        g.setColour (theme::text());
        g.drawText (description.name, nameX, 0, std::max (0, width - nameX - 150), height,
                    juce::Justification::centredLeft, true);
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (description.manufacturerName, std::max (0, width - 140), 0, 84, height,
                    juce::Justification::centredRight, true);
        g.drawText (description.pluginFormatName, std::max (0, width - 70), 0, 40, height,
                    juce::Justification::centredRight, true);

        if (! description.isInstrument)
        {
            g.setColour (theme::accentHover());
            g.fillRoundedRectangle (static_cast<float> (width - 25), 4.0f, 3.0f,
                                    static_cast<float> (height - 8), 1.5f);
        }

        juce::Path star;
        const float centreX = static_cast<float> (width - 12);
        const float centreY = static_cast<float> (height) * 0.5f;
        for (int point = 0; point < 10; ++point)
        {
            const float angle = -juce::MathConstants<float>::halfPi
                              + static_cast<float> (point) * juce::MathConstants<float>::pi / 5.0f;
            const float radius = point % 2 == 0 ? 5.5f : 2.5f;
            const float x = centreX + std::cos (angle) * radius;
            const float starY = centreY + std::sin (angle) * radius;
            if (point == 0)
                star.startNewSubPath (x, starY);
            else
                star.lineTo (x, starY);
        }
        star.closeSubPath();
        if (pluginColor >= 0)
        {
            g.setColour (favoriteColorPalette::colors[static_cast<std::size_t> (pluginColor)]);
            g.fillPath (star);
        }
        else
        {
            g.setColour (theme::textDim());
            g.strokePath (star, juce::PathStrokeType (1.2f));
        }
    }

    void itemDoubleClicked (const juce::MouseEvent& event) override
    {
        if (isFolder)
            juce::TreeViewItem::itemDoubleClicked (event);
        else
            owner.load (description);
    }

    void itemClicked (const juce::MouseEvent& event) override
    {
        if (isFolder)
            return;
        if (event.mods.isPopupMenu())
        {
            owner.showPluginMenu (description, event.getScreenPosition());
            return;
        }

        if (event.x >= getItemPosition (false).getWidth() - 24)
            owner.togglePluginFavorite (description);
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
    int count = 0, favoriteColor = -1;
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
    setTooltip ("Click to add to the selected channel or drag to a MIDI channel.");
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
        graphics.setColour (theme::isLight()
                                ? juce::Colours::black.withAlpha (0.055f)
                                : juce::Colours::white.withAlpha (0.08f));
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
      drumRackCard ("Drum Master", "Drag to the desired MIDI channel",
                    "plugin:" + juce::String (DrumRackProcessor::pluginIdentifier)),
      utilityCard ("Volume Master", theme::ui ("Volume, stereo, mono, phase, and modulation"),
                   "plugin:" + juce::String ("MaXXXtudio.Utility")),
      compMasterCard ("Comp Master", "Compressor, oscilloscope, and stereo meters",
                      "plugin:MaXXXtudio.CompMaster"),
      eqMasterCard ("EQ Master", "Eight-band parametric equalizer",
                    "plugin:MaXXXtudio.EqMaster"),
      referenceCard ("Reference Master",
                     "Reference deck: BPM, key, waveform, and spectrum",
                     "tool:reference-master")
{
    juce::PropertiesFile::Options options;
    options.applicationName = "MaXXXtudioPluginFavorites";
    options.filenameSuffix = ".settings";
    options.folderName = "MaXXXtudio";
    options.osxLibrarySubFolder = "Application Support";
    pluginFavoriteProperties.setStorageParameters (options);
    pluginFavoriteSettings = pluginFavoriteProperties.getUserSettings();
    loadPluginFavorites();

    title.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text());
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    for (auto* tab : { &vstTab, &maXXXtudioTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (4710);
        tab->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        tab->setColour (juce::TextButton::textColourOnId, theme::text());
        tab->setColour (juce::TextButton::buttonColourId, theme::control());
        tab->setColour (juce::TextButton::textColourOffId, theme::text());
        addAndMakeVisible (tab);
    }
    vstTab.setToggleState (true, juce::dontSendNotification);
    vstTab.onClick = [this] { setMaXXXtudioTab (false); };
    maXXXtudioTab.onClick = [this] { setMaXXXtudioTab (true); };

    search.setTextToShowWhenEmpty ("Search (name or vendor)", theme::textDim());
    search.setColour (juce::TextEditor::backgroundColourId, theme::control());
    search.setColour (juce::TextEditor::textColourId, theme::text());
    search.setColour (juce::TextEditor::outlineColourId, theme::border());
    search.onTextChange = [this] { refresh(); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    tree->setColour (juce::TreeView::backgroundColourId, theme::panel());
    tree->setRootItemVisible (true);
    tree->addKeyListener (this);
    addAndMakeVisible (*tree);

    for (auto* b : { &allButton, &instrumentsButton, &effectsButton, &favoritesButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, theme::text());
        b->setColour (juce::TextButton::buttonColourId, theme::control());
        b->setColour (juce::TextButton::textColourOffId, theme::text());
        addAndMakeVisible (b);
    }
    allButton.setToggleState (true, juce::dontSendNotification);
    allButton.setTooltip ("All Plugins");
    instrumentsButton.setTooltip ("Instruments");
    effectsButton.setTooltip ("Effects");
    favoritesButton.setTooltip (
        "Ctrl-click or Shift-click to select multiple favorites; "
        "right-click to change the color or remove");
    allButton.onClick         = [this] { setFilter (Filter::All); };
    instrumentsButton.onClick = [this] { setFilter (Filter::Instruments); };
    effectsButton.onClick     = [this] { setFilter (Filter::Effects); };
    favoritesButton.onClick   = [this] { setFilter (Filter::Favorites); };

    status.setFont (juce::FontOptions (12.5f));
    status.setColour (juce::Label::textColourId, theme::textDim());
    addAndMakeVisible (status);

    maXXXtudioDescription.setText ("Native instruments and effects.", juce::dontSendNotification);
    maXXXtudioDescription.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    maXXXtudioDescription.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (maXXXtudioDescription);
    drumRackCard.onClick = [this]
    {
        if (onAddDrumRackRequested)
            setStatus (onAddDrumRackRequested());
    };
    utilityCard.onClick = [this]
    {
        if (onAddUtilityRequested)
            setStatus (onAddUtilityRequested());
    };
    compMasterCard.onClick = [this]
    {
        if (onAddCompMasterRequested)
            setStatus (onAddCompMasterRequested());
    };
    eqMasterCard.onClick = [this]
    {
        if (onAddEqMasterRequested)
            setStatus (onAddEqMasterRequested());
    };
    referenceCard.onClick = [this]
    {
        if (onOpenReferenceRequested)
            setStatus (onOpenReferenceRequested());
    };
    addAndMakeVisible (drumRackCard);
    addAndMakeVisible (utilityCard);
    addAndMakeVisible (compMasterCard);
    addAndMakeVisible (eqMasterCard);
    addAndMakeVisible (referenceCard);
    setMaXXXtudioTab (false);

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
    if (showingMaXXXtudioTools)
        drumRackCard.grabKeyboardFocus();
    else
        tree->grabKeyboardFocus();
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! showingMaXXXtudioTools && key.getModifiers().isCommandDown()
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
    tree->setMultiSelectEnabled (f == Filter::Favorites);
    if (f != Filter::Favorites)
        tree->clearSelectedItems();
    refresh();
}

void BrowserPanel::setMaXXXtudioTab (bool showTools)
{
    showingMaXXXtudioTools = showTools;
    vstTab.setToggleState (! showTools, juce::dontSendNotification);
    maXXXtudioTab.setToggleState (showTools, juce::dontSendNotification);
    title.setText (showTools ? "MaXXXtudio Tools" : "VST Plugins",
                   juce::dontSendNotification);
    search.setVisible (! showTools);
    allButton.setVisible (! showTools);
    instrumentsButton.setVisible (! showTools);
    effectsButton.setVisible (! showTools);
    favoritesButton.setVisible (! showTools);
    tree->setVisible (! showTools);
    maXXXtudioDescription.setVisible (showTools);
    drumRackCard.setVisible (showTools);
    utilityCard.setVisible (showTools);
    compMasterCard.setVisible (showTools);
    eqMasterCard.setVisible (showTools);
    referenceCard.setVisible (showTools);
    resized();
}

juce::File BrowserPanel::treeStateFile (Filter stateFilter) const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("MaXXXtudio");
    directory.createDirectory();
    juce::String filename = "plugin-tree-state";
    switch (stateFilter)
    {
        case Filter::All:         break;
        case Filter::Instruments: filename += "-instruments"; break;
        case Filter::Effects:     filename += "-effects"; break;
        case Filter::Favorites:   filename += "-favorites"; break;
    }
    return directory.getChildFile (filename + ".xml");
}

void BrowserPanel::saveTreeState()
{
    if (searchActive || rebuildingTree || tree == nullptr || tree->getRootItem() == nullptr)
        return;

    if (auto state = tree->getOpennessState (true))
        state->writeTo (treeStateFile (renderedFilter));
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
    std::array<std::vector<juce::PluginDescription>, favoriteColorPalette::colors.size()> favoritesByColor;

    for (const auto& description : scanner.list().getTypes())
    {
        if (! scanner.supportsPluginFormat (description.pluginFormatName))
            continue;

        const bool kindMatches = filter == Filter::All
                              || (filter == Filter::Instruments && description.isInstrument)
                              || (filter == Filter::Effects && ! description.isInstrument)
                              || (filter == Filter::Favorites && isPluginFavorite (description));
        const bool textMatches = query.isEmpty()
                              || description.name.containsIgnoreCase (query)
                              || description.manufacturerName.containsIgnoreCase (query);
        if (! kindMatches || ! textMatches)
            continue;

        if (filter == Filter::Favorites)
        {
            const int color = pluginFavoriteColor (description);
            if (color >= 0)
                favoritesByColor[static_cast<std::size_t> (color)].push_back (description);
            continue;
        }

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
    for (auto& favorites : favoritesByColor)
        std::sort (favorites.begin(), favorites.end(), byPluginName);

    const bool showingFavorites = filter == Filter::Favorites;
    int totalPluginCount = 0;
    if (showingFavorites)
    {
        for (const auto& favorites : favoritesByColor)
            totalPluginCount += static_cast<int> (favorites.size());
    }
    else
    {
        for (const auto& format : formats)
        {
            totalPluginCount += static_cast<int> (format.loosePlugins.size());
            for (const auto& group : format.folders)
                totalPluginCount += static_cast<int> (group.plugins.size());
        }
    }

    auto root = std::make_unique<TreeItem> (
        *this, showingFavorites ? "Favorites" : "Plugins", totalPluginCount,
        showingFavorites ? "favorites" : "root");
    auto* rootPointer = root.get();
    if (showingFavorites)
    {
        for (std::size_t color = 0; color < favoritesByColor.size(); ++color)
        {
            const auto& favorites = favoritesByColor[color];
            auto category = std::make_unique<TreeItem> (
                *this, favoriteColorPalette::names[color], static_cast<int> (favorites.size()),
                "favorite-color-" + juce::String (static_cast<int> (color)),
                static_cast<int> (color));
            auto* categoryPointer = category.get();
            for (const auto& description : favorites)
                category->addSubItem (new TreeItem (*this, description));
            root->addSubItem (category.release());
            if (searching)
                categoryPointer->setOpen (true);
        }
    }
    else
    {
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
    else if (auto savedState = juce::XmlDocument::parse (treeStateFile (filter)))
    {
        tree->restoreOpennessState (*savedState, false);
    }
    else
    {
        rootPointer->setOpen (true);
    }

    if (showingFavorites && favoriteTreeStateAfterRefresh != nullptr)
    {
        tree->restoreOpennessState (*favoriteTreeStateAfterRefresh, true);
        favoriteTreeStateAfterRefresh.reset();
    }
    if (showingFavorites)
    {
        for (const int color : favoriteColorsToOpenAfterRefresh)
            if (auto* category = rootPointer->getSubItem (color))
                category->setOpen (true);
        favoriteColorsToOpenAfterRefresh.clear();
    }

    renderedFilter = filter;
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

void BrowserPanel::showPluginMenu (const juce::PluginDescription& description,
                                   juce::Point<int> screenPosition)
{
    if (filter == Filter::Favorites)
    {
        auto selected = selectedFavoritePlugins();
        if (selected.size() > 1)
        {
            showFavoriteSelectionMenu (selected, screenPosition);
            return;
        }
    }

    const bool favorite = isPluginFavorite (description);
    const int currentColor = pluginFavoriteColor (description);
    juce::PopupMenu colors;
    for (std::size_t color = 0; color < favoriteColorPalette::names.size(); ++color)
        colors.addItem (static_cast<int> (color + 1),
                        theme::ui (favoriteColorPalette::names[color]), true,
                        currentColor == static_cast<int> (color));

    juce::PopupMenu menu;
    menu.addSubMenu (theme::ui (favorite ? "Change Favorite Color" : "Add to Favorites"),
                     colors);
    if (favorite)
        menu.addItem (20, theme::ui ("Remove from Favorites"));

    juce::Component::SafePointer<BrowserPanel> safeThis (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            { screenPosition.x, screenPosition.y, 1, 1 }),
        [safeThis, description] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result >= 1 && result <= static_cast<int> (favoriteColorPalette::names.size()))
                safeThis->setPluginFavorite (description, result - 1);
            else if (result == 20)
                safeThis->removePluginFavorite (description);
        });
}

void BrowserPanel::showFavoriteSelectionMenu (
    const std::vector<juce::PluginDescription>& descriptions,
    juce::Point<int> screenPosition)
{
    const auto count = juce::String (static_cast<int> (descriptions.size()));
    juce::PopupMenu colors;
    for (std::size_t color = 0; color < favoriteColorPalette::names.size(); ++color)
        colors.addItem (static_cast<int> (color + 1),
                        theme::ui (favoriteColorPalette::names[color]));

    juce::PopupMenu menu;
    menu.addSubMenu (theme::ui ("Change color of ") + count + theme::ui (" favorites"), colors);
    menu.addItem (20, theme::ui ("Remove ") + count + theme::ui (" favorites"));

    juce::Component::SafePointer<BrowserPanel> safeThis (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            { screenPosition.x, screenPosition.y, 1, 1 }),
        [safeThis, descriptions] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result >= 1 && result <= static_cast<int> (favoriteColorPalette::names.size()))
                safeThis->setPluginFavorites (descriptions, result - 1);
            else if (result == 20)
                safeThis->removePluginFavorites (descriptions);
        });
}

std::vector<juce::PluginDescription> BrowserPanel::selectedFavoritePlugins() const
{
    std::vector<juce::PluginDescription> selected;
    for (int i = 0; i < tree->getNumSelectedItems(); ++i)
    {
        auto* item = dynamic_cast<TreeItem*> (tree->getSelectedItem (i));
        if (item != nullptr && item->isPlugin() && isPluginFavorite (item->plugin()))
            selected.push_back (item->plugin());
    }
    return selected;
}

bool BrowserPanel::isPluginFavorite (const juce::PluginDescription& description) const
{
    return favoritePluginColors.find (description.createIdentifierString()) != favoritePluginColors.end();
}

int BrowserPanel::pluginFavoriteColor (const juce::PluginDescription& description) const
{
    const auto favorite = favoritePluginColors.find (description.createIdentifierString());
    return favorite != favoritePluginColors.end() ? favorite->second : -1;
}

void BrowserPanel::togglePluginFavorite (const juce::PluginDescription& description)
{
    if (isPluginFavorite (description))
        removePluginFavorite (description);
    else
        setPluginFavorite (description, 4);
}

void BrowserPanel::setPluginFavorite (const juce::PluginDescription& description, int color)
{
    setPluginFavorites ({ description }, color);
}

void BrowserPanel::removePluginFavorite (const juce::PluginDescription& description)
{
    removePluginFavorites ({ description });
}

void BrowserPanel::setPluginFavorites (
    const std::vector<juce::PluginDescription>& descriptions, int color)
{
    if (color < 0 || color >= static_cast<int> (favoriteColorPalette::colors.size()))
        return;

    bool changed = false;
    for (const auto& description : descriptions)
    {
        const auto identifier = description.createIdentifierString();
        const auto current = favoritePluginColors.find (identifier);
        if (current == favoritePluginColors.end() || current->second != color)
        {
            favoritePluginColors[identifier] = color;
            changed = true;
        }
    }
    if (! changed)
        return;

    savePluginFavorites();
    refreshFavoriteTreeAfterChange (color);
}

void BrowserPanel::removePluginFavorites (
    const std::vector<juce::PluginDescription>& descriptions)
{
    bool changed = false;
    for (const auto& description : descriptions)
        changed = favoritePluginColors.erase (description.createIdentifierString()) > 0 || changed;
    if (! changed)
        return;

    savePluginFavorites();
    refreshFavoriteTreeAfterChange();
}

void BrowserPanel::refreshFavoriteTreeAfterChange (int colorToOpen)
{
    if (filter != Filter::Favorites)
    {
        tree->repaint();
        return;
    }

    favoriteTreeStateAfterRefresh = tree->getOpennessState (true);
    if (colorToOpen >= 0)
        favoriteColorsToOpenAfterRefresh.insert (colorToOpen);

    juce::Component::SafePointer<BrowserPanel> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr && safeThis->filter == Filter::Favorites)
            safeThis->refresh();
    });
}

void BrowserPanel::loadPluginFavorites()
{
    favoritePluginColors.clear();
    if (pluginFavoriteSettings == nullptr)
        return;

    const auto xml = juce::parseXML (pluginFavoriteSettings->getValue ("pluginFavorites"));
    if (xml == nullptr || ! xml->hasTagName ("PluginFavorites"))
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto item = root.getChild (i);
        const auto identifier = item.getProperty ("identifier").toString();
        const int color = static_cast<int> (item.getProperty ("color", 4));
        if (identifier.isNotEmpty()
            && color >= 0 && color < static_cast<int> (favoriteColorPalette::colors.size()))
            favoritePluginColors[identifier] = color;
    }
}

void BrowserPanel::savePluginFavorites()
{
    if (pluginFavoriteSettings == nullptr)
        return;

    juce::ValueTree root ("PluginFavorites");
    for (const auto& [identifier, color] : favoritePluginColors)
    {
        juce::ValueTree item ("Plugin");
        item.setProperty ("identifier", identifier, nullptr);
        item.setProperty ("color", color, nullptr);
        root.appendChild (item, nullptr);
    }
    pluginFavoriteSettings->setValue ("pluginFavorites", root.toXmlString());
    pluginFavoriteSettings->saveIfNeeded();
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
    maXXXtudioTab.setBounds (tabs.removeFromLeft (144));

    if (showingMaXXXtudioTools)
    {
        area.removeFromTop (12);
        maXXXtudioDescription.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        drumRackCard.setBounds (area.removeFromTop (68));
        area.removeFromTop (6);
        utilityCard.setBounds (area.removeFromTop (68));
        area.removeFromTop (6);
        compMasterCard.setBounds (area.removeFromTop (68));
        area.removeFromTop (6);
        eqMasterCard.setBounds (area.removeFromTop (68));
        area.removeFromTop (6);
        referenceCard.setBounds (area.removeFromTop (68));
    }
    else
    {
        area.removeFromTop (6);
        search.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);
        tabs = area.removeFromTop (24);
        const bool compactFilters = tabs.getWidth() < 360;
        instrumentsButton.setButtonText (compactFilters ? "Inst." : "Instruments");
        effectsButton.setButtonText (compactFilters ? "FX" : "Effects");
        favoritesButton.setButtonText (compactFilters ? "Fav." : "Favorites");
        const int gap = 3;
        const int buttonWidth = (tabs.getWidth() - gap * 3) / 4;
        allButton.setBounds (tabs.removeFromLeft (buttonWidth));
        tabs.removeFromLeft (gap);
        instrumentsButton.setBounds (tabs.removeFromLeft (buttonWidth));
        tabs.removeFromLeft (gap);
        effectsButton.setBounds (tabs.removeFromLeft (buttonWidth));
        tabs.removeFromLeft (gap);
        favoritesButton.setBounds (tabs);
    }

    area.removeFromTop (6);
    status.setBounds (area.removeFromBottom (20));
    tree->setBounds (showingMaXXXtudioTools ? juce::Rectangle<int>() : area);
}
