#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
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

TEST_CASE ("ProjectModel undo restores mixer and loop edits")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    REQUIRE (track != nullptr);
    const int trackId = track->id;

    project.setTrackSolo (trackId, true, false);
    REQUIRE (project.track (trackId)->solo);
    REQUIRE (project.undo());
    REQUIRE_FALSE (project.track (trackId)->solo);

    project.setTrackRouting (trackId, roll::TrackOutputRouting::ExtOut);
    REQUIRE (project.track (trackId)->outputRouting == roll::TrackOutputRouting::ExtOut);
    REQUIRE (project.undo());
    REQUIRE (project.track (trackId)->outputRouting == roll::TrackOutputRouting::Master);

    project.setLoop (4.0, 12.0);
    REQUIRE (project.loopStart == 4.0);
    REQUIRE (project.loopEnd == 12.0);
    REQUIRE (project.undo());
    REQUIRE (project.loopStart == 0.0);
    REQUIRE (project.loopEnd == 16.0);
}

TEST_CASE ("ProjectModel undo and redo preserve MIDI mapping edits")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    REQUIRE (track != nullptr);
    const int trackId = track->id;

    project.beginEdit();
    project.midiMappings.push_back ({
        trackId, MidiMappingTarget::TrackVolume, MidiMappingMessageType::ControlChange, 1, 7
    });
    project.endEdit();

    REQUIRE (project.midiMappings.size() == 1);
    REQUIRE (project.undo());
    CHECK (project.midiMappings.empty());
    REQUIRE (project.redo());
    REQUIRE (project.midiMappings.size() == 1);
    CHECK (project.midiMappings.front().target == MidiMappingTarget::TrackVolume);
    CHECK (project.midiMappings.front().number == 7);
}

TEST_CASE ("ProjectModel undo and redo preserve frozen track state")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    REQUIRE (track != nullptr);
    auto* clip = project.addClip (track->id, 0.0, 4.0);
    REQUIRE (clip != nullptr);
    const int trackId = track->id;

    project.beginEdit();
    track->frozen = true;
    track->frozenAudioClip = std::make_unique<roll::AudioPlacement>();
    track->frozenAudioClip->id = 2;
    track->frozenAudioClip->lengthBeats = 4.0;
    track->frozenAudioClip->filePath = "C:/audio/frozen.wav";
    project.endEdit();

    REQUIRE (project.track (trackId)->frozen);
    REQUIRE (project.undo());
    CHECK_FALSE (project.track (trackId)->frozen);
    CHECK (project.track (trackId)->clips.size() == 1);
    REQUIRE (project.redo());
    CHECK (project.track (trackId)->frozen);
    REQUIRE (project.track (trackId)->frozenAudioClip != nullptr);
    CHECK (project.track (trackId)->frozenAudioClip->filePath == "C:/audio/frozen.wav");
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

TEST_CASE ("ProjectModel copies snapshot clips over occupied content without losing the source snapshot")
{
    ProjectModel midiProject;
    auto* midiTrack = midiProject.addTrack (roll::TrackType::Midi);
    auto* midiSource = midiProject.addClip (midiTrack->id, 0.0, 2.0);
    auto* midiOccupied = midiProject.addClip (midiTrack->id, 4.0, 4.0);
    REQUIRE (midiSource != nullptr);
    REQUIRE (midiOccupied != nullptr);
    midiSource->clip.addNote ({ 72, 0.25, 1.0, 90 });
    const int midiTrackId = midiTrack->id;
    const int midiSourceId = midiSource->id;
    const roll::Placement midiSnapshot (*midiSource);
    const int midiCopyId = midiProject.copyClipReplacingRange (midiSnapshot, midiTrackId, 4.0);

    REQUIRE (midiCopyId > 0);
    REQUIRE (midiProject.track (midiTrackId)->find (midiSourceId) != nullptr);
    REQUIRE (midiProject.track (midiTrackId)->find (midiCopyId)->start == 4.0);
    REQUIRE (midiProject.track (midiTrackId)->find (midiCopyId)->clip.notes().front().pitch == 72);

    ProjectModel audioProject;
    auto* audioTrack = audioProject.addTrack (roll::TrackType::Audio);
    auto* audioSource = audioProject.addAudioClip (audioTrack->id, 0.0, 2.0,
        "C:/audio/source.wav", "source.wav", { { -0.5f, 0.5f } }, 1.0, 120.0);
    auto* audioOccupied = audioProject.addAudioClip (audioTrack->id, 4.0, 4.0,
        "C:/audio/occupied.wav", "occupied.wav", { { -1.0f, 1.0f } }, 2.0, 100.0);
    REQUIRE (audioSource != nullptr);
    REQUIRE (audioOccupied != nullptr);
    const int audioTrackId = audioTrack->id;
    const int audioSourceId = audioSource->id;
    const roll::AudioPlacement audioSnapshot (*audioSource);
    const int audioCopyId = audioProject.copyAudioClipReplacingRange (audioSnapshot, audioTrackId, 4.0);

    REQUIRE (audioCopyId > 0);
    REQUIRE (audioProject.track (audioTrackId)->findAudioClip (audioSourceId) != nullptr);
    REQUIRE (audioProject.track (audioTrackId)->findAudioClip (audioCopyId)->start == 4.0);
    REQUIRE (audioProject.track (audioTrackId)->findAudioClip (audioCopyId)->filePath == "C:/audio/source.wav");
}

