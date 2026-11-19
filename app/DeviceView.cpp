#include "DeviceView.h"
#include "Icons.h"

class DeviceView::Box : public juce::Component, public juce::DragAndDropTarget
{
    class IconButton final : public juce::TextButton
    {
    public:
        IconButton (icons::Icon icon, juce::String tooltip, bool invertActiveState = false)
            : juce::TextButton (), icon (icon), invertActiveState (invertActiveState)
        {
            setTooltip (tooltip);
            setName (tooltip);
            setMouseClickGrabsKeyboardFocus (false);
        }

        void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            const auto active = invertActiveState ? ! getToggleState() : getToggleState();
            g.setColour (isButtonDown ? theme::accent()
                                      : isMouseOver ? theme::accentHover()
                                                    : theme::panel());
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (isMouseOver || isButtonDown
                ? theme::accent()
                : active ? theme::accent()
                         : theme::textDim());
            g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
            if (const auto* drawable = icons::get (
                    icon, isMouseOver || isButtonDown ? theme::text() :
                    active ? theme::accent() : theme::textDim(), 18))
                drawable->drawWithin (g, getLocalBounds().reduced (5).toFloat(),
                                      juce::RectanglePlacement::centred, 1.0f);
        }

    private:
        icons::Icon icon;
        bool invertActiveState;
    };

