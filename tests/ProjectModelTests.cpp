#include <catch2/catch_test_macros.hpp>
#include "ProjectModel.h"

TEST_CASE ("ProjectModel copies MIDI clips from a clipboard snapshot")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    auto* destination = project.addTrack (roll::TrackType::Midi);
    auto* original = project.addClip (track->id, 0.0, 4.0);
    REQUIRE (original != nullptr);
    original->name = "phrase";
    original->clip.addNote ({ 64, 1.0, 2.0, 91 });
    const roll::Placement snapshot (*original);

    const int copyId = project.copyClip (snapshot, destination->id, 4.0);

    REQUIRE (copyId > 0);
    const auto* copy = destination->find (copyId);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->start == 4.0);
    REQUIRE (copy->name == "phrase");
    REQUIRE (copy->clip.notes().size() == 1);
    REQUIRE (copy->clip.notes().front().pitch == 64);
    REQUIRE (original->start == 0.0);
}

TEST_CASE ("ProjectModel copies audio clips from a clipboard snapshot")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Audio);
    auto* destination = project.addTrack (roll::TrackType::Audio);
    auto* original = project.addAudioClip (track->id, 0.0, 4.0, "C:/audio/loop.wav", "loop.wav",
                                           { { -0.5f, 0.5f }, { -0.8f, 0.8f } }, 2.0, 120.0);
    REQUIRE (original != nullptr);
    original->sourceOffsetSeconds = 0.25;
    original->fadeInBeats = 0.5;
    original->warpEnabled = false;
    const roll::AudioPlacement snapshot (*original);

    const int copyId = project.copyAudioClip (snapshot, destination->id, 4.0);

    REQUIRE (copyId > 0);
    const auto* copy = destination->findAudioClip (copyId);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->start == 4.0);
    REQUIRE (copy->filePath == "C:/audio/loop.wav");
    REQUIRE (copy->sourceOffsetSeconds == 0.25);
    REQUIRE (copy->fadeInBeats == 0.5);
    REQUIRE_FALSE (copy->warpEnabled);
    REQUIRE (copy->waveform.size() == 2);
}

TEST_CASE ("ProjectModel toggles multi-track selection without losing the primary track")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Audio)->id;
    const int third = project.addTrack (roll::TrackType::Midi)->id;

    project.selectTrack (first);
    project.selectTrack (second, true);
    project.selectTrack (third, true);

    REQUIRE (project.selectedTrackId == third);
    REQUIRE (project.isTrackSelected (first));
    REQUIRE (project.isTrackSelected (second));
    REQUIRE (project.isTrackSelected (third));

    project.selectTrack (third, true);
    REQUIRE (project.selectedTrackId == second);
    REQUIRE_FALSE (project.isTrackSelected (third));

    project.selectTrack (first);
    REQUIRE (project.selectedTrackId == first);
    REQUIRE (project.selectedTrackIds.size() == 1);
}

TEST_CASE ("Solo toggles selected tracks together or adds one track with Control")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Audio)->id;
    const int third = project.addTrack (roll::TrackType::Midi)->id;

    project.selectTrack (first);
    project.selectTrack (second, true);
    project.setTrackSolo (second, true, false);

    REQUIRE (project.track (first)->solo);
    REQUIRE (project.track (second)->solo);
    REQUIRE_FALSE (project.track (third)->solo);

    project.setTrackSolo (third, true, true);
    REQUIRE (project.track (first)->solo);
    REQUIRE (project.track (second)->solo);
    REQUIRE (project.track (third)->solo);

    project.setTrackSolo (second, false, false);
    REQUIRE_FALSE (project.track (first)->solo);
    REQUIRE_FALSE (project.track (second)->solo);
    REQUIRE_FALSE (project.track (third)->solo);
}

TEST_CASE ("ProjectModel groups selected tracks into one flat bus and restores grouping with undo")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int between = project.addTrack (roll::TrackType::Audio)->id;
    const int last = project.addTrack (roll::TrackType::Audio)->id;
    int addedTracks = 0;
    int removedTracks = 0;
    project.onTrackAdded = [&addedTracks] (int) { ++addedTracks; };
    project.onTrackRemoved = [&removedTracks] (int) { ++removedTracks; };

    const int groupId = project.groupTracks ({ first, last });

    REQUIRE (groupId > 0);
    REQUIRE (addedTracks == 1);
    REQUIRE (project.tracks.size() == 4);
    REQUIRE (project.tracks[0]->id == groupId);
    REQUIRE (project.tracks[1]->id == first);
    REQUIRE (project.tracks[2]->id == last);
    REQUIRE (project.tracks[3]->id == between);
    REQUIRE (project.track (groupId)->type == roll::TrackType::Group);
    REQUIRE (project.track (first)->parentGroupId == groupId);
    REQUIRE (project.track (last)->parentGroupId == groupId);
    REQUIRE (project.groupTracks ({ first, groupId }) == -1);

    REQUIRE (project.undo());
    REQUIRE (removedTracks == 1);
    REQUIRE (project.track (groupId) == nullptr);
    REQUIRE (project.track (first)->parentGroupId == 0);
    REQUIRE (project.track (last)->parentGroupId == 0);
}

TEST_CASE ("ProjectModel ungroups children without changing their order")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Audio)->id;
    const int groupId = project.groupTracks ({ first, second });

    REQUIRE (project.ungroupTrack (groupId));
    REQUIRE (project.track (groupId) == nullptr);
    REQUIRE (project.track (first)->parentGroupId == 0);
    REQUIRE (project.track (second)->parentGroupId == 0);
    REQUIRE (project.tracks[0]->id == first);
    REQUIRE (project.tracks[1]->id == second);
}

TEST_CASE ("ProjectModel duplicates a track's mix and clips within its group")
{
    ProjectModel project;
    const int midiId = project.addTrack (roll::TrackType::Midi)->id;
    auto* audio = project.addTrack (roll::TrackType::Audio);
    auto* clip = project.addAudioClip (audio->id, 2.0, 4.0, "C:/audio/kick.wav", "kick",
                                       { { -0.5f, 0.5f } }, 2.0, 120.0);
    REQUIRE (clip != nullptr);
    audio->volume = 0.7;
    audio->pan = -0.25;
    audio->mute = true;
    const int audioId = audio->id;
    const int groupId = project.groupTracks ({ midiId, audioId });

    const int duplicateId = project.duplicateTrack (audioId);

    REQUIRE (duplicateId > 0);
    const auto* duplicate = project.track (duplicateId);
    REQUIRE (duplicate != nullptr);
    REQUIRE (duplicate->type == roll::TrackType::Audio);
    REQUIRE (duplicate->parentGroupId == groupId);
    REQUIRE (duplicate->name == "2 AUDIO Copy");
    REQUIRE (duplicate->volume == 0.7);
    REQUIRE (duplicate->pan == -0.25);
    REQUIRE (duplicate->mute);
    REQUIRE (duplicate->audioClips.size() == 1);
    REQUIRE (duplicate->audioClips.front()->filePath == "C:/audio/kick.wav");
    REQUIRE (duplicate->audioClips.front()->waveform.size() == 1);
    REQUIRE (project.selectedTrackId == duplicateId);
    REQUIRE (project.undo());
    REQUIRE (project.track (duplicateId) == nullptr);
}

TEST_CASE ("ProjectModel does not duplicate a group bus by itself")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Audio)->id;
    const int groupId = project.groupTracks ({ first, second });

    REQUIRE (project.duplicateTrack (groupId) == -1);
    REQUIRE (project.tracks.size() == 3);
}
