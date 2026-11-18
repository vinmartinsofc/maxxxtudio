#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "ProjectFile.h"
#include "DrumRackProcessor.h"
#include "NativeEffectProcessors.h"

namespace
{
struct TemporaryProjectFolder
{
    TemporaryProjectFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("project_freedom_group_roundtrip", "", false))
    {
        folder.createDirectory();
    }

    ~TemporaryProjectFolder() { folder.deleteRecursively(); }

    juce::File folder;
};
}

TEST_CASE ("Project files preserve flat group bus membership")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    data.project.master.colour = 0xff9c27b0;

    roll::Track group;
    group.id = 10;
    group.name = "Drums";
    group.type = roll::TrackType::Group;
    data.project.tracks.push_back (group);

    roll::Track child;
    child.id = 11;
    child.name = "Kick";
    child.type = roll::TrackType::Audio;
    child.parentGroupId = group.id;
    child.collapsed = true;
    child.colour = 0xff2196f3;
    data.project.tracks.push_back (child);

    const auto file = temporary.folder.getChildFile ("group_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.tracks.size() == 2);
    REQUIRE (loaded.project.tracks[0].type == roll::TrackType::Group);
    REQUIRE (loaded.project.tracks[1].parentGroupId == 10);
    REQUIRE (loaded.project.tracks[1].collapsed);
    REQUIRE (loaded.project.tracks[1].colour == 0xff2196f3);
    REQUIRE (loaded.project.master.colour == 0xff9c27b0);
}

TEST_CASE ("Project files collect Drum Rack samples and restore their paths")
{
    TemporaryProjectFolder temporary;
    const auto sample = temporary.folder.getChildFile ("kick.wav");
    REQUIRE (sample.replaceWithText ("sample asset"));

    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    roll::Track track;
    track.id = 1;
    data.project.tracks.push_back (track);

    juce::XmlElement rack ("DrumRack");
    rack.setAttribute ("version", 1);
    auto* pad = rack.createNewChildElement ("Pad");
    pad->setAttribute ("index", 0);
    pad->setAttribute ("file", sample.getFullPathName());
    juce::MemoryOutputStream stateStream;
    rack.writeTo (stateStream);

    PluginState plugin;
    plugin.trackId = 1;
    plugin.order = 0;
    plugin.identifier = DrumRackProcessor::pluginIdentifier;
    plugin.state.replaceWith (stateStream.getData(), stateStream.getDataSize());
    data.plugins.push_back (std::move (plugin));

    const auto file = temporary.folder.getChildFile ("drum_rack_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());
    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.plugins.size() == 1);

    const auto loadedState = juce::XmlDocument::parse (
        juce::String::fromUTF8 (static_cast<const char*> (loaded.plugins[0].state.getData()),
                                static_cast<int> (loaded.plugins[0].state.getSize())));
    REQUIRE (loadedState != nullptr);
    const auto* loadedPad = loadedState->getChildByName ("Pad");
    REQUIRE (loadedPad != nullptr);
    const juce::File collectedSample (loadedPad->getStringAttribute ("file"));
    CHECK (collectedSample.existsAsFile());
    CHECK (collectedSample.isAChildOf (file.getParentDirectory()));
}

