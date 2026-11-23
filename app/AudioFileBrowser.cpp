#include "AudioFileBrowser.h"
#include "FavoriteColorPalette.h"
#include "AppModalDialog.h"
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

namespace
{
const auto& favoriteColors = favoriteColorPalette::colors;
const auto& favoriteColorNames = favoriteColorPalette::names;
constexpr std::size_t maxSearchResults = 5000;
}

class AudioFileBrowser::FileTreeItem : public juce::TreeViewItem
{
public:
    enum class Kind
    {
        FileSystem, ComputerRoot, FavoritesRoot, FavoriteCategory, FavoriteEntry,
        SearchResultsRoot, SearchFolderEntry, SearchResultEntry
    };

    FileTreeItem (AudioFileBrowser& browser, juce::File path, Kind itemKind = Kind::FileSystem, int color = -1)
        : owner (browser), file (std::move (path)), kind (itemKind), favoriteColor (color) {}

    bool mightContainSubItems() override
    {
        if (kind == Kind::FileSystem || kind == Kind::FavoriteEntry || kind == Kind::SearchResultEntry)
            return file.isDirectory();
        return kind != Kind::SearchResultEntry;
    }

    juce::String getUniqueName() const override
    {
        if (kind == Kind::ComputerRoot) return "computer";
        if (kind == Kind::FavoritesRoot) return "favorites";
        if (kind == Kind::SearchResultsRoot) return "search-results";
        if (kind == Kind::SearchFolderEntry) return "search-folder:" + file.getFullPathName();
        if (kind == Kind::FavoriteCategory) return "favorite-" + juce::String (favoriteColor);
        return file.getFullPathName();
    }

    const juce::File& getFile() const { return file; }
    Kind getKind() const { return kind; }
    bool isFilesystemDirectory() const { return kind == Kind::FileSystem && file.isDirectory(); }
    bool isFileEntry() const
    {
        return kind == Kind::FileSystem || kind == Kind::FavoriteEntry
            || kind == Kind::SearchResultEntry;
    }

    juce::String getTooltip() override
    {
        return kind == Kind::SearchResultEntry || kind == Kind::SearchFolderEntry
            ? file.getFullPathName() : juce::String();
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected)
        {
            const bool isPathEntry = kind == Kind::FileSystem || kind == Kind::FavoriteEntry
                || kind == Kind::SearchFolderEntry || kind == Kind::SearchResultEntry;
            const bool isDirectory = isPathEntry && file.isDirectory();
            const bool shouldSearchAgain = kind == Kind::FileSystem && isDirectory && ! owner.showingFavorites
                && owner.search.getText().trim().isNotEmpty();
            if (isDirectory)
                owner.searchDirectory = file;
            else if (isPathEntry && file.existsAsFile())
                owner.searchDirectory = file.getParentDirectory();
            if (isPathEntry)
                owner.recordNavigationState();
            owner.selectFile (kind == Kind::FileSystem || kind == Kind::FavoriteEntry || kind == Kind::SearchResultEntry
                                  ? file : juce::File());
            if (shouldSearchAgain && owner.searchDirectory.isDirectory())
            {
                juce::Component::SafePointer<AudioFileBrowser> safeOwner (&owner);
                juce::MessageManager::callAsync ([safeOwner]
                {
                    if (safeOwner != nullptr)
                        safeOwner->refreshTree();
                });
            }
        }
    }

    void itemClicked (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()
            && (kind == Kind::FileSystem || kind == Kind::FavoriteEntry || kind == Kind::SearchResultEntry)
            && file.exists())
            owner.showFileMenu (file, event.getScreenPosition());
        else if (! event.mods.isPopupMenu()
                 && (kind == Kind::FileSystem || kind == Kind::FavoriteEntry || kind == Kind::SearchResultEntry))
            owner.previewFile (file);
    }

    juce::var getDragSourceDescription() override
    {
        if ((kind != Kind::FileSystem && kind != Kind::FavoriteEntry && kind != Kind::SearchResultEntry)
            || ! file.exists())
            return {};

        const auto prefix = file.existsAsFile() && owner.isAudioFile (file) ? "audio:" : "browser-item:";
        return prefix + file.getFullPathName();
    }

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        if (details.sourceComponent == nullptr
            || (details.sourceComponent != &owner.tree
                && details.sourceComponent->findParentComponentOfClass<juce::TreeView>() != &owner.tree))
            return false;

        const auto description = details.description.toString();
        const auto prefix = description.startsWith ("audio:") ? juce::String ("audio:")
            : description.startsWith ("browser-item:") ? juce::String ("browser-item:") : juce::String();
        if (prefix.isEmpty())
            return false;

        const juce::File source (description.substring (prefix.length()));
        const auto* target = dynamic_cast<const FileTreeItem*> (owner.tree.getItemAt (details.localPosition.y));
        return source.exists() && target != nullptr
            && (target->kind == Kind::FileSystem || target->kind == Kind::FavoriteEntry
                || target->kind == Kind::SearchFolderEntry || target->kind == Kind::SearchResultEntry)
            && target->file.isDirectory();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details, int) override
    {
        if (const auto* target = dynamic_cast<const FileTreeItem*> (
                owner.tree.getItemAt (details.localPosition.y));
            target != nullptr && target->file.isDirectory())
            owner.moveSelectedItemsToDirectory (target->file);
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
            g.fillAll (theme::accent().withAlpha (0.25f));

        juce::String label;
        bool isBold = false;
        if (kind == Kind::ComputerRoot) { label = "This PC"; isBold = true; }
        else if (kind == Kind::FavoritesRoot) { label = "Favorites"; isBold = true; }
        else if (kind == Kind::FavoriteCategory)
        {
            label = favoriteColorNames[static_cast<std::size_t> (favoriteColor)];
            isBold = true;
        }
        else if (kind == Kind::SearchResultEntry)
            label = file.getFileName();
        else if (kind == Kind::SearchFolderEntry)
        {
            label = file.getFullPathName();
            isBold = true;
        }
        else
        {
            label = file.getFileName();
            isBold = file.isDirectory();
        }

        const int textX = favoriteColor >= 0 ? 22 : 5;
        if (favoriteColor >= 0)
        {
            g.setColour (favoriteColors[static_cast<std::size_t> (favoriteColor)]);
            g.fillRoundedRectangle (5.0f, static_cast<float> (height / 2 - 5), 10.0f, 10.0f, 2.0f);
        }
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (13.5f, isBold ? juce::Font::bold : juce::Font::plain));
        g.drawText (label, textX, 0, width - textX - 5, height, juce::Justification::centredLeft, true);
    }

    void itemOpennessChanged (bool isOpen) override
    {
        owner.treeItemOpennessChanged (getUniqueName(), isOpen);
        if (isOpen && ! owner.restoringTreeExpansion
            && (kind == Kind::FileSystem || kind == Kind::SearchResultEntry) && file.isDirectory())
        {
            owner.searchDirectory = file;
            owner.recordNavigationState();
        }
        if (isOpen && getNumSubItems() == 0)
            owner.populate (*this);
        if (isOpen)
            owner.restoreExpandedChildren (*this);
    }

