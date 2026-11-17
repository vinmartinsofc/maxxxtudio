#include "DrumRackProcessor.h"
#include "DrumRackEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

class DrumRackProcessor::PadAutomationParameter final : public juce::AudioProcessorParameterWithID
{
public:
    PadAutomationParameter (const juce::String& identifier, const juce::String& name,
                            PadParameterKind parameterKind, float initialValue)
        : juce::AudioProcessorParameterWithID (juce::ParameterID (identifier, 1), name),
          kind (parameterKind), defaultValue (initialValue), value (initialValue)
    {
    }

    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float newValue) override
    {
        newValue = std::clamp (newValue, 0.0f, 1.0f);
        if (kind == PadParameterKind::Reverse || kind == PadParameterKind::Loop
            || kind == PadParameterKind::ClassicMode)
            newValue = newValue >= 0.5f ? 1.0f : 0.0f;
        else if (kind == PadParameterKind::Pitch)
            newValue = std::round (newValue * 96.0f) / 96.0f;
        value.store (newValue, std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return defaultValue; }
    juce::String getText (float normalizedValue, int maximumStringLength) const override
    {
        juce::String text;
        if (kind == PadParameterKind::Reverse || kind == PadParameterKind::Loop
            || kind == PadParameterKind::ClassicMode)
            text = normalizedValue >= 0.5f ? "On" : "Off";
        else if (kind == PadParameterKind::Pitch)
            text = juce::String (static_cast<int> (std::round (normalizedValue * 96.0f - 48.0f)));
        else if (kind == PadParameterKind::Gain)
            text = juce::String (normalizedValue * 4.0f, 2);
        else if (kind == PadParameterKind::Start || kind == PadParameterKind::End)
            text = juce::String (normalizedValue * 100.0f, 1) + "%";
        else
            text = juce::String (normalizedValue, 2);
        return text.substring (0, maximumStringLength);
    }
    float getValueForText (const juce::String& text) const override
    {
        if (kind == PadParameterKind::Reverse || kind == PadParameterKind::Loop
            || kind == PadParameterKind::ClassicMode)
            return text.equalsIgnoreCase ("On") ? 1.0f : 0.0f;
        if (kind == PadParameterKind::Pitch)
            return std::clamp ((text.getFloatValue() + 48.0f) / 96.0f, 0.0f, 1.0f);
        if (kind == PadParameterKind::Gain)
            return std::clamp (text.getFloatValue() / 4.0f, 0.0f, 1.0f);
        if (kind == PadParameterKind::Start || kind == PadParameterKind::End)
            return std::clamp (text.getFloatValue() / 100.0f, 0.0f, 1.0f);
        return std::clamp (text.getFloatValue(), 0.0f, 1.0f);
    }

private:
    PadParameterKind kind;
    const float defaultValue;
    std::atomic<float> value;
};

DrumRackProcessor::DrumRackProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    pluginFormats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    static constexpr std::array<const char*, automatablePadParameterCount> parameterNames {
        "Volume", "Transpose", "Start", "End", "Reverse", "Loop", "Classic"
    };
    static constexpr std::array<PadParameterKind, automatablePadParameterCount> parameterKinds {
        PadParameterKind::Gain, PadParameterKind::Pitch, PadParameterKind::Start,
        PadParameterKind::End, PadParameterKind::Reverse, PadParameterKind::Loop,
        PadParameterKind::ClassicMode
    };
    static constexpr std::array<float, automatablePadParameterCount> parameterDefaults {
        0.25f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
    };
    for (int pad = 0; pad < numPads; ++pad)
        for (int parameterIndex = 0; parameterIndex < automatablePadParameterCount; ++parameterIndex)
        {
            const auto number = juce::String (pad + 1);
            const auto suffix = juce::String (parameterNames[static_cast<std::size_t> (parameterIndex)]);
            auto* parameter = new PadAutomationParameter (
                "pad_" + number + "_" + suffix.toLowerCase(),
                "Pad " + number + " " + suffix,
                parameterKinds[static_cast<std::size_t> (parameterIndex)],
                parameterDefaults[static_cast<std::size_t> (parameterIndex)]);
            addParameter (parameter);
            padAutomationParameters[static_cast<std::size_t> (pad)]
                                   [static_cast<std::size_t> (parameterIndex)] = parameter;
        }
    for (auto& chain : pendingPadEffects)
        chain.store (std::make_shared<PadEffectChain>(), std::memory_order_relaxed);
}

