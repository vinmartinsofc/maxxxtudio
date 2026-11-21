#include "GrooveLibrary.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr std::size_t binaryRecordStride = 25;
constexpr double grooveBarBeats = 4.0;

struct NoteRecord
{
    double time = 0.0;
    double duration = 0.0;
};

double readLittleEndianDouble (const juce::MemoryBlock& bytes, std::size_t offset)
{
    if (offset + sizeof (double) > bytes.getSize())
        return std::numeric_limits<double>::quiet_NaN();

    std::array<std::uint8_t, sizeof (double)> value {};
    std::memcpy (value.data(), static_cast<const std::uint8_t*> (bytes.getData()) + offset, value.size());
#if JUCE_BIG_ENDIAN
    std::reverse (value.begin(), value.end());
#endif
    double result = 0.0;
    std::memcpy (&result, value.data(), sizeof (result));
    return result;
}

float readLittleEndianFloat (const juce::MemoryBlock& bytes, std::size_t offset)
{
    if (offset + sizeof (float) > bytes.getSize())
        return std::numeric_limits<float>::quiet_NaN();

    std::array<std::uint8_t, sizeof (float)> value {};
    std::memcpy (value.data(), static_cast<const std::uint8_t*> (bytes.getData()) + offset, value.size());
#if JUCE_BIG_ENDIAN
    std::reverse (value.begin(), value.end());
#endif
    float result = 0.0f;
    std::memcpy (&result, value.data(), sizeof (result));
    return result;
}

double gridForName (const juce::String& name)
{
    const auto lower = name.toLowerCase();
    if (lower.contains ("32th"))  return 0.125;
    if (lower.contains ("16th"))  return 0.25;
    if (lower.contains ("8th"))   return 0.5;
    if (lower.contains ("4th"))   return 1.0;
    if (lower.contains ("16t"))   return 1.0 / 6.0;
    if (lower.contains ("8t"))    return 1.0 / 3.0;
    if (lower.startsWith ("quantize 4"))  return 1.0;
    if (lower.startsWith ("quantize 8"))  return 0.5;
    if (lower.startsWith ("quantize 16")) return 0.25;
    return 0.0;
}

bool validRecord (double velocity, double time, double duration)
{
    return std::isfinite (velocity) && velocity >= 0.0 && velocity <= 127.0
        && std::isfinite (time) && time >= 0.0 && time < grooveBarBeats
        && std::isfinite (duration) && duration >= 0.0 && duration <= 4.0;
}

bool extractBinaryNotes (const juce::MemoryBlock& bytes, double grid, std::vector<NoteRecord>& result)
{
    if (bytes.getSize() < binaryRecordStride * 2 || grid <= 0.0)
        return false;

    std::vector<NoteRecord> best;
    const auto maxStart = bytes.getSize() - binaryRecordStride * 4;

    for (std::size_t start = 0; start <= maxStart; ++start)
    {
        std::vector<NoteRecord> candidate;
        double previousTime = -1.0;
        int previousSlot = -1;

        for (std::size_t index = 0; start + (index + 1) * binaryRecordStride <= bytes.getSize(); ++index)
        {
            const auto offset = start + index * binaryRecordStride;
            const double velocity = readLittleEndianFloat (bytes, offset);
            const double time = readLittleEndianDouble (bytes, offset + 9);
            const double duration = readLittleEndianDouble (bytes, offset + 17);
            if (! validRecord (velocity, time, duration)
                || (index > 0 && time <= previousTime))
                break;

            const auto slot = static_cast<int> (std::llround (time / grid));
            const double offsetFromGrid = time - slot * grid;
            const auto slotCount = static_cast<int> (std::ceil (grooveBarBeats / grid));
            if (slot < 0 || slot > slotCount || slot <= previousSlot
                || std::abs (offsetFromGrid) > grid * 0.5 + 1.0e-9)
                break;

            candidate.push_back ({ time, duration });
            previousTime = time;
            previousSlot = slot;
        }

        if (candidate.size() > best.size())
            best = std::move (candidate);
    }

    if (best.size() < 2)
        return false;

    result = std::move (best);
    return true;
}

