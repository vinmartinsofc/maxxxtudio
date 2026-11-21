#pragma once

#include "ActivityIndicator.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TaskStatusBar final : public juce::Component,
                            private TaskRegistry::Listener
{
public:
    explicit TaskStatusBar (TaskRegistry&);
    ~TaskStatusBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void taskRegistryChanged() override;
    void refreshTask();

    TaskRegistry& registry;
    ActivityIndicator indicator;
    juce::Label label;
    juce::TextButton cancelButton { "Cancel" };
    std::optional<TaskRegistry::Task> currentTask;
};
