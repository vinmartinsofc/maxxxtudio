#include "ReferenceMasterWindow.h"
#include "AudioEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
bool isSupportedAudioFile (const juce::File& file)
{
    static const juce::StringArray extensions {
        ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"
    };
    return file.existsAsFile() && extensions.contains (file.getFileExtension(), true);
}

juce::String formatTime (double seconds)
{
    if (! std::isfinite (seconds) || seconds < 0.0)
        seconds = 0.0;
    const auto totalSeconds = static_cast<std::int64_t> (seconds);
    return juce::String (totalSeconds / 60).paddedLeft ('0', 2) + ":"
         + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
}

float waveformHue (float frequency)
{
    constexpr std::array<float, 7> frequencies { 40.0f, 100.0f, 220.0f, 600.0f,
                                                  1500.0f, 5000.0f, 16000.0f };
    constexpr std::array<float, 7> hues { 275.0f, 265.0f, 250.0f, 240.0f,
                                          225.0f, 205.0f, 185.0f };
    frequency = juce::jlimit (frequencies.front(), frequencies.back(), frequency);
    for (std::size_t i = 1; i < frequencies.size(); ++i)
    {
        if (frequency <= frequencies[i])
        {
            const float proportion = std::log (frequency / frequencies[i - 1])
                / std::log (frequencies[i] / frequencies[i - 1]);
            return (hues[i - 1] + proportion * (hues[i] - hues[i - 1])) / 360.0f;
        }
    }
    return hues.back() / 360.0f;
}

juce::Colour spectrumColour (float frequencyRatio, float intensity)
{
    juce::Colour colour;
    if (frequencyRatio < 0.18f)
    {
        const float band = frequencyRatio / 0.18f;
        colour = juce::Colour::fromRGB (255,
            static_cast<juce::uint8> (55 + band * 75),
            static_cast<juce::uint8> (20 + band * 5));
    }
    else if (frequencyRatio < 0.42f)
    {
        const float band = (frequencyRatio - 0.18f) / 0.24f;
        colour = juce::Colour::fromRGB (
            static_cast<juce::uint8> (255 - band * 40),
            static_cast<juce::uint8> (32 + band * 95),
            static_cast<juce::uint8> (128 + band * 95));
    }
    else
    {
        const float band = (frequencyRatio - 0.42f) / 0.58f;
        colour = juce::Colour::fromHSV (juce::jmap (band, 0.70f, 0.56f),
                                         juce::jmap (band, 0.82f, 0.68f),
                                         1.0f, 1.0f);
    }
    const float brightness = juce::jlimit (0.18f, 1.0f, 0.18f + intensity * 0.82f);
    return colour.withBrightness (brightness);
}
}