void findXmlNoteEvents (const juce::XmlElement& element, std::vector<NoteRecord>& events)
{
    if (element.hasTagName ("MidiNoteEvent"))
    {
        const double time = element.getDoubleAttribute ("Time", std::numeric_limits<double>::quiet_NaN());
        const double duration = element.getDoubleAttribute ("Duration", 0.0);
        if (std::isfinite (time) && time >= 0.0 && time < grooveBarBeats)
            events.push_back ({ time, duration });
    }

    for (auto* child = element.getFirstChildElement(); child != nullptr; child = child->getNextElement())
        findXmlNoteEvents (*child, events);
}

bool makeTemplate (const juce::String& name, const juce::File& file, double grid,
                   const std::vector<NoteRecord>& notes, roll::GrooveTemplate& groove)
{
    if (grid <= 0.0 || notes.empty())
        return false;

    const auto slotCount = static_cast<std::size_t> (std::ceil (grooveBarBeats / grid));
    std::vector<double> offsetSums (slotCount, 0.0);
    std::vector<int> offsetCounts (slotCount, 0);
    double previousTime = -1.0;

    for (const auto& note : notes)
    {
        if (! std::isfinite (note.time) || note.time < 0.0 || note.time >= grooveBarBeats
            || note.time < previousTime)
            return false;

        const auto unwrappedSlot = static_cast<std::int64_t> (std::llround (note.time / grid));
        if (unwrappedSlot < 0)
            return false;

        const auto slot = static_cast<std::size_t> (unwrappedSlot) % slotCount;
        const double offset = note.time - static_cast<double> (unwrappedSlot) * grid;
        if (std::abs (offset) > grid * 0.5 + 1.0e-9)
            return false;

        offsetSums[slot] += offset;
        ++offsetCounts[slot];
        previousTime = note.time;
    }

    std::vector<double> offsets (slotCount, 0.0);
    for (std::size_t slot = 0; slot < slotCount; ++slot)
        if (offsetCounts[slot] > 0)
            offsets[slot] = offsetSums[slot] / offsetCounts[slot];

    groove.name = name.toStdString();
    groove.sourcePath = file.getFullPathName().toStdString();
    groove.gridBeats = grid;
    groove.timingOffsets = std::move (offsets);
    groove.velocityMultipliers.clear();
    return groove.isValid();
}

double inferGridFromNotes (const juce::String& name, const juce::File& file,
                           const std::vector<NoteRecord>& notes)
{
    if (notes.empty())
        return 0.0;

    static const std::array<double, 6> candidates { 0.5, 0.25, 0.125, 1.0, 1.0 / 3.0, 1.0 / 6.0 };
    for (const double grid : candidates)
    {
        roll::GrooveTemplate candidate;
        if (makeTemplate (name, file, grid, notes, candidate))
            return grid;
    }

    return 0.0;
}
}

namespace
{
juce::File findInstalledGroovesRoot (const juce::File& installRoot)
{
    const auto coreLibrary = installRoot.getChildFile ("Resources").getChildFile ("Core Library");
    const auto groovesRoot = coreLibrary.getChildFile ("Grooves");
    juce::Array<juce::File> files;
    if (groovesRoot.isDirectory())
        groovesRoot.findChildFiles (files, juce::File::findFiles, true, "*.agr");
    return files.isEmpty() ? juce::File() : groovesRoot;
}

void loadCategoryFolders (const juce::File& directory, const juce::String& relativeName,
                          const std::vector<juce::String>& includedCategories,
                          std::vector<GrooveLibraryCategory>& categories, juce::String* skippedFiles)
{
    const bool includeDirectory = includedCategories.empty()
        || std::find (includedCategories.begin(), includedCategories.end(), relativeName)
               != includedCategories.end();
    auto entries = includeDirectory
        ? GrooveLibrary::loadDirectory (directory, skippedFiles)
        : std::vector<GrooveLibraryEntry> {};
    if (! entries.empty())
    {
        GrooveLibraryCategory category;
        category.name = relativeName.isEmpty() ? directory.getFileName() : relativeName;
        category.directory = directory;
        category.entries = std::move (entries);
        categories.push_back (std::move (category));
    }

    juce::Array<juce::File> subdirectories;
    directory.findChildFiles (subdirectories, juce::File::findDirectories, false);
    subdirectories.sort();
    for (const auto& subdirectory : subdirectories)
    {
        const auto childName = relativeName.isEmpty()
            ? subdirectory.getFileName()
            : relativeName + " / " + subdirectory.getFileName();
        loadCategoryFolders (subdirectory, childName, includedCategories, categories, skippedFiles);
    }
}
}