void DrumRackProcessor::syncPadAutomationParameters (int pad, const Pad& state)
{
    const auto index = static_cast<std::size_t> (pad);
    const int length = state.samples != nullptr ? state.samples->getNumSamples() : 0;
    const auto set = [this, index] (int parameter, float normalized)
    {
        padAutomationParameters[index][static_cast<std::size_t> (parameter)]->setValue (normalized);
    };
    set (0, state.settings.gain / 4.0f);
    set (1, (static_cast<float> (state.settings.pitchSemitones) + 48.0f) / 96.0f);
    set (2, length > 1 ? static_cast<float> (state.settings.startSample) / (length - 1) : 0.0f);
    set (3, length > 0 && state.settings.endSample > 0
                ? static_cast<float> (state.settings.endSample) / length : 1.0f);
    set (4, state.settings.reverse ? 1.0f : 0.0f);
    set (5, state.settings.loop ? 1.0f : 0.0f);
    set (6, state.settings.mode == PlaybackMode::Classic ? 1.0f : 0.0f);
}

std::vector<DrumRackProcessor::PadEffectInfo> DrumRackProcessor::padEffects (int pad) const
{
    if (pad < 0 || pad >= numPads)
        return {};

    const auto chain = pendingPadEffects[static_cast<std::size_t> (pad)].load (std::memory_order_acquire);
    std::vector<PadEffectInfo> result;
    if (chain == nullptr)
        return result;

    result.reserve (chain->effects.size());
    for (const auto& effect : chain->effects)
        result.push_back ({ effect->description.name, effect->description.createIdentifierString(),
                            effect->bypassed.load (std::memory_order_relaxed) });
    return result;
}

bool DrumRackProcessor::preparePadEffect (const juce::PluginDescription& description,
                                           std::unique_ptr<juce::AudioProcessor> instance,
                                           std::shared_ptr<PadEffect>& result, juce::String& error)
{
    if (instance == nullptr || description.isInstrument)
    {
        error = "Selecione um efeito de audio valido.";
        return false;
    }

    const int inputChannels = instance->getBusesLayout().getMainInputChannelSet().size();
    const int outputChannels = instance->getBusesLayout().getMainOutputChannelSet().size();
    if (inputChannels < 1 || inputChannels > 2 || outputChannels < 1 || outputChannels > 2)
    {
        error = "O efeito precisa aceitar de 1 a 2 canais de entrada e saida.";
        return false;
    }

    instance->setRateAndBufferSizeDetails (outputSampleRate, maximumBlockSize);
    instance->prepareToPlay (outputSampleRate, maximumBlockSize);
    auto effect = std::make_shared<PadEffect>();
    effect->description = description;
    effect->processor = std::shared_ptr<juce::AudioProcessor> (std::move (instance));
    effect->scratch.setSize (std::max (effect->processor->getTotalNumInputChannels(),
                                      effect->processor->getTotalNumOutputChannels()),
                            maximumBlockSize);
    result = std::move (effect);
    error.clear();
    return true;
}

void DrumRackProcessor::publishPadEffectChain (int pad, std::shared_ptr<PadEffectChain> chain)
{
    const auto index = static_cast<std::size_t> (pad);
    const auto previous = pendingPadEffects[index].load (std::memory_order_acquire);
    chain->version = previous != nullptr ? previous->version + 1 : 1;
    if (previous != nullptr)
        retiredPadEffectChains.push_back ({ pad, previous });
    pendingPadEffects[index].store (std::shared_ptr<const PadEffectChain> (std::move (chain)),
                                    std::memory_order_release);
}

bool DrumRackProcessor::addPadEffect (int pad, const juce::PluginDescription& description,
                                      std::unique_ptr<juce::AudioProcessor> instance,
                                      juce::String& error)
{
    if (pad < 0 || pad >= numPads)
    {
        error = "Pad MIDI invalido.";
        return false;
    }

    std::shared_ptr<PadEffect> effect;
    if (! preparePadEffect (description, std::move (instance), effect, error))
        return false;

    const juce::SpinLock::ScopedLockType lock (effectsLock);
    const auto index = static_cast<std::size_t> (pad);
    const auto current = pendingPadEffects[index].load (std::memory_order_acquire);
    auto next = std::make_shared<PadEffectChain> (current != nullptr ? *current : PadEffectChain{});
    next->effects.push_back (std::move (effect));
    publishPadEffectChain (pad, std::move (next));
    return true;
}

bool DrumRackProcessor::removePadEffect (int pad, int effectIndex)
{
    if (pad < 0 || pad >= numPads)
        return false;

    const juce::SpinLock::ScopedLockType lock (effectsLock);
    const auto index = static_cast<std::size_t> (pad);
    const auto current = pendingPadEffects[index].load (std::memory_order_acquire);
    if (current == nullptr || effectIndex < 0
        || effectIndex >= static_cast<int> (current->effects.size()))
        return false;

    auto next = std::make_shared<PadEffectChain> (*current);
    next->effects.erase (next->effects.begin() + effectIndex);
    publishPadEffectChain (pad, std::move (next));
    return true;
}

