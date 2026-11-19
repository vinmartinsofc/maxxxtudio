#include "AudioEngine.h"
#include "DrumRackProcessor.h"
#include "RemotePluginProcessor.h"
#include "NativeEffectProcessors.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>

namespace
{
juce::File audioDeviceSettingsFile()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("ProjectFreedom");
    directory.createDirectory();
    return directory.getChildFile ("audio-device-settings.xml");
}

void decodeAudioFile (juce::AudioFormatManager& formats, const juce::File& file,
                      std::function<void (const juce::String&, std::shared_ptr<const AudioFileData>)> callback)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        juce::MessageManager::callAsync ([callback = std::move (callback)] { callback ("Formato de audio nao suportado.", {}); });
        return;
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max()
        || reader->numChannels == 0 || reader->sampleRate <= 0.0)
    {
        juce::MessageManager::callAsync ([callback = std::move (callback)] { callback ("Arquivo de audio vazio ou grande demais.", {}); });
        return;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    const int numChannels = static_cast<int> (std::min (2u, reader->numChannels));
    constexpr std::uint64_t maxDecodedBytes = 512ULL * 1024 * 1024;
    const auto decodedBytes = static_cast<std::uint64_t> (numSamples)
                            * static_cast<std::uint64_t> (numChannels) * sizeof (float);
    if (decodedBytes > maxDecodedBytes)
    {
        juce::MessageManager::callAsync ([callback = std::move (callback)]
        {
            callback ("Arquivo de audio excede o limite de 512 MB decodificados.", {});
        });
        return;
    }

    auto data = std::make_shared<AudioFileData>();
    data->file = file;
    data->sampleRate = reader->sampleRate;
    data->durationSeconds = static_cast<double> (numSamples) / reader->sampleRate;
    data->samples = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    if (! reader->read (data->samples.get(), 0, numSamples, 0, true, true))
    {
        juce::MessageManager::callAsync ([callback = std::move (callback)] { callback ("Falha ao decodificar o arquivo de audio.", {}); });
        return;
    }

    constexpr int maxWaveformResolution = 16384;
    const int waveformResolution = std::min (numSamples, maxWaveformResolution);
    data->waveform.resize (static_cast<std::size_t> (waveformResolution));
    for (int bin = 0; bin < waveformResolution; ++bin)
    {
        const int start = static_cast<int> (static_cast<std::int64_t> (bin) * numSamples / waveformResolution);
        const int end = std::max (start + 1, static_cast<int> (
            static_cast<std::int64_t> (bin + 1) * numSamples / waveformResolution));
        float minimum = 1.0f;
        float maximum = -1.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            for (int sample = start; sample < std::min (end, numSamples); ++sample)
            {
                const float value = data->samples->getSample (channel, sample);
                minimum = std::min (minimum, value);
                maximum = std::max (maximum, value);
            }
        data->waveform[static_cast<std::size_t> (bin)] = { minimum, maximum };
    }

    juce::MessageManager::callAsync ([callback = std::move (callback), data]
    {
        callback ({}, data);
    });
}

bool connectAudioChannels (juce::AudioProcessorGraph& graph,
                           const juce::AudioProcessorGraph::Node::Ptr& source,
                           const juce::AudioProcessorGraph::Node::Ptr& destination)
{
    auto* sourceProcessor = source != nullptr ? source->getProcessor() : nullptr;
    auto* destinationProcessor = destination != nullptr ? destination->getProcessor() : nullptr;
    if (sourceProcessor == nullptr || destinationProcessor == nullptr)
        return false;

    const int sourceChannels = sourceProcessor->getTotalNumOutputChannels();
    const int destinationChannels = destinationProcessor->getTotalNumInputChannels();
    if (sourceChannels <= 0 || destinationChannels <= 0)
        return false;

    bool connected = false;
    for (int channel = 0; channel < std::min (2, destinationChannels); ++channel)
    {
        const auto sourceChannel = std::min (channel, sourceChannels - 1);
        connected = graph.addConnection ({ { source->nodeID, sourceChannel },
                                           { destination->nodeID, channel } }) || connected;
    }
    return connected;
}

bool sameAutomation (const std::vector<roll::AutomationEnvelope>& first,
                     const std::vector<roll::AutomationEnvelope>& second)
{
    if (first.size() != second.size())
        return false;
    for (std::size_t i = 0; i < first.size(); ++i)
        if (! (first[i].target == second[i].target)
            || first[i].target.currentValue != second[i].target.currentValue
            || first[i].target.parameterName != second[i].target.parameterName
            || first[i].points != second[i].points)
            return false;
    return true;
}
}

// ======================================================= TrackSource

TrackSource::TrackSource (const BlockShared& s)
    : juce::AudioProcessor (BusesProperties()), shared (s)
{
    events.reserve (4096);
    localNotes.reserve (maxNotes);
    pendingNotes.reserve (maxNotes);
}

void TrackSource::prepareToPlay (double sampleRate, int)
{
    previewCollector.reset (sampleRate);
}

void TrackSource::publishNotes (const std::vector<roll::Note>& notes)
{
    const juce::SpinLock::ScopedLockType lock (notesLock);
    pendingNotes.assign (notes.begin(), notes.begin() + static_cast<std::ptrdiff_t> (std::min (notes.size(), maxNotes)));
    ++notesVersion;
}

void TrackSource::preview (int pitch, bool on, float velocity)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    previewCollector.addMessageToQueue (
        (on ? juce::MidiMessage::noteOn (1, pitch, std::clamp (velocity, 0.0f, 1.0f))
            : juce::MidiMessage::noteOff (1, pitch)).withTimeStamp (now));
}

void TrackSource::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    {
        const juce::SpinLock::ScopedTryLockType lock (notesLock);
        if (lock.isLocked() && notesVersion != localVersion)
        {
            localNotes.assign (pendingNotes.begin(), pendingNotes.end());
            localVersion = notesVersion;
        }
    }

    events.clear();
    if (shared.flush)   sequencer.flush (0, events);
    if (shared.playing) sequencer.process (localNotes, shared.plan, events);

    for (const auto& e : events)
        midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.pitch, static_cast<juce::uint8> (e.velocity))
                            : juce::MidiMessage::noteOff (1, e.pitch),
                       e.offset);

    previewBuffer.clear();
    previewCollector.removeNextBlockOfMessages (previewBuffer, buffer.getNumSamples());
    midi.addEvents (previewBuffer, 0, buffer.getNumSamples(), 0);

    std::uint64_t lo, hi;
    sequencer.activeMask (lo, hi);
    maskLo = lo; maskHi = hi;
}

// ======================================================= TrackAudioSource

TrackAudioSource::TrackAudioSource (const BlockShared& s)
    : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      shared (s)
{
    pendingClips.reserve (256);
    localClips.reserve (256);
}

void TrackAudioSource::publishClips (const std::vector<AudioClipPlayback>& clips)
{
    const juce::SpinLock::ScopedLockType lock (clipsLock);
    pendingClips = clips;
    ++pendingVersion;
}

void TrackAudioSource::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (! shared.playing || shared.plan.beatsPerSample <= 0.0)
        return;

    {
        const juce::SpinLock::ScopedTryLockType lock (clipsLock);
        if (lock.isLocked() && pendingVersion != localVersion)
        {
            std::swap (localClips, pendingClips);
            localVersion = pendingVersion;
        }
    }

    const double outputRate = getSampleRate();
    if (outputRate <= 0.0)
        return;

    for (int segmentIndex = 0; segmentIndex < shared.plan.count; ++segmentIndex)
    {
        const auto& segment = shared.plan.seg[segmentIndex];
        if (segment.samples <= 0)
            continue;

        for (const auto& clip : localClips)
        {
            if (clip.source == nullptr || clip.source->samples == nullptr
                || clip.source->sampleRate <= 0.0 || clip.lengthBeats <= 0.0)
                continue;

            const double activeStart = std::max (segment.a, clip.startBeat);
            const double activeEnd = std::min (segment.b, clip.startBeat + clip.lengthBeats);
            if (activeEnd <= activeStart)
                continue;

            const auto& source = *clip.source->samples;
            const double projectBpm = shared.plan.beatsPerSample * outputRate * 60.0;
            const double warpRatio = clip.warpEnabled && projectBpm > 0.0
                ? projectBpm / clip.sourceBpm : 1.0;
            const double sourceStep = clip.source->sampleRate / outputRate * warpRatio;
            const double safetyFadeBeats = shared.plan.beatsPerSample * outputRate * 0.004;
            const double maximumFadeBeats = clip.lengthBeats * 0.5;
            const double fadeInBeats = std::min (maximumFadeBeats,
                std::max (clip.fadeInBeats, safetyFadeBeats));
            const double sourceStart = clip.sourceOffsetSeconds * clip.source->sampleRate
                                     + (activeStart - clip.startBeat)
                                       / shared.plan.beatsPerSample * sourceStep;
            const int outputStart = segment.offset + static_cast<int> (std::lround ((activeStart - segment.a) / shared.plan.beatsPerSample));
            const int outputEnd = std::min (buffer.getNumSamples(),
                                            segment.offset + segment.samples);
            const int frameCount = std::min (outputEnd - outputStart,
                static_cast<int> (std::ceil ((activeEnd - activeStart) / shared.plan.beatsPerSample)));

            for (int i = 0; i < frameCount; ++i)
            {
                const double sourcePosition = sourceStart + i * sourceStep;
                const int sourceIndex = static_cast<int> (sourcePosition);
                if (sourceIndex >= source.getNumSamples())
                    break;

                const int nextIndex = std::min (sourceIndex + 1, source.getNumSamples() - 1);
                const float fraction = static_cast<float> (sourcePosition - sourceIndex);
                const double beat = activeStart + i * shared.plan.beatsPerSample;
                double gain = 1.0;
                if (fadeInBeats > 0.0)
                    gain *= roll::audioFadeCurveValue ((beat - clip.startBeat) / fadeInBeats,
                                                       clip.fadeInCurve);
                if (clip.fadeOutBeats > 0.0)
                    gain *= 1.0 - roll::audioFadeCurveValue (
                        1.0 - (clip.startBeat + clip.lengthBeats - beat) / clip.fadeOutBeats,
                        clip.fadeOutCurve);
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const int sourceChannel = std::min (channel, source.getNumChannels() - 1);
                    const float first = source.getSample (sourceChannel, sourceIndex);
                    const float next = source.getSample (sourceChannel, nextIndex);
                    buffer.addSample (channel, outputStart + i,
                                      (first + (next - first) * fraction) * static_cast<float> (gain));
                }
            }
        }
    }
}

