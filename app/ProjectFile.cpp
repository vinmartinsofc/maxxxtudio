#include "ProjectFile.h"
#include "DrumRackProcessor.h"
#include "NativeEffectProcessors.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
juce::String stringFromUtf8 (const std::string& value)
{
    return juce::String::fromUTF8 (value.c_str());
}

class ProgressReporter
{
public:
    ProgressReporter (ProjectFile::ProgressCallback callback, std::size_t total)
        : update (std::move (callback)), totalWork (std::max<std::size_t> (1, total))
    {
        report (0.0);
    }

    void completeUnit()
    {
        ++completedWork;
        const auto progress = static_cast<double> (completedWork) / static_cast<double> (totalWork);
        if (progress - lastReported >= 0.01 || completedWork >= totalWork)
            report (std::min (1.0, progress));
    }

private:
    void report (double progress)
    {
        lastReported = progress;
        if (update)
            update (progress);
    }

    ProjectFile::ProgressCallback update;
    std::size_t totalWork = 1, completedWork = 0;
    double lastReported = -1.0;
};

std::size_t trackWorkUnits (const roll::Track& track)
{
    std::size_t count = 1;
    for (const auto& envelope : track.automation)
        count += 1 + envelope.points.size();
    for (const auto& clip : track.clips)
        count += 1 + clip->clip.notes().size() + clip->clip.pianoRollLocators().size();
    count += track.audioClips.size();
    for (const auto& clip : track.audioClips)
        count += clip->warpMarkers.size();
    return count;
}

std::size_t xmlWorkUnits (const juce::XmlElement& xml)
{
    std::size_t count = 1;
    if (const auto* tracks = xml.getChildByName ("Tracks"))
    {
        for (auto* track = tracks->getFirstChildElement(); track != nullptr; track = track->getNextElement())
        {
            if (! track->hasTagName ("Track"))
                continue;
            ++count;
            for (auto* child = track->getFirstChildElement(); child != nullptr; child = child->getNextElement())
            {
                if (child->hasTagName ("MidiClip"))
                {
                    ++count;
                    for (auto* note = child->getFirstChildElement(); note != nullptr; note = note->getNextElement())
                        if (note->hasTagName ("Note") || note->hasTagName ("PianoLocator"))
                            ++count;
                }
                else if (child->hasTagName ("AudioClip"))
                {
                    ++count;
                    for (auto* marker = child->getFirstChildElement(); marker != nullptr;
                         marker = marker->getNextElement())
                        if (marker->hasTagName ("WarpMarker"))
                            ++count;
                }
                else if (child->hasTagName ("Automation"))
                {
                    for (auto* envelope = child->getFirstChildElement(); envelope != nullptr;
                         envelope = envelope->getNextElement())
                    {
                        if (! envelope->hasTagName ("Envelope"))
                            continue;
                        ++count;
                        for (auto* point = envelope->getFirstChildElement(); point != nullptr;
                             point = point->getNextElement())
                            if (point->hasTagName ("Point"))
                                ++count;
                    }
                }
            }
        }
    }
    if (const auto* locators = xml.getChildByName ("Locators"))
        for (auto* node = locators->getFirstChildElement(); node != nullptr; node = node->getNextElement())
            if (node->hasTagName ("Locator"))
                ++count;
    if (const auto* mappings = xml.getChildByName ("MidiMappings"))
        for (auto* node = mappings->getFirstChildElement(); node != nullptr; node = node->getNextElement())
            if (node->hasTagName ("Mapping"))
                ++count;
    if (const auto* plugins = xml.getChildByName ("Plugins"))
        for (auto* node = plugins->getFirstChildElement(); node != nullptr; node = node->getNextElement())
            if (node->hasTagName ("Plugin"))
                ++count;
    return count;
}

juce::String collectAudioAsset (const juce::String& sourcePath,
                                const juce::File& projectFolder, const juce::File& assetFolder,
                                std::map<std::string, juce::String>& copiedAssets,
                                std::set<std::string>& assetNames)
{
    const auto key = sourcePath.toStdString();
    if (const auto found = copiedAssets.find (key); found != copiedAssets.end())
        return found->second;

    const juce::File source (sourcePath);
    const auto baseName = source.getFileNameWithoutExtension();
    const auto extension = source.getFileExtension();
    auto fileName = source.getFileName();
    int suffix = 2;
    while (assetNames.contains (fileName.toLowerCase().toStdString()))
        fileName = baseName + "_" + juce::String (suffix++) + extension;
    const auto target = assetFolder.getChildFile (fileName);
    if (! source.existsAsFile()
        || (source.getFullPathName().compareIgnoreCase (target.getFullPathName()) != 0
            && ! source.copyFileTo (target)))
        throw std::runtime_error ("Unable to collect audio file: " + source.getFullPathName().toStdString());

    assetNames.insert (fileName.toLowerCase().toStdString());
    const auto relativePath = target.getRelativePathFrom (projectFolder);
    copiedAssets.emplace (key, relativePath);
    return relativePath;
}

juce::String xmlStringFromBlock (const juce::MemoryBlock& block)
{
    return block.getSize() > 0
        ? juce::String::fromUTF8 (static_cast<const char*> (block.getData()),
                                  static_cast<int> (block.getSize()))
        : juce::String();
}

void storeXmlInBlock (const juce::XmlElement& xml, juce::MemoryBlock& block)
{
    juce::MemoryOutputStream output;
    xml.writeTo (output);
    block.replaceWith (output.getData(), output.getDataSize());
}