public:
    Box (const DeviceInfo& d, DeviceView& o, bool canMoveLeft, bool canMoveRight)
        : info (d), owner (o)
    {
        addAndMakeVisible (openButton);
        addAndMakeVisible (bypassButton);
        addAndMakeVisible (moveLeftButton);
        addAndMakeVisible (moveRightButton);
        addAndMakeVisible (removeButton);
        addAndMakeVisible (foldButton);
        foldButton.setVisible (false);
        moveLeftButton.setEnabled (canMoveLeft);
        moveRightButton.setEnabled (canMoveRight);
        openButton.setTooltip ("Abrir ou restaurar a janela do plugin");
        openButton.setName ("Abrir ou restaurar a janela do plugin");
        bypassButton.setName (info.isBypassed ? "Plugin em bypass" : "Plugin ativo");
        removeButton.setName ("Remover plugin");
        moveLeftButton.setTooltip (theme::ui ("Mover este plugin uma posicao para a esquerda"));
        moveRightButton.setTooltip (theme::ui ("Mover este plugin uma posicao para a direita"));
        openButton.onClick = [this] { if (owner.onOpen) owner.onOpen (owner.trackId, info.id); };
        bypassButton.setClickingTogglesState (true);
        bypassButton.setToggleState (info.isBypassed, juce::dontSendNotification);
        bypassButton.setTooltip (info.isBypassed ? "Ativar plugin" : "Colocar plugin em bypass");
        removeButton.setTooltip ("Remover plugin");
        bypassButton.onClick = [this]
        {
            const int tid = owner.trackId, did = info.id;
            const bool bypassed = bypassButton.getToggleState();
            setBypassed (bypassed);
            if (owner.onBypass)
                owner.onBypass (tid, did, bypassed);
        };
        removeButton.onClick = [this]
        {
            const int tid = owner.trackId, did = info.id;
            auto cb = owner.onRemove;                                // o Box sera destruido no callback
            juce::MessageManager::callAsync ([cb, tid, did] { if (cb) cb (tid, did); });
        };
        const auto requestMove = [this] (int direction)
        {
            const int tid = owner.trackId, did = info.id;
            auto cb = owner.onMove;
            juce::MessageManager::callAsync ([cb, tid, did, direction]
            {
                if (cb) cb (tid, did, direction);
            });
        };
        moveLeftButton.onClick = [requestMove] { requestMove (-1); };
        moveRightButton.onClick = [requestMove] { requestMove (1); };
        foldButton.onClick = [this]
        {
            if (info.groupId > 0)
                owner.toggleGroupCollapse (info.groupId);
        };
        openButton.setMouseClickGrabsKeyboardFocus (false);
        bypassButton.setMouseClickGrabsKeyboardFocus (false);
        moveLeftButton.setMouseClickGrabsKeyboardFocus (false);
        moveRightButton.setMouseClickGrabsKeyboardFocus (false);
        removeButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.setMouseClickGrabsKeyboardFocus (false);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragStarted = false;
        popupMenuRequested = event.mods.isPopupMenu();
        if (! popupMenuRequested || ! owner.isDeviceSelected (info.id))
            owner.selectDevice (info, event.mods);
        else
            owner.grabKeyboardFocus();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (info.isInstrument || info.groupId > 0 || dragStarted || ! event.mods.isLeftButtonDown()
            || event.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            const auto image = juce::ScaledImage (createComponentSnapshot (getLocalBounds()));
            container->startDragging (dragSourceDescription(), this, image, false, nullptr, &event.source);
            dragStarted = true;
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        dragStarted = false;
        if (popupMenuRequested)
            owner.showDeviceContextMenu (info.id, event.getScreenPosition());
        popupMenuRequested = false;
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        if (details.description.toString().startsWith ("plugin:"))
            return owner.isInterestedInDragSource (details);

        if (info.groupId > 0)
            return false;

        int sourceTrackId = -1, sourceDeviceId = -1;
        return ! info.isInstrument && draggedDevice (details, sourceTrackId, sourceDeviceId)
            && sourceTrackId == owner.trackId && sourceDeviceId != info.id;
    }

    void itemDragEnter (const SourceDetails& details) override
    {
        if (details.description.toString().startsWith ("plugin:"))
            owner.itemDragEnter (detailsInOwner (details));
        else
            updateDropTarget (details);
    }

    void itemDragMove (const SourceDetails& details) override
    {
        if (details.description.toString().startsWith ("plugin:"))
            owner.itemDragMove (detailsInOwner (details));
        else
            updateDropTarget (details);
    }

    void itemDragExit (const SourceDetails& details) override
    {
        if (details.description.toString().startsWith ("plugin:"))
        {
            owner.itemDragExit (detailsInOwner (details));
            return;
        }

        dropAfterTarget = false;
        dropHighlight = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        if (details.description.toString().startsWith ("plugin:"))
        {
            owner.itemDropped (detailsInOwner (details));
            return;
        }

        int sourceTrackId = -1, sourceDeviceId = -1;
        const bool afterTarget = details.localPosition.x >= getWidth() / 2;
        const int targetDeviceId = info.id;
        auto cb = owner.onReorder;
        dropHighlight = false;
        repaint();
        if (draggedDevice (details, sourceTrackId, sourceDeviceId))
            juce::MessageManager::callAsync ([cb, sourceTrackId, sourceDeviceId,
                                               targetDeviceId, afterTarget]
            {
                if (cb) cb (sourceTrackId, sourceDeviceId, targetDeviceId, afterTarget);
            });
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::panelLight());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (info.isInstrument ? theme::accent() : theme::accentHover());
        g.fillRoundedRectangle (r.withHeight (5.0f), 3.0f);
        if (rackCollapsed)
        {
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
            g.drawText ("Rack (" + juce::String (rackSize) + ")", getLocalBounds().reduced (10, 0).withTrimmedTop (12).withHeight (36),
                        juce::Justification::centredLeft, true);
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (theme::ui ("Cadeia em serie"), 10, 46, getWidth() - 20, 16,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::text());
            g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
            auto title = getLocalBounds().reduced (10, 0).withTrimmedTop (12).withHeight (36);
            if (rackHeader)
                title = title.withTrimmedRight (24);
            g.drawText (info.name, title, juce::Justification::centredLeft, true);
            g.setColour (theme::textDim());
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (info.isInstrument ? theme::ui ("Instrumento") : theme::ui ("Efeito"),
                        10, 46, getWidth() - 20, 16, juce::Justification::centredLeft);
        }

        if (owner.isDeviceSelected (info.id))
        {
            g.setColour (theme::accent());
            g.drawRoundedRectangle (r, 6.0f, 2.0f);
        }
        else
        {
            g.setColour (theme::border());
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! dropHighlight)
            return;
        g.setColour (theme::accent());
        g.fillRect (dropAfterTarget ? getWidth() - 4 : 0, 0, 4, getHeight());
    }

    void resized() override
    {
        foldButton.setBounds (getWidth() - 28, 8, 20, 20);
        auto a = getLocalBounds().reduced (10).removeFromBottom (34);
        removeButton.setBounds (a.removeFromRight (22));
        a.removeFromRight (3);
        moveRightButton.setBounds (a.removeFromRight (18));
        a.removeFromRight (3);
        moveLeftButton.setBounds (a.removeFromRight (18));
        a.removeFromRight (3);
        bypassButton.setBounds (a.removeFromRight (34));
        a.removeFromRight (3);
        openButton.setBounds (a.removeFromRight (34));
    }

    int groupId() const noexcept { return info.groupId; }
    const DeviceInfo& deviceInfo() const noexcept { return info; }
    bool canMoveLeft() const noexcept { return moveLeftButton.isEnabled(); }
    bool canMoveRight() const noexcept { return moveRightButton.isEnabled(); }
    void setBypassed (bool bypassed)
    {
        info.isBypassed = bypassed;
        bypassButton.setToggleState (bypassed, juce::dontSendNotification);
        bypassButton.setTooltip (bypassed ? "Ativar plugin" : "Colocar plugin em bypass");
        bypassButton.setName (bypassed ? "Plugin em bypass" : "Plugin ativo");
        bypassButton.repaint();
    }

    void setRackState (bool isHeader, bool collapsed, int size)
    {
        rackHeader = isHeader;
        rackCollapsed = isHeader && collapsed;
        rackSize = size;
        foldButton.setVisible (isHeader);
        foldButton.setButtonText (collapsed ? "+" : "-");
        foldButton.setTooltip (collapsed ? theme::ui ("Expandir rack") : theme::ui ("Recolher rack"));
        openButton.setVisible (! rackCollapsed);
        bypassButton.setVisible (! rackCollapsed);
        moveLeftButton.setVisible (! rackCollapsed);
        moveRightButton.setVisible (! rackCollapsed);
        removeButton.setVisible (! rackCollapsed);
        repaint();
    }

