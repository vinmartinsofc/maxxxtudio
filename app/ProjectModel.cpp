#include "ProjectModel.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr double clipOverlapEpsilon = 1.0e-9;

bool isAudioClipAutomation (roll::AutomationParameterType type)
{
    return type == roll::AutomationParameterType::AudioClipGain
        || type == roll::AutomationParameterType::AudioClipPitch
        || type == roll::AutomationParameterType::AudioClipFadeIn
        || type == roll::AutomationParameterType::AudioClipFadeOut;
}

bool replaceMidiRange (roll::Track& track, double start, double end, int ignoredClipId)
{
    std::vector<int> overlappingIds;
    for (const auto& clip : track.clips)
        if (clip->id != ignoredClipId
            && start < clip->end() - clipOverlapEpsilon
            && end > clip->start + clipOverlapEpsilon)
            overlappingIds.push_back (clip->id);

    for (const int id : overlappingIds)
    {
        const auto* clip = track.find (id);
        if (clip == nullptr)
            return false;
        const double clipStart = clip->start;
        const double clipEnd = clip->end();
        const bool preserveLeft = clipStart < start - clipOverlapEpsilon;
        const bool preserveRight = clipEnd > end + clipOverlapEpsilon;

        if (preserveLeft && preserveRight)
        {
            const int middleId = track.splitClip (id, start);
            if (middleId < 0 || track.splitClip (middleId, end) < 0)
                return false;
            if (! track.remove (middleId))
                return false;
        }
        else if (preserveLeft)
        {
            const int rightId = track.splitClip (id, start);
            if (rightId < 0 || ! track.remove (rightId))
                return false;
        }
        else if (preserveRight)
        {
            if (track.splitClip (id, end) < 0 || ! track.remove (id))
                return false;
        }
        else if (! track.remove (id))
        {
            return false;
        }
    }

    return true;
}

bool replaceAudioRange (roll::Track& track, double start, double end, int ignoredClipId)
{
    std::vector<int> overlappingIds;
    for (const auto& clip : track.audioClips)
        if (clip->id != ignoredClipId
            && start < clip->end() - clipOverlapEpsilon
            && end > clip->start + clipOverlapEpsilon)
            overlappingIds.push_back (clip->id);

    for (const int id : overlappingIds)
    {
        const auto* clip = track.findAudioClip (id);
        if (clip == nullptr)
            return false;
        const double clipStart = clip->start;
        const double clipEnd = clip->end();
        const bool preserveLeft = clipStart < start - clipOverlapEpsilon;
        const bool preserveRight = clipEnd > end + clipOverlapEpsilon;

        if (preserveLeft && preserveRight)
        {
            const int middleId = track.splitAudioClip (id, start);
            if (middleId < 0 || track.splitAudioClip (middleId, end) < 0
                || ! track.removeAudioClip (middleId))
                return false;
        }
        else if (preserveLeft)
        {
            const int rightId = track.splitAudioClip (id, start);
            if (rightId < 0 || ! track.removeAudioClip (rightId))
                return false;
        }
        else if (preserveRight)
        {
            if (track.splitAudioClip (id, end) < 0 || ! track.removeAudioClip (id))
                return false;
        }
        else if (! track.removeAudioClip (id))
        {
            return false;
        }
    }

    return true;
}

std::vector<roll::AudioWarpMarker> effectiveWarpMarkers (const roll::AudioPlacement& clip)
{
    if (! clip.warpMarkers.empty())
        return clip.warpMarkers;
    return { { clip.sourceOffsetSeconds, 0.0 },
             { clip.sourceOffsetSeconds + clip.durationSeconds, clip.lengthBeats } };
}

void resizeWarpMarkers (roll::AudioPlacement& clip, double oldLength, double oldSourceOffset)
{
    if (clip.warpMarkers.empty())
        return;
    const double beatScale = oldLength > 0.0 ? clip.lengthBeats / oldLength : 1.0;
    const double sourceShift = clip.sourceOffsetSeconds - oldSourceOffset;
    for (auto& marker : clip.warpMarkers)
    {
        marker.beat *= beatScale;
        marker.sourceSeconds += sourceShift;
    }
    clip.warpMarkers.front() = { clip.sourceOffsetSeconds, 0.0 };
    clip.warpMarkers.back() = {
        clip.sourceOffsetSeconds + clip.durationSeconds, clip.lengthBeats
    };
}
}

ProjectModel::ProjectModel()
{
    masterTrack = std::make_unique<roll::Track>();
    masterTrack->id = 0;
    masterTrack->name = "Master";
    masterTrack->volume = 1.0;
    masterTrack->pan = 0.0;
}

ProjectModel::State ProjectModel::createState() const
{
    State state;
    state.master = *masterTrack;
    state.tracks.reserve (tracks.size());
    for (const auto& track : tracks)
        state.tracks.push_back (*track);
    state.selectedTrackIds = selectedTrackIds;
    state.selectedTrackId = selectedTrackId;
    state.selectedClipId = selectedClipId;
    state.selectedAudioClipId = selectedAudioClipId;
    state.loopStart = loopStart;
    state.loopEnd = loopEnd;
    state.totalBeats = totalBeats;
    state.locators = locators;
    state.midiMappings = midiMappings;
    state.selectedLocatorId = selectedLocatorId;
    state.songStartLocatorId = songStartLocatorId;
    return state;
}

void ProjectModel::replaceState (State state)
{
    selectedTrackId = selectedClipId = selectedAudioClipId = -1;
    selectedTrackIds.clear();
    if (onSelectionChanged)
        onSelectionChanged();

    for (const auto& track : tracks)
        if (onTrackRemoved)
            onTrackRemoved (track->id);

    masterTrack = std::make_unique<roll::Track> (std::move (state.master));
    tracks.clear();
    tracks.reserve (state.tracks.size());
    nextTrackId = 1;
    for (auto& track : state.tracks)
    {
        nextTrackId = std::max (nextTrackId, track.id + 1);
        tracks.push_back (std::make_unique<roll::Track> (std::move (track)));
    }

    selectedTrackId = state.selectedTrackId;
    selectedTrackIds.clear();
    for (const int id : state.selectedTrackIds)
        if (track (id) != nullptr && std::find (selectedTrackIds.begin(), selectedTrackIds.end(), id) == selectedTrackIds.end())
            selectedTrackIds.push_back (id);
    selectedClipId = state.selectedClipId;
    selectedAudioClipId = state.selectedAudioClipId;
    loopStart = std::max (0.0, state.loopStart);
    loopEnd = std::max (loopStart + 1.0 / 64.0, state.loopEnd);
    totalBeats = std::max (16.0, state.totalBeats);
    locators = std::move (state.locators);
    midiMappings = std::move (state.midiMappings);
    selectedLocatorId = state.selectedLocatorId;
    songStartLocatorId = state.songStartLocatorId;
    nextLocatorId = 1;
    for (const auto& marker : locators)
        nextLocatorId = std::max (nextLocatorId, marker.id + 1);
    if (locator (selectedLocatorId) == nullptr)
        selectedLocatorId = -1;
    if (locator (songStartLocatorId) == nullptr)
        songStartLocatorId = -1;
    std::sort (locators.begin(), locators.end(), [] (const auto& a, const auto& b)
    {
        return a.beat == b.beat ? a.id < b.id : a.beat < b.beat;
    });

    undoStack.clear();
    redoStack.clear();
    redoStackBeforeEdit.clear();
    editInProgress = false;

    for (const auto& track : tracks)
    {
        if (onTrackAdded)
            onTrackAdded (track->id);
        if (onTrackFreezeChanged)
            onTrackFreezeChanged (track->id);
        else if (track->type == roll::TrackType::Audio && onAudioClipsChanged)
            onAudioClipsChanged (track->id);
        if (track->type == roll::TrackType::Midi && onTrackNotes)
            onTrackNotes (track->id);
    }

    if (selectedTrack() == nullptr)
    {
        selectedTrackId = selectedTrackIds.empty()
            ? (tracks.empty() ? -1 : tracks.front()->id)
            : selectedTrackIds.back();
        selectedClipId = selectedAudioClipId = -1;
    }
    if (selectedTrackId >= 0
        && std::find (selectedTrackIds.begin(), selectedTrackIds.end(), selectedTrackId) == selectedTrackIds.end())
        selectedTrackIds.push_back (selectedTrackId);
    if (selectedTrackIds.empty() && selectedTrackId == 0)
        selectedTrackIds.push_back (0);
    if (selectedClip() == nullptr)
        selectedClipId = -1;
    if (auto* selected = selectedTrack(); selected == nullptr
        || selected->findAudioClip (selectedAudioClipId) == nullptr)
        selectedAudioClipId = -1;

    if (onMixerChanged)
        onMixerChanged();
    if (onLoopChanged)
        onLoopChanged();
    if (onSelectionChanged)
        onSelectionChanged();
    sendChangeMessage();
}

