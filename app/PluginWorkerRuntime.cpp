#include "PluginWorkerRuntime.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
juce::String objectName (const juce::String& sessionId, const juce::String& suffix)
{
    return "Local\\MaXXXtudioPlugin_" + sessionId + "_" + suffix;
}

HWND findParentWindow (DWORD processId)
{
    struct SearchState
    {
        DWORD processId = 0;
        HWND window = nullptr;
        long long area = 0;
    } state { processId };

    EnumWindows ([] (HWND window, LPARAM userData)
    {
        auto& search = *reinterpret_cast<SearchState*> (userData);
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId (window, &windowProcessId);
        if (windowProcessId != search.processId || ! IsWindowVisible (window)
            || GetWindow (window, GW_OWNER) != nullptr)
            return TRUE;

        RECT bounds {};
        if (! GetWindowRect (window, &bounds))
            return TRUE;
        const auto area = static_cast<long long> (bounds.right - bounds.left)
                        * static_cast<long long> (bounds.bottom - bounds.top);
        if (area > search.area)
        {
            search.window = window;
            search.area = area;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM> (&state));

    return state.window;
}

void copyString (char* destination, int capacity, const juce::String& value)
{
    pluginworker::writeText (destination, capacity, value.toRawUTF8());
}

template <typename Function>
bool runOnMessageThreadAndWait (Function&& function)
{
    auto* messageManager = juce::MessageManager::getInstance();
    if (messageManager == nullptr)
        return false;
    if (messageManager->isThisTheMessageThread())
    {
        function();
        return true;
    }

    juce::WaitableEvent completed;
    if (! juce::MessageManager::callAsync (
            [callback = std::forward<Function> (function), &completed] () mutable
            {
                struct SignalOnExit
                {
                    juce::WaitableEvent& event;
                    ~SignalOnExit() { event.signal(); }
                } signal { completed };
                callback();
            }))
        return false;

    completed.wait();
    return true;
}

class ParameterGestureRelay final : public juce::AudioProcessorParameter::Listener
{
public:
    explicit ParameterGestureRelay (pluginworker::SharedData* sharedData) : shared (sharedData) {}

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override
    {
        if (shared != nullptr
            && parameterIndex >= 0 && parameterIndex < pluginworker::maxParameters)
            InterlockedExchange (gestureIsStarting ? &shared->parameterGestureStarted
                                                   : &shared->parameterGestureEnded,
                                 parameterIndex);
    }

private:
    pluginworker::SharedData* shared = nullptr;
};

class SharedPlayHead final : public juce::AudioPlayHead
{
public:
    explicit SharedPlayHead (pluginworker::SharedData& sharedData) : shared (sharedData) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (InterlockedCompareExchange (&shared.playHeadValid, 0, 0) == 0)
            return {};

        PositionInfo position;
        position.setTimeInSamples (shared.playHeadTimeSamples);
        position.setTimeInSeconds (shared.playHeadTimeSeconds);
        position.setBpm (shared.playHeadBpm);
        juce::AudioPlayHead::TimeSignature signature;
        signature.numerator = shared.playHeadTimeSignatureNumerator;
        signature.denominator = shared.playHeadTimeSignatureDenominator;
        position.setTimeSignature (signature);
        position.setPpqPosition (shared.playHeadPpqPosition);
        position.setPpqPositionOfLastBarStart (shared.playHeadLastBarStartPpq);
        position.setBarCount (shared.playHeadBarCount);
        position.setIsPlaying (InterlockedCompareExchange (&shared.playHeadPlaying, 0, 0) != 0);
        position.setIsRecording (InterlockedCompareExchange (&shared.playHeadRecording, 0, 0) != 0);
        const bool looping = InterlockedCompareExchange (&shared.playHeadLooping, 0, 0) != 0;
        position.setIsLooping (looping);
        if (looping)
        {
            juce::AudioPlayHead::LoopPoints loop;
            loop.ppqStart = shared.playHeadLoopStartPpq;
            loop.ppqEnd = shared.playHeadLoopEndPpq;
            position.setLoopPoints (loop);
        }
        return position;
    }

private:
    pluginworker::SharedData& shared;
};

void setNativeWindowIcon (juce::DocumentWindow& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr)
        return;

    const auto hwnd = static_cast<HWND> (peer->getNativeHandle());
    if (hwnd == nullptr)
        return;

    const auto instance = GetModuleHandleW (nullptr);
    auto largeIcon = static_cast<HICON> (LoadImageW (instance, L"IDI_MAXXXTUDIO", IMAGE_ICON,
                                                      0, 0, LR_DEFAULTSIZE | LR_SHARED));
    if (largeIcon == nullptr)
    {
        juce::Logger::writeToLog ("PluginWorker: Could not load the MaXXXtudio icon.");
        return;
    }

    auto smallIcon = static_cast<HICON> (LoadImageW (instance, L"IDI_MAXXXTUDIO", IMAGE_ICON,
                                                      GetSystemMetrics (SM_CXSMICON),
                                                      GetSystemMetrics (SM_CYSMICON), LR_SHARED));
    if (smallIcon == nullptr)
        smallIcon = largeIcon;

    SendMessageW (hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM> (largeIcon));
    SendMessageW (hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM> (smallIcon));
}