bool DrumRackProcessor::movePadEffect (int pad, int effectIndex, int direction)
{
    if (pad < 0 || pad >= numPads || (direction != -1 && direction != 1))
        return false;

    const juce::SpinLock::ScopedLockType lock (effectsLock);
    const auto index = static_cast<std::size_t> (pad);
    const auto current = pendingPadEffects[index].load (std::memory_order_acquire);
    if (current == nullptr || effectIndex < 0
        || effectIndex >= static_cast<int> (current->effects.size()))
        return false;

    const int targetIndex = effectIndex + direction;
    if (! juce::isPositiveAndBelow (targetIndex, static_cast<int> (current->effects.size())))
        return false;

    auto next = std::make_shared<PadEffectChain> (*current);
    std::swap (next->effects[static_cast<std::size_t> (effectIndex)],
               next->effects[static_cast<std::size_t> (targetIndex)]);
    publishPadEffectChain (pad, std::move (next));
    return true;
}

bool DrumRackProcessor::setPadEffectBypassed (int pad, int effectIndex, bool bypassed)
{
    if (pad < 0 || pad >= numPads)
        return false;
    const auto chain = pendingPadEffects[static_cast<std::size_t> (pad)].load (std::memory_order_acquire);
    if (chain == nullptr || effectIndex < 0
        || effectIndex >= static_cast<int> (chain->effects.size()))
        return false;
    chain->effects[static_cast<std::size_t> (effectIndex)]->bypassed.store (bypassed,
                                                                           std::memory_order_relaxed);
    return true;
}

std::shared_ptr<juce::AudioProcessor> DrumRackProcessor::padEffectProcessor (int pad,
                                                                            int effectIndex) const
{
    if (pad < 0 || pad >= numPads)
        return {};
    const auto chain = pendingPadEffects[static_cast<std::size_t> (pad)].load (std::memory_order_acquire);
    if (chain == nullptr || effectIndex < 0
        || effectIndex >= static_cast<int> (chain->effects.size()))
        return {};
    return chain->effects[static_cast<std::size_t> (effectIndex)]->processor;
}

void DrumRackProcessor::collectRetiredPadEffects()
{
    const juce::SpinLock::ScopedLockType lock (effectsLock);
    retiredPadEffectChains.erase (
        std::remove_if (retiredPadEffectChains.begin(), retiredPadEffectChains.end(),
            [this] (const RetiredPadEffectChain& retired)
            {
                if (retired.pad < 0 || retired.pad >= numPads)
                    return true;
                return audioPadEffectPointers[static_cast<std::size_t> (retired.pad)].load (
                           std::memory_order_acquire) != retired.chain.get();
            }),
        retiredPadEffectChains.end());
}

void DrumRackProcessor::setPadSettings (int pad, const PadSettings& settings)
{
    setPadSettingsInternal (pad, settings, true);
}

void DrumRackProcessor::setPadSettingsInternal (int pad, const PadSettings& settings, bool recordUndo)
{
    if (pad < 0 || pad >= numPads)
        return;

    const juce::SpinLock::ScopedLockType lock (padsLock);
    auto& target = pendingPads[static_cast<std::size_t> (pad)];
    auto normalized = settings;
    if (! std::isfinite (normalized.gain))
        normalized.gain = 1.0f;
    normalized.gain = std::clamp (normalized.gain, 0.0f, 4.0f);
    normalized.pitchSemitones = std::clamp (normalized.pitchSemitones, -48, 48);
    normalized.startSample = std::max (0, normalized.startSample);
    if (target.samples != nullptr)
    {
        const int length = target.samples->getNumSamples();
        normalized.startSample = std::clamp (normalized.startSample, 0, std::max (0, length - 1));
        normalized.endSample = normalized.endSample > normalized.startSample
                            ? std::clamp (normalized.endSample, normalized.startSample + 1, length)
                            : length;
    }
    else if (normalized.endSample <= normalized.startSample)
    {
        normalized.endSample = -1;
    }
    const auto& previous = target.settings;
    const bool unchanged = previous.gain == normalized.gain
                        && previous.pitchSemitones == normalized.pitchSemitones
                        && previous.startSample == normalized.startSample
                        && previous.endSample == normalized.endSample
                        && previous.reverse == normalized.reverse
                        && previous.loop == normalized.loop
                        && previous.mode == normalized.mode;
    if (unchanged)
        return;
    if (recordUndo)
        pushUndoStateLocked();
    target.settings = normalized;
    syncPadAutomationParameters (pad, target);
    ++pendingVersion;
}

DrumRackProcessor::PadSettings DrumRackProcessor::padSettings (int pad) const
{
    if (pad < 0 || pad >= numPads)
        return {};

    const juce::SpinLock::ScopedLockType lock (padsLock);
    return pendingPads[static_cast<std::size_t> (pad)].settings;
}

bool DrumRackProcessor::loadPadFromFile (int pad, const juce::File& file, juce::String& error)
{
    return loadPadFromFileInternal (pad, file, error, true);
}