TEST_CASE ("Moving a MIDI clip replaces only the overlapped destination range")
{
    ProjectModel project;
    auto* source = project.addTrack (roll::TrackType::Midi);
    auto* destination = project.addTrack (roll::TrackType::Midi);
    auto* moving = project.addClip (source->id, 0.0, 2.0);
    auto* existing = project.addClip (destination->id, 0.0, 8.0);
    REQUIRE (moving != nullptr);
    REQUIRE (existing != nullptr);
    moving->clip.addNote ({ 72, 0.25, 1.0, 90 });
    existing->clip.addNote ({ 60, 0.5, 1.0, 80 });
    existing->clip.addNote ({ 62, 2.5, 1.0, 85 });
    existing->clip.addNote ({ 64, 6.0, 1.0, 88 });
    const int sourceId = source->id;
    const int destinationId = destination->id;
    const int movingId = moving->id;
    const int existingId = existing->id;

    const int movedId = project.moveClip (sourceId, movingId, destinationId, 3.0);

    REQUIRE (movedId > 0);
    REQUIRE (project.track (sourceId)->clips.empty());
    const auto* left = project.track (destinationId)->find (existingId);
    REQUIRE (left != nullptr);
    REQUIRE (left->start == 0.0);
    REQUIRE (left->clip.length() == 3.0);
    REQUIRE (left->clip.notes().size() == 2);
    REQUIRE (left->clip.notes()[1].startBeats == 2.5);
    REQUIRE (left->clip.notes()[1].lengthBeats == 0.5);

    const auto* moved = project.track (destinationId)->find (movedId);
    REQUIRE (moved != nullptr);
    REQUIRE (moved->start == 3.0);
    REQUIRE (moved->clip.length() == 2.0);
    const auto& clips = project.track (destinationId)->clips;
    const auto rightIt = std::find_if (clips.begin(), clips.end(), [] (const auto& clip)
    {
        return clip->start == 5.0;
    });
    REQUIRE (rightIt != clips.end());
    const auto* right = rightIt->get();
    REQUIRE (right->start == 5.0);
    REQUIRE (right->clip.length() == 3.0);
    REQUIRE (right->clip.notes().size() == 1);
    REQUIRE (right->clip.notes().front().startBeats == 1.0);

    REQUIRE (project.undo());
    REQUIRE (project.track (sourceId)->find (movingId) != nullptr);
    REQUIRE (project.track (destinationId)->find (existingId)->clip.length() == 8.0);
    REQUIRE (project.track (destinationId)->clips.size() == 1);
}

