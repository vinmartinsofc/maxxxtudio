#include "Arrangement.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace roll
{
Track::Track (const Track& other)
    : id (other.id), name (other.name), type (other.type), volume (other.volume),
      pan (other.pan), colour (other.colour), mute (other.mute), solo (other.solo),
      collapsed (other.collapsed), frozen (other.frozen),
      freezeReversible (other.freezeReversible),
      outputRouting (other.outputRouting), parentGroupId (other.parentGroupId),
      automation (other.automation), nextId (other.nextId)
{
    if (other.frozenAudioClip != nullptr)
        frozenAudioClip = std::make_unique<AudioPlacement> (*other.frozenAudioClip);
    for (const auto& clip : other.clips)
        clips.push_back (std::make_unique<Placement> (*clip));
    for (const auto& clip : other.audioClips)
        audioClips.push_back (std::make_unique<AudioPlacement> (*clip));
}

Track& Track::operator= (const Track& other)
{
    if (this == &other)
        return *this;

    id = other.id;
    name = other.name;
    type = other.type;
    volume = other.volume;
    pan = other.pan;
    colour = other.colour;
    mute = other.mute;
    solo = other.solo;
    collapsed = other.collapsed;
    frozen = other.frozen;
    freezeReversible = other.freezeReversible;
    frozenAudioClip = other.frozenAudioClip != nullptr
        ? std::make_unique<AudioPlacement> (*other.frozenAudioClip) : nullptr;
    outputRouting = other.outputRouting;
    parentGroupId = other.parentGroupId;
    nextId = other.nextId;
    automation = other.automation;
    clips.clear();
    audioClips.clear();
    for (const auto& clip : other.clips)
        clips.push_back (std::make_unique<Placement> (*clip));
    for (const auto& clip : other.audioClips)
        audioClips.push_back (std::make_unique<AudioPlacement> (*clip));
    return *this;
}

Placement* Track::addClip (double start, double length)
{
    auto p = std::make_unique<Placement>();
    p->id = nextId++;
    p->start = start;
    p->clip.setLength (length);
    clips.push_back (std::move (p));
    return clips.back().get();
}

Placement* Track::find (int clipId) const
{
    for (const auto& c : clips) if (c->id == clipId) return c.get();
    return nullptr;
}

Placement* Track::copyClip (const Placement& source, double start)
{
    if (type != TrackType::Midi || ! std::isfinite (start) || start < 0.0
        || ! isFree (start, source.clip.length()))
        return nullptr;

    auto* copy = addClip (start, source.clip.length());
    copy->name = source.name;
    copy->enabled = source.enabled;
    copy->clip.setNotes (source.clip.notes());
    copy->clip.setGroove (source.clip.groove());
    return copy;
}

int Track::splitClip (int clipId, double beat)
{
    constexpr double eps = 1.0e-9;
    auto* clip = find (clipId);
    if (type != TrackType::Midi || clip == nullptr || ! std::isfinite (beat))
        return -1;

    const double offset = beat - clip->start;
    const double oldLength = clip->clip.length();
    const double rightLength = oldLength - offset;
    if (offset <= eps || rightLength <= eps || ! isFree (beat, rightLength, clipId))
        return -1;

    std::vector<Note> leftNotes, rightNotes;
    for (const auto& note : clip->clip.notes())
    {
        const double noteEnd = note.endBeats();
        if (note.startBeats < offset)
        {
            auto left = note;
            left.lengthBeats = std::min (noteEnd, offset) - note.startBeats;
            if (left.lengthBeats > eps)
                leftNotes.push_back (left);
        }

        if (noteEnd > offset)
        {
            auto right = note;
            const double rightStart = std::max (note.startBeats, offset);
            right.startBeats = rightStart - offset;
            right.lengthBeats = noteEnd - rightStart;
            if (right.lengthBeats > eps)
                rightNotes.push_back (right);
        }
    }

    const std::string name = clip->name;
    const bool enabled = clip->enabled;
    const auto groove = clip->clip.groove();
    clip->clip.setLength (offset);
    clip->clip.setNotes (std::move (leftNotes));
    auto* right = addClip (beat, rightLength);
    right->name = name;
    clip->enabled = enabled;
    right->enabled = enabled;
    right->clip.setNotes (std::move (rightNotes));
    right->clip.setGroove (groove);
    return right->id;
}

std::unique_ptr<Placement> Track::take (int clipId)
{
    for (auto it = clips.begin(); it != clips.end(); ++it)
        if ((*it)->id == clipId)
        {
            auto p = std::move (*it);
            clips.erase (it);
            return p;
        }
    return nullptr;
}

void Track::put (std::unique_ptr<Placement> p)
{
    p->id = nextId++;                  // New ID on the destination track.
    clips.push_back (std::move (p));
}

bool Track::remove (int clipId) { return take (clipId) != nullptr; }

bool Track::isFree (double start, double length, int ignoreId) const
{
    constexpr double eps = 1.0e-9;
    for (const auto& c : clips)
    {
        if (c->id == ignoreId) continue;
        if (start < c->end() - eps && start + length > c->start + eps) return false;
    }
    return true;
}

AudioPlacement* Track::addAudioClip (double start, double length, std::string filePath,
                                     std::string name, std::vector<WaveformRange> waveform,
                                     double durationSeconds, double sourceBpm)
{
    auto clip = std::make_unique<AudioPlacement>();
    clip->id = nextId++;
    clip->start = start;
    clip->lengthBeats = length;
    clip->durationSeconds = durationSeconds;
    clip->sourceBpm = sourceBpm;
    clip->filePath = std::move (filePath);
    clip->name = std::move (name);
    clip->waveform = std::move (waveform);
    audioClips.push_back (std::move (clip));
    return audioClips.back().get();
}

AudioPlacement* Track::findAudioClip (int clipId) const
{
    for (const auto& clip : audioClips)
        if (clip->id == clipId)
            return clip.get();
    return nullptr;
}

AudioPlacement* Track::copyAudioClip (const AudioPlacement& source, double start)
{
    if (type != TrackType::Audio || ! std::isfinite (start) || start < 0.0
        || ! isAudioFree (start, source.lengthBeats))
        return nullptr;

    auto* copy = addAudioClip (start, source.lengthBeats, source.filePath, source.name,
                               source.waveform, source.durationSeconds, source.sourceBpm);
    copy->sourceOffsetSeconds = source.sourceOffsetSeconds;
    copy->fadeInBeats = source.fadeInBeats;
    copy->fadeOutBeats = source.fadeOutBeats;
    copy->fadeInCurve = source.fadeInCurve;
    copy->fadeOutCurve = source.fadeOutCurve;
    copy->enabled = source.enabled;
    copy->warpEnabled = source.warpEnabled;
    copy->pitchSemitones = source.pitchSemitones;
    copy->pitchCents = source.pitchCents;
    copy->gainDb = source.gainDb;
    copy->warpMode = source.warpMode;
    copy->reverse = source.reverse;
    copy->warpMarkers = source.warpMarkers;
    return copy;
}

int Track::splitAudioClip (int clipId, double beat)
{
    constexpr double eps = 1.0e-9;
    auto* clip = findAudioClip (clipId);
    if (type != TrackType::Audio || clip == nullptr || ! std::isfinite (beat))
        return -1;

    const double offset = beat - clip->start;
    const double oldLength = clip->lengthBeats;
    const double rightLength = oldLength - offset;
    if (offset <= eps || rightLength <= eps || ! isAudioFree (beat, rightLength, clipId))
        return -1;

    const auto originalWarpMarkers = clip->warpMarkers;
    const double splitSourceSeconds = audioSourceSecondsAtBeat (*clip, offset);
    const double leftDuration = originalWarpMarkers.empty()
        ? clip->durationSeconds * offset / oldLength
        : std::clamp (splitSourceSeconds - clip->sourceOffsetSeconds, 0.0, clip->durationSeconds);
    const double rightDuration = clip->durationSeconds - leftDuration;
    const double sourceFraction = clip->durationSeconds > 0.0
        ? leftDuration / clip->durationSeconds : offset / oldLength;
    const double rightSourceOffset = originalWarpMarkers.empty()
        ? clip->sourceOffsetSeconds + leftDuration : splitSourceSeconds;
    std::vector<WaveformRange> leftWaveform, rightWaveform;
    if (clip->waveform.size() > 1)
    {
        const auto splitIndex = static_cast<std::size_t> (std::clamp (
            static_cast<int> (std::lround (clip->waveform.size() * sourceFraction)), 1,
            static_cast<int> (clip->waveform.size()) - 1));
        leftWaveform.assign (clip->waveform.begin(), clip->waveform.begin() + static_cast<std::ptrdiff_t> (splitIndex));
        rightWaveform.assign (clip->waveform.begin() + static_cast<std::ptrdiff_t> (splitIndex), clip->waveform.end());
    }
    else
    {
        leftWaveform = clip->waveform;
        rightWaveform = clip->waveform;
    }

    const auto filePath = clip->filePath;
    const auto name = clip->name;
    const double sourceBpm = clip->sourceBpm;
    const double sourceOffset = clip->sourceOffsetSeconds;
    const double fadeIn = clip->fadeInBeats;
    const double fadeOut = clip->fadeOutBeats;
    const double fadeInCurve = clip->fadeInCurve;
    const double fadeOutCurve = clip->fadeOutCurve;
    const bool enabled = clip->enabled;
    const bool warpEnabled = clip->warpEnabled;
    const double pitchSemitones = clip->pitchSemitones;
    const double pitchCents = clip->pitchCents;
    const double gainDb = clip->gainDb;
    const int warpMode = clip->warpMode;
    const bool reverse = clip->reverse;
    std::vector<AudioWarpMarker> leftWarpMarkers, rightWarpMarkers;
    if (! originalWarpMarkers.empty())
    {
        for (const auto& marker : originalWarpMarkers)
        {
            if (marker.beat < offset - eps)
                leftWarpMarkers.push_back (marker);
            else if (marker.beat > offset + eps)
                rightWarpMarkers.push_back ({ marker.sourceSeconds, marker.beat - offset });
            else
            {
                leftWarpMarkers.push_back ({ splitSourceSeconds, offset });
                rightWarpMarkers.push_back ({ splitSourceSeconds, 0.0 });
            }
        }
        if (leftWarpMarkers.empty() || leftWarpMarkers.back().beat < offset - eps)
            leftWarpMarkers.push_back ({ splitSourceSeconds, offset });
        if (rightWarpMarkers.empty() || rightWarpMarkers.front().beat > eps)
            rightWarpMarkers.insert (rightWarpMarkers.begin(), { splitSourceSeconds, 0.0 });
    }
    clip->lengthBeats = offset;
    clip->durationSeconds = leftDuration;
    clip->waveform = std::move (leftWaveform);
    clip->fadeInBeats = std::min (fadeIn, offset);
    clip->fadeOutBeats = std::max (0.0, fadeOut - rightLength);
    clip->fadeInCurve = fadeInCurve;
    clip->pitchSemitones = pitchSemitones;
    clip->pitchCents = pitchCents;
    clip->gainDb = gainDb;
    clip->warpMode = warpMode;
    clip->reverse = reverse;
    clip->warpMarkers = std::move (leftWarpMarkers);

    auto* right = addAudioClip (beat, rightLength, filePath, name, std::move (rightWaveform),
                                rightDuration, sourceBpm);
    right->sourceOffsetSeconds = rightSourceOffset;
    right->fadeInBeats = std::max (0.0, fadeIn - offset);
    right->fadeOutBeats = std::min (fadeOut, rightLength);
    right->fadeOutCurve = fadeOutCurve;
    right->enabled = enabled;
    right->warpEnabled = warpEnabled;
    right->pitchSemitones = pitchSemitones;
    right->pitchCents = pitchCents;
    right->gainDb = gainDb;
    right->warpMode = warpMode;
    right->reverse = reverse;
    right->warpMarkers = std::move (rightWarpMarkers);
    return right->id;
}

std::unique_ptr<AudioPlacement> Track::takeAudioClip (int clipId)
{
    for (auto it = audioClips.begin(); it != audioClips.end(); ++it)
        if ((*it)->id == clipId)
        {
            auto clip = std::move (*it);
            audioClips.erase (it);
            return clip;
        }
    return nullptr;
}

void Track::putAudioClip (std::unique_ptr<AudioPlacement> clip)
{
    clip->id = nextId++;
    audioClips.push_back (std::move (clip));
}

bool Track::removeAudioClip (int clipId)
{
    return takeAudioClip (clipId) != nullptr;
}

bool Track::isAudioFree (double start, double length, int ignoreId) const
{
    constexpr double eps = 1.0e-9;
    for (const auto& clip : audioClips)
    {
        if (clip->id == ignoreId) continue;
        if (start < clip->end() - eps && start + length > clip->start + eps) return false;
    }
    return true;
}

std::vector<Note> flatten (const Track& track)
{
    std::vector<Note> out;
    int id = 1;
    for (const auto& p : track.clips)
    {
        if (! p->enabled)
            continue;
        for (const auto& n : p->clip.notes())
        {
            if (n.startBeats >= p->clip.length()) continue;
            Note a = p->clip.groove() ? applyGroove (n, *p->clip.groove()) : n;
            a.startBeats = std::clamp (a.startBeats, 0.0, std::max (0.0, p->clip.length() - 1.0e-9));
            a.lengthBeats = std::min (n.lengthBeats, p->clip.length() - a.startBeats);
            a.startBeats += p->start;
            a.id = id++;
            out.push_back (a);
        }
    }
    return out;
}

float gainToDecibels (float gain, float minusInfinityDb)
{
    if (gain <= 0.00001f)
        return minusInfinityDb;
    return std::max (minusInfinityDb, 20.0f * std::log10 (gain));
}

float decibelsToGain (float decibels)
{
    if (decibels <= -59.9f)
        return 0.0f;
    return std::pow (10.0f, decibels / 20.0f);
}

double adaptiveGridBeat (double pixelsPerBeat, double targetSpacingPixels)
{
    if (! std::isfinite (pixelsPerBeat) || pixelsPerBeat <= 0.0
        || ! std::isfinite (targetSpacingPixels) || targetSpacingPixels <= 0.0)
        return 0.25;

    static constexpr std::array gridValues {
        1.0, 0.5, 0.25, 0.125, 0.0625,
        2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0, 4.0
    };
    const auto best = std::min_element (gridValues.begin(), gridValues.end(),
        [pixelsPerBeat, targetSpacingPixels] (double first, double second)
        {
            return std::abs (first * pixelsPerBeat - targetSpacingPixels)
                 < std::abs (second * pixelsPerBeat - targetSpacingPixels);
        });
    return *best;
}
} // namespace roll