bool DrumRackProcessor::loadPadFromFileInternal (int pad, const juce::File& file,
                                                  juce::String& error, bool recordUndo)
{
    if (pad < 0 || pad >= numPads)
    {
        error = "Pad MIDI invalido.";
        return false;
    }

    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Formato de audio nao suportado.";
        return false;
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max()
        || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        error = "Arquivo de audio vazio ou invalido.";
        return false;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    const int numChannels = static_cast<int> (std::min (2u, reader->numChannels));
    constexpr std::uint64_t maxDecodedBytes = 512ULL * 1024 * 1024;
    if (static_cast<std::uint64_t> (numSamples) * static_cast<std::uint64_t> (numChannels)
            * sizeof (float) > maxDecodedBytes)
    {
        error = "O sample excede o limite de 512 MB decodificados.";
        return false;
    }

    auto samples = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    if (! reader->read (samples.get(), 0, numSamples, 0, true, true))
    {
        error = "Falha ao decodificar o arquivo de audio.";
        return false;
    }

    Pad sample { file, std::move (samples), reader->sampleRate, {} };
    sample.settings.startSample = 0;
    sample.settings.endSample = numSamples;
    setPad (pad, std::move (sample), recordUndo);
    error.clear();
    return true;
}

void DrumRackProcessor::setPad (int pad, Pad sample, bool recordUndo)
{
    if (pad < 0 || pad >= numPads)
        return;
    const juce::SpinLock::ScopedLockType lock (padsLock);
    if (recordUndo)
        pushUndoStateLocked();
    pendingPads[static_cast<std::size_t> (pad)] = std::move (sample);
    syncPadAutomationParameters (pad, pendingPads[static_cast<std::size_t> (pad)]);
    ++pendingVersion;
}

void DrumRackProcessor::pushUndoStateLocked()
{
    constexpr std::size_t maxHistoryStates = 100;
    if (undoStack.size() == maxHistoryStates)
        undoStack.erase (undoStack.begin());
    undoStack.push_back (pendingPads);
    redoStack.clear();
}

void DrumRackProcessor::clearPads()
{
    const juce::SpinLock::ScopedLockType lock (padsLock);
    const auto hasContent = [] (const Pad& pad)
    {
        return pad.file != juce::File() || pad.samples != nullptr || pad.sampleRate != 0.0
            || pad.settings.gain != 1.0f || pad.settings.pitchSemitones != 0
            || pad.settings.startSample != 0 || pad.settings.endSample != -1
            || pad.settings.reverse || pad.settings.loop
            || pad.settings.mode != PlaybackMode::OneShot;
    };
    if (std::none_of (pendingPads.begin(), pendingPads.end(), hasContent))
        return;
    pushUndoStateLocked();
    for (auto& pad : pendingPads)
        pad = {};
    for (int pad = 0; pad < numPads; ++pad)
        syncPadAutomationParameters (pad, pendingPads[static_cast<std::size_t> (pad)]);
    ++pendingVersion;
}

void DrumRackProcessor::clearPad (int pad)
{
    if (pad < 0 || pad >= numPads)
        return;
    const juce::SpinLock::ScopedLockType lock (padsLock);
    auto& target = pendingPads[static_cast<std::size_t> (pad)];
    if (target.file == juce::File() && target.samples == nullptr && target.sampleRate == 0.0
        && target.settings.gain == 1.0f && target.settings.pitchSemitones == 0
        && target.settings.startSample == 0 && target.settings.endSample == -1
        && ! target.settings.reverse && ! target.settings.loop
        && target.settings.mode == PlaybackMode::OneShot)
        return;
    pushUndoStateLocked();
    target = {};
    syncPadAutomationParameters (pad, target);
    ++pendingVersion;
}

bool DrumRackProcessor::undo()
{
    const juce::SpinLock::ScopedLockType lock (padsLock);
    if (undoStack.empty())
        return false;
    constexpr std::size_t maxHistoryStates = 100;
    if (redoStack.size() == maxHistoryStates)
        redoStack.erase (redoStack.begin());
    redoStack.push_back (pendingPads);
    pendingPads = std::move (undoStack.back());
    undoStack.pop_back();
    for (int pad = 0; pad < numPads; ++pad)
        syncPadAutomationParameters (pad, pendingPads[static_cast<std::size_t> (pad)]);
    ++pendingVersion;
    return true;
}

bool DrumRackProcessor::redo()
{
    const juce::SpinLock::ScopedLockType lock (padsLock);
    if (redoStack.empty())
        return false;
    constexpr std::size_t maxHistoryStates = 100;
    if (undoStack.size() == maxHistoryStates)
        undoStack.erase (undoStack.begin());
    undoStack.push_back (pendingPads);
    pendingPads = std::move (redoStack.back());
    redoStack.pop_back();
    for (int pad = 0; pad < numPads; ++pad)
        syncPadAutomationParameters (pad, pendingPads[static_cast<std::size_t> (pad)]);
    ++pendingVersion;
    return true;
}

