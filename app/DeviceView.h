#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ActivityIndicator.h"
#include "AudioEngine.h"
#include "Theme.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Cadeia de dispositivos (plugins) da trilha selecionada: instrumento primeiro, efeitos depois.
class DeviceView : public juce::Component, public juce::DragAndDropTarget,
                   private TaskRegistry::Listener
{
public:
    explicit DeviceView (TaskRegistry&);
    ~DeviceView() override;

    std::function<void (int trackId, int deviceId)> onOpen, onRemove;
    std::function<void (int trackId, int deviceId, bool bypassed)> onBypass;
    std::function<void (int trackId, int deviceId, int direction)> onMove;
    std::function<void (int trackId, int deviceId, int targetDeviceId, bool afterTarget)> onReorder;
    std::function<void (int trackId, const std::vector<int>& deviceIds)> onGroupRequested, onUngroupRequested;
    std::function<bool (int trackId, const juce::String& pluginIdentifier)> canAcceptPluginDrop;
    std::function<void (int trackId, const juce::String& pluginIdentifier, int insertionIndex)> onPluginDropped;

    void setDevices (int trackId, const juce::String& trackName, const std::vector<DeviceInfo>& devices);
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    class Box;
    int insertionIndexAt (int x) const;
    void updatePluginDrop (const SourceDetails&);
    void selectDevice (const DeviceInfo&, juce::ModifierKeys);
    std::vector<int> selectedDevicesInOrder() const;
    bool isDeviceSelected (int deviceId) const;
    bool canGroupSelection() const;
    bool canUngroupSelection() const;
    void showDeviceContextMenu (int deviceId, juce::Point<int> screenPosition);
    void toggleGroupCollapse (int groupId);
    void taskRegistryChanged() override;
    void refreshLoadingIndicators();
    void positionLoadingIndicators();

    struct LoadingIndicator
    {
        int slot = 0;
        std::unique_ptr<ActivityIndicator> indicator;
    };

    TaskRegistry& registry;
    int trackId = -1;
    juce::String trackName;
    std::vector<std::unique_ptr<Box>> boxes;
    std::map<std::string, LoadingIndicator> loadingIndicators;
    std::set<int> selectedDeviceIds, collapsedGroupIds;
    bool pluginDragOver = false, popupMenuRequested = false;
    int pluginDropIndex = -1;
};
