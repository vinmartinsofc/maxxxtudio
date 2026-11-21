#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

struct AudioFileData;

class ReferenceMasterWindow final : public juce::DocumentWindow
{
public:
    ReferenceMasterWindow();
    ~ReferenceMasterWindow() override;

    std::function<void (const juce::File&)> onAnalyzeRequested;
    std::function<void (std::shared_ptr<const AudioFileData>, double)> onPlayRequested;
    std::function<void()> onPauseRequested, onResumeRequested, onStopRequested;
    std::function<void (double)> onSeekRequested;
    std::function<double()> onPlayheadPositionRequested;
    std::function<bool()> onIsPlayingRequested;
    void setAnalysisStatus (const juce::String&);
    void setAnalysis (const juce::File&, std::shared_ptr<const AudioFileData>);
    void closeButtonPressed() override;
    void activeWindowStatusChanged() override;

private:
    class Content;
    Content* content = nullptr;
};