void configurePluginBusesForStereoHost (juce::AudioProcessor& plugin);

class PluginWorkerEditorWindow : public juce::DocumentWindow
{
public:
    PluginWorkerEditorWindow (const juce::String& name,
                              std::unique_ptr<juce::AudioProcessorEditor> editor,
                              DWORD parentProcessId, int editorX, int editorY, bool hasPosition)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        const int editorWidth = std::max (1, editor->getWidth());
        const int editorHeight = std::max (1, editor->getHeight());
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = hasPosition
            ? displays.getDisplayForRect ({ editorX, editorY, 1, 1 })
            : displays.getPrimaryDisplay();
        if (display == nullptr)
            display = displays.getPrimaryDisplay();
        const auto displayArea = display != nullptr
            ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 720);
        const bool editorResizable = editor->isResizable();
        setResizable (editorResizable, false);
        const int horizontalMargin = std::min (16, std::max (0, (displayArea.getWidth() - 1) / 2));
        const int verticalMargin = std::min (16, std::max (0, (displayArea.getHeight() - 1) / 2));
        const auto available = displayArea.reduced (horizontalMargin, verticalMargin);
        const bool needsViewport = editorWidth > available.getWidth()
                                || editorHeight > available.getHeight();

        if (needsViewport)
        {
            auto viewport = std::make_unique<juce::Viewport>();
            viewport->setScrollBarsShown (true, true);
            viewport->setSize (std::min (editorWidth, available.getWidth()),
                               std::min (editorHeight, available.getHeight()));
            viewport->setViewedComponent (editor.release(), true);
            setContentOwned (viewport.release(), true);
        }
        else
        {
            setContentOwned (editor.release(), true);
        }

        auto bounds = displayArea.withSizeKeepingCentre (getWidth(), getHeight());
        if (hasPosition)
        {
            bounds.setX (editorX);
            const int minimumY = displayArea.getY();
            const int maximumY = std::max (minimumY, displayArea.getBottom() - bounds.getHeight());
            bounds.setY (std::clamp (editorY, minimumY, maximumY));
        }
        setBounds (bounds);
        setVisible (true);

        if (const auto parentWindow = findParentWindow (parentProcessId))
            if (const auto* peer = getPeer())
                if (const auto window = static_cast<HWND> (peer->getNativeHandle()))
                {
                    SetWindowLongPtrW (window, GWLP_HWNDPARENT,
                                       reinterpret_cast<LONG_PTR> (parentWindow));
                    SetWindowPos (window, nullptr, 0, 0, 0, 0,
                                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                }
        setNativeWindowIcon (*this);
        toFront (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};
}

class PluginWorkerRuntime::EditorWindow final : public PluginWorkerEditorWindow
{
public:
    EditorWindow (const juce::String& name, std::unique_ptr<juce::AudioProcessorEditor> editor,
                  DWORD parentProcessId, int editorX, int editorY, bool hasPosition)
        : PluginWorkerEditorWindow (name, std::move (editor), parentProcessId,
                                    editorX, editorY, hasPosition) {}
};