// ======================================================= AudioPreviewSource

AudioPreviewSource::AudioPreviewSource()
    : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
}

void AudioPreviewSource::play (std::shared_ptr<const AudioFileData> source)
{
    {
        const juce::SpinLock::ScopedLockType lock (sourceLock);
        pendingSource = std::move (source);
        ++pendingVersion;
    }
    playing.store (true);
}

void AudioPreviewSource::stop()
{
    playing.store (false);
    const juce::SpinLock::ScopedLockType lock (sourceLock);
    pendingSource.reset();
    ++pendingVersion;
}

void AudioPreviewSource::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    {
        const juce::SpinLock::ScopedTryLockType lock (sourceLock);
        if (lock.isLocked() && pendingVersion != localVersion)
        {
            std::swap (localSource, pendingSource);
            localVersion = pendingVersion;
            positionSamples = 0.0;
        }
    }

    if (! playing.load() || localSource == nullptr || localSource->samples == nullptr
        || localSource->sampleRate <= 0.0 || getSampleRate() <= 0.0)
        return;

    const auto& samples = *localSource->samples;
    const double sourceStep = localSource->sampleRate / getSampleRate();
    const float gainStep = (targetGain.load() - currentGain) / static_cast<float> (std::max (1, buffer.getNumSamples()));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        currentGain += gainStep;
        const int firstIndex = static_cast<int> (positionSamples);
        if (firstIndex >= samples.getNumSamples())
        {
            playing.store (false);
            break;
        }

        const int secondIndex = std::min (firstIndex + 1, samples.getNumSamples() - 1);
        const float fraction = static_cast<float> (positionSamples - firstIndex);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const int sourceChannel = std::min (channel, samples.getNumChannels() - 1);
            const float first = samples.getSample (sourceChannel, firstIndex);
            const float second = samples.getSample (sourceChannel, secondIndex);
            buffer.setSample (channel, i, (first + (second - first) * fraction) * currentGain);
        }
        positionSamples += sourceStep;
    }
}

// ======================================================= TrackMixer

class TrackAutomationProcessor final : public juce::AudioProcessor
{
public:
    explicit TrackAutomationProcessor (const BlockShared& state)
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("In", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          shared (state)
    {
    }

    void setAutomation (const std::vector<roll::AutomationEnvelope>& envelopes,
                        const std::vector<std::tuple<juce::String, juce::AudioProcessor*, int>>& devices)
    {
        const juce::CriticalSection::ScopedLockType processorLock (getCallbackLock());
        std::vector<Binding> bindings;
        for (const auto& envelope : envelopes)
        {
            if (envelope.target.type != roll::AutomationParameterType::PluginParameter)
                continue;

            const auto device = std::find_if (devices.begin(), devices.end(),
                [&envelope] (const auto& item)
                {
                    return std::get<0> (item) == juce::String (envelope.target.pluginIdentifier)
                        && (envelope.target.deviceIndex < 0
                            || std::get<2> (item) == envelope.target.deviceIndex);
                });
            if (device == devices.end() || std::get<1> (*device) == nullptr)
                continue;

            auto* processor = std::get<1> (*device);
            const juce::ScopedLock pluginCallbackLock (processor->getCallbackLock());
            int index = 0;
            juce::AudioProcessorParameter* match = nullptr;
            for (auto* candidate : processor->getParameters())
            {
                if (candidate == nullptr || ! candidate->isAutomatable())
                {
                    ++index;
                    continue;
                }
                const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (candidate);
                const auto* parameterWithId =
                    dynamic_cast<const juce::AudioProcessorParameterWithID*> (candidate);
                const auto parameterId = hosted != nullptr ? hosted->getParameterID()
                    : parameterWithId != nullptr ? parameterWithId->paramID : juce::String();
                if ((! envelope.target.parameterId.empty()
                     && parameterId
                        == juce::String::fromUTF8 (envelope.target.parameterId.c_str()))
                    || (envelope.target.parameterId.empty() && index == envelope.target.parameterIndex))
                {
                    match = candidate;
                    break;
                }
                ++index;
            }
            if (match != nullptr)
            {
                float manualValue = match->getValue();
                bool manualValueCaptured = false;
                if (const auto current = std::find_if (localBindings.begin(), localBindings.end(),
                        [match] (const auto& binding) { return binding.parameter == match; });
                    current != localBindings.end())
                {
                    manualValue = current->manualValue;
                    manualValueCaptured = current->manualValueCaptured;
                }
                bindings.push_back ({ processor, match, envelope, std::get<2> (*device),
                                      manualValue, manualValueCaptured });
            }
        }

        const juce::SpinLock::ScopedLockType lock (bindingLock);
        pendingBindings.swap (bindings);
        ++pendingVersion;
    }

    void clearBindings()
    {
        const juce::ScopedLock processorLock (getCallbackLock());
        const juce::SpinLock::ScopedLockType lock (bindingLock);
        pendingBindings.clear();
        localBindings.clear();
        ++pendingVersion;
        localVersion = pendingVersion;
        wasPlaying = false;
        needsRestore = false;
    }

    void setManualOverride (int deviceIndex, int parameterIndex, bool enabled)
    {
        const auto key = overrideKey (deviceIndex, parameterIndex);
        if (enabled)
            manualOverride.store (key, std::memory_order_release);
        else
        {
            auto expected = key;
            manualOverride.compare_exchange_strong (expected, 0, std::memory_order_acq_rel);
        }
    }

    const juce::String getName() const override { return "Track Automation"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        {
            const juce::SpinLock::ScopedTryLockType lock (bindingLock);
            if (lock.isLocked() && pendingVersion != localVersion)
            {
                localBindings.swap (pendingBindings);
                localVersion = pendingVersion;
            }
        }

        const bool playing = shared.playing && shared.plan.count > 0;
        if (! playing)
        {
            if (wasPlaying)
                needsRestore = true;
            if (needsRestore)
            {
                bool restored = true;
                for (const auto& binding : localBindings)
                {
                    if (isManuallyOverridden (binding))
                    {
                        restored = false;
                        continue;
                    }
                    const juce::CriticalSection::ScopedTryLockType processorLock (
                        binding.processor->getCallbackLock());
                    if (processorLock.isLocked())
                        binding.parameter->setValue (binding.manualValueCaptured
                            ? binding.manualValue
                            : static_cast<float> (binding.envelope.target.currentValue));
                    else
                        restored = false;
                }
                needsRestore = ! restored;
            }
            wasPlaying = false;
            return;
        }

        wasPlaying = true;
        needsRestore = false;
        for (auto& binding : localBindings)
        {
            if (isManuallyOverridden (binding))
                continue;
            const juce::CriticalSection::ScopedTryLockType processorLock (
                binding.processor->getCallbackLock());
            if (processorLock.isLocked())
            {
                if (! binding.manualValueCaptured)
                {
                    binding.manualValue = binding.parameter->getValue();
                    binding.manualValueCaptured = true;
                }
                for (int segmentIndex = 0; segmentIndex < shared.plan.count; ++segmentIndex)
                {
                    const auto& segment = shared.plan.seg[segmentIndex];
                    if (segment.samples <= 0)
                        continue;
                    const double beat = segment.a;
                    binding.parameter->setValue (static_cast<float> (
                        roll::automationValueAt (
                            binding.envelope, beat, binding.envelope.target.currentValue)));
                    break;
                }
            }
        }
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    struct Binding
    {
        juce::AudioProcessor* processor = nullptr;
        juce::AudioProcessorParameter* parameter = nullptr;
        roll::AutomationEnvelope envelope;
        int deviceIndex = -1;
        float manualValue = 0.0f;
        bool manualValueCaptured = false;
    };

    static std::uint64_t overrideKey (int deviceIndex, int parameterIndex)
    {
        return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (deviceIndex + 1)) << 32)
             | static_cast<std::uint32_t> (parameterIndex + 1);
    }

    bool isManuallyOverridden (const Binding& binding) const
    {
        return manualOverride.load (std::memory_order_acquire)
            == overrideKey (binding.deviceIndex, binding.envelope.target.parameterIndex);
    }

    const BlockShared& shared;
    juce::SpinLock bindingLock;
    std::vector<Binding> pendingBindings, localBindings;
    std::uint64_t pendingVersion = 0, localVersion = 0;
    std::atomic<std::uint64_t> manualOverride { 0 };
    bool wasPlaying = false, needsRestore = false;
};

TrackMixer::TrackMixer (const BlockShared& state)
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      shared (state)
{
}

void TrackMixer::set (float gain, float pan, bool muted, bool controlMuted)
{
    targetGain.store (gain);
    targetPan.store (pan);
    mute.store (muted);
    controlMute.store (controlMuted);
    liveGainValue.store (gain, std::memory_order_relaxed);
    livePanValue.store (pan, std::memory_order_relaxed);
    liveMuteValue.store (controlMuted, std::memory_order_relaxed);
}