private:
    juce::var dragSourceDescription() const
    {
        return info.isInstrument ? juce::var()
            : juce::var ("device-order:" + juce::String (owner.trackId) + ":" + juce::String (info.id));
    }

    bool draggedDevice (const SourceDetails& details, int& sourceTrackId, int& sourceDeviceId) const
    {
        const auto description = details.description.toString();
        const juce::String prefix = "device-order:";
        if (! description.startsWith (prefix))
            return false;

        const auto payload = description.substring (prefix.length());
        const int separator = payload.indexOfChar (':');
        if (separator <= 0)
            return false;

        sourceTrackId = payload.substring (0, separator).getIntValue();
        sourceDeviceId = payload.substring (separator + 1).getIntValue();
        return sourceTrackId >= 0 && sourceDeviceId > 0;
    }

    void updateDropTarget (const SourceDetails& details)
    {
        dropAfterTarget = details.localPosition.x >= getWidth() / 2;
        dropHighlight = true;
        repaint();
    }

    SourceDetails detailsInOwner (const SourceDetails& details) const
    {
        auto ownerDetails = details;
        ownerDetails.localPosition = owner.getLocalPoint (this, details.localPosition);
        return ownerDetails;
    }

    DeviceInfo info;
    DeviceView& owner;
    bool rackHeader = false, rackCollapsed = false;
    int rackSize = 0;
    bool dragStarted = false, dropHighlight = false, dropAfterTarget = false, popupMenuRequested = false;
    IconButton openButton { icons::Icon::OpenWindow, "Abrir ou restaurar a janela do plugin" };
    IconButton bypassButton { icons::Icon::Power, "Ativar ou colocar plugin em bypass", true };
    juce::TextButton moveLeftButton { "<" }, moveRightButton { ">" };
    IconButton removeButton { icons::Icon::Close, "Remover plugin" };
    juce::TextButton foldButton { "-" };
};

DeviceView::DeviceView (TaskRegistry& taskRegistry) : registry (taskRegistry)
{
    setWantsKeyboardFocus (true);
    horizontalScrollBar.setAutoHide (false);
    horizontalScrollBar.setSingleStepSize (96.0);
    horizontalScrollBar.setColour (juce::ScrollBar::backgroundColourId, theme::panel());
    horizontalScrollBar.setColour (juce::ScrollBar::trackColourId, theme::border());
    horizontalScrollBar.setColour (juce::ScrollBar::thumbColourId, theme::accent());
    horizontalScrollBar.setWantsKeyboardFocus (true);
    horizontalScrollBar.addListener (this);
    horizontalScrollBar.addKeyListener (this);
    horizontalScrollBar.setMouseClickGrabsKeyboardFocus (true);
    registry.addListener (*this);
}