private:
    AudioFileBrowser& owner;
    juce::File file;
    Kind kind;
    int favoriteColor = -1;
};

bool AudioFileBrowser::FileTreeView::keyPressed (const juce::KeyPress& key)
{
    const bool ctrl = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
    if (ctrl && juce::CharacterFunctions::toLowerCase (key.getKeyCode()) == 'f')
    {
        owner.search.grabKeyboardFocus();
        owner.search.selectAll();
        return true;
    }
    auto* selectedItem = dynamic_cast<FileTreeItem*> (getSelectedItem (0));
    if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
        && selectedItem != nullptr && selectedItem->isFileEntry()
        && selectedItem->getFile().exists())
    {
        owner.moveItemToTrash (selectedItem->getFile());
        return true;
    }

    auto* previouslySelected = getSelectedItem (0);
    const bool handled = juce::TreeView::keyPressed (key);
    if (handled && getSelectedItem (0) != previouslySelected)
        owner.previewFile (owner.selectedFile);
    return handled;
}

void AudioFileBrowser::moveSelectedItemsToDirectory (const juce::File& destination)
{
    if (! destination.isDirectory())
    {
        setStatus ("Select a valid destination folder.");
        return;
    }

    std::vector<juce::File> sources;
    for (int i = 0; i < tree.getNumSelectedItems(); ++i)
        if (const auto* item = dynamic_cast<const FileTreeItem*> (tree.getSelectedItem (i));
            item != nullptr && item->isFileEntry() && item->getFile().exists())
            sources.push_back (item->getFile());

    std::vector<juce::File> uniqueSources;
    for (const auto& source : sources)
    {
        const bool coveredBySelectedDirectory = std::any_of (sources.begin(), sources.end(),
            [&source] (const juce::File& other)
            {
                return source != other && other.isDirectory() && source.isAChildOf (other);
            });
        const bool alreadyIncluded = std::find (uniqueSources.begin(), uniqueSources.end(), source)
            != uniqueSources.end();
        if (! coveredBySelectedDirectory && ! alreadyIncluded)
            uniqueSources.push_back (source);
    }

    if (uniqueSources.empty())
    {
        setStatus ("Drag files or folders to a destination folder.");
        return;
    }

    struct Move
    {
        juce::File source;
        juce::File target;
        bool isDirectory = false;
    };
    std::vector<Move> moves;
    juce::StringArray plannedTargets;
    for (const auto& source : uniqueSources)
    {
        if (source == destination || (source.isDirectory() && destination.isAChildOf (source)))
        {
            setStatus ("A folder cannot be moved into itself.");
            return;
        }

        if (source.getParentDirectory() == destination)
            continue;

        const auto target = destination.getChildFile (source.getFileName());
        if (target.exists() || plannedTargets.contains (target.getFullPathName(), true))
        {
            setStatus ("An item named \" already exists at the destination." + source.getFileName() + "\" at the destination.");
            return;
        }
        if (canDeleteFile && ! canDeleteFile (source))
        {
            setStatus ("This file or folder contains audio currently used in the Arrangement and cannot be moved.");
            return;
        }
        plannedTargets.add (target.getFullPathName());
        moves.push_back ({ source, target, source.isDirectory() });
    }

    if (moves.empty())
    {
        setStatus ("The selected items are already in this folder.");
        return;
    }

    const bool selectedPreviewIsAffected = std::any_of (moves.begin(), moves.end(),
        [this] (const Move& move)
        {
            return selectedFile == move.source
                || (move.isDirectory && selectedFile.existsAsFile()
                    && selectedFile.isAChildOf (move.source));
        });
    if (selectedPreviewIsAffected)
        stopPreview();

    const auto updatePath = [] (const juce::File& path, const juce::File& oldPath,
                                const juce::File& newPath, bool oldPathWasDirectory)
    {
        if (path == oldPath)
            return newPath;
        if (oldPathWasDirectory && path.isAChildOf (oldPath))
            return newPath.getChildFile (path.getRelativePathFrom (oldPath));
        return path;
    };

    juce::StringArray movedNames, failedNames;
    for (const auto& move : moves)
    {
        std::error_code moveError;
        const auto movePath = [&move, &moveError]
        {
            const std::filesystem::path sourcePath (move.source.getFullPathName().toWideCharPointer());
            const std::filesystem::path targetPath (move.target.getFullPathName().toWideCharPointer());
            std::filesystem::rename (sourcePath, targetPath, moveError);
            if (! moveError)
                return true;

            if (moveError != std::errc::cross_device_link)
                return false;

            moveError.clear();
            if (move.isDirectory)
                std::filesystem::copy (sourcePath, targetPath,
                                       std::filesystem::copy_options::recursive, moveError);
            else
                std::filesystem::copy_file (sourcePath, targetPath, moveError);
            if (moveError)
                return false;

            if (move.isDirectory)
                std::filesystem::remove_all (sourcePath, moveError);
            else
                std::filesystem::remove (sourcePath, moveError);
            return ! moveError;
        };

        if (! movePath())
        {
            failedNames.add (move.source.getFileName() + " (" + moveError.message() + ")");
            continue;
        }

        for (auto& favorite : favorites)
            favorite.file = updatePath (favorite.file, move.source, move.target, move.isDirectory);

        const auto updateExpandedPaths = [&updatePath, &move] (std::set<juce::String>& paths)
        {
            std::set<juce::String> updated;
            for (const auto& path : paths)
                updated.insert (updatePath (juce::File (path), move.source, move.target,
                                            move.isDirectory).getFullPathName());
            paths = std::move (updated);
        };
        updateExpandedPaths (expandedFilesystemItems);
        updateExpandedPaths (expandedFavoriteItems);
        searchDirectory = updatePath (searchDirectory, move.source, move.target, move.isDirectory);
        selectedFile = updatePath (selectedFile, move.source, move.target, move.isDirectory);
        movedNames.add (move.source.getFileName());
    }

    saveFavorites();
    saveExpandedItems();
    refreshTree();

    auto message = movedNames.size() == 1
        ? "Moved: " + movedNames[0]
        : "Items moved: " + juce::String (movedNames.size());
    if (! failedNames.isEmpty())
        message << ". Failed to move: " << failedNames.joinIntoString (", ");
    setStatus (message);
}