ProjectModel::Snapshot ProjectModel::makeSnapshot (bool includePluginState) const
{
    Snapshot snapshot;
    snapshot.master = *masterTrack;
    snapshot.tracks.reserve (tracks.size());
    for (const auto& track : tracks)
        snapshot.tracks.push_back (*track);
    snapshot.selectedTrackIds = selectedTrackIds;
    snapshot.selectedTrackId = selectedTrackId;
    snapshot.selectedClipId = selectedClipId;
    snapshot.selectedAudioClipId = selectedAudioClipId;
    snapshot.nextTrackId = nextTrackId;
    snapshot.loopStart = loopStart;
    snapshot.loopEnd = loopEnd;
    snapshot.totalBeats = totalBeats;
    snapshot.locators = locators;
    snapshot.midiMappings = midiMappings;
    snapshot.selectedLocatorId = selectedLocatorId;
    snapshot.songStartLocatorId = songStartLocatorId;
    snapshot.nextLocatorId = nextLocatorId;
    snapshot.includesPluginState = includePluginState;
    if (includePluginState && capturePluginHistoryState)
        snapshot.plugins = capturePluginHistoryState();
    return snapshot;
}

void ProjectModel::restoreSnapshot (Snapshot&& snapshot)
{
    *masterTrack = snapshot.master;
    std::vector<int> previousTrackIds;
    previousTrackIds.reserve (tracks.size());
    for (const auto& track : tracks)
        previousTrackIds.push_back (track->id);

    for (const auto& track : tracks)
        if (std::none_of (snapshot.tracks.begin(), snapshot.tracks.end(),
                          [&track] (const auto& restored) { return restored.id == track->id; })
            && onTrackRemoved)
            onTrackRemoved (track->id);

    tracks.clear();
    tracks.reserve (snapshot.tracks.size());
    for (const auto& track : snapshot.tracks)
        tracks.push_back (std::make_unique<roll::Track> (track));
    for (const auto& track : tracks)
        if (std::find (previousTrackIds.begin(), previousTrackIds.end(), track->id) == previousTrackIds.end()
            && onTrackAdded)
            onTrackAdded (track->id);

    selectedTrackId = snapshot.selectedTrackId;
    selectedTrackIds = std::move (snapshot.selectedTrackIds);
    selectedClipId = snapshot.selectedClipId;
    selectedAudioClipId = snapshot.selectedAudioClipId;
    nextTrackId = snapshot.nextTrackId;
    loopStart = snapshot.loopStart;
    loopEnd = snapshot.loopEnd;
    totalBeats = snapshot.totalBeats;
    locators = std::move (snapshot.locators);
    midiMappings = std::move (snapshot.midiMappings);
    selectedLocatorId = snapshot.selectedLocatorId;
    songStartLocatorId = snapshot.songStartLocatorId;
    nextLocatorId = snapshot.nextLocatorId;

    std::erase_if (selectedTrackIds, [this] (int id) { return track (id) == nullptr; });
    if (selectedTrack() == nullptr)
        selectedTrackId = selectedTrackIds.empty() ? (tracks.empty() ? -1 : tracks.front()->id)
                                                    : selectedTrackIds.back();
    if (selectedTrackId >= 0
        && std::find (selectedTrackIds.begin(), selectedTrackIds.end(), selectedTrackId) == selectedTrackIds.end())
        selectedTrackIds.push_back (selectedTrackId);

    for (const auto& track : tracks)
    {
        if (onTrackFreezeChanged)
            onTrackFreezeChanged (track->id);
        else if (track->type == roll::TrackType::Audio && onAudioClipsChanged)
            onAudioClipsChanged (track->id);
        if (track->type == roll::TrackType::Midi && onTrackNotes)
            onTrackNotes (track->id);
    }
    if (onSelectionChanged) onSelectionChanged();
    if (onMixerChanged) onMixerChanged();
    if (onLoopChanged) onLoopChanged();
    if (snapshot.includesPluginState && restorePluginHistoryState)
        restorePluginHistoryState (snapshot.plugins);
    sendChangeMessage();
}

void ProjectModel::recordUndo()
{
    if (! editInProgress)
    {
        undoStack.push_back (makeSnapshot());
        if (undoStack.size() > 200)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
}

void ProjectModel::beginEdit()
{
    if (editInProgress)
        return;
    editInProgress = true;
    undoStack.push_back (makeSnapshot());
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStackBeforeEdit = std::move (redoStack);
    redoStack.clear();
}

void ProjectModel::beginExternalEdit()
{
    if (editInProgress)
        return;
    editInProgress = true;
    undoStack.push_back (makeSnapshot (true));
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStackBeforeEdit = std::move (redoStack);
    redoStack.clear();
}

void ProjectModel::endEdit()
{
    if (! editInProgress)
        return;
    editInProgress = false;
    redoStackBeforeEdit.clear();
}

void ProjectModel::cancelEdit()
{
    if (! editInProgress)
        return;
    editInProgress = false;
    if (! undoStack.empty())
        undoStack.pop_back();
    redoStack = std::move (redoStackBeforeEdit);
}

bool ProjectModel::undo()
{
    endEdit();
    if (undoStack.empty())
        return false;
    redoStack.push_back (makeSnapshot (undoStack.back().includesPluginState));
    auto snapshot = std::move (undoStack.back());
    undoStack.pop_back();
    restoreSnapshot (std::move (snapshot));
    return true;
}

bool ProjectModel::redo()
{
    endEdit();
    if (redoStack.empty())
        return false;
    undoStack.push_back (makeSnapshot (redoStack.back().includesPluginState));
    auto snapshot = std::move (redoStack.back());
    redoStack.pop_back();
    restoreSnapshot (std::move (snapshot));
    return true;
}

bool ProjectModel::retainsAudioFile (const std::string& path) const
{
    const auto containsPath = [&path] (const std::vector<roll::Track>& snapshotTracks)
    {
        for (const auto& track : snapshotTracks)
        {
            for (const auto& clip : track.audioClips)
                if (clip->filePath == path)
                    return true;
            if (track.frozenAudioClip != nullptr && track.frozenAudioClip->filePath == path)
                return true;
        }
        return false;
    };

    for (const auto& track : tracks)
    {
        for (const auto& clip : track->audioClips)
            if (clip->filePath == path)
                return true;
        if (track->frozenAudioClip != nullptr && track->frozenAudioClip->filePath == path)
            return true;
    }
    for (const auto& snapshot : undoStack)
        if (containsPath (snapshot.tracks))
            return true;
    for (const auto& snapshot : redoStack)
        if (containsPath (snapshot.tracks))
            return true;
    return false;
}

roll::Track* ProjectModel::track (int id) const
{
    if (id == 0) return masterTrack.get();
    for (const auto& t : tracks) if (t->id == id) return t.get();
    return nullptr;
}

int ProjectModel::trackIndex (int id) const
{
    if (id == 0) return -1;
    for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i]->id == id) return static_cast<int> (i);
    return -1;
}

roll::Placement* ProjectModel::selectedClip() const
{
    auto* t = selectedTrack();
    return t != nullptr ? t->find (selectedClipId) : nullptr;
}