void TrackMixer::setAutomation (const std::vector<roll::AutomationEnvelope>& automation)
{
    const juce::SpinLock::ScopedLockType lock (automationLock);
    pendingAutomation = automation;
    ++pendingAutomationVersion;
}

void TrackMixer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    {
        const juce::SpinLock::ScopedTryLockType lock (automationLock);
        if (lock.isLocked() && pendingAutomationVersion != localAutomationVersion)
        {
            localAutomation.swap (pendingAutomation);
            localAutomationVersion = pendingAutomationVersion;
        }
    }

    const int n = buffer.getNumSamples();
    const float target = mute.load() ? 0.0f : targetGain.load();
    const float pan = juce::jlimit (-1.0f, 1.0f, targetPan.load());
    const auto findEnvelope = [this] (roll::AutomationParameterType type) -> const roll::AutomationEnvelope*
    {
        const auto found = std::find_if (localAutomation.begin(), localAutomation.end(),
            [type] (const auto& envelope) { return envelope.target.type == type; });
        return found == localAutomation.end() ? nullptr : &*found;
    };
    const auto* volumeEnvelope = findEnvelope (roll::AutomationParameterType::Volume);
    const auto* panEnvelope = findEnvelope (roll::AutomationParameterType::Pan);
    const auto* muteEnvelope = findEnvelope (roll::AutomationParameterType::Mute);
    const bool hasMixerAutomation = shared.playing && shared.plan.count > 0
        && (volumeEnvelope != nullptr || panEnvelope != nullptr || muteEnvelope != nullptr);
    float peakNow = 0.0f;
    float peakLeftNow = 0.0f, peakRightNow = 0.0f;
    double sumSquares = 0.0;
    std::size_t sampleCount = 0;

    if (hasMixerAutomation)
    {
        float liveGain = targetGain.load();
        float livePan = pan;
        bool liveMute = controlMute.load();
        for (int i = 0; i < n; ++i)
        {
            double beat = 0.0;
            bool hasBeat = false;
            for (int segmentIndex = 0; segmentIndex < shared.plan.count; ++segmentIndex)
            {
                const auto& segment = shared.plan.seg[segmentIndex];
                if (i >= segment.offset && i < segment.offset + segment.samples)
                {
                    beat = segment.a + static_cast<double> (i - segment.offset) * shared.plan.beatsPerSample;
                    hasBeat = true;
                    break;
                }
            }

            const float automatedGain = hasBeat && volumeEnvelope != nullptr
                ? static_cast<float> (roll::automationValueAt (*volumeEnvelope, beat, targetGain.load()))
                : targetGain.load();
            const float automatedPan = hasBeat && panEnvelope != nullptr
                ? static_cast<float> (roll::automationValueAt (*panEnvelope, beat, pan))
                : pan;
            const bool envelopeMuted = hasBeat && muteEnvelope != nullptr
                && roll::automationValueAt (*muteEnvelope, beat, 0.0) >= 0.5;
            const bool automatedMute = mute.load() || envelopeMuted;
            liveGain = automatedGain;
            livePan = automatedPan;
            liveMute = controlMute.load() || envelopeMuted;
            const float gain = automatedMute ? 0.0f : automatedGain;
            const float left = automatedPan > 0.0f ? 1.0f - automatedPan : 1.0f;
            const float right = automatedPan < 0.0f ? 1.0f + automatedPan : 1.0f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const float side = channel == 0 ? left : right;
                auto* samples = buffer.getWritePointer (channel);
                samples[i] *= gain * side;
                const float level = std::abs (samples[i]);
                peakNow = std::max (peakNow, level);
                if (channel == 0)
                    peakLeftNow = std::max (peakLeftNow, level);
                else if (channel == 1)
                    peakRightNow = std::max (peakRightNow, level);
                sumSquares += static_cast<double> (samples[i]) * samples[i];
                ++sampleCount;
            }
        }
        liveGainValue.store (liveGain, std::memory_order_relaxed);
        livePanValue.store (livePan, std::memory_order_relaxed);
        liveMuteValue.store (liveMute, std::memory_order_relaxed);
        currentGain = target;
        peakLevel = std::max (peakNow, peakLevel.load() * 0.92f);
        if (buffer.getNumChannels() == 1)
            peakRightNow = peakLeftNow;
        peakLeftLevel = std::max (peakLeftNow, peakLeftLevel.load() * 0.92f);
        peakRightLevel = std::max (peakRightNow, peakRightLevel.load() * 0.92f);
        const float rmsNow = sampleCount > 0
            ? static_cast<float> (std::sqrt (sumSquares / static_cast<double> (sampleCount))) : 0.0f;
        rmsLevel = std::max (rmsNow, rmsLevel.load() * 0.92f);
        return;
    }

    const float gl = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float gr = pan < 0.0f ? 1.0f + pan : 1.0f;

    const float start = currentGain;
    const float step = (target - start) / static_cast<float> (std::max (1, n));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const float side = (ch == 0) ? gl : gr;
        float g = start;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            data[i] *= g * side;
            const float level = std::abs (data[i]);
            peakNow = std::max (peakNow, level);
            if (ch == 0)
                peakLeftNow = std::max (peakLeftNow, level);
            else if (ch == 1)
                peakRightNow = std::max (peakRightNow, level);
            sumSquares += static_cast<double> (data[i]) * data[i];
            ++sampleCount;
        }
    }
    currentGain = target;
    liveGainValue.store (targetGain.load(), std::memory_order_relaxed);
    livePanValue.store (pan, std::memory_order_relaxed);
    liveMuteValue.store (controlMute.load(), std::memory_order_relaxed);
    peakLevel = std::max (peakNow, peakLevel.load() * 0.92f);       // queda suave para o medidor
    if (buffer.getNumChannels() == 1)
        peakRightNow = peakLeftNow;
    peakLeftLevel = std::max (peakLeftNow, peakLeftLevel.load() * 0.92f);
    peakRightLevel = std::max (peakRightNow, peakRightLevel.load() * 0.92f);
    const float rmsNow = sampleCount > 0
        ? static_cast<float> (std::sqrt (sumSquares / static_cast<double> (sampleCount))) : 0.0f;
    rmsLevel = std::max (rmsNow, rmsLevel.load() * 0.92f);
}

void TrackMixer::resetPeak()
{
    peakLevel.store (0.0f, std::memory_order_relaxed);
    peakLeftLevel.store (0.0f, std::memory_order_relaxed);
    peakRightLevel.store (0.0f, std::memory_order_relaxed);
    rmsLevel.store (0.0f, std::memory_order_relaxed);
}

// ======================================================= SequencerGraph

void SequencerGraph::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto blockStart = std::chrono::steady_clock::now();
    shared.flush = false;
    const bool nowPlaying = playing.load();

    if (wasPlaying && ! nowPlaying) shared.flush = true;                      // pausa: solta as notas
    wasPlaying = nowPlaying;

    if (rewindRequested.exchange (false)) { shared.flush = true; clock.setPosition (0.0); }
    if (seekRequested.exchange (false))   { shared.flush = true; clock.setPosition (seekTarget.load()); }

    shared.plan.count = 0;
    shared.plan.numSamples = buffer.getNumSamples();
    shared.playing = nowPlaying;

    if (nowPlaying)
    {
        roll::TransportSettings t;
        t.bpm = bpm.load();
        t.loop = loopEnabled.load();
        t.loopStart = loopStart.load();
        t.loopEnd = loopEnd.load();
        clock.plan (t, getSampleRate(), buffer.getNumSamples(), shared.plan);
    }

    positionAtomic = clock.position();
    juce::AudioProcessorGraph::processBlock (buffer, midi);

    const double sampleRate = getSampleRate();
    if (! metronomeEnabled.load (std::memory_order_relaxed)
        || sampleRate <= 0.0 || buffer.getNumChannels() == 0)
    {
        clickSamplesRemaining = 0;
        freeMetronomeBeatPhase = 1.0;
        freeMetronomeBeatNumber = 0;
    }
    else if (nowPlaying)
    {
        freeMetronomeBeatPhase = 1.0;
        int cursor = 0;
        const auto startClick = [this, sampleRate] (bool accentedBeat)
        {
            clickSamplesRemaining = std::max (1, static_cast<int> (sampleRate * 0.012));
            clickPhase = 0.0;
            clickPhaseStep = juce::MathConstants<double>::twoPi
                * (accentedBeat ? 1500.0 : 1000.0) / sampleRate;
            clickEnvelope = metronomeGain.load (std::memory_order_relaxed)
                * (accentedBeat ? 0.9f : 0.65f);
            clickEnvelopeDecay = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.0025)));
        };
        const auto renderUntil = [this, &buffer, &cursor] (int end)
        {
            end = std::clamp (end, cursor, buffer.getNumSamples());
            for (; cursor < end; ++cursor)
            {
                if (clickSamplesRemaining <= 0)
                    continue;

                const float sample = clickEnvelope * static_cast<float> (std::sin (clickPhase));
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.addSample (channel, cursor, sample);

                clickPhase += clickPhaseStep;
                clickEnvelope *= clickEnvelopeDecay;
                --clickSamplesRemaining;
            }
        };

        constexpr double beatEpsilon = 1.0e-9;
        for (int segmentIndex = 0; segmentIndex < shared.plan.count; ++segmentIndex)
        {
            const auto& segment = shared.plan.seg[segmentIndex];
            if (segment.samples <= 0)
                continue;

            for (double beat = std::ceil (segment.a - beatEpsilon);
                 beat < segment.b - beatEpsilon;
                 beat += 1.0)
            {
                const int offset = std::clamp (
                    segment.offset + static_cast<int> (std::lround (
                        (beat - segment.a) / shared.plan.beatsPerSample)),
                    cursor, buffer.getNumSamples());
                renderUntil (offset);

                const auto beatNumber = static_cast<std::int64_t> (std::llround (beat));
                startClick (beatNumber % 4 == 0);
            }
        }
        renderUntil (buffer.getNumSamples());
    }
    else
    {
        const double beatsPerSample = std::clamp (bpm.load (std::memory_order_relaxed), 20.0, 999.0)
                                    / (60.0 * sampleRate);
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            if (freeMetronomeBeatPhase >= 1.0)
            {
                const bool accentedBeat = freeMetronomeBeatNumber % 4 == 0;
                clickSamplesRemaining = std::max (1, static_cast<int> (sampleRate * 0.012));
                clickPhase = 0.0;
                clickPhaseStep = juce::MathConstants<double>::twoPi
                    * (accentedBeat ? 1500.0 : 1000.0) / sampleRate;
                clickEnvelope = metronomeGain.load (std::memory_order_relaxed)
                    * (accentedBeat ? 0.9f : 0.65f);
                clickEnvelopeDecay = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.0025)));
                freeMetronomeBeatPhase -= 1.0;
                ++freeMetronomeBeatNumber;
            }

            if (clickSamplesRemaining > 0)
            {
                const float clickSample = clickEnvelope * static_cast<float> (std::sin (clickPhase));
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.addSample (channel, sampleIndex, clickSample);

                clickPhase += clickPhaseStep;
                clickEnvelope *= clickEnvelopeDecay;
                --clickSamplesRemaining;
            }
            freeMetronomeBeatPhase += beatsPerSample;
        }
    }

    if (sampleRate > 0.0 && buffer.getNumSamples() > 0)
    {
        const double blockDuration = static_cast<double> (buffer.getNumSamples()) / sampleRate;
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - blockStart).count();
        const double current = std::clamp (elapsed / blockDuration, 0.0, 4.0);
        const double alpha = std::clamp (blockDuration, 0.0, 1.0);
        const double average = audioCpuAverageValue.load (std::memory_order_relaxed);
        audioCpuCurrentValue.store (current, std::memory_order_relaxed);
        audioCpuAverageValue.store (average + alpha * (current - average), std::memory_order_relaxed);
    }
}

