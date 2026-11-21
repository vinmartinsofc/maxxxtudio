#include "AppModalDialog.h"
#include "Theme.h"
#include <algorithm>
#include <memory>

namespace
{
void drawWarningTriangle (juce::Graphics& g, juce::Rectangle<float> bounds,
                          juce::Colour colour, float strokeWidth, bool filled)
{
    juce::Path triangle;
    triangle.startNewSubPath (bounds.getCentreX(), bounds.getY() + 2.0f);
    triangle.lineTo (bounds.getRight() - 3.0f, bounds.getBottom() - 2.0f);
    triangle.lineTo (bounds.getX() + 3.0f, bounds.getBottom() - 2.0f);
    triangle.closeSubPath();

    if (filled)
    {
        g.setColour (colour);
        g.fillPath (triangle);
    }
    else
    {
        g.setColour (colour);
        g.strokePath (triangle, juce::PathStrokeType (strokeWidth,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    const auto centreX = bounds.getCentreX();
    const auto markTop = bounds.getY() + bounds.getHeight() * 0.40f;
    const auto markBottom = bounds.getY() + bounds.getHeight() * 0.65f;
    g.setColour (filled ? juce::Colour (0xff242426) : colour);
    g.drawLine (centreX, markTop, centreX, markBottom, std::max (2.0f, strokeWidth));
    g.fillEllipse (centreX - strokeWidth, bounds.getY() + bounds.getHeight() * 0.73f,
                   strokeWidth * 2.0f, strokeWidth * 2.0f);
}
}

class AppModalDialog::DialogButton final : public juce::TextButton
{
public:
    DialogButton (const Action& action, std::function<void()> callback)
        : juce::TextButton (action.text), style (action.style)
    {
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (false);
        onClick = std::move (callback);
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto fill = isButtonDown ? theme::accent()
            : isMouseOver ? theme::accentHover()
            : style == ButtonStyle::Tertiary ? juce::Colours::transparentBlack : theme::panel();
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);

        if (style != ButtonStyle::Tertiary)
        {
            g.setColour (isMouseOver || isButtonDown ? theme::accentHover() : theme::border());
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        }
        else
        {
            g.setColour (isMouseOver || isButtonDown ? theme::accentHover() : theme::border());
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        }

        const auto textColour = style == ButtonStyle::Tertiary && ! isMouseOver && ! isButtonDown
            ? theme::textDim() : theme::text();
        g.setColour (textColour);
        g.setFont (juce::FontOptions (14.0f, style == ButtonStyle::Primary
                                                 ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (8, 0),
                          juce::Justification::centred, 1);
    }

private:
    ButtonStyle style;
};

AppModalDialog::AppModalDialog (juce::Component& owner,
                                        juce::String dialogTitle,
                                        juce::String dialogMessage,
                                        Icon dialogIcon,
                                        std::vector<Action> actions,
                                        std::function<void (int)> callback,
                                        juce::String initialValue,
                                        juce::String placeholder,
                                        std::function<void (int, juce::String)> inputCallback)
    : parent (&owner), title (std::move (dialogTitle)), message (std::move (dialogMessage)),
      icon (dialogIcon), onResult (std::move (callback)),
      onInputResult (std::move (inputCallback)),
      inputVisible (static_cast<bool> (onInputResult))
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);
    owner.addComponentListener (this);

    if (inputVisible)
    {
        addAndMakeVisible (input);
        input.setText (initialValue, juce::dontSendNotification);
        input.setTextToShowWhenEmpty (placeholder, theme::textDim());
        input.setColour (juce::TextEditor::backgroundColourId, theme::control());
        input.setColour (juce::TextEditor::textColourId, theme::text());
        input.setColour (juce::TextEditor::outlineColourId, theme::controlBorder());
        input.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent());
        input.setColour (juce::TextEditor::highlightColourId, theme::accent().withAlpha (0.35f));
        input.onReturnKey = [this]
        {
            if (! buttons.empty())
                buttons.front()->triggerClick();
        };
    }

    for (const auto& action : actions)
    {
        const auto result = action.result;
        const juce::Component::SafePointer<AppModalDialog> safeThis (this);
        auto button = std::make_unique<DialogButton> (action, [safeThis, result]
        {
            juce::MessageManager::callAsync ([safeThis, result]
            {
                if (safeThis != nullptr)
                    safeThis->dismiss (result);
            });
        });
        addAndMakeVisible (*button);
        buttons.push_back (std::move (button));
    }
}

AppModalDialog::~AppModalDialog()
{
    if (auto* owner = parent.getComponent())
        owner->removeComponentListener (this);
}

void AppModalDialog::show (juce::Component& parent,
                               juce::String title,
                               juce::String message,
                               Icon icon,
                               std::vector<Action> actions,
                               std::function<void (int)> onResult)
{
    auto& owner = parent.getTopLevelComponent() != nullptr
        ? *parent.getTopLevelComponent() : parent;
    auto* dialog = new AppModalDialog (owner, std::move (title), std::move (message),
                                          icon, std::move (actions), std::move (onResult));
    owner.addAndMakeVisible (dialog);
    dialog->setBounds (owner.getLocalBounds());
    dialog->toFront (true);
    dialog->enterModalState (true);
}

