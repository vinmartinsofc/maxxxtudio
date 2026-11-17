#pragma once

#include "Arrangement.h"
#include <juce_core/juce_core.h>
#include <vector>

struct GrooveLibraryEntry
{
    juce::File file;
    roll::GrooveTemplate groove;
};

struct GrooveLibraryCategory
{
    juce::String name;
    juce::File directory;
    std::vector<GrooveLibraryEntry> entries;
};

class GrooveLibrary
{
public:
    static juce::File defaultGroovesDirectory();
    static std::vector<GrooveLibraryEntry> loadDirectory (const juce::File&, juce::String* skippedFiles = nullptr);
    static std::vector<GrooveLibraryCategory> loadCategories (const juce::File&,
                                                               juce::String* skippedFiles = nullptr,
                                                               const std::vector<juce::String>& includedCategories = {});
    static juce::Result loadFile (const juce::File&, roll::GrooveTemplate&);
};
