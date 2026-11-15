#include "TaskRegistry.h"
#include <algorithm>
#include <cmath>

struct TaskRegistry::State
{
    std::atomic<TaskId> nextId { 1 };
    std::vector<Task> tasks;
    juce::ListenerList<Listener> listeners;
};

TaskRegistry::TaskRegistry() : state (std::make_shared<State>())
{
}

TaskRegistry::~TaskRegistry() = default;

TaskRegistry::TaskId TaskRegistry::begin (juce::String label, juce::String target, double progress,
                                         bool cancelable, std::function<void()> cancelCallback)
{
    jassert (! cancelable || static_cast<bool> (cancelCallback));
    if (cancelable && ! cancelCallback)
        juce::Logger::writeToLog ("TaskRegistry: tarefa cancelavel criada sem callback de cancelamento.");

    Task task;
    task.id = state->nextId.fetch_add (1, std::memory_order_relaxed);
    task.label = std::move (label);
    task.target = std::move (target);
    task.progress = normaliseProgress (progress);
    task.cancelable = cancelable && static_cast<bool> (cancelCallback);
    task.startedAt = std::chrono::steady_clock::now();
    task.cancel = std::move (cancelCallback);

    const auto id = task.id;
    dispatch (state, [task = std::move (task)] (State& current) mutable
    {
        current.tasks.push_back (std::move (task));
        notifyListeners (current);
    });
    return id;
}

void TaskRegistry::setProgress (TaskId id, double progress)
{
    const auto value = normaliseProgress (progress);
    dispatch (state, [id, value] (State& current)
    {
        const auto task = std::find_if (current.tasks.begin(), current.tasks.end(),
                                        [id] (const Task& item) { return item.id == id; });
        if (task == current.tasks.end())
            return;

        task->progress = value;
        notifyListeners (current);
    });
}

void TaskRegistry::end (TaskId id)
{
    dispatch (state, [id] (State& current)
    {
        const auto oldSize = current.tasks.size();
        std::erase_if (current.tasks, [id] (const Task& item) { return item.id == id; });
        if (current.tasks.size() != oldSize)
            notifyListeners (current);
    });
}

bool TaskRegistry::cancel (TaskId id)
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager == nullptr || ! manager->isThisTheMessageThread())
    {
        juce::Logger::writeToLog ("TaskRegistry: cancel() deve ser chamado na message thread.");
        return false;
    }

    const auto task = std::find_if (state->tasks.begin(), state->tasks.end(),
                                    [id] (const Task& item) { return item.id == id; });
    if (task == state->tasks.end() || ! task->cancelable || ! task->cancel)
        return false;

    auto callback = task->cancel;
    task->cancelable = false;
    notifyListeners (*state);
    callback();
    return true;
}

std::vector<TaskRegistry::Task> TaskRegistry::tasksForTarget (const juce::String& target) const
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager == nullptr || ! manager->isThisTheMessageThread())
    {
        juce::Logger::writeToLog ("TaskRegistry: consulta deve ser feita na message thread.");
        return {};
    }

    std::vector<Task> result;
    for (const auto& task : state->tasks)
        if (targetMatches (task.target, target))
            result.push_back (task);
    return result;
}

std::optional<TaskRegistry::Task> TaskRegistry::latestForTarget (const juce::String& target) const
{
    auto matchingTasks = tasksForTarget (target);
    if (matchingTasks.empty())
        return std::nullopt;

    const auto latest = std::max_element (matchingTasks.begin(), matchingTasks.end(),
        [] (const Task& first, const Task& second)
        {
            return first.startedAt < second.startedAt
                || (first.startedAt == second.startedAt && first.id < second.id);
        });
    return *latest;
}

void TaskRegistry::addListener (Listener& listener)
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager != nullptr && manager->isThisTheMessageThread())
        state->listeners.add (&listener);
    else
        juce::Logger::writeToLog ("TaskRegistry: addListener deve ser chamado na message thread.");
}

void TaskRegistry::removeListener (Listener& listener)
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    jassert (manager != nullptr && manager->isThisTheMessageThread());
    if (manager != nullptr && manager->isThisTheMessageThread())
        state->listeners.remove (&listener);
    else
        juce::Logger::writeToLog ("TaskRegistry: removeListener deve ser chamado na message thread.");
}

bool TaskRegistry::targetMatches (const juce::String& taskTarget, const juce::String& queryTarget)
{
    if (queryTarget.isEmpty())
        return false;
    return taskTarget == queryTarget || taskTarget.startsWith (queryTarget + ":");
}

void TaskRegistry::dispatch (std::weak_ptr<State> weakState, std::function<void(State&)> operation)
{
    auto currentState = weakState.lock();
    if (currentState == nullptr)
        return;

    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager != nullptr && manager->isThisTheMessageThread())
    {
        operation (*currentState);
        return;
    }

    if (! juce::MessageManager::callAsync ([weakState = std::move (weakState),
                                             operation = std::move (operation)]() mutable
    {
        if (const auto lockedState = weakState.lock())
            operation (*lockedState);
    }))
    {
        juce::Logger::writeToLog ("TaskRegistry: nao foi possivel enviar a atualizacao para a message thread.");
    }
}

double TaskRegistry::normaliseProgress (double progress)
{
    if (! std::isfinite (progress) || progress < 0.0)
        return -1.0;
    return std::clamp (progress, 0.0, 1.0);
}

void TaskRegistry::notifyListeners (State& current)
{
    current.listeners.call ([] (Listener& listener) { listener.taskRegistryChanged(); });
}

TaskScope::TaskScope (TaskRegistry& owner, juce::String label, juce::String target,
                      double progress, bool cancelable, std::function<void()> cancel)
    : taskState (owner.state),
      taskId (owner.begin (std::move (label), std::move (target),
                           progress, cancelable, std::move (cancel)))
{
}

TaskScope::~TaskScope()
{
    end();
}

TaskScope::TaskScope (TaskScope&& other) noexcept
    : taskState (std::move (other.taskState)),
      taskId (std::exchange (other.taskId, 0))
{
}

TaskScope& TaskScope::operator= (TaskScope&& other) noexcept
{
    if (this != &other)
    {
        end();
        taskState = std::move (other.taskState);
        taskId = std::exchange (other.taskId, 0);
    }
    return *this;
}

void TaskScope::setProgress (double progress)
{
    if (taskId == 0)
        return;

    const auto value = TaskRegistry::normaliseProgress (progress);
    TaskRegistry::dispatch (taskState, [id = taskId, value] (TaskRegistry::State& current)
    {
        const auto task = std::find_if (current.tasks.begin(), current.tasks.end(),
                                        [id] (const TaskRegistry::Task& item) { return item.id == id; });
        if (task == current.tasks.end())
            return;

        task->progress = value;
        TaskRegistry::notifyListeners (current);
    });
}

void TaskScope::end()
{
    if (taskId == 0)
        return;

    TaskRegistry::dispatch (taskState, [id = taskId] (TaskRegistry::State& current)
    {
        const auto oldSize = current.tasks.size();
        std::erase_if (current.tasks, [id] (const TaskRegistry::Task& item) { return item.id == id; });
        if (current.tasks.size() != oldSize)
            TaskRegistry::notifyListeners (current);
    });
    taskState.reset();
    taskId = 0;
}