void AppModalDialog::showInput (juce::Component& parent,
                                    juce::String title,
                                    juce::String message,
                                    juce::String initialValue,
                                    juce::String placeholder,
                                    std::vector<Action> actions,
                                    std::function<void (int, juce::String)> onResult)
{
    auto& owner = parent.getTopLevelComponent() != nullptr
        ? *parent.getTopLevelComponent() : parent;
    auto* dialog = new AppModalDialog (
        owner, std::move (title), std::move (message), Icon::None, std::move (actions), {},
        std::move (initialValue), std::move (placeholder), std::move (onResult));
    owner.addAndMakeVisible (dialog);
    dialog->setBounds (owner.getLocalBounds());
    dialog->toFront (true);
    dialog->enterModalState (true);
    dialog->input.grabKeyboardFocus();
}

void AppModalDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101012).withAlpha (0.58f));

    const juce::DropShadow (juce::Colours::black.withAlpha (0.60f), 24, { 0, 10 })
        .drawForRectangle (g, dialogBounds);

    const auto panel = dialogBounds.toFloat();
    g.setColour (theme::panel());
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (theme::accent());
    g.drawRoundedRectangle (panel, 8.0f, 1.5f);

    if (icon != Icon::None)
    {
        if (icon == Icon::Question)
        {
            const auto badge = juce::Rectangle<float> (62.0f, 62.0f)
                                   .withCentre ({ panel.getX() + 52.0f, panel.getY() + 58.0f });
            g.setColour (theme::controlBorder());
            g.fillEllipse (badge);
            g.setColour (theme::accent());
            g.setFont (juce::FontOptions (52.0f, juce::Font::bold));
            g.drawText ("?", badge.toNearestInt().translated (0, -1),
                        juce::Justification::centred);
            drawWarningTriangle (g, { badge.getRight() - 23.0f, badge.getBottom() - 25.0f,
                                      37.0f, 35.0f }, theme::accent(), 2.5f, true);
        }
        else
        {
            const auto iconBounds = juce::Rectangle<float> (78.0f, 78.0f)
                                        .withCentre ({ panel.getX() + 54.0f, panel.getY() + 61.0f });
            drawWarningTriangle (g, iconBounds,
                                 icon == Icon::Error ? theme::recording() : theme::accent(),
                                 4.0f, false);
        }
    }

    const float contentLeft = panel.getX() + (icon == Icon::None ? 24.0f : 112.0f);
    const float contentRight = panel.getRight() - 24.0f;
    g.setColour (theme::text());
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawFittedText (title, juce::Rectangle<int> (juce::roundToInt (contentLeft),
                                                    juce::roundToInt (panel.getY() + 24.0f),
                                                    juce::roundToInt (contentRight - contentLeft), 26),
                      juce::Justification::centredLeft, 1);
    g.setColour (theme::textDim());
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (message, juce::Rectangle<int> (juce::roundToInt (contentLeft),
                                                      juce::roundToInt (panel.getY() + 58.0f),
                                                      juce::roundToInt (contentRight - contentLeft),
                                                      inputVisible ? 48
                                                                   : std::max (24, juce::roundToInt (panel.getHeight() - 126.0f))),
                      juce::Justification::topLeft, 4);
}

void AppModalDialog::resized()
{
    const int width = std::max (280, std::min (520, getWidth() - 32));
    const int height = std::max (180, std::min (244, getHeight() - 32));
    dialogBounds = juce::Rectangle<int> (width, height).withCentre (getLocalBounds().getCentre());
    if (inputVisible)
        input.setBounds (dialogBounds.getX() + 24, dialogBounds.getY() + 118,
                         dialogBounds.getWidth() - 48, 34);

    if (buttons.empty())
        return;

    constexpr int buttonHeight = 40;
    constexpr int gap = 8;
    const int innerWidth = dialogBounds.getWidth() - 48;
    const int buttonWidth = std::max (80, (innerWidth - gap * static_cast<int> (buttons.size() - 1))
                                               / static_cast<int> (buttons.size()));
    int x = dialogBounds.getRight() - 24 - buttonWidth;
    const int y = dialogBounds.getBottom() - 20 - buttonHeight;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    {
        (*it)->setBounds (x, y, buttonWidth, buttonHeight);
        x -= buttonWidth + gap;
    }
}

bool AppModalDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        const auto cancel = std::find_if (buttons.begin(), buttons.end(), [] (const auto& button)
        {
            return button->getButtonText().equalsIgnoreCase ("Cancel");
        });
        if (cancel != buttons.end())
        {
            (*cancel)->triggerClick();
            return true;
        }
        if (! buttons.empty())
        {
            buttons.back()->triggerClick();
            return true;
        }
    }

    if (key == juce::KeyPress::returnKey && ! buttons.empty())
    {
        buttons.front()->triggerClick();
        return true;
    }

    return false;
}

void AppModalDialog::componentMovedOrResized (juce::Component& component,
                                                  bool, bool wasResized)
{
    if (wasResized && parent != nullptr && &component == parent.getComponent())
        setBounds (component.getLocalBounds());
}

void AppModalDialog::dismiss (int result)
{
    auto callback = std::move (onResult);
    auto inputCallback = std::move (onInputResult);
    const auto inputText = input.getText();
    if (isCurrentlyModal())
        exitModalState (result);
    if (auto* owner = parent.getComponent())
        owner->removeChildComponent (this);
    delete this;
    if (callback)
        callback (result);
    if (inputCallback)
        inputCallback (result, inputText);
}