TEST_CASE ("Moving an audio clip preserves uncovered audio and its source metadata")
{
    ProjectModel project;
    auto* source = project.addTrack (roll::TrackType::Audio);
    auto* destination = project.addTrack (roll::TrackType::Audio);
    auto* moving = project.addAudioClip (source->id, 0.0, 2.0, "C:/audio/moving.wav",
                                         "moving.wav", {}, 1.0, 120.0);
    auto* existing = project.addAudioClip (destination->id, 0.0, 8.0, "C:/audio/source.wav",
                                            "source.wav",
                                            { { -1.0f, 1.0f }, { -0.8f, 0.8f }, { -0.6f, 0.6f },
                                              { -0.4f, 0.4f }, { -0.2f, 0.2f }, { -0.3f, 0.3f },
                                              { -0.5f, 0.5f }, { -0.7f, 0.7f } },
                                            8.0, 100.0);
    REQUIRE (moving != nullptr);
    REQUIRE (existing != nullptr);
    existing->sourceOffsetSeconds = 1.0;
    existing->fadeInBeats = 1.0;
    existing->fadeOutBeats = 1.5;
    existing->warpEnabled = false;
    existing->enabled = false;
    const int sourceId = source->id;
    const int destinationId = destination->id;
    const int movingId = moving->id;

    const int movedId = project.moveAudioClip (sourceId, movingId, destinationId, 3.0);

    REQUIRE (movedId > 0);
    REQUIRE (project.track (sourceId)->audioClips.empty());
    const auto& clips = project.track (destinationId)->audioClips;
    REQUIRE (clips.size() == 3);
    const auto* left = project.track (destinationId)->findAudioClip (1);
    REQUIRE (left != nullptr);
    REQUIRE (left->start == 0.0);
    REQUIRE (left->lengthBeats == 3.0);
    REQUIRE (left->durationSeconds == Catch::Approx (3.0));
    REQUIRE (left->fadeInBeats == 1.0);
    REQUIRE (left->fadeOutBeats == 0.0);

    const auto* moved = project.track (destinationId)->findAudioClip (movedId);
    REQUIRE (moved != nullptr);
    REQUIRE (moved->start == 3.0);
    REQUIRE (moved->lengthBeats == 2.0);

    const auto rightIt = std::find_if (clips.begin(), clips.end(), [] (const auto& clip)
    {
        return clip->start == 5.0;
    });
    REQUIRE (rightIt != clips.end());
    const auto* right = rightIt->get();
    REQUIRE (right->lengthBeats == 3.0);
    REQUIRE (right->durationSeconds == Catch::Approx (3.0));
    REQUIRE (right->sourceOffsetSeconds == Catch::Approx (6.0));
    REQUIRE (right->fadeInBeats == 0.0);
    REQUIRE (right->fadeOutBeats == 1.5);
    REQUIRE_FALSE (right->warpEnabled);
    REQUIRE_FALSE (right->enabled);
    REQUIRE (right->waveform.size() == 3);

    REQUIRE (project.undo());
    REQUIRE (project.track (sourceId)->findAudioClip (movingId) != nullptr);
    REQUIRE (project.track (destinationId)->audioClips.size() == 1);
    REQUIRE (project.track (destinationId)->findAudioClip (1)->lengthBeats == 8.0);
}

TEST_CASE ("Copying MIDI onto occupied content preserves the source and uncovered destination")
{
    ProjectModel project;
    auto* source = project.addTrack (roll::TrackType::Midi);
    auto* destination = project.addTrack (roll::TrackType::Midi);
    auto* original = project.addClip (source->id, 0.0, 2.0);
    auto* existing = project.addClip (destination->id, 0.0, 8.0);
    REQUIRE (original != nullptr);
    REQUIRE (existing != nullptr);
    original->clip.addNote ({ 72, 0.25, 1.0, 90 });
    existing->clip.addNote ({ 60, 6.0, 1.0, 80 });
    const int sourceId = source->id;
    const int destinationId = destination->id;
    const int originalId = original->id;
    const int existingId = existing->id;

    const int copyId = project.copyClip (sourceId, originalId, destinationId, 3.0);

    REQUIRE (copyId > 0);
    REQUIRE (project.track (sourceId)->find (originalId) != nullptr);
    REQUIRE (project.track (sourceId)->find (originalId)->start == 0.0);
    REQUIRE (project.track (destinationId)->find (existingId)->clip.length() == 3.0);
    const auto* copy = project.track (destinationId)->find (copyId);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->start == 3.0);
    REQUIRE (copy->clip.notes().size() == 1);
    const auto& clips = project.track (destinationId)->clips;
    REQUIRE (clips.size() == 3);
    REQUIRE (std::any_of (clips.begin(), clips.end(), [] (const auto& clip)
    {
        return clip->start == 5.0 && clip->clip.length() == 3.0;
    }));
}