void writeTrack (juce::XmlElement& parent, const roll::Track& track,
                 const juce::File& projectFolder, const juce::File& assetFolder,
                 std::map<std::string, juce::String>& copiedAssets,
                 std::set<std::string>& assetNames, ProgressReporter& progress)
{
    auto* node = parent.createNewChildElement ("Track");
    node->setAttribute ("id", track.id);
    node->setAttribute ("name", stringFromUtf8 (track.name));
    node->setAttribute ("type", track.type == roll::TrackType::Audio ? "audio"
                                      : track.type == roll::TrackType::Group ? "group" : "midi");
    node->setAttribute ("parentGroupId", track.parentGroupId);
    node->setAttribute ("colour", juce::String (static_cast<juce::int64> (track.colour)));
    node->setAttribute ("volume", track.volume);
    node->setAttribute ("pan", track.pan);
    node->setAttribute ("mute", track.mute);
    node->setAttribute ("solo", track.solo);
    node->setAttribute ("collapsed", track.collapsed);
    node->setAttribute ("routing", track.outputRouting == roll::TrackOutputRouting::ExtOut ? "external" : "master");

    for (const auto& envelope : track.automation)
    {
        if (! roll::isValidAutomationEnvelope (envelope))
            throw std::runtime_error ("A track contains an invalid automation envelope.");

        auto* envelopeNode = node->createNewChildElement ("Automation");
        auto* targetNode = envelopeNode->createNewChildElement ("Target");
        switch (envelope.target.type)
        {
            case roll::AutomationParameterType::Volume: targetNode->setAttribute ("type", "volume"); break;
            case roll::AutomationParameterType::Pan: targetNode->setAttribute ("type", "pan"); break;
            case roll::AutomationParameterType::Mute: targetNode->setAttribute ("type", "mute"); break;
            case roll::AutomationParameterType::PluginParameter: targetNode->setAttribute ("type", "plugin"); break;
            case roll::AutomationParameterType::AudioClipGain: targetNode->setAttribute ("type", "audioGain"); break;
            case roll::AutomationParameterType::AudioClipPitch: targetNode->setAttribute ("type", "audioPitch"); break;
            case roll::AutomationParameterType::AudioClipFadeIn: targetNode->setAttribute ("type", "audioFadeIn"); break;
            case roll::AutomationParameterType::AudioClipFadeOut: targetNode->setAttribute ("type", "audioFadeOut"); break;
        }
        targetNode->setAttribute ("pluginIdentifier", stringFromUtf8 (envelope.target.pluginIdentifier));
        targetNode->setAttribute ("parameterId", stringFromUtf8 (envelope.target.parameterId));
        targetNode->setAttribute ("parameterName", stringFromUtf8 (envelope.target.parameterName));
        targetNode->setAttribute ("parameterIndex", envelope.target.parameterIndex);
        targetNode->setAttribute ("deviceIndex", envelope.target.deviceIndex);
        targetNode->setAttribute ("clipId", envelope.target.clipId);
        targetNode->setAttribute ("currentValue", envelope.target.currentValue);
        for (const auto& point : envelope.points)
        {
            auto* pointNode = envelopeNode->createNewChildElement ("Point");
            pointNode->setAttribute ("beat", point.beat);
            pointNode->setAttribute ("value", point.value);
            progress.completeUnit();
        }
        progress.completeUnit();
    }

    for (const auto& clip : track.clips)
    {
        auto* clipNode = node->createNewChildElement ("MidiClip");
        clipNode->setAttribute ("id", clip->id);
        clipNode->setAttribute ("start", clip->start);
        clipNode->setAttribute ("length", clip->clip.length());
        clipNode->setAttribute ("enabled", clip->enabled);
        clipNode->setAttribute ("name", stringFromUtf8 (clip->name));
        clipNode->setAttribute ("selectedPianoLocatorId", clip->clip.selectedPianoRollLocatorId());
        if (clip->clip.groove())
        {
            const auto& groove = *clip->clip.groove();
            if (! groove.isValid())
                throw std::runtime_error ("A MIDI clip contains an invalid groove.");

            auto* grooveNode = clipNode->createNewChildElement ("Groove");
            grooveNode->setAttribute ("name", stringFromUtf8 (groove.name));
            grooveNode->setAttribute ("gridBeats", groove.gridBeats);
            grooveNode->setAttribute ("timingAmount", groove.timingAmount);
            grooveNode->setAttribute ("velocityAmount", groove.velocityAmount);
            for (const auto offset : groove.timingOffsets)
            {
                auto* offsetNode = grooveNode->createNewChildElement ("Offset");
                offsetNode->setAttribute ("beats", offset);
            }
            for (const auto multiplier : groove.velocityMultipliers)
            {
                auto* velocityNode = grooveNode->createNewChildElement ("Velocity");
                velocityNode->setAttribute ("multiplier", multiplier);
            }
        }
        for (const auto& note : clip->clip.notes())
        {
            auto* noteNode = clipNode->createNewChildElement ("Note");
            noteNode->setAttribute ("id", note.id);
            noteNode->setAttribute ("pitch", note.pitch);
            noteNode->setAttribute ("start", note.startBeats);
            noteNode->setAttribute ("length", note.lengthBeats);
            noteNode->setAttribute ("velocity", note.velocity);
            noteNode->setAttribute ("muted", note.muted);
            progress.completeUnit();
        }
        for (const auto& marker : clip->clip.pianoRollLocators())
        {
            auto* markerNode = clipNode->createNewChildElement ("PianoLocator");
            markerNode->setAttribute ("id", marker.id);
            markerNode->setAttribute ("beat", marker.beat);
            progress.completeUnit();
        }
        progress.completeUnit();
    }

    for (const auto& clip : track.audioClips)
    {
        if (! roll::isValidAudioWarpMarkers (clip->warpMarkers, clip->sourceOffsetSeconds,
                                              clip->durationSeconds, clip->lengthBeats))
            throw std::runtime_error ("An audio clip contains invalid warp markers.");
        const auto sourcePath = clip->filePath;
        const auto asset = collectAudioAsset (stringFromUtf8 (sourcePath), projectFolder, assetFolder,
                                              copiedAssets, assetNames);

        auto* audioNode = node->createNewChildElement ("AudioClip");
        audioNode->setAttribute ("id", clip->id);
        audioNode->setAttribute ("start", clip->start);
        audioNode->setAttribute ("length", clip->lengthBeats);
        audioNode->setAttribute ("duration", clip->durationSeconds);
        audioNode->setAttribute ("sourceOffset", clip->sourceOffsetSeconds);
        audioNode->setAttribute ("fadeIn", clip->fadeInBeats);
        audioNode->setAttribute ("fadeOut", clip->fadeOutBeats);
        audioNode->setAttribute ("fadeInCurve", clip->fadeInCurve);
        audioNode->setAttribute ("fadeOutCurve", clip->fadeOutCurve);
        audioNode->setAttribute ("sourceBpm", clip->sourceBpm);
        audioNode->setAttribute ("enabled", clip->enabled);
        audioNode->setAttribute ("warpEnabled", clip->warpEnabled);
        audioNode->setAttribute ("pitchSemitones", clip->pitchSemitones);
        audioNode->setAttribute ("pitchCents", clip->pitchCents);
        audioNode->setAttribute ("gainDb", clip->gainDb);
        audioNode->setAttribute ("warpMode", clip->warpMode);
        audioNode->setAttribute ("reverse", clip->reverse);
        audioNode->setAttribute ("name", stringFromUtf8 (clip->name));
        audioNode->setAttribute ("file", asset);
        for (const auto& marker : clip->warpMarkers)
        {
            auto* markerNode = audioNode->createNewChildElement ("WarpMarker");
            markerNode->setAttribute ("sourceSeconds", marker.sourceSeconds);
            markerNode->setAttribute ("beat", marker.beat);
            progress.completeUnit();
        }
        progress.completeUnit();
    }
    progress.completeUnit();
}