DeviceView::~DeviceView()
{
    horizontalScrollBar.removeListener (this);
    horizontalScrollBar.removeKeyListener (this);
    registry.removeListener (*this);
}

bool DeviceView::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::leftKey)
        || key.isKeyCode (juce::KeyPress::rightKey))
    {
        horizontalScrollBar.moveScrollbarInSteps (
            key.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1, juce::sendNotification);
        return true;
    }

    const bool ctrl = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (key.getKeyCode()));
    if (ctrl && keyCode == 'C')
    {
        const auto selected = selectedDevicesInOrder();
        if (! selected.empty() && onCopyDevices)
            onCopyDevices (trackId, selected);
        return true;
    }
    if (ctrl && keyCode == 'V')
    {
        if (canPasteDevices && canPasteDevices() && onPasteDevices)
            onPasteDevices (trackId);
        return true;
    }
    if (! ctrl || keyCode != 'G')
        return false;

    const auto selected = selectedDevicesInOrder();
    if (key.getModifiers().isShiftDown())
    {
        if (onUngroupRequested)
            onUngroupRequested (trackId, selected);
    }
    else if (onGroupRequested)
    {
        onGroupRequested (trackId, selected);
    }
    return true;
}

bool DeviceView::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return keyPressed (key);
}

bool DeviceView::isInterestedInDragSource (const SourceDetails& details)
{
    const auto description = details.description.toString();
    return description.startsWith ("plugin:") && canAcceptPluginDrop
        && canAcceptPluginDrop (trackId, description.substring (7));
}

void DeviceView::itemDragEnter (const SourceDetails& details)
{
    setMouseCursor (juce::MouseCursor::CopyingCursor);
    updatePluginDrop (details);
}

void DeviceView::itemDragMove (const SourceDetails& details)
{
    setMouseCursor (juce::MouseCursor::CopyingCursor);
    updatePluginDrop (details);
}

void DeviceView::itemDragExit (const SourceDetails&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    pluginDragOver = false;
    pluginDropIndex = -1;
    repaint();
}

void DeviceView::itemDropped (const SourceDetails& details)
{
    const auto description = details.description.toString();
    const int insertionIndex = insertionIndexAt (details.localPosition.x);
    const int targetTrackId = trackId;
    auto callback = onPluginDropped;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    pluginDragOver = false;
    pluginDropIndex = -1;
    repaint();
    if (description.startsWith ("plugin:"))
    {
        const auto identifier = description.substring (7);
        juce::MessageManager::callAsync ([callback, targetTrackId, identifier, insertionIndex]
        {
            if (callback) callback (targetTrackId, identifier, insertionIndex);
        });
    }
}

void DeviceView::mouseUp (const juce::MouseEvent& event)
{
    if (popupMenuRequested)
        showDeviceContextMenu (-1, event.getScreenPosition());
    popupMenuRequested = false;
}

void DeviceView::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    popupMenuRequested = event.mods.isPopupMenu();
}

int DeviceView::insertionIndexAt (int x) const
{
    for (std::size_t i = 0; i < boxes.size();)
    {
        const int groupId = boxes[i]->groupId();
        if (groupId == 0)
        {
            if (x < boxes[i]->getX() + boxes[i]->getWidth() / 2)
                return static_cast<int> (i);
            ++i;
            continue;
        }

        std::size_t last = i;
        while (last + 1 < boxes.size() && boxes[last + 1]->groupId() == groupId)
            ++last;
        const int midpoint = collapsedGroupIds.contains (groupId)
            ? boxes[i]->getX() + boxes[i]->getWidth() / 2
            : boxes[i]->getX() + (boxes[last]->getRight() - boxes[i]->getX()) / 2;
        if (x < midpoint)
            return static_cast<int> (i);
        i = last + 1;
    }
    return static_cast<int> (boxes.size());
}

void DeviceView::updatePluginDrop (const SourceDetails& details)
{
    pluginDragOver = true;
    pluginDropIndex = insertionIndexAt (details.localPosition.x);
    repaint();
}