roll::Track* ProjectModel::addTrack (roll::TrackType type)
{
    recordUndo();
    auto t = std::make_unique<roll::Track>();
    t->id = nextTrackId++;
    t->type = type;
    t->name = std::to_string (t->id) + (type == roll::TrackType::Audio ? " AUDIO"
                               : type == roll::TrackType::Group ? " GROUP" : " MIDI");
    auto* raw = t.get();
    tracks.push_back (std::move (t));
    if (onTrackAdded) onTrackAdded (raw->id);
    selectTrack (raw->id);
    sendChangeMessage();
    return raw;
}

void ProjectModel::removeTrack (int id)
{
    if (tracks.size() <= 1) return;                       // Keep at least one track.
    const int idx = trackIndex (id);
    if (idx < 0) return;
    if (tracks[static_cast<size_t> (idx)]->type == roll::TrackType::Group)
    {
        ungroupTrack (id);
        return;
    }

    beginExternalEdit();
    if (selectedTrackId == id)
    {
        selectedClipId = -1;
        selectedAudioClipId = -1;
        if (onSelectionChanged) onSelectionChanged();     // The piano roll releases the clip before it is deleted.
    }
    if (onTrackRemoved) onTrackRemoved (id);
    std::erase_if (midiMappings, [id] (const MidiMapping& mapping)
    {
        return mapping.trackId == id;
    });
    tracks.erase (tracks.begin() + idx);
    std::erase (selectedTrackIds, id);

    if (selectedTrackId == id)
        selectedTrackId = selectedTrackIds.empty()
            ? tracks[static_cast<size_t> (std::min (idx, static_cast<int> (tracks.size()) - 1))]->id
            : selectedTrackIds.back();
    if (selectedTrackIds.empty() && selectedTrackId >= 0)
        selectedTrackIds.push_back (selectedTrackId);
    if (onSelectionChanged) onSelectionChanged();
    if (onMixerChanged) onMixerChanged();
    endEdit();
    sendChangeMessage();
}

bool ProjectModel::moveTrack (int id, int newIndex)
{
    const int oldIndex = trackIndex (id);
    if (oldIndex < 0 || newIndex < 0 || newIndex >= static_cast<int> (tracks.size())
        || oldIndex == newIndex)
        return false;

    recordUndo();
    auto moved = std::move (tracks[static_cast<std::size_t> (oldIndex)]);
    tracks.erase (tracks.begin() + oldIndex);
    tracks.insert (tracks.begin() + newIndex, std::move (moved));
    sendChangeMessage();
    return true;
}

int ProjectModel::duplicateTrack (int id)
{
    const int index = trackIndex (id);
    const auto* source = track (id);
    if (index < 0 || source == nullptr || source->type == roll::TrackType::Group)
        return -1;

    recordUndo();
    auto duplicate = std::make_unique<roll::Track> (*source);
    duplicate->id = nextTrackId++;
    duplicate->name = source->name + " Copy";
    const int duplicateId = duplicate->id;
    tracks.insert (tracks.begin() + index + 1, std::move (duplicate));

    selectedTrackIds.assign (1, duplicateId);
    selectedTrackId = duplicateId;
    selectedClipId = selectedAudioClipId = -1;
    if (onTrackAdded)
        onTrackAdded (duplicateId);
    if (source->type == roll::TrackType::Audio)
    {
        if (onAudioClipsChanged)
            onAudioClipsChanged (duplicateId);
    }
    else if (onTrackNotes)
        onTrackNotes (duplicateId);
    if (onMixerChanged)
        onMixerChanged();
    if (onSelectionChanged)
        onSelectionChanged();
    sendChangeMessage();
    return duplicateId;
}

int ProjectModel::groupTracks (const std::vector<int>& trackIds)
{
    std::vector<int> uniqueIds;
    for (const int id : trackIds)
        if (std::find (uniqueIds.begin(), uniqueIds.end(), id) == uniqueIds.end())
            uniqueIds.push_back (id);
    if (uniqueIds.size() < 2)
        return -1;

    int firstSelectedIndex = static_cast<int> (tracks.size());
    for (const int id : uniqueIds)
    {
        const int index = trackIndex (id);
        const auto* selected = track (id);
        if (index < 0 || selected == nullptr || selected->type == roll::TrackType::Group
            || selected->parentGroupId != 0)
            return -1;
        firstSelectedIndex = std::min (firstSelectedIndex, index);
    }

    recordUndo();
    const int groupId = nextTrackId++;
    const int insertIndex = static_cast<int> (std::count_if (
        tracks.begin(), tracks.begin() + firstSelectedIndex,
        [&uniqueIds] (const auto& item)
        {
            return std::find (uniqueIds.begin(), uniqueIds.end(), item->id) == uniqueIds.end();
        }));
    auto group = std::make_unique<roll::Track>();
    group->id = groupId;
    group->type = roll::TrackType::Group;
    group->name = std::to_string (groupId) + " GROUP";

    std::vector<std::unique_ptr<roll::Track>> selectedTracks, remainingTracks;
    selectedTracks.reserve (uniqueIds.size());
    remainingTracks.reserve (tracks.size() - uniqueIds.size() + 1);
    for (auto& item : tracks)
    {
        if (std::find (uniqueIds.begin(), uniqueIds.end(), item->id) != uniqueIds.end())
        {
            item->parentGroupId = groupId;
            selectedTracks.push_back (std::move (item));
        }
        else
            remainingTracks.push_back (std::move (item));
    }
    remainingTracks.insert (remainingTracks.begin() + insertIndex, std::move (group));
    remainingTracks.insert (remainingTracks.begin() + insertIndex + 1,
                            std::make_move_iterator (selectedTracks.begin()),
                            std::make_move_iterator (selectedTracks.end()));
    tracks = std::move (remainingTracks);

    selectedTrackIds.assign (1, groupId);
    selectedTrackId = groupId;
    selectedClipId = selectedAudioClipId = -1;
    if (onTrackAdded)
        onTrackAdded (groupId);
    if (onMixerChanged)
        onMixerChanged();
    if (onSelectionChanged)
        onSelectionChanged();
    sendChangeMessage();
    return groupId;
}

bool ProjectModel::ungroupTrack (int groupId)
{
    const int index = trackIndex (groupId);
    if (index < 0 || tracks[static_cast<size_t> (index)]->type != roll::TrackType::Group)
        return false;

    recordUndo();
    int firstChildId = -1;
    for (const auto& item : tracks)
        if (item->parentGroupId == groupId)
        {
            if (firstChildId < 0)
                firstChildId = item->id;
            item->parentGroupId = 0;
        }

    if (selectedTrackId == groupId)
    {
        selectedTrackId = firstChildId;
        selectedTrackIds.clear();
        if (selectedTrackId >= 0)
            selectedTrackIds.push_back (selectedTrackId);
        selectedClipId = selectedAudioClipId = -1;
        if (onSelectionChanged)
            onSelectionChanged();
    }
    if (onTrackRemoved)
        onTrackRemoved (groupId);
    std::erase_if (midiMappings, [groupId] (const MidiMapping& mapping)
    {
        return mapping.trackId == groupId;
    });
    tracks.erase (tracks.begin() + index);
    std::erase (selectedTrackIds, groupId);
    if (selectedTrackId < 0 && ! tracks.empty())
    {
        selectedTrackId = tracks[static_cast<size_t> (std::min (index, static_cast<int> (tracks.size()) - 1))]->id;
        selectedTrackIds.assign (1, selectedTrackId);
    }
    if (onMixerChanged)
        onMixerChanged();
    if (onSelectionChanged)
        onSelectionChanged();
    sendChangeMessage();
    return true;
}

bool ProjectModel::isTrackSelected (int id) const
{
    return std::find (selectedTrackIds.begin(), selectedTrackIds.end(), id) != selectedTrackIds.end();
}