bool validNumber (double value)
{
    return std::isfinite (value);
}

bool readTrack (const juce::XmlElement& node, roll::Track& track,
                const juce::File& projectFolder, std::set<int>& trackIds,
                std::vector<juce::File>& missingAudioFiles, ProgressReporter& progress)
{
    track.id = node.getIntAttribute ("id", -1);
    const auto type = node.getStringAttribute ("type");
    if (track.id < 0 || ! trackIds.insert (track.id).second
        || (type != "midi" && type != "audio" && type != "group"))
        return false;

    track.name = node.getStringAttribute ("name").toStdString();
    track.type = type == "audio" ? roll::TrackType::Audio
               : type == "group" ? roll::TrackType::Group : roll::TrackType::Midi;
    track.parentGroupId = node.getIntAttribute ("parentGroupId", 0);
    track.colour = static_cast<std::uint32_t> (
        node.getStringAttribute ("colour", "0").getLargeIntValue());
    track.volume = node.getDoubleAttribute ("volume", 1.0);
    track.pan = node.getDoubleAttribute ("pan", 0.0);
    track.mute = node.getBoolAttribute ("mute", false);
    track.solo = node.getBoolAttribute ("solo", false);
    track.collapsed = node.getBoolAttribute ("collapsed", false);
    track.outputRouting = node.getStringAttribute ("routing") == "external"
        ? roll::TrackOutputRouting::ExtOut : roll::TrackOutputRouting::Master;
    if (track.parentGroupId < 0 || ! validNumber (track.volume) || track.volume < 0.0 || track.volume > 1.5
        || ! validNumber (track.pan) || track.pan < -1.0 || track.pan > 1.0)
        return false;

    std::set<int> clipIds;
    for (auto* child = node.getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->hasTagName ("Automation"))
        {
            auto* targetNode = child->getChildByName ("Target");
            if (targetNode == nullptr)
                return false;

            roll::AutomationEnvelope envelope;
            const auto targetType = targetNode->getStringAttribute ("type");
            if (targetType == "volume")
                envelope.target.type = roll::AutomationParameterType::Volume;
            else if (targetType == "pan")
                envelope.target.type = roll::AutomationParameterType::Pan;
            else if (targetType == "mute")
                envelope.target.type = roll::AutomationParameterType::Mute;
            else if (targetType == "plugin")
                envelope.target.type = roll::AutomationParameterType::PluginParameter;
            else if (targetType == "audioGain")
                envelope.target.type = roll::AutomationParameterType::AudioClipGain;
            else if (targetType == "audioPitch")
                envelope.target.type = roll::AutomationParameterType::AudioClipPitch;
            else if (targetType == "audioFadeIn")
                envelope.target.type = roll::AutomationParameterType::AudioClipFadeIn;
            else if (targetType == "audioFadeOut")
                envelope.target.type = roll::AutomationParameterType::AudioClipFadeOut;
            else
                return false;

            envelope.target.pluginIdentifier = targetNode->getStringAttribute ("pluginIdentifier").toStdString();
            envelope.target.parameterId = targetNode->getStringAttribute ("parameterId").toStdString();
            envelope.target.parameterName = targetNode->getStringAttribute ("parameterName").toStdString();
            envelope.target.parameterIndex = targetNode->getIntAttribute ("parameterIndex", -1);
            envelope.target.deviceIndex = targetNode->getIntAttribute ("deviceIndex", -1);
            envelope.target.clipId = targetNode->getIntAttribute ("clipId", -1);
            envelope.target.currentValue = targetNode->getDoubleAttribute ("currentValue", 0.0);
            for (auto* pointNode = child->getFirstChildElement(); pointNode != nullptr;
                 pointNode = pointNode->getNextElement())
            {
                if (! pointNode->hasTagName ("Point"))
                    continue;
                envelope.points.push_back ({ pointNode->getDoubleAttribute ("beat", -1.0),
                                             pointNode->getDoubleAttribute ("value", 0.0) });
                progress.completeUnit();
            }
            if (! roll::isValidAutomationEnvelope (envelope)
                || std::any_of (track.automation.begin(), track.automation.end(),
                    [&envelope] (const auto& existing) { return existing.target == envelope.target; }))
                return false;
            track.automation.push_back (std::move (envelope));
            progress.completeUnit();
        }
        else if (child->hasTagName ("MidiClip"))
        {
            const int id = child->getIntAttribute ("id", -1);
            const double start = child->getDoubleAttribute ("start", -1.0);
            const double length = child->getDoubleAttribute ("length", 0.0);
            if (track.type != roll::TrackType::Midi || id < 1 || ! clipIds.insert (id).second
                || ! validNumber (start) || start < 0.0 || ! validNumber (length) || length <= 0.0)
                return false;

            auto* clip = track.addClip (start, length);
            clip->id = id;
            track.reserveClipId (id);
            clip->enabled = child->getBoolAttribute ("enabled", true);
            clip->name = child->getStringAttribute ("name").toStdString();
            bool foundGroove = false;
            std::vector<roll::Note> notes;
            std::set<int> noteIds;
            std::vector<roll::PianoRollLocator> pianoLocators;
            std::set<int> pianoLocatorIds;
            for (auto* noteNode = child->getFirstChildElement(); noteNode != nullptr; noteNode = noteNode->getNextElement())
            {
                if (noteNode->hasTagName ("Groove"))
                {
                    if (foundGroove)
                        return false;
                    foundGroove = true;

                    roll::GrooveTemplate groove;
                    groove.name = noteNode->getStringAttribute ("name").toStdString();
                    groove.gridBeats = noteNode->getDoubleAttribute ("gridBeats", 0.0);
                    groove.timingAmount = noteNode->getDoubleAttribute ("timingAmount", 1.0);
                    groove.velocityAmount = noteNode->getDoubleAttribute ("velocityAmount", 1.0);
                    for (auto* value = noteNode->getFirstChildElement(); value != nullptr;
                         value = value->getNextElement())
                    {
                        if (value->hasTagName ("Offset"))
                            groove.timingOffsets.push_back (value->getDoubleAttribute ("beats",
                                std::numeric_limits<double>::quiet_NaN()));
                        else if (value->hasTagName ("Velocity"))
                            groove.velocityMultipliers.push_back (value->getDoubleAttribute ("multiplier",
                                std::numeric_limits<double>::quiet_NaN()));
                    }
                    if (! groove.isValid())
                        return false;
                    clip->clip.setGroove (std::move (groove));
                    continue;
                }

                if (noteNode->hasTagName ("PianoLocator"))
                {
                    roll::PianoRollLocator marker;
                    marker.id = noteNode->getIntAttribute ("id", -1);
                    marker.beat = noteNode->getDoubleAttribute ("beat",
                        std::numeric_limits<double>::quiet_NaN());
                    if (marker.id < 1 || ! pianoLocatorIds.insert (marker.id).second
                        || ! validNumber (marker.beat) || marker.beat < 0.0)
                        return false;
                    pianoLocators.push_back (marker);
                    progress.completeUnit();
                    continue;
                }

                if (! noteNode->hasTagName ("Note"))
                    continue;
                roll::Note note;
                note.id = noteNode->getIntAttribute ("id", -1);
                note.pitch = noteNode->getIntAttribute ("pitch", -1);
                note.startBeats = noteNode->getDoubleAttribute ("start", -1.0);
                note.lengthBeats = noteNode->getDoubleAttribute ("length", 0.0);
                note.velocity = noteNode->getIntAttribute ("velocity", 0);
                note.muted = noteNode->getBoolAttribute ("muted", false);
                if (note.id < 1 || ! noteIds.insert (note.id).second
                    || note.pitch < 0 || note.pitch > 127
                    || ! validNumber (note.startBeats) || note.startBeats < 0.0
                    || ! validNumber (note.lengthBeats) || note.lengthBeats <= 0.0
                    || note.velocity < 1 || note.velocity > 127)
                    return false;
                notes.push_back (note);
                progress.completeUnit();
            }
            clip->clip.setNotes (std::move (notes));
            clip->clip.setPianoRollLocators (
                std::move (pianoLocators), child->getIntAttribute ("selectedPianoLocatorId", -1));
            if (clip->clip.selectedPianoRollLocatorId() != child->getIntAttribute ("selectedPianoLocatorId", -1))
                return false;
            progress.completeUnit();
        }
        else if (child->hasTagName ("AudioClip"))
        {
            const int id = child->getIntAttribute ("id", -1);
            const double start = child->getDoubleAttribute ("start", -1.0);
            const double length = child->getDoubleAttribute ("length", 0.0);
            const auto relativePath = child->getStringAttribute ("file");
            if (track.type != roll::TrackType::Audio || id < 1 || ! clipIds.insert (id).second
                || ! validNumber (start) || start < 0.0 || ! validNumber (length) || length <= 0.0
                || relativePath.isEmpty() || juce::File::isAbsolutePath (relativePath))
                return false;

            const auto source = projectFolder.getChildFile (relativePath);
            if (! source.isAChildOf (projectFolder))
                return false;
            const double duration = child->getDoubleAttribute ("duration", 0.0);
            const double sourceOffset = child->getDoubleAttribute ("sourceOffset", 0.0);
            const double fadeIn = child->getDoubleAttribute ("fadeIn", 0.0);
            const double fadeOut = child->getDoubleAttribute ("fadeOut", 0.0);
            const double fadeInCurve = child->getDoubleAttribute ("fadeInCurve", 0.0);
            const double fadeOutCurve = child->getDoubleAttribute ("fadeOutCurve", 0.0);
            const double sourceBpm = child->getDoubleAttribute ("sourceBpm", 120.0);
            if (! validNumber (duration) || duration < 0.0 || ! validNumber (sourceOffset) || sourceOffset < 0.0
                || ! validNumber (fadeIn) || fadeIn < 0.0 || ! validNumber (fadeOut) || fadeOut < 0.0
                || ! validNumber (fadeInCurve) || fadeInCurve < -1.0 || fadeInCurve > 1.0
                || ! validNumber (fadeOutCurve) || fadeOutCurve < -1.0 || fadeOutCurve > 1.0
                || ! validNumber (sourceBpm) || sourceBpm < 20.0 || sourceBpm > 300.0)
                return false;

            auto* clip = track.addAudioClip (start, length, source.getFullPathName().toStdString(),
                                             child->getStringAttribute ("name").toStdString(), {},
                                             duration, sourceBpm);
            clip->id = id;
            track.reserveClipId (id);
            clip->sourceOffsetSeconds = sourceOffset;
            clip->fadeInBeats = fadeIn;
            clip->fadeOutBeats = fadeOut;
            clip->fadeInCurve = fadeInCurve;
            clip->fadeOutCurve = fadeOutCurve;
            clip->enabled = child->getBoolAttribute ("enabled", true);
            clip->warpEnabled = child->getBoolAttribute ("warpEnabled", true);
            clip->pitchSemitones = child->getDoubleAttribute ("pitchSemitones", 0.0);
            clip->pitchCents = child->getDoubleAttribute ("pitchCents", 0.0);
            clip->gainDb = child->getDoubleAttribute ("gainDb", 0.0);
            clip->warpMode = child->getIntAttribute ("warpMode", 0);
            clip->reverse = child->getBoolAttribute ("reverse", false);
            for (auto* markerNode = child->getFirstChildElement(); markerNode != nullptr;
                 markerNode = markerNode->getNextElement())
            {
                if (! markerNode->hasTagName ("WarpMarker"))
                    continue;
                clip->warpMarkers.push_back ({
                    markerNode->getDoubleAttribute ("sourceSeconds",
                        std::numeric_limits<double>::quiet_NaN()),
                    markerNode->getDoubleAttribute ("beat",
                        std::numeric_limits<double>::quiet_NaN())
                });
                progress.completeUnit();
            }
            if (! roll::isValidAudioWarpMarkers (clip->warpMarkers, clip->sourceOffsetSeconds,
                                                  clip->durationSeconds, clip->lengthBeats))
                return false;
            if (! source.existsAsFile())
                missingAudioFiles.push_back (source);
            progress.completeUnit();
        }
    }
    progress.completeUnit();
    return true;
}
}

