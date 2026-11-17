#include <catch2/catch_test_macros.hpp>
#include "ProjectFile.h"
#include "DrumRackProcessor.h"

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
    data.project.tracks.push_back (child);

    const auto file = temporary.folder.getChildFile ("group_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.tracks.size() == 2);
    REQUIRE (loaded.project.tracks[0].type == roll::TrackType::Group);
    REQUIRE (loaded.project.tracks[1].parentGroupId == 10);
    REQUIRE (loaded.project.tracks[1].collapsed);
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
