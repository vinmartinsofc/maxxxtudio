#include "RemotePluginProcessor.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#ifndef PROJECT_FREEDOM_PLUGIN_WORKER_PATH
 #define PROJECT_FREEDOM_PLUGIN_WORKER_PATH ""
#endif

namespace
{
juce::CriticalSection workerProcessIdsLock;
std::set<DWORD> workerProcessIds;

juce::String objectName (const juce::String& sessionId, const juce::String& suffix)
{
    return "Local\\ProjectFreedomPlugin_" + sessionId + "_" + suffix;
}

juce::String quoteArgument (const juce::String& argument)
{
    return "\"" + argument.replace ("\"", "\\\"") + "\"";
}
}

bool RemotePluginProcessor::isPluginEditorForeground()
{
    const auto foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == nullptr)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId (foregroundWindow, &processId);
    if (processId == 0)
        return false;

    const juce::ScopedLock lock (workerProcessIdsLock);
    return workerProcessIds.contains (processId);
}

class RemoteParameter final : public juce::AudioProcessorParameterWithID
{
public:
    RemoteParameter (RemotePluginProcessor& owner, int parameterIndex,
                     const juce::String& identifier, const juce::String& name)
        : juce::AudioProcessorParameterWithID (
              juce::ParameterID (identifier.isNotEmpty() ? identifier
                                                          : "parameter_" + juce::String (parameterIndex), 1),
              name),
          processor (owner), index (parameterIndex)
    {
    }

    float getValue() const override { return processor.readParameter (index); }
    void setValue (float value) override { processor.updateParameter (index, value); }
    float getDefaultValue() const override { return 0.0f; }
    juce::String getText (float value, int maximumLength) const override
    {
        return juce::String (value, 3).substring (0, maximumLength);
    }
    float getValueForText (const juce::String& text) const override
    {
        return std::clamp (text.getFloatValue(), 0.0f, 1.0f);
    }

private:
    RemotePluginProcessor& processor;
    const int index;
};

class RemotePluginProcessor::RemoteEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RemoteEditor (RemotePluginProcessor& processor)
        : AudioProcessorEditor (processor), owner (processor)
    {
        addAndMakeVisible (description);
        description.setText ("A interface do plugin e executada em um processo isolado.", juce::dontSendNotification);
        description.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (openButton);
        openButton.setButtonText ("Abrir interface do plugin");
        openButton.onClick = [this]
        {
            if (! owner.hasFailed())
                owner.sendCommand (pluginworker::Command::showEditor, 1000);
        };
        setSize (320, 100);
    }

    void resized() override
    {
        description.setBounds (10, 10, getWidth() - 20, 32);
        openButton.setBounds (10, 52, getWidth() - 20, 34);
    }

private:
    RemotePluginProcessor& owner;
    juce::Label description;
    juce::TextButton openButton;
};

RemotePluginProcessor::RemotePluginProcessor (const juce::PluginDescription& description,
                                              double rate, int blockSize)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), ! description.isInstrument)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      pluginName (description.name), instrument (description.isInstrument),
      sampleBlockSize (std::clamp (blockSize, 1, pluginworker::maxSamples)),
      sampleRate (rate)
{
}

bool RemotePluginProcessor::showEditor()
{
    return hasEditorFlag && sendCommand (pluginworker::Command::showEditor, 1000);
}

std::unique_ptr<RemotePluginProcessor> RemotePluginProcessor::create (
    const juce::PluginDescription& description, double sampleRate, int blockSize,
    juce::String& error)
{
#if ! defined(_WIN32)
    juce::ignoreUnused (description, sampleRate, blockSize);
    error = "O isolamento de plugins esta disponivel apenas no Windows.";
    return nullptr;
#else
    auto processor = std::unique_ptr<RemotePluginProcessor> (
        new RemotePluginProcessor (description, sampleRate, blockSize));
    if (! processor->startWorker (description, error))
        return nullptr;
    return processor;
#endif
}