// ======================================================= PassThroughProcessor (Master Bus In)

class PassThroughProcessor : public juce::AudioProcessor
{
public:
    PassThroughProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "Master Bus In"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

class TrackRangeCaptureProcessor : public juce::AudioProcessor
{
public:
    TrackRangeCaptureProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

    void setDestination (juce::AudioBuffer<float>* buffer)
    {
        destination = buffer;
        writePosition = 0;
    }

    int capturedSamples() const { return writePosition; }

    const juce::String getName() const override { return "Track Range Capture"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override { destination = nullptr; }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (destination == nullptr || buffer.getNumChannels() == 0
            || writePosition >= destination->getNumSamples())
            return;

        const int count = std::min (buffer.getNumSamples(), destination->getNumSamples() - writePosition);
        for (int channel = 0; channel < destination->getNumChannels(); ++channel)
            destination->copyFrom (channel, writePosition, buffer,
                                   std::min (channel, buffer.getNumChannels() - 1), 0, count);
        writePosition += count;
    }
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::AudioBuffer<float>* destination = nullptr;
    int writePosition = 0;
};

// ======================================================= AudioEngine

AudioEngine::AudioEngine()
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    audioFormatManager.registerBasicFormats();

    seqGraph.setPlayConfigDetails (0, 2, 44100.0, 512);
    outputNode = seqGraph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
                     juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    auto previewSource = std::make_unique<AudioPreviewSource>();
    previewSourcePtr = previewSource.get();
    previewNode = seqGraph.addNode (std::move (previewSource));
    for (int channel = 0; channel < 2; ++channel)
        seqGraph.addConnection ({ { previewNode->nodeID, channel }, { outputNode->nodeID, channel } });

    // Trilha Master fixa (ID 0)
    TrackNodes master;
    master.audioTrack = true;
    master.isMaster = true;
    auto busIn = std::make_unique<PassThroughProcessor>();
    master.source = seqGraph.addNode (std::move (busIn));
    auto automation = std::make_unique<TrackAutomationProcessor> (seqGraph.sharedBlock());
    master.automationProcessor = automation.get();
    master.automationNode = seqGraph.addNode (std::move (automation));
    auto mix = std::make_unique<TrackMixer> (seqGraph.sharedBlock());
    master.mixerPtr = mix.get();
    master.mixer = seqGraph.addNode (std::move (mix));
    tracks[masterTrackId] = std::move (master);
    rewireMaster();

    const auto settingsFile = audioDeviceSettingsFile();
    const auto savedSettings = settingsFile.existsAsFile()
        ? juce::XmlDocument::parse (settingsFile) : nullptr;
    if (settingsFile.existsAsFile() && savedSettings == nullptr)
        juce::Logger::writeToLog ("AudioEngine: nao foi possivel ler as preferencias do dispositivo de audio.");

    const auto error = deviceManager.initialise (0, 2, savedSettings.get(), true);
    if (error.isNotEmpty())
        juce::Logger::writeToLog ("AudioEngine: " + error);
    deviceManager.addChangeListener (this);
    player.setProcessor (&seqGraph);
    deviceManager.addAudioCallback (&player);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeChangeListener (this);
    audioLoadPool.removeAllJobs (true, -1);
    previewSourcePtr->stop();
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    seqGraph.clear();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
        saveAudioDeviceSettings();
}

void AudioEngine::saveAudioDeviceSettings()
{
    const auto state = deviceManager.createStateXml();
    if (state == nullptr)
        return;

    if (! state->writeTo (audioDeviceSettingsFile()))
        juce::Logger::writeToLog ("AudioEngine: nao foi possivel salvar as preferencias do dispositivo de audio.");
}

AudioEngine::TrackNodes* AudioEngine::find (int trackId)
{
    auto it = tracks.find (trackId);
    return it == tracks.end() ? nullptr : &it->second;
}

const AudioEngine::TrackNodes* AudioEngine::find (int trackId) const
{
    auto it = tracks.find (trackId);
    return it == tracks.end() ? nullptr : &it->second;
}

void AudioEngine::addTrack (int trackId, roll::TrackType type)
{
    if (find (trackId) != nullptr) return;

    TrackNodes t;
    t.audioTrack = type != roll::TrackType::Midi;
    t.isGroup = type == roll::TrackType::Group;
    if (t.isGroup)
        t.source = seqGraph.addNode (std::make_unique<PassThroughProcessor>());
    else if (type == roll::TrackType::Audio)
    {
        auto src = std::make_unique<TrackAudioSource> (seqGraph.sharedBlock());
        t.audioSourcePtr = src.get();
        t.source = seqGraph.addNode (std::move (src));
    }
    else
    {
        auto src = std::make_unique<TrackSource> (seqGraph.sharedBlock());
        t.sourcePtr = src.get();
        t.source = seqGraph.addNode (std::move (src));
    }

    auto automation = std::make_unique<TrackAutomationProcessor> (seqGraph.sharedBlock());
    t.automationProcessor = automation.get();
    t.automationNode = seqGraph.addNode (std::move (automation));
    auto mix = std::make_unique<TrackMixer> (seqGraph.sharedBlock());
    t.mixerPtr = mix.get();
    t.mixer = seqGraph.addNode (std::move (mix));

    tracks[trackId] = std::move (t);
    rewire (tracks[trackId]);
}

void AudioEngine::removeTrack (int trackId)
{
    if (trackId == masterTrackId) return;
    auto* t = find (trackId);
    if (t == nullptr) return;

    static_cast<TrackAutomationProcessor*> (t->automationProcessor)->clearBindings();
    for (auto& d : t->devices) seqGraph.removeNode (d.node->nodeID);
    seqGraph.removeNode (t->source->nodeID);
    seqGraph.removeNode (t->automationNode->nodeID);
    seqGraph.removeNode (t->mixer->nodeID);
    tracks.erase (trackId);
}

void AudioEngine::clearConnectionsOf (juce::AudioProcessorGraph::NodeID id)
{
    for (const auto& c : seqGraph.getConnections())
        if (c.source.nodeID == id || c.destination.nodeID == id)
            seqGraph.removeConnection (c);
}

void AudioEngine::clearOutputConnectionsOf (juce::AudioProcessorGraph::NodeID id)
{
    for (const auto& c : seqGraph.getConnections())
        if (c.source.nodeID == id)
            seqGraph.removeConnection (c);
}

void AudioEngine::rewireMaster()
{
    auto* m = find (masterTrackId);
    if (m == nullptr) return;

    clearOutputConnectionsOf (m->source->nodeID);
    clearConnectionsOf (m->automationNode->nodeID);
    clearConnectionsOf (m->mixer->nodeID);
    for (auto& d : m->devices) clearConnectionsOf (d.node->nodeID);
    std::vector<std::tuple<juce::String, juce::AudioProcessor*, int>> devices;
    for (std::size_t i = 0; i < m->devices.size(); ++i)
        devices.emplace_back (m->devices[i].pluginIdentifier, m->devices[i].node->getProcessor(),
                              static_cast<int> (i));
    static_cast<TrackAutomationProcessor*> (m->automationProcessor)
        ->setAutomation (m->automation, devices);

    for (int ch = 0; ch < 2; ++ch)
        seqGraph.addConnection ({ { m->mixer->nodeID, ch }, { outputNode->nodeID, ch } });

    for (int ch = 0; ch < 2; ++ch)
        seqGraph.addConnection ({ { m->source->nodeID, ch }, { m->automationNode->nodeID, ch } });

    auto previous = m->automationNode;
    for (const auto& device : m->devices)
    {
        auto* processor = device.node->getProcessor();
        if (processor->getTotalNumInputChannels() <= 0
            || processor->getTotalNumOutputChannels() <= 0)
            continue;
        if (connectAudioChannels (seqGraph, previous, device.node))
            previous = device.node;
    }
    connectAudioChannels (seqGraph, previous, m->mixer);
}