void DeviceView::setDevices (int id, const juce::String& name, const std::vector<DeviceInfo>& devices)
{
    if (trackId != id)
    {
        selectedDeviceIds.clear();
        collapsedGroupIds.clear();
        horizontalScrollOffset = 0;
    }
    trackId = id;
    trackName = name;

    std::set<int> deviceIds, groupIds;
    for (const auto& device : devices)
    {
        deviceIds.insert (device.id);
        if (device.groupId > 0)
            groupIds.insert (device.groupId);
    }
    for (auto it = selectedDeviceIds.begin(); it != selectedDeviceIds.end();)
        if (! deviceIds.contains (*it))
            it = selectedDeviceIds.erase (it);
        else
            ++it;
    for (auto it = collapsedGroupIds.begin(); it != collapsedGroupIds.end();)
        if (! groupIds.contains (*it))
            it = collapsedGroupIds.erase (it);
        else
            ++it;

    boxes.clear();
    removeAllChildren();
    for (std::size_t i = 0; i < devices.size(); ++i)
    {
        const bool canMoveLeft = i > 0 && ! devices[i].isInstrument && ! devices[i - 1].isInstrument
                              && devices[i].groupId == 0 && devices[i - 1].groupId == 0;
        const bool canMoveRight = i + 1 < devices.size() && ! devices[i].isInstrument
                               && ! devices[i + 1].isInstrument && devices[i].groupId == 0
                               && devices[i + 1].groupId == 0;
        boxes.push_back (std::make_unique<Box> (devices[i], *this, canMoveLeft, canMoveRight));
        addAndMakeVisible (*boxes.back());
    }
    addAndMakeVisible (horizontalScrollBar);
    refreshLoadingIndicators();
    repaint();
}

void DeviceView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    const auto newOffset = static_cast<int> (newRangeStart);
    if (horizontalScrollOffset == newOffset)
        return;
    horizontalScrollOffset = newOffset;
    resized();
    repaint();
}

void DeviceView::taskRegistryChanged()
{
    refreshLoadingIndicators();
}

void DeviceView::refreshLoadingIndicators()
{
    std::set<std::string> activeTargets;
    if (trackId >= 0)
    {
        const auto prefix = "track:" + juce::String (trackId) + ":plugin";
        for (const auto& task : registry.tasksForTarget (prefix))
        {
            // O sufixo do target identifica o slot que recebera o plugin.
            if (! task.target.startsWith (prefix + ":"))
                continue;

            const auto slotText = task.target.substring (prefix.length() + 1);
            const int slot = slotText.getIntValue();
            if (slot < 0 || juce::String (slot) != slotText)
                continue;

            const auto key = task.target.toStdString();
            activeTargets.insert (key);
            auto [indicator, inserted] = loadingIndicators.try_emplace (key);
            if (inserted)
                indicator->second.indicator = std::make_unique<ActivityIndicator> (registry, task.target);

            indicator->second.slot = slot;
            if (indicator->second.indicator->getParentComponent() != this)
                addChildComponent (*indicator->second.indicator);
        }
    }

    for (auto indicator = loadingIndicators.begin(); indicator != loadingIndicators.end();)
    {
        if (! activeTargets.contains (indicator->first))
        {
            removeChildComponent (indicator->second.indicator.get());
            indicator = loadingIndicators.erase (indicator);
        }
        else
        {
            ++indicator;
        }
    }

    resized();
    repaint();
}

void DeviceView::positionLoadingIndicators()
{
    for (auto& [target, loading] : loadingIndicators)
    {
        juce::ignoreUnused (target);
        juce::Component* anchor = nullptr;

        if (loading.slot >= 0 && loading.slot < static_cast<int> (boxes.size()))
        {
            auto* candidate = boxes[static_cast<std::size_t> (loading.slot)].get();
            if (candidate->isVisible())
            {
                anchor = candidate;
            }
            else if (candidate->groupId() > 0)
            {
                for (std::size_t index = 0; index <= static_cast<std::size_t> (loading.slot); ++index)
                    if (boxes[index]->groupId() == candidate->groupId())
                    {
                        anchor = boxes[index].get();
                        break;
                    }
            }
        }

        if (anchor != nullptr)
        {
            loading.indicator->setBounds (anchor->getX() + anchor->getWidth() - 28,
                                          anchor->getY() + 4, 24, 24);
        }
        else
        {
            int right = 8;
            for (const auto& box : boxes)
                if (box->isVisible())
                    right = std::max (right, box->getRight() + 6);
            const int x = std::min (right, std::max (8, getWidth() - 30));
            loading.indicator->setBounds (x, 8, 24, 24);
        }
    }
}

