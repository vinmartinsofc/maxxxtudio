#include "TaskStatusBar.h"
#include "Theme.h"

TaskStatusBar::TaskStatusBar (TaskRegistry& taskRegistry)
    : registry (taskRegistry), indicator (taskRegistry, "global")
{
    addChildComponent (indicator);
    addAndMakeVisible (label);
    addAndMakeVisible (cancelButton);

    label.setColour (juce::Label::textColourId, theme::textDim());
    label.setFont (juce::FontOptions (12.5f));

    cancelButton.setTooltip ("Cancelar a tarefa em andamento");
    cancelButton.setMouseClickGrabsKeyboardFocus (false);
    cancelButton.setColour (juce::TextButton::buttonColourId, theme::control());
    cancelButton.setColour (juce::TextButton::buttonOnColourId, theme::pressed());
    cancelButton.setColour (juce::TextButton::textColourOffId, theme::text());
    cancelButton.onClick = [this]
    {
        if (currentTask.has_value())
            registry.cancel (currentTask->id);
    };

    registry.addListener (*this);
    refreshTask();
}

TaskStatusBar::~TaskStatusBar()
{
    registry.removeListener (*this);
}

void TaskStatusBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel());
    g.setColour (theme::border());
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    if (currentTask.has_value() && currentTask->progress >= 0.0)
    {
        auto progressBounds = getLocalBounds().removeFromBottom (2).toFloat();
        g.setColour (theme::control());
        g.fillRect (progressBounds);
        g.setColour (theme::taskActive());
        progressBounds.setWidth (progressBounds.getWidth()
                                 * static_cast<float> (currentTask->progress));
        g.fillRect (progressBounds);
    }
}

void TaskStatusBar::resized()
{
    auto area = getLocalBounds().reduced (8, 2);
    cancelButton.setBounds (area.removeFromRight (84));
    area.removeFromRight (8);
    indicator.setBounds (area.removeFromLeft (24));
    area.removeFromLeft (7);
    label.setBounds (area);
}

void TaskStatusBar::taskRegistryChanged()
{
    refreshTask();
}

void TaskStatusBar::refreshTask()
{
    currentTask = registry.latestForTarget ("global");
    label.setText (currentTask.has_value() ? currentTask->label : theme::ui ("Pronto"),
                   juce::dontSendNotification);
    label.setColour (juce::Label::textColourId,
                     currentTask.has_value() ? theme::text() : theme::textDim());
    cancelButton.setVisible (currentTask.has_value() && currentTask->cancelable);
    repaint();
}
