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
    data.project.tracks.push_back (child);

    const auto file = temporary.folder.getChildFile ("group_test.pfs");
    REQUIRE (ProjectFile::save (file, data).wasOk());

    ProjectFileData loaded;
    REQUIRE (ProjectFile::load (file, loaded).wasOk());
    REQUIRE (loaded.project.tracks.size() == 2);
    REQUIRE (loaded.project.tracks[0].type == roll::TrackType::Group);
    REQUIRE (loaded.project.tracks[1].parentGroupId == 10);
}