class ReferenceMasterWindow::Content final : public juce::Component,
                                             public juce::FileDragAndDropTarget,
                                             private juce::Timer
{
public:
    explicit Content (ReferenceMasterWindow& ownerToUse) : owner (ownerToUse), deck (*this)
    {
        setWantsKeyboardFocus (true);
        title.setText ("REFERENCE MASTER", juce::dontSendNotification);
        title.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (title);

        instructions.setText ("REFERENCE ANALYZER  |  Click/drag to seek, Shift-drag to scroll, mouse wheel to zoom.",
                              juce::dontSendNotification);
        instructions.setFont (juce::FontOptions (11.5f));
        instructions.setColour (juce::Label::textColourId, theme::textDim());
        addAndMakeVisible (instructions);

        openButton.setButtonText ("Open Audio");
        openButton.onClick = [this] { openFileChooser(); };
        addAndMakeVisible (openButton);

        removeButton.setButtonText ("Remove");
        removeButton.onClick = [this] { clearTrack(); };
        removeButton.setEnabled (false);
        addAndMakeVisible (removeButton);

        rescanButton.setButtonText ("Reescanear");
        rescanButton.onClick = [this] { rescanAnalysis(); };
        rescanButton.setEnabled (false);
        addAndMakeVisible (rescanButton);

        fileName.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        fileName.setColour (juce::Label::textColourId, theme::text());
        addAndMakeVisible (fileName);

        status.setText ("Drag a track onto the deck or choose a file.",
                        juce::dontSendNotification);
        status.setFont (juce::FontOptions (11.5f));
        status.setColour (juce::Label::textColourId, theme::textDim());
        addAndMakeVisible (status);

        for (auto* metric : { &bpm, &key, &duration })
        {
            metric->setJustificationType (juce::Justification::centred);
            metric->setColour (juce::Label::textColourId, theme::text());
            addAndMakeVisible (*metric);
        }
        bpm.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        key.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        duration.setFont (juce::FontOptions (22.0f, juce::Font::bold));

        halveBpmButton.setButtonText ("/2");
        halveBpmButton.onClick = [this] { adjustBpm (0.5); };
        addAndMakeVisible (halveBpmButton);
        doubleBpmButton.setButtonText ("x2");
        doubleBpmButton.onClick = [this] { adjustBpm (2.0); };
        addAndMakeVisible (doubleBpmButton);

        playButton.setButtonText ("Play");
        playButton.onClick = [this] { playOrResume(); };
        addAndMakeVisible (playButton);
        pauseButton.setButtonText ("Pause");
        pauseButton.onClick = [this] { pausePlayback(); };
        addAndMakeVisible (pauseButton);
        stopButton.setButtonText ("Stop");
        stopButton.onClick = [this]
        {
            if (owner.onStopRequested)
                owner.onStopRequested();
            hasPlaybackStarted = false;
            isPaused = false;
            displayedPosition = 0.0;
            deck.setPosition (0.0, false);
            updateTransport();
        };
        addAndMakeVisible (stopButton);

        seekSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        seekSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        seekSlider.setColour (juce::Slider::trackColourId, theme::accent());
        seekSlider.setColour (juce::Slider::backgroundColourId, theme::border());
        seekSlider.onDragStart = [this]
        {
            seekSliderShouldResume = shouldKeepPlayingAfterSeek();
        };
        seekSlider.onValueChange = [this]
        {
            if (seekSlider.isMouseButtonDown())
                requestSeek (seekSlider.getValue());
        };
        seekSlider.onDragEnd = [this]
        {
            if (seekSliderShouldResume)
                requestSeek (seekSlider.getValue(), true);
            seekSliderShouldResume = false;
        };
        addAndMakeVisible (seekSlider);

        timeReadout.setText ("00:00 / 00:00", juce::dontSendNotification);
        timeReadout.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        timeReadout.setColour (juce::Label::textColourId, theme::text());
        timeReadout.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (timeReadout);

        zoomOutButton.setButtonText ("-");
        zoomOutButton.onClick = [this] { deck.zoomBy (0.75); };
        addAndMakeVisible (zoomOutButton);
        zoomInButton.setButtonText ("+");
        zoomInButton.onClick = [this] { deck.zoomBy (1.5); };
        addAndMakeVisible (zoomInButton);
        fitButton.setButtonText ("Fit");
        fitButton.onClick = [this] { deck.fitToTrack(); };
        addAndMakeVisible (fitButton);

        addAndMakeVisible (deck);
        pauseButton.setEnabled (false);
        stopButton.setEnabled (false);
        halveBpmButton.setEnabled (false);
        doubleBpmButton.setEnabled (false);
        zoomOutButton.setEnabled (false);
        zoomInButton.setEnabled (false);
        fitButton.setEnabled (false);
        for (auto* button : { &openButton, &removeButton, &halveBpmButton, &doubleBpmButton,
                              &rescanButton, &playButton, &pauseButton, &stopButton,
                              &zoomOutButton, &zoomInButton, &fitButton })
            button->setWantsKeyboardFocus (false);
        seekSlider.setWantsKeyboardFocus (false);
        deck.setWantsKeyboardFocus (false);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && isSupportedAudioFile (juce::File (files[0]));
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() != 1)
        {
            setAnalysisStatus ("Drag one audio file at a time.");
            return;
        }
        requestAnalysis (juce::File (files[0]));
    }

    void setAnalysisStatus (const juce::String& text)
    {
        status.setText (text, juce::dontSendNotification);
        if (text.startsWith ("Error") || text.startsWith ("Failure"))
        {
            loadPending = false;
            removeButton.setEnabled (analysis != nullptr);
            rescanButton.setEnabled (analysis != nullptr);
        }
    }

    void setAnalysis (const juce::File& file, std::shared_ptr<const AudioFileData> data)
    {
        if (data == nullptr)
        {
            loadPending = false;
            removeButton.setEnabled (analysis != nullptr);
            setAnalysisStatus ("Could not analyze the file.");
            return;
        }
        loadPending = false;
        if (owner.onStopRequested)
            owner.onStopRequested();
        hasPlaybackStarted = false;
        isPaused = false;
        displayedPosition = 0.0;
        analysis = std::move (data);
        fileName.setText (file.getFileName(), juce::dontSendNotification);
        bpmValue = analysis->estimatedBpm;
        updateBpm();
        key.setText (analysis->estimatedKey.isNotEmpty() ? analysis->estimatedKey : "--",
                     juce::dontSendNotification);
        duration.setText (formatTime (analysis->durationSeconds), juce::dontSendNotification);
        seekSlider.setRange (0.0, std::max (0.001, analysis->durationSeconds), 0.001);
        seekSlider.setValue (0.0, juce::dontSendNotification);
        deck.setAnalysis (analysis);
        pauseButton.setEnabled (false);
        stopButton.setEnabled (true);
        halveBpmButton.setEnabled (bpmValue > 0.0);
        doubleBpmButton.setEnabled (bpmValue > 0.0);
        removeButton.setEnabled (true);
        rescanButton.setEnabled (true);
        zoomOutButton.setEnabled (true);
        zoomInButton.setEnabled (true);
        fitButton.setEnabled (true);
        startTimerHz (60);
        setAnalysisStatus ("Analysis ready. BPM x2 and /2 adjust the display; audio remains at its normal speed.");
        updateTransport();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel());
        const auto bounds = getLocalBounds().reduced (12);
        auto cards = bounds.withY (bounds.getY() + 99).withHeight (76);
        const int cardWidth = cards.getWidth() / 3;
        for (int index = 0; index < 3; ++index)
        {
            auto card = cards.withX (cards.getX() + index * cardWidth)
                             .withWidth (index == 2 ? cards.getWidth() - index * cardWidth
                                                    : cardWidth - 6);
            g.setColour (theme::panelLight());
            g.fillRoundedRectangle (card.toFloat(), 6.0f);
            g.setColour (theme::border());
            g.drawRoundedRectangle (card.toFloat(), 6.0f, 1.0f);
        }
        g.setColour (theme::textDim());
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const auto bpmCard = cards.withWidth (cardWidth - 6);
        const auto keyCard = cards.withX (cards.getX() + cardWidth).withWidth (cardWidth - 6);
        const auto durationCard = cards.withX (cards.getX() + cardWidth * 2)
                                       .withWidth (cards.getWidth() - cardWidth * 2);
        g.drawText ("BPM", bpmCard.reduced (10).withHeight (15),
                    juce::Justification::centredLeft);
        g.drawText ("KEY", keyCard.reduced (10).withHeight (15),
                    juce::Justification::centredLeft);
        g.drawText ("DURATION", durationCard.reduced (10).withHeight (15),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        title.setBounds (area.removeFromTop (26));
        instructions.setBounds (area.removeFromTop (18));
        area.removeFromTop (3);

        auto fileRow = area.removeFromTop (30);
        openButton.setBounds (fileRow.removeFromLeft (102));
        fileRow.removeFromLeft (5);
        removeButton.setBounds (fileRow.removeFromLeft (61));
        fileRow.removeFromLeft (5);
        rescanButton.setBounds (fileRow.removeFromLeft (94));
        fileRow.removeFromLeft (9);
        fileName.setBounds (fileRow);
        status.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);

        auto cards = area.removeFromTop (76);
        const int cardWidth = cards.getWidth() / 3;
        bpm.setBounds (cards.getX() + 72, cards.getY() + 19,
                       std::max (60, cardWidth - 154), 43);
        halveBpmButton.setBounds (cards.getX() + 10, cards.getY() + 43, 42, 24);
        doubleBpmButton.setBounds (cards.getX() + 56, cards.getY() + 43, 42, 24);
        key.setBounds (cards.getX() + cardWidth, cards.getY() + 19,
                       cardWidth, 48);
        duration.setBounds (cards.getX() + cardWidth * 2, cards.getY() + 19,
                            cards.getWidth() - cardWidth * 2, 48);
        area.removeFromTop (7);

        auto transport = area.removeFromTop (36);
        playButton.setBounds (transport.removeFromLeft (82));
        transport.removeFromLeft (5);
        pauseButton.setBounds (transport.removeFromLeft (74));
        transport.removeFromLeft (5);
        stopButton.setBounds (transport.removeFromLeft (60));
        transport.removeFromLeft (9);
        timeReadout.setBounds (transport.removeFromLeft (100));
        transport.removeFromLeft (6);
        fitButton.setBounds (transport.removeFromRight (62));
        transport.removeFromRight (4);
        zoomInButton.setBounds (transport.removeFromRight (34));
        transport.removeFromRight (3);
        zoomOutButton.setBounds (transport.removeFromRight (34));
        transport.removeFromRight (8);
        seekSlider.setBounds (transport);
        area.removeFromTop (5);
        deck.setBounds (area);
    }

    bool keyPressed (const juce::KeyPress& keyPress) override
    {
        if (keyPress.isKeyCode (juce::KeyPress::spaceKey)
            && keyPress.getModifiers().isShiftDown())
        {
            playOrResume();
            return true;
        }

        if (keyPress == juce::KeyPress::spaceKey)
        {
            if (isPlaying())
                pausePlayback();
            else if (analysis != nullptr)
                playOrResume();
            return true;
        }

        return false;
    }