AudioFileBrowser::AudioFileBrowser (const juce::AudioFormatManager& audioFormats)
{
    for (int i = 0; i < audioFormats.getNumKnownFormats(); ++i)
        supportedAudioExtensions.addArray (audioFormats.getKnownFormat (i)->getFileExtensions());
    supportedAudioExtensions.removeDuplicates (true);

    juce::Desktop::getInstance().addGlobalMouseListener (this);
    juce::PropertiesFile::Options options;
    options.applicationName = "MaXXXtudioAudioFavorites";
    options.filenameSuffix = ".settings";
    options.folderName = "MaXXXtudio";
    options.osxLibrarySubFolder = "Application Support";
    favoriteProperties.setStorageParameters (options);
    favoriteSettings = favoriteProperties.getUserSettings();
    loadFavorites();
    loadExpandedItems();

    title.setFont (theme::fontOptions (14.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text());
    addAndMakeVisible (title);

    tree.setColour (juce::TreeView::backgroundColourId, theme::panel());
    tree.setRootItemVisible (true);
    tree.setRootItem (new FileTreeItem (*this, {}, FileTreeItem::Kind::ComputerRoot));
    if (auto* root = tree.getRootItem())
    {
        restoringTreeExpansion = true;
        root->setOpen (true);
        restoreExpandedItems (*root, expandedFilesystemItems);
        restoringTreeExpansion = false;
    }
    tree.setMultiSelectEnabled (true);
    tree.setIndentSize (10);
    tree.setWantsKeyboardFocus (true);
    addAndMakeVisible (tree);

    status.setFont (theme::fontOptions (12.0f));
    status.setColour (juce::Label::textColourId, theme::textDim());
    addAndMakeVisible (status);

    previewButton.onClick = [this]
    {
        if (selectedFile.existsAsFile() && onPreviewRequested)
        {
            setStatus ("Loading preview...");
            onPreviewRequested (selectedFile);
        }
    };
    stopButton.onClick = [this]
    {
        if (onPreviewStopRequested)
            onPreviewStopRequested();
        setStatus ("Preview stopped");
    };
    autoPreviewButton.setClickingTogglesState (true);
    autoPreviewButton.setToggleState (true, juce::dontSendNotification);
    autoPreviewButton.setTooltip ("Play files automatically when selected");
    previewGain.setSliderStyle (juce::Slider::LinearHorizontal);
    previewGain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewGain.setRange (0.0, 1.0, 0.01);
    previewGain.setValue (0.5, juce::dontSendNotification);
    previewGain.setTooltip ("Preview Volume");
    previewGainLabel.setColour (juce::Label::textColourId, theme::textDim());
    previewGainLabel.setFont (theme::fontOptions (12.5f));
    previewGain.onValueChange = [this]
    {
        if (onPreviewGainChanged)
            onPreviewGainChanged (static_cast<float> (previewGain.getValue()));
    };
    search.setTextToShowWhenEmpty ("Search the selected folder", theme::textDim());
    search.onTextChange = [this]
    {
        searchGeneration->fetch_add (1);
        stopTimer();
        if (search.getText().trim().isEmpty())
        {
            refreshTree();
            recordNavigationState();
        }
        else
        {
            setStatus ("Preparing search...");
            startTimer (250);
        }
    };
    backButton.setTooltip ("Back");
    forwardButton.setTooltip ("Avancar");
    backButton.onClick = [this] { navigateHistory (-1); };
    forwardButton.onClick = [this] { navigateHistory (1); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (forwardButton);
    addAndMakeVisible (search);
    previewButton.setEnabled (false);
    addAndMakeVisible (previewButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (autoPreviewButton);
    addAndMakeVisible (previewGainLabel);
    addAndMakeVisible (previewGain);
    recordNavigationState();
}

AudioFileBrowser::~AudioFileBrowser()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    searchGeneration->fetch_add (1);
}

void AudioFileBrowser::timerCallback()
{
    stopTimer();
    refreshTree();
    recordNavigationState();
}

void AudioFileBrowser::recordNavigationState()
{
    const NavigationState current { searchDirectory, search.getText().trim(), showingFavorites };
    if (navigationHistoryIndex >= 0)
    {
        const auto& previous = navigationHistory[static_cast<std::size_t> (navigationHistoryIndex)];
        if (previous.directory == current.directory && previous.query == current.query
            && previous.favorites == current.favorites)
            return;

        navigationHistory.erase (navigationHistory.begin() + navigationHistoryIndex + 1,
                                 navigationHistory.end());
    }

    navigationHistory.push_back (current);
    navigationHistoryIndex = static_cast<int> (navigationHistory.size()) - 1;
    updateHistoryButtons();
}

void AudioFileBrowser::navigateHistory (int direction)
{
    const auto targetIndex = navigationHistoryIndex + direction;
    if (targetIndex < 0 || targetIndex >= static_cast<int> (navigationHistory.size()))
        return;

    navigationHistoryIndex = targetIndex;
    const auto state = navigationHistory[static_cast<std::size_t> (navigationHistoryIndex)];
    showingFavorites = state.favorites;
    searchDirectory = state.directory;
    title.setText (showingFavorites ? "Favorites" : "Audio Files", juce::dontSendNotification);
    search.setTextToShowWhenEmpty (showingFavorites ? "Search Favorites" : "Search the selected folder",
                                   theme::textDim());
    search.setText (state.query, juce::dontSendNotification);
    searchGeneration->fetch_add (1);
    stopTimer();
    refreshTree();
    updateHistoryButtons();
}

void AudioFileBrowser::updateHistoryButtons()
{
    backButton.setEnabled (navigationHistoryIndex > 0);
    forwardButton.setEnabled (navigationHistoryIndex >= 0
        && navigationHistoryIndex + 1 < static_cast<int> (navigationHistory.size()));
}

void AudioFileBrowser::mouseDown (const juce::MouseEvent& event)
{
    auto* target = event.eventComponent;
    if (target != this && (target == nullptr || ! isParentOf (target)))
        stopPreview();
}

void AudioFileBrowser::stopPreview()
{
    if (onPreviewStopRequested)
        onPreviewStopRequested();
}

void AudioFileBrowser::paint (juce::Graphics& g)
{
    if (theme::isHighTech())
        theme::drawChromeSurface (g, getLocalBounds().toFloat(), 0.0f, theme::panel());
    else
        g.fillAll (theme::panel());
    g.setColour (theme::border());
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));
}

