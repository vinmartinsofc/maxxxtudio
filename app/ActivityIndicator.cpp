#include "ActivityIndicator.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr auto indicatorDelay = std::chrono::milliseconds (150);
}

ActivityIndicator::ActivityIndicator (TaskRegistry& taskRegistry, juce::String taskTarget)
    : registry (taskRegistry), target (std::move (taskTarget))
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
    registry.addListener (*this);
    refreshTasks();
}

ActivityIndicator::~ActivityIndicator()
{
    stopTimer();
    registry.removeListener (*this);
}

void ActivityIndicator::paint (juce::Graphics& g)
{
    if (! displayedTask.has_value())
        return;

    const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const auto centre = bounds.getCentre();
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.42f;
    juce::Path arc;

    g.setColour (theme::border());
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.8f);
    g.setColour (theme::taskActive());

    if (displayedTask->progress < 0.0)
    {
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                           angle, angle + juce::MathConstants<float>::pi * 1.35f, true);
    }
    else if (displayedTask->progress > 0.0)
    {
        const auto start = -juce::MathConstants<float>::halfPi;
        const auto end = start + juce::MathConstants<float>::twoPi
            * static_cast<float> (displayedTask->progress);
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    }

    g.strokePath (arc, juce::PathStrokeType (2.2f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void ActivityIndicator::taskRegistryChanged()
{
    refreshTasks();
}

void ActivityIndicator::timerCallback()
{
    angle = std::fmod (angle + 0.12f, juce::MathConstants<float>::twoPi);
    updateVisibility();
    repaint();
}

void ActivityIndicator::refreshTasks()
{
    activeTasks = registry.tasksForTarget (target);
    displayedTask.reset();

    if (! activeTasks.empty())
    {
        const auto latest = std::max_element (activeTasks.begin(), activeTasks.end(),
            [] (const TaskRegistry::Task& first, const TaskRegistry::Task& second)
            {
                return first.startedAt < second.startedAt
                    || (first.startedAt == second.startedAt && first.id < second.id);
            });
        displayedTask = *latest;
    }

    if (activeTasks.empty())
        stopTimer();
    else if (! isTimerRunning())
        startTimerHz (30);

    updateVisibility();
    repaint();
}

void ActivityIndicator::updateVisibility()
{
    const auto now = std::chrono::steady_clock::now();
    const bool hasVisibleTask = std::any_of (activeTasks.begin(), activeTasks.end(),
        [now] (const TaskRegistry::Task& task)
        {
            return now - task.startedAt >= indicatorDelay;
        });
    setVisible (hasVisibleTask);
}