bool RemotePluginProcessor::startWorker (const juce::PluginDescription& description, juce::String& error)
{
    sessionId = juce::Uuid().toString().removeCharacters ("{}");
    const auto mappingName = objectName (sessionId, "Data");
    const auto requestName = objectName (sessionId, "Request");
    const auto responseName = objectName (sessionId, "Response");
    const auto mappingSize = static_cast<std::uint64_t> (sizeof (pluginworker::SharedData));
    mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 static_cast<DWORD> (mappingSize >> 32),
                                 static_cast<DWORD> (mappingSize & 0xffffffffu),
                                 mappingName.toWideCharPointer());
    requestEvent = CreateEventW (nullptr, FALSE, FALSE, requestName.toWideCharPointer());
    responseEvent = CreateEventW (nullptr, FALSE, FALSE, responseName.toWideCharPointer());
    if (mapping == nullptr || requestEvent == nullptr || responseEvent == nullptr)
    {
        error = "Nao foi possivel criar o canal de comunicacao do plugin.";
        return false;
    }

    shared = static_cast<pluginworker::SharedData*> (
        MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (pluginworker::SharedData)));
    if (shared == nullptr)
    {
        error = "Nao foi possivel mapear a memoria compartilhada do plugin.";
        return false;
    }

    std::memset (shared, 0, sizeof (*shared));
    shared->version = pluginworker::protocolVersion;
    shared->sampleRate = sampleRate;
    shared->blockSize = sampleBlockSize;
    const auto stateFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("project-freedom-plugin-state-" + sessionId + ".bin");
    pluginworker::writeText (shared->stateFilePath, pluginworker::statePathLength,
                             stateFile.getFullPathName().toRawUTF8());
    const auto descriptionXml = description.createXml();
    if (descriptionXml == nullptr)
    {
        error = "Nao foi possivel preparar a descricao do plugin.";
        return false;
    }
    const auto descriptionText = descriptionXml->toString();
    if (descriptionText.length() >= pluginworker::descriptionLength)
    {
        error = "A descricao do plugin e grande demais para o protocolo.";
        return false;
    }
    pluginworker::writeText (shared->pluginDescription, pluginworker::descriptionLength,
                             descriptionText.toRawUTF8());

    auto workerExecutable = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                .getSiblingFile ("Project Freedom Plugin Worker.exe");
    if (! workerExecutable.existsAsFile())
        workerExecutable = juce::File (PROJECT_FREEDOM_PLUGIN_WORKER_PATH);
    if (! workerExecutable.existsAsFile())
    {
        error = "O executavel de isolamento de plugins nao foi encontrado: "
              + workerExecutable.getFullPathName();
        return false;
    }

    const auto commandLine = quoteArgument (workerExecutable.getFullPathName())
                           + " --plugin-worker " + sessionId + " "
                           + juce::String (GetCurrentProcessId());
    const auto* commandChars = commandLine.getCharPointer().getAddress();
    std::vector<wchar_t> mutableCommand (commandChars, commandChars + commandLine.length());
    mutableCommand.push_back (L'\0');

    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION process {};
    const bool created = CreateProcessW (workerExecutable.getFullPathName().toWideCharPointer(),
                                         mutableCommand.data(), nullptr, nullptr, FALSE,
                                         CREATE_NO_WINDOW, nullptr,
                                         workerExecutable.getParentDirectory().getFullPathName().toWideCharPointer(),
                                         &startup, &process) != FALSE;
    if (! created)
    {
        error = "Nao foi possivel iniciar o processo isolado do plugin.";
        return false;
    }
    CloseHandle (process.hThread);
    workerProcess = process.hProcess;
    workerProcessId = process.dwProcessId;
    {
        const juce::ScopedLock lock (workerProcessIdsLock);
        workerProcessIds.insert (workerProcessId);
    }

    HANDLE handles[] { responseEvent, workerProcess };
    const auto waitResult = WaitForMultipleObjects (2, handles, FALSE, 60000);
    if (waitResult != WAIT_OBJECT_0 || InterlockedCompareExchange (&shared->state, 0, 0) != 1)
    {
        const auto* workerError = shared->error[0] != '\0' ? shared->error : "O plugin nao iniciou a tempo.";
        error = juce::String::fromUTF8 (workerError);
        if (workerProcess != nullptr)
        {
            TerminateProcess (workerProcess, 1);
            WaitForSingleObject (workerProcess, 1000);
        }
        return false;
    }

    pluginName = juce::String::fromUTF8 (shared->pluginName);
    hasEditorFlag = shared->hasEditor != 0;
    acceptsMidiFlag = shared->acceptsMidi != 0;
    producesMidiFlag = shared->producesMidi != 0;
    midiEffectFlag = shared->midiEffect != 0;
    const int parameterCount = std::clamp (static_cast<int> (shared->numParameters), 0,
                                           pluginworker::maxParameters);
    for (int i = 0; i < parameterCount; ++i)
        addParameter (new RemoteParameter (*this, i,
                                           juce::String::fromUTF8 (shared->parameterIds[i]),
                                           juce::String::fromUTF8 (shared->parameterNames[i])));
    return true;
}