void AudioFileBrowser::resized()
{
    auto area = getLocalBounds().reduced (8);
    title.setBounds (area.removeFromTop (28));
    auto searchArea = area.removeFromTop (26);
    backButton.setBounds (searchArea.removeFromLeft (24));
    searchArea.removeFromLeft (2);
    forwardButton.setBounds (searchArea.removeFromLeft (24));
    searchArea.removeFromLeft (2);
    search.setBounds (searchArea);
    auto controls = area.removeFromTop (28);
    autoPreviewButton.setBounds (controls.removeFromLeft (56));
    controls.removeFromLeft (2);
    previewButton.setBounds (controls.removeFromLeft (58));
    controls.removeFromLeft (2);
    stopButton.setBounds (controls.removeFromLeft (56));
    auto volume = area.removeFromTop (22);
    previewGainLabel.setBounds (volume.removeFromLeft (48));
    previewGain.setBounds (volume);
    area.removeFromTop (4);
    tree.setBounds (area.removeFromTop (std::max (0, area.getHeight() - 30)));
    status.setBounds (area);
}

void AudioFileBrowser::setFavoritesMode (bool showing)
{
    if (showingFavorites == showing)
        return;

    showingFavorites = showing;
    selectFile ({});
    title.setText (showingFavorites ? "Favorites" : "Audio Files", juce::dontSendNotification);
    search.setTextToShowWhenEmpty (showingFavorites ? "Search Favorites" : "Search the selected folder",
                                   theme::textDim());
    refreshTree();
    recordNavigationState();
}

