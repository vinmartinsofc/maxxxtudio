#include <catch2/catch_test_macros.hpp>
#include "GrooveLibrary.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
struct TemporaryGrooveFolder
{
    TemporaryGrooveFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("maxxx_tudio_grooves", "", false))
    {
        folder.createDirectory();
    }

    ~TemporaryGrooveFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

void appendDouble (juce::MemoryBlock& bytes, double value)
{
    std::array<std::uint8_t, sizeof (double)> encoded {};
    std::memcpy (encoded.data(), &value, sizeof (value));
#if JUCE_BIG_ENDIAN
    std::reverse (encoded.begin(), encoded.end());
#endif
    bytes.append (encoded.data(), encoded.size());
}

void appendFloat (juce::MemoryBlock& bytes, float value)
{
    std::array<std::uint8_t, sizeof (float)> encoded {};
    std::memcpy (encoded.data(), &value, sizeof (value));
#if JUCE_BIG_ENDIAN
    std::reverse (encoded.begin(), encoded.end());
#endif
    bytes.append (encoded.data(), encoded.size());
}

juce::MemoryBlock makeBinaryGroove (double grid)
{
    juce::MemoryBlock bytes;
    const std::uint8_t prefix[] { 0xab, 0x1e, 0x56, 0x78 };
    bytes.append (prefix, sizeof (prefix));
    const int slotCount = static_cast<int> (std::lround (4.0 / grid));
    for (int slot = 0; slot < slotCount; ++slot)
    {
        appendFloat (bytes, 100.0f);
        const std::array<std::uint8_t, 5> metadata {};
        bytes.append (metadata.data(), metadata.size());
        appendDouble (bytes, slot * grid + (slot % 2 == 1 ? grid * 0.04 : 0.0));
        appendDouble (bytes, 0.0625);
    }
    return bytes;
}

void writeGroove (const juce::File& file, double grid)
{
    const auto bytes = makeBinaryGroove (grid);
    file.replaceWithData (bytes.getData(), bytes.getSize());
}
}

TEST_CASE ("Groove library imports a binary .agr swing pattern")
{
    TemporaryGrooveFolder temporary;
    const auto file = temporary.folder.getChildFile ("Swing 8ths 57.agr");
    writeGroove (file, 0.5);

    roll::GrooveTemplate groove;
    REQUIRE (GrooveLibrary::loadFile (file, groove).wasOk());
    REQUIRE (groove.name == "Swing 8ths 57");
    REQUIRE (groove.gridBeats == 0.5);
    REQUIRE (groove.timingOffsets.size() == 8);
    REQUIRE (groove.timingOffsets[0] == 0.0);
    REQUIRE (std::abs (groove.timingOffsets[1] - 0.02) < 1.0e-8);
}

TEST_CASE ("Groove library supports quarter, eighth, sixteenth, and thirty-second-note grids")
{
    TemporaryGrooveFolder temporary;
    const std::array<std::pair<const char*, double>, 4> grooves {{
        { "Basic 4ths.agr", 1.0 },
        { "Basic 8ths.agr", 0.5 },
        { "Basic 16ths.agr", 0.25 },
        { "Basic 32ths.agr", 0.125 }
    }};

    for (const auto& [name, grid] : grooves)
    {
        const auto file = temporary.folder.getChildFile (name);
        writeGroove (file, grid);

        roll::GrooveTemplate groove;
        REQUIRE (GrooveLibrary::loadFile (file, groove).wasOk());
        REQUIRE (groove.gridBeats == grid);
        REQUIRE (groove.timingOffsets.size() == static_cast<std::size_t> (4.0 / grid));
    }
}

TEST_CASE ("Groove library reports incompatible files instead of inventing a groove")
{
    TemporaryGrooveFolder temporary;
    const auto file = temporary.folder.getChildFile ("unsupported.agr");
    REQUIRE (file.replaceWithText ("not a supported groove"));

    roll::GrooveTemplate groove;
    REQUIRE (GrooveLibrary::loadFile (file, groove).failed());
}