// Cadeia: MIDI -> instrumento -> efeitos ou audio -> efeitos -> mixer -> Master / Ext. Out
void AudioEngine::rewire (TrackNodes& t)
{
    if (t.isMaster)
    {
        rewireMaster();
        return;
    }

    if (t.isGroup)
        clearOutputConnectionsOf (t.source->nodeID);
    else
        clearConnectionsOf (t.source->nodeID);
    clearConnectionsOf (t.automationNode->nodeID);
    clearConnectionsOf (t.mixer->nodeID);
    for (auto& d : t.devices) clearConnectionsOf (d.node->nodeID);

    using AG = juce::AudioProcessorGraph;
    const int midi = AG::midiChannelIndex;
    std::vector<std::tuple<juce::String, juce::AudioProcessor*, int>> devices;
    for (std::size_t i = 0; i < t.devices.size(); ++i)
        devices.emplace_back (t.devices[i].pluginIdentifier, t.devices[i].node->getProcessor(),
                              static_cast<int> (i));
    static_cast<TrackAutomationProcessor*> (t.automationProcessor)
        ->setAutomation (t.automation, devices);

    auto* master = find (masterTrackId);
    auto* parentGroup = t.parentGroupId != 0 ? find (t.parentGroupId) : nullptr;
    auto destNode = (t.routing == roll::TrackOutputRouting::ExtOut || master == nullptr)
                        ? outputNode
                        : parentGroup != nullptr && parentGroup->isGroup ? parentGroup->source
                                                                         : master->source;

    for (int ch = 0; ch < 2; ++ch)
        seqGraph.addConnection ({ { t.mixer->nodeID, ch }, { destNode->nodeID, ch } });

    if (t.audioTrack)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            seqGraph.addConnection ({ { t.source->nodeID, ch }, { t.automationNode->nodeID, ch } });
        }

        auto previous = t.automationNode;
        for (const auto& device : t.devices)
        {
            auto* processor = device.node->getProcessor();
            if (processor->getTotalNumInputChannels() <= 0
                || processor->getTotalNumOutputChannels() <= 0)
                continue;
            if (connectAudioChannels (seqGraph, previous, device.node))
                previous = device.node;
        }
        connectAudioChannels (seqGraph, previous, t.mixer);
        return;
    }

    if (t.devices.empty() || ! t.devices.front().instrument)
        return;

    seqGraph.addConnection ({ { t.source->nodeID, midi }, { t.automationNode->nodeID, midi } });
    seqGraph.addConnection ({ { t.automationNode->nodeID, midi },
                              { t.devices.front().node->nodeID, midi } });

    auto previous = t.devices.front().node;
    for (std::size_t i = 1; i < t.devices.size(); ++i)
    {
        auto* processor = t.devices[i].node->getProcessor();
        if (processor->getTotalNumInputChannels() <= 0
            || processor->getTotalNumOutputChannels() <= 0)
            continue;
        if (connectAudioChannels (seqGraph, previous, t.devices[i].node))
            previous = t.devices[i].node;
    }
    connectAudioChannels (seqGraph, previous, t.mixer);
}

void AudioEngine::publishNotes (int trackId, const std::vector<roll::Note>& notes)
{
    if (auto* t = find (trackId); t != nullptr && t->sourcePtr != nullptr) t->sourcePtr->publishNotes (notes);
}

void AudioEngine::publishAudioClips (int trackId, const std::vector<AudioClipPlayback>& clips)
{
    if (auto* t = find (trackId); t != nullptr && t->audioSourcePtr != nullptr)
        t->audioSourcePtr->publishClips (clips);
}

void AudioEngine::setMixer (int trackId, float gain, float pan, bool muted)
{
    setMixer (trackId, gain, pan, muted, muted);
}

void AudioEngine::setMixer (int trackId, float gain, float pan, bool muted, bool controlMuted)
{
    if (auto* t = find (trackId)) t->mixerPtr->set (gain, pan, muted, controlMuted);
}

void AudioEngine::setTrackAutomation (int trackId,
                                      const std::vector<roll::AutomationEnvelope>& automation)
{
    auto* track = find (trackId);
    if (track == nullptr)
        return;
    if (sameAutomation (track->automation, automation))
        return;

    track->automation = automation;
    track->mixerPtr->setAutomation (automation);
    std::vector<std::tuple<juce::String, juce::AudioProcessor*, int>> devices;
    for (std::size_t i = 0; i < track->devices.size(); ++i)
        devices.emplace_back (track->devices[i].pluginIdentifier,
                              track->devices[i].node->getProcessor(), static_cast<int> (i));
    static_cast<TrackAutomationProcessor*> (track->automationProcessor)
        ->setAutomation (automation, devices);
}

void AudioEngine::setPluginAutomationOverride (
    int trackId, int deviceId, int parameterIndex, bool enabled)
{
    auto* track = find (trackId);
    if (track == nullptr || track->automationProcessor == nullptr || parameterIndex < 0)
        return;

    for (std::size_t deviceIndex = 0; deviceIndex < track->devices.size(); ++deviceIndex)
        if (track->devices[deviceIndex].id == deviceId)
        {
            static_cast<TrackAutomationProcessor*> (track->automationProcessor)
                ->setManualOverride (static_cast<int> (deviceIndex), parameterIndex, enabled);
            return;
        }
}

void AudioEngine::previewNote (int trackId, int pitch, bool on, float velocity)
{
    if (auto* t = find (trackId)) t->sourcePtr->preview (pitch, on, velocity);
}

void AudioEngine::activeMask (int trackId, std::uint64_t& lo, std::uint64_t& hi) const
{
    lo = hi = 0;
    if (auto* t = find (trackId); t != nullptr && t->sourcePtr != nullptr)
        t->sourcePtr->activeMask (lo, hi);
}

float AudioEngine::mixerGain (int trackId) const
{
    const auto* t = find (trackId);
    return t != nullptr && t->mixerPtr != nullptr ? t->mixerPtr->liveGain() : 1.0f;
}

float AudioEngine::mixerPan (int trackId) const
{
    const auto* t = find (trackId);
    return t != nullptr && t->mixerPtr != nullptr ? t->mixerPtr->livePan() : 0.0f;
}

bool AudioEngine::mixerMuted (int trackId) const
{
    const auto* t = find (trackId);
    return t != nullptr && t->mixerPtr != nullptr && t->mixerPtr->liveMute();
}

float AudioEngine::peak (int trackId) const
{
    auto* t = find (trackId);
    return t != nullptr ? t->mixerPtr->peak() : 0.0f;
}

float AudioEngine::rms (int trackId) const
{
    auto* t = find (trackId);
    return t != nullptr ? t->mixerPtr->rms() : 0.0f;
}

float AudioEngine::masterPeak() const
{
    return peak (masterTrackId);
}

std::pair<float, float> AudioEngine::masterPeakStereo() const
{
    const auto* master = find (masterTrackId);
    if (master == nullptr || master->mixerPtr == nullptr)
        return { 0.0f, 0.0f };
    return { master->mixerPtr->peakLeft(), master->mixerPtr->peakRight() };
}

void AudioEngine::setTrackRouting (int trackId, roll::TrackOutputRouting routing, int parentGroupId)
{
    if (auto* t = find (trackId))
    {
        if (t->routing != routing || t->parentGroupId != parentGroupId)
        {
            t->routing = routing;
            t->parentGroupId = parentGroupId;
            rewire (*t);
        }
    }
}

void AudioEngine::setMasterMixer (float gain, float pan, bool muted)
{
    setMixer (masterTrackId, gain, pan, muted);
}

void AudioEngine::resetPeak (int trackId)
{
    if (auto* t = find (trackId); t != nullptr && t->mixerPtr != nullptr)
        t->mixerPtr->resetPeak();
}

void AudioEngine::loadAudioFileAsync (
    const juce::File& file,
    std::function<void (const juce::String&, std::shared_ptr<const AudioFileData>)> callback)
{
    audioLoadPool.addJob ([this, file, callback = std::move (callback)]
    {
        decodeAudioFile (audioFormatManager, file, callback);
    });
}

void AudioEngine::previewAudio (std::shared_ptr<const AudioFileData> source)
{
    previewSourcePtr->play (std::move (source));
}

void AudioEngine::stopAudioPreview()
{
    previewSourcePtr->stop();
}

void AudioEngine::setAudioPreviewGain (float gain)
{
    previewSourcePtr->setGain (gain);
}

bool AudioEngine::beginOfflineRender (juce::String& error)
{
    return beginOfflineRenderInternal (error, -1);
}

bool AudioEngine::beginOfflineTrackRender (int trackId, juce::String& error)
{
    return beginOfflineRenderInternal (error, trackId);
}