TEST_CASE ("Copying audio onto occupied content preserves the source and destination metadata")
{
    ProjectModel project;
    auto* source = project.addTrack (roll::TrackType::Audio);
    auto* destination = project.addTrack (roll::TrackType::Audio);
    auto* original = project.addAudioClip (source->id, 0.0, 2.0, "C:/audio/original.wav",
                                           "original.wav", { { -0.5f, 0.5f } }, 1.0, 120.0);
    auto* existing = project.addAudioClip (destination->id, 0.0, 8.0, "C:/audio/existing.wav",
                                            "existing.wav",
                                            { { -1.0f, 1.0f }, { -0.8f, 0.8f }, { -0.6f, 0.6f },
                                              { -0.4f, 0.4f }, { -0.2f, 0.2f }, { -0.3f, 0.3f },
                                              { -0.5f, 0.5f }, { -0.7f, 0.7f } },
                                            8.0, 100.0);
    REQUIRE (original != nullptr);
    REQUIRE (existing != nullptr);
    original->sourceOffsetSeconds = 0.25;
    original->fadeInBeats = 0.5;
    original->warpEnabled = false;
    const int sourceId = source->id;
    const int destinationId = destination->id;
    const int originalId = original->id;
    const int existingId = existing->id;

    const int copyId = project.copyAudioClip (sourceId, originalId, destinationId, 3.0);

    REQUIRE (copyId > 0);
    REQUIRE (project.track (sourceId)->findAudioClip (originalId) != nullptr);
    REQUIRE (project.track (sourceId)->findAudioClip (originalId)->start == 0.0);
    REQUIRE (project.track (destinationId)->findAudioClip (existingId)->lengthBeats == 3.0);
    const auto* copy = project.track (destinationId)->findAudioClip (copyId);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->start == 3.0);
    REQUIRE (copy->sourceOffsetSeconds == 0.25);
    REQUIRE (copy->fadeInBeats == 0.5);
    REQUIRE_FALSE (copy->warpEnabled);
    const auto& clips = project.track (destinationId)->audioClips;
    REQUIRE (clips.size() == 3);
    REQUIRE (std::any_of (clips.begin(), clips.end(), [] (const auto& clip)
    {
        return clip->start == 5.0 && clip->lengthBeats == 3.0;
    }));
}

TEST_CASE ("Same-track MIDI and audio copies replace only their overlapping source range")
{
    ProjectModel midiProject;
    auto* midiTrack = midiProject.addTrack (roll::TrackType::Midi);
    auto* midiSource = midiProject.addClip (midiTrack->id, 0.0, 4.0);
    REQUIRE (midiSource != nullptr);
    const int midiTrackId = midiTrack->id;
    const int midiSourceId = midiSource->id;

    const int midiCopyId = midiProject.copyClip (midiTrackId, midiSourceId, midiTrackId, 2.0);
    REQUIRE (midiCopyId > 0);
    const auto* leftMidi = midiProject.track (midiTrackId)->find (midiSourceId);
    REQUIRE (leftMidi != nullptr);
    REQUIRE (leftMidi->start == 0.0);
    REQUIRE (leftMidi->clip.length() == 2.0);
    const auto* midiCopy = midiProject.track (midiTrackId)->find (midiCopyId);
    REQUIRE (midiCopy != nullptr);
    REQUIRE (midiCopy->start == 2.0);
    REQUIRE (midiCopy->clip.length() == 4.0);
    REQUIRE (midiProject.track (midiTrackId)->clips.size() == 2);

    ProjectModel audioProject;
    auto* audioTrack = audioProject.addTrack (roll::TrackType::Audio);
    auto* audioSource = audioProject.addAudioClip (audioTrack->id, 0.0, 4.0,
                                                    "C:/audio/source.wav", "source.wav",
                                                    {}, 2.0, 120.0);
    REQUIRE (audioSource != nullptr);
    const int audioTrackId = audioTrack->id;
    const int audioSourceId = audioSource->id;

    const int audioCopyId = audioProject.copyAudioClip (audioTrackId, audioSourceId, audioTrackId, 2.0);
    REQUIRE (audioCopyId > 0);
    const auto* leftAudio = audioProject.track (audioTrackId)->findAudioClip (audioSourceId);
    REQUIRE (leftAudio != nullptr);
    REQUIRE (leftAudio->start == 0.0);
    REQUIRE (leftAudio->lengthBeats == 2.0);
    const auto* audioCopy = audioProject.track (audioTrackId)->findAudioClip (audioCopyId);
    REQUIRE (audioCopy != nullptr);
    REQUIRE (audioCopy->start == 2.0);
    REQUIRE (audioCopy->lengthBeats == 4.0);
    REQUIRE (audioProject.track (audioTrackId)->audioClips.size() == 2);
}