TEST_CASE ("Project files preserve mixer and plugin automation envelopes")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    roll::AutomationEnvelope masterEnvelope;
    masterEnvelope.target.type = roll::AutomationParameterType::Pan;
    masterEnvelope.points = { { 0.0, -0.5 }, { 8.0, 0.75 } };
    data.project.master.automation.push_back (masterEnvelope);

    roll::Track track;
    track.id = 1;
    track.automation.push_back ({
        { roll::AutomationParameterType::PluginParameter, "vendor.effect", "cutoff",
          "Cutoff", 3, 1, 0.4 },
        { { 2.0, 0.2 }, { 6.0, 0.9 } }
    });
    data.project.tracks.push_back (track);

    const auto file = temporary.folder.getChildFile ("automation_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.master.automation.size() == 1);
    REQUIRE (loaded.project.master.automation[0].target.type == roll::AutomationParameterType::Pan);
    REQUIRE (loaded.project.master.automation[0].points == data.project.master.automation[0].points);
    REQUIRE (loaded.project.tracks.size() == 1);
    REQUIRE (loaded.project.tracks[0].automation.size() == 1);
    const auto& envelope = loaded.project.tracks[0].automation[0];
    REQUIRE (envelope.target.type == roll::AutomationParameterType::PluginParameter);
    REQUIRE (envelope.target.pluginIdentifier == "vendor.effect");
    REQUIRE (envelope.target.parameterId == "cutoff");
    REQUIRE (envelope.target.parameterName == "Cutoff");
    REQUIRE (envelope.target.parameterIndex == 3);
    REQUIRE (envelope.target.deviceIndex == 1);
    REQUIRE (envelope.target.currentValue == 0.4);
    REQUIRE (envelope.points == data.project.tracks[0].automation[0].points);
}