RemotePluginProcessor::~RemotePluginProcessor()
{
#if defined(_WIN32)
    if (workerProcessId != 0)
    {
        const juce::ScopedLock lock (workerProcessIdsLock);
        workerProcessIds.erase (workerProcessId);
    }
    if (workerProcess != nullptr)
    {
        if (WaitForSingleObject (workerProcess, 0) == WAIT_TIMEOUT)
            TerminateProcess (workerProcess, 0);
        WaitForSingleObject (workerProcess, 2000);
        CloseHandle (workerProcess);
    }
    if (shared != nullptr) UnmapViewOfFile (shared);
    if (responseEvent != nullptr) CloseHandle (responseEvent);
    if (requestEvent != nullptr) CloseHandle (requestEvent);
    if (mapping != nullptr) CloseHandle (mapping);
    if (sessionId.isNotEmpty())
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("project-freedom-plugin-state-" + sessionId + ".bin").deleteFile();
#endif
}

void RemotePluginProcessor::prepareToPlay (double rate, int maximumBlockSize)
{
#if defined(_WIN32)
    if (rate <= 0.0 || maximumBlockSize <= 0
        || maximumBlockSize > pluginworker::maxSamples || hasFailed())
        return;
    if (std::abs (sampleRate - rate) < 0.001
        && sampleBlockSize == maximumBlockSize && resourcesPrepared)
        return;

    sampleRate = rate;
    sampleBlockSize = maximumBlockSize;
    shared->sampleRate = rate;
    shared->blockSize = maximumBlockSize;
    resourcesPrepared = sendCommand (pluginworker::Command::prepare, 10000);
#else
    juce::ignoreUnused (rate, maximumBlockSize);
#endif
}

void RemotePluginProcessor::releaseResources()
{
#if defined(_WIN32)
    if (resourcesPrepared && ! hasFailed())
        sendCommand (pluginworker::Command::releaseResources, 5000);
    resourcesPrepared = false;
#endif
}

bool RemotePluginProcessor::sendCommand (pluginworker::Command command, int timeoutMs)
{
#if defined(_WIN32)
    if (shared == nullptr || requestEvent == nullptr || responseEvent == nullptr
        || workerProcess == nullptr || hasFailed())
        return false;

    const juce::ScopedLock lock (requestLock);
    if (WaitForSingleObject (workerProcess, 0) != WAIT_TIMEOUT)
    {
        failWorker();
        return false;
    }

    ResetEvent (responseEvent);
    InterlockedExchange (&shared->command, static_cast<LONG> (command));
    SetEvent (requestEvent);
    HANDLE handles[] { responseEvent, workerProcess };
    const auto result = WaitForMultipleObjects (2, handles, FALSE, static_cast<DWORD> (timeoutMs));
    if (result != WAIT_OBJECT_0
        || InterlockedCompareExchange (&shared->result, 0, 0) == 0)
    {
        failWorker();
        return false;
    }
    return true;
#else
    juce::ignoreUnused (command, timeoutMs);
    return false;
#endif
}

void RemotePluginProcessor::failWorker()
{
    if (failed.exchange (true, std::memory_order_acq_rel))
        return;
#if defined(_WIN32)
    if (workerProcess != nullptr && WaitForSingleObject (workerProcess, 0) == WAIT_TIMEOUT)
        TerminateProcess (workerProcess, 1);
#endif
    if (onFailure)
    {
        const auto callback = onFailure;
        const auto name = pluginName;
        if (! juce::MessageManager::callAsync ([callback, name] { callback (name); }))
            juce::Logger::writeToLog ("RemotePluginProcessor: nao foi possivel notificar a falha do plugin.");
    }
}

void RemotePluginProcessor::updateParameter (int index, float value) noexcept
{
#if defined(_WIN32)
    if (shared != nullptr && index >= 0 && index < shared->numParameters)
    {
        InterlockedExchange (&shared->parameterValues[index],
                             pluginworker::floatToBits (std::clamp (value, 0.0f, 1.0f)));
        InterlockedExchange (&shared->parameterChanged[index], 1);
    }
#else
    juce::ignoreUnused (index, value);
#endif
}

float RemotePluginProcessor::readParameter (int index) const noexcept
{
#if defined(_WIN32)
    if (shared != nullptr && index >= 0 && index < shared->numParameters)
        return pluginworker::bitsToFloat (
            InterlockedCompareExchange (&shared->parameterValues[index], 0, 0));
#else
    juce::ignoreUnused (index);
#endif
    return 0.0f;
}

void RemotePluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
#if defined(_WIN32)
    if (hasFailed())
    {
        if (instrument)
            buffer.clear();
        return;
    }

    const juce::ScopedLock lock (requestLock);
    if (shared == nullptr || workerProcess == nullptr
        || WaitForSingleObject (workerProcess, 0) != WAIT_TIMEOUT)
    {
        failWorker();
        if (instrument)
            buffer.clear();
        return;
    }

    const int samples = std::min (buffer.getNumSamples(), pluginworker::maxSamples);
    shared->audioSamples = samples;
    if (shared->numInputs == 1)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const float left = buffer.getNumChannels() > 0 ? buffer.getSample (0, sample) : 0.0f;
            const float right = buffer.getNumChannels() > 1 ? buffer.getSample (1, sample) : left;
            shared->audio[0][sample] = 0.5f * (left + right);
        }
    }
    else
    {
        for (int channel = 0; channel < shared->numInputs; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                shared->audio[channel][sample] = channel < buffer.getNumChannels()
                    ? buffer.getSample (channel, sample) : 0.0f;
    }

    shared->midiCount = 0;
    for (const auto metadata : midi)
    {
        if (shared->midiCount >= pluginworker::maxMidiEvents)
            break;
        const auto message = metadata.getMessage();
        const int size = std::min (message.getRawDataSize(), pluginworker::maxMidiBytes);
        if (size <= 0)
            continue;
        auto& event = shared->midi[shared->midiCount++];
        event.samplePosition = metadata.samplePosition;
        event.size = size;
        std::memcpy (event.data, message.getRawData(), static_cast<std::size_t> (size));
    }

    const double blockDurationMs = sampleRate > 0.0
        ? 1000.0 * samples / sampleRate : 20.0;
    const int timeoutMs = std::clamp (static_cast<int> (std::ceil (blockDurationMs * 4.0)), 20, 250);
    ResetEvent (responseEvent);
    InterlockedExchange (&shared->command, static_cast<LONG> (pluginworker::Command::process));
    SetEvent (requestEvent);
    HANDLE handles[] { responseEvent, workerProcess };
    if (WaitForMultipleObjects (2, handles, FALSE, static_cast<DWORD> (timeoutMs)) != WAIT_OBJECT_0
        || InterlockedCompareExchange (&shared->result, 0, 0) == 0)
    {
        failWorker();
        if (instrument)
            buffer.clear();
        return;
    }

    const int outputChannels = std::min (static_cast<int> (shared->numOutputs),
                                         pluginworker::maxChannels);
    if (outputChannels == 1)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom (channel, 0, shared->audio[0], samples);
    }
    else
    {
        for (int channel = 0; channel < std::min (buffer.getNumChannels(), outputChannels); ++channel)
            buffer.copyFrom (channel, 0, shared->audio[channel], samples);
        for (int channel = outputChannels; channel < buffer.getNumChannels(); ++channel)
            buffer.clear (channel, 0, samples);
    }
#else
    juce::ignoreUnused (midi);
    if (instrument)
        buffer.clear();
#endif
}

juce::AudioProcessorEditor* RemotePluginProcessor::createEditor()
{
    if (! hasEditorFlag || hasFailed())
        return nullptr;
    showEditor();
    return new RemoteEditor (*this);
}

void RemotePluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
#if defined(_WIN32)
    if (! sendCommand (pluginworker::Command::getState, 30000))
    {
        const juce::ScopedLock lock (stateLock);
        destination.replaceAll (cachedState.getData(), cachedState.getSize());
        return;
    }
    const auto stateFile = juce::File::createFileWithoutCheckingPath (
        juce::String::fromUTF8 (shared->stateFilePath));
    if (! stateFile.loadFileAsData (destination))
    {
        juce::Logger::writeToLog ("RemotePluginProcessor: nao foi possivel ler o estado do plugin.");
        const juce::ScopedLock lock (stateLock);
        destination.replaceAll (cachedState.getData(), cachedState.getSize());
        return;
    }
    const juce::ScopedLock lock (stateLock);
    cachedState.replaceAll (destination.getData(), destination.getSize());
#else
    juce::ignoreUnused (destination);
#endif
}

void RemotePluginProcessor::setStateInformation (const void* data, int size)
{
#if defined(_WIN32)
    if (size < 0 || shared == nullptr)
        return;
    {
        const juce::ScopedLock lock (stateLock);
        cachedState.replaceAll (data, static_cast<std::size_t> (size));
    }
    shared->stateBytes = size;
    const auto stateFile = juce::File::createFileWithoutCheckingPath (
        juce::String::fromUTF8 (shared->stateFilePath));
    {
        juce::FileOutputStream stream (stateFile);
        if (! stream.openedOk() || (size > 0 && ! stream.write (data, static_cast<std::size_t> (size))))
        {
            juce::Logger::writeToLog ("RemotePluginProcessor: nao foi possivel gravar o estado do plugin.");
            return;
        }
        stream.flush();
    }
    sendCommand (pluginworker::Command::setState, 30000);
#else
    juce::ignoreUnused (data, size);
#endif
}

#endif