void ProjectModel::selectTrack (int id, bool toggleSelection)
{
    if (track (id) == nullptr)
        return;

    if (! toggleSelection)
    {
        selectedTrackIds.assign (1, id);
        selectedTrackId = id;
        selectedClipId = selectedAudioClipId = -1;
    }
    else if (isTrackSelected (id))
    {
        if (selectedTrackIds.size() > 1)
        {
            std::erase (selectedTrackIds, id);
            if (selectedTrackId == id)
                selectedTrackId = selectedTrackIds.back();
            selectedClipId = selectedAudioClipId = -1;
        }
    }
    else
    {
        selectedTrackIds.push_back (id);
        selectedTrackId = id;
        selectedClipId = selectedAudioClipId = -1;
    }

    if (onSelectionChanged) onSelectionChanged();
    sendChangeMessage();
}

void ProjectModel::selectTrackRange (int anchorTrackId, int targetTrackId, bool additive)
{
    const int anchorIndex = trackIndex (anchorTrackId);
    const int targetIndex = trackIndex (targetTrackId);
    if (anchorIndex < 0 || targetIndex < 0)
        return;

    const int first = std::min (anchorIndex, targetIndex);
    const int last = std::max (anchorIndex, targetIndex);
    if (! additive)
        selectedTrackIds.clear();

    for (int i = first; i <= last; ++i)
    {
        const int id = tracks[static_cast<std::size_t> (i)]->id;
        if (! isTrackSelected (id))
            selectedTrackIds.push_back (id);
    }

    selectedTrackId = targetTrackId;
    selectedClipId = selectedAudioClipId = -1;
    if (onSelectionChanged) onSelectionChanged();
    sendChangeMessage();
}

void ProjectModel::setTrackSolo (int id, bool solo, bool additive)
{
    const auto target = std::find_if (tracks.begin(), tracks.end(),
        [id] (const auto& item) { return item->id == id; });
    if (target == tracks.end())
        return;

    const bool applyToSelection = ! additive && solo
        && isTrackSelected (id) && selectedTrackIds.size() > 1;
    bool changed = false;
    for (const auto& item : tracks)
    {
        const bool desired = additive ? (item->id == id ? solo : item->solo)
            : solo ? (applyToSelection ? isTrackSelected (item->id) : item->id == id)
                   : false;
        changed |= item->solo != desired;
    }
    if (! changed)
        return;
    recordUndo();

    if (additive)
    {
        (*target)->solo = solo;
    }
    else if (solo)
    {
        for (auto& item : tracks)
            item->solo = applyToSelection
                ? isTrackSelected (item->id)
                : item->id == id;
    }
    else
    {
        for (auto& item : tracks)
            item->solo = false;
    }
    notifyMixer();
}

void ProjectModel::selectClip (int trackId, int clipId)
{
    if (track (trackId) == nullptr) return;
    selectedTrackIds.assign (1, trackId);
    selectedTrackId = trackId;
    selectedClipId = clipId;
    selectedAudioClipId = -1;
    if (onSelectionChanged) onSelectionChanged();
    sendChangeMessage();
}

void ProjectModel::selectAudioClip (int trackId, int clipId)
{
    auto* t = track (trackId);
    if (t == nullptr || t->findAudioClip (clipId) == nullptr) return;
    selectedTrackIds.assign (1, trackId);
    selectedTrackId = trackId;
    selectedClipId = -1;
    selectedAudioClipId = clipId;
    if (onSelectionChanged) onSelectionChanged();
    sendChangeMessage();
}

bool ProjectModel::setTrackColour (int trackId, std::uint32_t colour)
{
    auto* t = track (trackId);
    if (t == nullptr || t->colour == colour)
        return false;

    recordUndo();
    t->colour = colour;
    sendChangeMessage();
    return true;
}

void ProjectModel::setTrackRouting (int trackId, roll::TrackOutputRouting routing)
{
    if (auto* t = track (trackId))
    {
        if (t->outputRouting != routing)
        {
            recordUndo();
            t->outputRouting = routing;
            notifyMixer();
        }
    }
}

roll::Placement* ProjectModel::addClip (int trackId, double start, double length)
{
    auto* t = track (trackId);
    if (t == nullptr || t->type != roll::TrackType::Midi || ! t->isFree (start, length)) return nullptr;
    recordUndo();
    auto* p = t->addClip (start, length);
    growTo (p->end() + 64.0);
    selectClip (trackId, p->id);
    notifyTrackNotes (trackId);
    return p;
}

int ProjectModel::copyClip (int fromTrack, int clipId, int toTrack, double start)
{
    auto* sourceTrack = track (fromTrack);
    auto* destinationTrack = track (toTrack);
    auto* source = sourceTrack != nullptr ? sourceTrack->find (clipId) : nullptr;
    if (source == nullptr || sourceTrack->type != roll::TrackType::Midi
        || destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Midi
        || ! std::isfinite (start) || start < 0.0)
        return -1;

    const roll::Placement sourceSnapshot (*source);
    const double length = sourceSnapshot.clip.length();
    if (! std::isfinite (length) || length <= 0.0)
        return -1;
    roll::Track updatedDestination (*destinationTrack);
    if (! replaceMidiRange (updatedDestination, start, start + length, -1))
        return -1;
    auto* copy = updatedDestination.copyClip (sourceSnapshot, start);
    if (copy == nullptr)
        return -1;

    const int copyId = copy->id;
    recordUndo();
    *destinationTrack = std::move (updatedDestination);
    growTo (start + length + 64.0);
    selectClip (toTrack, copyId);
    notifyTrackNotes (toTrack);
    sendChangeMessage();
    return copyId;
}

int ProjectModel::copyClip (const roll::Placement& source, int toTrack, double start)
{
    auto* destinationTrack = track (toTrack);
    if (destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Midi
        || ! std::isfinite (start) || start < 0.0 || source.clip.length() <= 0.0
        || ! destinationTrack->isFree (start, source.clip.length()))
        return -1;

    recordUndo();
    auto* copy = destinationTrack->copyClip (source, start);
    if (copy == nullptr)
        return -1;

    growTo (copy->end() + 64.0);
    selectClip (toTrack, copy->id);
    notifyTrackNotes (toTrack);
    sendChangeMessage();
    return copy->id;
}

int ProjectModel::copyClipReplacingRange (const roll::Placement& source, int toTrack, double start)
{
    auto* destinationTrack = track (toTrack);
    const double length = source.clip.length();
    if (destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Midi
        || ! std::isfinite (start) || start < 0.0 || ! std::isfinite (length) || length <= 0.0)
        return -1;

    roll::Track updatedDestination (*destinationTrack);
    if (! replaceMidiRange (updatedDestination, start, start + length, -1))
        return -1;
    auto* copy = updatedDestination.copyClip (source, start);
    if (copy == nullptr)
        return -1;

    const int copyId = copy->id;
    recordUndo();
    *destinationTrack = std::move (updatedDestination);
    growTo (start + length + 64.0);
    selectClip (toTrack, copyId);
    notifyTrackNotes (toTrack);
    sendChangeMessage();
    return copyId;
}

int ProjectModel::duplicateClip (int trackId, int clipId)
{
    auto* t = track (trackId);
    auto* source = t != nullptr ? t->find (clipId) : nullptr;
    if (source == nullptr)
        return -1;

    const double length = source->clip.length();
    double start = source->end();
    for (int attempt = 0; attempt < 64; ++attempt, start += length)
        if (t->isFree (start, length))
        {
            recordUndo();
            auto* copy = t->copyClip (*source, start);
            if (copy == nullptr)
                return -1;
            growTo (copy->end() + 64.0);
            selectClip (trackId, copy->id);
            notifyTrackNotes (trackId);
            sendChangeMessage();
            return copy->id;
        }
    return -1;
}

