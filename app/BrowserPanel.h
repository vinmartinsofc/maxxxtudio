#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginScanner.h"
#include "Theme.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

// List of detected instruments. Double-click (or press Enter) to load onto the track.
class BrowserPanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::KeyListener
{
public:
    explicit BrowserPanel (PluginScanner&);
    ~BrowserPanel() override;

    std::function<void (const juce::PluginDescription&)> onLoadRequested;
    std::function<juce::String()> onAddDrumRackRequested;
    std::function<juce::String()> onAddUtilityRequested;
    std::function<juce::String()> onAddCompMasterRequested;
    std::function<juce::String()> onAddEqMasterRequested;
    std::function<juce::String()> onOpenReferenceRequested;

    void setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }
    void refresh();
    void grabFocus();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TreeItem;
    class PluginTree;
    enum class Filter { All, Instruments, Effects, Favorites };
    class VstTabButton final : public juce::TextButton
    {
    public:
        VstTabButton() : juce::TextButton ("VST") {}
        void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;
    };

    class NativeToolCard final : public juce::TextButton
    {
    public:
        NativeToolCard (const juce::String&, const juce::String&, const juce::String& dragDescription);
        void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String detail;
        juce::String dragDescription;
        bool dragStarted = false;
    };

    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool loadSelectedPlugin();
    void setMaXXXtudioTab (bool);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void load (const juce::PluginDescription&);
    void showPluginMenu (const juce::PluginDescription&, juce::Point<int>);
    void showFavoriteSelectionMenu (const std::vector<juce::PluginDescription>&,
                                    juce::Point<int>);
    std::vector<juce::PluginDescription> selectedFavoritePlugins() const;
    bool isPluginFavorite (const juce::PluginDescription&) const;
    int pluginFavoriteColor (const juce::PluginDescription&) const;
    void togglePluginFavorite (const juce::PluginDescription&);
    void setPluginFavorite (const juce::PluginDescription&, int color);
    void removePluginFavorite (const juce::PluginDescription&);
    void setPluginFavorites (const std::vector<juce::PluginDescription>&, int color);
    void removePluginFavorites (const std::vector<juce::PluginDescription>&);
    void refreshFavoriteTreeAfterChange (int colorToOpen = -1);
    void loadPluginFavorites();
    void savePluginFavorites();
    void saveTreeState();
    juce::File treeStateFile (Filter stateFilter) const;

    PluginScanner& scanner;
    juce::Label title { {}, "Plugins" };
    juce::TextEditor search;
    std::unique_ptr<PluginTree> tree;
    juce::Viewport toolsViewport;
    juce::Component toolsContent;
    VstTabButton vstTab;
    juce::TextButton maXXXtudioTab { "MaXXXtudio" };
    NativeToolCard drumRackCard;
    NativeToolCard utilityCard;
    NativeToolCard compMasterCard;
    NativeToolCard eqMasterCard;
    NativeToolCard referenceCard;
    juce::Label maXXXtudioDescription { {}, "MaXXXtudio Tools" };
    juce::TextButton allButton { "All" }, instrumentsButton { "Instruments" },
                      effectsButton { "Effects" }, favoritesButton { "Favorites" };
    Filter filter = Filter::All;
    void setFilter (Filter f);
    juce::Label status;
    std::unique_ptr<juce::XmlElement> opennessBeforeSearch;
    bool searchActive = false, rebuildingTree = false;
    bool showingMaXXXtudioTools = false;
    Filter renderedFilter = Filter::All;
    juce::ApplicationProperties pluginFavoriteProperties;
    juce::PropertiesFile* pluginFavoriteSettings = nullptr;
    std::map<juce::String, int> favoritePluginColors;
    std::unique_ptr<juce::XmlElement> favoriteTreeStateAfterRefresh;
    std::set<int> favoriteColorsToOpenAfterRefresh;
};