private:
    class DeckDisplay final : public juce::Component
    {
    public:
        explicit DeckDisplay (Content& ownerToUse)
            : owner (ownerToUse), seekOverlay (*this), visualizerShield (*this)
        {
            addAndMakeVisible (seekOverlay);
            addAndMakeVisible (visualizerShield);
        }

        void setAnalysis (std::shared_ptr<const AudioFileData> data)
        {
            analysis = std::move (data);
            viewStartSeconds = 0.0;
            zoom = 1.0;
            positionSeconds = 0.0;
            buildSpectrumImage();
            repaint();
        }

        void clearAnalysis()
        {
            analysis.reset();
            spectrumImage = {};
            zoom = 1.0;
            viewStartSeconds = 0.0;
            positionSeconds = 0.0;
            repaint();
        }

        void setPosition (double seconds, bool follow)
        {
            positionSeconds = analysis != nullptr
                ? std::clamp (seconds, 0.0, analysis->durationSeconds) : 0.0;
            if (follow && zoom > 1.0 && ! seekOverlay.isInteracting())
            {
                const double range = visibleDuration();
                const double leftFollowPoint = viewStartSeconds + range * 0.2;
                const double rightFollowPoint = viewStartSeconds + range * 0.8;
                if (positionSeconds < leftFollowPoint)
                    viewStartSeconds = std::clamp (positionSeconds - range * 0.2, 0.0,
                        std::max (0.0, analysis->durationSeconds - range));
                else if (positionSeconds > rightFollowPoint)
                    viewStartSeconds = std::clamp (positionSeconds - range * 0.8, 0.0,
                        std::max (0.0, analysis->durationSeconds - range));
            }
            repaint();
        }

        void zoomBy (double factor)
        {
            if (analysis == nullptr || analysis->durationSeconds <= 0.0)
                return;
            const double range = visibleDuration();
            const double anchor = viewStartSeconds + range * 0.5;
            const double maxZoom = std::max (1.0, analysis->durationSeconds / 0.35);
            zoom = std::clamp (zoom * factor, 1.0, maxZoom);
            viewStartSeconds = std::clamp (anchor - visibleDuration() * 0.5, 0.0,
                std::max (0.0, analysis->durationSeconds - visibleDuration()));
            repaint();
        }

        void fitToTrack()
        {
            zoom = 1.0;
            viewStartSeconds = 0.0;
            repaint();
        }

        void resized() override
        {
            seekOverlay.setBounds (waveformBounds());
            visualizerShield.setBounds (visualizerBounds());
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel());
            auto bounds = getLocalBounds().reduced (4);
            if (bounds.isEmpty())
                return;
            const int spectrumHeight = std::min (170, std::max (120, bounds.getHeight() / 2));
            auto waveformSection = bounds.removeFromTop (
                std::max (80, bounds.getHeight() - spectrumHeight - 8));
            const auto timeRuler = waveformSection.removeFromTop (21);
            const auto waveformArea = waveformSection.reduced (0, 1);
            bounds.removeFromTop (7);
            auto spectrumSection = bounds;

            drawTimeRuler (g, timeRuler);
            drawWaveform (g, waveformArea);
            auto spectrumArea = spectrumSection;
            auto liveArea = spectrumArea.removeFromRight (
                std::min (350, std::max (240, spectrumArea.getWidth() / 3)));
            spectrumArea.removeFromRight (8);
            drawSpectrum (g, spectrumArea);
            drawLiveSpectrum (g, liveArea);
        }

        void mouseWheelMove (const juce::MouseEvent& event,
                             const juce::MouseWheelDetails& wheel) override
        {
            zoomAt (event.position.x, wheel.deltaY);
        }

        void zoomAt (float x, float delta)
        {
            if (analysis == nullptr || analysis->durationSeconds <= 0.0 || delta == 0.0f)
                return;
            const auto area = waveformBounds();
            const double oldRange = visibleDuration();
            const double proportion = std::clamp (
                (x - area.getX()) / std::max (1.0, static_cast<double> (area.getWidth())),
                0.0, 1.0);
            const double anchor = viewStartSeconds + proportion * oldRange;
            const double maxZoom = std::max (1.0, analysis->durationSeconds / 0.35);
            zoom = std::clamp (zoom * (delta > 0.0f ? 1.25 : 0.8), 1.0, maxZoom);
            viewStartSeconds = std::clamp (anchor - proportion * visibleDuration(), 0.0,
                std::max (0.0, analysis->durationSeconds - visibleDuration()));
            repaint();
        }

        void panBy (float deltaX)
        {
            const auto area = waveformBounds();
            if (analysis == nullptr || area.getWidth() <= 0)
                return;
            viewStartSeconds = std::clamp (
                viewStartSeconds - deltaX * visibleDuration() / area.getWidth(),
                0.0, std::max (0.0, analysis->durationSeconds - visibleDuration()));
            repaint();
        }

        void seekAt (float x, bool keepPlaying)
        {
            const auto area = waveformBounds();
            if (analysis == nullptr || area.getWidth() <= 0)
                return;
            const double proportion = std::clamp (
                (x - area.getX()) / static_cast<double> (area.getWidth()), 0.0, 1.0);
            owner.requestSeek (viewStartSeconds + proportion * visibleDuration(), keepPlaying);
        }

    private:
        double visibleDuration() const
        {
            return analysis != nullptr && analysis->durationSeconds > 0.0
                ? analysis->durationSeconds / zoom : 0.0;
        }

        juce::Rectangle<int> waveformBounds() const
        {
            auto bounds = getLocalBounds().reduced (4);
            const int spectrumHeight = std::min (170, std::max (120, bounds.getHeight() / 2));
            auto waveformSection = bounds.removeFromTop (
                std::max (80, bounds.getHeight() - spectrumHeight - 8));
            waveformSection.removeFromTop (21);
            return waveformSection.reduced (0, 1);
        }

        juce::Rectangle<int> visualizerBounds() const
        {
            auto bounds = getLocalBounds().reduced (4);
            if (bounds.isEmpty())
                return {};
            const int spectrumHeight = std::min (170, std::max (120, bounds.getHeight() / 2));
            bounds.removeFromTop (
                std::max (80, bounds.getHeight() - spectrumHeight - 8));
            bounds.removeFromTop (7);
            return bounds;
        }

        void drawTimeRuler (juce::Graphics& g, juce::Rectangle<int> area) const
        {
            g.setColour (theme::panelLight());
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
            if (analysis == nullptr || analysis->durationSeconds <= 0.0)
                return;

            const double range = visibleDuration();
            const int targetTicks = std::max (1, area.getWidth() / 105);
            const double rawInterval = range / targetTicks;
            const double power = std::pow (10.0, std::floor (std::log10 (
                std::max (0.001, rawInterval))));
            const double normalized = rawInterval / power;
            const double interval = (normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0
                                      : normalized <= 5.0 ? 5.0 : 10.0) * power;
            const double first = std::ceil (viewStartSeconds / interval) * interval;
            g.setFont (juce::FontOptions (10.0f));
            for (double seconds = first;
                 seconds <= viewStartSeconds + range && interval > 0.0;
                 seconds += interval)
            {
                const float x = static_cast<float> (area.getX()
                    + (seconds - viewStartSeconds) / range * area.getWidth());
                g.setColour (theme::textDim().withAlpha (0.65f));
                g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (area.getY() + 13),
                                    static_cast<float> (area.getBottom()));
                g.drawText (formatTime (seconds),
                    juce::Rectangle<int> (juce::roundToInt (x) + 3, area.getY(),
                                          48, area.getHeight() - 3),
                    juce::Justification::centredLeft);
            }
        }

        void drawWaveform (juce::Graphics& g, juce::Rectangle<int> area) const
        {
            g.setColour (juce::Colour (0xff161616));
            g.fillRect (area);
            if (analysis == nullptr || analysis->waveformColumns.empty() || area.isEmpty())
            {
                g.setColour (theme::textDim());
                g.setFont (14.0f);
                g.drawText ("Load audio to view the waveform.",
                            area, juce::Justification::centred);
                return;
            }

            const double range = visibleDuration();
            if (range <= 0.0 || analysis->durationSeconds <= 0.0)
                return;

            const auto& columns = analysis->waveformColumns;
            const float centreY = static_cast<float> (area.getCentreY());
            const float amplitudeScale = static_cast<float> (area.getHeight()) * 0.46f;
            g.setColour (juce::Colour (0xff353535));
            g.drawHorizontalLine (juce::roundToInt (centreY), static_cast<float> (area.getX()),
                                  static_cast<float> (area.getRight()));

            constexpr int columnStep = 2;
            constexpr float barWidth = 1.5f;
            const int displayColumns = (area.getWidth() + columnStep - 1) / columnStep;
            for (int displayColumn = 0; displayColumn < displayColumns; ++displayColumn)
            {
                const int xOffset = displayColumn * columnStep;
                const int remainingWidth = area.getWidth() - xOffset;
                const float x = static_cast<float> (area.getX() + xOffset);
                const float width = std::min (barWidth, static_cast<float> (remainingWidth));
                const double startRatio = static_cast<double> (xOffset) / area.getWidth();
                const double endRatio = static_cast<double> (
                    std::min (xOffset + columnStep, area.getWidth())) / area.getWidth();
                const double startSeconds = viewStartSeconds + startRatio * range;
                const double endSeconds = viewStartSeconds + endRatio * range;
                const double sourceStart = startSeconds / analysis->durationSeconds
                    * columns.size();
                const double sourceEnd = endSeconds / analysis->durationSeconds
                    * columns.size();
                const auto first = std::min (columns.size() - 1, static_cast<std::size_t> (
                    sourceStart));
                const auto last = std::max (first + 1, std::min (columns.size(),
                    static_cast<std::size_t> (std::ceil (sourceEnd))));

                float peak = 0.0f;
                double weightedCentroid = 0.0;
                double weightedFlatness = 0.0;
                double totalWeight = 0.0;
                for (auto index = first; index < last; ++index)
                {
                    const auto& column = columns[index];
                    peak = std::max (peak, column.peak);
                    weightedCentroid += column.centroid * column.peak;
                    weightedFlatness += column.flatness * column.peak;
                    totalWeight += column.peak;
                }
                float centroid = totalWeight > 0.0
                    ? static_cast<float> (weightedCentroid / totalWeight) : 0.0f;
                float flatness = totalWeight > 0.0
                    ? juce::jlimit (0.0f, 1.0f,
                        static_cast<float> (weightedFlatness / totalWeight)) : 0.0f;

                const double sourceSpan = sourceEnd - sourceStart;
                if (sourceSpan < 2.0)
                {
                    const double samplePosition = std::clamp (
                        (sourceStart + sourceEnd) * 0.5 - 0.5, 0.0,
                        static_cast<double> (columns.size() - 1));
                    const auto sampleBefore = static_cast<std::size_t> (samplePosition);
                    const auto sampleAfter = std::min (sampleBefore + 1, columns.size() - 1);
                    const float interpolation = static_cast<float> (
                        samplePosition - static_cast<double> (sampleBefore));
                    const auto interpolate = [interpolation] (float a, float b)
                    {
                        return a + (b - a) * interpolation;
                    };
                    const float blend = static_cast<float> ((2.0 - sourceSpan) / 2.0);
                    peak += (interpolate (columns[sampleBefore].peak,
                                          columns[sampleAfter].peak) - peak) * blend;
                    const float sampledCentroid = interpolate (
                        columns[sampleBefore].centroid, columns[sampleAfter].centroid);
                    const float sampledFlatness = interpolate (
                        columns[sampleBefore].flatness, columns[sampleAfter].flatness);
                    const float blendedCentroid = centroid
                        + (sampledCentroid - centroid) * blend;
                    const float blendedFlatness = flatness
                        + (sampledFlatness - flatness) * blend;
                    centroid = blendedCentroid;
                    flatness = juce::jlimit (0.0f, 1.0f, blendedFlatness);
                }

                if (peak < 0.001f || centroid < 40.0f)
                {
                    g.setColour (juce::Colour (0xffa45bff));
                    g.fillRect (x, centreY - 0.5f, width, 1.0f);
                    continue;
                }

                const float frequency = juce::jlimit (40.0f, 16000.0f, centroid);
                const float noiseAmount = juce::jlimit (0.0f, 1.0f,
                    (flatness - 0.55f) / 0.30f);
                const float peakLevel = juce::jlimit (0.0f, 1.0f, peak);
                const float transientLevel = peakLevel * peakLevel * peakLevel;
                const float saturation = juce::jmap (noiseAmount, 0.9f, 0.04f);
                const float amplitudeLightness = juce::jmap (
                    transientLevel, 0.28f, 0.68f);
                const float lightness = juce::jmap (noiseAmount, amplitudeLightness, 0.88f);
                g.setColour (juce::Colour::fromHSL (
                    waveformHue (frequency), saturation, lightness, 1.0f));
                const float halfHeight = std::max (0.75f, transientLevel * amplitudeScale);
                g.fillRect (x, centreY - halfHeight, width, halfHeight * 2.0f);
            }

            const float playheadX = static_cast<float> (area.getX()
                + (positionSeconds - viewStartSeconds) / range * area.getWidth());
            if (playheadX >= area.getX() && playheadX <= area.getRight())
            {
                g.setColour (juce::Colour (0xffe33a45));
                g.drawVerticalLine (juce::roundToInt (playheadX),
                                    static_cast<float> (area.getY()),
                                    static_cast<float> (area.getBottom()));
            }
        }

        void drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area) const
        {
            g.setColour (juce::Colour (0xff07121d));
            g.fillRoundedRectangle (area.toFloat(), 4.0f);
            if (analysis == nullptr || spectrumImage.isNull()
                || area.getWidth() <= 48 || area.getHeight() <= 0)
                return;

            auto labels = area.removeFromLeft (48);
            auto heatmap = area.reduced (1);
            const double fileDuration = analysis->durationSeconds;
            const int columns = analysis->spectrumColumns;
            if (fileDuration > 0.0)
            {
                int firstColumn = std::clamp (static_cast<int> (
                    viewStartSeconds / fileDuration * columns), 0, columns - 1);
                int lastColumn = std::clamp (static_cast<int> (std::ceil (
                    (viewStartSeconds + visibleDuration()) / fileDuration * columns)),
                    firstColumn + 1, columns);
                g.drawImage (spectrumImage, heatmap.getX(), heatmap.getY(),
                             heatmap.getWidth(), heatmap.getHeight(),
                             firstColumn, 0, lastColumn - firstColumn,
                             spectrumImage.getHeight());
            }

            g.setColour (theme::textDim().withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.5f));
            g.drawText ("8 kHz", labels.withY (labels.getY()).withHeight (14),
                        juce::Justification::centredLeft);
            g.drawText ("1 kHz", labels.withY (heatmap.getY()
                              + juce::roundToInt (0.39f * heatmap.getHeight()) - 7)
                              .withHeight (14),
                        juce::Justification::centredLeft);
            g.drawText ("20 Hz", labels.withY (labels.getBottom() - 14).withHeight (14),
                        juce::Justification::centredLeft);
            for (const float ratio : { 0.18f, 0.42f })
            {
                const float y = static_cast<float> (heatmap.getBottom())
                              - ratio * heatmap.getHeight();
                g.setColour (theme::text().withAlpha (0.35f));
                g.drawHorizontalLine (juce::roundToInt (y),
                                      static_cast<float> (heatmap.getX()),
                                      static_cast<float> (heatmap.getRight()));
            }
            const float playheadX = static_cast<float> (heatmap.getX()
                + (positionSeconds - viewStartSeconds) / visibleDuration() * heatmap.getWidth());
            if (playheadX >= heatmap.getX() && playheadX <= heatmap.getRight())
            {
                g.setColour (juce::Colour (0xffffbd59).withAlpha (0.9f));
                g.drawVerticalLine (juce::roundToInt (playheadX),
                                    static_cast<float> (heatmap.getY()),
                                    static_cast<float> (heatmap.getBottom()));
            }
        }

        void buildSpectrumImage()
        {
            if (analysis == nullptr || analysis->spectrumColumns <= 0
                || analysis->spectrumRows <= 0
                || analysis->spectrum.size() < static_cast<std::size_t> (
                    analysis->spectrumColumns * analysis->spectrumRows))
            {
                spectrumImage = {};
                return;
            }

            const int columns = analysis->spectrumColumns;
            const int rows = analysis->spectrumRows;
            spectrumImage = juce::Image (juce::Image::RGB, columns, rows, true);
            for (int row = 0; row < rows; ++row)
            {
                const float frequency = static_cast<float> (row)
                                      / static_cast<float> (std::max (1, rows - 1));
                for (int column = 0; column < columns; ++column)
                {
                    const auto index = static_cast<std::size_t> (row * columns + column);
                    const float intensity = juce::jlimit (0.0f, 1.0f,
                                                            analysis->spectrum[index]);
                    const auto colour = intensity < 0.035f
                        ? juce::Colour (0xff07121d) : spectrumColour (frequency, intensity);
                    spectrumImage.setPixelAt (column, rows - row - 1, colour);
                }
            }
        }

        void drawLiveSpectrum (juce::Graphics& g, juce::Rectangle<int> area) const
                {
                    g.setColour (juce::Colour (0xff07121d));
                    g.fillRoundedRectangle (area.toFloat(), 4.0f);
                    g.setColour (theme::border());
                    g.drawRoundedRectangle (area.toFloat(), 4.0f, 1.0f);
                    if (analysis == nullptr || analysis->spectrumColumns <= 0
                        || analysis->spectrumRows < 2 || analysis->durationSeconds <= 0.0
                        || analysis->spectrum.size()
                            < static_cast<std::size_t> (analysis->spectrumColumns * analysis->spectrumRows))
                    {
                        g.setColour (theme::textDim());
                        g.setFont (11.0f);
                        g.drawText ("Live Spectrum", area, juce::Justification::centred);
                        return;
                    }

                    auto plot = area.reduced (8, 10);
                    if (plot.getWidth() < 80 || plot.getHeight() < 45)
                        return;

                    constexpr int historyCount = 14;
                    const int bandCount = analysis->spectrumRows;
                    const int columnCount = analysis->spectrumColumns;
                    const double frameSpacing = std::max (
                        0.04, analysis->durationSeconds / std::max (1, columnCount - 1));
                    const float depthWidth = plot.getWidth() * 0.22f;
                    const float depthHeight = plot.getHeight() * 0.2f;
                    const float frequencyWidth = plot.getWidth() - depthWidth - 3.0f;
                    const float amplitudeHeight = plot.getHeight() * 0.62f;
                    const float floorY = static_cast<float> (plot.getBottom() - 2);

                    const auto intensityAt = [this] (double seconds, int band)
                    {
                        if (seconds < 0.0 || seconds > analysis->durationSeconds)
                            return 0.0f;
                        const double columnPosition = seconds / analysis->durationSeconds
                                                    * (analysis->spectrumColumns - 1);
                        const int first = std::clamp (static_cast<int> (columnPosition),
                                                      0, analysis->spectrumColumns - 1);
                        const int second = std::min (first + 1, analysis->spectrumColumns - 1);
                        const float fraction = static_cast<float> (columnPosition - first);
                        const auto firstIndex = static_cast<std::size_t> (
                            band * analysis->spectrumColumns + first);
                        const auto secondIndex = static_cast<std::size_t> (
                            band * analysis->spectrumColumns + second);
                        return juce::jmap (fraction, analysis->spectrum[firstIndex],
                                           analysis->spectrum[secondIndex]);
                    };

                    const auto pointAt = [&] (int slice, int band, float intensity)
                    {
                        const float frequency = static_cast<float> (band) / (bandCount - 1);
                        const float depth = static_cast<float> (historyCount - 1 - slice)
                                          / (historyCount - 1);
                        const float x = static_cast<float> (plot.getX()) + frequency * frequencyWidth
                                      + depth * depthWidth;
                        const float base = floorY - depth * depthHeight;
                        const float y = base - intensity * amplitudeHeight;
                        return juce::Point<float> (x, y);
                    };

                    g.saveState();
                    g.reduceClipRegion (plot);
                    g.setColour (theme::border().withAlpha (0.32f));
                    for (int line = 0; line <= 8; ++line)
                    {
                        const float frequency = static_cast<float> (line) / 8.0f;
                        const float x = static_cast<float> (plot.getX()) + frequency * frequencyWidth;
                        g.drawLine (x, floorY, x + depthWidth, floorY - depthHeight, 0.7f);
                    }
                    for (int slice = 0; slice <= 6; ++slice)
                    {
                        const float depth = static_cast<float> (slice) / 6.0f;
                        const float offsetX = depth * depthWidth;
                        const float offsetY = depth * depthHeight;
                        g.drawLine (static_cast<float> (plot.getX()) + offsetX, floorY - offsetY,
                                    static_cast<float> (plot.getX()) + frequencyWidth + offsetX,
                                    floorY - offsetY, 0.7f);
                    }

                    for (int slice = 0; slice < historyCount - 1; ++slice)
                    {
                        const double seconds = positionSeconds
                            - static_cast<double> (historyCount - 1 - slice) * frameSpacing;
                        for (int band = 0; band < bandCount - 1; ++band)
                        {
                            const float a = intensityAt (seconds, band);
                            const float b = intensityAt (seconds, band + 1);
                            const float c = intensityAt (seconds + frameSpacing, band + 1);
                            const float d = intensityAt (seconds + frameSpacing, band);
                            const float intensity = (a + b + c + d) * 0.25f;
                            if (intensity < 0.035f)
                                continue;

                            juce::Path surface;
                            surface.startNewSubPath (pointAt (slice, band, a));
                            surface.lineTo (pointAt (slice, band + 1, b));
                            surface.lineTo (pointAt (slice + 1, band + 1, c));
                            surface.lineTo (pointAt (slice + 1, band, d));
                            surface.closeSubPath();
                            const float frequency = static_cast<float> (band) / (bandCount - 1);
                            const float depth = static_cast<float> (historyCount - 1 - slice)
                                              / (historyCount - 1);
                            g.setColour (spectrumColour (frequency, intensity)
                                             .withAlpha (0.5f + (1.0f - depth) * 0.4f));
                            g.fillPath (surface);
                            g.setColour (theme::text().withAlpha (0.14f + intensity * 0.22f));
                            g.strokePath (surface, juce::PathStrokeType (0.55f));
                        }
                    }

                    juce::Path currentOutline;
                    const double currentHistoryTime = positionSeconds;
                    for (int band = 0; band < bandCount; ++band)
                    {
                        const auto point = pointAt (historyCount - 1, band,
                                                     intensityAt (currentHistoryTime, band));
                        if (band == 0)
                            currentOutline.startNewSubPath (point);
                        else
                            currentOutline.lineTo (point);
                    }
                    g.setColour (juce::Colour (0xffffbd59).withAlpha (0.9f));
                    g.strokePath (currentOutline, juce::PathStrokeType (1.4f));
                    g.restoreState();

                    g.setColour (owner.isPlaying() ? juce::Colour (0xff36e3cf)
                                                    : theme::textDim().withAlpha (0.55f));
                    g.fillEllipse (static_cast<float> (area.getRight() - 12),
                                   static_cast<float> (area.getY() + 7), 5.0f, 5.0f);
                }

        Content& owner;
        class SeekOverlay final : public juce::Component
        {
        public:
            explicit SeekOverlay (DeckDisplay& deckToUse) : deck (deckToUse) {}

            void paint (juce::Graphics&) override {}
            bool isInteracting() const { return panning || scrubbing; }
            bool hitTest (int x, int y) override
            {
                return deck.waveformBounds().contains (x + getX(), y + getY());
            }

            void mouseWheelMove (const juce::MouseEvent& event,
                                 const juce::MouseWheelDetails& wheel) override
            {
                deck.zoomAt (event.getEventRelativeTo (&deck).position.x, wheel.deltaY);
            }

            void mouseDown (const juce::MouseEvent& event) override
            {
                deck.owner.grabKeyboardFocus();
                resumeAfterScrub = deck.owner.shouldKeepPlayingAfterSeek();
                if (event.mods.isShiftDown())
                {
                    panning = true;
                    resumeAfterScrub = false;
                    lastX = event.position.x;
                    return;
                }
                scrubbing = true;
                deck.seekAt (event.getEventRelativeTo (&deck).position.x, false);
            }

            void mouseDrag (const juce::MouseEvent& event) override
            {
                if (panning)
                {
                    deck.panBy (event.position.x - lastX);
                    lastX = event.position.x;
                    return;
                }
                if (scrubbing)
                    deck.seekAt (event.getEventRelativeTo (&deck).position.x, false);
            }

            void mouseUp (const juce::MouseEvent& event) override
            {
                if (scrubbing && resumeAfterScrub)
                    deck.seekAt (event.getEventRelativeTo (&deck).position.x, true);
                panning = false;
                scrubbing = false;
                resumeAfterScrub = false;
            }

        private:
            DeckDisplay& deck;
            float lastX = 0.0f;
            bool panning = false, scrubbing = false, resumeAfterScrub = false;
        };

        class VisualizerShield final : public juce::Component
        {
        public:
            explicit VisualizerShield (DeckDisplay& deckToUse) : deck (deckToUse)
            {
                setWantsKeyboardFocus (false);
            }

            void paint (juce::Graphics&) override {}
            void mouseDown (const juce::MouseEvent&) override {}

            void mouseWheelMove (const juce::MouseEvent& event,
                                 const juce::MouseWheelDetails& wheel) override
            {
                deck.zoomAt (event.getEventRelativeTo (&deck).position.x, wheel.deltaY);
            }

        private:
            DeckDisplay& deck;
        };

        SeekOverlay seekOverlay;
        VisualizerShield visualizerShield;
        std::shared_ptr<const AudioFileData> analysis;
        juce::Image spectrumImage;
        double zoom = 1.0, viewStartSeconds = 0.0, positionSeconds = 0.0;
    };

    void openFileChooser()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose Reference Audio", juce::File(),
            "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<Content> safeThis (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis != nullptr)
                    safeThis->requestAnalysis (chooser.getResult());
            });
    }

    void requestAnalysis (const juce::File& file)
    {
        if (! isSupportedAudioFile (file))
        {
            setAnalysisStatus ("Choose a supported audio file.");
            return;
        }
        loadPending = true;
        removeButton.setEnabled (false);
        rescanButton.setEnabled (false);
        fileName.setText (file.getFileName(), juce::dontSendNotification);
        setAnalysisStatus ("Loading waveform and spectrum...");
        if (owner.onAnalyzeRequested)
            owner.onAnalyzeRequested (file);
        else
        {
            loadPending = false;
            setAnalysisStatus ("Analysis is unavailable.");
        }
    }

    void rescanAnalysis()
    {
        if (analysis == nullptr || loadPending)
            return;
        requestAnalysis (analysis->file);
    }

    void clearTrack()
    {
        if (analysis == nullptr || loadPending)
            return;
        if (owner.onStopRequested)
            owner.onStopRequested();
        analysis.reset();
        bpmValue = 0.0;
        displayedPosition = 0.0;
        hasPlaybackStarted = false;
        isPaused = false;
        fileName.setText ("No track loaded.", juce::dontSendNotification);
        bpm.setText ("--", juce::dontSendNotification);
        key.setText ("--", juce::dontSendNotification);
        duration.setText ("00:00", juce::dontSendNotification);
        seekSlider.setRange (0.0, 1.0, 0.0);
        seekSlider.setValue (0.0, juce::dontSendNotification);
        deck.clearAnalysis();
        halveBpmButton.setEnabled (false);
        doubleBpmButton.setEnabled (false);
        rescanButton.setEnabled (false);
        removeButton.setEnabled (false);
        zoomOutButton.setEnabled (false);
        zoomInButton.setEnabled (false);
        fitButton.setEnabled (false);
        stopTimer();
        setAnalysisStatus ("Deck is empty.");
        updateTransport();
    }

    void adjustBpm (double factor)
    {
        if (bpmValue <= 0.0)
            return;
        bpmValue = std::clamp (bpmValue * factor, 1.0, 999.9);
        updateBpm();
    }

    void updateBpm()
    {
        bpm.setText (bpmValue > 0.0 ? juce::String (bpmValue, 1) : "--",
                     juce::dontSendNotification);
    }

    void requestSeek (double seconds, bool keepPlaying = false)
    {
        if (analysis == nullptr)
            return;
        displayedPosition = std::clamp (seconds, 0.0, analysis->durationSeconds);
        deck.setPosition (displayedPosition, false);
        seekSlider.setValue (displayedPosition, juce::dontSendNotification);
        if (owner.onSeekRequested)
            owner.onSeekRequested (displayedPosition);
        if (keepPlaying && ! isPaused)
        {
            if (owner.onResumeRequested)
                owner.onResumeRequested();
            if (! isPlaying() && owner.onPlayRequested)
                owner.onPlayRequested (analysis, displayedPosition);
            hasPlaybackStarted = true;
            isPaused = false;
        }
        updateTransport (false);
    }

    bool shouldKeepPlayingAfterSeek() const
    {
        return isPlaying() || (hasPlaybackStarted && ! isPaused);
    }

    double getPlayheadPosition() const
    {
        return owner.onPlayheadPositionRequested
            ? owner.onPlayheadPositionRequested() : displayedPosition;
    }

    bool isPlaying() const
    {
        return owner.onIsPlayingRequested && owner.onIsPlayingRequested();
    }

    void playOrResume()
    {
        if (analysis == nullptr)
        {
            setAnalysisStatus ("Load audio before starting the deck.");
            return;
        }
        if (isPlaying())
            return;

        double position = displayedPosition;
        if (position >= analysis->durationSeconds - 0.02)
            position = 0.0;
        if (owner.onPlayRequested)
        {
            owner.onPlayRequested (analysis, position);
            hasPlaybackStarted = true;
            isPaused = false;
        }
        else
        {
            setAnalysisStatus ("Playback is unavailable.");
            return;
        }
        displayedPosition = position;
        startTimerHz (60);
        updateTransport();
    }

    void timerCallback() override
    {
        updateTransport();
    }

    void pausePlayback()
    {
        if (owner.onPauseRequested)
            owner.onPauseRequested();
        isPaused = true;
        updateTransport();
    }

    void updateTransport (bool updatePosition = true)
    {
        const bool playing = isPlaying();
        if (updatePosition && (playing || hasPlaybackStarted))
            displayedPosition = getPlayheadPosition();
        if (analysis != nullptr)
            displayedPosition = std::clamp (displayedPosition, 0.0, analysis->durationSeconds);
        deck.setPosition (displayedPosition, playing);
        if (! seekSlider.isMouseButtonDown())
            seekSlider.setValue (displayedPosition, juce::dontSendNotification);
        timeReadout.setText (formatTime (displayedPosition) + " / "
                                 + formatTime (analysis != nullptr ? analysis->durationSeconds : 0.0),
                             juce::dontSendNotification);
        playButton.setButtonText (playing ? "Reproduzindo"
            : (isPaused && analysis != nullptr
               && displayedPosition < analysis->durationSeconds - 0.02 ? "Resume" : "Play"));
        playButton.setEnabled (analysis != nullptr && ! playing);
        pauseButton.setEnabled (analysis != nullptr && playing);
        stopButton.setEnabled (analysis != nullptr
            && (playing || isPaused || displayedPosition > 0.0));
    }

    ReferenceMasterWindow& owner;
    DeckDisplay deck;
    juce::Label title, instructions, fileName, status, bpm, key, duration, timeReadout;
    juce::TextButton openButton, removeButton, rescanButton, halveBpmButton, doubleBpmButton;
    juce::TextButton playButton, pauseButton, stopButton, zoomOutButton, zoomInButton, fitButton;
    juce::Slider seekSlider;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::shared_ptr<const AudioFileData> analysis;
    double bpmValue = 0.0, displayedPosition = 0.0;
    bool hasPlaybackStarted = false, isPaused = false, loadPending = false;
    bool seekSliderShouldResume = false;
};

ReferenceMasterWindow::ReferenceMasterWindow()
    : juce::DocumentWindow ("Reference Master", theme::background(),
                            juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    auto windowContent = std::make_unique<Content> (*this);
    content = windowContent.get();
    setContentOwned (windowContent.release(), true);
    setColour (juce::DocumentWindow::backgroundColourId, theme::background());
    centreWithSize (1080, 700);
}

ReferenceMasterWindow::~ReferenceMasterWindow() = default;

void ReferenceMasterWindow::activeWindowStatusChanged()
{
    juce::DocumentWindow::activeWindowStatusChanged();
    if (isActiveWindow() && content != nullptr)
        content->grabKeyboardFocus();
}

void ReferenceMasterWindow::setAnalysisStatus (const juce::String& text)
{
    if (content != nullptr)
        content->setAnalysisStatus (text);
}

void ReferenceMasterWindow::setAnalysis (const juce::File& file,
                                         std::shared_ptr<const AudioFileData> data)
{
    if (content != nullptr)
        content->setAnalysis (file, std::move (data));
}

void ReferenceMasterWindow::closeButtonPressed()
{
    if (onStopRequested)
        onStopRequested();
    setVisible (false);
}