void AudioFileBrowser::revealFile (const juce::File& file)
{
    if (! file.existsAsFile() || ! isAudioFile (file))
    {
        setStatus ("Audio file not found.");
        return;
    }

    if (showingFavorites)
        setFavoritesMode (false);

    searchDirectory = file.getParentDirectory();
    search.setText (file.getFileName(), juce::dontSendNotification);
    searchGeneration->fetch_add (1);
    searchResults = { file };
    tree.setRootItem (new FileTreeItem (*this, {}, FileTreeItem::Kind::SearchResultsRoot));
    if (auto* root = tree.getRootItem())
    {
        root->setOpen (true);
        if (root->getNumSubItems() > 0)
            if (auto* folder = root->getSubItem (0))
            {
                folder->setOpen (true);
                if (folder->getNumSubItems() > 0)
                    if (auto* item = folder->getSubItem (0))
                    {
                        item->setSelected (true, true);
                        tree.scrollToKeepItemVisible (item);
                    }
            }
    }
    setStatus ("Located: " + file.getFileName());
    recordNavigationState();
}

bool AudioFileBrowser::isAudioFile (const juce::File& file) const
{
    return supportedAudioExtensions.contains (file.getFileExtension(), true);
}

void AudioFileBrowser::selectFile (const juce::File& file)
{
    selectedFile = file.existsAsFile() && isAudioFile (file) ? file : juce::File();
    previewButton.setEnabled (selectedFile.existsAsFile());
    if (onPreviewStopRequested)
        stopPreview();
    setStatus (selectedFile.existsAsFile()
                   ? "Selected: " + selectedFile.getFileName()
                   : "Select an audio file to preview");
}

void AudioFileBrowser::refreshTree()
{
    stopTimer();
    const auto query = search.getText().trim();
    const auto generation = searchGeneration->fetch_add (1) + 1;
    searchResults.clear();

    if (query.isEmpty())
    {
        tree.setRootItem (new FileTreeItem (*this, {}, showingFavorites
            ? FileTreeItem::Kind::FavoritesRoot : FileTreeItem::Kind::ComputerRoot));
        if (tree.getRootItem() != nullptr)
        {
            restoringTreeExpansion = true;
            tree.getRootItem()->setOpen (true);
            restoreExpandedItems (*tree.getRootItem(),
                                  showingFavorites ? expandedFavoriteItems : expandedFilesystemItems);
            restoringTreeExpansion = false;
        }
        setStatus ("Select an audio file to preview");
        return;
    }

    if (! showingFavorites && ! searchDirectory.isDirectory())
    {
        juce::File deepestOpenDirectory;
        int deepestPathDepth = -1;
        std::function<void (juce::TreeViewItem&)> findOpenDirectory = [&] (juce::TreeViewItem& item)
        {
            if (auto* fileItem = dynamic_cast<FileTreeItem*> (&item);
                fileItem != nullptr && fileItem->isFilesystemDirectory() && fileItem->isOpen())
            {
                auto parent = fileItem->getFile();
                int pathDepth = 0;
                while (parent.getParentDirectory() != parent)
                {
                    ++pathDepth;
                    parent = parent.getParentDirectory();
                }
                if (pathDepth > deepestPathDepth)
                {
                    deepestPathDepth = pathDepth;
                    deepestOpenDirectory = fileItem->getFile();
                }
            }

            for (int i = 0; i < item.getNumSubItems(); ++i)
                if (auto* child = item.getSubItem (i))
                    findOpenDirectory (*child);
        };
        if (auto* root = tree.getRootItem())
            findOpenDirectory (*root);
        searchDirectory = deepestOpenDirectory;
    }

    std::vector<juce::File> directories, directMatches;
    if (showingFavorites)
    {
        std::set<juce::String> seenDirectories;
        for (const auto& favorite : favorites)
        {
            if (favorite.file.isDirectory())
            {
                const auto path = favorite.file.getFullPathName();
                if (seenDirectories.insert (path).second)
                    directories.push_back (favorite.file);
            }
            else if (favorite.file.existsAsFile() && favorite.file.getFileName().containsIgnoreCase (query))
                directMatches.push_back (favorite.file);
        }
    }
    else if (searchDirectory.isDirectory())
        directories.push_back (searchDirectory);
    else if (juce::File ("C:\\").isDirectory())
        directories.push_back (juce::File ("C:\\"));

    tree.setRootItem (new FileTreeItem (*this, {}, FileTreeItem::Kind::SearchResultsRoot));
    tree.getRootItem()->setOpen (true);
    setStatus (showingFavorites ? "Searching Favorites..."
        : searchDirectory.isDirectory()
            ? "Searching in " + searchDirectory.getFullPathName() + "..."
            : "Searching drive C...");

    const auto generationState = searchGeneration;
    juce::Component::SafePointer<AudioFileBrowser> safeThis (this);
    searchPool.addJob ([safeThis, directories = std::move (directories),
                        directMatches = std::move (directMatches), query, generation, generationState]
    {
        std::vector<juce::File> results;
        std::set<juce::String> seenPaths;
        bool truncated = false;

        const auto addResult = [&] (const juce::File& file)
        {
            const auto path = file.getFullPathName();
            if (seenPaths.count (path) != 0)
                return true;
            if (results.size() >= maxSearchResults)
            {
                truncated = true;
                return false;
            }
            seenPaths.insert (path);
            results.push_back (file);
            return true;
        };

        for (const auto& file : directMatches)
            if (! addResult (file))
                break;

        for (const auto& directory : directories)
        {
            if (truncated)
                break;
            for (const auto& entry : juce::RangedDirectoryIterator (
                     directory, true, "*", juce::File::findFilesAndDirectories,
                     juce::File::FollowSymlinks::no))
            {
                if (generationState->load() != generation)
                    return;

                const auto& file = entry.getFile();
                if (file.getFileName().containsIgnoreCase (query)
                    && ! addResult (file))
                    break;
            }
        }

        if (generationState->load() != generation)
            return;
        std::sort (results.begin(), results.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0;
        });
        juce::MessageManager::callAsync ([safeThis, generation, generationState,
                                           results = std::move (results), truncated]() mutable
        {
            if (safeThis == nullptr || generationState->load() != generation)
                return;
            safeThis->searchResults = std::move (results);
            safeThis->tree.setRootItem (new FileTreeItem (
                *safeThis, {}, FileTreeItem::Kind::SearchResultsRoot));
            if (auto* root = safeThis->tree.getRootItem())
            {
                root->setOpen (true);
                for (int i = 0; i < root->getNumSubItems(); ++i)
                    if (auto* folder = root->getSubItem (i))
                        folder->setOpen (true);
            }
            const auto count = safeThis->searchResults.size();
            safeThis->setStatus (truncated
                ? "More than " + juce::String (count) + " results; display limit reached."
                : juce::String (count) + " item(s) found");
        });
    });
}