PluginWorkerRuntime::PluginWorkerRuntime (const juce::String& sessionId, DWORD parentProcessId)
    : juce::Thread ("Isolated plugin worker"), parentProcessId (parentProcessId)
{
    const auto mappingName = objectName (sessionId, "Data");
    const auto requestName = objectName (sessionId, "Request");
    const auto responseName = objectName (sessionId, "Response");
    mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, mappingName.toWideCharPointer());
    if (mapping == nullptr)
    {
        error = "Could not open plugin shared memory.";
        return;
    }

    shared = static_cast<pluginworker::SharedData*> (
        MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (pluginworker::SharedData)));
    requestEvent = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, requestName.toWideCharPointer());
    responseEvent = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, responseName.toWideCharPointer());
    parentProcess = OpenProcess (SYNCHRONIZE, FALSE, parentProcessId);
    if (shared == nullptr || requestEvent == nullptr || responseEvent == nullptr || parentProcess == nullptr)
    {
        error = "Could not connect to the main process.";
        publishError (error);
        return;
    }

    if (shared->version != pluginworker::protocolVersion)
    {
        error = "Incompatible plugin protocol version.";
        publishError (error);
        return;
    }

    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    const auto xml = juce::XmlDocument::parse (
        juce::String::fromUTF8 (shared->pluginDescription));
    juce::PluginDescription description;
    if (xml == nullptr || ! description.loadFromXml (*xml))
    {
        error = "Invalid plugin description.";
        publishError (error);
        return;
    }

    juce::String loadError;
    processor = formats.createPluginInstance (description, shared->sampleRate, shared->blockSize, loadError);
    if (processor == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError : "Failed to load the plugin.";
        publishError (error);
        return;
    }

    playHead = std::make_unique<SharedPlayHead> (*shared);
    processor->setPlayHead (playHead.get());
    configurePluginBusesForStereoHost (*processor);
    const int inputChannels = processor->getTotalNumInputChannels();
    const int outputChannels = processor->getTotalNumOutputChannels();
    if (inputChannels > pluginworker::maxChannels || outputChannels > pluginworker::maxChannels)
    {
        error = "The plugin exceeds the channel limit supported by the worker.";
        processor.reset();
        publishError (error);
        return;
    }

    processor->setRateAndBufferSizeDetails (shared->sampleRate, shared->blockSize);
    processor->prepareToPlay (shared->sampleRate, shared->blockSize);
    audioBuffer.setSize (std::max (1, std::max (inputChannels, outputChannels)),
                         pluginworker::maxSamples);
    shared->numInputs = inputChannels;
    shared->numOutputs = outputChannels;
    shared->hasEditor = processor->hasEditor() ? 1 : 0;
    shared->acceptsMidi = processor->acceptsMidi() ? 1 : 0;
    shared->producesMidi = processor->producesMidi() ? 1 : 0;
    shared->midiEffect = processor->isMidiEffect() ? 1 : 0;
    copyString (shared->pluginName, pluginworker::pluginNameLength, processor->getName());

    const auto parameters = processor->getParameters();
    shared->numParameters = std::min (parameters.size(), pluginworker::maxParameters);
    for (int i = 0; i < shared->numParameters; ++i)
    {
        auto* parameter = parameters.getUnchecked (i);
        copyString (shared->parameterNames[i], pluginworker::parameterNameLength, parameter->getName (64));
        const auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        copyString (shared->parameterIds[i], pluginworker::parameterIdLength,
                    parameterWithId != nullptr ? parameterWithId->paramID : juce::String());
        InterlockedExchange (&shared->parameterValues[i],
                             pluginworker::floatToBits (parameter->getValue()));
    }

    parameterGestureListener = std::make_unique<ParameterGestureRelay> (shared);
    for (auto* parameter : parameters)
        if (parameter != nullptr && parameter->isAutomatable())
            parameter->addListener (parameterGestureListener.get());

    ready = true;
    InterlockedExchange (&shared->state, 1);
    SetEvent (responseEvent);
    startThread();
}

PluginWorkerRuntime::~PluginWorkerRuntime()
{
    signalThreadShouldExit();
    if (requestEvent != nullptr)
        SetEvent (requestEvent);
    stopThread (2000);
    closeEditor();
    if (processor != nullptr && parameterGestureListener != nullptr)
        for (auto* parameter : processor->getParameters())
            if (parameter != nullptr)
                parameter->removeListener (parameterGestureListener.get());
    parameterGestureListener.reset();
    if (processor != nullptr)
    {
        processor->setPlayHead (nullptr);
        processor->releaseResources();
    }
    if (shared != nullptr)
        UnmapViewOfFile (shared);
    if (parentProcess != nullptr) CloseHandle (parentProcess);
    if (responseEvent != nullptr) CloseHandle (responseEvent);
    if (requestEvent != nullptr) CloseHandle (requestEvent);
    if (mapping != nullptr) CloseHandle (mapping);
}

