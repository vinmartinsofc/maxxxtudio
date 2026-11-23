#pragma once

#include "AudioEngine.h"
#include "ProjectModel.h"
#include <functional>

struct ProjectFileData
{
    ProjectModel::State project;
    std::vector<PluginState> plugins;
    double bpm = 120.0;
    double transportPosition = 0.0;
    bool loopEnabled = true;
    bool metronomeEnabled = false;
    bool computerMidiKeyboardEnabled = false;
    float metronomeGain = 1.0f;
    std::vector<juce::File> missingAudioFiles;
};

class ProjectFile
{
public:
    using ProgressCallback = std::function<void (double)>;

    static juce::Result save (const juce::File&, const ProjectFileData&,
                              ProgressCallback = {});
    static juce::Result load (const juce::File&, ProjectFileData&,
                              ProgressCallback = {});
    static juce::File recoveryDirectory();
    static std::vector<juce::File> recoveryFiles (const juce::File& directory);
    static juce::Result saveRecovery (const juce::File& directory, const ProjectFileData&);
    static void clearIncompleteRecoveryFiles (const juce::File& directory);
    static void clearRecoveryFiles (const juce::File& directory);
};