bool AudioEngine::beginOfflineRenderInternal (juce::String& error, int captureTrackId)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (offlineRenderActive)
    {
        error = "An audio export is already in progress.";
        return false;
    }
    if (seqGraph.isPlaying())
    {
        error = "Stop playback before exporting audio.";
        return false;
    }
    auto* captureTrack = captureTrackId >= 0 ? find (captureTrackId) : nullptr;
    if (captureTrackId >= 0 && (captureTrack == nullptr || ! captureTrack->audioTrack))
    {
        error = "The selected track cannot be rendered as audio.";
        return false;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    offlineRenderRate = captureTrackId >= 0 ? 44100.0
        : device != nullptr ? device->getCurrentSampleRate() : seqGraph.getSampleRate();
    offlineRenderBlockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : seqGraph.getBlockSize();
    if (! std::isfinite (offlineRenderRate) || offlineRenderRate <= 0.0
        || offlineRenderBlockSize <= 0)
    {
        error = "The audio device has no valid sample rate or buffer size.";
        return false;
    }

    previousPosition = seqGraph.positionBeats();
    previousLoopEnabled = seqGraph.isLoop();
    previousMetronomeEnabled = seqGraph.isMetronomeEnabled();
    previousMetronomeGain = seqGraph.getMetronomeGain();

    deviceManager.removeAudioCallback (&player);
    seqGraph.setPlayConfigDetails (0, 2, offlineRenderRate, offlineRenderBlockSize);
    seqGraph.setNonRealtime (true);
    seqGraph.setLoop (false);
    seqGraph.setMetronomeEnabled (false);
    previewSourcePtr->stop();
    offlineRenderActive = true;

    if (captureTrack != nullptr)
    {
        auto capture = std::make_unique<TrackRangeCaptureProcessor>();
        offlineCaptureProcessor = capture.get();
        offlineCaptureNode = seqGraph.addNode (std::move (capture));
        if (offlineCaptureNode == nullptr)
        {
            offlineCaptureProcessor = nullptr;
            error = "Could not create the track render capture node.";
            finishOfflineRender();
            return false;
        }

        auto source = captureTrack->devices.empty() ? captureTrack->source : captureTrack->devices.back().node;
        auto* sourceProcessor = source != nullptr ? source->getProcessor() : nullptr;
        const int sourceOutputs = sourceProcessor != nullptr
            ? std::max (1, sourceProcessor->getTotalNumOutputChannels()) : 0;
        if (source == nullptr || sourceOutputs <= 0)
        {
            error = "The selected track has no renderable audio output.";
            finishOfflineRender();
            return false;
        }

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto sourceChannel = juce::AudioProcessorGraph::NodeAndChannel {
                source->nodeID, std::min (channel, sourceOutputs - 1) };
            const auto captureInput = juce::AudioProcessorGraph::NodeAndChannel {
                offlineCaptureNode->nodeID, channel };
            const auto captureOutput = juce::AudioProcessorGraph::NodeAndChannel {
                offlineCaptureNode->nodeID, channel };
            const auto graphOutput = juce::AudioProcessorGraph::NodeAndChannel {
                outputNode->nodeID, channel };

            if (! seqGraph.addConnection ({ sourceChannel, captureInput })
                || ! seqGraph.addConnection ({ captureOutput, graphOutput }))
            {
                error = "Could not connect the track render capture node.";
                finishOfflineRender();
                return false;
            }
        }
    }

    seqGraph.prepareToPlay (offlineRenderRate, offlineRenderBlockSize);
    seqGraph.seek (0.0);
    seqGraph.setPlaying (captureTrackId < 0);
    return true;
}

juce::Result AudioEngine::renderMasterToWav (const juce::File& file,
                                             double durationSeconds,
                                             std::function<void (double)> onProgress,
                                             std::function<bool()> shouldCancel)
{
    if (! offlineRenderActive)
        return juce::Result::fail ("Offline rendering was not prepared.");
    if (! std::isfinite (durationSeconds) || durationSeconds <= 0.0)
        return juce::Result::fail ("The requested render duration is invalid.");

    const double frameCount = std::ceil (durationSeconds * offlineRenderRate);
    constexpr std::uint64_t maxWavDataBytes = 0xffffffffULL - 4096;
    constexpr std::uint64_t bytesPerStereoFrame = 2 * 3;
    const auto maxFrames = maxWavDataBytes / bytesPerStereoFrame;
    if (! std::isfinite (frameCount) || frameCount < 1.0
        || frameCount > static_cast<double> (maxFrames))
        return juce::Result::fail ("The render is too long for a standard WAV file.");

    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (! fileStream->openedOk())
        return juce::Result::fail ("Could not create the WAV file.");

    std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));
    juce::WavAudioFormat wavFormat;
    const juce::AudioFormatWriterOptions writerOptions;
    auto writer = wavFormat.createWriterFor (outputStream,
        writerOptions.withSampleRate (offlineRenderRate)
                     .withNumChannels (2)
                     .withBitsPerSample (24));
    if (writer == nullptr)
    {
        outputStream.reset();
        file.deleteFile();
        return juce::Result::fail ("Could not initialise the WAV writer.");
    }

    const auto totalFrames = static_cast<std::uint64_t> (frameCount);
    juce::AudioBuffer<float> buffer (2, offlineRenderBlockSize);
    juce::MidiBuffer midi;
    std::uint64_t renderedFrames = 0;
    while (renderedFrames < totalFrames)
    {
        if (shouldCancel && shouldCancel())
        {
            writer.reset();
            outputStream.reset();
            file.deleteFile();
            return juce::Result::fail ("Audio export cancelled.");
        }

        buffer.clear();
        midi.clear();
        seqGraph.processBlock (buffer, midi);

        const int framesToWrite = static_cast<int> (std::min<std::uint64_t> (
            static_cast<std::uint64_t> (offlineRenderBlockSize), totalFrames - renderedFrames));
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, framesToWrite))
        {
            writer.reset();
            outputStream.reset();
            file.deleteFile();
            return juce::Result::fail ("Failed while writing the WAV file.");
        }

        renderedFrames += static_cast<std::uint64_t> (framesToWrite);
        if (onProgress)
            onProgress (static_cast<double> (renderedFrames) / static_cast<double> (totalFrames));
    }

    writer.reset();
    return juce::Result::ok();
}

juce::Result AudioEngine::renderTrackRange (double startBeat,
                                            double lengthBeats,
                                            juce::AudioBuffer<float>& destination,
                                            std::function<void (double)> onProgress,
                                            std::function<bool()> shouldCancel)
{
    if (! offlineRenderActive || offlineCaptureProcessor == nullptr)
        return juce::Result::fail ("Track rendering was not prepared.");
    if (! std::isfinite (startBeat) || startBeat < 0.0
        || ! std::isfinite (lengthBeats) || lengthBeats <= 0.0)
        return juce::Result::fail ("The requested track range is invalid.");

    const double bpm = seqGraph.getBpm();
    if (! std::isfinite (bpm) || bpm <= 0.0)
        return juce::Result::fail ("The project tempo is invalid.");
    const double frameCount = std::ceil (lengthBeats * 60.0 / bpm * offlineRenderRate);
    if (! std::isfinite (frameCount) || frameCount < 1.0
        || frameCount > destination.getNumSamples()
        || destination.getNumChannels() < 2)
        return juce::Result::fail ("The track render buffer has invalid dimensions.");

    auto* capture = static_cast<TrackRangeCaptureProcessor*> (offlineCaptureProcessor);
    destination.clear();
    capture->setDestination (&destination);
    seqGraph.seek (startBeat);
    seqGraph.setPlaying (true);

    juce::AudioBuffer<float> graphBuffer (2, offlineRenderBlockSize);
    juce::MidiBuffer midi;
    const auto totalFrames = static_cast<std::uint64_t> (frameCount);
    std::uint64_t renderedFrames = 0;
    while (renderedFrames < totalFrames)
    {
        if (shouldCancel && shouldCancel())
        {
            seqGraph.setPlaying (false);
            capture->setDestination (nullptr);
            return juce::Result::fail ("Track render cancelled.");
        }

        graphBuffer.clear();
        midi.clear();
        seqGraph.processBlock (graphBuffer, midi);

        const auto framesThisBlock = std::min<std::uint64_t> (
            static_cast<std::uint64_t> (offlineRenderBlockSize), totalFrames - renderedFrames);
        renderedFrames += framesThisBlock;
        if (onProgress)
            onProgress (static_cast<double> (renderedFrames) / static_cast<double> (totalFrames));
    }

    seqGraph.setPlaying (false);
    capture->setDestination (nullptr);
    return juce::Result::ok();
}

void AudioEngine::finishOfflineRender()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (! offlineRenderActive)
        return;

    seqGraph.setPlaying (false);
    if (offlineCaptureNode != nullptr)
    {
        seqGraph.removeNode (offlineCaptureNode->nodeID);
        offlineCaptureNode.reset();
        offlineCaptureProcessor = nullptr;
    }
    seqGraph.setLoop (previousLoopEnabled);
    seqGraph.setMetronomeEnabled (previousMetronomeEnabled);
    seqGraph.setMetronomeGain (previousMetronomeGain);
    seqGraph.seek (previousPosition);
    seqGraph.setNonRealtime (false);
    seqGraph.releaseResources();
    deviceManager.addAudioCallback (&player);
    offlineRenderActive = false;
}

bool AudioEngine::canAcceptPlugin (int trackId, const juce::PluginDescription& description) const
{
    const auto* t = find (trackId);
    if (t == nullptr) return false;
    if (t->isMaster) return ! description.isInstrument;
    return ! t->audioTrack || ! description.isInstrument;
}

