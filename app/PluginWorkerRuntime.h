#pragma once

#include "PluginWorkerProtocol.h"

#if defined(_WIN32)

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

class PluginWorkerRuntime final : private juce::Thread
{
public:
    PluginWorkerRuntime (const juce::String& sessionId, DWORD parentProcessId);
    ~PluginWorkerRuntime() override;

    bool isReady() const noexcept { return ready; }
    const juce::String& startupError() const noexcept { return error; }

private:
    class EditorWindow;
    void run() override;
    void processRequest (pluginworker::Command);
    void publishError (const juce::String&);
    void openEditor (int editorX = -1, int editorY = -1, bool hasPosition = false);
    void closeEditor();

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    std::unique_ptr<EditorWindow> editorWindow;
    std::unique_ptr<juce::AudioProcessorParameter::Listener> parameterGestureListener;
    juce::AudioBuffer<float> audioBuffer;
    HANDLE mapping = nullptr, requestEvent = nullptr, responseEvent = nullptr, parentProcess = nullptr;
    pluginworker::SharedData* shared = nullptr;
    DWORD parentProcessId = 0;
    juce::String error;
    bool ready = false;
};

#endif
