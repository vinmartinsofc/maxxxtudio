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