juce::Result ProjectFile::save (const juce::File& file, const ProjectFileData& data,
                                ProgressCallback onProgress)
{
    try
    {
        if (file.getFileName().isEmpty() || ! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("Unable to create the project folder.");

        const auto projectFolder = file.getParentDirectory();
        const auto assetFolder = projectFolder.getChildFile (file.getFileNameWithoutExtension() + "_Files")
                                             .getChildFile ("Samples").getChildFile ("Imported");
        if (! assetFolder.createDirectory())
            return juce::Result::fail ("Unable to create the project audio folder.");

        const auto& state = data.project;
        std::size_t totalWork = 1 + trackWorkUnits (state.master) + state.locators.size();
        for (const auto& track : state.tracks)
            totalWork += trackWorkUnits (track);
        totalWork += state.midiMappings.size();
        totalWork += data.plugins.size();
        ProgressReporter progress (std::move (onProgress), totalWork);

        auto xml = std::make_unique<juce::XmlElement> ("ProjectFreedom");
        xml->setAttribute ("version", 1);
        xml->setAttribute ("bpm", data.bpm);
        xml->setAttribute ("transportPosition", data.transportPosition);
        xml->setAttribute ("loopEnabled", data.loopEnabled);
        xml->setAttribute ("metronomeEnabled", data.metronomeEnabled);
        xml->setAttribute ("metronomeGain", data.metronomeGain);
        xml->setAttribute ("loopStart", state.loopStart);
        xml->setAttribute ("loopEnd", state.loopEnd);
        xml->setAttribute ("totalBeats", state.totalBeats);
        xml->setAttribute ("selectedTrackId", state.selectedTrackId);
        xml->setAttribute ("selectedClipId", state.selectedClipId);
        xml->setAttribute ("selectedAudioClipId", state.selectedAudioClipId);
        xml->setAttribute ("selectedLocatorId", state.selectedLocatorId);
        xml->setAttribute ("songStartLocatorId", state.songStartLocatorId);

        auto* tracksNode = xml->createNewChildElement ("Tracks");
        std::map<std::string, juce::String> copiedAssets;
        std::set<std::string> assetNames;
        writeTrack (*tracksNode, state.master, projectFolder, assetFolder, copiedAssets, assetNames, progress);
        for (const auto& track : state.tracks)
            writeTrack (*tracksNode, track, projectFolder, assetFolder, copiedAssets, assetNames, progress);

        auto* locatorsNode = xml->createNewChildElement ("Locators");
        for (const auto& marker : state.locators)
        {
            auto* node = locatorsNode->createNewChildElement ("Locator");
            node->setAttribute ("id", marker.id);
            node->setAttribute ("beat", marker.beat);
            node->setAttribute ("name", stringFromUtf8 (marker.name));
            node->setAttribute ("info", stringFromUtf8 (marker.info));
            progress.completeUnit();
        }

        auto* midiMappingsNode = xml->createNewChildElement ("MidiMappings");
        for (const auto& mapping : state.midiMappings)
        {
            auto* node = midiMappingsNode->createNewChildElement ("Mapping");
            node->setAttribute ("trackId", mapping.trackId);
            node->setAttribute ("target", static_cast<int> (mapping.target));
            node->setAttribute ("messageType", static_cast<int> (mapping.messageType));
            node->setAttribute ("channel", mapping.channel);
            node->setAttribute ("number", mapping.number);
            progress.completeUnit();
        }

        auto* pluginsNode = xml->createNewChildElement ("Plugins");
        for (const auto& plugin : data.plugins)
        {
            auto* pluginNode = pluginsNode->createNewChildElement ("Plugin");
            pluginNode->setAttribute ("trackId", plugin.trackId);
            pluginNode->setAttribute ("order", plugin.order);
            pluginNode->setAttribute ("identifier", plugin.identifier);
            pluginNode->setAttribute ("bypassed", plugin.bypassed);
            pluginNode->setAttribute ("groupId", plugin.groupId);
            auto pluginState = plugin.state;
            if (plugin.identifier == DrumRackProcessor::pluginIdentifier && pluginState.getSize() > 0)
            {
                auto stateXml = juce::XmlDocument::parse (xmlStringFromBlock (pluginState));
                if (stateXml == nullptr || ! stateXml->hasTagName ("DrumRack"))
                    throw std::runtime_error ("The Drum Rack state is invalid.");
                for (auto* pad = stateXml->getFirstChildElement(); pad != nullptr; pad = pad->getNextElement())
                {
                    if (! pad->hasTagName ("Pad"))
                        continue;
                    const auto path = pad->getStringAttribute ("file");
                    if (path.isNotEmpty())
                        pad->setAttribute ("file", collectAudioAsset (path, projectFolder, assetFolder,
                                                                       copiedAssets, assetNames));
                }
                storeXmlInBlock (*stateXml, pluginState);
            }
            pluginNode->setAttribute ("state", pluginState.toBase64Encoding());
            progress.completeUnit();
        }

        if (! xml->writeTo (file))
            return juce::Result::fail ("Unable to write the project file.");
        progress.completeUnit();
        return juce::Result::ok();
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail (e.what());
    }
}

juce::File ProjectFile::recoveryDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ProjectFreedom").getChildFile ("autosave");
}

