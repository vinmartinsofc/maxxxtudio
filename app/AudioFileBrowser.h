#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Theme.h"
#include <functional>
#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

class AudioFileBrowser : public juce::Component, private juce::Timer
{
public:
    explicit AudioFileBrowser (const juce::AudioFormatManager&);
    ~AudioFileBrowser() override;
    std::function<void (const juce::File&)> onPreviewRequested;
    std::function<void()> onPreviewStopRequested;
    std::function<void (float)> onPreviewGainChanged;
    std::function<bool (const juce::File&)> canDeleteFile;
    void setFavoritesMode (bool);
    void stopPreview();
    void revealFile (const juce::File&);
    void setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Favorite
    {
        juce::File file;
        int color = 0;
    };

    struct NavigationState
    {
        juce::File directory;
        juce::String query;
        bool favorites = false;
    };

    class FileTreeItem;
    class FileTreeView : public juce::TreeView
    {
    public:
        explicit FileTreeView (AudioFileBrowser& browser) : owner (browser) {}
        bool keyPressed (const juce::KeyPress&) override;

    private:
        AudioFileBrowser& owner;
    };

    void populate (FileTreeItem&);
    void selectFile (const juce::File&);
    void refreshTree();
    void recordNavigationState();
    void navigateHistory (int direction);
    void updateHistoryButtons();
    void moveSelectedItemsToDirectory (const juce::File&);
    void moveItemToTrash (const juce::File&);
    void createFolderInDirectory (const juce::File&);
    void previewFile (const juce::File&);
    void showFileMenu (const juce::File&, juce::Point<int> screenPosition);
    void locateInExplorer (const juce::File&);
    void setFavorite (const juce::File&, int color);
    void removeFavorite (const juce::File&);
    void loadFavorites();
    void saveFavorites();
    void loadExpandedItems();
    void saveExpandedItems();
    void restoreExpandedItems (juce::TreeViewItem&, const std::set<juce::String>&);
    void restoreExpandedChildren (juce::TreeViewItem&);
    void treeItemOpennessChanged (const juce::String&, bool);
    bool isAudioFile (const juce::File&) const;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Label title { {}, "Audio Files" };
    juce::Label status { {}, "Select an audio file to preview" };
    juce::TextButton backButton { "<" }, forwardButton { ">" };
    juce::TextEditor search;
    FileTreeView tree { *this };
    juce::TextButton previewButton { "Ouvir" }, stopButton { "Stop" };
    juce::ToggleButton autoPreviewButton { "Auto" };
    juce::Label previewGainLabel { {}, "Volume" };
    juce::Slider previewGain;
    juce::File selectedFile;
    juce::File searchDirectory;
    std::vector<juce::File> searchResults;
    std::vector<NavigationState> navigationHistory;
    int navigationHistoryIndex = -1;
    std::shared_ptr<std::atomic<std::uint64_t>> searchGeneration {
        std::make_shared<std::atomic<std::uint64_t>> (0)
    };
    juce::ApplicationProperties favoriteProperties;
    juce::PropertiesFile* favoriteSettings = nullptr;
    std::vector<Favorite> favorites;
    juce::StringArray supportedAudioExtensions;
    std::set<juce::String> expandedFilesystemItems, expandedFavoriteItems;
    bool restoringTreeExpansion = false;
    bool showingFavorites = false;
    juce::ThreadPool searchPool { 1 };
};
