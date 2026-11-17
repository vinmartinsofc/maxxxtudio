#include <catch2/catch_test_macros.hpp>
#include "ProjectFile.h"

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