void DeviceView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());

    for (std::size_t i = 0; i < boxes.size();)
    {
        const int groupId = boxes[i]->groupId();
        if (groupId == 0 || collapsedGroupIds.contains (groupId))
        {
            ++i;
            continue;
        }

        std::size_t last = i;
        while (last + 1 < boxes.size() && boxes[last + 1]->groupId() == groupId)
            ++last;

        const float left = static_cast<float> (boxes[i]->getX() - 4);
        const float right = static_cast<float> (boxes[last]->getRight() + 4);
        const auto frame = juce::Rectangle<float> (left, 2.0f, right - left,
                                                   static_cast<float> (std::max (1, getHeight() - 4)));
        g.setColour (theme::accent().withAlpha (0.8f));
        g.drawRoundedRectangle (frame, 8.0f, 1.5f);
        const auto label = juce::Rectangle<float> (left + 8.0f, 2.0f, 38.0f, 14.0f);
        g.setColour (theme::panel());
        g.fillRect (label);
        g.setColour (theme::accent());
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("Rack", label.toNearestInt(), juce::Justification::centred, true);
        i = last + 1;
    }

    g.setColour (theme::textDim());
    g.setFont (juce::FontOptions (13.5f));
    if (boxes.empty())
    {
        if (trackId == 0)
            g.drawFittedText (theme::ui ("Trilha Master sem efeitos.\nDe duplo clique em um plugin da lista para adicionar efeitos de masterizacao.\n(A trilha Master aceita apenas efeitos.)"),
                              getLocalBounds().reduced (20), juce::Justification::centred, 4);
        else
            g.drawFittedText (theme::ui ("Trilha \"") + trackName + theme::ui ("\" sem dispositivos.\nDe duplo clique em um plugin da lista para adicionar.\n(O instrumento vai primeiro; efeitos entram em sequencia.)"),
                              getLocalBounds().reduced (20), juce::Justification::centred, 4);
    }
}

void DeviceView::paintOverChildren (juce::Graphics& g)
{
    if (! pluginDragOver)
        return;

    g.setColour (theme::accent());
    if (boxes.empty())
    {
        g.drawFittedText (theme::ui ("Solte o plugin para adicionar"),
                          getLocalBounds().reduced (20), juce::Justification::centred, 1);
        return;
    }

    int x = 0;
    if (pluginDropIndex >= static_cast<int> (boxes.size()))
    {
        for (std::size_t i = 0; i < boxes.size();)
        {
            const int groupId = boxes[i]->groupId();
            if (groupId > 0 && collapsedGroupIds.contains (groupId))
            {
                x = boxes[i]->getRight();
                while (i + 1 < boxes.size() && boxes[i + 1]->groupId() == groupId)
                    ++i;
            }
            else
            {
                x = boxes[i]->getRight();
            }
            ++i;
        }
        x += 2;
    }
    else
    {
        x = boxes[static_cast<std::size_t> (pluginDropIndex)]->getX() - 3;
    }
    g.fillRect (x - 1, 4, 3, getHeight() - 8);
}

