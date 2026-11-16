#include "ProjectFile.h"
#include <algorithm>
#include <cmath>
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
    for (const auto& clip : track.clips)
        count += 1 + clip->clip.notes().size();
    count += track.audioClips.size();
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
                        if (note->hasTagName ("Note"))
                            ++count;
                }
                else if (child->hasTagName ("AudioClip"))
                {
                    ++count;
                }
            }
        }
    }
    if (const auto* locators = xml.getChildByName ("Locators"))
        for (auto* node = locators->getFirstChildElement(); node != nullptr; node = node->getNextElement())
            if (node->hasTagName ("Locator"))
                ++count;
    if (const auto* plugins = xml.getChildByName ("Plugins"))
        for (auto* node = plugins->getFirstChildElement(); node != nullptr; node = node->getNextElement())
            if (node->hasTagName ("Plugin"))
                ++count;
    return count;
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
    node->setAttribute ("volume", track.volume);
    node->setAttribute ("pan", track.pan);
    node->setAttribute ("mute", track.mute);
    node->setAttribute ("solo", track.solo);
    node->setAttribute ("routing", track.outputRouting == roll::TrackOutputRouting::ExtOut ? "external" : "master");

    for (const auto& clip : track.clips)
    {
        auto* clipNode = node->createNewChildElement ("MidiClip");
        clipNode->setAttribute ("id", clip->id);
        clipNode->setAttribute ("start", clip->start);
        clipNode->setAttribute ("length", clip->clip.length());
        clipNode->setAttribute ("enabled", clip->enabled);
        clipNode->setAttribute ("name", stringFromUtf8 (clip->name));
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
        progress.completeUnit();
    }

    for (const auto& clip : track.audioClips)
    {
        const auto sourcePath = clip->filePath;
        auto asset = copiedAssets.find (sourcePath);
        if (asset == copiedAssets.end())
        {
            const juce::File source (stringFromUtf8 (sourcePath));
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
            asset = copiedAssets.emplace (sourcePath,
                target.getRelativePathFrom (projectFolder)).first;
        }

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
        audioNode->setAttribute ("name", stringFromUtf8 (clip->name));
        audioNode->setAttribute ("file", asset->second);
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
    track.volume = node.getDoubleAttribute ("volume", 1.0);
    track.pan = node.getDoubleAttribute ("pan", 0.0);
    track.mute = node.getBoolAttribute ("mute", false);
    track.solo = node.getBoolAttribute ("solo", false);
    track.outputRouting = node.getStringAttribute ("routing") == "external"
        ? roll::TrackOutputRouting::ExtOut : roll::TrackOutputRouting::Master;
    if (track.parentGroupId < 0 || ! validNumber (track.volume) || track.volume < 0.0 || track.volume > 1.5
        || ! validNumber (track.pan) || track.pan < -1.0 || track.pan > 1.0)
        return false;

    std::set<int> clipIds;
    for (auto* child = node.getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->hasTagName ("MidiClip"))
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
            std::vector<roll::Note> notes;
            std::set<int> noteIds;
            for (auto* noteNode = child->getFirstChildElement(); noteNode != nullptr; noteNode = noteNode->getNextElement())
            {
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

        auto* pluginsNode = xml->createNewChildElement ("Plugins");
        for (const auto& plugin : data.plugins)
        {
            auto* pluginNode = pluginsNode->createNewChildElement ("Plugin");
            pluginNode->setAttribute ("trackId", plugin.trackId);
            pluginNode->setAttribute ("order", plugin.order);
            pluginNode->setAttribute ("identifier", plugin.identifier);
            pluginNode->setAttribute ("bypassed", plugin.bypassed);
            pluginNode->setAttribute ("groupId", plugin.groupId);
            pluginNode->setAttribute ("state", plugin.state.toBase64Encoding());
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
            loaded.plugins.push_back (std::move (plugin));
            progress.completeUnit();
        }
    }

    data = std::move (loaded);
    return juce::Result::ok();
}