TEST_CASE ("Dropping audio onto occupied content replaces only the overlapped range")
{
    ProjectModel project;
    auto* destination = project.addTrack (roll::TrackType::Audio);
    auto* existing = project.addAudioClip (destination->id, 0.0, 8.0, "C:/audio/existing.wav",
                                            "existing.wav",
                                            { { -1.0f, 1.0f }, { -0.8f, 0.8f }, { -0.6f, 0.6f },
                                              { -0.4f, 0.4f }, { -0.2f, 0.2f }, { -0.3f, 0.3f },
                                              { -0.5f, 0.5f }, { -0.7f, 0.7f } },
                                            8.0, 100.0);
    REQUIRE (existing != nullptr);
    const int destinationId = destination->id;
    const int existingId = existing->id;

    auto* dropped = project.addAudioClipReplacingRange (
        destinationId, 3.0, 2.0, "C:/audio/dropped.wav", "dropped.wav",
        { { -0.9f, 0.9f } }, 1.0, 120.0);

    REQUIRE (dropped != nullptr);
    REQUIRE (dropped->start == 3.0);
    REQUIRE (dropped->lengthBeats == 2.0);
    REQUIRE (project.track (destinationId)->findAudioClip (existingId)->lengthBeats == 3.0);
    REQUIRE (project.track (destinationId)->audioClips.size() == 3);
    REQUIRE (std::any_of (project.track (destinationId)->audioClips.begin(),
                          project.track (destinationId)->audioClips.end(),
                          [] (const auto& clip)
    {
        return clip->start == 5.0 && clip->lengthBeats == 3.0;
    }));
}

TEST_CASE ("Adjacent split MIDI clips can be joined")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    REQUIRE (track != nullptr);

    auto* left = project.addClip (track->id, 0.0, 4.0);
    REQUIRE (left != nullptr);
    left->clip.addNote ({ 60, 0.0, 1.0, 100 });

    const int rightId = project.splitClip (track->id, left->id, 2.0);
    REQUIRE (rightId > 0);
    auto* right = track->find (rightId);
    REQUIRE (right != nullptr);
    right->clip.addNote ({ 64, 0.5, 1.0, 90 });

    REQUIRE (project.joinMidiClips (track->id, left->id, rightId));
    const auto* joined = track->find (left->id);
    REQUIRE (joined != nullptr);
    REQUIRE (joined->clip.length() == Catch::Approx (4.0));
    REQUIRE (joined->clip.notes().size() == 2);
    REQUIRE (track->find (rightId) == nullptr);
}

TEST_CASE ("Adjacent split audio clips can be joined")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Audio);
    REQUIRE (track != nullptr);

    auto* left = project.addAudioClip (
        track->id, 0.0, 4.0, "C:/audio/recover.wav", "recover.wav",
        { { -0.5f, 0.5f }, { -0.4f, 0.4f } }, 2.0, 120.0);
    REQUIRE (left != nullptr);
    left->sourceOffsetSeconds = 0.25;

    const int rightId = project.splitAudioClip (track->id, left->id, 2.0);
    REQUIRE (rightId > 0);
    REQUIRE (project.joinAudioClips (track->id, left->id, rightId));

    const auto* joined = track->findAudioClip (left->id);
    REQUIRE (joined != nullptr);
    REQUIRE (joined->lengthBeats == Catch::Approx (4.0));
    REQUIRE (joined->durationSeconds == Catch::Approx (2.0));
    REQUIRE (joined->sourceOffsetSeconds == Catch::Approx (0.25));
    REQUIRE (track->findAudioClip (rightId) == nullptr);
}