bool DrumRackProcessor::canUndo() const
{
    const juce::SpinLock::ScopedLockType lock (padsLock);
    return ! undoStack.empty();
}

bool DrumRackProcessor::canRedo() const
{
    const juce::SpinLock::ScopedLockType lock (padsLock);
    return ! redoStack.empty();
}

juce::String DrumRackProcessor::padName (int pad) const
{
    if (pad < 0 || pad >= numPads)
        return {};
    const juce::SpinLock::ScopedLockType lock (padsLock);
    const auto& sample = pendingPads[static_cast<std::size_t> (pad)];
    return sample.file.existsAsFile() ? sample.file.getFileName()
                                     : "MIDI " + juce::String (firstMidiNote + pad) + " - vazio";
}

int DrumRackProcessor::padSampleLength (int pad) const
{
    if (pad < 0 || pad >= numPads)
        return 0;
    const juce::SpinLock::ScopedLockType lock (padsLock);
    const auto& samples = pendingPads[static_cast<std::size_t> (pad)].samples;
    return samples != nullptr ? samples->getNumSamples() : 0;
}

double DrumRackProcessor::getTailLengthSeconds() const
{
    double longestChainTail = 0.0;
    for (const auto& pending : pendingPadEffects)
    {
        const auto chain = pending.load (std::memory_order_acquire);
        double chainTail = 0.0;
        if (chain != nullptr)
            for (const auto& effect : chain->effects)
                chainTail += std::max (0.0, effect->processor->getTailLengthSeconds());
        longestChainTail = std::max (longestChainTail, chainTail);
    }
    return longestChainTail;
}

juce::String DrumRackProcessor::stateXml() const
{
    juce::XmlElement xml ("DrumRack");
    xml.setAttribute ("version", 5);
    const juce::SpinLock::ScopedLockType lock (padsLock);
    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto& sample = pendingPads[static_cast<std::size_t> (pad)];
        const auto chain = pendingPadEffects[static_cast<std::size_t> (pad)].load (std::memory_order_acquire);
        if (sample.file == juce::File() && (chain == nullptr || chain->effects.empty()))
            continue;
        auto* node = xml.createNewChildElement ("Pad");
        node->setAttribute ("index", pad);
        if (sample.file != juce::File())
            node->setAttribute ("file", sample.file.getFullPathName());
        node->setAttribute ("gain", sample.settings.gain);
        node->setAttribute ("pitchSemitones", sample.settings.pitchSemitones);
        node->setAttribute ("startSample", sample.settings.startSample);
        node->setAttribute ("endSample", sample.settings.endSample);
        node->setAttribute ("reverse", sample.settings.reverse);
        node->setAttribute ("loop", sample.settings.loop);
        node->setAttribute ("mode", sample.settings.mode == PlaybackMode::Classic ? "classic" : "oneShot");

        if (chain != nullptr)
            for (const auto& effect : chain->effects)
            {
                auto* effectNode = node->createNewChildElement ("Effect");
                if (auto description = effect->description.createXml())
                    effectNode->addChildElement (description.release());
                juce::MemoryBlock state;
                effect->processor->getStateInformation (state);
                effectNode->setAttribute ("state", state.toBase64Encoding());
                effectNode->setAttribute ("bypassed", effect->bypassed.load (std::memory_order_relaxed));
            }
    }
    return xml.toString();
}