void PluginWorkerRuntime::publishError (const juce::String& message)
{
    if (shared != nullptr)
    {
        copyString (shared->error, pluginworker::errorLength, message);
        InterlockedExchange (&shared->state, -1);
        if (responseEvent != nullptr)
            SetEvent (responseEvent);
    }
    juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
}

namespace
{
void configurePluginBusesForStereoHost (juce::AudioProcessor& plugin)
{
    for (const bool isInput : { true, false })
    {
        for (int busIndex = plugin.getBusCount (isInput) - 1; busIndex > 0; --busIndex)
            if (auto* bus = plugin.getBus (isInput, busIndex); bus != nullptr && bus->isEnabled())
                bus->enable (false);

        if (auto* mainBus = plugin.getBus (isInput, 0);
            mainBus != nullptr && mainBus->isEnabled() && mainBus->getNumberOfChannels() > 2)
            mainBus->setNumberOfChannels (2);
    }
}
}

void PluginWorkerRuntime::run()
{
    HANDLE handles[] { requestEvent, parentProcess };
    while (! threadShouldExit())
    {
        const auto result = WaitForMultipleObjects (2, handles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1)
        {
            juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
            break;
        }
        if (result != WAIT_OBJECT_0)
        {
            publishError ("Communication with the main process failed.");
            break;
        }

        const auto command = static_cast<pluginworker::Command> (
            InterlockedExchange (&shared->command,
                                 static_cast<LONG> (pluginworker::Command::none)));
        if (command == pluginworker::Command::shutdown)
            break;

        processRequest (command);
        SetEvent (responseEvent);
    }
}