roll::AudioPlacement* ProjectModel::addAudioClip (int trackId, double start, double length,
                                                   const std::string& filePath, const std::string& name,
                                                   const std::vector<roll::WaveformRange>& waveform,
                                                   double durationSeconds, double sourceBpm)
{
    auto* t = track (trackId);
    if (t == nullptr || t->type != roll::TrackType::Audio || start < 0.0 || length <= 0.0
        || filePath.empty() || durationSeconds < 0.0 || sourceBpm < 20.0 || sourceBpm > 300.0
        || ! t->isAudioFree (start, length))
        return nullptr;

    recordUndo();
    auto* clip = t->addAudioClip (start, length, filePath, name, waveform, durationSeconds, sourceBpm);
    growTo (clip->end() + 64.0);
    selectAudioClip (trackId, clip->id);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    return clip;
}

roll::AudioPlacement* ProjectModel::addAudioClipReplacingRange (
    int trackId, double start, double length, const std::string& filePath, const std::string& name,
    const std::vector<roll::WaveformRange>& waveform, double durationSeconds, double sourceBpm,
    double edgeFadeBeats)
{
    auto* t = track (trackId);
    if (t == nullptr || t->type != roll::TrackType::Audio || ! std::isfinite (start)
        || ! std::isfinite (length) || ! std::isfinite (durationSeconds)
        || ! std::isfinite (sourceBpm) || ! std::isfinite (edgeFadeBeats)
        || start < 0.0 || length <= 0.0 || edgeFadeBeats < 0.0
        || filePath.empty() || durationSeconds < 0.0 || sourceBpm < 20.0 || sourceBpm > 300.0)
        return nullptr;

    roll::Track updatedTrack (*t);
    if (! replaceAudioRange (updatedTrack, start, start + length, -1))
        return nullptr;
    auto* clip = updatedTrack.addAudioClip (start, length, filePath, name, waveform,
                                            durationSeconds, sourceBpm);
    if (clip == nullptr)
        return nullptr;
    clip->fadeInBeats = clip->fadeOutBeats = std::min (edgeFadeBeats, length * 0.5);

    const int clipId = clip->id;
    recordUndo();
    *t = std::move (updatedTrack);
    growTo (start + length + 64.0);
    selectAudioClip (trackId, clipId);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    return t->findAudioClip (clipId);
}

void ProjectModel::deleteClip (int trackId, int clipId)
{
    auto* t = track (trackId);
    if (t == nullptr || t->find (clipId) == nullptr) return;

    recordUndo();
    if (selectedTrackId == trackId && selectedClipId == clipId)
    {
        selectedClipId = -1;
        if (onSelectionChanged) onSelectionChanged();
    }
    t->remove (clipId);
    notifyTrackNotes (trackId);
    sendChangeMessage();
}

int ProjectModel::splitClip (int trackId, int clipId, double beat)
{
    auto* t = track (trackId);
    auto* source = t != nullptr ? t->find (clipId) : nullptr;
    if (t == nullptr || source == nullptr || beat <= source->start
        || beat >= source->end() || ! t->isFree (beat, source->end() - beat, clipId))
        return -1;

    recordUndo();
    const int newId = t->splitClip (clipId, beat);
    if (newId < 0)
        return -1;

    growTo (beat + 64.0);
    selectClip (trackId, newId);
    notifyTrackNotes (trackId);
    sendChangeMessage();
    return newId;
}

void ProjectModel::deleteAudioClip (int trackId, int clipId)
{
    auto* t = track (trackId);
    if (t == nullptr || t->findAudioClip (clipId) == nullptr) return;
    recordUndo();
    if (selectedTrackId == trackId && selectedAudioClipId == clipId)
    {
        selectedAudioClipId = -1;
        if (onSelectionChanged) onSelectionChanged();
    }
    t->removeAudioClip (clipId);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
}

bool ProjectModel::setClipEnabled (int trackId, int clipId, bool audio, bool enabled)
{
    auto* t = track (trackId);
    if (t == nullptr)
        return false;

    if (audio)
    {
        auto* clip = t->type == roll::TrackType::Audio ? t->findAudioClip (clipId) : nullptr;
        if (clip == nullptr || clip->enabled == enabled)
            return false;
        recordUndo();
        clip->enabled = enabled;
        if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    }
    else
    {
        auto* clip = t->type == roll::TrackType::Midi ? t->find (clipId) : nullptr;
        if (clip == nullptr || clip->enabled == enabled)
            return false;
        recordUndo();
        clip->enabled = enabled;
        notifyTrackNotes (trackId);
    }

    sendChangeMessage();
    return true;
}

bool ProjectModel::setAutomationEnvelope (int trackId, roll::AutomationEnvelope envelope)
{
    auto* targetTrack = trackId == 0 ? masterTrack.get() : track (trackId);
    if (targetTrack == nullptr || ! roll::isValidAutomationEnvelope (envelope))
        return false;
    if (isAudioClipAutomation (envelope.target.type)
        && (targetTrack->type != roll::TrackType::Audio
            || targetTrack->findAudioClip (envelope.target.clipId) == nullptr))
        return false;

    const auto found = std::find_if (targetTrack->automation.begin(), targetTrack->automation.end(),
        [&envelope] (const auto& existing) { return existing.target == envelope.target; });
    if (found != targetTrack->automation.end() && found->points == envelope.points)
        return true;

    const bool audioClipAutomation = isAudioClipAutomation (envelope.target.type);
    recordUndo();
    if (found == targetTrack->automation.end())
        targetTrack->automation.push_back (std::move (envelope));
    else
        *found = std::move (envelope);

    if (onMixerChanged)
        onMixerChanged();
    if (audioClipAutomation && onAudioClipAutomationChanged)
        onAudioClipAutomationChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::removeAutomationEnvelope (int trackId, const roll::AutomationTarget& target)
{
    auto* targetTrack = trackId == 0 ? masterTrack.get() : track (trackId);
    if (targetTrack == nullptr)
        return false;
    const auto found = std::find_if (targetTrack->automation.begin(), targetTrack->automation.end(),
        [&target] (const auto& envelope) { return envelope.target == target; });
    if (found == targetTrack->automation.end())
        return false;

    const bool audioClipAutomation = isAudioClipAutomation (found->target.type);
    recordUndo();
    targetTrack->automation.erase (found);
    if (onMixerChanged)
        onMixerChanged();
    if (audioClipAutomation && onAudioClipAutomationChanged)
        onAudioClipAutomationChanged (trackId);
    sendChangeMessage();
    return true;
}

int ProjectModel::copyAudioClip (int fromTrack, int clipId, int toTrack, double start)
{
    auto* sourceTrack = track (fromTrack);
    auto* destinationTrack = track (toTrack);
    auto* source = sourceTrack != nullptr ? sourceTrack->findAudioClip (clipId) : nullptr;
    if (source == nullptr || sourceTrack->type != roll::TrackType::Audio
        || destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Audio
        || ! std::isfinite (start) || start < 0.0)
        return -1;

    const roll::AudioPlacement sourceSnapshot (*source);
    const double length = sourceSnapshot.lengthBeats;
    if (! std::isfinite (length) || length <= 0.0)
        return -1;
    roll::Track updatedDestination (*destinationTrack);
    if (! replaceAudioRange (updatedDestination, start, start + length, -1))
        return -1;
    auto* copy = updatedDestination.copyAudioClip (sourceSnapshot, start);
    if (copy == nullptr)
        return -1;

    const int copyId = copy->id;
    recordUndo();
    *destinationTrack = std::move (updatedDestination);
    growTo (start + length + 64.0);
    selectAudioClip (toTrack, copyId);
    if (onAudioClipsChanged) onAudioClipsChanged (toTrack);
    sendChangeMessage();
    return copyId;
}

int ProjectModel::copyAudioClip (const roll::AudioPlacement& source, int toTrack, double start)
{
    auto* destinationTrack = track (toTrack);
    if (destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Audio
        || ! std::isfinite (start) || start < 0.0 || source.lengthBeats <= 0.0
        || ! destinationTrack->isAudioFree (start, source.lengthBeats))
        return -1;

    recordUndo();
    auto* copy = destinationTrack->copyAudioClip (source, start);
    if (copy == nullptr)
        return -1;

    growTo (copy->end() + 64.0);
    selectAudioClip (toTrack, copy->id);
    if (onAudioClipsChanged) onAudioClipsChanged (toTrack);
    sendChangeMessage();
    return copy->id;
}

int ProjectModel::copyAudioClipReplacingRange (const roll::AudioPlacement& source, int toTrack, double start)
{
    auto* destinationTrack = track (toTrack);
    if (destinationTrack == nullptr || destinationTrack->type != roll::TrackType::Audio
        || ! std::isfinite (start) || start < 0.0 || ! std::isfinite (source.lengthBeats)
        || source.lengthBeats <= 0.0)
        return -1;

    roll::Track updatedDestination (*destinationTrack);
    if (! replaceAudioRange (updatedDestination, start, start + source.lengthBeats, -1))
        return -1;
    auto* copy = updatedDestination.copyAudioClip (source, start);
    if (copy == nullptr)
        return -1;

    const int copyId = copy->id;
    recordUndo();
    *destinationTrack = std::move (updatedDestination);
    growTo (start + source.lengthBeats + 64.0);
    selectAudioClip (toTrack, copyId);
    if (onAudioClipsChanged)
        onAudioClipsChanged (toTrack);
    sendChangeMessage();
    return copyId;
}

int ProjectModel::duplicateAudioClip (int trackId, int clipId)
{
    auto* t = track (trackId);
    auto* source = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (source == nullptr)
        return -1;

    const double length = source->lengthBeats;
    double start = source->end();
    for (int attempt = 0; attempt < 64; ++attempt, start += length)
        if (t->isAudioFree (start, length))
        {
            recordUndo();
            auto* copy = t->copyAudioClip (*source, start);
            if (copy == nullptr)
                return -1;
            growTo (copy->end() + 64.0);
            selectAudioClip (trackId, copy->id);
            if (onAudioClipsChanged) onAudioClipsChanged (trackId);
            sendChangeMessage();
            return copy->id;
        }
    return -1;
}

int ProjectModel::splitAudioClip (int trackId, int clipId, double beat)
{
    auto* t = track (trackId);
    auto* source = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || source == nullptr || beat <= source->start
        || beat >= source->end() || ! t->isAudioFree (beat, source->end() - beat, clipId))
        return -1;

    recordUndo();
    const int newId = t->splitAudioClip (clipId, beat);
    if (newId < 0)
        return -1;

    if (auto* clip = t->findAudioClip (newId))
        growTo (clip->end() + 64.0);
    selectAudioClip (trackId, newId);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return newId;
}