void DeviceView::resized()
{
    int totalWidth = 8;
    for (std::size_t i = 0; i < boxes.size();)
    {
        const int groupId = boxes[i]->groupId();
        if (groupId == 0)
        {
            totalWidth += 196;
            ++i;
            continue;
        }

        std::size_t last = i;
        while (last + 1 < boxes.size() && boxes[last + 1]->groupId() == groupId)
            ++last;
        totalWidth += collapsedGroupIds.contains (groupId)
            ? 196 : static_cast<int> (last - i + 1) * 196;
        i = last + 1;
    }
    totalWidth += 8;

    const int maximumOffset = std::max (0, totalWidth - getWidth());
    horizontalScrollOffset = std::clamp (horizontalScrollOffset, 0, maximumOffset);
    const bool needsScrollbar = maximumOffset > 0;
    horizontalScrollBar.setVisible (needsScrollbar);
    horizontalScrollBar.setRangeLimits (0.0, static_cast<double> (std::max (1, totalWidth)),
                                        juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange (static_cast<double> (horizontalScrollOffset),
                                         static_cast<double> (std::max (1, getWidth())),
                                         juce::dontSendNotification);

    const int cardBottom = needsScrollbar ? getHeight() - 28 : getHeight() - 8;
    int x = 8;
    for (std::size_t i = 0; i < boxes.size();)
    {
        const int groupId = boxes[i]->groupId();
        if (groupId == 0)
        {
            boxes[i]->setRackState (false, false, 0);
            boxes[i]->setVisible (true);
            boxes[i]->setBounds (x - horizontalScrollOffset, 8, 190,
                                 std::max (1, cardBottom - 8));
            x += 196;
            ++i;
            continue;
        }

        std::size_t last = i;
        while (last + 1 < boxes.size() && boxes[last + 1]->groupId() == groupId)
            ++last;

        const int count = static_cast<int> (last - i + 1);
        const bool collapsed = collapsedGroupIds.contains (groupId);
        if (collapsed)
        {
            boxes[i]->setRackState (true, true, count);
            boxes[i]->setVisible (true);
            boxes[i]->setBounds (x - horizontalScrollOffset, 8, 190,
                                 std::max (1, cardBottom - 8));
            for (std::size_t j = i + 1; j <= last; ++j)
            {
                boxes[j]->setVisible (false);
                boxes[j]->setBounds ({});
            }
            x += 196;
        }
        else
        {
            for (std::size_t j = i; j <= last; ++j)
            {
                boxes[j]->setRackState (j == i, false, count);
                boxes[j]->setVisible (true);
                boxes[j]->setBounds (x - horizontalScrollOffset, 20, 190,
                                     std::max (1, cardBottom - 20));
                x += 196;
            }
        }
        i = last + 1;
    }

    if (needsScrollbar)
    {
        const int scrollbarWidth = std::min (280, std::max (1, getWidth() - 16));
        horizontalScrollBar.setBounds (getWidth() - scrollbarWidth - 8,
                                       getHeight() - 20, scrollbarWidth, 14);
    }

    positionLoadingIndicators();
}

void DeviceView::selectDevice (const DeviceInfo& device, juce::ModifierKeys modifiers)
{
    grabKeyboardFocus();
    std::vector<int> targets;
    if (device.groupId > 0)
    {
        for (const auto& box : boxes)
            if (box->groupId() == device.groupId)
                targets.push_back (box->deviceInfo().id);
    }
    else
    {
        targets.push_back (device.id);
    }

    const bool additive = modifiers.isCtrlDown() || modifiers.isCommandDown();
    if (! additive)
        selectedDeviceIds.clear();

    const bool allSelected = std::all_of (targets.begin(), targets.end(),
        [this] (int id) { return selectedDeviceIds.contains (id); });
    for (const auto id : targets)
        if (additive && allSelected)
            selectedDeviceIds.erase (id);
        else
            selectedDeviceIds.insert (id);

    grabKeyboardFocus();
    repaint();
}

std::vector<int> DeviceView::selectedDevicesInOrder() const
{
    std::vector<int> selected;
    for (const auto& box : boxes)
        if (selectedDeviceIds.contains (box->deviceInfo().id))
            selected.push_back (box->deviceInfo().id);
    return selected;
}

bool DeviceView::isDeviceSelected (int deviceId) const
{
    return selectedDeviceIds.contains (deviceId);
}

bool DeviceView::canGroupSelection() const
{
    const auto selected = selectedDevicesInOrder();
    if (selected.size() < 2)
        return false;

    const std::set<int> selectedSet (selected.begin(), selected.end());
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        if (selectedSet.contains (boxes[i]->deviceInfo().id))
            indices.push_back (i);

    if (indices.size() != selected.size() || indices.back() - indices.front() + 1 != indices.size())
        return false;

    for (std::size_t i = indices.front(); i <= indices.back(); ++i)
        if (! selectedSet.contains (boxes[i]->deviceInfo().id)
            || (boxes[i]->deviceInfo().isInstrument && i != 0))
            return false;

    for (const auto& box : boxes)
        if (box->groupId() > 0 && selectedSet.contains (box->deviceInfo().id))
            for (const auto& member : boxes)
                if (member->groupId() == box->groupId()
                    && ! selectedSet.contains (member->deviceInfo().id))
                    return false;

    return true;
}

bool DeviceView::canUngroupSelection() const
{
    return std::any_of (boxes.begin(), boxes.end(), [this] (const auto& box)
    {
        return box->groupId() > 0 && selectedDeviceIds.contains (box->deviceInfo().id);
    });
}

void DeviceView::showDeviceContextMenu (int deviceId, juce::Point<int> screenPosition)
{
    int clickedGroupId = 0;
    const Box* clickedBox = nullptr;
    for (const auto& box : boxes)
        if (box->deviceInfo().id == deviceId)
        {
            clickedBox = box.get();
            clickedGroupId = box->groupId();
            break;
        }

    juce::PopupMenu menu;
    if (clickedBox != nullptr)
    {
        const auto info = clickedBox->deviceInfo();
        menu.addItem (10, "Abrir interface do plugin", static_cast<bool> (onOpen));
        menu.addItem (11, info.isBypassed ? "Ativar plugin" : "Desativar plugin", static_cast<bool> (onBypass));
        menu.addItem (12, "Mover plugin para antes", clickedBox->canMoveLeft() && static_cast<bool> (onMove));
        menu.addItem (13, "Mover plugin para depois", clickedBox->canMoveRight() && static_cast<bool> (onMove));
        menu.addItem (14, "Remover plugin", static_cast<bool> (onRemove));
        menu.addItem (15, "Copiar plugin(s) configurado(s) (Ctrl+C)",
                      ! selectedDevicesInOrder().empty() && static_cast<bool> (onCopyDevices));
        menu.addSeparator();
    }
    menu.addItem (16, "Colar plugin(s) configurado(s) (Ctrl+V)",
                  canPasteDevices && canPasteDevices() && static_cast<bool> (onPasteDevices));
    menu.addItem (1, "Agrupar selecionados em rack (Ctrl+G)", canGroupSelection() && static_cast<bool> (onGroupRequested));
    menu.addItem (2, "Desagrupar rack (Ctrl+Shift+G)", canUngroupSelection() && static_cast<bool> (onUngroupRequested));
    if (clickedGroupId > 0)
    {
        menu.addSeparator();
        menu.addItem (3, collapsedGroupIds.contains (clickedGroupId) ? "Expandir rack" : "Recolher rack");
    }

    juce::Component::SafePointer<DeviceView> safeThis (this);
    const auto selected = selectedDevicesInOrder();
    const int targetTrackId = trackId;
    const bool wasBypassed = clickedBox != nullptr && clickedBox->deviceInfo().isBypassed;
    const juce::PopupMenu::Options options;
    menu.showMenuAsync (options.withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [safeThis, selected, clickedGroupId, deviceId, targetTrackId, wasBypassed] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result == 10 && safeThis->onOpen)
                safeThis->onOpen (targetTrackId, deviceId);
            else if (result == 11)
            {
                for (auto& box : safeThis->boxes)
                    if (box->deviceInfo().id == deviceId)
                        box->setBypassed (! wasBypassed);
                if (safeThis->onBypass)
                    safeThis->onBypass (targetTrackId, deviceId, ! wasBypassed);
            }
            else if (result == 12 && safeThis->onMove)
                safeThis->onMove (targetTrackId, deviceId, -1);
            else if (result == 13 && safeThis->onMove)
                safeThis->onMove (targetTrackId, deviceId, 1);
            else if (result == 14 && safeThis->onRemove)
                safeThis->onRemove (targetTrackId, deviceId);
            else if (result == 15 && safeThis->onCopyDevices)
                safeThis->onCopyDevices (targetTrackId, selected);
            else if (result == 16 && safeThis->onPasteDevices)
                safeThis->onPasteDevices (targetTrackId);
            else if (result == 1 && safeThis->onGroupRequested)
                safeThis->onGroupRequested (safeThis->trackId, selected);
            else if (result == 2 && safeThis->onUngroupRequested)
                safeThis->onUngroupRequested (safeThis->trackId, selected);
            else if (result == 3)
                safeThis->toggleGroupCollapse (clickedGroupId);
        });
}

void DeviceView::toggleGroupCollapse (int groupId)
{
    if (groupId <= 0)
        return;
    if (! collapsedGroupIds.erase (groupId))
        collapsedGroupIds.insert (groupId);
    grabKeyboardFocus();
    resized();
    repaint();
}
