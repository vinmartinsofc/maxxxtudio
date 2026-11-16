#include "TaskRegistry.h"
#include "ActivityIndicator.h"
#include "DeviceView.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
class RecordingListener final : public TaskRegistry::Listener
{
public:
    explicit RecordingListener (TaskRegistry& taskRegistry) : registry (taskRegistry) {}

    void taskRegistryChanged() override
    {
        const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        calledOnlyOnMessageThread = calledOnlyOnMessageThread
            && manager != nullptr && manager->isThisTheMessageThread();

        for (const auto& task : registry.tasksForTarget ("global"))
            observedProgress.push_back (task.progress);
    }

    bool calledOnlyOnMessageThread = true;
    std::vector<double> observedProgress;

private:
    TaskRegistry& registry;
};
}

TEST_CASE ("TaskScope publishes worker progress on the message thread and ends automatically")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TaskRegistry registry;
    RecordingListener listener (registry);
    registry.addListener (listener);

    std::atomic<bool> workerFinished { false };
    std::thread worker ([&]
    {
        {
            TaskScope task (registry, "Teste", "global", 0.0);
            task.setProgress (0.25);
            std::this_thread::sleep_for (std::chrono::milliseconds (180));
            task.setProgress (0.75);
        }
        workerFinished.store (true, std::memory_order_release);
    });

    auto* manager = juce::MessageManager::getInstance();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
    while ((! workerFinished.load (std::memory_order_acquire)
            || ! registry.tasksForTarget ("global").empty())
           && std::chrono::steady_clock::now() < deadline)
    {
        manager->runDispatchLoopUntil (10);
    }
    worker.join();

    REQUIRE (workerFinished.load (std::memory_order_acquire));
    REQUIRE (registry.tasksForTarget ("global").empty());
    REQUIRE (listener.calledOnlyOnMessageThread);
    REQUIRE (std::find (listener.observedProgress.begin(), listener.observedProgress.end(), 0.25)
             != listener.observedProgress.end());
    REQUIRE (std::find (listener.observedProgress.begin(), listener.observedProgress.end(), 0.75)
             != listener.observedProgress.end());

    registry.removeListener (listener);
}

TEST_CASE ("Task targets match hierarchical prefixes and cancellation is delivered once")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TaskRegistry registry;
    bool cancelled = false;

    const auto pluginTask = registry.begin ("Carregando plugin", "track:3:plugin:2", 0.4,
                                           true, [&] { cancelled = true; });
    const auto otherTask = registry.begin ("Outra faixa", "track:30:plugin:2");

    const auto matching = registry.tasksForTarget ("track:3");
    REQUIRE (matching.size() == 1);
    REQUIRE (matching.front().id == pluginTask);
    REQUIRE (TaskRegistry::targetMatches ("track:3:plugin:2", "track:3"));
    REQUIRE_FALSE (TaskRegistry::targetMatches ("track:30:plugin:2", "track:3"));

    REQUIRE (registry.cancel (pluginTask));
    REQUIRE (cancelled);
    const auto afterCancellation = registry.latestForTarget ("track:3");
    REQUIRE (afterCancellation.has_value());
    REQUIRE_FALSE (afterCancellation->cancelable);

    registry.end (pluginTask);
    registry.end (otherTask);
}

TEST_CASE ("Activity indicator appears only after a task lasts 150 milliseconds")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TaskRegistry registry;
    ActivityIndicator indicator (registry, "global");
    indicator.setBounds (0, 0, 24, 24);
    const auto id = registry.begin ("Teste", "global", -1.0);
    auto* manager = juce::MessageManager::getInstance();

    REQUIRE_FALSE (indicator.isVisible());
    manager->runDispatchLoopUntil (80);
    REQUIRE_FALSE (indicator.isVisible());
    manager->runDispatchLoopUntil (100);
    REQUIRE (indicator.isVisible());

    registry.end (id);
    REQUIRE_FALSE (indicator.isVisible());
}

TEST_CASE ("Plugin task indicator is attached to the affected device slot")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TaskRegistry registry;
    DeviceView deviceView (registry);
    deviceView.setBounds (0, 0, 800, 300);
    const std::vector<DeviceInfo> devices {
        { 1, "Instrument", "plugin:instrument", true, false, 0 },
        { 2, "Effect", "plugin:effect", false, false, 0 }
    };
    deviceView.setDevices (3, "Track 3", devices);
    const int deviceCount = deviceView.getNumChildComponents();

    {
        TaskScope task (registry, "Loading effect", "track:3:plugin:1");
        REQUIRE (deviceView.getNumChildComponents() == deviceCount + 1);
        auto* indicator = deviceView.getChildComponent (deviceCount);
        REQUIRE (indicator->getX() == 366);
        REQUIRE_FALSE (indicator->isVisible());

        juce::MessageManager::getInstance()->runDispatchLoopUntil (180);
        REQUIRE (indicator->isVisible());
    }

    REQUIRE (deviceView.getNumChildComponents() == deviceCount);
}