bool ProjectModel::joinAudioClips (int trackId, int leftClipId, int rightClipId)
{
    auto* t = track (trackId);
    auto* left = t != nullptr ? t->findAudioClip (leftClipId) : nullptr;
    auto* right = t != nullptr ? t->findAudioClip (rightClipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || left == nullptr || right == nullptr
        || left->filePath != right->filePath
        || std::abs (left->end() - right->start) > 1.0e-7)
        return false;

    const double leftLength = left->lengthBeats;
    const double leftSourceEnd = left->sourceOffsetSeconds + left->durationSeconds;
    std::vector<roll::AudioWarpMarker> joinedMarkers;
    if (! left->warpMarkers.empty() || ! right->warpMarkers.empty())
    {
        auto leftMarkers = effectiveWarpMarkers (*left);
        auto rightMarkers = effectiveWarpMarkers (*right);
        if (std::abs (leftSourceEnd - rightMarkers.front().sourceSeconds) <= 1.0e-7)
        {
            joinedMarkers = std::move (leftMarkers);
            for (std::size_t i = 1; i < rightMarkers.size(); ++i)
            {
                rightMarkers[i].beat += leftLength;
                joinedMarkers.push_back (rightMarkers[i]);
            }
        }
    }
    recordUndo();
    left->lengthBeats += right->lengthBeats;
    left->durationSeconds += right->durationSeconds;
    left->warpMarkers = std::move (joinedMarkers);
    left->fadeOutBeats = right->fadeOutBeats;
    left->fadeOutCurve = right->fadeOutCurve;
    left->waveform.insert (left->waveform.end(),
                           right->waveform.begin(), right->waveform.end());
    t->removeAudioClip (rightClipId);
    growTo (left->end() + 64.0);
    selectAudioClip (trackId, leftClipId);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::resizeAudioClip (int trackId, int clipId, double length)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr || length <= 0.0
        || ! t->isAudioFree (clip->start, length, clipId))
        return false;
    recordUndo();
    const double oldLength = clip->lengthBeats;
    clip->lengthBeats = length;
    resizeWarpMarkers (*clip, oldLength, clip->sourceOffsetSeconds);
    clip->fadeInBeats = std::min (clip->fadeInBeats, length);
    clip->fadeOutBeats = std::min (clip->fadeOutBeats, length);
    growTo (clip->end() + 64.0);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::resizeAudioClipStart (int trackId, int clipId, double start, double length)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || ! std::isfinite (start) || ! std::isfinite (length)
        || start < 0.0 || length <= 0.0
        || ! t->isAudioFree (start, length, clipId))
        return false;

    recordUndo();
    const double oldLength = clip->lengthBeats;
    const double oldSourceOffset = clip->sourceOffsetSeconds;
    const double beatDelta = start - clip->start;
    if (beatDelta < 0.0 && clip->sourceBpm > 0.0)
        clip->sourceOffsetSeconds = std::max (
            0.0, clip->sourceOffsetSeconds + beatDelta * 60.0 / clip->sourceBpm);
    clip->start = start;
    clip->lengthBeats = length;
    resizeWarpMarkers (*clip, oldLength, oldSourceOffset);
    clip->fadeInBeats = std::min (clip->fadeInBeats, length);
    clip->fadeOutBeats = std::min (clip->fadeOutBeats, length);
    growTo (clip->end() + 64.0);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::resizeClipStart (int trackId, int clipId, double start, double length)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->find (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Midi || clip == nullptr
        || ! std::isfinite (start) || ! std::isfinite (length)
        || start < 0.0 || length <= 0.0
        || ! t->isFree (start, length, clipId))
        return false;

    recordUndo();
    clip->start = start;
    clip->clip.setLength (length);
    growTo (clip->end() + 64.0);
    if (onTrackNotes) onTrackNotes (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::joinMidiClips (int trackId, int leftClipId, int rightClipId)
{
    auto* t = track (trackId);
    auto* left = t != nullptr ? t->find (leftClipId) : nullptr;
    auto* right = t != nullptr ? t->find (rightClipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Midi || left == nullptr || right == nullptr
        || std::abs (left->end() - right->start) > 1.0e-7)
        return false;

    recordUndo();
    std::vector<roll::Note> notes = left->clip.notes();
    const double offset = right->start - left->start;
    for (const auto& rightNote : right->clip.notes())
    {
        auto note = rightNote;
        note.startBeats += offset;
        notes.push_back (note);
    }
    left->clip.setLength (left->clip.length() + right->clip.length());
    left->clip.setNotes (std::move (notes));
    t->remove (rightClipId);
    growTo (left->end() + 64.0);
    selectClip (trackId, leftClipId);
    if (onTrackNotes) onTrackNotes (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipFades (int trackId, int clipId, double fadeInBeats,
                                      double fadeOutBeats)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || ! std::isfinite (fadeInBeats) || ! std::isfinite (fadeOutBeats)
        || fadeInBeats < 0.0 || fadeOutBeats < 0.0
        || fadeInBeats > clip->lengthBeats || fadeOutBeats > clip->lengthBeats)
        return false;

    if (clip->fadeInBeats == fadeInBeats && clip->fadeOutBeats == fadeOutBeats)
        return true;

    recordUndo();
    clip->fadeInBeats = fadeInBeats;
    clip->fadeOutBeats = fadeOutBeats;
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipFadeCurves (int trackId, int clipId, double fadeInCurve,
                                           double fadeOutCurve)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || ! std::isfinite (fadeInCurve) || ! std::isfinite (fadeOutCurve)
        || fadeInCurve < -1.0 || fadeInCurve > 1.0
        || fadeOutCurve < -1.0 || fadeOutCurve > 1.0)
        return false;

    if (clip->fadeInCurve == fadeInCurve && clip->fadeOutCurve == fadeOutCurve)
        return true;

    recordUndo();
    clip->fadeInCurve = fadeInCurve;
    clip->fadeOutCurve = fadeOutCurve;
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipSourceBpm (int trackId, int clipId, double sourceBpm)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || sourceBpm < 20.0 || sourceBpm > 300.0 || clip->durationSeconds <= 0.0)
        return false;

    const double newLength = clip->durationSeconds * sourceBpm / 60.0;
    if (! t->isAudioFree (clip->start, newLength, clipId))
        return false;

    recordUndo();
    const double oldLength = clip->lengthBeats;
    clip->sourceBpm = sourceBpm;
    clip->lengthBeats = newLength;
    resizeWarpMarkers (*clip, oldLength, clip->sourceOffsetSeconds);
    clip->fadeInBeats = std::min (clip->fadeInBeats, newLength);
    clip->fadeOutBeats = std::min (clip->fadeOutBeats, newLength);
    growTo (clip->end() + 64.0);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipWarpMarkers (
    int trackId, int clipId, const std::vector<roll::AudioWarpMarker>& markers)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || ! roll::isValidAudioWarpMarkers (markers, clip->sourceOffsetSeconds,
                                            clip->durationSeconds, clip->lengthBeats))
        return false;
    if (clip->warpMarkers == markers)
        return true;

    recordUndo();
    clip->warpMarkers = markers;
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::deleteRange (int trackId, double start, double end)
{
    auto* t = track (trackId);
    if (t == nullptr || ! std::isfinite (start) || ! std::isfinite (end)
        || end <= start)
        return false;

    roll::Track updated (*t);
    if (t->type == roll::TrackType::Audio)
    {
        if (! replaceAudioRange (updated, start, end, -1))
            return false;
    }
    else
    {
        std::vector<int> ids;
        for (const auto& clip : t->clips)
            if (start < clip->end() - clipOverlapEpsilon
                && end > clip->start + clipOverlapEpsilon)
                ids.push_back (clip->id);

        for (const int id : ids)
        {
            auto* clip = updated.find (id);
            if (clip == nullptr)
                continue;
            const double clipStart = clip->start;
            const double clipEnd = clip->end();
            const bool preserveLeft = clipStart < start - clipOverlapEpsilon;
            const bool preserveRight = clipEnd > end + clipOverlapEpsilon;

            if (preserveLeft && preserveRight)
            {
                const int middleId = updated.splitClip (id, start);
                if (middleId < 0 || updated.splitClip (middleId, end) < 0
                    || ! updated.remove (middleId))
                    return false;
            }
            else if (preserveLeft)
            {
                const int rightId = updated.splitClip (id, start);
                if (rightId < 0 || ! updated.remove (rightId))
                    return false;
            }
            else if (preserveRight)
            {
                if (updated.splitClip (id, end) < 0 || ! updated.remove (id))
                    return false;
            }
            else
                if (! updated.remove (id))
                    return false;
        }
    }

    recordUndo();
    *t = std::move (updated);
    if (t->type == roll::TrackType::Audio)
    {
        if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    }
    else if (onTrackNotes)
        onTrackNotes (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::cropRange (int trackId, double start, double end)
{
    auto* t = track (trackId);
    if (t == nullptr || ! std::isfinite (start) || ! std::isfinite (end)
        || end <= start)
        return false;

    roll::Track updated (*t);
    if (t->type == roll::TrackType::Audio)
    {
        std::vector<int> ids;
        for (const auto& clip : t->audioClips)
            if (clip->start < end - clipOverlapEpsilon
                && clip->end() > start + clipOverlapEpsilon)
                ids.push_back (clip->id);

        for (const int id : ids)
        {
            auto* clip = updated.findAudioClip (id);
            if (clip == nullptr)
                continue;
            const double left = std::max (start, clip->start);
            const double right = std::min (end, clip->end());
            if (right <= left)
                continue;
            clip->sourceOffsetSeconds += std::max (0.0, left - clip->start)
                                       * 60.0 / std::max (clip->sourceBpm, 20.0);
            clip->start = left;
            clip->lengthBeats = right - left;
            clip->durationSeconds = clip->lengthBeats * 60.0
                                  / std::max (clip->sourceBpm, 20.0);
        }
        for (auto it = updated.audioClips.begin(); it != updated.audioClips.end();)
        {
            const auto& clip = *it;
            if (clip->end() <= start + clipOverlapEpsilon
                || clip->start >= end - clipOverlapEpsilon)
                it = updated.audioClips.erase (it);
            else
                ++it;
        }
    }
    else
    {
        for (const auto& clip : t->clips)
        {
            if (clip->start < end - clipOverlapEpsilon
                && clip->end() > start + clipOverlapEpsilon)
            {
                auto* updatedClip = updated.find (clip->id);
                if (updatedClip == nullptr)
                    continue;
                const double left = std::max (start, clip->start);
                const double right = std::min (end, clip->end());
                std::vector<roll::Note> notes;
                for (const auto& note : clip->clip.notes())
                {
                    const double absoluteStart = clip->start + note.startBeats;
                    const double absoluteEnd = absoluteStart + note.lengthBeats;
                    if (absoluteEnd <= left + clipOverlapEpsilon
                        || absoluteStart >= right - clipOverlapEpsilon)
                        continue;
                    roll::Note cropped = note;
                    cropped.startBeats = std::max (absoluteStart, left) - left;
                    const double croppedEnd = std::min (absoluteEnd, right) - left;
                    cropped.lengthBeats = std::max (clipOverlapEpsilon,
                                                    croppedEnd - cropped.startBeats);
                    notes.push_back (cropped);
                }
                updatedClip->start = left;
                updatedClip->clip.setLength (right - left);
                updatedClip->clip.setNotes (std::move (notes));
            }
        }
        for (auto it = updated.clips.begin(); it != updated.clips.end();)
        {
            const auto& clip = *it;
            if (clip->end() <= start + clipOverlapEpsilon
                || clip->start >= end - clipOverlapEpsilon)
                it = updated.clips.erase (it);
            else
                ++it;
        }
    }

    recordUndo();
    *t = std::move (updated);
    if (t->type == roll::TrackType::Audio)
    {
        if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    }
    else if (onTrackNotes)
        onTrackNotes (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipPlayback (
    int trackId, int clipId, double pitchSemitones, double pitchCents,
    double gainDb, int warpMode, bool reverse)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr
        || ! std::isfinite (pitchSemitones) || ! std::isfinite (pitchCents)
        || ! std::isfinite (gainDb) || pitchSemitones < -24.0
        || pitchSemitones > 24.0 || pitchCents < -100.0
        || pitchCents > 100.0 || gainDb < -24.0 || gainDb > 24.0
        || warpMode < 0 || warpMode > 5)
        return false;

    if (clip->pitchSemitones == pitchSemitones
        && clip->pitchCents == pitchCents
        && clip->gainDb == gainDb
        && clip->warpMode == warpMode
        && clip->reverse == reverse)
        return true;

    recordUndo();
    clip->pitchSemitones = pitchSemitones;
    clip->pitchCents = pitchCents;
    clip->gainDb = gainDb;
    clip->warpMode = warpMode;
    clip->reverse = reverse;
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setAudioClipWarpEnabled (int trackId, int clipId, bool enabled)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr)
        return false;
    if (clip->warpEnabled == enabled)
        return true;

    recordUndo();
    clip->warpEnabled = enabled;
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
    sendChangeMessage();
    return true;
}

int ProjectModel::moveClip (int fromTrack, int clipId, int toTrack, double newStart)
{
    auto* src = track (fromTrack);
    auto* dst = track (toTrack);
    if (src == nullptr || dst == nullptr
        || src->type != roll::TrackType::Midi || dst->type != roll::TrackType::Midi) return -1;
    auto* p = src->find (clipId);
    if (p == nullptr || ! std::isfinite (newStart)) return -1;

    const double len = p->clip.length();
    newStart = std::max (0.0, newStart);
    if (! std::isfinite (len) || len <= 0.0)
        return -1;
    if (fromTrack == toTrack && std::abs (p->start - newStart) <= clipOverlapEpsilon)
        return clipId;

    roll::Track updatedDestination (*dst);
    if (! replaceMidiRange (updatedDestination, newStart, newStart + len,
                            fromTrack == toTrack ? clipId : -1))
        return -1;
    recordUndo();
    int newId = clipId;
    if (fromTrack == toTrack)
    {
        auto* movedClip = updatedDestination.find (clipId);
        if (movedClip == nullptr)
            return -1;
        movedClip->start = newStart;
        *src = std::move (updatedDestination);
    }
    else
    {
        auto taken = src->take (clipId);
        if (taken == nullptr)
            return -1;
        taken->start = newStart;
        updatedDestination.put (std::move (taken));
        newId = updatedDestination.clips.back()->id;
        *dst = std::move (updatedDestination);
    }

    const bool sourceWasSelected = selectedTrackId == fromTrack && selectedClipId == clipId;
    const bool destinationSelectionRemoved = selectedTrackId == toTrack && selectedClipId > 0
        && dst->find (selectedClipId) == nullptr;
    if (sourceWasSelected || destinationSelectionRemoved)
    {
        selectedTrackId = toTrack;
        selectedTrackIds.assign (1, toTrack);
        selectedClipId = newId;
        selectedAudioClipId = -1;
    }
    growTo (newStart + len + 64.0);
    notifyTrackNotes (fromTrack);
    if (fromTrack != toTrack)
        notifyTrackNotes (toTrack);
    if (sourceWasSelected || destinationSelectionRemoved || fromTrack != toTrack)
        if (onSelectionChanged) onSelectionChanged();
    sendChangeMessage();
    return newId;
}

int ProjectModel::moveAudioClip (int fromTrack, int clipId, int toTrack, double newStart)
{
    auto* src = track (fromTrack);
    auto* dst = track (toTrack);
    if (src == nullptr || dst == nullptr
        || src->type != roll::TrackType::Audio || dst->type != roll::TrackType::Audio)
        return -1;
    auto* clip = src->findAudioClip (clipId);
    if (clip == nullptr || ! std::isfinite (newStart)) return -1;
    newStart = std::max (0.0, newStart);
    const double length = clip->lengthBeats;
    if (! std::isfinite (length) || length <= 0.0)
        return -1;

    if (fromTrack == toTrack && std::abs (clip->start - newStart) <= clipOverlapEpsilon)
        return clipId;

    roll::Track updatedDestination (*dst);
    if (! replaceAudioRange (updatedDestination, newStart, newStart + length,
                             fromTrack == toTrack ? clipId : -1))
        return -1;
    recordUndo();
    int newId = clipId;
    if (fromTrack == toTrack)
    {
        auto* movedClip = updatedDestination.findAudioClip (clipId);
        if (movedClip == nullptr)
            return -1;
        movedClip->start = newStart;
        *src = std::move (updatedDestination);
    }
    else
    {
        auto moved = src->takeAudioClip (clipId);
        if (moved == nullptr)
            return -1;
        moved->start = newStart;
        updatedDestination.putAudioClip (std::move (moved));
        newId = updatedDestination.audioClips.back()->id;
        *dst = std::move (updatedDestination);
    }
    const bool sourceWasSelected = selectedTrackId == fromTrack && selectedAudioClipId == clipId;
    const bool destinationSelectionRemoved = selectedTrackId == toTrack && selectedAudioClipId > 0
        && dst->findAudioClip (selectedAudioClipId) == nullptr;
    if (sourceWasSelected || destinationSelectionRemoved)
    {
        selectedTrackId = toTrack;
        selectedTrackIds.assign (1, toTrack);
        selectedClipId = -1;
        selectedAudioClipId = newId;
        if (onSelectionChanged) onSelectionChanged();
    }
    growTo (newStart + length + 64.0);
    if (onAudioClipsChanged) onAudioClipsChanged (fromTrack);
    if (fromTrack != toTrack && onAudioClipsChanged) onAudioClipsChanged (toTrack);
    sendChangeMessage();
    return newId;
}

void ProjectModel::notifyTrackNotes (int trackId)
{
    if (onTrackNotes) onTrackNotes (trackId);
}

void ProjectModel::setLoop (double s, double e)
{
    const double newStart = std::max (0.0, s);
    const double newEnd = std::max (newStart + 1.0 / 64.0, e);
    if (newStart == loopStart && newEnd == loopEnd)
        return;
    recordUndo();
    loopStart = newStart;
    loopEnd = newEnd;
    if (onLoopChanged) onLoopChanged();
    sendChangeMessage();
}

int ProjectModel::addLocator (double beat)
{
    if (! std::isfinite (beat) || beat < 0.0)
        return -1;

    recordUndo();
    const int id = nextLocatorId++;
    locators.push_back ({ id, beat, std::to_string (id), {} });
    std::sort (locators.begin(), locators.end(), [] (const auto& a, const auto& b)
    {
        return a.beat == b.beat ? a.id < b.id : a.beat < b.beat;
    });
    selectedLocatorId = id;
    sendChangeMessage();
    return id;
}

bool ProjectModel::moveLocator (int id, double beat)
{
    auto* marker = locator (id);
    if (marker == nullptr || ! std::isfinite (beat) || beat < 0.0)
        return false;
    if (marker->beat == beat)
        return true;

    recordUndo();
    marker->beat = beat;
    std::sort (locators.begin(), locators.end(), [] (const auto& a, const auto& b)
    {
        return a.beat == b.beat ? a.id < b.id : a.beat < b.beat;
    });
    sendChangeMessage();
    return true;
}

bool ProjectModel::renameLocator (int id, std::string name)
{
    auto* marker = locator (id);
    if (marker == nullptr)
        return false;
    if (name.empty())
        name = std::to_string (id);
    if (marker->name == name)
        return true;

    recordUndo();
    marker->name = std::move (name);
    sendChangeMessage();
    return true;
}

bool ProjectModel::setLocatorInfo (int id, std::string info)
{
    auto* marker = locator (id);
    if (marker == nullptr)
        return false;
    if (marker->info == info)
        return true;

    recordUndo();
    marker->info = std::move (info);
    sendChangeMessage();
    return true;
}

bool ProjectModel::removeLocator (int id)
{
    const auto it = std::find_if (locators.begin(), locators.end(),
                                  [id] (const auto& marker) { return marker.id == id; });
    if (it == locators.end())
        return false;

    recordUndo();
    locators.erase (it);
    if (selectedLocatorId == id)
        selectedLocatorId = -1;
    if (songStartLocatorId == id)
        songStartLocatorId = -1;
    sendChangeMessage();
    return true;
}

void ProjectModel::selectLocator (int id)
{
    if (id != -1 && locator (id) == nullptr)
        return;
    if (selectedLocatorId == id)
        return;
    selectedLocatorId = id;
    sendChangeMessage();
}

void ProjectModel::setSongStartLocator (int id)
{
    if (id != -1 && locator (id) == nullptr)
        return;
    if (songStartLocatorId == id)
        return;
    recordUndo();
    songStartLocatorId = id;
    sendChangeMessage();
}

ArrangementLocator* ProjectModel::locator (int id)
{
    const auto it = std::find_if (locators.begin(), locators.end(),
                                  [id] (const auto& marker) { return marker.id == id; });
    return it == locators.end() ? nullptr : &*it;
}

const ArrangementLocator* ProjectModel::locator (int id) const
{
    const auto it = std::find_if (locators.begin(), locators.end(),
                                  [id] (const auto& marker) { return marker.id == id; });
    return it == locators.end() ? nullptr : &*it;
}

void ProjectModel::growTo (double beats)
{
    if (beats > totalBeats) totalBeats = std::ceil (beats / 16.0) * 16.0;
}