TEST_CASE ("Project files migrate legacy Volume Master gain and rate automation")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    roll::Track track;
    track.id = 1;
    track.automation.push_back ({
        { roll::AutomationParameterType::PluginParameter, UtilityProcessor::pluginIdentifier, "gain",
          "Gain", 0, 0, 0.25 },
        { { 0.0, 0.25 }, { 4.0, 0.75 } }
    });
    track.automation.push_back ({
        { roll::AutomationParameterType::PluginParameter, UtilityProcessor::pluginIdentifier, "pan_rate",
          "Rate", 7, 0, 0.1 },
        { { 0.0, 0.1 }, { 4.0, 0.9 } }
    });
    data.project.tracks.push_back (track);
    roll::Track rangeTwoTrack;
    rangeTwoTrack.id = 2;
    rangeTwoTrack.automation.push_back ({
        { roll::AutomationParameterType::PluginParameter, UtilityProcessor::pluginIdentifier, "gain",
          "Gain", 0, 0, 0.25 },
        { { 0.0, 0.25 }, { 4.0, 0.75 } }
    });
    data.project.tracks.push_back (rangeTwoTrack);

    juce::XmlElement oldState ("NativeEffect");
    auto* gain = oldState.createNewChildElement ("Parameter");
    gain->setAttribute ("id", "gain");
    gain->setAttribute ("value", 0.25);
    auto* rate = oldState.createNewChildElement ("Parameter");
    rate->setAttribute ("id", "pan_rate");
    rate->setAttribute ("value", 0.1);
    juce::MemoryOutputStream stateStream;
    oldState.writeTo (stateStream);
    PluginState plugin;
    plugin.trackId = 1;
    plugin.order = 0;
    plugin.identifier = UtilityProcessor::pluginIdentifier;
    plugin.state.replaceAll (stateStream.getData(), stateStream.getDataSize());
    data.plugins.push_back (std::move (plugin));
    juce::XmlElement rangeTwoState ("NativeEffect");
    rangeTwoState.setAttribute ("gainRangeVersion", 2);
    auto* rangeTwoGain = rangeTwoState.createNewChildElement ("Parameter");
    rangeTwoGain->setAttribute ("id", "gain");
    rangeTwoGain->setAttribute ("value", 0.25);
    juce::MemoryOutputStream rangeTwoStream;
    rangeTwoState.writeTo (rangeTwoStream);
    PluginState rangeTwoPlugin;
    rangeTwoPlugin.trackId = 2;
    rangeTwoPlugin.order = 0;
    rangeTwoPlugin.identifier = UtilityProcessor::pluginIdentifier;
    rangeTwoPlugin.state.replaceAll (rangeTwoStream.getData(), rangeTwoStream.getDataSize());
    data.plugins.push_back (std::move (rangeTwoPlugin));

    const auto file = temporary.folder.getChildFile ("legacy_gain_automation.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.tracks[0].automation.size() == 2);
    const auto& legacyEnvelope = loaded.project.tracks[0].automation[0];
    REQUIRE (std::abs (legacyEnvelope.target.currentValue - 9.0 / 14.0) < 0.000001);
    REQUIRE (legacyEnvelope.points.size() == 2);
    REQUIRE (std::abs (legacyEnvelope.points[0].value - 9.0 / 14.0) < 0.000001);
    REQUIRE (std::abs (legacyEnvelope.points[1].value - 11.0 / 14.0) < 0.000001);
    const auto& rateEnvelope = loaded.project.tracks[0].automation[1];
    REQUIRE (std::abs (rateEnvelope.target.currentValue - UtilityProcessor::migrateLegacyRate (0.1f))
             < 0.000001);
    REQUIRE (std::abs (rateEnvelope.points[1].value - UtilityProcessor::migrateLegacyRate (0.9f))
             < 0.000001);
    const auto& rangeTwoEnvelope = loaded.project.tracks[1].automation[0];
    REQUIRE (std::abs (rangeTwoEnvelope.target.currentValue - 3.0 / 14.0) < 0.000001);
    REQUIRE (std::abs (rangeTwoEnvelope.points[0].value - 3.0 / 14.0) < 0.000001);
    REQUIRE (std::abs (rangeTwoEnvelope.points[1].value - 9.0 / 14.0) < 0.000001);
    auto migratedState = juce::XmlDocument::parse (
        juce::String::fromUTF8 (static_cast<const char*> (loaded.plugins[0].state.getData()),
                                static_cast<int> (loaded.plugins[0].state.getSize())));
    REQUIRE (migratedState != nullptr);
    REQUIRE (migratedState->getIntAttribute ("gainRangeVersion") == 3);
    REQUIRE (migratedState->getIntAttribute ("rateRangeVersion") == 1);
    const auto* migratedGain = migratedState->getChildByName ("Parameter");
    REQUIRE (migratedGain != nullptr);
    REQUIRE (std::abs (migratedGain->getDoubleAttribute ("value") - 9.0 / 14.0) < 0.000001);
    const auto* migratedRate = migratedGain->getNextElement();
    REQUIRE (migratedRate != nullptr);
    REQUIRE (std::abs (migratedRate->getDoubleAttribute ("value")
                       - UtilityProcessor::migrateLegacyRate (0.1f)) < 0.000001);

    const auto migratedFile = temporary.folder.getChildFile ("migrated_gain_automation.pfs");
    REQUIRE (ProjectFile::save (migratedFile, loaded).wasOk());
    ProjectFileData reloaded;
    REQUIRE (ProjectFile::load (migratedFile, reloaded).wasOk());
    REQUIRE (std::abs (reloaded.project.tracks[0].automation[0].points[0].value
                       - 9.0 / 14.0) < 0.000001);
    REQUIRE (std::abs (reloaded.project.tracks[0].automation[0].points[1].value
                       - 11.0 / 14.0) < 0.000001);
    REQUIRE (std::abs (reloaded.project.tracks[1].automation[0].points[0].value
                       - 3.0 / 14.0) < 0.000001);
}

TEST_CASE ("Project files preserve non-destructive MIDI groove settings")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;

    roll::Track track;
    track.id = 1;
    auto* clip = track.addClip (0.0, 4.0);
    clip->clip.addNote ({ 60, 0.25, 0.25, 96 });
    roll::GrooveTemplate groove;
    groove.name = "Swing 16ths 57";
    groove.gridBeats = 0.25;
    groove.timingAmount = 0.7;
    groove.velocityAmount = 0.6;
    groove.timingOffsets = { 0.0, 0.04, 0.0, 0.04 };
    groove.velocityMultipliers = { 1.0, 0.8, 1.0, 0.9 };
    clip->clip.setGroove (groove);
    data.project.tracks.push_back (track);

    const auto file = temporary.folder.getChildFile ("groove_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    const auto& loadedGroove = loaded.project.tracks[0].clips[0]->clip.groove();
    REQUIRE (loadedGroove.has_value());
    REQUIRE (loadedGroove->name == groove.name);
    REQUIRE (loadedGroove->gridBeats == groove.gridBeats);
    REQUIRE (loadedGroove->timingAmount == groove.timingAmount);
    REQUIRE (loadedGroove->velocityAmount == groove.velocityAmount);
    REQUIRE (loadedGroove->timingOffsets == groove.timingOffsets);
    REQUIRE (loadedGroove->velocityMultipliers == groove.velocityMultipliers);
    REQUIRE (loadedGroove->sourcePath.empty());
    REQUIRE (loaded.project.tracks[0].clips[0]->clip.notes()[0].startBeats == 0.25);
}

