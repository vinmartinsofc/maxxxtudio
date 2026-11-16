#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>

class TaskRegistry;
class TaskScope;

// Detecta plugins nos formatos registrados e persiste a lista em disco.
// O trabalho pesado roda em um worker, enquanto o progresso vai para o TaskRegistry.
class PluginScanner
{
public:
    PluginScanner (juce::AudioPluginFormatManager&, TaskRegistry&);
    ~PluginScanner();

    juce::KnownPluginList& list() { return knownList; }
    std::vector<juce::File> searchRoots (const juce::String& formatName) const;
    std::vector<juce::File> customSearchPaths (const juce::String& formatName) const;
    bool supportsPluginFormat (const juce::String& formatName) const;
    juce::Result addCustomSearchPath (const juce::String& formatName, const juce::File& directory);
    juce::Result removeCustomSearchPath (const juce::String& formatName, const juce::File& directory);
    void startScan();
    void cancelScan();
    void stopScan();
    bool isScanning() const { return activeScan != nullptr; }

    std::function<void()> onStarted;                                  // message thread
    std::function<void (const juce::String& currentPlugin)> onProgress; // message thread
    std::function<void (bool cancelled)> onFinished;                  // message thread

private:
    struct ScanState;

    void runScan (const std::shared_ptr<ScanState>&,
                  const std::shared_ptr<TaskScope>&,
                  const std::shared_ptr<std::atomic<bool>>&);
    static juce::File dataDirectory();
    void saveList();
    void loadSearchPaths();
    bool saveSearchPaths() const;
    juce::FileSearchPath searchPathsForFormat (juce::AudioPluginFormat&) const;

    juce::AudioPluginFormatManager& formatManager;
    TaskRegistry& taskRegistry;
    juce::KnownPluginList knownList;
    mutable juce::CriticalSection customPathsLock;
    std::map<std::string, std::vector<std::string>> customPathsByFormat;
    juce::ThreadPool scanPool { 1 };
    std::shared_ptr<ScanState> activeScan;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    bool rescanPending = false;
};