TEST_CASE ("Audio warp markers stay aligned across split and join")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Audio);
    REQUIRE (track != nullptr);
    auto* clip = project.addAudioClip (
        track->id, 0.0, 4.0, "C:/audio/warped.wav", "warped.wav", {}, 2.0, 120.0);
    REQUIRE (clip != nullptr);
    clip->sourceOffsetSeconds = 0.25;
    const std::vector<roll::AudioWarpMarker> markers {
        { 0.25, 0.0 }, { 1.5, 2.0 }, { 2.25, 4.0 }
    };

    REQUIRE (project.setAudioClipWarpMarkers (track->id, clip->id, markers));
    CHECK (roll::audioSourceSecondsAtBeat (*clip, 1.0) == Catch::Approx (0.875));
    const int rightId = project.splitAudioClip (track->id, clip->id, 2.0);
    REQUIRE (rightId > 0);
    const auto* left = track->findAudioClip (clip->id);
    const auto* right = track->findAudioClip (rightId);
    REQUIRE (left != nullptr);
    REQUIRE (right != nullptr);
    REQUIRE (left->warpMarkers.size() == 2);
    REQUIRE (right->warpMarkers.size() == 2);
    CHECK (left->durationSeconds == Catch::Approx (1.25));
    CHECK (right->sourceOffsetSeconds == Catch::Approx (1.5));

    REQUIRE (project.joinAudioClips (track->id, left->id, rightId));
    const auto* joined = track->findAudioClip (clip->id);
    REQUIRE (joined != nullptr);
    CHECK (joined->warpMarkers == markers);
}

TEST_CASE ("Imported audio can receive short non-overlapping edge fades")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Audio);
    REQUIRE (track != nullptr);

    auto* regular = project.addAudioClipReplacingRange (
        track->id, 0.0, 1.0, "C:/audio/regular.wav", "regular.wav", {}, 0.5, 120.0, 0.02);
    REQUIRE (regular != nullptr);
    REQUIRE (regular->fadeInBeats == Catch::Approx (0.02));
    REQUIRE (regular->fadeOutBeats == Catch::Approx (0.02));

    auto* shortClip = project.addAudioClipReplacingRange (
        track->id, 2.0, 0.04, "C:/audio/short.wav", "short.wav", {}, 0.02, 120.0, 1.0);
    REQUIRE (shortClip != nullptr);
    REQUIRE (shortClip->fadeInBeats == Catch::Approx (0.02));
    REQUIRE (shortClip->fadeOutBeats == Catch::Approx (0.02));
    REQUIRE (shortClip->fadeInBeats + shortClip->fadeOutBeats <= shortClip->lengthBeats);
}

TEST_CASE ("Moving a clip fully over another clip removes the covered destination")
{
    ProjectModel project;
    auto* source = project.addTrack (roll::TrackType::Midi);
    auto* destination = project.addTrack (roll::TrackType::Midi);
    auto* moving = project.addClip (source->id, 0.0, 2.0);
    auto* covered = project.addClip (destination->id, 3.0, 2.0);
    REQUIRE (moving != nullptr);
    REQUIRE (covered != nullptr);
    const int sourceId = source->id;
    const int destinationId = destination->id;
    const int movingId = moving->id;
    const int coveredId = covered->id;

    const int movedId = project.moveClip (sourceId, movingId, destinationId, 3.0);

    REQUIRE (movedId > 0);
    REQUIRE (project.track (destinationId)->find (coveredId) == nullptr);
    REQUIRE (project.track (destinationId)->find (movedId) != nullptr);
    REQUIRE (project.track (destinationId)->clips.size() == 1);
}