TEST_CASE ("Project files keep Piano Roll locators per MIDI clip and separate from arrangement locators")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    data.project.locators.push_back ({ 1, 12.0, "Arrangement", {} });
    data.project.selectedLocatorId = 1;
    data.project.songStartLocatorId = 1;

    roll::Track track;
    track.id = 1;
    auto* firstClip = track.addClip (0.0, 8.0);
    const int first = firstClip->clip.addPianoRollLocator (1.0);
    const int selected = firstClip->clip.addPianoRollLocator (5.0);
    firstClip->clip.selectPianoRollLocator (selected);
    track.addClip (16.0, 4.0);
    data.project.tracks.push_back (track);

    const auto file = temporary.folder.getChildFile ("piano_locators_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.locators.size() == 1);
    CHECK (loaded.project.locators[0].beat == 12.0);
    CHECK (loaded.project.selectedLocatorId == 1);
    REQUIRE (loaded.project.tracks[0].clips.size() == 2);

    const auto& loadedFirstClip = loaded.project.tracks[0].clips[0]->clip;
    REQUIRE (loadedFirstClip.pianoRollLocators().size() == 2);
    CHECK (loadedFirstClip.pianoRollLocators()[0].id == first);
    CHECK (loadedFirstClip.pianoRollLocators()[0].beat == 1.0);
    CHECK (loadedFirstClip.pianoRollLocators()[1].beat == 5.0);
    CHECK (loadedFirstClip.selectedPianoRollLocatorId() == selected);
    CHECK (loaded.project.tracks[0].clips[1]->clip.pianoRollLocators().empty());
}

TEST_CASE ("Recovery snapshots keep recent complete versions and discard partial writes")
{
    TemporaryProjectFolder temporary;
    ProjectFileData data;
    data.project.master.id = AudioEngine::masterTrackId;
    roll::Track track;
    track.id = 1;
    track.name = "First version";
    data.project.tracks.push_back (track);

    REQUIRE (ProjectFile::saveRecovery (temporary.folder, data).wasOk());
    data.project.tracks[0].name = "Second version";
    REQUIRE (ProjectFile::saveRecovery (temporary.folder, data).wasOk());
    data.project.tracks[0].name = "Latest version";
    REQUIRE (ProjectFile::saveRecovery (temporary.folder, data).wasOk());

    const auto files = ProjectFile::recoveryFiles (temporary.folder);
    REQUIRE (files.size() == 2);

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (files.front(), loaded).wasOk());
    REQUIRE (loaded.project.tracks[0].name == "Latest version");

    const auto partialFile = temporary.folder.getChildFile ("Recovery-partial.tmp");
    REQUIRE (partialFile.create());
    const auto partialAssets = temporary.folder.getChildFile ("Recovery-partial_Files");
    REQUIRE (partialAssets.createDirectory());
    ProjectFile::clearIncompleteRecoveryFiles (temporary.folder);
    REQUIRE_FALSE (partialFile.existsAsFile());
    REQUIRE_FALSE (partialAssets.exists());

    ProjectFile::clearRecoveryFiles (temporary.folder);
    REQUIRE (ProjectFile::recoveryFiles (temporary.folder).empty());
}
