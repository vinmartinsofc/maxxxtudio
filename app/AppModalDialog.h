#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

class AppModalDialog final : public juce::Component,
                                private juce::ComponentListener
{
public:
    enum class Icon { None, Warning, Question, Error };
    enum class ButtonStyle { Primary, Secondary, Tertiary };

    struct Action
    {
        juce::String text;
        int result = 0;
        ButtonStyle style = ButtonStyle::Secondary;
    };

    static void show (juce::Component& parent,
                      juce::String title,
                      juce::String message,
                      Icon,
                      std::vector<Action>,
                      std::function<void (int)> onResult = {});
    static void showInput (juce::Component& parent,
                          juce::String title,
                          juce::String message,
                          juce::String initialValue,
                          juce::String placeholder,
                          std::vector<Action>,
                          std::function<void (int, juce::String)> onResult);

    ~AppModalDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class DialogButton;

    AppModalDialog (juce::Component&, juce::String, juce::String, Icon,
                        std::vector<Action>, std::function<void (int)>,
                        juce::String = {}, juce::String = {},
                        std::function<void (int, juce::String)> = {});
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void dismiss (int result);

    juce::Component::SafePointer<juce::Component> parent;
    juce::String title, message;
    Icon icon;
    std::function<void (int)> onResult;
    std::function<void (int, juce::String)> onInputResult;
    std::vector<std::unique_ptr<DialogButton>> buttons;
    juce::TextEditor input;
    bool inputVisible = false;
    juce::Rectangle<int> dialogBounds;
};
