#include "ProjectModel.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr double clipOverlapEpsilon = 1.0e-9;

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
    editInProgress = false;

    for (const auto& track : tracks)
    {
        if (onTrackAdded)
            onTrackAdded (track->id);
        if (track->type == roll::TrackType::Audio)
        {
            if (onAudioClipsChanged)
                onAudioClipsChanged (track->id);
        }
        else if (onTrackNotes)
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

ProjectModel::Snapshot ProjectModel::makeSnapshot() const
{
    Snapshot snapshot;
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
    snapshot.selectedLocatorId = selectedLocatorId;
    snapshot.songStartLocatorId = songStartLocatorId;
    snapshot.nextLocatorId = nextLocatorId;
    return snapshot;
}

void ProjectModel::restoreSnapshot (Snapshot&& snapshot)
{
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
        if (track->type == roll::TrackType::Audio)
        {
            if (onAudioClipsChanged) onAudioClipsChanged (track->id);
        }
        else if (track->type == roll::TrackType::Midi && onTrackNotes)
            onTrackNotes (track->id);
    }
    if (onSelectionChanged) onSelectionChanged();
    if (onMixerChanged) onMixerChanged();
    if (onLoopChanged) onLoopChanged();
    sendChangeMessage();
}

void ProjectModel::recordUndo()
{
    if (! editInProgress)
    {
        undoStack.push_back (makeSnapshot());
        if (undoStack.size() > 200)
            undoStack.erase (undoStack.begin());
    }
    redoStack.clear();
}

void ProjectModel::beginEdit()
{
    if (editInProgress)
        return;
    editInProgress = true;
    undoStack.push_back (makeSnapshot());
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void ProjectModel::endEdit()
{
    editInProgress = false;
}

bool ProjectModel::undo()
{
    endEdit();
    if (undoStack.empty())
        return false;
    redoStack.push_back (makeSnapshot());
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
    undoStack.push_back (makeSnapshot());
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
            for (const auto& clip : track.audioClips)
                if (clip->filePath == path)
                    return true;
        return false;
    };

    for (const auto& track : tracks)
        for (const auto& clip : track->audioClips)
            if (clip->filePath == path)
                return true;
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
    if (tracks.size() <= 1) return;                       // sempre resta uma trilha
    const int idx = trackIndex (id);
    if (idx < 0) return;
    if (tracks[static_cast<size_t> (idx)]->type == roll::TrackType::Group)
    {
        ungroupTrack (id);
        return;
    }

    if (selectedTrackId == id)
    {
        selectedClipId = -1;
        selectedAudioClipId = -1;
        if (onSelectionChanged) onSelectionChanged();     // o piano roll solta o clip antes dele ser apagado
    }
    if (onTrackRemoved) onTrackRemoved (id);
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

void ProjectModel::setTrackSolo (int id, bool solo, bool additive)
{
    if (track (id) == nullptr)
        return;

    if (additive)
    {
        track (id)->solo = solo;
    }
    else if (solo)
    {
        const bool applyToSelection = isTrackSelected (id) && selectedTrackIds.size() > 1;
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

void ProjectModel::setTrackRouting (int trackId, roll::TrackOutputRouting routing)
{
    if (auto* t = track (trackId))
    {
        if (t->outputRouting != routing)
        {
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
    const std::vector<roll::WaveformRange>& waveform, double durationSeconds, double sourceBpm)
{
    auto* t = track (trackId);
    if (t == nullptr || t->type != roll::TrackType::Audio || ! std::isfinite (start)
        || ! std::isfinite (length) || ! std::isfinite (durationSeconds)
        || ! std::isfinite (sourceBpm) || start < 0.0 || length <= 0.0
        || filePath.empty() || durationSeconds < 0.0 || sourceBpm < 20.0 || sourceBpm > 300.0)
        return nullptr;

    roll::Track updatedTrack (*t);
    if (! replaceAudioRange (updatedTrack, start, start + length, -1))
        return nullptr;
    auto* clip = updatedTrack.addAudioClip (start, length, filePath, name, waveform,
                                            durationSeconds, sourceBpm);
    if (clip == nullptr)
        return nullptr;

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

bool ProjectModel::resizeAudioClip (int trackId, int clipId, double length)
{
    auto* t = track (trackId);
    auto* clip = t != nullptr ? t->findAudioClip (clipId) : nullptr;
    if (t == nullptr || t->type != roll::TrackType::Audio || clip == nullptr || length <= 0.0
        || ! t->isAudioFree (clip->start, length, clipId))
        return false;
    recordUndo();
    clip->lengthBeats = length;
    clip->fadeInBeats = std::min (clip->fadeInBeats, length);
    clip->fadeOutBeats = std::min (clip->fadeOutBeats, length);
    growTo (clip->end() + 64.0);
    if (onAudioClipsChanged) onAudioClipsChanged (trackId);
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
    clip->sourceBpm = sourceBpm;
    clip->lengthBeats = newLength;
    clip->fadeInBeats = std::min (clip->fadeInBeats, newLength);
    clip->fadeOutBeats = std::min (clip->fadeOutBeats, newLength);
    growTo (clip->end() + 64.0);
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
    loopStart = std::max (0.0, s);
    loopEnd = std::max (loopStart + 1.0 / 64.0, e);
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