void AudioFileBrowser::previewFile (const juce::File& file)
{
    if (autoPreviewButton.getToggleState() && file.existsAsFile() && isAudioFile (file))
    {
        selectedFile = file;
        previewButton.setEnabled (true);
        setStatus ("Loading preview...");
        if (onPreviewRequested)
            onPreviewRequested (file);
    }
}

void AudioFileBrowser::showFileMenu (const juce::File& file, juce::Point<int> screenPosition)
{
    const bool canBeFavorite = file.isDirectory() || isAudioFile (file);
    const auto existing = std::find_if (favorites.begin(), favorites.end(),
        [&file] (const Favorite& favorite) { return favorite.file == file; });

    juce::PopupMenu colors;
    for (int color = 0; color < static_cast<int> (favoriteColorNames.size()); ++color)
        colors.addItem (color + 1, favoriteColorNames[static_cast<std::size_t> (color)]);

    juce::PopupMenu menu;
    if (canBeFavorite)
        menu.addSubMenu ("Add to Favorites", colors);
    if (canBeFavorite && existing != favorites.end())
        menu.addItem (20, "Remove from Favorites");
    if (file.isDirectory())
    {
        menu.addItem (21, "Create New Folder Here...");
        if (file.getParentDirectory() != file)
            menu.addItem (23, "Move Folder and Contents to Recycle Bin...");
    }
    else if (file.existsAsFile())
        menu.addItem (22, "Move File to Recycle Bin...");
    if (file.exists())
        menu.addItem (24, "Show in File Explorer");

    juce::Component::SafePointer<AudioFileBrowser> safeThis (this);
    const juce::PopupMenu::Options options;
    menu.showMenuAsync (options.withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [safeThis, file] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result >= 1 && result <= 7)
                safeThis->setFavorite (file, result - 1);
            else if (result == 20)
                safeThis->removeFavorite (file);
            else if (result == 21 && file.isDirectory())
                safeThis->createFolderInDirectory (file);
            else if (result == 22 && file.existsAsFile())
                safeThis->moveItemToTrash (file);
            else if (result == 23 && file.isDirectory())
                safeThis->moveItemToTrash (file);
            else if (result == 24)
                safeThis->locateInExplorer (file);
        });
}

void AudioFileBrowser::locateInExplorer (const juce::File& file)
{
#if defined(_WIN32)
    const juce::String path = file.existsAsFile()
        ? file.getFullPathName()
        : file.isDirectory() ? file.getFullPathName()
                             : juce::String();
    if (path.isEmpty())
        return;

    const juce::String command = file.existsAsFile()
        ? juce::String ("/select,\"") + path + juce::String ("\"")
        : juce::String ("/e,\"") + path + juce::String ("\"");
    ShellExecuteW (nullptr, L"open", L"explorer.exe",
                   command.toWideCharPointer(),
                   nullptr, SW_SHOWNORMAL);
#else
    juce::ignoreUnused (file);
#endif
}