TEST_CASE ("Moving within a track replaces the destination and updates a removed selection")
{
    ProjectModel project;
    auto* track = project.addTrack (roll::TrackType::Midi);
    auto* moving = project.addClip (track->id, 0.0, 2.0);
    auto* covered = project.addClip (track->id, 5.0, 2.0);
    REQUIRE (moving != nullptr);
    REQUIRE (covered != nullptr);
    const int trackId = track->id;
    const int movingId = moving->id;
    const int coveredId = covered->id;
    project.selectClip (trackId, coveredId);

    const int movedId = project.moveClip (trackId, movingId, trackId, 5.0);

    REQUIRE (movedId == movingId);
    REQUIRE (project.track (trackId)->find (coveredId) == nullptr);
    REQUIRE (project.track (trackId)->find (movedId)->start == 5.0);
    REQUIRE (project.selectedTrackId == trackId);
    REQUIRE (project.selectedClipId == movedId);
    REQUIRE (project.track (trackId)->clips.size() == 1);
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

TEST_CASE ("ProjectModel selects a contiguous track range from an anchor in either direction")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Audio)->id;
    const int third = project.addTrack (roll::TrackType::Midi)->id;
    const int fourth = project.addTrack (roll::TrackType::Audio)->id;

    project.selectTrack (first);
    project.selectTrackRange (second, fourth);
    REQUIRE (project.selectedTrackIds == std::vector<int> { second, third, fourth });
    REQUIRE (project.selectedTrackId == fourth);

    project.selectTrackRange (third, first);
    REQUIRE (project.selectedTrackIds == std::vector<int> { first, second, third });
    REQUIRE (project.selectedTrackId == first);
}

TEST_CASE ("ProjectModel can add a track range to an existing multi-selection")
{
    ProjectModel project;
    const int first = project.addTrack (roll::TrackType::Midi)->id;
    const int second = project.addTrack (roll::TrackType::Midi)->id;
    const int third = project.addTrack (roll::TrackType::Midi)->id;
    const int fourth = project.addTrack (roll::TrackType::Midi)->id;

    project.selectTrack (first);
    project.selectTrack (fourth, true);
    project.selectTrackRange (second, third, true);

    REQUIRE (project.isTrackSelected (first));
    REQUIRE (project.isTrackSelected (second));
    REQUIRE (project.isTrackSelected (third));
    REQUIRE (project.isTrackSelected (fourth));
    REQUIRE (project.selectedTrackId == third);
}

TEST_CASE ("ProjectModel validates automation and includes it in undo and redo")
{
    ProjectModel model;
    auto* track = model.addTrack();
    REQUIRE (track != nullptr);

    roll::AutomationEnvelope envelope;
    envelope.target.type = roll::AutomationParameterType::Volume;
    envelope.target.currentValue = 0.8;
    envelope.points = { { 0.0, 0.8 }, { 4.0, 0.2 } };
    const int trackId = track->id;
    REQUIRE (model.setAutomationEnvelope (trackId, envelope));
    REQUIRE (track->automation.size() == 1);
    REQUIRE (model.undo());
    REQUIRE (model.track (trackId)->automation.empty());
    REQUIRE (model.redo());
    REQUIRE (model.track (trackId)->automation.size() == 1);

    envelope.points[1].value = 2.0;
    REQUIRE_FALSE (model.setAutomationEnvelope (trackId, envelope));
    REQUIRE (model.track (trackId)->automation.size() == 1);
}

TEST_CASE ("Audio clip automation requires an existing audio clip and supports undo")
{
    ProjectModel model;
    const auto* midiTrack = model.addTrack (roll::TrackType::Midi);
    REQUIRE (midiTrack != nullptr);
    const int midiTrackId = midiTrack->id;

    roll::AutomationEnvelope envelope;
    envelope.target.type = roll::AutomationParameterType::AudioClipGain;
    envelope.target.clipId = 1;
    envelope.points = { { 0.0, 0.0 }, { 4.0, 6.0 } };
    REQUIRE_FALSE (model.setAutomationEnvelope (midiTrackId, envelope));

    const auto* audioTrack = model.addTrack (roll::TrackType::Audio);
    REQUIRE (audioTrack != nullptr);
    const int audioTrackId = audioTrack->id;
    const auto* clip = model.addAudioClip (audioTrackId, 0.0, 4.0,
                                           "audio.wav", "Audio", {}, 2.0, 120.0);
    REQUIRE (clip != nullptr);
    envelope.target.clipId = clip->id;
    REQUIRE (model.setAutomationEnvelope (audioTrackId, envelope));
    REQUIRE (model.track (audioTrackId)->automation.size() == 1);
    REQUIRE (model.undo());
    REQUIRE (model.track (audioTrackId)->automation.empty());
    REQUIRE (model.redo());
    REQUIRE (model.track (audioTrackId)->automation.size() == 1);
}