void AudioEngine::addDevice (int trackId, const juce::PluginDescription& desc,
                             std::function<void (const juce::String&, int)> onDone, int insertionIndex)
{
    auto* track = find (trackId);
    if (track == nullptr) { onDone ("Trilha inexistente.", 0); return; }
    if (track->isMaster && desc.isInstrument) { onDone ("A trilha Master aceita apenas efeitos.", 0); return; }
    if (track->audioTrack && desc.isInstrument) { onDone ("Canais de audio aceitam apenas efeitos.", 0); return; }

    const auto pluginIdentifier = desc.createIdentifierString();
    for (const auto& device : find (trackId)->devices)
        if (device.pluginIdentifier == pluginIdentifier)
        {
            onDone ({}, device.id);
            return;
        }

    const auto pendingKey = std::make_pair (trackId, pluginIdentifier.toStdString());
    if (! pendingPluginLoads.insert (pendingKey).second)
    {
        onDone ("Este plugin ja esta sendo carregado nesta trilha.", 0);
        return;
    }

    double sampleRate = 44100.0;
    int blockSize = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = dev->getCurrentSampleRate();
        blockSize  = dev->getCurrentBufferSizeSamples();
    }

    auto instance = std::make_shared<std::unique_ptr<RemotePluginProcessor>>();
    auto loadError = std::make_shared<juce::String>();
    audioLoadPool.addJob ([this, desc, sampleRate, blockSize, trackId, pluginIdentifier,
                           pendingKey, onDone, insertionIndex, instance, loadError]
    {
        *instance = RemotePluginProcessor::create (desc, sampleRate, blockSize, *loadError);
        if (! juce::MessageManager::callAsync (
            [this, trackId, pluginIdentifier, pendingKey, onDone, insertionIndex, instance, loadError]
        {
            pendingPluginLoads.erase (pendingKey);
            auto* t = find (trackId);
            if (*instance == nullptr || t == nullptr)
            {
                onDone (loadError->isNotEmpty() ? *loadError : "Falha ao carregar o plugin.", 0);
                return;
            }

            Device d;
            d.id = nextDeviceId++;
            d.name = (*instance)->getName();
            d.pluginIdentifier = pluginIdentifier;
            d.instrument = (*instance)->isInstrument();
            const auto failureCallback = onPluginCrashed;
            (*instance)->setFailureCallback ([failureCallback, trackId, deviceId = d.id]
                                             (const juce::String& name)
            {
                if (failureCallback)
                    failureCallback (trackId, deviceId, name);
            });
            d.node = seqGraph.addNode (std::move (*instance));
            if (d.node == nullptr)
            {
                onDone ("Nao foi possivel adicionar o plugin ao grafo de audio.", 0);
                return;
            }

            if (d.instrument)
            {
                // um instrumento por trilha: o novo substitui o anterior
                if (! t->devices.empty() && t->devices.front().instrument)
                {
                    static_cast<TrackAutomationProcessor*> (t->automationProcessor)->clearBindings();
                    d.groupId = t->devices.front().groupId;
                    seqGraph.removeNode (t->devices.front().node->nodeID);
                    t->devices.erase (t->devices.begin());
                }
                t->devices.insert (t->devices.begin(), d);
            }
            else
            {
                if (insertionIndex < 0)
                    t->devices.push_back (d);
                else
                {
                    const int firstEffect = ! t->devices.empty() && t->devices.front().instrument ? 1 : 0;
                    const int index = std::clamp (insertionIndex, firstEffect,
                                                  static_cast<int> (t->devices.size()));
                    t->devices.insert (t->devices.begin() + index, d);
                }
            }

            rewire (*t);
            onDone ({}, d.id);
        }))
            juce::Logger::writeToLog ("AudioEngine: nao foi possivel publicar o resultado do carregamento do plugin.");
    });
}

int AudioEngine::addDrumRack (int trackId)
{
    auto* track = find (trackId);
    if (track == nullptr || track->isMaster || track->audioTrack)
        return 0;

    for (const auto& device : track->devices)
        if (device.pluginIdentifier == drumRackPluginIdentifier)
            return device.id;

    Device device;
    device.id = nextDeviceId++;
    device.name = "Drum Rack";
    device.pluginIdentifier = drumRackPluginIdentifier;
    device.instrument = true;
    device.node = seqGraph.addNode (std::make_unique<DrumRackProcessor>());
    if (device.node == nullptr)
        return 0;

    if (! track->devices.empty() && track->devices.front().instrument)
    {
        static_cast<TrackAutomationProcessor*> (track->automationProcessor)->clearBindings();
        device.groupId = track->devices.front().groupId;
        seqGraph.removeNode (track->devices.front().node->nodeID);
        track->devices.erase (track->devices.begin());
    }

    track->devices.insert (track->devices.begin(), std::move (device));
    rewire (*track);
    return track->devices.front().id;
}

int AudioEngine::addUtility (int trackId)
{
    auto* track = find (trackId);
    if (track == nullptr)
        return 0;
    for (const auto& device : track->devices)
        if (device.pluginIdentifier == utilityPluginIdentifier)
            return device.id;

    Device device;
    device.id = nextDeviceId++;
    device.name = "Volume Master";
    device.pluginIdentifier = utilityPluginIdentifier;
    device.instrument = false;
    device.node = seqGraph.addNode (std::make_unique<UtilityProcessor>());
    if (device.node == nullptr)
        return 0;
    track->devices.push_back (std::move (device));
    rewire (*track);
    return track->devices.back().id;
}

void AudioEngine::addPadEffect (int trackId, int rackDeviceId, int pad,
                                const juce::PluginDescription& description,
                                std::function<void (const juce::String&)> onDone)
{
    auto* track = find (trackId);
    auto* rack = track != nullptr
        ? [&]() -> DrumRackProcessor*
          {
              for (auto& device : track->devices)
                  if (device.id == rackDeviceId)
                      return dynamic_cast<DrumRackProcessor*> (device.node->getProcessor());
              return nullptr;
          }()
        : nullptr;
    if (rack == nullptr || pad < 0 || pad >= DrumRackProcessor::numPads || description.isInstrument)
    {
        onDone ("Selecione um efeito valido e um pad do Drum Rack.");
        return;
    }

    double sampleRate = 44100.0;
    int blockSize = 512;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    auto instance = std::make_shared<std::unique_ptr<RemotePluginProcessor>>();
    auto loadError = std::make_shared<juce::String>();
    audioLoadPool.addJob ([this, trackId, rackDeviceId, pad, description, onDone,
                           sampleRate, blockSize, instance, loadError]
    {
        *instance = RemotePluginProcessor::create (description, sampleRate, blockSize, *loadError);
        if (! juce::MessageManager::callAsync (
            [this, trackId, rackDeviceId, pad, description, onDone, instance, loadError]
        {
            auto* targetTrack = find (trackId);
            DrumRackProcessor* targetRack = nullptr;
            if (targetTrack != nullptr)
                for (auto& device : targetTrack->devices)
                    if (device.id == rackDeviceId)
                        targetRack = dynamic_cast<DrumRackProcessor*> (device.node->getProcessor());

            if (*instance == nullptr || targetRack == nullptr)
            {
                onDone (loadError->isNotEmpty() ? *loadError : "O Drum Rack nao esta mais disponivel.");
                return;
            }

            const auto failureCallback = onPluginCrashed;
            (*instance)->setFailureCallback ([failureCallback, trackId, rackDeviceId]
                                             (const juce::String& name)
            {
                if (failureCallback)
                    failureCallback (trackId, rackDeviceId, name);
            });
            juce::String addError;
            if (! targetRack->addPadEffect (pad, description, std::move (*instance), addError))
            {
                onDone (addError);
                return;
            }
            onDone ({});
        }))
            juce::Logger::writeToLog ("AudioEngine: nao foi possivel publicar o resultado do carregamento de efeito.");
    });
}

std::unique_ptr<juce::AudioProcessorEditor> AudioEngine::createPadEffectEditor (
    int trackId, int rackDeviceId, int pad, int effectIndex)
{
    auto* track = find (trackId);
    if (track == nullptr)
        return nullptr;
    for (auto& device : track->devices)
        if (device.id == rackDeviceId)
            if (auto* rack = dynamic_cast<DrumRackProcessor*> (device.node->getProcessor()))
                if (auto processor = rack->padEffectProcessor (pad, effectIndex);
                    processor != nullptr && processor->hasEditor())
                    return std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditorAndMakeActive());
    return nullptr;
}

void AudioEngine::removeDevice (int trackId, int deviceId)
{
    auto* t = find (trackId);
    if (t == nullptr) return;

    static_cast<TrackAutomationProcessor*> (t->automationProcessor)->clearBindings();
    for (auto it = t->devices.begin(); it != t->devices.end(); ++it)
        if (it->id == deviceId)
        {
            seqGraph.removeNode (it->node->nodeID);
            t->devices.erase (it);
            break;
        }

    std::map<int, int> groupSizes;
    for (const auto& device : t->devices)
        if (device.groupId > 0)
            ++groupSizes[device.groupId];
    for (auto& device : t->devices)
        if (device.groupId > 0 && groupSizes[device.groupId] < 2)
            device.groupId = 0;

    rewire (*t);
}

bool AudioEngine::moveDevice (int trackId, int deviceId, int direction)
{
    if (direction != -1 && direction != 1)
        return false;

    auto* t = find (trackId);
    if (t == nullptr)
        return false;

    const auto device = std::find_if (t->devices.begin(), t->devices.end(),
        [deviceId] (const Device& candidate) { return candidate.id == deviceId; });
    if (device == t->devices.end())
        return false;

    const auto index = static_cast<std::ptrdiff_t> (device - t->devices.begin());
    const auto targetIndex = index + direction;
    if (targetIndex < 0 || targetIndex >= static_cast<std::ptrdiff_t> (t->devices.size()))
        return false;

    auto target = t->devices.begin() + targetIndex;
    if (device->instrument || target->instrument || device->groupId > 0 || target->groupId > 0)
        return false;

    std::iter_swap (device, target);
    rewire (*t);
    return true;
}