void AudioFileBrowser::createFolderInDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
    {
        setStatus ("Select a valid folder in which to create the new folder.");
        return;
    }

    juce::Component::SafePointer<AudioFileBrowser> safeThis (this);
    AppModalDialog::showInput (
        *this, "Create Folder", "Enter the new folder name:", {}, "Name",
        { { "Create", 1, AppModalDialog::ButtonStyle::Primary },
          { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
        [safeThis, directory] (int result, juce::String input)
        {
            if (safeThis == nullptr || result != 1)
                return;

            const auto name = input.trim();
            if (name.isEmpty() || name == "." || name == ".."
                || name.containsChar ('/') || name.containsChar ('\\'))
            {
                safeThis->setStatus ("Enter a valid folder name.");
                return;
            }

            const auto newDirectory = directory.getChildFile (name);
            if (newDirectory.exists())
            {
                safeThis->setStatus ("A file or folder with that name already exists.");
                return;
            }

            if (const auto error = newDirectory.createDirectory(); error.failed())
            {
                safeThis->setStatus ("Failed to create folder: " + error.getErrorMessage());
                return;
            }

            safeThis->refreshTree();
            safeThis->setStatus ("Folder created: " + name);
        });
}

void AudioFileBrowser::moveItemToTrash (const juce::File& file)
{
    if (! file.exists())
    {
        setStatus ("The file or folder no longer exists.");
        return;
    }
    if (file.isDirectory() && file.getParentDirectory() == file)
    {
        setStatus ("A drive root cannot be removed.");
        return;
    }
    if (canDeleteFile && ! canDeleteFile (file))
    {
        setStatus ("This file or folder contains audio currently used in the Arrangement and cannot be removed.");
        return;
    }

    const bool isDirectory = file.isDirectory();
    const auto itemDescription = isDirectory
        ? "The folder \"" + file.getFileName() + "\" and all its contents will be moved to the Recycle Bin."
        : "The file \"" + file.getFileName() + "\" will be moved to the Recycle Bin.";
    juce::Component::SafePointer<AudioFileBrowser> safeThis (this);
    AppModalDialog::show (
        *this, "Move to Recycle Bin?", itemDescription, AppModalDialog::Icon::Warning,
        { { "Move to Recycle Bin", 1, AppModalDialog::ButtonStyle::Primary },
          { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
        [safeThis, file] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            if (! file.exists())
            {
                safeThis->setStatus ("The file or folder no longer exists.");
                return;
            }
            if (file.isDirectory() && file.getParentDirectory() == file)
            {
                safeThis->setStatus ("A drive root cannot be removed.");
                return;
            }
            if (safeThis->canDeleteFile && ! safeThis->canDeleteFile (file))
            {
                safeThis->setStatus ("This file or folder contains audio currently used in the Arrangement and cannot be removed.");
                return;
            }
            const auto selectedFileIsAffected = safeThis->selectedFile == file
                || (file.isDirectory() && safeThis->selectedFile.existsAsFile()
                    && safeThis->selectedFile.isAChildOf (file));
            if (selectedFileIsAffected && safeThis->onPreviewStopRequested)
                safeThis->onPreviewStopRequested();
            if (! file.moveToTrash())
            {
                safeThis->setStatus ("Failed to move the file or folder to the Recycle Bin.");
                return;
            }
            safeThis->favorites.erase (std::remove_if (safeThis->favorites.begin(), safeThis->favorites.end(),
                [&file] (const Favorite& favorite)
                {
                    return favorite.file == file
                        || (file.isDirectory() && favorite.file.isAChildOf (file));
                }), safeThis->favorites.end());
            safeThis->saveFavorites();
            safeThis->selectFile ({});
            safeThis->refreshTree();
            safeThis->setStatus (file.getFileName() + " moved to the Recycle Bin.");
        });
}

void AudioFileBrowser::setFavorite (const juce::File& file, int color)
{
    if (! file.exists() || (! file.isDirectory() && ! isAudioFile (file))
        || color < 0 || color >= static_cast<int> (favoriteColors.size()))
        return;

    const auto existing = std::find_if (favorites.begin(), favorites.end(),
        [&file] (const Favorite& favorite) { return favorite.file == file; });
    if (existing == favorites.end())
        favorites.push_back ({ file, color });
    else
        existing->color = color;

    saveFavorites();
    if (showingFavorites)
    {
        setFavoritesMode (false);
        setFavoritesMode (true);
    }
}

void AudioFileBrowser::removeFavorite (const juce::File& file)
{
    favorites.erase (std::remove_if (favorites.begin(), favorites.end(),
        [&file] (const Favorite& favorite) { return favorite.file == file; }), favorites.end());
    saveFavorites();
    if (showingFavorites)
    {
        setFavoritesMode (false);
        setFavoritesMode (true);
    }
}

void AudioFileBrowser::loadFavorites()
{
    if (favoriteSettings == nullptr)
        return;

    const auto xml = juce::parseXML (favoriteSettings->getValue ("audioFavorites"));
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto item = root.getChild (i);
        const juce::File file (item.getProperty ("path").toString());
        const int color = static_cast<int> (item.getProperty ("color", 0));
        if (file.exists() && (file.isDirectory() || isAudioFile (file))
            && color >= 0 && color < static_cast<int> (favoriteColors.size()))
            favorites.push_back ({ file, color });
    }
}

void AudioFileBrowser::saveFavorites()
{
    if (favoriteSettings == nullptr)
        return;

    juce::ValueTree root ("Favorites");
    for (const auto& favorite : favorites)
    {
        juce::ValueTree item ("Item");
        item.setProperty ("path", favorite.file.getFullPathName(), nullptr);
        item.setProperty ("color", favorite.color, nullptr);
        root.appendChild (item, nullptr);
    }
    favoriteSettings->setValue ("audioFavorites", root.toXmlString());
    favoriteSettings->saveIfNeeded();
}

void AudioFileBrowser::loadExpandedItems()
{
    if (favoriteSettings == nullptr)
        return;

    const auto load = [this] (const juce::String& key, std::set<juce::String>& paths)
    {
        const auto xml = juce::parseXML (favoriteSettings->getValue (key));
        if (xml == nullptr)
            return;
        const auto root = juce::ValueTree::fromXml (*xml);
        for (int i = 0; i < root.getNumChildren(); ++i)
        {
            const auto path = root.getChild (i).getProperty ("name").toString();
            if (path.isNotEmpty())
                paths.insert (path);
        }
    };
    load ("audioBrowserExpandedFilesystem", expandedFilesystemItems);
    load ("audioBrowserExpandedFavorites", expandedFavoriteItems);
}

void AudioFileBrowser::saveExpandedItems()
{
    if (favoriteSettings == nullptr)
        return;

    const auto save = [this] (const juce::String& key, const std::set<juce::String>& paths)
    {
        juce::ValueTree root ("ExpandedItems");
        for (const auto& path : paths)
        {
            juce::ValueTree item ("Item");
            item.setProperty ("name", path, nullptr);
            root.appendChild (item, nullptr);
        }
        favoriteSettings->setValue (key, root.toXmlString());
    };
    save ("audioBrowserExpandedFilesystem", expandedFilesystemItems);
    save ("audioBrowserExpandedFavorites", expandedFavoriteItems);
    favoriteSettings->saveIfNeeded();
}

void AudioFileBrowser::restoreExpandedItems (juce::TreeViewItem& item,
                                              const std::set<juce::String>& paths)
{
    if (paths.count (item.getUniqueName()) != 0 && ! item.isOpen())
        item.setOpen (true);
    for (int i = 0; i < item.getNumSubItems(); ++i)
        if (auto* child = item.getSubItem (i))
            restoreExpandedItems (*child, paths);
}

void AudioFileBrowser::restoreExpandedChildren (juce::TreeViewItem& item)
{
    if (tree.getRootItem() == nullptr)
        return;
    const auto rootName = tree.getRootItem()->getUniqueName();
    if (rootName != "computer" && rootName != "favorites")
        return;
    const bool wasRestoring = restoringTreeExpansion;
    restoringTreeExpansion = true;
    restoreExpandedItems (item, rootName == "favorites"
        ? expandedFavoriteItems : expandedFilesystemItems);
    restoringTreeExpansion = wasRestoring;
}

void AudioFileBrowser::treeItemOpennessChanged (const juce::String& name, bool isOpen)
{
    if (restoringTreeExpansion || tree.getRootItem() == nullptr)
        return;
    const auto rootName = tree.getRootItem()->getUniqueName();
    if (rootName != "computer" && rootName != "favorites")
        return;

    auto& paths = rootName == "favorites" ? expandedFavoriteItems : expandedFilesystemItems;
    if (isOpen)
        paths.insert (name);
    else
        paths.erase (name);
    saveExpandedItems();
}

void AudioFileBrowser::populate (FileTreeItem& item)
{
    if (item.getUniqueName() == "favorites")
    {
        for (int color = 0; color < static_cast<int> (favoriteColorNames.size()); ++color)
            item.addSubItem (new FileTreeItem (*this, {}, FileTreeItem::Kind::FavoriteCategory, color));
        return;
    }

    if (item.getUniqueName().startsWith ("favorite-"))
    {
        const int color = item.getUniqueName().substring (9).getIntValue();
        std::vector<juce::File> entries;
        for (const auto& favorite : favorites)
            if (favorite.color == color)
                entries.push_back (favorite.file);
        std::sort (entries.begin(), entries.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        });
        for (const auto& file : entries)
            item.addSubItem (new FileTreeItem (*this, file, FileTreeItem::Kind::FavoriteEntry, color));
        return;
    }

    if (item.getUniqueName() == "search-results")
    {
        std::vector<juce::File> folders;
        std::set<juce::String> seenPaths;
        for (const auto& file : searchResults)
        {
            const auto folder = file.getParentDirectory();
            if (seenPaths.insert (folder.getFullPathName()).second)
                folders.push_back (folder);
        }
        std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0;
        });
        for (const auto& folder : folders)
            item.addSubItem (new FileTreeItem (*this, folder, FileTreeItem::Kind::SearchFolderEntry));
        return;
    }

    if (item.getUniqueName().startsWith ("search-folder:"))
    {
        const auto folderPath = item.getFile().getFullPathName();
        for (const auto& file : searchResults)
            if (file.getParentDirectory().getFullPathName().compareIgnoreCase (folderPath) == 0)
                item.addSubItem (new FileTreeItem (*this, file, FileTreeItem::Kind::SearchResultEntry));
        return;
    }

    if (item.getUniqueName() == "computer")
    {
        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots (roots);
        for (const auto& root : roots)
            item.addSubItem (new FileTreeItem (*this, root));
        return;
    }

    juce::Array<juce::File> children;
    item.getFile().findChildFiles (children, juce::File::findFilesAndDirectories, false);
    std::vector<juce::File> directories, files;
    for (const auto& child : children)
    {
        if (child.isDirectory())
            directories.push_back (child);
        else if (child.existsAsFile())
            files.push_back (child);
    }
    const auto sortByName = [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
    };
    std::sort (directories.begin(), directories.end(), sortByName);
    std::sort (files.begin(), files.end(), sortByName);
    const auto childKind = item.getKind() == FileTreeItem::Kind::SearchResultEntry
        ? FileTreeItem::Kind::SearchResultEntry
        : item.getKind() == FileTreeItem::Kind::FavoriteEntry
            ? FileTreeItem::Kind::FavoriteEntry : FileTreeItem::Kind::FileSystem;
    for (const auto& directory : directories)
        item.addSubItem (new FileTreeItem (*this, directory, childKind));
    for (const auto& file : files)
        item.addSubItem (new FileTreeItem (*this, file, childKind));
}