std::vector<juce::File> ProjectFile::recoveryFiles (const juce::File& directory)
{
    juce::Array<juce::File> files;
    directory.findChildFiles (files, juce::File::findFiles, false, "Recovery-*.pfs");
    std::sort (files.begin(), files.end(), [] (const juce::File& left, const juce::File& right)
    {
        const auto leftTime = left.getLastModificationTime();
        const auto rightTime = right.getLastModificationTime();
        return leftTime == rightTime ? left.getFileName() > right.getFileName() : leftTime > rightTime;
    });

    return { files.begin(), files.end() };
}

juce::Result ProjectFile::saveRecovery (const juce::File& directory, const ProjectFileData& data)
{
    if (! directory.createDirectory())
        return juce::Result::fail ("Unable to create the recovery folder.");

    static std::atomic<std::uint64_t> recoverySequence { 0 };
    const auto baseName = "Recovery-" + juce::String (juce::Time::getCurrentTime().toMilliseconds())
                        + "-" + juce::String (recoverySequence.fetch_add (1, std::memory_order_relaxed));
    const auto temporaryFile = directory.getChildFile (baseName + ".tmp");
    const auto recoveryFile = directory.getChildFile (baseName + ".pfs");
    const auto assetFolder = directory.getChildFile (baseName + "_Files");

    const auto result = save (temporaryFile, data);
    if (result.failed())
    {
        temporaryFile.deleteFile();
        assetFolder.deleteRecursively();
        return result;
    }

    if (! temporaryFile.moveFileTo (recoveryFile))
    {
        temporaryFile.deleteFile();
        assetFolder.deleteRecursively();
        return juce::Result::fail ("Unable to publish the recovery snapshot.");
    }

    auto completedFiles = recoveryFiles (directory);
    for (std::size_t index = 2; index < completedFiles.size(); ++index)
    {
        completedFiles[index].deleteFile();
        directory.getChildFile (completedFiles[index].getFileNameWithoutExtension() + "_Files")
            .deleteRecursively();
    }
    clearIncompleteRecoveryFiles (directory);
    return juce::Result::ok();
}

