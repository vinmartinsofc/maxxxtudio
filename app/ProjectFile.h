#pragma once

#include "AudioEngine.h"
#include "ProjectModel.h"

struct ProjectFileData
{
    ProjectModel::State project;
    std::vector<PluginState> plugins;
    double bpm = 120.0;
    double transportPosition = 0.0;
    bool loopEnabled = true;
    bool metronomeEnabled = false;
    float metronomeGain = 1.0f;
    std::vector<juce::File> missingAudioFiles;
};

class ProjectFile
{
public:
    static juce::Result save (const juce::File&, const ProjectModel&, AudioEngine&,
                              double transportPosition);
    static juce::Result load (const juce::File&, ProjectFileData&);
};