juce::File GrooveLibrary::defaultGroovesDirectory()
{
    const auto commonData = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
    juce::Array<juce::File> installFolders;
    commonData.findChildFiles (installFolders, juce::File::findDirectories, false);

    for (const auto& install : installFolders)
    {
        const auto candidate = findInstalledGroovesRoot (install);
        if (candidate.isDirectory())
            return candidate;

        juce::Array<juce::File> nestedFolders;
        install.findChildFiles (nestedFolders, juce::File::findDirectories, false);
        for (const auto& nested : nestedFolders)
        {
            const auto nestedCandidate = findInstalledGroovesRoot (nested);
            if (nestedCandidate.isDirectory())
                return nestedCandidate;
        }
    }

    return commonData.getChildFile ("Grooves");
}

std::vector<GrooveLibraryEntry> GrooveLibrary::loadDirectory (const juce::File& directory,
                                                               juce::String* skippedFiles)
{
    std::vector<GrooveLibraryEntry> entries;
    juce::Array<juce::File> files;
    directory.findChildFiles (files, juce::File::findFiles, false, "*.agr");
    files.sort();

    for (const auto& file : files)
    {
        roll::GrooveTemplate groove;
        const auto result = loadFile (file, groove);
        if (result.wasOk())
            entries.push_back ({ file, std::move (groove) });
        else if (skippedFiles != nullptr)
            *skippedFiles << (skippedFiles->isEmpty() ? "" : "\n")
                          << file.getFileName() << ": " << result.getErrorMessage();
    }
    return entries;
}

std::vector<GrooveLibraryCategory> GrooveLibrary::loadCategories (const juce::File& directory,
                                                                   juce::String* skippedFiles,
                                                                   const std::vector<juce::String>& includedCategories)
{
    std::vector<GrooveLibraryCategory> categories;
    if (directory.isDirectory())
        loadCategoryFolders (directory, {}, includedCategories, categories, skippedFiles);
    return categories;
}

juce::Result GrooveLibrary::loadFile (const juce::File& file, roll::GrooveTemplate& groove)
{
    if (! file.existsAsFile() || ! file.getFileExtension().equalsIgnoreCase (".agr"))
        return juce::Result::fail ("Select an existing .agr file.");

    if (file.getSize() <= 0 || file.getSize() > 1024 * 1024)
        return juce::Result::fail ("The .agr file is empty or exceeds the 1 MB limit.");

    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return juce::Result::fail ("Could not read the .agr file.");

    const auto fileName = file.getFileNameWithoutExtension();
    double grid = gridForName (fileName);
    std::vector<NoteRecord> notes;
    const auto firstNonSpace = std::find_if_not (static_cast<const char*> (bytes.getData()),
        static_cast<const char*> (bytes.getData()) + bytes.getSize(),
        [] (char character) { return std::isspace (static_cast<unsigned char> (character)) != 0; });

    if (firstNonSpace != static_cast<const char*> (bytes.getData()) + bytes.getSize()
        && *firstNonSpace == '<')
    {
        const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
            static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize())));
        if (xml != nullptr)
            findXmlNoteEvents (*xml, notes);
    }
    else
    {
        bool extracted = grid > 0.0 && extractBinaryNotes (bytes, grid, notes);
        if (grid <= 0.0)
        {
            static const std::array<double, 6> candidateGrids {
                0.5, 0.25, 0.125, 1.0, 1.0 / 3.0, 1.0 / 6.0
            };
            for (const double candidateGrid : candidateGrids)
            {
                std::vector<NoteRecord> candidateNotes;
                if (extractBinaryNotes (bytes, candidateGrid, candidateNotes)
                    && candidateNotes.size() > notes.size())
                {
                    grid = candidateGrid;
                    notes = std::move (candidateNotes);
                    extracted = true;
                }
            }
        }

        if (! extracted)
            return juce::Result::fail ("This groove binary format is not supported yet.");
    }

    std::sort (notes.begin(), notes.end(), [] (const auto& first, const auto& second)
    {
        return first.time < second.time;
    });

    const double inferredGrid = grid > 0.0 ? grid : inferGridFromNotes (fileName, file, notes);
    if (inferredGrid <= 0.0)
        return juce::Result::fail ("Could not identify the groove time division from its name.");

    if (! makeTemplate (fileName, file, inferredGrid, notes, groove))
        return juce::Result::fail ("The groove does not contain a note pattern compatible with its time division.");

    return juce::Result::ok();
}