bool DrumRackProcessor::restoreStateXml (const juce::XmlElement& xml)
{
    const int version = xml.getIntAttribute ("version", 0);
    if (! xml.hasTagName ("DrumRack") || version < 1 || version > 5)
        return false;

    std::array<const juce::XmlElement*, numPads> padNodes {};
    std::array<int, numPads> padPriorities {};
    for (auto* node = xml.getFirstChildElement(); node != nullptr; node = node->getNextElement())
    {
        if (! node->hasTagName ("Pad"))
            continue;
        const int storedIndex = node->getIntAttribute ("index", -1);
        int pad = -1;
        int priority = 0;
        if (version < 3)
        {
            pad = storedIndex;
            priority = 2;
        }
        else if (version == 3)
        {
            if (storedIndex >= 36 && storedIndex < 36 + numPads)
            {
                pad = storedIndex - 36;
                priority = 3;
            }
            else if (storedIndex >= firstMidiNote && storedIndex < firstMidiNote + numPads)
            {
                pad = storedIndex - firstMidiNote;
                priority = 2;
            }
            else if (storedIndex >= 0 && storedIndex < numPads)
            {
                pad = storedIndex;
                priority = 1;
            }
        }
        else
        {
            pad = storedIndex;
            priority = 3;
        }

        if (pad < 0 || pad >= numPads)
            continue;
        const auto padIndex = static_cast<std::size_t> (pad);
        if (priority < padPriorities[padIndex])
            continue;
        if (priority == padPriorities[padIndex] && padNodes[padIndex] != nullptr)
            return false;
        padNodes[padIndex] = node;
        padPriorities[padIndex] = priority;
    }

    {
        const juce::SpinLock::ScopedLockType lock (padsLock);
        pendingPads = {};
        for (int pad = 0; pad < numPads; ++pad)
            syncPadAutomationParameters (pad, pendingPads[static_cast<std::size_t> (pad)]);
        undoStack.clear();
        redoStack.clear();
        ++pendingVersion;
    }
    {
        const juce::SpinLock::ScopedLockType lock (effectsLock);
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto index = static_cast<std::size_t> (pad);
            const auto old = pendingPadEffects[index].load (std::memory_order_acquire);
            if (old != nullptr)
                retiredPadEffectChains.push_back ({ pad, old });
            pendingPadEffects[index].store (std::make_shared<PadEffectChain>(), std::memory_order_release);
        }
    }

    bool effectsRestored = true;
    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto* node = padNodes[static_cast<std::size_t> (pad)];
        if (node == nullptr)
            continue;
        const juce::File file (node->getStringAttribute ("file"));
        if (file.getFullPathName().isNotEmpty() && file.existsAsFile())
        {
            juce::String error;
            if (! loadPadFromFileInternal (pad, file, error, false))
                return false;
        }
        else if (file.getFullPathName().isNotEmpty())
        {
            setPad (pad, { file, {}, 0.0, {} }, false);
        }

        PadSettings settings;
        settings.gain = static_cast<float> (node->getDoubleAttribute ("gain", 1.0));
        settings.pitchSemitones = node->getIntAttribute ("pitchSemitones", 0);
        settings.startSample = node->getIntAttribute ("startSample", 0);
        settings.endSample = node->getIntAttribute ("endSample", -1);
        if (version < 3 && settings.endSample >= 0)
            ++settings.endSample;
        settings.reverse = node->getBoolAttribute ("reverse", false);
        settings.loop = node->getBoolAttribute ("loop", false);
        const auto mode = node->getStringAttribute ("mode");
        settings.mode = mode == "classic" || (mode.isEmpty() && version < 3 && settings.loop)
                      ? PlaybackMode::Classic : PlaybackMode::OneShot;
        setPadSettingsInternal (pad, settings, false);

        for (auto* effectNode = node->getFirstChildElement(); effectNode != nullptr;
             effectNode = effectNode->getNextElement())
        {
            if (! effectNode->hasTagName ("Effect"))
                continue;
            auto* descriptionNode = effectNode->getFirstChildElement();
            juce::PluginDescription description;
            if (descriptionNode == nullptr || ! description.loadFromXml (*descriptionNode))
            {
                effectsRestored = false;
                continue;
            }

            juce::String error;
            auto instance = pluginFormats.createPluginInstance (description, outputSampleRate,
                                                                 maximumBlockSize, error);
            if (instance == nullptr)
            {
                effectsRestored = false;
                continue;
            }

            juce::MemoryBlock state;
            if (effectNode->hasAttribute ("state")
                && ! state.fromBase64Encoding (effectNode->getStringAttribute ("state")))
            {
                effectsRestored = false;
                continue;
            }
            if (state.getSize() > 0)
                instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

            if (! addPadEffect (pad, description, std::move (instance), error))
            {
                effectsRestored = false;
                continue;
            }
            if (effectNode->getBoolAttribute ("bypassed", false))
                setPadEffectBypassed (pad, static_cast<int> (padEffects (pad).size()) - 1, true);
        }
    }
    return effectsRestored;
}

void DrumRackProcessor::syncPadsForAudioThread()
{
    if (padsLock.tryEnter())
    {
        if (pendingVersion != audioVersion)
        {
            for (int pad = 0; pad < numPads; ++pad)
            {
                const auto index = static_cast<std::size_t> (pad);
                audioSamples[index] = pendingPads[index].samples;
                audioSampleRates[index] = pendingPads[index].sampleRate;
                audioPadSettings[index] = pendingPads[index].settings;
            }
            audioVersion = pendingVersion;
        }
        padsLock.exit();
    }

    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto index = static_cast<std::size_t> (pad);
        const auto pending = pendingPadEffects[index].load (std::memory_order_acquire);
        const auto& current = audioPadEffects[index];
        if (pending != nullptr && pending != current)
        {
            audioPadEffects[index] = pending;
            audioPadEffectPointers[index].store (pending.get(), std::memory_order_release);
        }
    }
}

void DrumRackProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    outputSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    maximumBlockSize = std::max (1, maximumExpectedSamplesPerBlock);
    for (int pad = 0; pad < numPads; ++pad)
    {
        padBuffers[static_cast<std::size_t> (pad)].setSize (2, maximumBlockSize);
        audioPadEffects[static_cast<std::size_t> (pad)] =
            pendingPadEffects[static_cast<std::size_t> (pad)].load (std::memory_order_acquire);
        audioPadEffectPointers[static_cast<std::size_t> (pad)].store (
            audioPadEffects[static_cast<std::size_t> (pad)].get(), std::memory_order_release);
        const auto& chain = audioPadEffects[static_cast<std::size_t> (pad)];
        if (chain != nullptr)
            for (const auto& effect : chain->effects)
            {
                effect->processor->setRateAndBufferSizeDetails (outputSampleRate, maximumBlockSize);
                effect->processor->prepareToPlay (outputSampleRate, maximumBlockSize);
                effect->scratch.setSize (
                    std::max (effect->processor->getTotalNumInputChannels(),
                              effect->processor->getTotalNumOutputChannels()),
                    maximumBlockSize);
            }
    }
    for (auto& voice : voices)
        voice = {};
}

void DrumRackProcessor::releaseResources()
{
    for (auto& voice : voices)
        voice = {};
    for (const auto& chain : audioPadEffects)
        if (chain != nullptr)
            for (const auto& effect : chain->effects)
                effect->processor->releaseResources();
}

void DrumRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    syncPadsForAudioThread();
    for (auto& padBuffer : padBuffers)
        padBuffer.clear (0, buffer.getNumSamples());

    for (const auto metadata : midi)
    {
        const auto& message = metadata.getMessage();
        if (message.isNoteOff())
        {
            const auto releaseSample = static_cast<int> (std::clamp<int64_t> (
                metadata.samplePosition, 0, std::max (0, buffer.getNumSamples() - 1)));
            for (auto& voice : voices)
                if (voice.active && voice.midiNote == message.getNoteNumber() && voice.classicMode)
                {
                    voice.releaseSample = releaseSample;
                    voice.releaseSamplesRemaining = std::max (1, static_cast<int> (outputSampleRate * 0.005));
                    voice.releasePending = true;
                }
            continue;
        }

        if (! message.isNoteOn())
            continue;

        const int pad = message.getNoteNumber() - firstMidiNote;
        if (pad < 0 || pad >= numPads || buffer.getNumSamples() <= 0)
            continue;
        triggeredPadMask.fetch_or (std::uint32_t { 1 } << pad, std::memory_order_relaxed);
        const auto index = static_cast<std::size_t> (pad);
        const auto& samples = audioSamples[index];
        if (samples == nullptr || samples->getNumSamples() == 0)
            continue;

        auto settings = audioPadSettings[index];
        const int sourceLength = samples->getNumSamples();
        const auto parameterValue = [this, index] (int parameter)
        {
            return padAutomationParameters[index][static_cast<std::size_t> (parameter)]->getValue();
        };
        settings.gain = parameterValue (0) * 4.0f;
        settings.pitchSemitones = static_cast<int> (std::lround (parameterValue (1) * 96.0f - 48.0f));
        settings.startSample = static_cast<int> (std::lround (
            parameterValue (2) * std::max (0, sourceLength - 1)));
        settings.endSample = static_cast<int> (std::lround (parameterValue (3) * sourceLength));
        settings.reverse = parameterValue (4) >= 0.5f;
        settings.loop = parameterValue (5) >= 0.5f;
        settings.mode = parameterValue (6) >= 0.5f ? PlaybackMode::Classic : PlaybackMode::OneShot;
        const int startSample = std::clamp (settings.startSample, 0, std::max (0, sourceLength - 1));
        const int endSample = std::clamp (settings.endSample, startSample + 1, sourceLength);
        const double pitchRatio = std::pow (2.0, settings.pitchSemitones / 12.0);

        auto voice = std::find_if (voices.begin(), voices.end(), [] (const Voice& candidate)
        {
            return ! candidate.active;
        });
        if (voice == voices.end())
            voice = voices.begin();

        *voice = {};
        voice->samples = samples;
        voice->sampleRate = audioSampleRates[index];
        voice->position = settings.reverse ? static_cast<double> (endSample - 1)
                                           : static_cast<double> (startSample);
        voice->pitchRatio = pitchRatio;
        voice->gain = message.getFloatVelocity() * settings.gain;
        voice->startOffset = startSample;
        voice->endOffset = endSample;
        voice->reverse = settings.reverse;
        voice->loop = settings.mode == PlaybackMode::Classic && settings.loop;
        voice->classicMode = settings.mode == PlaybackMode::Classic;
        voice->active = true;
        voice->triggerSample = static_cast<int> (std::clamp<int64_t> (
            metadata.samplePosition, 0, static_cast<int64_t> (buffer.getNumSamples() - 1)));
        voice->midiNote = message.getNoteNumber();
        voice->pad = pad;
    }
    midi.clear();

    for (auto& voice : voices)
    {
        if (! voice.active || voice.samples == nullptr)
            continue;

        const auto& source = *voice.samples;
        const int sourceLength = source.getNumSamples();
        const int start = std::clamp<int> (voice.startOffset, 0, sourceLength);
        const int end = std::max<int> (start, voice.endOffset);
        const int safeEnd = std::max<int> (start, end - 1);
        const double step = (voice.sampleRate / outputSampleRate) * voice.pitchRatio;
        const double direction = voice.reverse ? -1.0 : 1.0;

        for (int outputSample = 0; outputSample < buffer.getNumSamples(); ++outputSample)
        {
            if (outputSample < voice.triggerSample)
                continue;

            if (voice.releasePending && ! voice.releasing && outputSample >= voice.releaseSample)
            {
                voice.releasePending = false;
                voice.releasing = true;
            }

            const long posAsLong = static_cast<long> (std::floor (voice.position));
            const int index = std::clamp<int> (static_cast<int> (posAsLong), start, safeEnd);
            const int nextIndex = std::clamp<int> (index + 1, start, safeEnd);
            const float fraction = static_cast<float> (voice.position - index);
            const float left = source.getSample (0, index)
                             + fraction * (source.getSample (0, nextIndex) - source.getSample (0, index));
            const int rightChannel = std::min (1, source.getNumChannels() - 1);
            const float right = source.getSample (rightChannel, index)
                              + fraction * (source.getSample (rightChannel, nextIndex)
                                            - source.getSample (rightChannel, index));
            float envelope = 1.0f;
            if (voice.releasing)
            {
                if (voice.releaseSamplesRemaining <= 0)
                {
                    voice.active = false;
                    break;
                }
                envelope = static_cast<float> (voice.releaseSamplesRemaining)
                         / std::max (1, static_cast<int> (outputSampleRate * 0.005));
                --voice.releaseSamplesRemaining;
            }
            if (voice.pad >= 0 && voice.pad < numPads)
            {
                auto& padBuffer = padBuffers[static_cast<std::size_t> (voice.pad)];
                padBuffer.addSample (0, outputSample, left * voice.gain * envelope);
                padBuffer.addSample (1, outputSample, right * voice.gain * envelope);
            }

            voice.position += direction * step;
            if ((direction > 0.0 && voice.position >= end) || (direction < 0.0 && voice.position < start))
            {
                if (voice.loop && end > start)
                {
                    const double range = static_cast<double> (end - start);
                    voice.position = start + std::fmod (std::max (0.0, voice.position - start), range);
                    if (voice.reverse)
                        voice.position = end - 1 - std::fmod (std::max (0.0, end - 1 - voice.position), range);
                }
                else
                {
                    voice.active = false;
                    break;
                }
            }
        }

        if (! voice.active)
            voice = {};
        else
            voice.triggerSample = 0;
    }

    juce::MidiBuffer effectMidi;
    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& padBuffer = padBuffers[static_cast<std::size_t> (pad)];
        const auto& chain = audioPadEffects[static_cast<std::size_t> (pad)];
        if (chain != nullptr)
            for (const auto& effect : chain->effects)
            {
                if (effect->bypassed.load (std::memory_order_relaxed))
                    continue;

                auto& scratch = effect->scratch;
                const int inputChannels = effect->processor->getBusesLayout()
                                              .getMainInputChannelSet().size();
                const int outputChannels = effect->processor->getBusesLayout()
                                               .getMainOutputChannelSet().size();
                const int samples = std::min (buffer.getNumSamples(), scratch.getNumSamples());
                scratch.clear (0, samples);
                if (inputChannels == 1)
                {
                    for (int sample = 0; sample < samples; ++sample)
                        scratch.setSample (0, sample, 0.5f * (padBuffer.getSample (0, sample)
                                                             + padBuffer.getSample (1, sample)));
                }
                else
                {
                    for (int channel = 0; channel < inputChannels; ++channel)
                        scratch.copyFrom (channel, 0, padBuffer, channel, 0, samples);
                }

                effectMidi.clear();
                effect->processor->processBlock (scratch, effectMidi);
                padBuffer.clear (0, 0, samples);
                if (outputChannels == 1)
                {
                    padBuffer.copyFrom (0, 0, scratch, 0, 0, samples);
                    padBuffer.copyFrom (1, 0, scratch, 0, 0, samples);
                }
                else
                {
                    for (int channel = 0; channel < std::min (2, outputChannels); ++channel)
                        padBuffer.copyFrom (channel, 0, scratch, channel, 0, samples);
                }
            }

        for (int channel = 0; channel < std::min (2, buffer.getNumChannels()); ++channel)
            buffer.addFrom (channel, 0, padBuffer, channel, 0, buffer.getNumSamples());
    }
}

juce::AudioProcessorEditor* DrumRackProcessor::createEditor()
{
    return new DrumRackEditor (*this);
}

void DrumRackProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const auto xml = stateXml();
    destination.replaceAll (xml.toRawUTF8(), static_cast<std::size_t> (xml.getNumBytesAsUTF8()));
}

void DrumRackProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0)
    {
        stateRestoreSucceededFlag = false;
        return;
    }
    const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (static_cast<const char*> (data), size));
    stateRestoreSucceededFlag = xml != nullptr && restoreStateXml (*xml);
}
