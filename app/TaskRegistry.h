#pragma once

#include <juce_events/juce_events.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class TaskScope;

class TaskRegistry
{
public:
    using TaskId = std::uint64_t;

    struct Task
    {
        TaskId id = 0;
        juce::String label;
        juce::String target;
        double progress = -1.0;
        bool cancelable = false;
        std::chrono::steady_clock::time_point startedAt;
        std::function<void()> cancel;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void taskRegistryChanged() = 0;
    };

    TaskRegistry();
    ~TaskRegistry();

    TaskRegistry (const TaskRegistry&) = delete;
    TaskRegistry& operator= (const TaskRegistry&) = delete;

    // begin/setProgress/end aceitam chamadas de workers; mutacoes e listeners ficam na message thread.
    // O registry deve sobreviver a todos os workers que receberam uma referencia para ele.
    TaskId begin (juce::String label, juce::String target, double progress = -1.0,
                  bool cancelable = false, std::function<void()> cancel = {});
    void setProgress (TaskId, double progress);
    void end (TaskId);

    // Cancelamento, consultas e registro de listeners sao exclusivos da message thread.
    bool cancel (TaskId);

    std::vector<Task> tasksForTarget (const juce::String& target) const;
    std::optional<Task> latestForTarget (const juce::String& target) const;

    void addListener (Listener&);
    void removeListener (Listener&);

    static bool targetMatches (const juce::String& taskTarget, const juce::String& queryTarget);

private:
    friend class TaskScope;

    struct State;
    static void dispatch (std::weak_ptr<State>, std::function<void(State&)>);
    static double normaliseProgress (double);
    static void notifyListeners (State&);

    std::shared_ptr<State> state;
};

class TaskScope final
{
public:
    TaskScope (TaskRegistry&, juce::String label, juce::String target,
               double progress = -1.0, bool cancelable = false,
               std::function<void()> cancel = {});
    ~TaskScope();

    TaskScope (const TaskScope&) = delete;
    TaskScope& operator= (const TaskScope&) = delete;
    TaskScope (TaskScope&&) noexcept;
    TaskScope& operator= (TaskScope&&) noexcept;

    void setProgress (double);
    void end();
    TaskRegistry::TaskId id() const { return taskId; }

private:
    std::weak_ptr<TaskRegistry::State> taskState;
    TaskRegistry::TaskId taskId = 0;
};