void PluginWorkerRuntime::processRequest (pluginworker::Command command)
{
    InterlockedExchange (&shared->result, 0);
    switch (command)
    {
        case pluginworker::Command::process:
        {
            const int samples = std::clamp (static_cast<int> (shared->audioSamples), 0,
                                            pluginworker::maxSamples);
            const int channels = std::max (1, std::max (static_cast<int> (shared->numInputs),
                                                        static_cast<int> (shared->numOutputs)));
            audioBuffer.setSize (channels, samples, false, false, true);
            audioBuffer.clear();
            for (int channel = 0; channel < std::min (static_cast<int> (shared->numInputs),
                                                      pluginworker::maxChannels); ++channel)
                std::copy_n (shared->audio[channel], samples, audioBuffer.getWritePointer (channel));

            juce::MidiBuffer midi;
            const int midiCount = std::clamp (static_cast<int> (shared->midiCount), 0,
                                              pluginworker::maxMidiEvents);
            for (int i = 0; i < midiCount; ++i)
            {
                const auto& event = shared->midi[i];
                if (event.size > 0 && event.size <= pluginworker::maxMidiBytes)
                    midi.addEvent (event.data, event.size,
                                   std::clamp (event.samplePosition, 0, std::max (0, samples - 1)));
            }

            const int parameterCount = std::min (static_cast<int> (shared->numParameters),
                                                 processor->getParameters().size());
            for (int i = 0; i < parameterCount; ++i)
            {
                auto* parameter = processor->getParameters().getUnchecked (i);
                if (InterlockedExchange (&shared->parameterChanged[i], 0) != 0)
                {
                    const auto value = pluginworker::bitsToFloat (
                        InterlockedCompareExchange (&shared->parameterValues[i], 0, 0));
                    if (std::isfinite (value))
                        parameter->setValue (value);
                }
            }

            processor->processBlock (audioBuffer, midi);
            for (int channel = 0; channel < std::min (static_cast<int> (shared->numOutputs),
                                                      pluginworker::maxChannels); ++channel)
                std::copy_n (audioBuffer.getReadPointer (channel), samples, shared->audio[channel]);
            for (int i = 0; i < parameterCount; ++i)
                InterlockedExchange (&shared->parameterValues[i],
                                     pluginworker::floatToBits (
                                         processor->getParameters().getUnchecked (i)->getValue()));
            InterlockedExchange (&shared->result, 1);
            break;
        }
        case pluginworker::Command::prepare:
            if (shared->sampleRate > 0.0 && shared->blockSize > 0
                && shared->blockSize <= pluginworker::maxSamples)
            {
                processor->releaseResources();
                processor->setRateAndBufferSizeDetails (shared->sampleRate, shared->blockSize);
                processor->prepareToPlay (shared->sampleRate, shared->blockSize);
                audioBuffer.setSize (std::max (1, std::max (processor->getTotalNumInputChannels(),
                                                             processor->getTotalNumOutputChannels())),
                                     pluginworker::maxSamples);
                InterlockedExchange (&shared->result, 1);
            }
            break;
        case pluginworker::Command::releaseResources:
            processor->releaseResources();
            InterlockedExchange (&shared->result, 1);
            break;
        case pluginworker::Command::getState:
        {
            juce::MemoryBlock state;
            if (! runOnMessageThreadAndWait (
                    [this, &state] { processor->getStateInformation (state); }))
            {
                copyString (shared->error, pluginworker::errorLength,
                            "Could not query plugin state on the message thread.");
                break;
            }
            const auto stateFile = juce::File::createFileWithoutCheckingPath (
                juce::String::fromUTF8 (shared->stateFilePath));
            if (stateFile.existsAsFile() && ! stateFile.deleteFile())
            {
                copyString (shared->error, pluginworker::errorLength,
                            "Could not replace the previous plugin state file.");
                break;
            }
            juce::FileOutputStream stream (stateFile);
            if (state.getSize() <= static_cast<std::size_t> (std::numeric_limits<int>::max())
                && stream.openedOk()
                && (state.getSize() == 0 || stream.write (state.getData(), state.getSize())))
            {
                stream.flush();
                if (stateFile.getSize() == static_cast<juce::int64> (state.getSize()))
                {
                    shared->stateBytes = static_cast<LONG> (state.getSize());
                    InterlockedExchange (&shared->result, 1);
                }
                else
                    copyString (shared->error, pluginworker::errorLength,
                                "The plugin state file was not written completely.");
            }
            else
            {
                copyString (shared->error, pluginworker::errorLength,
                            "Could not save plugin state.");
            }
            break;
        }
        case pluginworker::Command::setState:
        {
            juce::MemoryBlock state;
            const auto stateFile = juce::File::createFileWithoutCheckingPath (
                juce::String::fromUTF8 (shared->stateFilePath));
            if (shared->stateBytes >= 0 && stateFile.loadFileAsData (state)
                && state.getSize() == static_cast<std::size_t> (shared->stateBytes))
            {
                if (runOnMessageThreadAndWait ([this, &state]
                    {
                        processor->setStateInformation (
                            state.getData(), static_cast<int> (state.getSize()));
                        const auto parameters = processor->getParameters();
                        const int count = std::min (parameters.size(),
                                                    static_cast<int> (shared->numParameters));
                        for (int i = 0; i < count; ++i)
                            if (auto* parameter = parameters.getUnchecked (i))
                            {
                                InterlockedExchange (&shared->parameterValues[i],
                                    pluginworker::floatToBits (parameter->getValue()));
                                InterlockedExchange (&shared->parameterChanged[i], 0);
                            }
                    }))
                    InterlockedExchange (&shared->result, 1);
                else
                    copyString (shared->error, pluginworker::errorLength,
                                "Could not restore plugin state on the message thread.");
            }
            break;
        }
        case pluginworker::Command::showEditor:
        {
            const int editorX = shared->editorX;
            const int editorY = shared->editorY;
            const bool hasPosition = shared->editorPositionValid != 0;
            juce::MessageManager::callAsync (
                [this, editorX, editorY, hasPosition] { openEditor (editorX, editorY, hasPosition); });
            InterlockedExchange (&shared->result, 1);
            break;
        }
        case pluginworker::Command::hideEditor:
            juce::MessageManager::callAsync ([this] { closeEditor(); });
            InterlockedExchange (&shared->result, 1);
            break;
        case pluginworker::Command::none:
        case pluginworker::Command::shutdown:
            break;
    }
}

void PluginWorkerRuntime::openEditor (int editorX, int editorY, bool hasPosition)
{
    if (editorWindow != nullptr)
    {
        editorWindow->setVisible (true);
        editorWindow->toFront (true);
        return;
    }

    if (processor == nullptr || ! processor->hasEditor())
        return;

    if (auto* editor = processor->createEditorAndMakeActive())
    {
        editorWindow = std::make_unique<EditorWindow> (
            processor->getName(), std::unique_ptr<juce::AudioProcessorEditor> (editor),
            parentProcessId, editorX, editorY, hasPosition);
        editorWindow->setVisible (true);
        editorWindow->toFront (true);
    }
}

void PluginWorkerRuntime::closeEditor()
{
    editorWindow.reset();
}

#endif