bool AudioEngine::reorderDevice (int trackId, int deviceId, int targetDeviceId, bool afterTarget)
{
    auto* t = find (trackId);
    if (t == nullptr || deviceId == targetDeviceId)
        return false;

    const auto source = std::find_if (t->devices.begin(), t->devices.end(),
        [deviceId] (const Device& candidate) { return candidate.id == deviceId; });
    const auto target = std::find_if (t->devices.begin(), t->devices.end(),
        [targetDeviceId] (const Device& candidate) { return candidate.id == targetDeviceId; });
    if (source == t->devices.end() || target == t->devices.end()
        || source->instrument || target->instrument
        || source->groupId > 0 || target->groupId > 0)
        return false;

    const auto sourceIndex = static_cast<std::ptrdiff_t> (source - t->devices.begin());
    const auto targetIndex = static_cast<std::ptrdiff_t> (target - t->devices.begin());
    auto insertIndex = targetIndex + (afterTarget ? 1 : 0);
    if (sourceIndex < insertIndex)
        --insertIndex;
    if (insertIndex == sourceIndex)
        return false;

    auto device = std::move (*source);
    t->devices.erase (source);
    t->devices.insert (t->devices.begin() + insertIndex, std::move (device));
    rewire (*t);
    return true;
}

bool AudioEngine::groupDevices (int trackId, const std::vector<int>& deviceIds)
{
    auto* t = find (trackId);
    if (t == nullptr || deviceIds.size() < 2)
        return false;

    const std::set<int> selected (deviceIds.begin(), deviceIds.end());
    if (selected.size() != deviceIds.size())
        return false;

    std::vector<std::size_t> indices;
    indices.reserve (deviceIds.size());
    for (const auto id : selected)
    {
        const auto found = std::find_if (t->devices.begin(), t->devices.end(),
            [id] (const Device& device) { return device.id == id; });
        if (found == t->devices.end())
            return false;
        indices.push_back (static_cast<std::size_t> (found - t->devices.begin()));
    }

    std::sort (indices.begin(), indices.end());
    if (indices.back() - indices.front() + 1 != indices.size())
        return false;
    if (std::any_of (indices.begin(), indices.end(),
                     [t] (std::size_t index) { return t->devices[index].instrument && index != 0; }))
        return false;

    for (std::size_t i = 0; i < t->devices.size(); ++i)
        if (indices.front() <= i && i <= indices.back()
            && t->devices[i].groupId > 0 && ! selected.contains (t->devices[i].id))
            return false;

    for (const auto& device : t->devices)
        if (device.groupId > 0 && selected.contains (device.id))
            for (const auto& member : t->devices)
                if (member.groupId == device.groupId && ! selected.contains (member.id))
                    return false;

    const int groupId = nextDeviceGroupId++;
    for (const auto index : indices)
        t->devices[index].groupId = groupId;

    return true;
}

bool AudioEngine::ungroupDevices (int trackId, const std::vector<int>& deviceIds)
{
    auto* t = find (trackId);
    if (t == nullptr || deviceIds.empty())
        return false;

    std::set<int> groupIds;
    for (const auto id : deviceIds)
        for (const auto& device : t->devices)
            if (device.id == id && device.groupId > 0)
                groupIds.insert (device.groupId);

    if (groupIds.empty())
        return false;

    for (auto& device : t->devices)
        if (groupIds.contains (device.groupId))
            device.groupId = 0;

    return true;
}

void AudioEngine::setDeviceBypassed (int trackId, int deviceId, bool bypassed)
{
    if (auto* t = find (trackId))
        for (auto& d : t->devices)
            if (d.id == deviceId)
            {
                d.node->setBypassed (bypassed);
                break;
            }
}

std::vector<DeviceInfo> AudioEngine::devices (int trackId) const
{
    std::vector<DeviceInfo> out;
    if (auto* t = find (trackId))
        for (const auto& d : t->devices)
        {
            DeviceInfo info { d.id, d.name, d.pluginIdentifier, d.instrument, d.node->isBypassed(), d.groupId };
            if (auto* processor = d.node->getProcessor())
            {
                const auto parameters = processor->getParameters();
                for (int index = 0; index < parameters.size(); ++index)
                {
                    auto* parameter = parameters[index];
                    if (parameter == nullptr || ! parameter->isAutomatable())
                        continue;
                    const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (parameter);
                    const auto* parameterWithId =
                        dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
                    info.automatableParameters.push_back (
                        { index, hosted != nullptr ? hosted->getParameterID()
                            : parameterWithId != nullptr ? parameterWithId->paramID : juce::String(),
                          parameter->getName (64), parameter->getValue() });
                }
            }
            out.push_back (std::move (info));
        }
    return out;
}

juce::String AudioEngine::pluginParameterValueText (
    int trackId, const roll::AutomationTarget& target, double normalizedValue) const
{
    const auto* track = find (trackId);
    if (track == nullptr || target.type != roll::AutomationParameterType::PluginParameter)
        return {};

    int deviceIndex = 0;
    for (const auto& device : track->devices)
    {
        const bool matchesDevice = device.pluginIdentifier
                == juce::String::fromUTF8 (target.pluginIdentifier.c_str())
            && (target.deviceIndex < 0 || target.deviceIndex == deviceIndex);
        ++deviceIndex;
        if (! matchesDevice || device.node == nullptr)
            continue;

        auto* processor = device.node->getProcessor();
        if (processor == nullptr)
            return {};
        int parameterIndex = 0;
        for (auto* parameter : processor->getParameters())
        {
            if (parameter == nullptr || ! parameter->isAutomatable())
            {
                ++parameterIndex;
                continue;
            }
            const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (parameter);
            const auto* parameterWithId =
                dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
            const auto parameterId = hosted != nullptr ? hosted->getParameterID()
                : parameterWithId != nullptr ? parameterWithId->paramID : juce::String();
            const bool matchesParameter = ! target.parameterId.empty()
                ? parameterId == juce::String::fromUTF8 (target.parameterId.c_str())
                : parameterIndex == target.parameterIndex;
            if (matchesParameter)
                return parameter->getText (static_cast<float> (
                    std::clamp (normalizedValue, 0.0, 1.0)), 48);
            ++parameterIndex;
        }
    }
    return {};
}

std::vector<PluginParameterGesture> AudioEngine::consumePluginParameterGestures()
{
    std::vector<PluginParameterGesture> gestures;
    for (auto& [trackId, track] : tracks)
        for (auto& device : track.devices)
            if (device.node != nullptr)
                if (auto* remote = dynamic_cast<RemotePluginProcessor*> (device.node->getProcessor()))
                {
                    if (const int parameterIndex = remote->consumeParameterGestureStarted();
                        parameterIndex >= 0)
                        gestures.push_back ({ trackId, device.id, parameterIndex, true });
                    if (const int parameterIndex = remote->consumeParameterGestureEnded();
                        parameterIndex >= 0)
                        gestures.push_back ({ trackId, device.id, parameterIndex, false });
                }
    return gestures;
}

std::vector<PluginState> AudioEngine::getPluginStates()
{
    std::vector<PluginState> states;
    for (auto& [trackId, track] : tracks)
        for (size_t order = 0; order < track.devices.size(); ++order)
        {
            const auto& device = track.devices[order];
            PluginState state;
            state.trackId = trackId;
            state.order = static_cast<int> (order);
            state.identifier = device.pluginIdentifier;
            state.bypassed = device.node->isBypassed();
            state.groupId = device.groupId;
            if (auto* processor = device.node->getProcessor())
                processor->getStateInformation (state.state);
            states.push_back (std::move (state));
        }
    return states;
}

bool AudioEngine::restorePluginState (int trackId, int deviceId, const juce::MemoryBlock& state)
{
    if (auto* track = find (trackId))
        for (auto& device : track->devices)
            if (device.id == deviceId)
                if (auto* processor = device.node->getProcessor())
                {
                    if (state.getSize() > 0)
                        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                    if (auto* remote = dynamic_cast<RemotePluginProcessor*> (processor);
                        remote != nullptr && remote->hasFailed())
                        return false;
                    if (auto* rack = dynamic_cast<DrumRackProcessor*> (processor))
                        return rack->stateRestoreSucceeded();
                    return true;
                }
    return false;
}

AudioEngine::IsolatedEditorResult AudioEngine::showIsolatedDeviceEditor (
    int trackId, int deviceId, int editorX, int editorY)
{
    auto* track = find (trackId);
    if (track == nullptr)
        return IsolatedEditorResult::notIsolated;

    for (auto& device : track->devices)
        if (device.id == deviceId)
            if (auto* remote = dynamic_cast<RemotePluginProcessor*> (device.node->getProcessor()))
            {
                if (! remote->hasEditor())
                    return IsolatedEditorResult::noEditor;
                return remote->showEditor (editorX, editorY) ? IsolatedEditorResult::opened
                                                              : IsolatedEditorResult::failed;
            }

    return IsolatedEditorResult::notIsolated;
}

AudioEngine::IsolatedEditorResult AudioEngine::showIsolatedPadEffectEditor (
    int trackId, int rackDeviceId, int pad, int effectIndex, int editorX, int editorY)
{
    auto* track = find (trackId);
    if (track == nullptr)
        return IsolatedEditorResult::notIsolated;

    for (auto& device : track->devices)
        if (device.id == rackDeviceId)
            if (auto* rack = dynamic_cast<DrumRackProcessor*> (device.node->getProcessor()))
                if (auto processor = rack->padEffectProcessor (pad, effectIndex))
                    if (auto* remote = dynamic_cast<RemotePluginProcessor*> (processor.get()))
                    {
                        if (! remote->hasEditor())
                            return IsolatedEditorResult::noEditor;
                        return remote->showEditor (editorX, editorY) ? IsolatedEditorResult::opened
                                                                      : IsolatedEditorResult::failed;
                    }

    return IsolatedEditorResult::notIsolated;
}

std::unique_ptr<juce::AudioProcessorEditor> AudioEngine::createDeviceEditor (int trackId, int deviceId)
{
    if (auto* t = find (trackId))
        for (auto& d : t->devices)
            if (d.id == deviceId)
            {
                auto* proc = d.node->getProcessor();
                if (proc->hasEditor())
                    return std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditorAndMakeActive());
            }
    return nullptr;
}
