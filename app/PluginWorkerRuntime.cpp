#include "PluginWorkerRuntime.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
juce::String objectName (const juce::String& sessionId, const juce::String& suffix)
{
    return "Local\\ProjectFreedomPlugin_" + sessionId + "_" + suffix;
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

void configurePluginBusesForStereoHost (juce::AudioProcessor& plugin);

class PluginWorkerEditorWindow : public juce::DocumentWindow
{
public:
    PluginWorkerEditorWindow (const juce::String& name,
                              std::unique_ptr<juce::AudioProcessorEditor> editor,
                              DWORD parentProcessId)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        const int editorWidth = std::max (1, editor->getWidth());
        const int editorHeight = std::max (1, editor->getHeight());
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
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

        setBounds (displayArea.withSizeKeepingCentre (getWidth(), getHeight()));
        setVisible (true);
        setAlwaysOnTop (true);

        if (const auto parentWindow = findParentWindow (parentProcessId))
            if (const auto* peer = getPeer())
                if (const auto window = static_cast<HWND> (peer->getNativeHandle()))
                {
                    SetWindowLongPtrW (window, GWLP_HWNDPARENT,
                                       reinterpret_cast<LONG_PTR> (parentWindow));
                    SetWindowPos (window, nullptr, 0, 0, 0, 0,
                                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                }
        toFront (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};
}

class PluginWorkerRuntime::EditorWindow final : public PluginWorkerEditorWindow
{
public:
    EditorWindow (const juce::String& name, std::unique_ptr<juce::AudioProcessorEditor> editor,
                  DWORD parentProcessId)
        : PluginWorkerEditorWindow (name, std::move (editor), parentProcessId) {}
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
        error = "Nao foi possivel abrir a memoria compartilhada do plugin.";
        return;
    }

    shared = static_cast<pluginworker::SharedData*> (
        MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (pluginworker::SharedData)));
    requestEvent = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, requestName.toWideCharPointer());
    responseEvent = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, responseName.toWideCharPointer());
    parentProcess = OpenProcess (SYNCHRONIZE, FALSE, parentProcessId);
    if (shared == nullptr || requestEvent == nullptr || responseEvent == nullptr || parentProcess == nullptr)
    {
        error = "Nao foi possivel conectar ao processo principal.";
        publishError (error);
        return;
    }

    if (shared->version != pluginworker::protocolVersion)
    {
        error = "Versao incompativel do protocolo de plugins.";
        publishError (error);
        return;
    }

    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    const auto xml = juce::XmlDocument::parse (
        juce::String::fromUTF8 (shared->pluginDescription));
    juce::PluginDescription description;
    if (xml == nullptr || ! description.loadFromXml (*xml))
    {
        error = "Descricao do plugin invalida.";
        publishError (error);
        return;
    }

    juce::String loadError;
    processor = formats.createPluginInstance (description, shared->sampleRate, shared->blockSize, loadError);
    if (processor == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError : "Falha ao carregar o plugin.";
        publishError (error);
        return;
    }

    configurePluginBusesForStereoHost (*processor);
    const int inputChannels = processor->getTotalNumInputChannels();
    const int outputChannels = processor->getTotalNumOutputChannels();
    if (inputChannels > pluginworker::maxChannels || outputChannels > pluginworker::maxChannels)
    {
        error = "O plugin excede o limite de canais suportado pelo worker.";
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
    if (processor != nullptr)
        processor->releaseResources();
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
            break;
        if (result != WAIT_OBJECT_0)
        {
            publishError ("Falha de comunicacao com o processo principal.");
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
            processor->getStateInformation (state);
            const auto stateFile = juce::File::createFileWithoutCheckingPath (
                juce::String::fromUTF8 (shared->stateFilePath));
            juce::FileOutputStream stream (stateFile);
            if (state.getSize() <= static_cast<std::size_t> (std::numeric_limits<int>::max())
                && stream.openedOk()
                && (state.getSize() == 0 || stream.write (state.getData(), state.getSize())))
            {
                shared->stateBytes = static_cast<LONG> (state.getSize());
                stream.flush();
                InterlockedExchange (&shared->result, 1);
            }
            else
            {
                copyString (shared->error, pluginworker::errorLength,
                            "Nao foi possivel salvar o estado do plugin.");
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
                processor->setStateInformation (state.getData(), shared->stateBytes);
                InterlockedExchange (&shared->result, 1);
            }
            break;
        }
        case pluginworker::Command::showEditor:
            juce::MessageManager::callAsync ([this] { openEditor(); });
            InterlockedExchange (&shared->result, 1);
            break;
        case pluginworker::Command::hideEditor:
            juce::MessageManager::callAsync ([this] { closeEditor(); });
            InterlockedExchange (&shared->result, 1);
            break;
        case pluginworker::Command::none:
        case pluginworker::Command::shutdown:
            break;
    }
}

void PluginWorkerRuntime::openEditor()
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
            processor->getName(), std::unique_ptr<juce::AudioProcessorEditor> (editor), parentProcessId);
        editorWindow->setVisible (true);
        editorWindow->toFront (true);
    }
}

void PluginWorkerRuntime::closeEditor()
{
    editorWindow.reset();
}

#endif
