#pragma once

#include "TaskRegistry.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ActivityIndicator final : public juce::Component,
                                private juce::Timer,
                                private TaskRegistry::Listener
{
public:
    ActivityIndicator (TaskRegistry&, juce::String target);
    ~ActivityIndicator() override;

    void paint (juce::Graphics&) override;

private:
    void taskRegistryChanged() override;
    void timerCallback() override;
    void refreshTasks();
    void updateVisibility();

    TaskRegistry& registry;
    juce::String target;
    std::vector<TaskRegistry::Task> activeTasks;
    std::optional<TaskRegistry::Task> displayedTask;
    float angle = 0.0f;
};