TEST_CASE ("Groove library combines simultaneous notes and wraps the bar boundary")
{
    TemporaryGrooveFolder temporary;
    const auto file = temporary.folder.getChildFile ("Combined 16ths.agr");
    const auto xml = R"(<Groove>
        <KeyTrack><Notes>
            <MidiNoteEvent Time="0" Duration="0.0625" />
            <MidiNoteEvent Time="0.25" Duration="0.0625" />
            <MidiNoteEvent Time="3.99" Duration="0.0625" />
        </Notes></KeyTrack>
        <KeyTrack><Notes>
            <MidiNoteEvent Time="0" Duration="0.0625" />
        </Notes></KeyTrack>
    </Groove>)";
    REQUIRE (file.replaceWithText (xml));

    roll::GrooveTemplate groove;
    REQUIRE (GrooveLibrary::loadFile (file, groove).wasOk());
    REQUIRE (groove.gridBeats == 0.25);
    REQUIRE (groove.timingOffsets.size() == 16);
    REQUIRE (std::abs (groove.timingOffsets[0] + 0.01 / 3.0) < 1.0e-8);
}

TEST_CASE ("Groove library groups selected folders into browseable categories")
{
    TemporaryGrooveFolder temporary;
    const std::array<std::pair<const char*, double>, 5> files {{
        { "Utility/Utility 8ths.agr", 0.5 },
        { "Swing/Basic/Basic 16ths.agr", 0.25 },
        { "Style/Style 32ths.agr", 0.125 },
        { "Percussion/Percussion 16ths.agr", 0.25 },
        { "Swing/Logic/Logic 32ths.agr", 0.125 }
    }};
    for (const auto& [relativePath, grid] : files)
    {
        const auto file = temporary.folder.getChildFile (relativePath);
        REQUIRE (file.getParentDirectory().createDirectory().wasOk());
        writeGroove (file, grid);
    }

    const std::vector<juce::String> includedCategories {
        "Utility", "Swing / Basic", "Style", "Percussion"
    };
    juce::String skipped;
    const auto categories = GrooveLibrary::loadCategories (temporary.folder, &skipped, includedCategories);
    REQUIRE (categories.size() == 4);
    REQUIRE (skipped.isEmpty());

    juce::StringArray names;
    std::array<bool, 3> foundGrid {};
    for (const auto& category : categories)
    {
        names.add (category.name);
        REQUIRE (category.entries.size() == 1);
        const auto grid = category.entries.front().groove.gridBeats;
        if (grid == 0.5) foundGrid[0] = true;
        if (grid == 0.25) foundGrid[1] = true;
        if (grid == 0.125) foundGrid[2] = true;
    }
    REQUIRE (names.contains ("Utility"));
    REQUIRE (names.contains ("Swing / Basic"));
    REQUIRE (names.contains ("Style"));
    REQUIRE (names.contains ("Percussion"));
    REQUIRE_FALSE (names.contains ("Swing / Logic"));
    REQUIRE (std::all_of (foundGrid.begin(), foundGrid.end(), [] (bool found) { return found; }));
}

TEST_CASE ("Groove library can read an installed groove collection")
{
    const auto directory = GrooveLibrary::defaultGroovesDirectory();
    if (! directory.isDirectory())
        SKIP ("No local groove collection is installed on this machine.");

    const std::vector<juce::String> includedCategories {
        "Utility", "Swing / Basic", "Style", "Percussion"
    };
    juce::String skipped;
    const auto categories = GrooveLibrary::loadCategories (directory, &skipped, includedCategories);
    REQUIRE (categories.size() == includedCategories.size());
    std::size_t sourceFileCount = 0;
    std::size_t loadedFileCount = 0;
    for (const auto& category : categories)
    {
        juce::Array<juce::File> files;
        category.directory.findChildFiles (files, juce::File::findFiles, false, "*.agr");
        sourceFileCount += static_cast<std::size_t> (files.size());
        loadedFileCount += category.entries.size();
    }
    INFO (skipped);
    REQUIRE (loadedFileCount == sourceFileCount);
    REQUIRE (std::all_of (categories.begin(), categories.end(),
                          [] (const auto& category) { return ! category.entries.empty(); }));
    REQUIRE (directory.getFullPathName().containsIgnoreCase ("Grooves"));
}
