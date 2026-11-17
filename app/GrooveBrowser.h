#pragma once

#include "GrooveLibrary.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class GrooveBrowserWindow : public juce::DocumentWindow, private juce::KeyListener
{
public:
    GrooveBrowserWindow();
    ~GrooveBrowserWindow() override;

    std::function<void (const roll::GrooveTemplate&)> onPreview;
    std::function<void()> onStopPreview;
    std::function<juce::String (const roll::GrooveTemplate&)> onApply;
    std::function<juce::String()> onClear;
    std::function<bool (const juce::KeyPress&)> onTransportKeyPress;

    void setTargetAvailable (bool);
    void setAppliedGrooveName (const juce::String&);
    void setStatus (const juce::String&);
    void setAutoPreviewEnabled (bool);
    void activeWindowStatusChanged() override;
    void closeButtonPressed() override;

private:
    class Content;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void addTransportKeyListenerRecursively (juce::Component&);
    void removeTransportKeyListenerRecursively (juce::Component&);
    Content* content = nullptr;
};
