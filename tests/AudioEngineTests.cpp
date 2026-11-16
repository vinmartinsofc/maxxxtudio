#include <catch2/catch_test_macros.hpp>
#include "AudioEngine.h"

namespace
{
struct TemporaryWavFile
{
    TemporaryWavFile()
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("project_freedom_master_render", ".wav", false))
    {
    }

    ~TemporaryWavFile() { file.deleteFile(); }

    juce::File file;
};
}

TEST_CASE ("Offline WAV export renders the master bus")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineRender (error));

    TemporaryWavFile output;
    const auto result = engine.renderMasterToWav (output.file, 1.1, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (output.file.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (output.file));
    REQUIRE (reader != nullptr);
    REQUIRE (reader->numChannels == 2);
    REQUIRE (reader->bitsPerSample == 24);
    REQUIRE (reader->lengthInSamples > reader->sampleRate);

    juce::AudioBuffer<float> firstFrames (2, 1024);
    REQUIRE (reader->read (&firstFrames, 0, firstFrames.getNumSamples(), 0, true, true));
    REQUIRE (firstFrames.getMagnitude (0, firstFrames.getNumSamples()) > 0.1f);
    reader.reset();
}

TEST_CASE ("Audio engine registers only VST3")
{
    AudioEngine engine;
    bool hasVst2 = false;
    bool hasVst3 = false;

    for (int index = 0; index < engine.formats().getNumFormats(); ++index)
    {
        const auto name = engine.formats().getFormat (index)->getName();
        hasVst2 = hasVst2 || name == "VST";
        hasVst3 = hasVst3 || name == "VST3";
    }

    REQUIRE (hasVst3);
    REQUIRE_FALSE (hasVst2);
}

TEST_CASE ("Offline track rendering captures audio before the master bus")
{
    AudioEngine engine;
    engine.addTrack (1, roll::TrackType::Audio);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (1, error));

    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    REQUIRE (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
    REQUIRE (rendered.getMagnitude (1, rendered.getNumSamples()) > 0.1f);
}

TEST_CASE ("Audio from a track passes through its group bus before the master")
{
    AudioEngine engine;
    engine.addTrack (10, roll::TrackType::Group);
    engine.addTrack (1, roll::TrackType::Audio);
    engine.setTrackRouting (1, roll::TrackOutputRouting::Master, 10);
    engine.setMixer (10, 0.5f, 0.0f, false);

    auto source = std::make_shared<AudioFileData>();
    source->samples = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    source->samples->clear();
    for (int channel = 0; channel < source->samples->getNumChannels(); ++channel)
        for (int sample = 0; sample < source->samples->getNumSamples(); ++sample)
            source->samples->setSample (channel, sample, 0.25f);
    source->sampleRate = 44100.0;
    source->durationSeconds = 1.0;

    AudioClipPlayback clip;
    clip.lengthBeats = 2.0;
    clip.sourceBpm = 120.0;
    clip.source = std::move (source);
    clip.warpEnabled = false;
    engine.publishAudioClips (1, { clip });

    juce::String error;
    REQUIRE (engine.beginOfflineTrackRender (AudioEngine::masterTrackId, error));
    juce::AudioBuffer<float> rendered (2, 44100);
    const auto result = engine.renderTrackRange (0.0, 2.0, rendered, {}, {});
    engine.finishOfflineRender();

    REQUIRE (result.wasOk());
    const auto settledLevel = std::abs (rendered.getSample (0, rendered.getNumSamples() - 1));
    REQUIRE (settledLevel > 0.12f);
    REQUIRE (settledLevel < 0.13f);
}

TEST_CASE ("Cancelling an offline WAV export removes the partial file")
{
    AudioEngine engine;
    juce::String error;
    REQUIRE (engine.beginOfflineRender (error));

    TemporaryWavFile output;
    const auto result = engine.renderMasterToWav (output.file, 0.1, {}, [] { return true; });
    engine.finishOfflineRender();

    REQUIRE (result.failed());
    REQUIRE_FALSE (output.file.existsAsFile());
}