void ProjectFile::clearIncompleteRecoveryFiles (const juce::File& directory)
{
    juce::Array<juce::File> temporaryFiles;
    directory.findChildFiles (temporaryFiles, juce::File::findFiles, false, "Recovery-*.tmp");
    for (const auto& file : temporaryFiles)
    {
        file.deleteFile();
        directory.getChildFile (file.getFileNameWithoutExtension() + "_Files").deleteRecursively();
    }
}

void ProjectFile::clearRecoveryFiles (const juce::File& directory)
{
    juce::Array<juce::File> files;
    directory.findChildFiles (files, juce::File::findFiles, false, "Recovery-*.pfs");
    for (const auto& file : files)
    {
        file.deleteFile();
        directory.getChildFile (file.getFileNameWithoutExtension() + "_Files").deleteRecursively();
    }
    clearIncompleteRecoveryFiles (directory);
}

juce::Result ProjectFile::load (const juce::File& file, ProjectFileData& data,
                                ProgressCallback onProgress)
{
    if (onProgress)
        onProgress (-1.0);
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("ProjectFreedom")
        || xml->getIntAttribute ("version", 0) != 1)
        return juce::Result::fail ("Invalid or unsupported project file.");

    ProjectFileData loaded;
    loaded.bpm = xml->getDoubleAttribute ("bpm", 120.0);
    loaded.transportPosition = xml->getDoubleAttribute ("transportPosition", 0.0);
    loaded.loopEnabled = xml->getBoolAttribute ("loopEnabled", true);
    loaded.metronomeEnabled = xml->getBoolAttribute ("metronomeEnabled", false);
    loaded.metronomeGain = static_cast<float> (xml->getDoubleAttribute ("metronomeGain", 1.0));
    loaded.project.loopStart = xml->getDoubleAttribute ("loopStart", 0.0);
    loaded.project.loopEnd = xml->getDoubleAttribute ("loopEnd", 16.0);
    loaded.project.totalBeats = xml->getDoubleAttribute ("totalBeats", 256.0);
    loaded.project.selectedTrackId = xml->getIntAttribute ("selectedTrackId", -1);
    loaded.project.selectedClipId = xml->getIntAttribute ("selectedClipId", -1);
    loaded.project.selectedAudioClipId = xml->getIntAttribute ("selectedAudioClipId", -1);
    loaded.project.selectedLocatorId = xml->getIntAttribute ("selectedLocatorId", -1);
    loaded.project.songStartLocatorId = xml->getIntAttribute ("songStartLocatorId", -1);
    if (! validNumber (loaded.bpm) || loaded.bpm < 20.0 || loaded.bpm > 999.0
        || ! validNumber (loaded.transportPosition) || loaded.transportPosition < 0.0
        || ! validNumber (loaded.project.loopStart) || loaded.project.loopStart < 0.0
        || ! validNumber (loaded.project.loopEnd) || loaded.project.loopEnd <= loaded.project.loopStart
        || ! validNumber (loaded.project.totalBeats) || loaded.project.totalBeats < 16.0
        || ! std::isfinite (loaded.metronomeGain) || loaded.metronomeGain < 0.0f || loaded.metronomeGain > 1.0f)
        return juce::Result::fail ("Project settings are invalid.");

    auto* tracksNode = xml->getChildByName ("Tracks");
    if (tracksNode == nullptr)
        return juce::Result::fail ("The project has no tracks.");
    ProgressReporter progress (std::move (onProgress), xmlWorkUnits (*xml));
    progress.completeUnit();
    std::set<int> trackIds;
    bool foundMaster = false;
    for (auto* node = tracksNode->getFirstChildElement(); node != nullptr; node = node->getNextElement())
    {
        if (! node->hasTagName ("Track"))
            continue;
        roll::Track track;
        if (! readTrack (*node, track, file.getParentDirectory(), trackIds,
                         loaded.missingAudioFiles, progress))
            return juce::Result::fail ("A track or clip contains invalid data.");
        if (track.id == AudioEngine::masterTrackId)
        {
            if (foundMaster)
                return juce::Result::fail ("The project contains more than one master track.");
            loaded.project.master = std::move (track);
            foundMaster = true;
        }
        else
            loaded.project.tracks.push_back (std::move (track));
    }
    if (! foundMaster || loaded.project.tracks.empty())
        return juce::Result::fail ("The project is missing its master or arrangement tracks.");

    for (const auto& track : loaded.project.tracks)
    {
        if (track.type == roll::TrackType::Group && track.parentGroupId != 0)
            return juce::Result::fail ("Groups cannot be nested.");
        if (track.parentGroupId != 0)
        {
            const auto group = std::find_if (loaded.project.tracks.begin(), loaded.project.tracks.end(),
                [&track] (const auto& candidate) { return candidate.id == track.parentGroupId; });
            if (group == loaded.project.tracks.end() || group->type != roll::TrackType::Group)
                return juce::Result::fail ("A track references a missing group.");
        }
    }

    std::set<std::pair<int, int>> mappingTargets;
    if (auto* mappingsNode = xml->getChildByName ("MidiMappings"))
    {
        for (auto* node = mappingsNode->getFirstChildElement(); node != nullptr; node = node->getNextElement())
        {
            if (! node->hasTagName ("Mapping"))
                continue;

            MidiMapping mapping;
            mapping.trackId = node->getIntAttribute ("trackId", -1);
            const int target = node->getIntAttribute ("target", -1);
            const int messageType = node->getIntAttribute ("messageType", -1);
            mapping.channel = node->getIntAttribute ("channel", -1);
            mapping.number = node->getIntAttribute ("number", -1);
            if (target < static_cast<int> (MidiMappingTarget::TrackVolume)
                || target > static_cast<int> (MidiMappingTarget::MasterPan)
                || messageType < static_cast<int> (MidiMappingMessageType::ControlChange)
                || messageType > static_cast<int> (MidiMappingMessageType::Note)
                || mapping.channel < 1 || mapping.channel > 16
                || mapping.number < 0 || mapping.number > 127)
                return juce::Result::fail ("A MIDI mapping contains invalid data.");

            mapping.target = static_cast<MidiMappingTarget> (target);
            mapping.messageType = static_cast<MidiMappingMessageType> (messageType);
            const bool masterTarget = mapping.target == MidiMappingTarget::MasterVolume
                                   || mapping.target == MidiMappingTarget::MasterPan;
            const bool buttonTarget = mapping.target == MidiMappingTarget::TrackMute
                                   || mapping.target == MidiMappingTarget::TrackSolo;
            if ((masterTarget && mapping.trackId != AudioEngine::masterTrackId)
                || (! masterTarget && (mapping.trackId <= AudioEngine::masterTrackId
                                       || ! trackIds.contains (mapping.trackId)))
                || (mapping.messageType == MidiMappingMessageType::Note && ! buttonTarget)
                || ! mappingTargets.emplace (mapping.trackId, target).second)
                return juce::Result::fail ("A MIDI mapping references an invalid or duplicate control.");

            loaded.project.midiMappings.push_back (mapping);
            progress.completeUnit();
        }
    }

    std::set<int> locatorIds;
    if (auto* locatorsNode = xml->getChildByName ("Locators"))
    {
        for (auto* node = locatorsNode->getFirstChildElement(); node != nullptr; node = node->getNextElement())
        {
            if (! node->hasTagName ("Locator"))
                continue;
            ArrangementLocator marker;
            marker.id = node->getIntAttribute ("id", -1);
            marker.beat = node->getDoubleAttribute ("beat", -1.0);
            marker.name = node->getStringAttribute ("name").toStdString();
            marker.info = node->getStringAttribute ("info").toStdString();
            if (marker.id < 1 || ! locatorIds.insert (marker.id).second
                || ! validNumber (marker.beat) || marker.beat < 0.0)
                return juce::Result::fail ("A locator contains invalid data.");
            loaded.project.locators.push_back (std::move (marker));
            progress.completeUnit();
        }
    }
    const auto hasLocator = [&locatorIds] (int id) { return id == -1 || locatorIds.contains (id); };
    if (! hasLocator (loaded.project.selectedLocatorId) || ! hasLocator (loaded.project.songStartLocatorId))
        return juce::Result::fail ("The project references a missing locator.");

    if (auto* pluginsNode = xml->getChildByName ("Plugins"))
    {
        for (auto* node = pluginsNode->getFirstChildElement(); node != nullptr; node = node->getNextElement())
        {
            if (! node->hasTagName ("Plugin"))
                continue;
            PluginState plugin;
            plugin.trackId = node->getIntAttribute ("trackId", -1);
            plugin.order = node->getIntAttribute ("order", -1);
            plugin.identifier = node->getStringAttribute ("identifier");
            plugin.bypassed = node->getBoolAttribute ("bypassed", false);
            plugin.groupId = node->getIntAttribute ("groupId", 0);
            const auto encodedState = node->getStringAttribute ("state");
            if (plugin.trackId < 0 || ! trackIds.contains (plugin.trackId)
                || plugin.order < 0 || plugin.identifier.isEmpty() || plugin.groupId < 0
                || (encodedState.isNotEmpty() && ! plugin.state.fromBase64Encoding (encodedState)))
                return juce::Result::fail ("A plugin state contains invalid data.");
            if (plugin.identifier == DrumRackProcessor::pluginIdentifier && plugin.state.getSize() > 0)
            {
                auto stateXml = juce::XmlDocument::parse (xmlStringFromBlock (plugin.state));
                if (stateXml == nullptr || ! stateXml->hasTagName ("DrumRack"))
                    return juce::Result::fail ("The Drum Rack state is invalid.");
                for (auto* pad = stateXml->getFirstChildElement(); pad != nullptr; pad = pad->getNextElement())
                {
                    if (! pad->hasTagName ("Pad"))
                        continue;
                    const auto relativePath = pad->getStringAttribute ("file");
                    if (relativePath.isEmpty() || juce::File::isAbsolutePath (relativePath))
                        return juce::Result::fail ("A Drum Rack sample path is invalid.");
                    const auto sample = file.getParentDirectory().getChildFile (relativePath);
                    if (! sample.isAChildOf (file.getParentDirectory()))
                        return juce::Result::fail ("A Drum Rack sample path escapes the project folder.");
                    pad->setAttribute ("file", sample.getFullPathName());
                }
                storeXmlInBlock (*stateXml, plugin.state);
            }
            loaded.plugins.push_back (std::move (plugin));
            progress.completeUnit();
        }
    }

    const auto migrateLegacyGainValue = [] (double value, int version)
    {
        if (version < 2)
            value = (2.0 + value) / 3.0;
        if (version < 3)
            value *= 6.0 / 7.0;
        return value;
    };
    std::set<int> legacyUtilityTracks, legacyAutoPanTracks, legacyRateTracks;
    std::map<int, int> legacyGainTracks;
    for (auto& plugin : loaded.plugins)
    {
        if (plugin.identifier == AutoPanProcessor::pluginIdentifier)
        {
            legacyAutoPanTracks.insert (plugin.trackId);
            std::unique_ptr<juce::XmlElement> stateXml;
            if (plugin.state.getSize() > 0)
                stateXml = juce::XmlDocument::parse (xmlStringFromBlock (plugin.state));
            if (stateXml == nullptr || stateXml->getIntAttribute ("rateRangeVersion", 0) < 1)
                legacyRateTracks.insert (plugin.trackId);
            continue;
        }
        if (plugin.identifier != UtilityProcessor::pluginIdentifier)
            continue;

        const bool hadState = plugin.state.getSize() > 0;
        auto stateXml = hadState
            ? juce::XmlDocument::parse (xmlStringFromBlock (plugin.state))
            : std::make_unique<juce::XmlElement> ("NativeEffect");
        bool hasUnifiedPanParameters = false;
        if (stateXml != nullptr && stateXml->hasTagName ("NativeEffect"))
        {
            const int gainRangeVersion = stateXml->getIntAttribute ("gainRangeVersion", 0);
            const int rateRangeVersion = stateXml->getIntAttribute ("rateRangeVersion", 0);
            if (gainRangeVersion < 3)
            {
                const auto [legacyGain, inserted] =
                    legacyGainTracks.emplace (plugin.trackId, gainRangeVersion);
                if (! inserted)
                    legacyGain->second = std::min (legacyGain->second, gainRangeVersion);
                for (auto* parameter = stateXml->getFirstChildElement();
                     parameter != nullptr; parameter = parameter->getNextElement())
                    if (parameter->getStringAttribute ("id") == "gain")
                        parameter->setAttribute ("value",
                            migrateLegacyGainValue (
                                parameter->getDoubleAttribute ("value", 0.0), gainRangeVersion));
                stateXml->setAttribute ("gainRangeVersion", 3);
                storeXmlInBlock (*stateXml, plugin.state);
            }
            if (rateRangeVersion < 1)
            {
                for (auto* parameter = stateXml->getFirstChildElement();
                     parameter != nullptr; parameter = parameter->getNextElement())
                {
                    const auto id = parameter->getStringAttribute ("id");
                    if (id == "pan_rate" || id == "rate")
                        parameter->setAttribute ("value",
                            UtilityProcessor::migrateLegacyRate (
                                static_cast<float> (parameter->getDoubleAttribute ("value", 0.0))));
                }
                stateXml->setAttribute ("rateRangeVersion", 1);
                storeXmlInBlock (*stateXml, plugin.state);
                legacyRateTracks.insert (plugin.trackId);
            }
            for (auto* parameter = stateXml->getFirstChildElement();
                 parameter != nullptr; parameter = parameter->getNextElement())
                hasUnifiedPanParameters = hasUnifiedPanParameters
                    || parameter->getStringAttribute ("id") == "pan_rate";
        }
        if (hadState && ! hasUnifiedPanParameters)
            legacyUtilityTracks.insert (plugin.trackId);
    }

    for (auto& track : loaded.project.tracks)
        for (auto& envelope : track.automation)
        {
            auto& target = envelope.target;
            if (target.type != roll::AutomationParameterType::PluginParameter)
                continue;

            if (legacyAutoPanTracks.contains (track.id)
                && target.pluginIdentifier == AutoPanProcessor::pluginIdentifier)
            {
                target.pluginIdentifier = UtilityProcessor::pluginIdentifier;
                if (target.parameterId == "rate")
                {
                    target.parameterId = "pan_rate";
                    target.parameterName = "Rate";
                    target.parameterIndex = 7;
                }
                else if (target.parameterId == "depth")
                {
                    target.parameterId = "pan_amount";
                    target.parameterName = "Amount";
                    target.parameterIndex = 6;
                }
                else if (target.parameterId == "phase")
                {
                    target.parameterId = "pan_phase";
                    target.parameterName = "Phase";
                    target.parameterIndex = 8;
                }
            }

            if (legacyUtilityTracks.contains (track.id)
                && target.pluginIdentifier == UtilityProcessor::pluginIdentifier)
            {
                if (target.parameterId == "width")
                {
                    target.currentValue *= 0.5;
                    for (auto& point : envelope.points)
                        point.value *= 0.5;
                }
                else if (target.parameterId == "invert_left" || target.parameterId == "invert_right")
                {
                    target.parameterId = "invert_phase";
                    target.parameterName = "Invert Phase";
                    target.parameterIndex = 5;
                }
            }

            if (const auto legacyGain = legacyGainTracks.find (track.id);
                legacyGain != legacyGainTracks.end()
                && target.pluginIdentifier == UtilityProcessor::pluginIdentifier
                && target.parameterId == "gain")
            {
                target.currentValue = migrateLegacyGainValue (target.currentValue, legacyGain->second);
                for (auto& point : envelope.points)
                    point.value = migrateLegacyGainValue (point.value, legacyGain->second);
            }

            if (legacyRateTracks.contains (track.id)
                && target.pluginIdentifier == UtilityProcessor::pluginIdentifier
                && target.parameterId == "pan_rate")
            {
                target.currentValue = UtilityProcessor::migrateLegacyRate (
                    static_cast<float> (target.currentValue));
                for (auto& point : envelope.points)
                    point.value = UtilityProcessor::migrateLegacyRate (static_cast<float> (point.value));
            }
        }

    data = std::move (loaded);
    return juce::Result::ok();
}