TEST_CASE ("ProjectModel stores track colours and includes them in undo and redo")
{
    ProjectModel project;
    const int trackId = project.addTrack()->id;
    constexpr std::uint32_t blue = 0xff2196f3;

    REQUIRE (project.setTrackColour (trackId, blue));
    REQUIRE (project.track (trackId)->colour == blue);
    REQUIRE_FALSE (project.setTrackColour (trackId, blue));
    REQUIRE_FALSE (project.setTrackColour (-1, blue));

    REQUIRE (project.undo());
    REQUIRE (project.track (trackId)->colour == 0);
    REQUIRE (project.redo());
    REQUIRE (project.track (trackId)->colour == blue);
}

TEST_CASE ("ProjectModel stores the Master colour and includes it in undo and redo")
{
    ProjectModel project;
    constexpr std::uint32_t purple = 0xff9c27b0;

    REQUIRE (project.setTrackColour (0, purple));
    REQUIRE (project.masterTrack->colour == purple);
    REQUIRE (project.undo());
    REQUIRE (project.masterTrack->colour == 0);
    REQUIRE (project.redo());
    REQUIRE (project.masterTrack->colour == purple);
}

TEST_CASE ("ProjectModel reorders audio and MIDI tracks and supports undo")
{
    ProjectModel project;
    const int firstMidi = project.addTrack (roll::TrackType::Midi)->id;
    const int audio = project.addTrack (roll::TrackType::Audio)->id;
    const int lastMidi = project.addTrack (roll::TrackType::Midi)->id;

    REQUIRE (project.moveTrack (audio, 0));
    REQUIRE (project.tracks[0]->id == audio);
    REQUIRE (project.tracks[1]->id == firstMidi);
    REQUIRE (project.tracks[2]->id == lastMidi);

    REQUIRE (project.undo());
    REQUIRE (project.tracks[0]->id == firstMidi);
    REQUIRE (project.tracks[1]->id == audio);
    REQUIRE (project.tracks[2]->id == lastMidi);

    REQUIRE (project.redo());
    REQUIRE (project.tracks[0]->id == audio);
    REQUIRE (project.tracks[1]->id == firstMidi);
    REQUIRE (project.tracks[2]->id == lastMidi);
    REQUIRE_FALSE (project.moveTrack (audio, 0));
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
    audio->solo = true;
    audio->outputRouting = roll::TrackOutputRouting::ExtOut;
    const int audioId = audio->id;
    constexpr std::uint32_t audioColour = 0xff2196f3;
    REQUIRE (project.setTrackColour (audioId, audioColour));
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
    REQUIRE (duplicate->solo);
    REQUIRE (duplicate->outputRouting == roll::TrackOutputRouting::ExtOut);
    REQUIRE (duplicate->colour == audioColour);
    REQUIRE (duplicate->audioClips.size() == 1);
    REQUIRE (duplicate->audioClips.front()->filePath == "C:/audio/kick.wav");
    REQUIRE (duplicate->audioClips.front()->waveform.size() == 1);
    REQUIRE (project.selectedTrackId == duplicateId);
    REQUIRE (project.undo());
    REQUIRE (project.track (duplicateId) == nullptr);
}

TEST_CASE ("ProjectModel duplicates a MIDI track's clips and note data")
{
    ProjectModel project;
    auto* original = project.addTrack (roll::TrackType::Midi);
    auto* clip = project.addClip (original->id, 4.0, 8.0);
    REQUIRE (clip != nullptr);
    clip->clip.addNote ({ 67, 1.0, 2.0, 96 });
    const int originalId = original->id;

    const int duplicateId = project.duplicateTrack (originalId);

    REQUIRE (duplicateId > 0);
    const auto* duplicate = project.track (duplicateId);
    REQUIRE (duplicate != nullptr);
    REQUIRE (duplicate->type == roll::TrackType::Midi);
    REQUIRE (duplicate->clips.size() == 1);
    REQUIRE (duplicate->clips.front()->start == 4.0);
    REQUIRE (duplicate->clips.front()->clip.notes().size() == 1);
    REQUIRE (duplicate->clips.front()->clip.notes().front().pitch == 67);
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
