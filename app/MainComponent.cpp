#if defined(_WIN32) && ! defined(NOMINMAX)
 #define NOMINMAX
#endif

#if defined(_WIN32)
 #include <windows.h>
#endif

#include "MainComponent.h"
#include "AppModalDialog.h"
#include "OptionsWindow.h"
#include "WindowPlacement.h"
#include "DrumRackEditor.h"
#include "DrumRackProcessor.h"
#include "NativeEffectProcessors.h"
#include "RemotePluginProcessor.h"
#include "ThemeKnobSlider.h"
#include "Recording.h"
#include "MidiClipExporter.h"
#include <juce_dsp/juce_dsp.h>
#include "Icons.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <utility>

namespace
{
bool isPianoRollToggleShortcut (const juce::KeyPress& key)
{
    const bool ctrl = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
    if (! ctrl)
        return false;
    return key.getTextCharacter() == '`'
        || key.getKeyCode() == '`'
        || key.getKeyCode() == 96;
}

void replaceAccentColour (juce::Component& component, int colourId,
                          juce::Colour previousAccent, juce::Colour accent,
                          juce::Colour previousHover, juce::Colour hover)
{
    if (! component.isColourSpecified (colourId))
        return;

    const auto current = component.findColour (colourId, false);
    if (current == previousAccent)
        component.setColour (colourId, accent);
    else if (current == previousHover)
        component.setColour (colourId, hover);
}

void refreshAccentColours (juce::Component& component,
                           juce::Colour previousAccent, juce::Colour accent,
                           juce::Colour previousHover, juce::Colour hover)
{
    if (dynamic_cast<juce::TextButton*> (&component) != nullptr)
        for (const int id : { juce::TextButton::buttonColourId, juce::TextButton::buttonOnColourId,
                              juce::TextButton::textColourOffId, juce::TextButton::textColourOnId })
            replaceAccentColour (component, id, previousAccent, accent, previousHover, hover);

    if (dynamic_cast<juce::Slider*> (&component) != nullptr)
        for (const int id : { juce::Slider::thumbColourId, juce::Slider::trackColourId,
                              juce::Slider::rotarySliderFillColourId,
                              juce::Slider::textBoxHighlightColourId })
            replaceAccentColour (component, id, previousAccent, accent, previousHover, hover);

    if (dynamic_cast<juce::Label*> (&component) != nullptr)
        replaceAccentColour (component, juce::Label::textColourId,
                             previousAccent, accent, previousHover, hover);

    if (dynamic_cast<juce::ToggleButton*> (&component) != nullptr)
        replaceAccentColour (component, juce::ToggleButton::tickColourId,
                             previousAccent, accent, previousHover, hover);

    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            refreshAccentColours (*child, previousAccent, accent, previousHover, hover);

    component.repaint();
}

juce::File userPreferencesFile()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("MaXXXtudio");
    directory.createDirectory();
    return directory.getChildFile ("preferences.xml");
}

bool loadCreateFadesOnClipEdges()
{
    const auto xml = juce::XmlDocument::parse (userPreferencesFile());
    return xml != nullptr && xml->hasTagName ("MAXXXTUDIO_PREFERENCES")
        && xml->getBoolAttribute ("createFadesOnClipEdges", false);
}

double loadArrangementRatio()
{
    const auto xml = juce::XmlDocument::parse (userPreferencesFile());
    const double ratio = xml != nullptr && xml->hasTagName ("MAXXXTUDIO_PREFERENCES")
        ? xml->getDoubleAttribute ("arrangementRatio", 0.42) : 0.42;
    return std::isfinite (ratio) ? std::clamp (ratio, 0.15, 0.75) : 0.42;
}

int loadArrangementHeight()
{
    const auto xml = juce::XmlDocument::parse (userPreferencesFile());
    return xml != nullptr && xml->hasTagName ("MAXXXTUDIO_PREFERENCES")
        ? std::max (-1, xml->getIntAttribute ("arrangementHeight", -1)) : -1;
}

std::unique_ptr<juce::XmlElement> loadUserPreferences()
{
    auto xml = juce::XmlDocument::parse (userPreferencesFile());
    if (xml == nullptr || ! xml->hasTagName ("MAXXXTUDIO_PREFERENCES"))
        xml = std::make_unique<juce::XmlElement> ("MAXXXTUDIO_PREFERENCES");
    return xml;
}

void saveCreateFadesOnClipEdges (bool enabled)
{
    auto xml = loadUserPreferences();
    xml->setAttribute ("createFadesOnClipEdges", enabled);
    if (! xml->writeTo (userPreferencesFile()))
        juce::Logger::writeToLog ("MainComponent: Could not save preferences.");
}

void saveArrangementLayout (double ratio, int height)
{
    auto xml = loadUserPreferences();
    xml->setAttribute ("arrangementRatio", ratio);
    xml->setAttribute ("arrangementHeight", height);
    if (! xml->writeTo (userPreferencesFile()))
        juce::Logger::writeToLog ("MainComponent: Could not save preferences.");
}

std::shared_ptr<AudioFileData> createGroovePreviewAudio (const roll::GrooveTemplate& groove, double bpm)
{
    constexpr double sampleRate = 44100.0;
    constexpr int clickLength = 2205;
    constexpr double maximumDurationSeconds = 30.0;
    const double durationSeconds = 4.0 * 60.0 / bpm;
    if (! std::isfinite (durationSeconds) || durationSeconds <= 0.0
        || durationSeconds > maximumDurationSeconds)
        return {};

    const int sampleCount = static_cast<int> (std::ceil (durationSeconds * sampleRate));
    auto preview = std::make_shared<AudioFileData>();
    preview->sampleRate = sampleRate;
    preview->durationSeconds = durationSeconds;
    preview->samples = std::make_shared<juce::AudioBuffer<float>> (2, sampleCount);
    preview->samples->clear();

    const int slotsPerBeat = std::max (1, static_cast<int> (std::lround (1.0 / groove.gridBeats)));
    for (std::size_t slot = 0; slot < groove.timingOffsets.size(); ++slot)
    {
        const double beat = std::max (0.0, static_cast<double> (slot) * groove.gridBeats
                                           + groove.timingOffsets[slot] * groove.timingAmount);
        const int start = static_cast<int> (std::lround (beat * 60.0 / bpm * sampleRate));
        if (start >= sampleCount)
            continue;

        const bool accented = static_cast<int> (slot) % slotsPerBeat == 0;
        const double frequency = accented ? 1320.0 : 880.0;
        double velocity = 1.0;
        if (groove.velocityMultipliers.size() == groove.timingOffsets.size())
            velocity += (groove.velocityMultipliers[slot] - 1.0) * groove.velocityAmount;
        const float amplitude = static_cast<float> ((accented ? 0.75 : 0.48) * velocity);

        for (int sample = 0; sample < clickLength && start + sample < sampleCount; ++sample)
        {
            const double envelope = std::exp (-static_cast<double> (sample) / (sampleRate * 0.006));
            const double phase = juce::MathConstants<double>::twoPi * frequency * sample / sampleRate;
            const float value = amplitude * static_cast<float> (envelope * std::sin (phase));
            for (int channel = 0; channel < preview->samples->getNumChannels(); ++channel)
                preview->samples->addSample (channel, start + sample, value);
        }
    }

    return preview;
}

bool isProjectFile (const juce::File& file)
{
    const auto extension = file.getFileExtension();
    return extension.equalsIgnoreCase (".std");
}

bool isTextEditorFocused()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr
        && (dynamic_cast<juce::TextEditor*> (focused) != nullptr
            || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr);
}

juce::String midiNoteName (int pitch)
{
    static constexpr std::array<const char*, 12> names {{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    }};
    return names[static_cast<std::size_t> (pitch % 12)] + juce::String (pitch / 12 - 2);
}

void setNativeWindowOwner (juce::Component& window, juce::TopLevelWindow* ownerWindow)
{
#if defined(_WIN32)
    if (ownerWindow == nullptr || window.getPeer() == nullptr || ownerWindow->getPeer() == nullptr)
        return;

    const auto nativeWindow = static_cast<HWND> (window.getPeer()->getNativeHandle());
    const auto nativeOwner = static_cast<HWND> (ownerWindow->getPeer()->getNativeHandle());
    if (nativeWindow == nullptr || nativeOwner == nullptr || ! IsWindow (nativeWindow)
        || ! IsWindow (nativeOwner)
        || GetWindowLongPtrW (nativeWindow, GWLP_HWNDPARENT) == reinterpret_cast<LONG_PTR> (nativeOwner))
        return;

    SetWindowLongPtrW (nativeWindow, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR> (nativeOwner));
    SetWindowPos (nativeWindow, nullptr, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    juce::ignoreUnused (window, ownerWindow);
#endif
}

}

class MainComponent::SpectrumAnalyzer final : public juce::Component
{
public:
    SpectrumAnalyzer()
    {
        addAndMakeVisible (foldButton);
        foldButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.setColour (juce::TextButton::buttonColourId, theme::control());
        foldButton.setColour (juce::TextButton::textColourOffId, theme::textDim());
        foldButton.setTooltip ("Minimize or expand Spectrum");
        foldButton.onClick = [this]
        {
            collapsed = ! collapsed;
            foldButton.setButtonText (collapsed ? ">" : "v");
            if (onCollapseChanged)
                onCollapseChanged();
            repaint();
        };
        foldButton.setButtonText ("v");
    }

    std::function<void()> onCollapseChanged;
    bool isCollapsed() const noexcept { return collapsed; }

    void update (const std::array<float, SequencerGraph::visualizationSampleCount>& left,
                 const std::array<float, SequencerGraph::visualizationSampleCount>& right,
                 double sampleRate)
    {
        if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
            sampleRate = 44100.0;

        constexpr int sampleCount = SequencerGraph::visualizationSampleCount;
        constexpr int bandCount = 36;
        std::array<float, sampleCount * 2> fftData {};
        for (int i = 0; i < sampleCount; ++i)
        {
            const float mono = (left[static_cast<std::size_t> (i)]
                              + right[static_cast<std::size_t> (i)]) * 0.5f;
            const float window = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * static_cast<float> (i)
                / static_cast<float> (sampleCount - 1));
            fftData[static_cast<std::size_t> (i)] = mono * window;
        }
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const double maximumFrequency = std::min (18000.0, sampleRate * 0.48);
        for (int band = 0; band < bandCount; ++band)
        {
            const double lowFrequency = 25.0 * std::pow (
                maximumFrequency / 25.0, static_cast<double> (band) / bandCount);
            const double highFrequency = 25.0 * std::pow (
                maximumFrequency / 25.0, static_cast<double> (band + 1) / bandCount);
            const int firstBin = std::clamp (
                static_cast<int> (std::floor (lowFrequency * sampleCount / sampleRate)),
                1, sampleCount / 2 - 1);
            const int lastBin = std::clamp (
                static_cast<int> (std::ceil (highFrequency * sampleCount / sampleRate)),
                firstBin + 1, sampleCount / 2);
            float magnitude = 0.0f;
            for (int bin = firstBin; bin < lastBin; ++bin)
                magnitude = std::max (magnitude, fftData[static_cast<std::size_t> (bin)]);

            const float db = magnitude > 1.0e-6f
                ? 20.0f * std::log10 (magnitude / static_cast<float> (sampleCount)) : -100.0f;
            const float currentLevel = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
            auto& level = bands[static_cast<std::size_t> (band)];
            level = std::max (currentLevel, level * 0.82f);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        theme::drawChromeSurface (g, bounds.reduced (0.5f), 6.0f, theme::panel());
        auto content = bounds.reduced (10, 8);
        const auto heading = content.removeFromTop (18.0f);
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (11.5f, juce::Font::bold));
        g.drawText ("SPECTRUM", heading, juce::Justification::centredLeft);
        if (collapsed)
            return;

        auto plot = content.reduced (3.0f, 2.0f);
        plot.removeFromBottom (12.0f);
        const auto plotBottom = plot.getBottom();
        g.setColour (theme::border().withAlpha (0.7f));
        for (int line = 0; line <= 3; ++line)
        {
            const float y = plot.getY() + static_cast<float> (line) * plot.getHeight() / 3.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }
        g.setColour (theme::control());
        g.fillRoundedRectangle (plot, 3.0f);
        g.setColour (theme::border().withAlpha (0.7f));
        for (int line = 0; line <= 3; ++line)
        {
            const float y = plot.getY() + static_cast<float> (line) * plot.getHeight() / 3.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        juce::Path spectrum;
        spectrum.startNewSubPath (plot.getX(), plotBottom);
        for (int band = 0; band < static_cast<int> (bands.size()); ++band)
        {
            const float x = plot.getX() + static_cast<float> (band) * plot.getWidth()
                          / static_cast<float> (bands.size() - 1);
            const float y = plotBottom - bands[static_cast<std::size_t> (band)] * plot.getHeight();
            spectrum.lineTo (x, y);
        }
        spectrum.lineTo (plot.getRight(), plotBottom);
        spectrum.closeSubPath();
        juce::ColourGradient fill (theme::accent().withAlpha (0.06f), plot.getX(), plot.getY(),
                                   theme::accentHover().withAlpha (0.4f), plot.getRight(),
                                   plot.getBottom(), false);
        g.setGradientFill (fill);
        g.fillPath (spectrum);
        g.setColour (theme::accentHover());
        g.strokePath (spectrum, juce::PathStrokeType (1.5f));

        g.setColour (theme::textDim());
        g.setFont (theme::fontOptions (9.5f));
        g.drawText ("25 Hz", static_cast<int> (plot.getX()), static_cast<int> (plot.getBottom()),
                    42, 12, juce::Justification::centredLeft);
        g.drawText ("1 kHz", static_cast<int> (plot.getCentreX()) - 20,
                    static_cast<int> (plot.getBottom()), 42, 12, juce::Justification::centred);
        g.drawText ("18 kHz", static_cast<int> (plot.getRight()) - 42,
                    static_cast<int> (plot.getBottom()), 42, 12, juce::Justification::centredRight);
    }

    void resized() override
    {
        foldButton.setBounds (getWidth() - 28, 4, 20, 18);
    }

private:
    juce::dsp::FFT fft { 10 };
    std::array<float, 36> bands {};
    juce::TextButton foldButton { "v" };
    bool collapsed = false;
};

class MainComponent::StereoImager final : public juce::Component
{
public:
    StereoImager()
    {
        addAndMakeVisible (foldButton);
        foldButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.setColour (juce::TextButton::buttonColourId, theme::control());
        foldButton.setColour (juce::TextButton::textColourOffId, theme::textDim());
        foldButton.setTooltip ("Minimize or expand Stereo Imager");
        foldButton.onClick = [this]
        {
            collapsed = ! collapsed;
            foldButton.setButtonText (collapsed ? ">" : "v");
            if (onCollapseChanged)
                onCollapseChanged();
            repaint();
        };
        foldButton.setButtonText ("v");
    }

    std::function<void()> onCollapseChanged;
    bool isCollapsed() const noexcept { return collapsed; }

    void update (const std::array<float, SequencerGraph::visualizationSampleCount>& left,
                 const std::array<float, SequencerGraph::visualizationSampleCount>& right)
    {
        leftSamples = left;
        rightSamples = right;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        theme::drawChromeSurface (g, bounds.reduced (0.5f), 6.0f, theme::panel());
        auto content = bounds.reduced (10.0f, 8.0f);
        const auto heading = content.removeFromTop (18.0f);
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (11.5f, juce::Font::bold));
        g.drawText ("STEREO IMAGER", heading, juce::Justification::centredLeft);
        if (collapsed)
            return;

        auto plot = content.reduced (2.0f, 3.0f);
        const float radius = std::max (0.0f, std::min (plot.getWidth(), plot.getHeight()) * 0.44f);
        const juce::Point<float> centre (plot.getCentreX(), plot.getCentreY());
        const juce::Rectangle<float> circle (centre.x - radius, centre.y - radius,
                                             radius * 2.0f, radius * 2.0f);
        g.setColour (theme::control());
        g.fillEllipse (circle);
        g.setColour (theme::border());
        g.drawEllipse (circle, 1.0f);
        g.drawLine (centre.x - radius, centre.y, centre.x + radius, centre.y, 0.8f);
        g.drawLine (centre.x, centre.y - radius, centre.x, centre.y + radius, 0.8f);
        g.setColour (theme::accentHover().withAlpha (0.62f));
        for (int i = 1; i < SequencerGraph::visualizationSampleCount; i += 2)
        {
            const auto previous = stereoPoint (i - 1, centre, radius);
            const auto current = stereoPoint (i, centre, radius);
            g.drawLine (previous.x, previous.y, current.x, current.y, 1.0f);
        }
    }

    void resized() override
    {
        foldButton.setBounds (getWidth() - 28, 4, 20, 18);
    }

private:
    juce::Point<float> stereoPoint (int index, juce::Point<float> centre, float radius) const
    {
        const auto i = static_cast<std::size_t> (index);
        const float left = leftSamples[i];
        const float right = rightSamples[i];
        const float mid = (left + right) * 0.5f;
        const float side = (left - right) * 0.5f;
        return { centre.x + side * radius, centre.y - mid * radius };
    }

    std::array<float, SequencerGraph::visualizationSampleCount> leftSamples {};
    std::array<float, SequencerGraph::visualizationSampleCount> rightSamples {};
    juce::TextButton foldButton { "v" };
    bool collapsed = false;
};

class MainComponent::LoudnessHistory final : public juce::Component,
                                             public juce::SettableTooltipClient
{
public:
    LoudnessHistory()
    {
        setTooltip ("Integrated and 3-second short-term RMS history in dBFS; this is not calibrated LUFS.");
        foldButton.setMouseClickGrabsKeyboardFocus (false);
        foldButton.setColour (juce::TextButton::buttonColourId, theme::control());
        foldButton.setColour (juce::TextButton::textColourOffId, theme::textDim());
        foldButton.setTooltip ("Minimize or expand Loudness History");
        foldButton.onClick = [this]
        {
            collapsed = ! collapsed;
            foldButton.setButtonText (collapsed ? ">" : "v");
            if (onCollapseChanged)
                onCollapseChanged();
            repaint();
        };
        foldButton.setButtonText ("v");
        addAndMakeVisible (foldButton);
    }

    std::function<void()> onCollapseChanged;
    bool isCollapsed() const noexcept { return collapsed; }

    void update (float rms)
    {
        if (! std::isfinite (rms))
            rms = 0.0f;
        const double energy = static_cast<double> (std::max (0.0f, rms))
                           * static_cast<double> (std::max (0.0f, rms));
        if (windowCount == shortTermWindow.size())
            windowEnergy -= shortTermWindow[windowIndex];
        else
            ++windowCount;
        shortTermWindow[windowIndex] = energy;
        windowEnergy += energy;
        windowIndex = (windowIndex + 1) % shortTermWindow.size();

        integratedEnergy += energy;
        ++integratedCount;
        shortTermLevels[historyIndex] = levelFromEnergy (windowEnergy / windowCount);
        integratedLevels[historyIndex] = levelFromEnergy (integratedEnergy / integratedCount);
        historyIndex = (historyIndex + 1) % historySize;
        historyCount = std::min (historyCount + 1, historySize);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        theme::drawChromeSurface (g, bounds.reduced (0.5f), 6.0f, theme::panel());
        auto content = bounds.reduced (10.0f, 7.0f);
        auto heading = content.removeFromTop (18.0f);
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (11.5f, juce::Font::bold));
        g.drawText ("LOUDNESS HISTORY", heading, juce::Justification::centredLeft);
        if (collapsed)
            return;

        auto legend = content.removeFromBottom (14.0f);
        auto plot = content.reduced (2.0f, 2.0f);
        auto levelAxis = plot.removeFromLeft (22.0f);
        g.setColour (theme::control());
        g.fillRoundedRectangle (plot, 3.0f);
        g.setColour (theme::border().withAlpha (0.7f));
        for (int line = 0; line <= 2; ++line)
        {
            const float y = plot.getY() + static_cast<float> (line) * plot.getHeight() * 0.5f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }
        g.setColour (theme::textDim());
        g.setFont (theme::fontOptions (7.5f));
        g.drawText ("0", levelAxis.withY (plot.getY()).withHeight (10.0f),
                    juce::Justification::centredLeft);
        g.drawText ("-30", levelAxis.withY (plot.getCentreY() - 5.0f).withHeight (10.0f),
                    juce::Justification::centredLeft);
        g.drawText ("-60", levelAxis.withY (plot.getBottom() - 10.0f).withHeight (10.0f),
                    juce::Justification::centredLeft);

        juce::Path integratedPath, shortTermPath;
        for (std::size_t sample = 0; sample < historyCount; ++sample)
        {
            const auto index = (historyIndex + historySize - historyCount + sample) % historySize;
            const float x = plot.getX() + static_cast<float> (sample) * plot.getWidth()
                / static_cast<float> (std::max<std::size_t> (1, historyCount - 1));
            const float integratedY = plot.getBottom()
                - integratedLevels[index] * plot.getHeight();
            const float shortTermY = plot.getBottom()
                - shortTermLevels[index] * plot.getHeight();
            if (sample == 0)
            {
                integratedPath.startNewSubPath (x, integratedY);
                shortTermPath.startNewSubPath (x, shortTermY);
            }
            else
            {
                integratedPath.lineTo (x, integratedY);
                shortTermPath.lineTo (x, shortTermY);
            }
        }
        g.setColour (theme::cyan());
        g.strokePath (integratedPath, juce::PathStrokeType (1.2f));
        g.setColour (theme::accent());
        g.strokePath (shortTermPath, juce::PathStrokeType (1.4f));
        g.setFont (theme::fontOptions (8.5f));
        g.setColour (theme::cyan());
        g.drawText ("Integrated", legend.removeFromLeft (68), juce::Justification::centredLeft);
        g.setColour (theme::accent());
        g.drawText ("Short Term", legend, juce::Justification::centredLeft);
    }

    void resized() override
    {
        foldButton.setBounds (getWidth() - 28, 4, 20, 18);
    }

private:
    static float levelFromEnergy (double energy)
    {
        const double rms = std::sqrt (std::max (0.0, energy));
        const double db = rms > 1.0e-7 ? 20.0 * std::log10 (rms) : -72.0;
        return static_cast<float> (juce::jlimit (0.0, 1.0, (db + 60.0) / 60.0));
    }

    static constexpr std::size_t historySize = 120;
    static constexpr std::size_t shortTermWindowSize = 90;
    std::array<float, historySize> shortTermLevels {}, integratedLevels {};
    std::array<double, shortTermWindowSize> shortTermWindow {};
    juce::TextButton foldButton { "v" };
    std::size_t historyIndex = 0, historyCount = 0, windowIndex = 0, windowCount = 0;
    double windowEnergy = 0.0, integratedEnergy = 0.0, integratedCount = 0.0;
    bool collapsed = false;
};

class MainComponent::MixerView final : public juce::Component
{
    class ChannelStrip final : public juce::Component
    {
    public:
        ChannelStrip (ProjectModel& model, int trackIdentifier,
                      juce::String channelName, bool isMaster)
            : project (model), trackId (trackIdentifier), master (isMaster),
              channelLabel (std::move (channelName))
        {
            name.setText (channelLabel, juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, theme::text());
            name.setJustificationType (juce::Justification::centred);
            name.setMinimumHorizontalScale (0.75f);
            addAndMakeVisible (name);

            panLabel.setText ("PAN", juce::dontSendNotification);
            panLabel.setColour (juce::Label::textColourId, theme::textDim());
            panLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (panLabel);

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setTooltip ("Pan: left to right; double-click to center.");
            pan.onDragStart = [this] { project.beginEdit(); };
            pan.onDragEnd = [this] { project.endEdit(); };
            pan.onValueChange = [this] { updatePan(); };
            addAndMakeVisible (pan);

            for (auto* button : { &mute, &solo })
            {
                button->setClickingTogglesState (true);
                button->setColour (juce::TextButton::buttonColourId, theme::control());
                button->setColour (juce::TextButton::buttonOnColourId, theme::accent());
                button->setColour (juce::TextButton::textColourOffId, theme::textDim());
                button->setColour (juce::TextButton::textColourOnId, theme::text());
                addAndMakeVisible (*button);
            }
            mute.setButtonText ("M");
            mute.setTooltip ("Mute channel");
            mute.onClick = [this] { updateMute(); };
            solo.setButtonText ("S");
            solo.setTooltip ("Solo channel");
            solo.setEnabled (! master);
            solo.onClick = [this] { updateSolo(); };

            volumeDb.setColour (juce::Label::textColourId, theme::text());
            volumeDb.setJustificationType (juce::Justification::centred);
            volumeDb.setFont (theme::fontOptions (9.5f));
            addAndMakeVisible (volumeDb);

            volume.setNormalisableRange (MixerFaderSlider::decibelRange (
                master ? 6.0 : roll::gainToDecibels (1.5f)));
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setTooltip ("Channel level in decibels.");
            volume.onDragStart = [this] { project.beginEdit(); };
            volume.onDragEnd = [this] { project.endEdit(); };
            volume.onValueChange = [this] { updateVolume(); };
            addAndMakeVisible (volume);

            syncFromProject();
        }

        void syncFromProject()
        {
            auto* track = getTrack();
            if (track == nullptr)
                return;

            name.setText (master ? "MASTER" : track->name, juce::dontSendNotification);
            channelColour = ArrangementView::colourFor (*track);
            volume.setTrackColour (channelColour);
            pan.setTrackColour (channelColour);
            const float gain = static_cast<float> (track->volume);
            const double db = gain > 0.000001f ? roll::gainToDecibels (gain) : -60.0;
            volume.setValue (db, juce::dontSendNotification);
            pan.setValue (track->pan, juce::dontSendNotification);
            mute.setToggleState (track->mute, juce::dontSendNotification);
            solo.setToggleState (track->solo, juce::dontSendNotification);
            volumeDb.setText (formatDb (db), juce::dontSendNotification);
            repaint();
        }

        void setLevels (float left, float right)
        {
            leftLevel = left;
            rightLevel = right;
            repaint();
        }

        int id() const noexcept { return trackId; }
        const juce::String& label() const noexcept { return channelLabel; }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            theme::drawChromeSurface (g, bounds, 5.0f, theme::panel());
            g.setColour (channelColour);
            g.fillRoundedRectangle (bounds.getX() + 3.0f, bounds.getY() + 3.0f,
                                    bounds.getWidth() - 6.0f, 3.0f, 1.0f);

            const float meterY = 100.0f;
            const float meterHeight = std::max (1.0f, static_cast<float> (getHeight()) - meterY - 24.0f);
            const float meterX = static_cast<float> (getWidth()) - 17.0f;
            theme::drawSegmentedMeter (g, { meterX, meterY, 5.0f, meterHeight },
                                       theme::meterAmount (leftLevel), true, 16);
            theme::drawSegmentedMeter (g, { meterX + 7.0f, meterY, 5.0f, meterHeight },
                                       theme::meterAmount (rightLevel), true, 16);
            g.setColour (theme::textDim());
            g.setFont (theme::fontOptions (7.5f));
            g.drawText ("L", static_cast<int> (meterX), getHeight() - 17, 5, 12,
                        juce::Justification::centred);
            g.drawText ("R", static_cast<int> (meterX + 7.0f), getHeight() - 17, 5, 12,
                        juce::Justification::centred);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (5);
            name.setBounds (area.removeFromTop (20));
            panLabel.setBounds (area.removeFromTop (13));
            pan.setBounds (area.removeFromTop (32).withSizeKeepingCentre (34, 32));
            auto buttons = area.removeFromTop (22);
            mute.setBounds (buttons.removeFromLeft (31));
            buttons.removeFromLeft (4);
            solo.setBounds (buttons.removeFromLeft (31));
            area.removeFromTop (5);
            const int faderBottom = getHeight() - 22;
            volumeDb.setBounds (0, faderBottom, getWidth() - 20, 17);
            volume.setBounds (3, area.getY(), std::max (1, getWidth() - 30),
                              std::max (1, faderBottom - area.getY() - 4));
        }

    private:
        roll::Track* getTrack() const
        {
            return master ? project.masterTrack.get() : project.track (trackId);
        }

        static juce::String formatDb (double db)
        {
            if (db <= -59.5)
                return "-inf";
            return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
        }

        void updateVolume()
        {
            if (auto* track = getTrack())
            {
                track->volume = roll::decibelsToGain (static_cast<float> (volume.getValue()));
                volumeDb.setText (formatDb (volume.getValue()), juce::dontSendNotification);
                project.notifyMixer();
            }
        }

        void updatePan()
        {
            if (auto* track = getTrack())
            {
                track->pan = pan.getValue();
                project.notifyMixer();
            }
        }

        void updateMute()
        {
            if (auto* track = getTrack())
            {
                project.beginEdit();
                track->mute = mute.getToggleState();
                project.notifyMixer();
                project.endEdit();
            }
        }

        void updateSolo()
        {
            if (! master)
                project.setTrackSolo (trackId, solo.getToggleState(), true);
        }

        ProjectModel& project;
        int trackId = 0;
        bool master = false;
        juce::String channelLabel;
        juce::Label name, panLabel, volumeDb;
        MixerPanSlider pan;
        MixerFaderSlider volume;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Colour channelColour = theme::accent();
        float leftLevel = 0.0f, rightLevel = 0.0f;
    };

public:
    MixerView (ProjectModel& model, AudioEngine& audio)
        : project (model), engine (audio)
    {
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (10);
        viewport.setViewedComponent (&content, false);
        addAndMakeVisible (viewport);
        refresh();
    }

    ~MixerView() override
    {
        viewport.setViewedComponent (nullptr, false);
    }

    void refresh()
    {
        std::vector<std::pair<int, juce::String>> channels;
        for (const auto& track : project.tracks)
            if (track != nullptr)
                channels.emplace_back (track->id, track->name);
        channels.emplace_back (0, "MASTER");

        bool rebuild = channels.size() != strips.size();
        for (std::size_t i = 0; ! rebuild && i < channels.size(); ++i)
            rebuild = strips[i]->id() != channels[i].first
                   || strips[i]->label() != channels[i].second;

        if (rebuild)
        {
            content.removeAllChildren();
            strips.clear();
            for (std::size_t i = 0; i < channels.size(); ++i)
            {
                const auto [trackId, channelName] = channels[i];
                auto strip = std::make_unique<ChannelStrip> (
                    project, trackId, channelName, trackId == 0);
                content.addAndMakeVisible (*strip);
                strips.push_back (std::move (strip));
            }
            resized();
        }

        for (auto& strip : strips)
        {
            strip->syncFromProject();
            const auto levels = strip->id() == 0
                ? engine.masterPeakStereo() : engine.peakStereo (strip->id());
            strip->setLevels (levels.first, levels.second);
        }
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        constexpr int stripWidth = 94;
        constexpr int gap = 6;
        const int contentWidth = std::max (1, 8 + static_cast<int> (strips.size()) * (stripWidth + gap));
        content.setSize (contentWidth, std::max (1, viewport.getHeight() - 10));
        int x = 4;
        for (auto& strip : strips)
        {
            strip->setBounds (x, 4, stripWidth, std::max (1, content.getHeight() - 8));
            x += stripWidth + gap;
        }
    }

private:
    ProjectModel& project;
    AudioEngine& engine;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
};

class MainComponent::ActionIndicator final : public juce::Component, private juce::Timer
{
public:
    ActionIndicator()
    {
        message.setColour (juce::Label::textColourId, theme::text());
        message.setFont (theme::fontOptions (13.5f, juce::Font::bold));
        message.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (message);
        setInterceptsMouseClicks (false, false);
    }

    void showMessage (const juce::String& text, icons::Icon icon, bool busy)
    {
        message.setText (text, juce::dontSendNotification);
        const bool wasVisible = isVisible();
        if (busy != loading)
        {
            loading = busy;
            if (loading)
                startTimerHz (30);
            else
                stopTimer();
        }
        currentIcon = icon;
        setVisible (true);
        if (! wasVisible)
            toFront (false);
        repaint();
    }

    void hide()
    {
        if (loading)
        {
            loading = false;
            stopTimer();
        }
        setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel().withAlpha (0.97f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
        g.setColour (theme::border());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
        if (loading)
        {
            drawLoadingCube (g, juce::Rectangle<float> (4.0f, 4.0f, 44.0f, 44.0f));
        }
        else
            if (const auto* icon = icons::get (currentIcon, theme::accent(), 24))
                icon->drawWithin (g, juce::Rectangle<float> (12.0f, 12.0f, 24.0f, 24.0f),
                                  juce::RectanglePlacement::centred, 1.0f);
    }

    void resized() override
    {
        message.setBounds (52, 0, getWidth() - 60, getHeight());
    }

private:
    struct Point3D { float x, y, z; };

    void timerCallback() override
    {
        angle = std::fmod (angle + 0.075f, juce::MathConstants<float>::twoPi);
        repaint();
    }

    void drawLoadingCube (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        constexpr std::array<Point3D, 8> vertices
        {{
            { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
            { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
        }};
        constexpr std::array<std::array<int, 4>, 6> faces
        {{
            {{ 0, 3, 2, 1 }}, {{ 4, 5, 6, 7 }}, {{ 0, 4, 7, 3 }},
            {{ 1, 2, 6, 5 }}, {{ 0, 1, 5, 4 }}, {{ 3, 7, 6, 2 }}
        }};
        constexpr std::array<Point3D, 6> normals
        {{
            { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 },
            { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }
        }};
        const float cosineY = std::cos (angle);
        const float sineY = std::sin (angle);
        const float cosineX = std::cos (angle * 0.72f + 0.35f);
        const float sineX = std::sin (angle * 0.72f + 0.35f);
        std::array<juce::Point<float>, 8> projected;
        std::array<float, 8> depth;

        const auto rotate = [=] (Point3D point)
        {
            const float x = point.x * cosineY + point.z * sineY;
            const float z = -point.x * sineY + point.z * cosineY;
            return Point3D { x, point.y * cosineX - z * sineX,
                             point.y * sineX + z * cosineX };
        };
        const float scale = std::min (bounds.getWidth(), bounds.getHeight()) * 0.36f;
        const auto centre = bounds.getCentre();
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            const auto point = rotate (vertices[i]);
            depth[i] = point.z;
            const float perspective = 1.0f / (1.0f - point.z * 0.12f);
            projected[i] = { centre.x + point.x * scale * perspective,
                             centre.y + point.y * scale * perspective };
        }

        std::array<int, 6> faceOrder { 0, 1, 2, 3, 4, 5 };
        const auto averageDepth = [&] (int face)
        {
            float value = 0.0f;
            for (const int vertex : faces[static_cast<std::size_t> (face)])
                value += depth[static_cast<std::size_t> (vertex)];
            return value * 0.25f;
        };
        std::sort (faceOrder.begin(), faceOrder.end(),
                   [&] (int first, int second) { return averageDepth (first) < averageDepth (second); });

        for (const int faceIndex : faceOrder)
        {
            const auto index = static_cast<std::size_t> (faceIndex);
            juce::Path face;
            for (int corner = 0; corner < 4; ++corner)
            {
                const auto point = projected[static_cast<std::size_t> (faces[index][static_cast<std::size_t> (corner)])];
                if (corner == 0) face.startNewSubPath (point);
                else face.lineTo (point);
            }
            face.closeSubPath();

            const auto normal = rotate (normals[index]);
            const float lighting = std::max (0.0f, -0.45f * normal.x - 0.55f * normal.y + 0.70f * normal.z);
            const auto colour = theme::accent().withMultipliedBrightness (0.48f + lighting * 0.52f);
            g.setColour (colour);
            g.fillPath (face);
            g.setColour (theme::accent().brighter (0.25f).withAlpha (0.85f));
            g.strokePath (face, juce::PathStrokeType (0.8f));
        }
    }

    icons::Icon currentIcon = icons::Icon::Pointer;
    float angle = 0.0f;
    bool loading = false;
    juce::Label message;
};

class MainComponent::ComputerMidiIndicator final : public juce::Component, private juce::Timer
{
public:
    explicit ComputerMidiIndicator (bool alignToParentTopRight = false)
        : alignToParentTopRight (alignToParentTopRight)
    {
        setInterceptsMouseClicks (false, false);
    }

    void showStatus (const juce::String& noteText, const juce::String& detailText, bool noteHeld)
    {
        const bool wasVisible = isVisible();
        const bool changed = primaryText != noteText || secondaryText != detailText || ! wasVisible;
        primaryText = noteText;
        secondaryText = detailText;
        held = noteHeld;
        setVisible (true);
        if (! wasVisible)
            toFront (false);

        if (held)
            stopTimer();
        else if (changed)
            startTimer (1100);

        if (changed)
            repaint();
    }

    void hide()
    {
        stopTimer();
        held = false;
        setVisible (false);
    }

    void alignWithParent() { parentSizeChanged(); }
    const juce::String& getPrimaryText() const { return primaryText; }
    const juce::String& getSecondaryText() const { return secondaryText; }
    bool isHeld() const { return held; }

    void parentSizeChanged() override
    {
        if (! alignToParentTopRight)
            return;

        if (auto* parent = getParentComponent())
        {
            const int width = std::min (224, std::max (0, parent->getWidth() - 16));
            setBounds (std::max (8, parent->getWidth() - width - 8), 8, width, 44);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel().withAlpha (0.97f));
        g.fillRoundedRectangle (bounds, 7.0f);

        auto textBounds = getLocalBounds().reduced (12, 4);
        g.setColour (theme::text());
        g.setFont (theme::fontOptions (13.0f, juce::Font::bold));
        g.drawText (primaryText, textBounds.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim());
        g.setFont (theme::fontOptions (11.5f));
        g.drawText (secondaryText, textBounds, juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (! held)
            setVisible (false);
    }

    juce::String primaryText, secondaryText;
    bool held = false;
    bool alignToParentTopRight = false;
};

class MainComponent::PluginWindowNavigator final : public juce::DocumentWindow,
                                                   private juce::ScrollBar::Listener,
                                                   private juce::KeyListener
{
public:
    PluginWindowNavigator (juce::TopLevelWindow* ownerWindow,
                           std::function<void (int)> onScroll)
        : DocumentWindow ("Plugin window navigation", theme::panel(), 0),
          onScroll (std::move (onScroll))
    {
        auto scrollBarComponent = std::make_unique<juce::ScrollBar> (false);
        scrollBar = scrollBarComponent.get();
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);
        setResizable (false, false);
        setBackgroundColour (theme::panel());
        scrollBar->setAutoHide (false);
        scrollBar->setSingleStepSize (48.0);
        scrollBar->setColour (juce::ScrollBar::backgroundColourId, theme::panel());
        scrollBar->setColour (juce::ScrollBar::trackColourId, theme::border());
        scrollBar->setColour (juce::ScrollBar::thumbColourId, theme::accent());
        scrollBar->setWantsKeyboardFocus (true);
        scrollBar->setMouseClickGrabsKeyboardFocus (true);
        scrollBar->addListener (this);
        scrollBar->addKeyListener (this);
        scrollBar->addMouseListener (this, false);
        setContentOwned (scrollBarComponent.release(), true);
        setContentComponentSize (360, 28);
        setVisible (false);
        setOwnerWindow (ownerWindow);
    }

    ~PluginWindowNavigator() override
    {
        if (scrollBar != nullptr)
        {
            scrollBar->removeListener (this);
            scrollBar->removeKeyListener (this);
            scrollBar->removeMouseListener (this);
        }
    }

    void setOwnerWindow (juce::TopLevelWindow* ownerWindow)
    {
        setNativeWindowOwner (*this, ownerWindow);
    }

    void updateRange (int maximum, int value, int viewportWidth, bool shouldBeVisible)
    {
        if (isVisible() != shouldBeVisible)
            setVisible (shouldBeVisible);

        const auto visibleWidth = static_cast<double> (std::max (1, viewportWidth));
        scrollBar->setRangeLimits (0.0, static_cast<double> (std::max (0, maximum)) + visibleWidth,
                                   juce::dontSendNotification);
        scrollBar->setCurrentRange (static_cast<double> (std::clamp (value, 0, maximum)),
                                    visibleWidth, juce::dontSendNotification);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    void mouseDown (const juce::MouseEvent&) override
    {
        scrollBar->grabKeyboardFocus();
    }

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override
    {
        if (onScroll)
            onScroll (static_cast<int> (std::lround (newRangeStart)));
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (key.isKeyCode (juce::KeyPress::leftKey))
        {
            scrollBar->moveScrollbarInSteps (-1, juce::sendNotification);
            return true;
        }
        if (key.isKeyCode (juce::KeyPress::rightKey))
        {
            scrollBar->moveScrollbarInSteps (1, juce::sendNotification);
            return true;
        }
        return false;
    }

    juce::ScrollBar* scrollBar = nullptr;
    std::function<void (int)> onScroll;
};

struct MainComponent::PluginHistoryRestore
{
    std::vector<PluginState> plugins;
    std::vector<int> trackIds;
    std::size_t nextPlugin = 0;
    int failures = 0;
};

struct MainComponent::PluginCopyOperation
{
    int targetTrackId = -1;
    std::vector<PluginState> devices;
    std::size_t nextDevice = 0;
    int added = 0;
    int failures = 0;
    std::map<int, std::vector<int>> copiedGroups;
    std::map<int, std::size_t> expectedGroupSizes;
};

// Plugin window: keeps the editor visible and provides scrolling as a fallback.
class MainComponent::PluginWindow : public juce::DocumentWindow,
                                    private juce::KeyListener,
                                    private juce::AudioProcessorParameter::Listener,
                                    private juce::Timer
{
public:
    PluginWindow (const juce::String& name,
                  std::unique_ptr<juce::AudioProcessorEditor> editor,
                  juce::TopLevelWindow* ownerWindow,
                  const juce::Rectangle<int>& displayUserArea,
                  int horizontalScrollOffset,
                  int cascadeIndex,
                  const std::vector<roll::Rect>& occupiedWindows,
                  std::function<void()> onActiveWindowStatusChanged,
                  std::function<bool (const juce::KeyPress&)> onTransportKeyPress,
                  int automationTrackId = -1,
                  int automationDeviceId = -1,
                  std::function<void (int, int, int, bool)> onPluginParameterGesture = {})
        : DocumentWindow (name, theme::panel(), DocumentWindow::minimiseButton | DocumentWindow::closeButton)
        , onActiveWindowStatusChanged (std::move (onActiveWindowStatusChanged))
        , onTransportKeyPress (std::move (onTransportKeyPress))
        , automationTrackId (automationTrackId)
        , automationDeviceId (automationDeviceId)
        , onPluginParameterGesture (std::move (onPluginParameterGesture))
    {
        setUsingNativeTitleBar (true);
        setIcon (icons::appIcon());
        editorComponent = editor.get();
        if (automationTrackId >= 0 && automationDeviceId > 0 && onPluginParameterGesture)
            if (auto* processor = editor->getAudioProcessor())
                for (auto* parameter : processor->getParameters())
                    if (parameter != nullptr && parameter->isAutomatable())
                    {
                        parameter->addListener (this);
                        observedParameters.push_back (parameter);
                    }
        if (! observedParameters.empty())
            startTimerHz (30);
        addTransportKeyListenerRecursively (*editorComponent);
        addKeyListener (this);
        const bool editorResizable = editor->isResizable();
        const bool fixedNativeEditor =
            dynamic_cast<UtilityProcessor*> (editor->getAudioProcessor()) != nullptr;

        setResizable (editorResizable && ! fixedNativeEditor, false);
        const int horizontalMargin = std::min (16, std::max (0, (displayUserArea.getWidth() - 1) / 2));
        const int verticalMargin = std::min (16, std::max (0, (displayUserArea.getHeight() - 1) / 2));
        const auto available = displayUserArea.reduced (horizontalMargin, verticalMargin);

        const bool needsViewport = editor->getWidth() > available.getWidth()
                                || editor->getHeight() > available.getHeight();
        if (needsViewport)
        {
            auto viewport = std::make_unique<juce::Viewport>();
            viewport->setScrollBarsShown (true, true);
            viewport->setSize (std::min (editor->getWidth(), available.getWidth()),
                               std::min (editor->getHeight(), available.getHeight()));
            viewport->setViewedComponent (editor.release(), true);
            setContentOwned (viewport.release(), true);
        }
        else
        {
            setContentOwned (editor.release(), true);
        }

        computerMidiIndicator = std::make_unique<ComputerMidiIndicator> (true);
        addAndMakeVisible (computerMidiIndicator.get());
        computerMidiIndicator->setVisible (false);
        computerMidiIndicator->alignWithParent();

        const auto placeWindow = [this, &displayUserArea, horizontalScrollOffset,
                                  cascadeIndex, &occupiedWindows]
        {
            const auto virtualDisplayArea = displayUserArea.translated (horizontalScrollOffset, 0);
            return roll::WindowPlacement::placePluginWindow (
                { 0, 0, getWidth(), getHeight() },
                { virtualDisplayArea.getX(), virtualDisplayArea.getY(),
                  virtualDisplayArea.getWidth(), virtualDisplayArea.getHeight() },
                cascadeIndex, 64, occupiedWindows);
        };
        auto bounds = placeWindow();
        bounds.x -= horizontalScrollOffset;
        setBounds (bounds.x, bounds.y, bounds.width, bounds.height);
        setVisible (true);
        bounds = placeWindow();
        bounds.x -= horizontalScrollOffset;
        setBounds (bounds.x, bounds.y, bounds.width, bounds.height);
        setOwnerWindow (ownerWindow);
    }

    ~PluginWindow() override
    {
        stopTimer();
        for (auto* parameter : observedParameters)
            if (parameter != nullptr)
                parameter->removeListener (this);
        observedParameters.clear();
        if (editorComponent != nullptr)
            removeTransportKeyListenerRecursively (*editorComponent);
        removeKeyListener (this);
    }

    void showComputerMidiStatus (const juce::String& primary,
                                const juce::String& secondary,
                                bool noteHeld)
    {
        computerMidiIndicator->showStatus (primary, secondary, noteHeld);
    }

    void hideComputerMidiStatus() { computerMidiIndicator->hide(); }

    void closeButtonPressed() override { setVisible (false); }

    void setOwnerWindow (juce::TopLevelWindow* ownerWindow)
    {
        setNativeWindowOwner (*this, ownerWindow);
    }

    void moved() override
    {
        juce::DocumentWindow::moved();
        if (onActiveWindowStatusChanged)
            onActiveWindowStatusChanged();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        if (onActiveWindowStatusChanged)
            onActiveWindowStatusChanged();
    }

    void activeWindowStatusChanged() override
    {
        juce::DocumentWindow::activeWindowStatusChanged();
        if (onActiveWindowStatusChanged)
            onActiveWindowStatusChanged();
    }

private:
    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override
    {
        if (gestureIsStarting)
            pendingParameterGestureStart.store (parameterIndex, std::memory_order_release);
        else
            pendingParameterGestureEnd.store (parameterIndex, std::memory_order_release);
    }

    void timerCallback() override
    {
        const int startedParameter = pendingParameterGestureStart.exchange (-1, std::memory_order_acq_rel);
        if (startedParameter >= 0 && isVisible() && ! isMinimised() && isActiveWindow()
            && onPluginParameterGesture)
            onPluginParameterGesture (automationTrackId, automationDeviceId, startedParameter, true);

        const int endedParameter = pendingParameterGestureEnd.exchange (-1, std::memory_order_acq_rel);
        if (endedParameter >= 0 && onPluginParameterGesture)
            onPluginParameterGesture (automationTrackId, automationDeviceId, endedParameter, false);
    }

    void addTransportKeyListenerRecursively (juce::Component& component)
    {
        component.addKeyListener (this);
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i))
                addTransportKeyListenerRecursively (*child);
    }

    void removeTransportKeyListenerRecursively (juce::Component& component)
    {
        component.removeKeyListener (this);
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i))
                removeTransportKeyListenerRecursively (*child);
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        return onTransportKeyPress && onTransportKeyPress (key);
    }

    juce::AudioProcessorEditor* editorComponent = nullptr;
    std::function<void()> onActiveWindowStatusChanged;
    std::function<bool (const juce::KeyPress&)> onTransportKeyPress;
    std::function<void (int, int, int, bool)> onPluginParameterGesture;
    std::vector<juce::AudioProcessorParameter*> observedParameters;
    std::atomic<int> pendingParameterGestureStart { -1 }, pendingParameterGestureEnd { -1 };
    int automationTrackId = -1, automationDeviceId = -1;
    std::unique_ptr<ComputerMidiIndicator> computerMidiIndicator;
};

struct MainComponent::RecordingSession
{
    enum class Kind { Audio, Midi };
    static constexpr std::size_t midiEventCapacity = 65536;

    Kind kind = Kind::Midi;
    std::uint64_t generation = 0;
    int trackId = -1;
    double startBeat = 0.0, startBpm = 120.0, sampleRate = 0.0;
    bool loopWasEnabled = false;
    int audioChannels = 0;
    juce::File audioFile;
    juce::TimeSliceThread writerThread { "Audio Recording Writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<std::int64_t> samplesWritten { 0 };
    std::atomic<std::size_t> midiEventCount { 0 };
    std::array<roll::RecordedMidiEvent, midiEventCapacity> midiEvents {};
    std::atomic<bool> overflow { false }, inputLost { false };
};

MainComponent::MainComponent()
{
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 450);
    tooltipWindow->setColour (juce::TooltipWindow::backgroundColourId, theme::panel());
    tooltipWindow->setColour (juce::TooltipWindow::textColourId, theme::text());
    tooltipWindow->setColour (juce::TooltipWindow::outlineColourId, theme::border());

    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    engine.onPluginCrashed = [this, token] (
        int, int, const juce::String& pluginName, const juce::String& failureReason)
    {
        const auto alive = token.lock();
        if (alive != nullptr && alive->load())
        {
            browser.setStatus ("The plugin " + pluginName
                               + " failed and was terminated; the track was protected.");
            AppModalDialog::show (
                *this, "Plugin Failure",
                "The plugin " + pluginName
                    + " failed and was terminated. The track was protected, and MaXXXtudio is still running.\n\n"
                    + failureReason,
                AppModalDialog::Icon::Error,
                { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
        }
    };

    createFadesOnClipEdges = loadCreateFadesOnClipEdges();
    arrangementRatio = loadArrangementRatio();
    arrangementHeight = loadArrangementHeight();
    addAndMakeVisible (themeBackground);
    themeBackground.toBack();
    addAndMakeVisible (taskStatusBar);
    addAndMakeVisible (transport);
    transport.onFollowToggled = [this] (bool enabled) { setFollowArrangementEnabled (enabled); };
    transport.onProjectMenu = [this] { showProjectMenu(); };
    transport.onOptionsMenu = [this] { showOptionsWindow(); };
    transport.onBpmChanged = [this] (double bpm)
    {
        displayedBpm = bpm;
        arrangement.setBpm (bpm);
        refreshAudioClipEditor();
    };
    addAndMakeVisible (browser);
    addAndMakeVisible (audioBrowser);
    audioBrowser.setVisible (false);
    addAndMakeVisible (arrangement);
    if (auto* masterControl = arrangement.masterControlComponent())
        addAndMakeVisible (masterControl);
    spectrumAnalyzer = std::make_unique<SpectrumAnalyzer>();
    addAndMakeVisible (spectrumAnalyzer.get());
    spectrumAnalyzer->onCollapseChanged = [this] { resized(); };
    stereoImager = std::make_unique<StereoImager>();
    addAndMakeVisible (stereoImager.get());
    stereoImager->onCollapseChanged = [this] { resized(); };
    loudnessHistory = std::make_unique<LoudnessHistory>();
    addAndMakeVisible (loudnessHistory.get());
    loudnessHistory->onCollapseChanged = [this] { resized(); };
    arrangement.setBpm (engine.graph().getBpm());
    arrangement.setAudioDataProvider ([this] (const std::string& path) -> const AudioFileData*
    {
        const auto asset = audioAssets.find (path);
        return asset == audioAssets.end() ? nullptr : asset->second.get();
    });
    addAndMakeVisible (editor);
    addAndMakeVisible (audioClipEditor);
    audioClipEditor.setVisible (false);
    addAndMakeVisible (deviceView);
    mixerView = std::make_unique<MixerView> (project, engine);
    addAndMakeVisible (mixerView.get());
    mixerView->setVisible (false);
    addAndMakeVisible (clipTab);
    addAndMakeVisible (deviceTab);
    addAndMakeVisible (mixerTab);
    actionIndicator = std::make_unique<ActionIndicator>();
    addAndMakeVisible (actionIndicator.get());
    computerMidiIndicator = std::make_unique<ComputerMidiIndicator>();
    addAndMakeVisible (computerMidiIndicator.get());
    computerMidiIndicator->setVisible (false);

    clipControls = { &scaleCaption, &lengthCaption, &gridCaption, &zoomCaption, &rootBox, &modeBox, &lengthBox, &scaleButton,
                     &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton, &groovesButton,
                     &zoomHMinus, &zoomHPlus, &zoomVMinus, &zoomVPlus, &gridLabel, &editor,
                     &pianoLocatorPreviousButton, &pianoLocatorAddButton, &pianoLocatorNextButton,
                     &pianoLocatorDeleteButton };
    for (auto* c : clipControls) addAndMakeVisible (c);

    for (auto* l : { &scaleCaption, &lengthCaption, &gridCaption, &zoomCaption })
    {
        l->setColour (juce::Label::textColourId, theme::textDim());
        l->setFont (theme::fontOptions (12.5f));
    }
    gridLabel.setColour (juce::Label::textColourId, theme::text());

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) rootBox.addItem (names[i], i + 1);
    rootBox.setSelectedId (1, juce::dontSendNotification);
    for (int m = 0; m <= static_cast<int> (roll::Mode::MelodicMinor); ++m)
        modeBox.addItem (juce::String (roll::Scale::nameOf (static_cast<roll::Mode> (m)).data()), m + 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    for (int bars : { 1, 2, 4, 8, 16, 32 }) lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);

    for (auto* b : { &scaleButton, &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, theme::text());
    }
    for (auto* b : { &clipTab, &deviceTab, &mixerTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4801);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent());
        b->setColour (juce::TextButton::textColourOnId, theme::text());
    }
    clipTab.setToggleState (true, juce::dontSendNotification);
    clipTab.onClick = [this] { audioClipEditorOpen = false; showDetail (false); };
    deviceTab.onClick = [this] { showDetail (true); };
    mixerTab.onClick = [this] { showMixer(); };

    snapButton.setToggleState (true, juce::dontSendNotification);
    highlightButton.setToggleState (true, juce::dontSendNotification);
    previewButton.setToggleState (true, juce::dontSendNotification);
    audioClipEditor.onClose = [this]
    {
        audioClipEditorOpen = false;
        showDetail (false);
        resized();
    };
    audioClipEditor.onSourceBpmChanged = [this] (double bpm)
    {
        return project.setAudioClipSourceBpm (project.selectedTrackId, project.selectedAudioClipId, bpm);
    };
    audioClipEditor.onWarpChanged = [this] (bool enabled)
    {
        project.setAudioClipWarpEnabled (project.selectedTrackId, project.selectedAudioClipId, enabled);
    };
    audioClipEditor.onWarpMarkersChanged = [this] (const auto& markers)
    {
        return project.setAudioClipWarpMarkers (
            project.selectedTrackId, project.selectedAudioClipId, markers);
    };
    audioClipEditor.onSplitAtPlayhead = [this]
    {
        if (project.selectedTrackId >= 0 && project.selectedAudioClipId >= 0)
        {
            project.splitAudioClip (project.selectedTrackId,
                                    project.selectedAudioClipId,
                                    arrangement.snapBeat (arrangement.getPlayhead()));
        }
    };
    audioClipEditor.onJoinWithNext = [this]
    {
        if (project.selectedTrackId < 0 || project.selectedAudioClipId < 0)
            return false;
        auto* track = project.track (project.selectedTrackId);
        auto* left = track != nullptr ? track->findAudioClip (project.selectedAudioClipId) : nullptr;
        if (track == nullptr || left == nullptr)
            return false;
        roll::AudioPlacement* right = nullptr;
        int rightId = -1;
        for (auto& candidate : track->audioClips)
            if (candidate != nullptr && candidate.get() != left
                && std::abs (candidate->start - left->end()) < 1.0e-7)
            {
                right = candidate.get();
                rightId = candidate->id;
                break;
            }
        return right != nullptr
            && project.joinAudioClips (project.selectedTrackId, left->id, rightId);
    };
    audioClipEditor.onFadesChanged = [this] (double fadeIn, double fadeOut)
    {
        return project.setAudioClipFades (project.selectedTrackId,
                                          project.selectedAudioClipId,
                                          fadeIn, fadeOut);
    };
    audioClipEditor.onPlaybackChanged = [this] (
        double pitchSemitones, double pitchCents, double gainDb,
        int warpMode, bool reverse)
    {
        return project.setAudioClipPlayback (
            project.selectedTrackId, project.selectedAudioClipId,
            pitchSemitones, pitchCents, gainDb, warpMode, reverse);
    };
    audioClipEditor.onAutomationParameterChanged =
        [this] (juce::String parameterName, double value)
    {
        if (! arrangement.getAutomationMode()) return;
        const int trackId = project.selectedTrackId;
        const int clipId = project.selectedAudioClipId;
        roll::AutomationParameterType type = roll::AutomationParameterType::AudioClipGain;
        if (parameterName == "Pitch")
            type = roll::AutomationParameterType::AudioClipPitch;
        else if (parameterName == "Fade In")
            type = roll::AutomationParameterType::AudioClipFadeIn;
        else if (parameterName == "Fade Out")
            type = roll::AutomationParameterType::AudioClipFadeOut;
        arrangement.selectAudioClipAutomationParameter (trackId, clipId, type, value);
    };
    audioClipEditor.onAutomationGestureStarted = [this]
    {
        if (arrangement.getAutomationMode())
            project.beginEdit();
    };
    audioClipEditor.onAutomationGestureEnded = [this] { project.endEdit(); };

    // Buttons do not steal keyboard focus (Space still toggles play/pause).
    for (auto* b : { &scaleButton, &highlightButton, &foldButton, &foldScaleButton, &drawButton, &snapButton, &previewButton,
                     &groovesButton,
                     &zoomHMinus, &zoomHPlus, &zoomVMinus, &zoomVPlus, &clipTab, &deviceTab,
                     &pianoLocatorPreviousButton, &pianoLocatorAddButton, &pianoLocatorNextButton,
                     &pianoLocatorDeleteButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    pianoLocatorPreviousButton.setTooltip ("Go to Previous Locator");
    pianoLocatorAddButton.setTooltip ("Add Locator at Current Position");
    pianoLocatorNextButton.setTooltip ("Go to Next Locator");
    pianoLocatorDeleteButton.setTooltip ("Remove the selected locator");
    pianoLocatorPreviousButton.onClick = [this] { navigatePianoRollLocator (false); };
    pianoLocatorAddButton.onClick = [this] { addPianoRollLocatorAtPlayhead(); };
    pianoLocatorNextButton.onClick = [this] { navigatePianoRollLocator (true); };
    pianoLocatorDeleteButton.onClick = [this] { deletePianoRollLocator(); };
    pianoLocatorPreviousButton.setEnabled (false);
    pianoLocatorAddButton.setEnabled (false);
    pianoLocatorNextButton.setEnabled (false);
    pianoLocatorDeleteButton.setEnabled (false);

    clipTab.setTooltip ("Edit the audio or MIDI selection in the detail panel");
    deviceTab.setTooltip ("Show devices and effects for the selected track");
    mixerTab.setTooltip ("Show the project mixer");
    scaleButton.setTooltip ("Scale: highlight or constrain notes to the scale");
    highlightButton.setTooltip ("Highlight notes in the active scale");
    foldButton.setTooltip ("Fold: show only pitches used in the clip");
    foldScaleButton.setTooltip ("Scale Fold: show only notes in the scale");
    drawButton.setTooltip ("Draw notes directly in the piano roll");
    snapButton.setTooltip ("Snap: align notes to the active rhythmic grid");
    previewButton.setTooltip ("Preview notes when inserting or moving events");
    groovesButton.setTooltip ("Grooves: apply rhythms and timing offsets to the selection");
    zoomHMinus.setTooltip ("Zoom out horizontally in the piano roll");
    zoomHPlus.setTooltip ("Zoom in horizontally in the piano roll");
    zoomVMinus.setTooltip ("Zoom out vertically on notes");
    zoomVPlus.setTooltip ("Zoom in vertically on notes");
    rootBox.setMouseClickGrabsKeyboardFocus (false);
    modeBox.setMouseClickGrabsKeyboardFocus (false);
    lengthBox.setMouseClickGrabsKeyboardFocus (false);

    rootBox.onChange = [this] { applyScale(); };
    modeBox.onChange = [this] { applyScale(); };
    lengthBox.onChange = [this] { applyLength(); };
    scaleButton.onClick = [this] { model.scaleOn = scaleButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    highlightButton.onClick = [this] { model.highlight = highlightButton.getToggleState(); model.sendChangeMessage(); };
    foldButton.onClick = [this] { model.fold = foldButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    foldScaleButton.onClick = [this] { model.foldScale = foldScaleButton.getToggleState(); model.rebuildRows(); model.sendChangeMessage(); };
    drawButton.onClick = [this] { model.drawMode = drawButton.getToggleState(); };
    snapButton.onClick = [this] { model.snap = snapButton.getToggleState(); model.sendChangeMessage(); };
    previewButton.onClick = [this] { model.previewOn = previewButton.getToggleState(); };
    groovesButton.onClick = [this] { openGrooveBrowser(); };
    zoomHMinus.onClick = [this] { editor.zoomHorizontal (1.0 / 1.25); };
    zoomHPlus.onClick  = [this] { editor.zoomHorizontal (1.25); };
    zoomVMinus.onClick = [this] { editor.zoomVertical (-2); };
    zoomVPlus.onClick  = [this] { editor.zoomVertical (2); };

    // ---- projeto <-> engine ----
    project.onTrackAdded = [this] (int id)
    {
        const auto* track = project.track (id);
        const auto type = track != nullptr ? track->type : roll::TrackType::Midi;
        engine.addTrack (id, type);
        if (track != nullptr && track->frozen)
        {
            engine.setTrackFrozen (id, true);
            publishAudioTrack (id);
        }
        if (type == roll::TrackType::Midi)
            engine.publishNotes (id, {});
        applyMixers();
    };
    project.onTrackRemoved = [this] (int id)
    {
        if (transport.isCountInActive())
            transport.cancelCountIn();
        if (recordingSession != nullptr && recordingSession->trackId == id)
        {
            finishRecording (true);
            setProjectStatus ("Recording cancelled because its target track was removed.");
        }
        closeWindowsOfTrack (id);
        engine.removeTrack (id);
        publishAudioTrack (id);
    };
    project.onTrackNotes = [this] (int id)
    {
        if (auto* t = project.track (id); t != nullptr && t->type == roll::TrackType::Midi)
            engine.publishNotes (id, roll::flatten (*t));
    };
    project.onAudioClipsChanged = [this] (int id) { publishAudioTrack (id); };
    project.onTrackFreezeChanged = [this] (int id)
    {
        if (const auto* track = project.track (id); track != nullptr)
        {
            engine.setTrackType (id, track->type);
            engine.setTrackFrozen (id, track->frozen);
        }
        publishAudioTrack (id);
    };
    project.onMixerChanged = [this] { applyMixers(); };
    project.onAudioClipAutomationChanged = [this] (int trackId)
    {
        publishAudioTrack (trackId);
    };
    project.onSelectionChanged = [this]
    {
        if (transport.isCountInActive())
            transport.cancelCountIn();
        if (recordingSession != nullptr && project.selectedTrackId != recordingSession->trackId)
        {
            finishRecording();
            setProjectStatus ("Recording finalized because the selected track changed.");
        }
        onSelectionChanged();
    };
    project.onLoopChanged = [this] { engine.graph().setLoopRegion (project.loopStart, project.loopEnd); };
    arrangement.onMidiLearnRequested = [this] (int trackId, MidiMappingTarget target)
    {
        requestMidiLearn (trackId, target);
    };
    arrangement.onMidiMappingClearRequested = [this] (int trackId, MidiMappingTarget target)
    {
        clearMidiMapping (trackId, target);
    };
    project.capturePluginHistoryState = [this] (
        const std::vector<int>& trackIds, const std::vector<PluginState>& reusableStates)
    {
        return engine.getPluginStates (trackIds, reusableStates);
    };
    project.restorePluginHistoryState = [this] (
        const std::vector<PluginState>& plugins, const std::vector<int>& trackIds)
    {
        restorePluginHistoryState (plugins, trackIds);
    };
    project.addChangeListener (this);
    model.addChangeListener (this);
    engine.audioDeviceManager().addMidiInputDeviceCallback ({}, this);
    engine.audioDeviceManager().addAudioCallback (this);
    transport.onRecordStart = [this] (bool loopWasEnabled)
    {
        return startRecording (loopWasEnabled);
    };
    transport.onRecordStop = [this] { finishRecording(); };

    // ---- piano roll <-> projeto ----
    model.onNotesChanged = [this] { project.notifyTrackNotes (project.selectedTrackId); project.sendChangeMessage(); };
    model.onEditStarted = [this] { project.beginEdit(); };
    model.onEditFinished = [this] { project.endEdit(); };
    model.onGlobalUndoRedo = [this] (bool redo)
    {
        if (pluginHistoryRestoreInProgress || pluginCopyInProgress)
            return;
        if (redo)
            project.redo();
        else
            project.undo();
    };
    model.canGlobalUndo = [this] { return project.canUndo(); };
    model.canGlobalRedo = [this] { return project.canRedo(); };
    model.onLengthChanged = [this]
    {
        project.notifyTrackNotes (project.selectedTrackId);
        project.sendChangeMessage();
    };
    model.onPreview = [this] (int pitch, bool on)
    {
        if (! on)                 engine.previewNote (project.selectedTrackId, pitch, false);
        else if (model.previewOn) engine.previewNote (project.selectedTrackId, pitch, true);
    };
    model.onInsertMarker = [this] (double local)
    {
        if (! engine.graph().isPlaying())
        {
            const double g = model.clipStartBeats + local;
            transportPosition = g;
            engine.graph().seek (g);
            arrangement.setPlayhead (g);
            editor.setPlayhead (local);
        }
    };
    editor.onSeek = [this] (double local)
    {
        const double g = model.clipStartBeats + local;
        transportPosition = g;
        engine.graph().seek (g);
        arrangement.setPlayhead (g);
        editor.setPlayhead (local);
        if (! engine.graph().isPlaying())
        {
            stopGroovePreview();
            transportPaused = false;
            pendingArrangementSeek = true;
            pendingArrangementSeekBeat = g;
            transport.togglePlay();
        }
        else
            pendingArrangementSeek = false;
    };
    editor.onLocatorSelected = [this] (int locatorId)
    {
        auto* placement = project.selectedClip();
        if (placement == nullptr)
            return;
        const auto& markers = placement->clip.pianoRollLocators();
        const auto marker = std::find_if (markers.begin(), markers.end(),
            [locatorId] (const auto& item) { return item.id == locatorId; });
        if (marker == markers.end())
            return;
        const double localBeat = marker->beat;
        placement->clip.selectPianoRollLocator (locatorId);
        project.sendChangeMessage();
        if (editor.onSeek)
            editor.onSeek (localBeat);
    };
    editor.onLocatorDeleteRequested = [this] (int locatorId) { deletePianoRollLocator (locatorId); };
    editor.onLocatorMoveStarted = [this] (int locatorId)
    {
        if (auto* placement = project.selectedClip())
        {
            project.beginEdit();
            placement->clip.selectPianoRollLocator (locatorId);
            project.sendChangeMessage();
        }
    };
    editor.onLocatorMoved = [this] (int locatorId, double beat)
    {
        if (auto* placement = project.selectedClip();
            placement != nullptr && placement->clip.movePianoRollLocator (locatorId, beat))
            project.sendChangeMessage();
    };
    editor.onLocatorMoveFinished = [this] { project.endEdit(); };
    editor.onTransportToggle = [this] { transport.togglePlay(); };
    editor.isTransportPlaying = [this] { return engine.graph().isPlaying(); };

    arrangement.onSeek = [this] (double g)
    {
        pendingArrangementSeek = true;
        pendingArrangementSeekBeat = g;
        transportPosition = g;
        engine.graph().seek (g);
        arrangement.setPlayhead (g);
        editor.setPlayhead (g - model.clipStartBeats);
    };
    arrangement.onArrangementSeekRequested = [this] (double beats)
    {
        pendingArrangementSeek = ! engine.graph().isPlaying();
        pendingArrangementSeekBeat = beats;
    };
    arrangement.onLocatorLaunchRequested = [this] (double beats)
    {
        if (engine.graph().isPlaying())
            return;
        pendingArrangementSeek = false;
        transportPaused = false;
        if (arrangement.onSeek)
            arrangement.onSeek (beats);
        suppressSongStartOnce = true;
        transport.togglePlay();
    };
    arrangement.meterProvider = [this] (int id) { return engine.peak (id); };
    arrangement.trackStereoPeakProvider = [this] (int id) { return engine.peakStereo (id); };
    arrangement.liveInputProvider = [this] (int trackId)
    {
        const auto* track = project.track (trackId);
        const auto* device = engine.audioDeviceManager().getCurrentAudioDevice();
        if (track == nullptr || track->type != roll::TrackType::Audio
            || project.selectedTrackId != trackId || device == nullptr
            || device->getActiveInputChannels().countNumberOfSetBits() == 0)
            return ArrangementView::LiveInputValues {};

        return ArrangementView::LiveInputValues {
            liveAudioInputPeaks[0].load (std::memory_order_relaxed),
            liveAudioInputPeaks[1].load (std::memory_order_relaxed), true
        };
    };
    arrangement.recordingIndicatorProvider = [this] (int trackId)
    {
        if (transport.isCountInActive() && project.selectedTrackId == trackId)
            return ArrangementView::RecordingIndicatorState::CountIn;
        if (recordingSession == nullptr || recordingSession->trackId != trackId)
            return ArrangementView::RecordingIndicatorState::None;
        return recordingSession->kind == RecordingSession::Kind::Audio
            ? ArrangementView::RecordingIndicatorState::Audio
            : ArrangementView::RecordingIndicatorState::Midi;
    };
    arrangement.masterStereoPeakProvider = [this] { return engine.masterPeakStereo(); };
    arrangement.liveMixerProvider = [this] (int id)
    {
        return ArrangementView::LiveMixerValues {
            engine.mixerGain (id), engine.mixerPan (id), engine.mixerMuted (id)
        };
    };
    arrangement.playbackStateProvider = [this] { return engine.graph().isPlaying(); };
    arrangement.computerMidiKeyboardEnabledProvider = [this]
    {
        return computerMidiKeyboardEnabled;
    };
    arrangement.onMasterPeakReset = [this] { engine.resetPeak (AudioEngine::masterTrackId); };
    arrangement.deviceSummary = [this] (int id) { return deviceSummary (id); };
    arrangement.automationParametersProvider = [this] (int trackId)
    {
        std::vector<ArrangementView::AutomationParameterChoice> choices;
        if (const auto* track = project.track (trackId))
        {
            for (const auto& clip : track->audioClips)
            {
                const auto name = juce::String::fromUTF8 (clip->name.c_str());
                choices.push_back ({
                    [&]
                    {
                        roll::AutomationTarget target;
                        target.type = roll::AutomationParameterType::AudioClipGain;
                        target.currentValue = clip->gainDb;
                        target.clipId = clip->id;
                        return target;
                    }(),
                    name + " : Gain"
                });
                choices.push_back ({
                    [&]
                    {
                        roll::AutomationTarget target;
                        target.type = roll::AutomationParameterType::AudioClipPitch;
                        target.currentValue = clip->pitchSemitones + clip->pitchCents / 100.0;
                        target.clipId = clip->id;
                        return target;
                    }(),
                    name + " : Pitch"
                });
                choices.push_back ({
                    [&]
                    {
                        roll::AutomationTarget target;
                        target.type = roll::AutomationParameterType::AudioClipFadeIn;
                        target.currentValue = clip->fadeInBeats;
                        target.clipId = clip->id;
                        return target;
                    }(),
                    name + " : Fade In"
                });
                choices.push_back ({
                    [&]
                    {
                        roll::AutomationTarget target;
                        target.type = roll::AutomationParameterType::AudioClipFadeOut;
                        target.currentValue = clip->fadeOutBeats;
                        target.clipId = clip->id;
                        return target;
                    }(),
                    name + " : Fade Out"
                });
            }
        }
        const auto devices = engine.devices (trackId);
        for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
        {
            const auto& device = devices[deviceIndex];
            for (const auto& parameter : device.automatableParameters)
            {
                ArrangementView::AutomationParameterChoice choice;
                choice.target.type = roll::AutomationParameterType::PluginParameter;
                choice.target.pluginIdentifier = device.pluginIdentifier.toStdString();
                choice.target.parameterId = parameter.id.toStdString();
                choice.target.parameterName = parameter.name.toStdString();
                choice.target.parameterIndex = parameter.index;
                choice.target.deviceIndex = static_cast<int> (deviceIndex);
                choice.target.currentValue = parameter.value;
                choice.label = device.name + " "
                             + juce::String (static_cast<int> (deviceIndex + 1))
                             + " : " + parameter.name;
                choices.push_back (std::move (choice));
            }
        }
        return choices;
    };
    arrangement.automationParameterValueTextProvider = [this] (
        int trackId, const roll::AutomationTarget& target, double value)
    {
        return engine.pluginParameterValueText (trackId, target, value);
    };
    arrangement.onTrackDuplicateRequested = [this] (int id) { duplicateSelectedTrack (id); };
    arrangement.canAcceptPluginDrop = [this] (const juce::String& identifier, int trackId)
    {
        const auto* track = project.track (trackId);
        if (track == nullptr || track->frozen)
            return false;
        if (identifier == DrumRackProcessor::pluginIdentifier)
        {
            return track->type == roll::TrackType::Midi;
        }
        if (AudioEngine::isNativeEffectIdentifier (identifier))
            return true;
        const auto description = scanner.list().getTypeForIdentifierString (identifier);
        return description != nullptr
            && (trackId != AudioEngine::masterTrackId || engine.canAcceptPlugin (trackId, *description));
    };
    arrangement.onPluginDropped = [this] (const juce::String& identifier, int trackId, double)
    {
        if (identifier == DrumRackProcessor::pluginIdentifier)
        {
            addNativeDrumRackToTrack (trackId);
            return;
        }
        if (AudioEngine::isNativeEffectIdentifier (identifier))
        {
            browser.setStatus (addNativeEffectToTrack (trackId, identifier));
            return;
        }
        if (auto description = scanner.list().getTypeForIdentifierString (identifier))
        {
            auto* target = project.track (trackId);
            if (target != nullptr && target->frozen)
            {
                browser.setStatus (theme::ui ("Unfreeze the track before editing devices."));
                return;
            }
            if (trackId == AudioEngine::masterTrackId && ! engine.canAcceptPlugin (trackId, *description))
            {
                browser.setStatus ("The Master track only accepts effects.");
                return;
            }
            if (! engine.canAcceptPlugin (trackId, *description))
            {
                const auto compatible = std::find_if (project.tracks.begin(), project.tracks.end(),
                    [this, &description] (const auto& track)
                    {
                        return engine.canAcceptPlugin (track->id, *description);
                    });
                target = compatible != project.tracks.end() ? compatible->get() : nullptr;
            }
            if (target == nullptr)
                target = project.addTrack (roll::TrackType::Midi);
            project.selectTrack (target->id);
            loadDevice (*description);
        }
    };
    const auto canTransferDevice = [this] (
        int sourceTrackId, int sourceDeviceId, int targetTrackId, bool)
    {
        if (sourceTrackId == targetTrackId || sourceDeviceId <= 0
            || trackDuplicationInProgress || pluginCopyInProgress
            || pluginHistoryRestoreInProgress || scanningPlugins || restoringProjectPlugins
            || projectFileOperationInProgress || audioRenderInProgress || pendingAudioLoads > 0)
            return false;

        const auto* sourceTrack = sourceTrackId == AudioEngine::masterTrackId
            ? nullptr : project.track (sourceTrackId);
        const auto* targetTrack = targetTrackId == AudioEngine::masterTrackId
            ? nullptr : project.track (targetTrackId);
        if ((sourceTrackId != AudioEngine::masterTrackId
             && (sourceTrack == nullptr || sourceTrack->frozen || sourceTrack->type == roll::TrackType::Group))
            || (targetTrackId != AudioEngine::masterTrackId
                && (targetTrack == nullptr || targetTrack->frozen
                    || targetTrack->type == roll::TrackType::Group)))
            return false;

        const auto sourceDevices = engine.devices (sourceTrackId);
        const auto sourceDevice = std::find_if (sourceDevices.begin(), sourceDevices.end(),
            [sourceDeviceId] (const DeviceInfo& device) { return device.id == sourceDeviceId; });
        if (sourceDevice == sourceDevices.end())
            return false;

        const auto targetDevices = engine.devices (targetTrackId);
        if (sourceDevice->isInstrument
            && std::any_of (targetDevices.begin(), targetDevices.end(),
                            [] (const DeviceInfo& device) { return device.isInstrument; }))
            return false;
        if (sourceDevice->pluginIdentifier == DrumRackProcessor::pluginIdentifier)
            return targetTrack != nullptr && targetTrack->type == roll::TrackType::Midi;
        if (AudioEngine::isNativeEffectIdentifier (sourceDevice->pluginIdentifier)
            || sourceDevice->pluginIdentifier == AudioEngine::autoPanPluginIdentifier)
            return true;

        const auto description = scanner.list().getTypeForIdentifierString (
            sourceDevice->pluginIdentifier);
        return description != nullptr && engine.canAcceptPlugin (targetTrackId, *description);
    };
    arrangement.canAcceptDeviceDrop = canTransferDevice;
    arrangement.onDeviceDropped = [this] (
        int sourceTrackId, int sourceDeviceId, int targetTrackId, bool copy)
    {
        transferDeviceToTrack (sourceTrackId, sourceDeviceId, targetTrackId, -1, copy);
    };
    arrangement.canAcceptAudioDrop = [] (const juce::File& file, int trackId)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        return trackId != AudioEngine::masterTrackId && file.existsAsFile() && (extension == ".wav" || extension == ".wave"
            || extension == ".aif" || extension == ".aiff" || extension == ".flac" || extension == ".ogg");
    };
    arrangement.onAudioDropped = [this] (const juce::File& file, int trackId, double beat)
    {
        auto* target = project.track (trackId);
        if (target == nullptr || target->type != roll::TrackType::Audio)
        {
            const auto existing = std::find_if (project.tracks.begin(), project.tracks.end(),
                [] (const auto& candidate) { return candidate->type == roll::TrackType::Audio; });
            target = existing != project.tracks.end() ? existing->get() : project.addTrack (roll::TrackType::Audio);
        }
        loadAudioFile (file, target->id, beat);
    };
    arrangement.onMidiDropped = [this] (const juce::File& file, int trackId, double beat)
    {
        juce::FileInputStream input (file);
        juce::MidiFile midi;
        if (! input.openedOk() || ! midi.readFrom (input))
        {
            setProjectStatus ("Could not read the MIDI file.");
            return;
        }

        auto* target = project.track (trackId);
        if (target == nullptr || target->type != roll::TrackType::Midi)
        {
            target = project.addTrack (roll::TrackType::Midi);
            if (target != nullptr)
                trackId = target->id;
        }
        if (target == nullptr)
            return;

        const double ticksPerQuarter = std::max (1.0,
            static_cast<double> (midi.getTimeFormat()));
        std::vector<roll::Note> notes;
        double clipLength = 1.0;
        for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
        {
            const auto* events = midi.getTrack (trackIndex);
            if (events == nullptr)
                continue;
            std::map<int, std::pair<double, int>> activeNotes;
            for (int eventIndex = 0; eventIndex < events->getNumEvents(); ++eventIndex)
            {
                const auto& event = events->getEventPointer (eventIndex)->message;
                const double startBeats = event.getTimeStamp() / ticksPerQuarter;
                clipLength = std::max (clipLength, startBeats + 0.25);
                if (event.isNoteOn())
                {
                    activeNotes[event.getNoteNumber()] = {
                        startBeats, std::clamp (static_cast<int> (event.getVelocity()), 1, 127)
                    };
                }
                else if (event.isNoteOff())
                {
                    const auto active = activeNotes.find (event.getNoteNumber());
                    if (active == activeNotes.end())
                        continue;
                    const double length = std::max (1.0 / 64.0, startBeats - active->second.first);
                    notes.push_back ({ event.getNoteNumber(), active->second.first,
                                       length, active->second.second });
                    clipLength = std::max (clipLength, startBeats);
                    activeNotes.erase (active);
                }
            }
        }
        if (notes.empty())
        {
            setProjectStatus ("The MIDI file contains no notes.");
            return;
        }

        project.beginEdit();
        auto* clip = project.addClip (trackId, beat, clipLength);
        if (clip != nullptr)
        {
            clip->name = file.getFileNameWithoutExtension().toStdString();
            clip->clip.setNotes (std::move (notes));
            project.notifyTrackNotes (trackId);
        }
        project.endEdit();
        setProjectStatus (clip != nullptr
            ? "MIDI imported: " + file.getFileName()
            : "Could not insert the MIDI file at this position.");
    };
    arrangement.onMidiClipExportRequested = [this] (int trackId, int clipId)
    {
        chooseMidiClipExportFile (trackId, clipId);
    };
    arrangement.onAudioClipDoubleClicked = [this] (int trackId, int clipId)
    {
        openAudioClipEditor (trackId, clipId);
    };
    arrangement.onLocateAudioFileRequested = [this] (const juce::File& file)
    {
        setAudioBrowserVisible (true);
        audioBrowser.revealFile (file);
    };
    arrangement.onConsolidateRequested = [this] (double startBeat, double endBeat, int firstTrack, int lastTrack)
    {
        consolidateAudioRange (startBeat, endBeat, firstTrack, lastTrack);
    };
    arrangement.onFreezeTrackRequested = [this] (int trackId) { freezeTrack (trackId); };
    arrangement.onUnfreezeTrackRequested = [this] (int trackId) { unfreezeTrack (trackId); };
    arrangement.onFlattenTrackRequested = [this] (int trackId) { flattenTrack (trackId); };
    audioBrowser.canDeleteFile = [this] (const juce::File& file)
    {
        for (const auto& track : project.tracks)
            for (const auto& clip : track->audioClips)
            {
                const juce::File audioFile (juce::String (clip->filePath));
                if (audioFile == file || (file.isDirectory() && audioFile.isAChildOf (file)))
                    return false;
            }
        return true;
    };
    arrangement.onGridChanged = [this] { refreshAudioClipEditor(); };
    audioBrowser.onPreviewRequested = [this] (const juce::File& file)
    {
        stopGroovePreview();
        const int request = ++previewLoadToken;
        ++pendingPreviewLoads;
        updateActionIndicator();
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        engine.loadAudioFileAsync (file, [this, token, file, request]
            (const juce::String& error, std::shared_ptr<const AudioFileData> data)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;
            pendingPreviewLoads = std::max (0, pendingPreviewLoads - 1);
            updateActionIndicator();
            if (request != previewLoadToken)
                return;
            if (error.isNotEmpty() || data == nullptr)
            {
                audioBrowser.setStatus ("Preview error: " + error);
                return;
            }
            engine.previewAudio (std::move (data));
            audioBrowser.setStatus ("Tocando: " + file.getFileName());
        });
    };
    audioBrowser.onPreviewStopRequested = [this]
    {
        ++previewLoadToken;
        stopGroovePreview();
        engine.stopAudioPreview();
    };
    audioBrowser.onPreviewGainChanged = [this] (float gain) { engine.setAudioPreviewGain (gain); };
    transport.onPosition = [this] (double beats)
    {
        transportPosition = beats;
        const double bpm = engine.graph().getBpm();
        if (std::abs (bpm - displayedBpm) > 1.0e-6)
        {
            displayedBpm = bpm;
            arrangement.setBpm (bpm);
            refreshAudioClipEditor();
        }
        arrangement.setPlayhead (beats);
        if (followArrangement)
            arrangement.scrollToBeat (beats);
        editor.setPlayhead (beats - model.clipStartBeats);
    };
    transport.onGoToStart = [this] { seekTransport (0.0); };
    transport.onGoToEnd = [this] { seekTransport (projectEndBeat()); };
    transport.onBeforePlay = [this]
    {
        stopGroovePreview();
        if (pendingArrangementSeek)
        {
            const double seekBeat = pendingArrangementSeekBeat;
            pendingArrangementSeek = false;
            suppressSongStartOnce = false;
            transportPaused = false;
            transportPosition = seekBeat;
            engine.graph().seek (seekBeat);
            arrangement.setPlayhead (seekBeat);
            editor.setPlayhead (seekBeat - model.clipStartBeats);
            return;
        }
        if (transportPaused && ! pendingArrangementSeek)
        {
            transportPaused = false;
            transportPosition = pausedPosition;
            engine.graph().seek (pausedPosition);
            arrangement.setPlayhead (pausedPosition);
            editor.setPlayhead (pausedPosition - model.clipStartBeats);
            return;
        }
        transportPaused = false;
        double selectionStart = 0.0;
        if (arrangement.getTimeSelectionStart (selectionStart))
        {
            pendingArrangementSeek = false;
            transportPosition = selectionStart;
            engine.graph().seek (selectionStart);
            arrangement.setPlayhead (selectionStart);
            editor.setPlayhead (selectionStart - model.clipStartBeats);
            return;
        }

        if (transportPaused)
        {
            transport.togglePlay();
            return;
        }

        if (suppressSongStartOnce)
        {
            suppressSongStartOnce = false;
            return;
        }
        if (engine.graph().positionBeats() > 1.0e-6)
            return;
        if (const auto* marker = project.locator (project.songStartLocatorId))
        {
            transportPosition = marker->beat;
            engine.graph().seek (marker->beat);
            arrangement.setPlayhead (marker->beat);
            editor.setPlayhead (marker->beat - model.clipStartBeats);
        }
    };
    const auto rememberPausedPosition = [this]
    {
        pausedPosition = transportPosition;
    };
    transport.onBeforePause = [this, rememberPausedPosition]
    {
        rememberPausedPosition();
        transportPaused = true;
    };
    transport.onBeforeStop = [this, rememberPausedPosition]
    {
        rememberPausedPosition();
        transportPaused = false;
    };
    transport.onLoopToggled = [this] (bool on) { arrangement.setLoopEnabled (on); };
    arrangement.onLoopToggleRequested = [this] (bool on) { transport.setLoopEnabled (on); };
    transport.onBrowserToggled = [this] (bool visible) { setBrowserVisible (visible); };
    transport.onFileBrowserToggled = [this] (bool visible) { setAudioBrowserVisible (visible); };
    transport.onFavoritesToggled = [this] (bool visible) { setFavoritesVisible (visible); };
    transport.onPianoRollToggled = [this] (bool visible) { setPianoRollVisible (visible); };
    transport.onComputerMidiKeyboardToggled = [this] (bool enabled)
    {
        setComputerMidiKeyboardEnabled (enabled);
    };

    deviceView.onOpen = [this] (int trackId, int deviceId) { openDeviceEditor (trackId, deviceId); };
    deviceView.onBypass = [this] (int trackId, int deviceId, bool bypassed)
    {
        project.beginExternalEdit ({ trackId });
        engine.setDeviceBypassed (trackId, deviceId, bypassed);
        project.endEdit();
    };
    deviceView.onMove = [this] (int trackId, int deviceId, int direction)
    {
        project.beginExternalEdit ({ trackId });
        if (engine.moveDevice (trackId, deviceId, direction))
        {
            project.endEdit();
            refreshDeviceView();
            project.sendChangeMessage();
        }
        else
            project.cancelEdit();
    };
    deviceView.onReorder = [this] (int trackId, int deviceId, int targetDeviceId, bool afterTarget)
    {
        project.beginExternalEdit ({ trackId });
        if (engine.reorderDevice (trackId, deviceId, targetDeviceId, afterTarget))
        {
            project.endEdit();
            refreshDeviceView();
            project.sendChangeMessage();
        }
        else
            project.cancelEdit();
    };
    deviceView.onGroupRequested = [this] (int trackId, const std::vector<int>& deviceIds)
    {
        project.beginExternalEdit ({ trackId });
        if (engine.groupDevices (trackId, deviceIds))
        {
            project.endEdit();
            refreshDeviceView();
            project.sendChangeMessage();
            browser.setStatus ("Plugins grouped. Press Ctrl+Shift+G to ungroup.");
        }
        else
        {
            project.cancelEdit();
            browser.setStatus ("Select at least two consecutive plugins to group.");
        }
    };
    deviceView.onUngroupRequested = [this] (int trackId, const std::vector<int>& deviceIds)
    {
        project.beginExternalEdit ({ trackId });
        if (engine.ungroupDevices (trackId, deviceIds))
        {
            project.endEdit();
            refreshDeviceView();
            project.sendChangeMessage();
            browser.setStatus ("Rack ungrouped.");
        }
        else
        {
            project.cancelEdit();
            browser.setStatus ("Select a rack to ungroup.");
        }
    };
    deviceView.onCopyDevices = [this] (int trackId, const std::vector<int>& deviceIds)
    {
        copyDevicesToClipboard (trackId, deviceIds);
    };
    deviceView.onPasteDevices = [this] (int trackId) { pasteDevicesToTrack (trackId); };
    deviceView.canPasteDevices = [this]
    {
        return ! copiedPluginStates.empty() && ! pluginCopyInProgress && ! pluginHistoryRestoreInProgress;
    };
    deviceView.canAcceptDeviceTransfer = canTransferDevice;
    deviceView.onDeviceTransferRequested = [this] (
        int sourceTrackId, int sourceDeviceId, int targetTrackId, int insertionIndex, bool copy)
    {
        transferDeviceToTrack (sourceTrackId, sourceDeviceId, targetTrackId, insertionIndex, copy);
    };
    deviceView.canAcceptPluginDrop = [this] (int trackId, const juce::String& identifier)
    {
        const auto* track = project.track (trackId);
        if (track == nullptr || track->frozen)
            return false;
        if (identifier == DrumRackProcessor::pluginIdentifier)
            return track->type == roll::TrackType::Midi;
        if (AudioEngine::isNativeEffectIdentifier (identifier))
            return true;
        const auto description = scanner.list().getTypeForIdentifierString (identifier);
        return description != nullptr && engine.canAcceptPlugin (trackId, *description);
    };
    deviceView.onPluginDropped = [this] (int trackId, const juce::String& identifier, int insertionIndex)
    {
        if (const auto* track = project.track (trackId); track != nullptr && track->frozen)
        {
            browser.setStatus (theme::ui ("Unfreeze the track before editing devices."));
            return;
        }
        if (identifier == DrumRackProcessor::pluginIdentifier)
        {
            addNativeDrumRackToTrack (trackId);
            return;
        }
        if (AudioEngine::isNativeEffectIdentifier (identifier))
        {
            browser.setStatus (addNativeEffectToTrack (trackId, identifier, insertionIndex));
            return;
        }
        if (auto description = scanner.list().getTypeForIdentifierString (identifier))
            loadDevice (*description, trackId, insertionIndex);
    };
    deviceView.onRemove = [this] (int trackId, int deviceId)
    {
        project.beginExternalEdit ({ trackId });
        windows.erase (deviceId);
        closeWindowsOfRack (trackId, deviceId);
        engine.removeDevice (trackId, deviceId);
        project.endEdit();
        refreshDeviceView();
        project.sendChangeMessage();
    };

    browser.onLoadRequested = [this] (const juce::PluginDescription& d) { loadDevice (d); };
    browser.onOpenReferenceRequested = [this]
    {
        if (referenceMasterWindow == nullptr)
        {
            referenceMasterWindow = std::make_unique<ReferenceMasterWindow>();
            referenceMasterWindow->onAnalyzeRequested = [this] (const juce::File& file)
            {
                if (referenceMasterWindow == nullptr)
                    return;

                juce::Component::SafePointer<ReferenceMasterWindow> safeWindow (
                    referenceMasterWindow.get());
                engine.analyzeAudioFileAsync (file, [safeWindow, file]
                    (const juce::String& error, std::shared_ptr<const AudioFileData> data)
                {
                    if (safeWindow == nullptr)
                        return;
                    if (error.isNotEmpty() || data == nullptr)
                    {
                        safeWindow->setAnalysisStatus ("Analysis error: " + error);
                        return;
                    }
                    safeWindow->setAnalysis (file, std::move (data));
                });
            };
            referenceMasterWindow->onPlayRequested = [this]
                (std::shared_ptr<const AudioFileData> data, double startPositionSeconds)
            {
                engine.previewAudio (std::move (data), startPositionSeconds);
            };
            referenceMasterWindow->onPauseRequested = [this] { engine.pauseAudioPreview(); };
            referenceMasterWindow->onResumeRequested = [this] { engine.resumeAudioPreview(); };
            referenceMasterWindow->onStopRequested = [this] { engine.stopAudioPreview(); };
            referenceMasterWindow->onSeekRequested = [this] (double seconds)
            {
                engine.seekAudioPreview (seconds);
            };
            referenceMasterWindow->onPlayheadPositionRequested = [this]
            {
                return engine.getAudioPreviewPositionSeconds();
            };
            referenceMasterWindow->onIsPlayingRequested = [this]
            {
                return engine.isAudioPreviewPlaying();
            };
        }
        referenceMasterWindow->setVisible (true);
        referenceMasterWindow->toFront (true);
        return juce::String ("Reference Master opened.");
    };
    browser.onAddDrumRackRequested = [this]
    {
        if (const auto* track = project.track (project.selectedTrackId);
            track != nullptr && track->frozen)
            return theme::ui ("Unfreeze the track before editing devices.");
        const int trackId = project.selectedTrackId;
        project.beginExternalEdit ({ trackId });
        const auto deviceId = engine.addDrumRack (trackId);
        if (deviceId <= 0)
        {
            project.cancelEdit();
            return juce::String ("Drum Master can only be added to a MIDI track.");
        }
        project.endEdit();
        showDetail (true);
        return juce::String ("Drum Master added to the selected track.");
    };
    browser.onAddUtilityRequested = [this]
    {
        return addNativeEffectToTrack (project.selectedTrackId);
    };
    browser.onAddCompMasterRequested = [this]
    {
        return addNativeEffectToTrack (project.selectedTrackId, AudioEngine::compMasterPluginIdentifier);
    };
    browser.onAddEqMasterRequested = [this]
    {
        return addNativeEffectToTrack (project.selectedTrackId, AudioEngine::eqMasterPluginIdentifier);
    };
    scanner.onStarted = [this]
    {
        scanningPlugins = true;
        scanMessage = "Scanning plugins...";
        browser.setStatus (scanMessage);
        updateActionIndicator();
    };
    scanner.onProgress = [this] (const juce::String& n)
    {
        scanMessage = "Verificando: " + juce::File (n).getFileName();
        browser.setStatus (scanMessage);
    };
    scanner.onFinished = [this] (bool cancelled)
    {
        scanningPlugins = false;
        scanMessage.clear();
        browser.refresh();
        browser.setStatus (cancelled
            ? theme::ui ("Scan cancelled. ") + juce::String (scanner.list().getNumTypes()) + " plugins found"
            : juce::String (scanner.list().getNumTypes()) + " plugins found");
        updateActionIndicator();
        if (waitingForProjectPluginScan)
        {
            waitingForProjectPluginScan = false;
            restoreNextProjectPlugin();
        }
    };

    engine.graph().setLoopRegion (project.loopStart, project.loopEnd);
    if (project.tracks.empty())
    {
        auto* midiTrack = project.addTrack();
        project.addTrack (roll::TrackType::Audio);
        project.selectTrack (midiTrack->id);
    }
    onSelectionChanged();
    changeListenerCallback (&model);
    showDetail (false);

    setWantsKeyboardFocus (true);
    addKeyListenerRecursively (*this);
    setSize (1360, 800);
    startTimerHz (30);
    nextRecoverySaveTime = juce::Time::getMillisecondCounterHiRes() + 120000.0;

    scanner.startScan();
}

MainComponent::~MainComponent()
{
    lifetimeToken->store (false);
    engine.audioDeviceManager().removeMidiInputDeviceCallback ({}, this);
    engine.audioDeviceManager().removeAudioCallback (this);
    finishRecording();
    removeKeyListenerRecursively (*this);
    stopGroovePreview();
    grooveBrowserWindow.reset();
    releaseComputerMidiNotes();

    // The registry is a member of this class; stop any workers using it first.
    if (taskSimulationThread.joinable())
    {
        taskSimulationThread.request_stop();
        taskSimulationThread.join();
    }

    lifetimeToken->store (false);
    optionsWindow.reset();
    scanner.stopScan();
    projectFilePool.removeAllJobs (true, -1);
    if (audioRenderInProgress)
    {
        engine.finishOfflineRender();
        audioRenderInProgress = false;
    }
    if (projectLoadTask != nullptr)
    {
        projectLoadTask->end();
        projectLoadTask.reset();
    }
    stopTimer();
    project.removeChangeListener (this);
    model.removeChangeListener (this);
    windows.clear();                              // Close plugin UIs before the engine.
}

void MainComponent::setMainWindow (juce::TopLevelWindow* window)
{
    mainWindow = window;
    updatePluginWindowZOrder();
}

void MainComponent::restorePluginHistoryState (
    const std::vector<PluginState>& plugins, const std::vector<int>& scopeTrackIds)
{
    if (pluginHistoryRestoreInProgress)
    {
        setProjectStatus ("Wait for the current plugin restoration to finish.");
        return;
    }

    pluginHistoryRestoreInProgress = true;
    pluginHistoryRestore = std::make_shared<PluginHistoryRestore>();
    pluginHistoryRestore->plugins = plugins;
    pluginHistoryRestore->trackIds = scopeTrackIds;
    std::stable_sort (pluginHistoryRestore->plugins.begin(), pluginHistoryRestore->plugins.end(),
        [] (const PluginState& a, const PluginState& b)
        {
            return a.trackId == b.trackId ? a.order < b.order : a.trackId < b.trackId;
        });

    std::vector<int> trackIds = scopeTrackIds;
    if (trackIds.empty())
    {
        trackIds.push_back (0);
        for (const auto& track : project.tracks)
            trackIds.push_back (track->id);
    }

    for (const int trackId : trackIds)
    {
        for (const auto& device : engine.devices (trackId))
        {
            const auto desired = std::find_if (plugins.begin(), plugins.end(),
                [trackId, &device] (const PluginState& candidate)
                {
                    return candidate.trackId == trackId
                        && candidate.identifier == device.pluginIdentifier;
                });
            if (desired != plugins.end())
                continue;

            windows.erase (device.id);
            closeWindowsOfRack (trackId, device.id);
            engine.removeDevice (trackId, device.id);
        }
    }

    setProjectStatus ("Restoring plugin state...");
    restoreNextPluginHistoryState();
}

void MainComponent::restoreNextPluginHistoryState()
{
    const auto operation = pluginHistoryRestore;
    if (operation == nullptr)
        return;

    while (operation->nextPlugin < operation->plugins.size())
    {
        const auto state = operation->plugins[operation->nextPlugin++];
        const auto devices = engine.devices (state.trackId);
        const auto existing = std::find_if (devices.begin(), devices.end(),
            [&state] (const DeviceInfo& candidate)
            {
                return candidate.pluginIdentifier == state.identifier;
            });

        if (existing != devices.end())
        {
            engine.setDeviceBypassed (state.trackId, existing->id, state.bypassed);
            continue;
        }

        int nativeDeviceId = 0;
        if (state.identifier == AudioEngine::drumRackPluginIdentifier)
            nativeDeviceId = engine.addDrumRack (state.trackId);
        else if (AudioEngine::isNativeEffectIdentifier (state.identifier))
            nativeDeviceId = engine.addNativeEffect (state.trackId, state.identifier);
        else if (state.identifier == AudioEngine::autoPanPluginIdentifier)
            nativeDeviceId = engine.addUtility (state.trackId);

        if (nativeDeviceId > 0)
        {
            if (! engine.restorePluginState (state.trackId, nativeDeviceId, state.state))
                ++operation->failures;
            engine.setDeviceBypassed (state.trackId, nativeDeviceId, state.bypassed);
            continue;
        }

        const auto description = scanner.list().getTypeForIdentifierString (state.identifier);
        if (description == nullptr)
        {
            ++operation->failures;
            continue;
        }

        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        engine.addDevice (state.trackId, *description,
            [this, token, operation, state] (const juce::String& error, int deviceId)
            {
                const auto alive = token.lock();
                if (alive == nullptr || ! alive->load() || pluginHistoryRestore != operation)
                    return;

                if (error.isNotEmpty())
                {
                    ++operation->failures;
                    browser.setStatus ("Could not restore " + state.identifier + ": " + error);
                }
                else
                {
                    if (! engine.restorePluginState (state.trackId, deviceId, state.state))
                        ++operation->failures;
                    engine.setDeviceBypassed (state.trackId, deviceId, state.bypassed);
                }

                restoreNextPluginHistoryState();
            }, state.order);
        return;
    }

    std::set<int> trackIds (operation->trackIds.begin(), operation->trackIds.end());
    for (const auto& state : operation->plugins)
        trackIds.insert (state.trackId);

    for (const int trackId : trackIds)
    {
        auto devices = engine.devices (trackId);
        std::vector<int> deviceIds;
        deviceIds.reserve (devices.size());
        for (const auto& device : devices)
            deviceIds.push_back (device.id);
        engine.ungroupDevices (trackId, deviceIds);

        std::vector<PluginState> desiredTrack;
        for (const auto& state : operation->plugins)
            if (state.trackId == trackId)
                desiredTrack.push_back (state);

        for (std::size_t targetIndex = 0; targetIndex < desiredTrack.size(); ++targetIndex)
        {
            devices = engine.devices (trackId);
            auto target = std::find_if (devices.begin(), devices.end(),
                [&desiredTrack, targetIndex] (const DeviceInfo& device)
                {
                    return device.pluginIdentifier == desiredTrack[targetIndex].identifier;
                });
            if (target == devices.end())
            {
                ++operation->failures;
                continue;
            }

            auto currentIndex = static_cast<int> (target - devices.begin());
            while (currentIndex > static_cast<int> (targetIndex))
            {
                if (! engine.moveDevice (trackId, target->id, -1))
                {
                    ++operation->failures;
                    break;
                }
                --currentIndex;
                devices = engine.devices (trackId);
                target = std::find_if (devices.begin(), devices.end(),
                    [&desiredTrack, targetIndex] (const DeviceInfo& device)
                    {
                        return device.pluginIdentifier == desiredTrack[targetIndex].identifier;
                    });
            }
        }

        std::map<int, std::vector<int>> groups;
        devices = engine.devices (trackId);
        for (const auto& state : desiredTrack)
            if (state.groupId > 0)
                if (const auto device = std::find_if (devices.begin(), devices.end(),
                        [&state] (const DeviceInfo& candidate)
                        {
                            return candidate.pluginIdentifier == state.identifier;
                        }); device != devices.end())
                    groups[state.groupId].push_back (device->id);
        for (const auto& [groupId, members] : groups)
        {
            juce::ignoreUnused (groupId);
            if (members.size() > 1 && ! engine.groupDevices (trackId, members))
                ++operation->failures;
        }
    }

    finishPluginHistoryRestore();
}

void MainComponent::addKeyListenerRecursively (juce::Component& component)
{
    component.addKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            addKeyListenerRecursively (*child);
}

void MainComponent::removeKeyListenerRecursively (juce::Component& component)
{
    component.removeKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            removeKeyListenerRecursively (*child);
}

void MainComponent::transferDeviceToTrack (int sourceTrackId, int sourceDeviceId,
                                           int targetTrackId, int insertionIndex, bool copy)
{
    if (sourceTrackId == targetTrackId || sourceDeviceId <= 0
        || trackDuplicationInProgress || pluginCopyInProgress || pluginHistoryRestoreInProgress
        || scanningPlugins || restoringProjectPlugins || projectFileOperationInProgress
        || audioRenderInProgress || pendingAudioLoads > 0)
        return;

    const auto sourceDevices = engine.devices (sourceTrackId);
    const auto sourceDevice = std::find_if (sourceDevices.begin(), sourceDevices.end(),
        [sourceDeviceId] (const DeviceInfo& device) { return device.id == sourceDeviceId; });
    if (sourceDevice == sourceDevices.end())
        return;

    const auto targetDevices = engine.devices (targetTrackId);
    if (sourceDevice->isInstrument
        && std::any_of (targetDevices.begin(), targetDevices.end(),
                        [] (const DeviceInfo& device) { return device.isInstrument; }))
    {
        setProjectStatus ("Remove the target channel's instrument before moving or copying another one.");
        return;
    }

    const auto* targetTrack = targetTrackId == AudioEngine::masterTrackId
        ? nullptr : project.track (targetTrackId);
    if (targetTrackId != AudioEngine::masterTrackId
        && (targetTrack == nullptr || targetTrack->frozen || targetTrack->type == roll::TrackType::Group))
    {
        setProjectStatus ("The target channel is not available for a device transfer.");
        return;
    }

    if (sourceDevice->pluginIdentifier == DrumRackProcessor::pluginIdentifier)
    {
        if (targetTrack == nullptr || targetTrack->type != roll::TrackType::Midi)
        {
            setProjectStatus ("The Drum Master can only be moved or copied to a MIDI channel.");
            return;
        }
    }
    else if (! AudioEngine::isNativeEffectIdentifier (sourceDevice->pluginIdentifier)
             && sourceDevice->pluginIdentifier != AudioEngine::autoPanPluginIdentifier)
    {
        const auto description = scanner.list().getTypeForIdentifierString (
            sourceDevice->pluginIdentifier);
        if (description == nullptr || ! engine.canAcceptPlugin (targetTrackId, *description))
        {
            setProjectStatus ("This plugin is not compatible with the target channel.");
            return;
        }
    }

    bool statesCaptured = false;
    juce::String stateCaptureError;
    auto states = engine.getPluginStates (
        { sourceTrackId, targetTrackId }, {}, &statesCaptured, &stateCaptureError);
    if (! statesCaptured)
    {
        setProjectStatus ("Could not read the current plugin state; transfer was cancelled. "
                          + stateCaptureError);
        return;
    }

    const auto sourceOrder = static_cast<int> (sourceDevice - sourceDevices.begin());
    const auto state = std::find_if (states.begin(), states.end(),
        [sourceTrackId, sourceOrder] (const PluginState& candidate)
        {
            return candidate.trackId == sourceTrackId && candidate.order == sourceOrder;
        });
    if (state == states.end())
    {
        setProjectStatus ("Could not find the device state; transfer was cancelled.");
        return;
    }

    struct TransferOperation
    {
        int sourceTrackId = -1;
        int sourceDeviceId = -1;
        int targetTrackId = -1;
        bool copy = false;
        PluginState state;
    };

    auto operation = std::make_shared<TransferOperation>();
    operation->sourceTrackId = sourceTrackId;
    operation->sourceDeviceId = sourceDeviceId;
    operation->targetTrackId = targetTrackId;
    operation->copy = copy;
    operation->state = *state;

    project.beginExternalEdit ({ sourceTrackId, targetTrackId }, states);
    pluginCopyInProgress = true;
    setProjectStatus (copy ? "Copying the configured plugin to the target channel..."
                           : "Moving the configured plugin to the target channel...");
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    const auto finishTransfer = [this, token, operation] (
        const juce::String& error, int targetDeviceId)
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;

        const bool restored = error.isEmpty() && targetDeviceId > 0
            && engine.restorePluginState (operation->targetTrackId, targetDeviceId,
                                          operation->state.state);
        if (! restored)
        {
            if (targetDeviceId > 0)
                engine.removeDevice (operation->targetTrackId, targetDeviceId);
            pluginCopyInProgress = false;
            project.cancelEdit();
            refreshDeviceView();
            setProjectStatus ("Could not restore the plugin state; the source device was left unchanged.");
            return;
        }

        engine.setDeviceBypassed (operation->targetTrackId, targetDeviceId,
                                  operation->state.bypassed);
        if (! operation->copy)
        {
            windows.erase (operation->sourceDeviceId);
            closeWindowsOfRack (operation->sourceTrackId, operation->sourceDeviceId);
            engine.removeDevice (operation->sourceTrackId, operation->sourceDeviceId);
        }

        pluginCopyInProgress = false;
        project.endEdit();
        if (operation->targetTrackId != AudioEngine::masterTrackId)
            project.selectTrack (operation->targetTrackId);
        project.sendChangeMessage();
        refreshDeviceView();
        setProjectStatus (operation->copy ? "Configured plugin copied to the target channel."
                                          : "Configured plugin moved to the target channel.");
    };

    const auto& plugin = operation->state;
    if (plugin.identifier == DrumRackProcessor::pluginIdentifier)
    {
        finishTransfer ({}, engine.addDrumRack (targetTrackId));
        return;
    }
    if (AudioEngine::isNativeEffectIdentifier (plugin.identifier))
    {
        finishTransfer ({}, engine.addNativeEffect (targetTrackId, plugin.identifier,
                                                     insertionIndex, true));
        return;
    }
    if (plugin.identifier == AudioEngine::autoPanPluginIdentifier)
    {
        const int deviceId = engine.addNativeEffect (
            targetTrackId, AudioEngine::utilityPluginIdentifier, insertionIndex, true);
        finishTransfer ({}, deviceId);
        return;
    }

    const auto description = scanner.list().getTypeForIdentifierString (plugin.identifier);
    if (description == nullptr)
    {
        finishTransfer ("The plugin is not installed.", 0);
        return;
    }

    engine.addDevice (targetTrackId, *description,
        [finishTransfer] (const juce::String& error, int targetDeviceId)
        {
            finishTransfer (error, targetDeviceId);
        }, insertionIndex, true);
}

void MainComponent::copyDevicesToClipboard (int trackId, const std::vector<int>& deviceIds)
{
    copiedPluginStates.clear();
    if (deviceIds.empty())
        return;

    const std::set<int> selected (deviceIds.begin(), deviceIds.end());
    const auto devices = engine.devices (trackId);
    bool statesCaptured = false;
    juce::String stateCaptureError;
    auto states = engine.getPluginStates ({ trackId }, {}, &statesCaptured, &stateCaptureError);
    if (! statesCaptured)
    {
        setProjectStatus ("Could not read the selected plugin state; copy was cancelled. "
                          + stateCaptureError);
        return;
    }
    for (std::size_t order = 0; order < devices.size(); ++order)
    {
        if (! selected.contains (devices[order].id))
            continue;

        const auto state = std::find_if (states.begin(), states.end(),
            [trackId, order] (const PluginState& candidate)
            {
                return candidate.trackId == trackId && candidate.order == static_cast<int> (order);
            });
        if (state != states.end())
            copiedPluginStates.push_back (*state);
    }

    std::stable_sort (copiedPluginStates.begin(), copiedPluginStates.end(),
        [] (const PluginState& a, const PluginState& b) { return a.order < b.order; });
    browser.setStatus (copiedPluginStates.empty()
        ? "Could not copy the selected plugins."
        : juce::String (copiedPluginStates.size()) + " configured plugin(s) copied. "
          "Select another track and press Ctrl+V.");
}

void MainComponent::pasteDevicesToTrack (int trackId)
{
    const auto* targetTrack = project.track (trackId);
    if (copiedPluginStates.empty() || targetTrack == nullptr)
        return;
    if (targetTrack->frozen)
    {
        setProjectStatus (theme::ui ("Unfreeze the track before editing devices."));
        return;
    }
    if (pluginCopyInProgress || pluginHistoryRestoreInProgress)
    {
        setProjectStatus ("Wait for the current plugin operation to finish.");
        return;
    }

    const auto targetDevices = engine.devices (trackId);
    const bool targetHasInstrument = std::any_of (
        targetDevices.begin(), targetDevices.end(),
        [] (const DeviceInfo& device) { return device.isInstrument; });
    const bool copiesInstrument = std::any_of (
        copiedPluginStates.begin(), copiedPluginStates.end(), [this] (const PluginState& state)
        {
            if (state.identifier == DrumRackProcessor::pluginIdentifier)
                return true;
            if (AudioEngine::isNativeEffectIdentifier (state.identifier)
                || state.identifier == AudioEngine::autoPanPluginIdentifier)
                return false;
            const auto description = scanner.list().getTypeForIdentifierString (state.identifier);
            return description != nullptr && description->isInstrument;
        });
    if (targetHasInstrument && copiesInstrument)
    {
        setProjectStatus ("Remove the target channel's instrument before pasting another one.");
        return;
    }

    auto operation = std::make_shared<PluginCopyOperation>();
    operation->targetTrackId = trackId;
    operation->devices = copiedPluginStates;
    for (const auto& state : operation->devices)
        if (state.groupId > 0)
            ++operation->expectedGroupSizes[state.groupId];

    project.beginExternalEdit ({ trackId });
    pluginCopyOperation = std::move (operation);
    pluginCopyInProgress = true;
    setProjectStatus ("Copying configured plugins to the track...");
    pasteNextDevice();
}

void MainComponent::pasteNextDevice()
{
    const auto operation = pluginCopyOperation;
    if (operation == nullptr)
        return;

    while (operation->nextDevice < operation->devices.size())
    {
        const auto state = operation->devices[operation->nextDevice++];
        int deviceId = 0;
        if (state.identifier == AudioEngine::drumRackPluginIdentifier)
        {
            deviceId = engine.addDrumRack (operation->targetTrackId);
        }
        else if (AudioEngine::isNativeEffectIdentifier (state.identifier))
        {
            deviceId = engine.addNativeEffect (operation->targetTrackId, state.identifier, -1, true);
        }
        else if (state.identifier == AudioEngine::autoPanPluginIdentifier)
        {
            deviceId = engine.addNativeEffect (
                operation->targetTrackId, AudioEngine::utilityPluginIdentifier, -1, true);
        }
        else
        {
            const auto description = scanner.list().getTypeForIdentifierString (state.identifier);
            if (description == nullptr || ! engine.canAcceptPlugin (operation->targetTrackId, *description))
            {
                ++operation->failures;
                continue;
            }

            const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
            engine.addDevice (operation->targetTrackId, *description,
                [this, token, operation, state] (const juce::String& error, int loadedDeviceId)
                {
                    const auto alive = token.lock();
                    if (alive == nullptr || ! alive->load() || pluginCopyOperation != operation)
                        return;
                    if (error.isNotEmpty() || loadedDeviceId <= 0)
                    {
                        ++operation->failures;
                        browser.setStatus ("Could not copy " + state.identifier + ": "
                                          + (error.isNotEmpty() ? error : "failed to add."));
                    }
                    else
                    {
                        if (! engine.restorePluginState (operation->targetTrackId, loadedDeviceId, state.state))
                        {
                            engine.removeDevice (operation->targetTrackId, loadedDeviceId);
                            ++operation->failures;
                        }
                        else
                        {
                            engine.setDeviceBypassed (operation->targetTrackId, loadedDeviceId, state.bypassed);
                            operation->copiedGroups[state.groupId].push_back (loadedDeviceId);
                            ++operation->added;
                        }
                    }
                    pasteNextDevice();
                }, -1, true);
            return;
        }

        if (deviceId <= 0)
        {
            ++operation->failures;
            continue;
        }
        if (! engine.restorePluginState (operation->targetTrackId, deviceId, state.state))
        {
            engine.removeDevice (operation->targetTrackId, deviceId);
            ++operation->failures;
        }
        else
        {
            engine.setDeviceBypassed (operation->targetTrackId, deviceId, state.bypassed);
            operation->copiedGroups[state.groupId].push_back (deviceId);
            ++operation->added;
        }
    }

    for (const auto& [groupId, deviceIds] : operation->copiedGroups)
    {
        const auto expected = operation->expectedGroupSizes.find (groupId);
        if (groupId > 0 && expected != operation->expectedGroupSizes.end()
            && expected->second == deviceIds.size() && deviceIds.size() > 1
            && ! engine.groupDevices (operation->targetTrackId, deviceIds))
            ++operation->failures;
    }
    finishDeviceCopy();
}

void MainComponent::finishDeviceCopy()
{
    const auto operation = pluginCopyOperation;
    if (operation == nullptr)
        return;

    if (operation->added > 0)
    {
        project.endEdit();
        project.sendChangeMessage();
        refreshDeviceView();
    }
    else
    {
        project.cancelEdit();
    }

    pluginCopyOperation.reset();
    pluginCopyInProgress = false;
    const auto result = juce::String (operation->added) + " plugin(s) copied";
    setProjectStatus (operation->failures == 0
        ? result + "."
        : result + "; " + juce::String (operation->failures) + " could not be copied.");
}

void MainComponent::finishPluginHistoryRestore()
{
    const int failures = pluginHistoryRestore != nullptr ? pluginHistoryRestore->failures : 0;
    pluginHistoryRestore.reset();
    pluginHistoryRestoreInProgress = false;
    refreshDeviceView();
    project.sendChangeMessage();
    setProjectStatus (failures == 0
        ? "Plugin state restored."
        : "State restored with " + juce::String (failures) + " plugin(s) failed to load.");
}

std::vector<juce::Rectangle<int>> MainComponent::pluginWindowBounds() const
{
    std::vector<juce::Rectangle<int>> bounds;
    bounds.reserve (windows.size() + 4);
    for (const auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        if (window != nullptr && window->isVisible() && ! window->isMinimised())
            bounds.push_back (window->getScreenBounds());
    }

    auto workerBounds = RemotePluginProcessor::pluginEditorWindowBounds();
    bounds.insert (bounds.end(), workerBounds.begin(), workerBounds.end());
    return bounds;
}

juce::Point<int> MainComponent::nextPluginWindowPosition() const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    const auto area = display != nullptr ? display->userBounds.toNearestInt()
                                        : juce::Rectangle<int> (0, 0, 1280, 720);
    const roll::Rect virtualDisplay { area.getX() + pluginWindowScrollOffset, area.getY(),
                                     area.getWidth(), area.getHeight() };
    std::vector<roll::Rect> occupiedWindows;
    for (const auto& bounds : pluginWindowBounds())
        occupiedWindows.push_back ({ bounds.getX() + pluginWindowScrollOffset, bounds.getY(),
                                     bounds.getWidth(), bounds.getHeight() });

    const auto next = roll::WindowPlacement::placePluginWindow (
        { 0, 0, 600, 400 }, virtualDisplay, nextPluginCascadeIndex, 64, occupiedWindows);
    return { next.x - pluginWindowScrollOffset, next.y };
}

void MainComponent::updatePluginWindowNavigator()
{
    if (mainWindow == nullptr)
        return;

    if (pluginWindowNavigator == nullptr)
    {
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        pluginWindowNavigator = std::make_unique<PluginWindowNavigator> (
            mainWindow, [this, token] (int offset)
            {
                const auto alive = token.lock();
                if (alive != nullptr && alive->load())
                    scrollPluginWindowsTo (offset);
            });
    }

    pluginWindowNavigator->setOwnerWindow (mainWindow);
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    const auto area = display != nullptr ? display->userBounds.toNearestInt()
                                        : juce::Rectangle<int> (0, 0, 1280, 720);
    auto rightmost = area.getRight() + pluginWindowScrollOffset;
    for (const auto& bounds : pluginWindowBounds())
        rightmost = std::max (rightmost, bounds.getRight() + pluginWindowScrollOffset);

    const int maximumOffset = std::max (0, rightmost - area.getRight());
    if (pluginWindowScrollOffset > maximumOffset)
    {
        scrollPluginWindowsTo (maximumOffset);
        return;
    }

    pluginWindowScrollRange = maximumOffset;
    const bool appForeground = juce::Process::isForegroundProcess()
                            || RemotePluginProcessor::isPluginEditorForeground();
    const int navigatorWidth = std::min (360, area.getWidth());
    const int navigatorHeight = std::min (28, area.getHeight());
    const auto navigatorBounds = juce::Rectangle<int> (area.getRight() - navigatorWidth,
                                                       area.getBottom() - navigatorHeight,
                                                       navigatorWidth, navigatorHeight);
    if (pluginWindowNavigator->getBounds() != navigatorBounds)
        pluginWindowNavigator->setBounds (navigatorBounds);
    const bool wasVisible = pluginWindowNavigator->isVisible();
    pluginWindowNavigator->updateRange (pluginWindowScrollRange, pluginWindowScrollOffset,
                                        area.getWidth(),
                                        pluginWindowScrollRange > 0 && appForeground);
    pluginWindowNavigator->setOwnerWindow (mainWindow);
#if defined(_WIN32)
    const auto foreground = reinterpret_cast<std::uintptr_t> (GetForegroundWindow());
    const bool foregroundChanged = foreground != pluginNavigatorForegroundWindow;
    pluginNavigatorForegroundWindow = foreground;
#else
    const bool foregroundChanged = false;
#endif
    if (pluginWindowNavigator->isVisible() && appForeground
        && (foregroundChanged || ! wasVisible))
        pluginWindowNavigator->toFront (false);
}

void MainComponent::scrollPluginWindowsTo (int offset)
{
    const int newOffset = std::clamp (offset, 0, pluginWindowScrollRange);
    const int delta = newOffset - pluginWindowScrollOffset;
    if (delta == 0)
        return;

    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        if (window == nullptr)
            continue;

        const auto bounds = window->getScreenBounds();
        window->setBounds (bounds.translated (-delta, 0));
    }

    RemotePluginProcessor::movePluginEditorWindows (-delta);
    pluginWindowScrollOffset = newOffset;
    updatePluginWindowNavigator();
}

void MainComponent::updatePluginWindowZOrder()
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        for (auto& [deviceId, window] : safeThis->windows)
        {
            juce::ignoreUnused (deviceId);
            if (window == nullptr)
                continue;

            if (window->isAlwaysOnTop())
                window->setAlwaysOnTop (false);
            window->setOwnerWindow (safeThis->mainWindow);
        }

        safeThis->updatePluginWindowNavigator();
        if (safeThis->pluginWindowNavigator != nullptr
            && safeThis->pluginWindowNavigator->isVisible()
            && (juce::Process::isForegroundProcess()
                || RemotePluginProcessor::isPluginEditorForeground()))
            safeThis->pluginWindowNavigator->toFront (false);
    });
}

// ------------------------------------------------------------ layout

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background());
    const auto drawResizeHandle = [&g] (juce::Rectangle<int> handle, bool horizontal, bool hovered)
    {
        if (handle.isEmpty())
            return;

        const auto bounds = handle.toFloat();
        const auto accent = theme::accent();
        g.setColour (theme::panel());
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (accent.withAlpha (hovered ? 0.18f : 0.10f));
        g.fillRoundedRectangle (bounds.reduced (0.5f), 2.0f);

        const auto drawNeonLine = [&g, &accent, hovered] (
            float startX, float startY, float endX, float endY)
        {
            g.setColour (accent.withAlpha (hovered ? 0.30f : 0.14f));
            g.drawLine (startX, startY, endX, endY, 4.0f);
            g.setColour (accent.withAlpha (hovered ? 0.52f : 0.32f));
            g.drawLine (startX, startY, endX, endY, 2.0f);
            g.setColour (accent.withAlpha (hovered ? 1.0f : 0.90f));
            g.drawLine (startX, startY, endX, endY, 1.0f);
        };
        if (horizontal)
        {
            drawNeonLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f);
            drawNeonLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f);
        }
        else
        {
            drawNeonLine (bounds.getX() + 0.5f, bounds.getY(), bounds.getX() + 0.5f, bounds.getBottom());
            drawNeonLine (bounds.getRight() - 0.5f, bounds.getY(), bounds.getRight() - 0.5f, bounds.getBottom());
        }

        const auto grip = horizontal
            ? juce::Rectangle<float> (bounds.getCentreX() - 18.0f, bounds.getCentreY() - 1.0f, 36.0f, 2.0f)
            : juce::Rectangle<float> (bounds.getCentreX() - 1.0f, bounds.getCentreY() - 18.0f, 2.0f, 36.0f);
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.20f));
            g.fillRoundedRectangle (grip.expanded (2.0f), 2.0f);
        }
        g.setColour (accent.withAlpha (hovered ? 1.0f : 0.78f));
        g.fillRoundedRectangle (grip, 1.0f);
    };

    drawResizeHandle (splitterRect, true, hoveringSplitter);
    drawResizeHandle (browserSplitterRect, false, hoveringBrowserSplitter);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    themeBackground.setBounds (area);
    themeBackground.toBack();
    transport.setBounds (area.removeFromTop (44));
    taskStatusBar.setBounds (area.removeFromBottom (30));
    if (actionIndicator != nullptr)
        actionIndicator->setBounds ((getWidth() - 320) / 2, 50, 320, 52);
    if (computerMidiIndicator != nullptr)
        computerMidiIndicator->setBounds (
            transport.computerMidiIndicatorBounds().translated (transport.getX(), transport.getY()));

    auto workspace = area.reduced (8);
    monitorPanelWidth = std::clamp (workspace.getWidth() / 5, 220, 280);
    auto monitorArea = workspace.removeFromRight (monitorPanelWidth);
    workspace.removeFromRight (8);

    if (auto* masterControl = arrangement.masterControlComponent())
    {
        masterControl->setVisible (true);
        const int masterHeight = project.masterTrack != nullptr && project.masterTrack->collapsed ? 30 : 184;
        masterControl->setBounds (monitorArea.removeFromTop (masterHeight));
    }
    monitorArea.removeFromTop (8);
    const int collapsedAnalyzerHeight = 28;
    const int loudnessHeight = std::min (monitorArea.getHeight(),
        loudnessHistory->isCollapsed()
            ? collapsedAnalyzerHeight : std::clamp (monitorArea.getHeight() / 5, 108, 136));
    const int analysisHeight = std::max (1, monitorArea.getHeight() - loudnessHeight - 16);
    const int spectrumHeight = spectrumAnalyzer->isCollapsed()
        ? collapsedAnalyzerHeight
        : stereoImager->isCollapsed()
            ? std::max (collapsedAnalyzerHeight,
                        analysisHeight - 8 - collapsedAnalyzerHeight)
            : std::clamp (analysisHeight / 2, 145, 220);
    auto analysisArea = monitorArea.removeFromTop (analysisHeight);
    spectrumAnalyzer->setBounds (analysisArea.removeFromTop (spectrumHeight));
    analysisArea.removeFromTop (8);
    const int stereoHeight = stereoImager->isCollapsed()
        ? std::min (collapsedAnalyzerHeight, analysisArea.getHeight()) : analysisArea.getHeight();
    stereoImager->setBounds (analysisArea.removeFromTop (stereoHeight));
    monitorArea.removeFromTop (8);
    loudnessHistory->setBounds (monitorArea.removeFromTop (
        std::min (loudnessHeight, monitorArea.getHeight())));

    rightTop = workspace.getY();
    rightHeight = std::max (1, workspace.getHeight());
    const bool browserVisible = browser.isVisible() || audioBrowser.isVisible();
    const bool needsLowerPane = pianoRollVisible || browserVisible;
    const int lowerPaneMinimum = pianoRollVisible ? 230 : 190;
    const int maxArrangementHeight = needsLowerPane
        ? std::max (130, workspace.getHeight() - lowerPaneMinimum - 8)
        : workspace.getHeight();
    const int arrH = needsLowerPane
        ? (arrangementHeight >= 0
            ? std::clamp (arrangementHeight, 130, maxArrangementHeight)
            : std::clamp (static_cast<int> (workspace.getHeight() * arrangementRatio),
                          130, maxArrangementHeight))
        : workspace.getHeight();
    arrangement.setBounds (workspace.removeFromTop (arrH));
    splitterRect = needsLowerPane ? workspace.removeFromTop (8) : juce::Rectangle<int>();

    auto lower = workspace;
    if (browserVisible)
    {
        const int maxBrowserWidth = std::max (180, std::min (390, lower.getWidth() - 420));
        browserWidth = std::clamp (browserWidth, 180, maxBrowserWidth);
        auto browserBounds = lower.removeFromLeft (browserWidth);
        browser.setBounds (browser.isVisible() ? browserBounds : juce::Rectangle<int>());
        audioBrowser.setBounds (audioBrowser.isVisible() ? browserBounds : juce::Rectangle<int>());
        browserSplitterRect = lower.removeFromLeft (8);
    }
    else
    {
        browser.setBounds ({});
        audioBrowser.setBounds ({});
        browserSplitterRect = {};
    }

    if (! pianoRollVisible)
    {
        clipTab.setBounds ({});
        deviceTab.setBounds ({});
        mixerTab.setBounds ({});
        deviceView.setBounds ({});
        mixerView->setBounds ({});
        audioClipEditor.setBounds ({});
        for (auto* c : clipControls) c->setBounds ({});
        return;
    }

    auto right = lower;
    auto tabs = right.removeFromTop (26);
    clipTab.setBounds (tabs.removeFromLeft (80));
    tabs.removeFromLeft (4);
    deviceTab.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (4);
    mixerTab.setBounds (tabs.removeFromLeft (86));
    auto locatorButtons = tabs.removeFromRight (128);
    tabs.removeFromRight (10);
    pianoLocatorPreviousButton.setBounds (locatorButtons.removeFromLeft (26).reduced (0, 2));
    locatorButtons.removeFromLeft (4);
    pianoLocatorAddButton.setBounds (locatorButtons.removeFromLeft (26).reduced (0, 2));
    locatorButtons.removeFromLeft (4);
    pianoLocatorNextButton.setBounds (locatorButtons.removeFromLeft (26).reduced (0, 2));
    locatorButtons.removeFromLeft (4);
    pianoLocatorDeleteButton.setBounds (locatorButtons.removeFromLeft (36).reduced (0, 2));
    right.removeFromTop (4);

    deviceView.setBounds (right);
    mixerView->setBounds (right);
    audioClipEditor.setBounds (right);

    auto side = right.removeFromLeft (170);
    right.removeFromLeft (8);
    editor.setBounds (right);

    scaleCaption.setBounds (side.removeFromTop (16));
    auto row = side.removeFromTop (26);
    rootBox.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (4);
    modeBox.setBounds (row);
    side.removeFromTop (4);
    row = side.removeFromTop (26);
    scaleButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    highlightButton.setBounds (row);
    side.removeFromTop (4);
    row = side.removeFromTop (26);
    foldButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    foldScaleButton.setBounds (row);
    side.removeFromTop (8);
    lengthCaption.setBounds (side.removeFromTop (16));
    lengthBox.setBounds (side.removeFromTop (26));
    side.removeFromTop (8);
    gridCaption.setBounds (side.removeFromTop (16));
    gridLabel.setBounds (side.removeFromTop (20));
    row = side.removeFromTop (26);
    drawButton.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    snapButton.setBounds (row);
    side.removeFromTop (4);
    row = side.removeFromTop (26);
    previewButton.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (4);
    groovesButton.setBounds (row);
    side.removeFromTop (8);
    zoomCaption.setBounds (side.removeFromTop (16));
    row = side.removeFromTop (26);
    zoomHMinus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (4);
    zoomHPlus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (8);
    zoomVMinus.setBounds (row.removeFromLeft (40));
    row.removeFromLeft (4);
    zoomVPlus.setBounds (row.removeFromLeft (40));
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingBrowserSplitter = browserSplitterRect.expanded (4, 0).contains (e.getPosition());
    draggingSplitter = splitterRect.expanded (0, 4).contains (e.getPosition());
    grabKeyboardFocus();
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBrowserSplitter)
    {
        const int maxBrowserWidth = std::max (180, std::min (390, getWidth() - monitorPanelWidth - 440));
        browserWidth = std::clamp (e.x - 8, 180, maxBrowserWidth);
        resized();
        repaint();
    }
    else if (draggingSplitter)
    {
        arrangementHeight = std::clamp (e.y - rightTop, 130, std::max (131, rightHeight - 230));
        arrangementRatio = std::clamp (static_cast<double> (arrangementHeight) / rightHeight, 0.15, 0.75);
        resized();
        repaint();
    }
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    if (draggingSplitter)
        saveArrangementLayout (arrangementRatio, arrangementHeight);
    draggingBrowserSplitter = draggingSplitter = false;
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const bool overBrowserSplitter = browserSplitterRect.expanded (4, 0).contains (e.getPosition());
    const bool overSplitter = splitterRect.expanded (0, 4).contains (e.getPosition());
    if (hoveringBrowserSplitter != overBrowserSplitter || hoveringSplitter != overSplitter)
    {
        hoveringBrowserSplitter = overBrowserSplitter;
        hoveringSplitter = overSplitter;
        repaint();
    }

    if (overBrowserSplitter)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (overSplitter)
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseExit (const juce::MouseEvent&)
{
    if (! hoveringSplitter && ! hoveringBrowserSplitter)
        return;

    hoveringSplitter = hoveringBrowserSplitter = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void MainComponent::showDetail (bool devices)
{
    showingDevices = devices;
    showingMixer = false;
    clipTab.setToggleState (! devices, juce::dontSendNotification);
    deviceTab.setToggleState (devices, juce::dontSendNotification);
    mixerTab.setToggleState (false, juce::dontSendNotification);
    for (auto* c : clipControls) c->setVisible (! devices && ! audioClipEditorOpen);
    deviceView.setVisible (devices);
    mixerView->setVisible (false);
    audioClipEditor.setVisible (! devices && audioClipEditorOpen);
    if (devices) refreshDeviceView();
    resized();
}

void MainComponent::showMixer()
{
    showingDevices = true;
    showingMixer = true;
    clipTab.setToggleState (false, juce::dontSendNotification);
    deviceTab.setToggleState (false, juce::dontSendNotification);
    mixerTab.setToggleState (true, juce::dontSendNotification);
    for (auto* component : clipControls)
        component->setVisible (false);
    deviceView.setVisible (false);
    audioClipEditor.setVisible (false);
    mixerView->setVisible (true);
    mixerView->refresh();
    resized();
}

void MainComponent::setBrowserVisible (bool visible)
{
    if (visible && audioBrowser.isVisible())
    {
        audioBrowser.setVisible (false);
        audioBrowser.setFavoritesMode (false);
        transport.setFileBrowserVisible (false);
        transport.setFavoritesVisible (false);
    }
    browser.setVisible (visible);
    transport.setBrowserVisible (visible);
    resized();
}

void MainComponent::setAudioBrowserVisible (bool visible)
{
    if (! visible)
        audioBrowser.stopPreview();
    if (visible && browser.isVisible())
    {
        browser.setVisible (false);
        transport.setBrowserVisible (false);
    }
    if (visible)
        audioBrowser.setFavoritesMode (false);
    audioBrowser.setVisible (visible);
    transport.setFileBrowserVisible (visible);
    transport.setFavoritesVisible (false);
    resized();
}

void MainComponent::setFavoritesVisible (bool visible)
{
    if (visible && browser.isVisible())
    {
        browser.setVisible (false);
        transport.setBrowserVisible (false);
    }
    audioBrowser.setFavoritesMode (visible);
    audioBrowser.setVisible (visible);
    transport.setFileBrowserVisible (false);
    transport.setFavoritesVisible (visible);
    resized();
}

void MainComponent::setPianoRollVisible (bool visible)
{
    pianoRollVisible = visible;
    transport.setPianoRollVisible (visible);
    resized();
}

void MainComponent::loadAudioFile (const juce::File& file, int trackId, double startBeat)
{
    ++pendingAudioLoads;
    audioBrowser.setStatus ("Loading " + file.getFileName() + "...");
    updateActionIndicator();
    const int generation = projectGeneration;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    engine.loadAudioFileAsync (file, [this, token, file, trackId, startBeat, generation]
        (const juce::String& error, std::shared_ptr<const AudioFileData> data)
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load()) return;

        pendingAudioLoads = std::max (0, pendingAudioLoads - 1);
        updateActionIndicator();
        if (generation != projectGeneration)
            return;
        if (error.isNotEmpty() || data == nullptr)
        {
            audioBrowser.setStatus ("Error loading audio: " + error);
            return;
        }

        auto* track = project.track (trackId);
        if (track == nullptr || track->type != roll::TrackType::Audio)
        {
            audioBrowser.setStatus ("The audio track was removed while loading.");
            return;
        }

        const auto path = file.getFullPathName().toStdString();
        audioAssets[path] = data;
        const bool hasAudioClips = std::any_of (
            project.tracks.begin(), project.tracks.end(),
            [] (const auto& candidate)
            {
                return candidate != nullptr && candidate->type == roll::TrackType::Audio
                    && (! candidate->audioClips.empty() || candidate->frozenAudioClip != nullptr);
            });
        const bool hasUsableBpmEstimate = std::isfinite (data->estimatedBpm)
            && data->estimatedBpm >= 20.0 && data->estimatedBpm <= 300.0;
        const double sourceBpm = hasUsableBpmEstimate
            ? data->estimatedBpm : engine.graph().getBpm();
        if (! hasAudioClips && hasUsableBpmEstimate)
            transport.setBpm (sourceBpm);
        const double projectBpm = engine.graph().getBpm();
        const double lengthBeats = data->durationSeconds * sourceBpm / 60.0;
        const double edgeFadeBeats = createFadesOnClipEdges
            ? projectBpm * 0.004 / 60.0 : 0.0;
        if (project.addAudioClipReplacingRange (trackId, startBeat, lengthBeats, path,
                                                file.getFileName().toStdString(), data->waveform,
                                                data->durationSeconds, sourceBpm, edgeFadeBeats) == nullptr)
        {
            publishAudioTrack (trackId);
            audioBrowser.setStatus ("Could not insert the audio at this position on the track.");
            return;
        }

        audioBrowser.setStatus (file.getFileName() + " added");
    }, true);
}

void MainComponent::consolidateAudioRange (double startBeat, double endBeat, int firstTrack, int lastTrack)
{
    if (audioRenderInProgress || projectFileOperationInProgress
        || ! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || scanningPlugins)
    {
        audioBrowser.setStatus ("Wait for current loads and operations to finish before consolidating.");
        return;
    }

    if (firstTrack != lastTrack || firstTrack < 0 || firstTrack >= static_cast<int> (project.tracks.size())
        || startBeat < 0.0 || endBeat <= startBeat)
    {
        audioBrowser.setStatus ("Consolidate: select a range on a single audio track.");
        return;
    }

    auto* track = project.tracks[static_cast<std::size_t> (firstTrack)].get();
    if (track->type != roll::TrackType::Audio || track->frozen)
    {
        audioBrowser.setStatus ("Consolidate: select an unfrozen audio track.");
        return;
    }

    constexpr double consolidationSampleRate = 44100.0;
    const double bpm = engine.graph().getBpm();
    if (! std::isfinite (bpm) || bpm <= 0.0)
    {
        audioBrowser.setStatus ("Consolidate: the project BPM is invalid.");
        return;
    }
    const double durationSeconds = (endBeat - startBeat) * 60.0 / bpm;
    const auto frameCountDouble = std::ceil (durationSeconds * consolidationSampleRate);
    if (! std::isfinite (frameCountDouble) || frameCountDouble < 1.0
        || durationSeconds > 600.0
        || frameCountDouble > static_cast<double> (std::numeric_limits<int>::max()))
    {
        audioBrowser.setStatus ("Consolidate: the selection is too large to render (maximum 10 minutes).");
        return;
    }

    bool foundSource = false;
    for (const auto& clip : track->audioClips)
    {
        if (! clip->enabled)
            continue;

        const double overlapStart = std::max (startBeat, clip->start);
        const double overlapEnd = std::min (endBeat, clip->end());
        if (overlapEnd <= overlapStart)
            continue;

        const auto asset = audioAssets.find (clip->filePath);
        if (asset == audioAssets.end() || asset->second == nullptr || asset->second->samples == nullptr
            || asset->second->sampleRate <= 0.0)
        {
            audioBrowser.setStatus ("Consolidate: could not access the source for " + juce::String (clip->name));
            return;
        }

        foundSource = true;
    }

    if (! foundSource)
    {
        audioBrowser.setStatus ("Consolidate: the selection contains no audio clips.");
        return;
    }

    std::vector<int> overlappingIds;
    for (const auto& clip : track->audioClips)
        if (clip->start < endBeat && clip->end() > startBeat)
            overlappingIds.push_back (clip->id);

    auto outputDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile ("MaXXXtudio").getChildFile ("Consolidated");
    if (const auto error = outputDirectory.createDirectory(); error.failed())
    {
        audioBrowser.setStatus ("Consolidate: could not create the destination folder: " + error.getErrorMessage());
        return;
    }
    const auto outputFile = outputDirectory.getNonexistentChildFile (
        "Consolidated_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"),
        ".wav", false);
    const int trackId = track->id;
    const auto trackName = juce::String (track->name);

    releaseComputerMidiNotes();
    juce::String error;
    if (! engine.beginOfflineTrackRender (trackId, error))
    {
        audioBrowser.setStatus ("Could not start consolidation: " + error);
        return;
    }

    audioRenderInProgress = true;
    setAudioRenderUiEnabled (false);
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Consolidating track: " + trackName, "global", 0.0, true,
        [cancelled] { cancelled->store (true, std::memory_order_release); });
    const auto outputSampleRate = engine.offlineRenderSampleRate();
    const auto token = std::weak_ptr<std::atomic<bool>> (lifetimeToken);

    projectFilePool.addJob ([this, token, task, cancelled, outputFile, trackId,
                              startBeat, endBeat, bpm, durationSeconds, outputSampleRate,
                              overlappingIds = std::move (overlappingIds)]
    {
        const auto frameCount = static_cast<int> (std::ceil (durationSeconds * outputSampleRate));
        auto renderedSamples = std::make_shared<juce::AudioBuffer<float>> (2, frameCount);
        int lastReportedPercent = -1;
        const auto renderResult = engine.renderTrackRange (
            startBeat, endBeat - startBeat, *renderedSamples,
            [task, &lastReportedPercent] (double progress)
            {
                const int percent = static_cast<int> (progress * 100.0);
                if (percent != lastReportedPercent)
                {
                    task->setProgress (progress);
                    lastReportedPercent = percent;
                }
            },
            [cancelled] { return cancelled->load (std::memory_order_acquire); });

        auto result = renderResult;
        std::shared_ptr<AudioFileData> data;
        if (result.wasOk())
        {
            auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
            if (! fileStream->openedOk())
            {
                result = juce::Result::fail ("Could not create the WAV file.");
            }
            else
            {
                std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));
                juce::WavAudioFormat wavFormat;
                const juce::AudioFormatWriterOptions writerOptions;
                auto writer = wavFormat.createWriterFor (outputStream,
                    writerOptions.withSampleRate (outputSampleRate).withNumChannels (2).withBitsPerSample (24));
                if (writer == nullptr
                    || ! writer->writeFromAudioSampleBuffer (*renderedSamples, 0, renderedSamples->getNumSamples()))
                {
                    writer.reset();
                    outputStream.reset();
                    result = juce::Result::fail ("Failed to write the WAV file.");
                }
                else
                {
                    writer.reset();
                    data = std::make_shared<AudioFileData>();
                    data->file = outputFile;
                    data->samples = renderedSamples;
                    data->sampleRate = outputSampleRate;
                    data->durationSeconds = durationSeconds;

                    constexpr int maxWaveformBins = 16384;
                    const int waveformBins = std::min (renderedSamples->getNumSamples(), maxWaveformBins);
                    data->waveform.resize (static_cast<std::size_t> (waveformBins));
                    for (int bin = 0; bin < waveformBins; ++bin)
                    {
                        const int sampleCount = renderedSamples->getNumSamples();
                        const int from = static_cast<int> (static_cast<std::int64_t> (bin) * sampleCount / waveformBins);
                        const int to = std::max (from + 1, static_cast<int> (
                            static_cast<std::int64_t> (bin + 1) * sampleCount / waveformBins));
                        float minimum = 1.0f;
                        float maximum = -1.0f;
                        for (int frame = from; frame < std::min (to, sampleCount); ++frame)
                            for (int channel = 0; channel < renderedSamples->getNumChannels(); ++channel)
                            {
                                const float value = renderedSamples->getSample (channel, frame);
                                minimum = std::min (minimum, value);
                                maximum = std::max (maximum, value);
                            }
                        data->waveform[static_cast<std::size_t> (bin)] = { minimum, maximum };
                    }
                }
            }
        }

        if (result.failed())
            outputFile.deleteFile();

        task->end();
        juce::MessageManager::callAsync ([this, token, task, cancelled, outputFile, trackId,
                                          startBeat, endBeat, bpm, durationSeconds, result, data,
                                          overlappingIds = std::move (overlappingIds)]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
            {
                outputFile.deleteFile();
                return;
            }

            engine.finishOfflineRender();
            audioRenderInProgress = false;
            setAudioRenderUiEnabled (true);

            if (cancelled->load (std::memory_order_acquire))
            {
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidation cancelled.");
                return;
            }
            if (result.failed())
            {
                audioBrowser.setStatus ("Consolidation failed: " + result.getErrorMessage());
                return;
            }

            auto* targetTrack = project.track (trackId);
            if (targetTrack == nullptr || targetTrack->type != roll::TrackType::Audio || data == nullptr)
            {
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidate: the track changed during rendering.");
                return;
            }

            project.beginEdit();
            bool replaced = true;
            for (const int clipId : overlappingIds)
            {
                auto* clip = targetTrack->findAudioClip (clipId);
                if (clip == nullptr)
                    continue;
                int innerClipId = clipId;
                if (clip->start < startBeat && clip->end() > startBeat)
                {
                    innerClipId = project.splitAudioClip (trackId, clipId, startBeat);
                    if (innerClipId < 0)
                    {
                        replaced = false;
                        break;
                    }
                }

                clip = targetTrack->findAudioClip (innerClipId);
                if (clip != nullptr && clip->end() > endBeat)
                    project.splitAudioClip (trackId, innerClipId, endBeat);
                clip = targetTrack->findAudioClip (innerClipId);
                if (clip != nullptr && clip->start >= startBeat - 1.0e-9
                    && clip->end() <= endBeat + 1.0e-9)
                    project.deleteAudioClip (trackId, innerClipId);
            }

            const auto path = outputFile.getFullPathName().toStdString();
            audioAssets[path] = data;
            if (replaced && project.addAudioClip (trackId, startBeat, endBeat - startBeat, path,
                                                  outputFile.getFileName().toStdString(), data->waveform,
                                                  durationSeconds, bpm) == nullptr)
                replaced = false;
            project.endEdit();
            if (! replaced)
            {
                project.undo();
                audioAssets.erase (path);
                outputFile.deleteFile();
                audioBrowser.setStatus ("Consolidate: could not replace the selection.");
                return;
            }

            audioBrowser.setStatus ("Selection consolidated: " + outputFile.getFileName()
                                    + " (track effects applied)");
        });
    });
}

void MainComponent::freezeTrack (int trackId)
{
    auto* track = project.track (trackId);
    if (trackId <= 0 || track == nullptr || track->type == roll::TrackType::Group || track->frozen)
        return;
    if (audioRenderInProgress || projectFileOperationInProgress
        || ! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || scanningPlugins)
    {
        setProjectStatus ("Wait for current loads and operations to finish before freezing.");
        return;
    }

    double endBeat = 0.0;
    if (track->type == roll::TrackType::Audio)
    {
        for (const auto& clip : track->audioClips)
        {
            endBeat = std::max (endBeat, clip->end());
            if (! clip->enabled)
                continue;
            const auto asset = audioAssets.find (clip->filePath);
            if (asset == audioAssets.end() || asset->second == nullptr || asset->second->samples == nullptr)
            {
                setProjectStatus ("Freeze: could not access the source for " + juce::String (clip->name));
                return;
            }
        }
    }
    else
    {
        for (const auto& clip : track->clips)
            endBeat = std::max (endBeat, clip->end());
    }
    if (endBeat <= 0.0)
    {
        setProjectStatus ("Freeze: the track has no clips to render.");
        return;
    }

    const double bpm = engine.graph().getBpm();
    if (! std::isfinite (bpm) || bpm <= 0.0)
    {
        setProjectStatus ("Freeze: the project BPM is invalid.");
        return;
    }
    const double tailSeconds = engine.trackTailLengthSeconds (trackId);
    const double renderEndBeat = endBeat + tailSeconds * bpm / 60.0;
    const double durationSeconds = renderEndBeat * 60.0 / bpm;
    if (! std::isfinite (durationSeconds) || durationSeconds <= 0.0 || durationSeconds > 600.0)
    {
        setProjectStatus ("Freeze: the track is too long to render (maximum 10 minutes).");
        return;
    }

    auto outputDirectory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("MaXXXtudio").getChildFile ("Frozen");
    if (const auto error = outputDirectory.createDirectory(); error.failed())
    {
        setProjectStatus ("Freeze: could not create the cache folder: " + error.getErrorMessage());
        return;
    }
    const auto outputFile = outputDirectory.getNonexistentChildFile (
        "Frozen_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"),
        ".wav", false);
    const auto trackName = juce::String (track->name);

    releaseComputerMidiNotes();
    juce::String error;
    if (! engine.beginOfflineTrackRender (trackId, error))
    {
        setProjectStatus ("Could not start freezing: " + error);
        return;
    }

    audioRenderInProgress = true;
    setAudioRenderUiEnabled (false);
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Freezing track: " + trackName, "global", 0.0, true,
        [cancelled] { cancelled->store (true, std::memory_order_release); });
    const auto outputSampleRate = engine.offlineRenderSampleRate();
    const auto token = std::weak_ptr<std::atomic<bool>> (lifetimeToken);

    projectFilePool.addJob ([this, token, task, cancelled, outputFile, trackId,
                              renderEndBeat, bpm, durationSeconds, outputSampleRate]
    {
        const auto frameCountDouble = std::ceil (durationSeconds * outputSampleRate);
        if (! std::isfinite (frameCountDouble) || frameCountDouble < 1.0
            || frameCountDouble > static_cast<double> (std::numeric_limits<int>::max()))
        {
            juce::MessageManager::callAsync ([this, token, outputFile, task]
            {
                if (const auto alive = token.lock(); alive != nullptr && alive->load())
                {
                    engine.finishOfflineRender();
                    audioRenderInProgress = false;
                    setAudioRenderUiEnabled (true);
                    outputFile.deleteFile();
                    task->end();
                    setProjectStatus ("Freeze: the render buffer is too large.");
                }
            });
            return;
        }

        const int frameCount = static_cast<int> (frameCountDouble);
        auto renderedSamples = std::make_shared<juce::AudioBuffer<float>> (2, frameCount);
        int lastReportedPercent = -1;
        auto result = engine.renderTrackRange (
            0.0, renderEndBeat, *renderedSamples,
            [task, &lastReportedPercent] (double progress)
            {
                const int percent = static_cast<int> (progress * 100.0);
                if (percent != lastReportedPercent)
                {
                    task->setProgress (progress);
                    lastReportedPercent = percent;
                }
            },
            [cancelled] { return cancelled->load (std::memory_order_acquire); });

        std::shared_ptr<AudioFileData> data;
        if (result.wasOk())
        {
            auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
            if (! fileStream->openedOk())
                result = juce::Result::fail ("Could not create the frozen audio file.");
            else
            {
                std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));
                juce::WavAudioFormat wavFormat;
                const juce::AudioFormatWriterOptions writerOptions;
                auto writer = wavFormat.createWriterFor (outputStream,
                    writerOptions.withSampleRate (outputSampleRate).withNumChannels (2).withBitsPerSample (24));
                if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (
                        *renderedSamples, 0, renderedSamples->getNumSamples()))
                {
                    writer.reset();
                    outputStream.reset();
                    result = juce::Result::fail ("Failed to write the frozen audio file.");
                }
                else
                {
                    writer.reset();
                    data = std::make_shared<AudioFileData>();
                    data->file = outputFile;
                    data->samples = renderedSamples;
                    data->sampleRate = outputSampleRate;
                    data->durationSeconds = durationSeconds;
                    constexpr int maxWaveformBins = 16384;
                    const int waveformBins = std::min (renderedSamples->getNumSamples(), maxWaveformBins);
                    data->waveform.resize (static_cast<std::size_t> (waveformBins));
                    for (int bin = 0; bin < waveformBins; ++bin)
                    {
                        const int from = static_cast<int> (
                            static_cast<std::int64_t> (bin) * frameCount / waveformBins);
                        const int to = std::max (from + 1, static_cast<int> (
                            static_cast<std::int64_t> (bin + 1) * frameCount / waveformBins));
                        float minimum = 1.0f;
                        float maximum = -1.0f;
                        for (int frame = from; frame < std::min (to, frameCount); ++frame)
                            for (int channel = 0; channel < renderedSamples->getNumChannels(); ++channel)
                            {
                                const float value = renderedSamples->getSample (channel, frame);
                                minimum = std::min (minimum, value);
                                maximum = std::max (maximum, value);
                            }
                        data->waveform[static_cast<std::size_t> (bin)] = { minimum, maximum };
                    }
                }
            }
        }
        if (result.failed())
            outputFile.deleteFile();

        task->end();
        juce::MessageManager::callAsync ([this, token, task, cancelled, outputFile, trackId,
                                          renderEndBeat, bpm, durationSeconds, result, data]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
            {
                outputFile.deleteFile();
                return;
            }
            engine.finishOfflineRender();
            audioRenderInProgress = false;
            setAudioRenderUiEnabled (true);
            if (cancelled->load (std::memory_order_acquire))
            {
                outputFile.deleteFile();
                setProjectStatus ("Freeze cancelled.");
                return;
            }
            if (result.failed())
            {
                setProjectStatus ("Freeze failed: " + result.getErrorMessage());
                return;
            }

            auto* targetTrack = project.track (trackId);
            if (targetTrack == nullptr || targetTrack->type == roll::TrackType::Group
                || targetTrack->frozen || data == nullptr)
            {
                outputFile.deleteFile();
                setProjectStatus ("Freeze: the track changed during rendering.");
                return;
            }

            const auto path = outputFile.getFullPathName().toStdString();
            audioAssets[path] = data;
            auto frozenClip = std::make_unique<roll::AudioPlacement>();
            frozenClip->id = targetTrack->nextClipId();
            frozenClip->start = 0.0;
            frozenClip->lengthBeats = renderEndBeat;
            frozenClip->durationSeconds = durationSeconds;
            frozenClip->sourceBpm = bpm;
            frozenClip->warpEnabled = false;
            frozenClip->name = targetTrack->name + " (Frozen)";
            frozenClip->filePath = path;
            frozenClip->waveform = data->waveform;

            project.beginEdit();
            targetTrack->frozenAudioClip = std::move (frozenClip);
            targetTrack->frozen = true;
            targetTrack->freezeReversible = true;
            project.growTo (renderEndBeat + 64.0);
            project.selectClip (trackId, -1);
            project.endEdit();
            if (project.onTrackFreezeChanged)
                project.onTrackFreezeChanged (trackId);
            project.sendChangeMessage();
            setProjectStatus ("Track frozen: " + outputFile.getFileName());
        });
    });
}

void MainComponent::unfreezeTrack (int trackId)
{
    auto* track = project.track (trackId);
    if (trackId <= 0 || track == nullptr || ! track->frozen || ! track->freezeReversible)
        return;
    if (audioRenderInProgress || projectFileOperationInProgress
        || restoringProjectPlugins || pendingAudioLoads > 0 || scanningPlugins)
    {
        setProjectStatus ("Wait for current loads and operations to finish before unfreezing.");
        return;
    }

    project.beginEdit();
    track->frozen = false;
    track->frozenAudioClip.reset();
    project.endEdit();
    if (project.onTrackFreezeChanged)
        project.onTrackFreezeChanged (trackId);
    project.sendChangeMessage();
    setProjectStatus ("Track unfrozen.");
}

void MainComponent::flattenTrack (int trackId)
{
    auto* track = project.track (trackId);
    if (trackId <= 0 || track == nullptr || ! track->frozen || ! track->freezeReversible
        || track->frozenAudioClip == nullptr)
        return;
    if (audioRenderInProgress || projectFileOperationInProgress
        || restoringProjectPlugins || pendingAudioLoads > 0 || scanningPlugins)
    {
        setProjectStatus ("Wait for current loads and operations to finish before flattening.");
        return;
    }

    closeWindowsOfTrack (trackId);
    project.beginExternalEdit ({ trackId });
    for (const auto& device : engine.devices (trackId))
        engine.removeDevice (trackId, device.id);
    std::erase_if (track->automation, [] (const auto& envelope)
    {
        return envelope.target.type == roll::AutomationParameterType::PluginParameter
            || envelope.target.type == roll::AutomationParameterType::AudioClipGain
            || envelope.target.type == roll::AutomationParameterType::AudioClipPitch
            || envelope.target.type == roll::AutomationParameterType::AudioClipFadeIn
            || envelope.target.type == roll::AutomationParameterType::AudioClipFadeOut;
    });
    const int frozenClipId = track->frozenAudioClip->id;
    track->frozenAudioClip->name = track->name;
    track->clips.clear();
    track->audioClips.clear();
    track->audioClips.push_back (std::move (track->frozenAudioClip));
    if (track->type == roll::TrackType::Midi)
        track->type = roll::TrackType::Audio;
    track->frozen = false;
    track->freezeReversible = true;
    project.endEdit();
    refreshDeviceView();
    if (project.onTrackFreezeChanged)
        project.onTrackFreezeChanged (trackId);
    project.selectAudioClip (trackId, frozenClipId);
    project.sendChangeMessage();
    setProjectStatus ("Track flattened to audio.");
}

void MainComponent::publishAudioTrack (int trackId)
{
    std::vector<AudioClipPlayback> clips;
    if (const auto* track = project.track (trackId); track != nullptr)
    {
        const auto appendClip = [&] (const roll::AudioPlacement& clip)
        {
            if (! clip.enabled)
                return;
            const auto asset = audioAssets.find (clip.filePath);
            if (asset == audioAssets.end())
                return;

            AudioClipPlayback playback {
                clip.start, clip.lengthBeats, clip.sourceBpm,
                clip.sourceOffsetSeconds, clip.fadeInBeats,
                clip.fadeOutBeats, clip.fadeInCurve, clip.fadeOutCurve,
                clip.pitchSemitones + clip.pitchCents / 100.0,
                clip.gainDb, clip.warpMode, asset->second,
                clip.warpEnabled, clip.reverse
            };
            playback.durationSeconds = clip.durationSeconds;
            playback.warpMarkers = clip.warpMarkers;
            for (const auto& envelope : track->automation)
                if (envelope.target.clipId == clip.id)
                    playback.automation.push_back (envelope);
            clips.push_back (std::move (playback));
        };
        if (track->frozen)
        {
            if (track->frozenAudioClip != nullptr)
                appendClip (*track->frozenAudioClip);
        }
        else if (track->type == roll::TrackType::Audio)
            for (const auto& clip : track->audioClips)
                appendClip (*clip);
    }
    engine.publishAudioClips (trackId, clips);

    for (auto it = audioAssets.begin(); it != audioAssets.end();)
        if (! project.retainsAudioFile (it->first))
            it = audioAssets.erase (it);
        else
            ++it;
}

bool MainComponent::selectedTrackSpan (double& start, double& end) const
{
    const auto* track = project.selectedTrack();
    if (track == nullptr)
        return false;

    start = std::numeric_limits<double>::max();
    end = 0.0;
    if (track->frozen)
    {
        if (track->frozenAudioClip != nullptr)
        {
            start = track->frozenAudioClip->start;
            end = track->frozenAudioClip->end();
        }
    }
    else if (track->type == roll::TrackType::Audio)
    {
        for (const auto& clip : track->audioClips)
        {
            start = std::min (start, clip->start);
            end = std::max (end, clip->end());
        }
    }
    else
    {
        for (const auto& clip : track->clips)
        {
            start = std::min (start, clip->start);
            end = std::max (end, clip->end());
        }
    }
    return start != std::numeric_limits<double>::max();
}

void MainComponent::playSelectedTrack()
{
    if (engine.graph().isPlaying())
    {
        transport.togglePlay();
        return;
    }

    double selectionStart = 0.0;
    if (arrangement.getTimeSelectionStart (selectionStart))
    {
        pendingArrangementSeek = true;
        pendingArrangementSeekBeat = selectionStart;
        transportPosition = selectionStart;
        engine.graph().seek (selectionStart);
        arrangement.setPlayhead (selectionStart);
        editor.setPlayhead (selectionStart - model.clipStartBeats);
        transport.togglePlay();
        return;
    }

    if (pendingArrangementSeek
        && std::abs (transportPosition - pendingArrangementSeekBeat) < 1.0e-6)
    {
        transportPosition = pendingArrangementSeekBeat;
        engine.graph().seek (pendingArrangementSeekBeat);
        arrangement.setPlayhead (pendingArrangementSeekBeat);
        editor.setPlayhead (pendingArrangementSeekBeat - model.clipStartBeats);
        transport.togglePlay();
        return;
    }
    pendingArrangementSeek = false;

    double start = 0.0, end = 0.0;
    const auto* selectedTrack = project.selectedTrack();
    const auto* selectedAudio = selectedTrack != nullptr && selectedTrack->type == roll::TrackType::Audio
        ? selectedTrack->findAudioClip (project.selectedAudioClipId) : nullptr;
    const auto* selectedMidi = project.selectedClip();
    if (selectedAudio != nullptr)
    {
        start = selectedAudio->start;
        end = selectedAudio->end();
    }
    else if (selectedMidi != nullptr)
    {
        start = selectedMidi->start;
        end = selectedMidi->end();
    }

    if (selectedAudio != nullptr || selectedMidi != nullptr || selectedTrackSpan (start, end))
    {
        if (engine.graph().isLoop() && (start < project.loopStart || end > project.loopEnd))
            transport.setLoopEnabled (false);
        transportPosition = start;
        engine.graph().seek (start);
        arrangement.setPlayhead (start);
        editor.setPlayhead (start - model.clipStartBeats);
    }
    else
    {
        engine.graph().seek (0.0);
        arrangement.setPlayhead (0.0);
    }
    transport.togglePlay();
}

void MainComponent::resumeFromPausedPosition()
{
    if (engine.graph().isPlaying())
        return;

    pendingArrangementSeek = false;
    transportPosition = pausedPosition;
    arrangement.setPlayhead (pausedPosition);
    editor.setPlayhead (pausedPosition - model.clipStartBeats);
    transport.resumeFromPosition (pausedPosition);
}

void MainComponent::seekTransport (double beats)
{
    finishRecording();
    const double position = std::isfinite (beats) ? std::max (0.0, beats) : 0.0;
    pendingArrangementSeek = false;
    suppressSongStartOnce = false;
    transportPaused = false;
    transportPosition = position;
    pausedPosition = position;
    engine.graph().seek (position);
    arrangement.setPlayhead (position);
    arrangement.scrollToBeat (position);
    editor.setPlayhead (position - model.clipStartBeats);
}

double MainComponent::projectEndBeat() const
{
    double endBeat = 0.0;
    for (const auto& track : project.tracks)
    {
        if (track->type == roll::TrackType::Audio)
        {
            for (const auto& clip : track->audioClips)
                endBeat = std::max (endBeat, clip->end());
        }
        else
        {
            for (const auto& clip : track->clips)
                endBeat = std::max (endBeat, clip->end());
        }
    }
    return endBeat;
}

bool MainComponent::handleFollowKeyPress (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const bool ctrl = modifiers.isCtrlDown() || modifiers.isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (
        static_cast<juce::juce_wchar> (key.getKeyCode()));
    if (! ctrl || ! modifiers.isShiftDown() || modifiers.isAltDown()
        || keyCode != 'F' || isTextEditorFocused())
        return false;

    setFollowArrangementEnabled (! followArrangement);
    return true;
}

void MainComponent::setFollowArrangementEnabled (bool enabled)
{
    followArrangement = enabled;
    transport.setFollowEnabled (enabled);
    if (enabled)
        arrangement.scrollToBeat (transportPosition);
    setProjectStatus (enabled ? "Arrangement Follow enabled (Ctrl+Shift+F)."
                              : "Arrangement Follow disabled.");
}

bool MainComponent::handleTransportKeyPress (const juce::KeyPress& key)
{
    if (audioRenderInProgress)
        return true;

    if (key.isKeyCode (juce::KeyPress::spaceKey) && key.getModifiers().isShiftDown())
    {
        resumeFromPausedPosition();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        playSelectedTrack();
        return true;
    }

    return false;
}

void MainComponent::loopSelectedTrack()
{
    if (engine.graph().isLoop())
    {
        transport.setLoopEnabled (false);
        return;
    }

    double start = 0.0, end = 0.0;
    const auto* track = project.selectedTrack();
    if (track != nullptr && track->frozen)
    {
        if (! selectedTrackSpan (start, end))
            return;
        project.setLoop (start, end);
        transport.setLoopEnabled (true);
        return;
    }
    const auto* audio = track != nullptr && track->type == roll::TrackType::Audio
        ? track->findAudioClip (project.selectedAudioClipId) : nullptr;
    const auto* midi = project.selectedClip();
    if (audio != nullptr)
    {
        start = audio->start;
        end = audio->end();
    }
    else if (midi != nullptr)
    {
        start = midi->start;
        end = midi->end();
    }
    else if (! selectedTrackSpan (start, end))
        return;

    project.setLoop (start, end);
    transport.setLoopEnabled (true);
}

// ------------------------------------------------------------ Keyboard

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (pendingMidiLearn.has_value() && key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        pendingMidiLearn.reset();
        setProjectStatus ("MIDI Learn cancelled.");
        return true;
    }

    if (isTextEditorFocused())
        return false;
    if (handleFollowKeyPress (key))
        return true;

    const auto modifiers = key.getModifiers();
    const bool ctrl = modifiers.isCtrlDown() || modifiers.isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (
        static_cast<juce::juce_wchar> (key.getKeyCode()));
    if (isPianoRollToggleShortcut (key) && ! isTextEditorFocused())
    {
        setPianoRollVisible (! pianoRollVisible);
        return true;
    }
    const bool undo = ctrl && keyCode == 'Z' && ! modifiers.isShiftDown();
    const bool redo = ctrl && (keyCode == 'Y' || (keyCode == 'Z' && modifiers.isShiftDown()));
    if (! undo && ! redo)
        return false;

    if (! pluginHistoryRestoreInProgress && ! pluginCopyInProgress)
    {
        if (redo)
            project.redo();
        else
            project.undo();
    }
    return true;
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (pendingMidiLearn.has_value() && k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        pendingMidiLearn.reset();
        setProjectStatus ("MIDI Learn cancelled.");
        return true;
    }

    if (audioRenderInProgress)
        return true;
    if (handleFollowKeyPress (k))
        return true;

#if JUCE_DEBUG
    if (k.getKeyCode() == juce::KeyPress::F10Key
        && k.getModifiers().isCtrlDown() && k.getModifiers().isAltDown())
    {
        startSimulatedTask();
        return true;
    }
#endif

    const bool ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (
        static_cast<juce::juce_wchar> (k.getKeyCode()));
    if (isPianoRollToggleShortcut (k) && ! isTextEditorFocused())
    {
        setPianoRollVisible (! pianoRollVisible);
        return true;
    }
    if (ctrl && ! k.getModifiers().isAltDown() && ! k.getModifiers().isShiftDown()
        && (k.getTextCharacter() == ',' || k.getKeyCode() == ','))
    {
        showOptionsWindow();
        return true;
    }
    const bool zeroKey = k.getKeyCode() == '0'
                      || k.getKeyCode() == juce::KeyPress::numberPad0
                      || k.getTextCharacter() == '0';
    if (zeroKey && ! ctrl && ! k.getModifiers().isAltDown()
        && ! k.getModifiers().isShiftDown() && editor.hasKeyboardFocus (true))
    {
        model.toggleMuteSelected();
        return true;
    }

    if (juce::Process::isForegroundProcess() && ! isTextEditorFocused()
        && ! ctrl && ! k.getModifiers().isAltDown() && keyCode == 'M')
    {
        if (! computerMidiModeKeyDown)
        {
            computerMidiModeKeyDown = true;
            setComputerMidiKeyboardEnabled (! computerMidiKeyboardEnabled);
        }
        return true;
    }

    if (computerMidiKeyboardEnabled && juce::Process::isForegroundProcess() && ! isTextEditorFocused()
        && ! ctrl && ! k.getModifiers().isAltDown() && ! k.getModifiers().isShiftDown())
    {
        const auto& keyboardMap = DrumRackProcessor::computerMidiKeyboardMap;
        const bool isMappedKey = std::any_of (keyboardMap.begin(), keyboardMap.end(),
            [keyCode] (const auto& mapping) { return mapping.first == static_cast<int> (keyCode); });
        if (isMappedKey || keyCode == 'Z' || keyCode == 'X' || keyCode == 'C' || keyCode == 'V')
        {
            updateComputerMidiKeyboard();
            return true;
        }
    }

    if (! ctrl && ! k.getModifiers().isAltDown() && ! k.getModifiers().isShiftDown()
        && ! isTextEditorFocused() && keyCode == 'A')
    {
        arrangement.toggleAutomationModeFromKey();
        return true;
    }

    if (ctrl && keyCode == 'S')
    {
        if (currentProjectFile.existsAsFile())
            saveProject (currentProjectFile);
        else
            chooseProjectFile (false);
        return true;
    }
    if (ctrl && keyCode == 'O')
    {
        chooseProjectFile (true);
        return true;
    }
    if (ctrl && keyCode == 'D' && ! isTextEditorFocused()
        && ! editor.hasKeyboardFocus (true) && arrangement.hasSelectionToDuplicate())
    {
        arrangement.duplicateSelection();
        return true;
    }
    if (ctrl && keyCode == 'D' && ! isTextEditorFocused()
        && ! arrangement.hasKeyboardFocus (true) && ! editor.hasKeyboardFocus (true)
        && project.selectedTrackId > 0 && project.selectedClipId < 0
        && project.selectedAudioClipId < 0)
    {
        double selectionStart = 0.0;
        if (! arrangement.getTimeSelectionStart (selectionStart))
        {
            duplicateSelectedTrack (project.selectedTrackId);
            return true;
        }
    }
    if (ctrl && keyCode == 'Z' && ! k.getModifiers().isShiftDown())
        return pluginHistoryRestoreInProgress || pluginCopyInProgress || project.undo();
    if (ctrl && (keyCode == 'Y' || (keyCode == 'Z' && k.getModifiers().isShiftDown())))
        return pluginHistoryRestoreInProgress || pluginCopyInProgress || project.redo();

    if (k.getModifiers().isCtrlDown()
        && juce::CharacterFunctions::toLowerCase (k.getKeyCode()) == 'b')
    {
        setBrowserVisible (! browser.isVisible());
        return true;
    }
    if (k.getModifiers().isCtrlDown()
        && juce::CharacterFunctions::toLowerCase (k.getKeyCode()) == 'l')
    {
        loopSelectedTrack();
        return true;
    }
    if (handleTransportKeyPress (k))
        return true;
    if (k == juce::KeyPress::homeKey)  { seekTransport (0.0); return true; }
    if (k == juce::KeyPress (juce::KeyPress::F12Key) || k == juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0))
    {
        showDetail (showingMixer ? false : ! showingDevices);
        return true;
    }
    if (k == juce::KeyPress (juce::KeyPress::F6Key)) { cycleFocus(); return true; }
    if (ctrl && keyCode == 'T')
    {
        project.addTrack (k.getModifiers().isShiftDown()
            ? roll::TrackType::Midi : roll::TrackType::Audio);
        return true;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    updateComputerMidiKeyboard();
    return false;
}

void MainComponent::cycleFocus()
{
    // Keyboard navigation: plugin list -> arrangement -> piano roll.
    if (browser.hasKeyboardFocus (true))          arrangement.grabFocus();
    else if (arrangement.hasKeyboardFocus (true)) { if (showingDevices) showDetail (false); editor.grabFocus(); }
    else                                          browser.grabFocus();
}

// ------------------------------------------------------------ projeto / engine

juce::String MainComponent::deviceSummary (int trackId) const
{
    juce::StringArray names;
    for (const auto& d : engine.devices (trackId)) names.add (d.name);
    const auto summary = names.joinIntoString (" > ");
    if (trackId == 0) return summary.isNotEmpty() ? summary : theme::ui ("Master Effects");
    const auto* track = project.track (trackId);
    if (summary.isNotEmpty())
        return summary;
    if (track == nullptr)
        return {};
    if (track->type == roll::TrackType::Group)
        return theme::ui ("Group Bus");
    return track->type == roll::TrackType::Audio ? theme::ui ("Audio Channel") : juce::String();
}

void MainComponent::applyMixers()
{
    bool anySolo = false;
    for (const auto& t : project.tracks) anySolo |= t->solo;
    for (const auto& t : project.tracks)
    {
        engine.setTrackRouting (t->id, t->outputRouting, t->parentGroupId);
        bool audibleWhenSoloed = t->solo;
        if (t->type == roll::TrackType::Group)
        {
            audibleWhenSoloed |= std::any_of (project.tracks.begin(), project.tracks.end(),
                [id = t->id] (const auto& child) { return child->parentGroupId == id && child->solo; });
        }
        else if (t->parentGroupId != 0)
        {
            if (const auto* group = project.track (t->parentGroupId))
                audibleWhenSoloed |= group->solo;
        }
        engine.setMixer (t->id, static_cast<float> (t->volume), static_cast<float> (t->pan),
                         t->mute || (anySolo && ! audibleWhenSoloed), t->mute);
        engine.setTrackAutomation (t->id, t->automation);
    }
    if (auto* m = project.masterTrack.get())
    {
        engine.setMasterMixer (static_cast<float> (m->volume), static_cast<float> (m->pan), m->mute);
        engine.setTrackAutomation (AudioEngine::masterTrackId, m->automation);
    }
}

void MainComponent::requestMidiLearn (int trackId, MidiMappingTarget target)
{
    const bool masterTarget = target == MidiMappingTarget::MasterVolume
                           || target == MidiMappingTarget::MasterPan;
    if ((masterTarget && trackId != AudioEngine::masterTrackId)
        || (! masterTarget && project.track (trackId) == nullptr))
        return;

    pendingMidiLearn = PendingMidiLearn { trackId, target };
    const auto controlName = target == MidiMappingTarget::TrackVolume ? "Volume"
                           : target == MidiMappingTarget::TrackPan ? "Pan"
                           : target == MidiMappingTarget::TrackMute ? "Mute"
                           : target == MidiMappingTarget::TrackSolo ? "Solo"
                           : target == MidiMappingTarget::MasterVolume ? "Volume Master"
                           : "Pan Master";
    setProjectStatus (juce::String ("MIDI Learn: move a control to ") + controlName
                      + " (CC; Mute/Solo also accept notes; Esc cancels).");
}

void MainComponent::clearMidiMapping (int trackId, MidiMappingTarget target)
{
    const auto mappingExists = std::any_of (project.midiMappings.begin(), project.midiMappings.end(),
        [trackId, target] (const MidiMapping& mapping)
        {
            return mapping.trackId == trackId && mapping.target == target;
        });
    if (! mappingExists)
        return;

    project.beginEdit();
    std::erase_if (project.midiMappings, [trackId, target] (const MidiMapping& mapping)
    {
        return mapping.trackId == trackId && mapping.target == target;
    });
    project.endEdit();
    lastMidiButtonValues.erase ({ trackId, static_cast<int> (target) });
    project.sendChangeMessage();
    arrangement.refreshMidiMappingInfo();
    setProjectStatus ("MIDI mapping removed.");
}

bool MainComponent::startRecording (bool loopWasEnabled)
{
    if (recordingSession != nullptr)
    {
        setProjectStatus ("A recording is already active.");
        return false;
    }
    if (audioFinalizationInProgress)
    {
        setProjectStatus ("Wait for the previous audio recording to finish processing.");
        return false;
    }

    auto* track = project.selectedTrack();
    if (track == nullptr)
    {
        setProjectStatus ("Select a track before recording.");
        return false;
    }
    if (track->type != roll::TrackType::Audio && track->type != roll::TrackType::Midi)
    {
        setProjectStatus ("Recording is not supported on this track type.");
        return false;
    }
    if (track->frozen)
    {
        setProjectStatus ("Unfreeze the selected track before recording.");
        return false;
    }

    auto session = std::make_unique<RecordingSession>();
    session->trackId = track->id;
    session->startBeat = std::max (0.0, engine.graph().positionBeats());
    session->startBpm = std::clamp (engine.graph().getBpm(), 20.0, 999.0);
    session->loopWasEnabled = loopWasEnabled;

    if (track->type == roll::TrackType::Midi)
    {
        if (! track->isFree (session->startBeat, 1.0 / 16.0))
        {
            setProjectStatus ("Recording cannot start: the selected MIDI track already has a clip here.");
            return false;
        }
        session->kind = RecordingSession::Kind::Midi;
    }
    else
    {
        auto* device = engine.audioDeviceManager().getCurrentAudioDevice();
        if (device == nullptr)
        {
            setProjectStatus ("Audio recording unavailable: no audio device is active.");
            return false;
        }
        const auto activeInputs = device->getActiveInputChannels();
        const int inputCount = activeInputs.countNumberOfSetBits();
        session->sampleRate = device->getCurrentSampleRate();
        if (inputCount == 0)
        {
            setProjectStatus ("Audio recording unavailable: enable an input channel in Audio Preferences.");
            return false;
        }
        if (! std::isfinite (session->sampleRate) || session->sampleRate <= 0.0)
        {
            setProjectStatus ("Audio recording unavailable: the input device has an invalid sample rate.");
            return false;
        }
        if (! track->isAudioFree (session->startBeat, 1.0 / 16.0))
        {
            setProjectStatus ("Recording cannot start: the selected audio track already has a clip here.");
            return false;
        }

        session->kind = RecordingSession::Kind::Audio;
        session->audioChannels = std::clamp (inputCount, 1, 2);
        auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("MaXXXtudio").getChildFile ("Recordings");
        const auto folderError = folder.createDirectory();
        if (folderError.failed())
        {
            setProjectStatus ("Could not create the audio recording folder: " + folderError.getErrorMessage());
            return false;
        }

        session->audioFile = folder.getNonexistentChildFile ("Recording", ".wav", true);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> output = session->audioFile.createOutputStream();
        if (output == nullptr)
        {
            setProjectStatus ("Could not create an audio recording file.");
            return false;
        }
        const juce::AudioFormatWriterOptions writerOptions;
        auto writer = wav.createWriterFor (output, writerOptions.withSampleRate (session->sampleRate)
            .withNumChannels (session->audioChannels).withBitsPerSample (24));
        if (writer == nullptr)
        {
            session->audioFile.deleteFile();
            setProjectStatus ("Could not initialize the WAV recording writer.");
            return false;
        }
        if (! session->writerThread.startThread())
        {
            session->audioFile.deleteFile();
            setProjectStatus ("Could not start the audio recording writer.");
            return false;
        }
        const int bufferedSamples = juce::jlimit (8192, 262144,
            static_cast<int> (std::min (session->sampleRate * 4.0, 262144.0)));
        session->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer.release(), session->writerThread, bufferedSamples);
    }

    if (session->loopWasEnabled)
        transport.setLoopEnabled (false);
    session->generation = nextRecordingGeneration.fetch_add (1, std::memory_order_relaxed);
    recordingSession = std::move (session);
    activeRecordingGeneration.store (recordingSession->generation, std::memory_order_release);
    activeRecordingSession.store (recordingSession.get(), std::memory_order_seq_cst);
    setProjectStatus (recordingSession->kind == RecordingSession::Kind::Audio
        ? "Recording audio input to the selected track..."
        : "Recording MIDI input to the selected track...");
    return true;
}

void MainComponent::finishRecording (bool discard)
{
    if (recordingSession == nullptr)
        return;

    transport.setRecording (false);
    activeRecordingGeneration.store (0, std::memory_order_release);
    activeRecordingSession.store (nullptr, std::memory_order_seq_cst);
    while (recordingCallbacksInFlight.load (std::memory_order_seq_cst) != 0)
        juce::Thread::sleep (1);

    auto session = std::move (recordingSession);
    const double endBeat = std::max (session->startBeat, engine.graph().positionBeats());
    if (session->loopWasEnabled)
        transport.setLoopEnabled (true);
    const auto reportFailure = [this] (const juce::String& reason)
    {
        setProjectStatus ("Recording failed: " + reason);
    };

    if (session->kind == RecordingSession::Kind::Audio)
    {
        session->writer.reset();
        session->writerThread.stopThread (5000);
        if (discard)
        {
            session->audioFile.deleteFile();
            return;
        }
        if (session->overflow.load())
        {
            session->audioFile.deleteFile();
            reportFailure ("the audio writer could not keep up; no clip was created.");
            return;
        }
        if (session->inputLost.load())
        {
            session->audioFile.deleteFile();
            reportFailure ("an input channel stopped delivering audio; no clip was created.");
            return;
        }

        const auto audioFile = session->audioFile;
        const int trackId = session->trackId;
        const double startBeat = session->startBeat;
        const double sourceBpm = std::clamp (session->startBpm, 20.0, 300.0);
        const int generation = projectGeneration;
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        audioFinalizationInProgress = true;
        ++pendingAudioLoads;
        updateActionIndicator();
        setProjectStatus ("Finalizing audio recording...");
        engine.loadAudioFileAsync (audioFile,
            [this, token, audioFile, trackId, startBeat, sourceBpm, generation, reportFailure]
            (const juce::String& error, std::shared_ptr<const AudioFileData> data)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            audioFinalizationInProgress = false;
            pendingAudioLoads = std::max (0, pendingAudioLoads - 1);
            updateActionIndicator();
            if (generation != projectGeneration)
            {
                audioFile.deleteFile();
                return;
            }
            if (error.isNotEmpty() || data == nullptr || data->durationSeconds <= 0.0
                || data->waveform.empty())
            {
                audioFile.deleteFile();
                reportFailure (error.isNotEmpty() ? error : "the recorded WAV file was empty.");
                return;
            }

            auto* target = project.track (trackId);
            const double lengthBeats = data->durationSeconds * sourceBpm / 60.0;
            if (target == nullptr || target->type != roll::TrackType::Audio
                || ! target->isAudioFree (startBeat, lengthBeats))
            {
                audioFile.deleteFile();
                reportFailure (target == nullptr ? "the target track no longer exists."
                    : "the recorded range overlaps an existing clip.");
                return;
            }

            const auto path = audioFile.getFullPathName().toStdString();
            audioAssets[path] = data;
            if (project.addAudioClip (trackId, startBeat, lengthBeats, path,
                                      audioFile.getFileName().toStdString(), data->waveform,
                                      data->durationSeconds, sourceBpm) == nullptr)
            {
                audioAssets.erase (path);
                audioFile.deleteFile();
                reportFailure ("the recorded clip could not be inserted.");
                return;
            }
            setProjectStatus ("Audio recording saved as " + audioFile.getFileName() + ".");
        }, true);
        return;
    }

    const auto capturedEvents = std::min (session->midiEventCount.load (std::memory_order_acquire),
                                          RecordingSession::midiEventCapacity);
    if (discard)
        return;
    if (session->overflow.load())
    {
        reportFailure ("the MIDI event buffer filled; no clip was created.");
        return;
    }
    std::vector<roll::RecordedMidiEvent> events (
        session->midiEvents.begin(), session->midiEvents.begin() + capturedEvents);
    const auto notes = roll::makeRecordedNotes (events, session->startBeat, endBeat);
    if (notes.empty())
    {
        setProjectStatus ("No MIDI notes were received; no clip was created.");
        return;
    }
    auto* target = project.track (session->trackId);
    if (target == nullptr || target->type != roll::TrackType::Midi)
    {
        reportFailure ("the target track no longer exists.");
        return;
    }
    double lengthBeats = 0.25;
    for (const auto& note : notes)
        lengthBeats = std::max (lengthBeats, note.endBeats());
    lengthBeats = std::ceil (lengthBeats * 16.0) / 16.0;
    if (! target->isFree (session->startBeat, lengthBeats))
    {
        reportFailure ("the recorded range overlaps an existing clip.");
        return;
    }
    auto* clip = project.addClip (session->trackId, session->startBeat, lengthBeats);
    if (clip == nullptr)
    {
        reportFailure ("a clip could not be inserted because the track changed or became occupied.");
        return;
    }
    for (auto note : notes)
        clip->clip.addNote (note);
    project.notifyTrackNotes (session->trackId);
    setProjectStatus ("MIDI recording created " + juce::String (notes.size()) + " notes.");
}

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                       int numInputChannels,
                                                       float* const* outputChannelData,
                                                       int numOutputChannels, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    // AudioDeviceManager mixes outputs from every callback; this input-only callback must add silence.
    for (int channel = 0; outputChannelData != nullptr && channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            std::fill_n (outputChannelData[channel], numSamples, 0.0f);

    recordingCallbacksInFlight.fetch_add (1, std::memory_order_seq_cst);
    std::array<float, 2> inputPeaks {};
    for (int channel = 0; inputChannelData != nullptr && channel < std::min (2, numInputChannels);
         ++channel)
    {
        const auto* samples = inputChannelData[channel];
        if (samples == nullptr)
            continue;
        for (int sample = 0; sample < numSamples; ++sample)
            inputPeaks[static_cast<std::size_t> (channel)] = std::max (
                inputPeaks[static_cast<std::size_t> (channel)], std::abs (samples[sample]));
    }
    if (numInputChannels == 1)
        inputPeaks[1] = inputPeaks[0];
    for (std::size_t channel = 0; channel < inputPeaks.size(); ++channel)
        liveAudioInputPeaks[channel].store (inputPeaks[channel], std::memory_order_relaxed);

    auto* session = activeRecordingSession.load (std::memory_order_seq_cst);
    if (session != nullptr && session->kind == RecordingSession::Kind::Audio && session->writer != nullptr)
    {
        const float* selectedChannels[2] {};
        int selected = 0;
        for (int channel = 0; inputChannelData != nullptr
                            && channel < numInputChannels && selected < session->audioChannels; ++channel)
            if (inputChannelData[channel] != nullptr)
                selectedChannels[selected++] = inputChannelData[channel];

        if (selected < session->audioChannels)
            session->inputLost.store (true, std::memory_order_relaxed);
        else if (! session->writer->write (selectedChannels, numSamples))
            session->overflow.store (true, std::memory_order_relaxed);
        else
            session->samplesWritten.fetch_add (numSamples, std::memory_order_relaxed);
    }
    recordingCallbacksInFlight.fetch_sub (1, std::memory_order_seq_cst);
}

void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice*)
{
}

void MainComponent::audioDeviceStopped()
{
    const auto stoppedGeneration = activeRecordingGeneration.load (std::memory_order_acquire);
    activeRecordingSession.store (nullptr, std::memory_order_seq_cst);
    if (stoppedGeneration == 0)
        return;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    juce::MessageManager::callAsync ([this, token, stoppedGeneration]
    {
        const auto alive = token.lock();
        if (alive != nullptr && alive->load() && recordingSession != nullptr
            && recordingSession->generation == stoppedGeneration
            && recordingSession->kind == RecordingSession::Kind::Audio)
        {
            setProjectStatus ("Audio device stopped; finalizing the recording...");
            finishRecording();
        }
    });
}

void MainComponent::captureRecordedMidiEvent (int pitch, int channel, int velocity,
                                              double beat, bool noteOn)
{
    recordingCallbacksInFlight.fetch_add (1, std::memory_order_seq_cst);
    if (auto* session = activeRecordingSession.load (std::memory_order_seq_cst);
        session != nullptr && session->kind == RecordingSession::Kind::Midi)
    {
        const auto index = session->midiEventCount.fetch_add (1, std::memory_order_relaxed);
        if (index < RecordingSession::midiEventCapacity)
            session->midiEvents[index] = { pitch, channel, velocity, beat, noteOn };
        else
            session->overflow.store (true, std::memory_order_relaxed);
    }
    recordingCallbacksInFlight.fetch_sub (1, std::memory_order_seq_cst);
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    MidiMappingMessageType messageType;
    int number = 0, value = 0;
    bool noteOn = false;
    if (message.isController())
    {
        messageType = MidiMappingMessageType::ControlChange;
        number = message.getControllerNumber();
        value = message.getControllerValue();
    }
    else if (message.isNoteOn() || message.isNoteOff())
    {
        messageType = MidiMappingMessageType::Note;
        number = message.getNoteNumber();
        value = message.getVelocity();
        noteOn = message.isNoteOn();
    }
    else
    {
        return;
    }

    if (message.isNoteOn() || message.isNoteOff())
        captureRecordedMidiEvent (message.getNoteNumber(), message.getChannel(),
                                  message.getVelocity(), engine.graph().positionBeats(),
                                  message.isNoteOn());

    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    if (! juce::MessageManager::callAsync ([this, token, channel = message.getChannel(),
                                            messageType, number, value, noteOn]
    {
        const auto alive = token.lock();
        if (alive != nullptr && alive->load())
            handleMidiMessageOnMessageThread (channel, messageType, number, value, noteOn);
    }))
        juce::Logger::writeToLog ("MainComponent: Could not queue a MIDI message.");
}

void MainComponent::handleMidiMessageOnMessageThread (int channel, MidiMappingMessageType messageType,
                                                      int number, int value, bool noteOn)
{
    if (pendingMidiLearn.has_value())
    {
        const auto pending = *pendingMidiLearn;
        const bool buttonTarget = pending.target == MidiMappingTarget::TrackMute
                               || pending.target == MidiMappingTarget::TrackSolo;
        if (messageType == MidiMappingMessageType::ControlChange
            || (messageType == MidiMappingMessageType::Note && buttonTarget && noteOn))
        {
            const bool masterTarget = pending.target == MidiMappingTarget::MasterVolume
                                   || pending.target == MidiMappingTarget::MasterPan;
            if ((masterTarget && pending.trackId != AudioEngine::masterTrackId)
                || (! masterTarget && project.track (pending.trackId) == nullptr))
            {
                pendingMidiLearn.reset();
                setProjectStatus ("MIDI Learn cancelled: track removed.");
                return;
            }

            project.beginEdit();
            std::erase_if (project.midiMappings, [&pending] (const MidiMapping& mapping)
            {
                return mapping.trackId == pending.trackId && mapping.target == pending.target;
            });
            project.midiMappings.push_back ({ pending.trackId, pending.target, messageType, channel, number });
            project.endEdit();
            pendingMidiLearn.reset();
            lastMidiButtonValues.erase ({ pending.trackId, static_cast<int> (pending.target) });
            project.sendChangeMessage();
            arrangement.refreshMidiMappingInfo();
            setProjectStatus ("MIDI mapped: channel " + juce::String (channel)
                              + (messageType == MidiMappingMessageType::ControlChange ? " CC " : " note ")
                              + juce::String (number) + ".");
            return;
        }
    }

    for (const auto& mapping : project.midiMappings)
        if (mapping.channel == channel && mapping.messageType == messageType
            && mapping.number == number)
            applyMidiMapping (mapping, value, noteOn);
}

void MainComponent::applyMidiMapping (const MidiMapping& mapping, int value, bool noteOn)
{
    auto* track = mapping.trackId == AudioEngine::masterTrackId
        ? project.masterTrack.get() : project.track (mapping.trackId);
    if (track == nullptr)
        return;

    const bool buttonTarget = mapping.target == MidiMappingTarget::TrackMute
                           || mapping.target == MidiMappingTarget::TrackSolo;
    if (buttonTarget)
    {
        bool pressed = false;
        if (mapping.messageType == MidiMappingMessageType::Note)
        {
            pressed = noteOn;
        }
        else
        {
            const auto key = std::make_pair (mapping.trackId, static_cast<int> (mapping.target));
            const auto previous = lastMidiButtonValues.find (key);
            pressed = value >= 64 && (previous == lastMidiButtonValues.end() || previous->second < 64);
            lastMidiButtonValues[key] = value;
        }

        if (! pressed)
            return;

        if (mapping.target == MidiMappingTarget::TrackSolo)
        {
            project.setTrackSolo (mapping.trackId, ! track->solo, true);
        }
        else
        {
            project.beginEdit();
            track->mute = ! track->mute;
            project.notifyMixer();
            project.endEdit();
        }
        return;
    }

    const double normalized = std::clamp (static_cast<double> (value) / 127.0, 0.0, 1.0);
    bool changed = false;
    switch (mapping.target)
    {
        case MidiMappingTarget::TrackVolume:
        {
            const double newValue = normalized * 1.5;
            changed = std::abs (track->volume - newValue) >= 0.0005;
            if (changed)
                track->volume = newValue;
            break;
        }
        case MidiMappingTarget::TrackPan:
        case MidiMappingTarget::MasterPan:
        {
            const double newValue = value <= 64
                ? (static_cast<double> (value) - 64.0) / 64.0
                : (static_cast<double> (value) - 64.0) / 63.0;
            changed = std::abs (track->pan - newValue) >= 0.005;
            if (changed)
                track->pan = newValue;
            break;
        }
        case MidiMappingTarget::MasterVolume:
        {
            const double decibels = -60.0 + normalized * 66.0;
            const double newValue = roll::decibelsToGain (static_cast<float> (decibels));
            changed = std::abs (track->volume - newValue) >= 0.0005;
            if (changed)
                track->volume = newValue;
            break;
        }
        case MidiMappingTarget::TrackMute:
        case MidiMappingTarget::TrackSolo:
            break;
    }

    if (changed)
        project.notifyMixer();
}

void MainComponent::handlePluginParameterGesture (
    int trackId, int deviceId, int parameterIndex, bool gestureIsStarting)
{
    engine.setPluginAutomationOverride (trackId, deviceId, parameterIndex, gestureIsStarting);
    if (! gestureIsStarting)
        return;

    const auto devices = engine.devices (trackId);
    for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
    {
        const auto& device = devices[deviceIndex];
        if (device.id != deviceId)
            continue;

        const auto parameter = std::find_if (device.automatableParameters.begin(),
                                              device.automatableParameters.end(),
            [parameterIndex] (const auto& candidate) { return candidate.index == parameterIndex; });
        if (parameter == device.automatableParameters.end())
            return;

        roll::AutomationTarget target;
        target.type = roll::AutomationParameterType::PluginParameter;
        target.pluginIdentifier = device.pluginIdentifier.toStdString();
        target.parameterId = parameter->id.toStdString();
        target.parameterName = parameter->name.toStdString();
        target.parameterIndex = parameter->index;
        target.deviceIndex = static_cast<int> (deviceIndex);
        target.currentValue = std::clamp (static_cast<double> (parameter->value), 0.0, 1.0);
        arrangement.selectPluginAutomationParameter (trackId, target);
        return;
    }
}

void MainComponent::showProjectMenu()
{
    std::vector<AppModalDialog::Action> actions
    {
        { "New Project", 7, AppModalDialog::ButtonStyle::Secondary },
        { "Open Project", 1, AppModalDialog::ButtonStyle::Secondary }
    };
    if (currentProjectFile.existsAsFile())
        actions.push_back ({ "Save", 2, AppModalDialog::ButtonStyle::Primary });
    actions.push_back ({ "Save As", 3, AppModalDialog::ButtonStyle::Secondary });
    actions.push_back ({ "Export WAV", 5, AppModalDialog::ButtonStyle::Secondary });
    actions.push_back ({ "Preferences", 6, AppModalDialog::ButtonStyle::Secondary });
    actions.push_back ({ "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary });

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    AppModalDialog::show (*this, "Project", "Open, create or save a project.",
                          AppModalDialog::Icon::None, std::move (actions),
        [safeThis] (int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 1) safeThis->chooseProjectFile (true);
            else if (result == 2) safeThis->saveProject (safeThis->currentProjectFile);
            else if (result == 3) safeThis->chooseProjectFile (false);
            else if (result == 7) safeThis->confirmCreateNewProject();
            else if (result == 5) safeThis->chooseWavExportFile();
            else if (result == 6) safeThis->showOptionsWindow();
        });
}

void MainComponent::confirmCreateNewProject()
{
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    AppModalDialog::show (
        *this, "Create New Project",
        "This will replace the current session with an empty project. Save anything you want to keep first.",
        AppModalDialog::Icon::Question,
        { { "Create", 1, AppModalDialog::ButtonStyle::Primary },
          { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
        [safeThis] (int result)
        {
            if (safeThis != nullptr && result == 1)
                safeThis->createNewProject();
        });
}

void MainComponent::createNewProject()
{
    if (! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || projectFileOperationInProgress || audioRenderInProgress)
    {
        setProjectStatus ("Wait for the current operation to finish before creating a project.");
        return;
    }

    finishRecording (true);
    ProjectModel blankProject;
    const auto* midiTrack = blankProject.addTrack();
    blankProject.addTrack (roll::TrackType::Audio);
    if (midiTrack != nullptr)
        blankProject.selectTrack (midiTrack->id);

    ProjectFileData data;
    data.project = blankProject.createState();
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Creating new project", "global", 0.0);
    projectFileOperationInProgress = true;
    applyLoadedProject ({}, std::move (data), task, false);
}

void MainComponent::showOptionsWindow()
{
    if (optionsWindow == nullptr)
    {
        optionsWindow = std::make_unique<OptionsWindow> (
            engine.audioDeviceManager(), scanner, createFadesOnClipEdges,
            [this] (bool enabled)
            {
                createFadesOnClipEdges = enabled;
                saveCreateFadesOnClipEdges (enabled);
            },
            [this] (theme::AccentStyle style)
            {
                const auto oldAccent = theme::accent();
                const auto oldHover = theme::accentHover();
                theme::setAccentStyle (style);
                const auto newAccent = theme::accent();
                const auto newHover = theme::accentHover();

                if (auto* laf = dynamic_cast<juce::LookAndFeel_V4*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
                {
                    laf->setColourScheme ({ theme::background(), theme::panel(), theme::panel(),
                                            theme::border(), theme::text(), theme::control(),
                                            theme::text(), theme::accent(), theme::text() });
                    laf->setColour (juce::TextButton::buttonOnColourId, theme::accent());
                    laf->setColour (juce::Slider::thumbColourId, theme::accent());
                    laf->setColour (juce::Slider::rotarySliderFillColourId, theme::accent());
                }

                refreshAccentColours (*this, oldAccent, newAccent, oldHover, newHover);
                if (optionsWindow != nullptr)
                    refreshAccentColours (*optionsWindow, oldAccent, newAccent, oldHover, newHover);
                if (tooltipWindow != nullptr)
                    refreshAccentColours (*tooltipWindow, oldAccent, newAccent, oldHover, newHover);
                if (auto* window = dynamic_cast<juce::DocumentWindow*> (mainWindow))
                    window->setIcon (icons::appIcon());
            },
            theme::waveformStyle(),
            [this] (theme::WaveformStyle style)
            {
                theme::setWaveformStyle (style);
                arrangement.refreshWaveformAppearance();
            },
            [this]
            {
                return std::pair<float, float> {
                    liveAudioInputPeaks[0].load (std::memory_order_relaxed),
                    liveAudioInputPeaks[1].load (std::memory_order_relaxed)
                };
            });
    }

    optionsWindow->setVisible (true);
    optionsWindow->toFront (true);
}

void MainComponent::chooseWavExportFile()
{
    if (audioRenderInProgress || projectFileOperationInProgress)
    {
        setProjectStatus ("Wait for the current operation to finish before exporting.");
        return;
    }

    const auto suggestedFile = currentProjectFile.existsAsFile()
        ? currentProjectFile.withFileExtension (".wav")
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("MaXXXtudio.wav");
    audioExportChooser = std::make_unique<juce::FileChooser> (
        "Export Master as WAV", suggestedFile, "*.wav");
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    audioExportChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, token] (const juce::FileChooser& chooser)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            const auto result = chooser.getResult();
            audioExportChooser.reset();
            if (result == juce::File())
                return;

            showWavRenderOptions (result.withFileExtension (".wav"));
        });
}

void MainComponent::chooseMidiClipExportFile (int trackId, int clipId)
{
    if (midiExportChooser != nullptr)
        return;

    const auto* track = project.track (trackId);
    const auto* placement = track != nullptr && track->type == roll::TrackType::Midi
        ? track->find (clipId) : nullptr;
    if (placement == nullptr)
    {
        setProjectStatus ("The MIDI clip is no longer available.");
        return;
    }

    const auto suggestedName = juce::File::createLegalFileName (
        placement->name.empty() ? "MIDI Clip" : juce::String (placement->name));
    const auto suggestedFile = currentProjectFile.existsAsFile()
        ? currentProjectFile.getParentDirectory().getChildFile (suggestedName + ".mid")
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile (suggestedName + ".mid");
    const auto clip = placement->clip;
    const double bpm = engine.graph().getBpm();
    midiExportChooser = std::make_unique<juce::FileChooser> (
        "Export MIDI Clip", suggestedFile, "*.mid");

    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    midiExportChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, token, clip, bpm] (const juce::FileChooser& chooser)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            const auto result = chooser.getResult();
            midiExportChooser.reset();
            if (result == juce::File())
                return;

            const auto file = result.withFileExtension (".mid");
            juce::FileOutputStream output (file);
            if (! output.openedOk())
            {
                setProjectStatus ("Could not create the MIDI file.");
                return;
            }

            const auto writeResult = midi_clip_export::write (clip, bpm, output);
            output.flush();
            if (writeResult.failed() || output.getStatus().failed())
            {
                setProjectStatus ("Could not export MIDI: "
                    + (writeResult.failed() ? writeResult.getErrorMessage()
                                            : output.getStatus().getErrorMessage()));
                return;
            }

            setProjectStatus ("MIDI clip exported: " + file.getFileName());
        });
}

void MainComponent::showWavRenderOptions (const juce::File& file)
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    AppModalDialog::showInput (
        *this, "Export Master as WAV",
        "Tail after the end of the Arrangement (seconds):", "2.0", "Segundos",
        { { "Export", 1, AppModalDialog::ButtonStyle::Primary },
          { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
        [safeThis, file] (int result, juce::String tailText)
        {
            if (safeThis == nullptr || result != 1)
                return;

            tailText = tailText.trim().replaceCharacter (',', '.');
            bool hasDigit = false;
            for (int index = 0; index < tailText.length(); ++index)
            {
                if (juce::CharacterFunctions::isDigit (tailText[index]))
                {
                    hasDigit = true;
                    break;
                }
            }
            const double tailSeconds = tailText.getDoubleValue();
            if (! hasDigit || ! std::isfinite (tailSeconds) || tailSeconds < 0.0 || tailSeconds > 60.0)
            {
                safeThis->setProjectStatus ("The tail must be between 0 and 60 seconds.");
                return;
            }

            safeThis->startWavRender (file, tailSeconds);
        });
}

void MainComponent::startWavRender (const juce::File& file, double tailSeconds)
{
    if (audioRenderInProgress || projectFileOperationInProgress
        || ! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || scanningPlugins)
    {
        setProjectStatus ("Wait for current loads and operations to finish before exporting.");
        return;
    }

    double endBeat = 0.0;
    for (const auto& track : project.tracks)
    {
        if (track->type == roll::TrackType::Audio)
        {
            for (const auto& clip : track->audioClips)
                if (clip->enabled)
                    endBeat = std::max (endBeat, clip->end());
        }
        else
        {
            for (const auto& clip : track->clips)
                if (clip->enabled)
                    endBeat = std::max (endBeat, clip->end());
        }
    }

    const double bpm = engine.graph().getBpm();
    if (endBeat <= 0.0 || ! std::isfinite (bpm) || bpm <= 0.0)
    {
        setProjectStatus ("The project has no audio or clips to export.");
        return;
    }

    releaseComputerMidiNotes();
    juce::String error;
    if (! engine.beginOfflineRender (error))
    {
        setProjectStatus ("Could not start export: " + error);
        return;
    }

    audioRenderInProgress = true;
    setAudioRenderUiEnabled (false);
    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto task = std::make_shared<TaskScope> (
        taskRegistry, "Renderizando Master: " + file.getFileName(), "global", 0.0, true,
        [cancelled] { cancelled->store (true, std::memory_order_release); });
    const auto durationSeconds = endBeat * 60.0 / bpm + tailSeconds;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;

    projectFilePool.addJob ([this, token, task, cancelled, file, durationSeconds]
    {
        const auto result = engine.renderMasterToWav (
            file, durationSeconds,
            [task] (double progress) { task->setProgress (progress); },
            [cancelled] { return cancelled->load (std::memory_order_acquire); });

        juce::MessageManager::callAsync ([this, token, task, cancelled, file, result]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            engine.finishOfflineRender();
            audioRenderInProgress = false;
            setAudioRenderUiEnabled (true);
            task->end();

            if (cancelled->load (std::memory_order_acquire))
                setProjectStatus ("Audio export cancelled.");
            else if (result.failed())
                setProjectStatus ("WAV export failed: " + result.getErrorMessage());
            else
                setProjectStatus ("Master exported: " + file.getFullPathName());
        });
    });
}

void MainComponent::setAudioRenderUiEnabled (bool enabled)
{
    transport.setEnabled (enabled);
    browser.setEnabled (enabled);
    audioBrowser.setEnabled (enabled);
    arrangement.setEnabled (enabled);
    editor.setEnabled (enabled);
    audioClipEditor.setEnabled (enabled);
    deviceView.setEnabled (enabled);
    clipTab.setEnabled (enabled);
    deviceTab.setEnabled (enabled);
    for (auto* control : clipControls)
        control->setEnabled (enabled);
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->setEnabled (enabled);
    }
}

void MainComponent::openProjectFromFile (const juce::File& file)
{
    if (file.existsAsFile() && isProjectFile (file))
        loadProject (file);
}

void MainComponent::offerRecoveryBeforeOpening (std::function<void()> continueStartup)
{
    const auto directory = ProjectFile::recoveryDirectory();
    ProjectFile::clearIncompleteRecoveryFiles (directory);
    const auto files = ProjectFile::recoveryFiles (directory);
    if (files.empty())
    {
        if (continueStartup)
            continueStartup();
        return;
    }

    finishRecording();
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    AppModalDialog::show (
        *this, "Recovery Session Found",
        "MaXXXtudio found a recent automatic backup. Do you want to recover it?",
        AppModalDialog::Icon::Question,
        { { "Recuperar", 1, AppModalDialog::ButtonStyle::Primary },
          { "Descartar", 2, AppModalDialog::ButtonStyle::Secondary } },
        [safeThis, recoveryFile = files.front(), continueStartup = std::move (continueStartup)] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            if (result == 1)
            {
                safeThis->loadProject (recoveryFile, true);
                return;
            }

            if (result == 2)
                ProjectFile::clearRecoveryFiles (ProjectFile::recoveryDirectory());

            if (continueStartup)
                continueStartup();
        });
}

void MainComponent::requestClose (std::function<void()> onCloseApproved)
{
    if (recoverySaveInProgress)
    {
        setProjectStatus ("Wait for the recovery copy to finish before closing.");
        return;
    }

    if (recordingSession != nullptr)
    {
        finishRecording();
        transport.stop();
    }

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    AppModalDialog::show (
        *this, "Save Project?",
        "Do you want to save the project before closing?",
        AppModalDialog::Icon::Question,
        { { "Save", 1, AppModalDialog::ButtonStyle::Primary },
          { "Don't Save", 2, AppModalDialog::ButtonStyle::Secondary },
          { "Cancel", 3, AppModalDialog::ButtonStyle::Tertiary } },
        [safeThis, onCloseApproved = std::move (onCloseApproved)] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            if (result == 2)
            {
                ProjectFile::clearRecoveryFiles (ProjectFile::recoveryDirectory());
                if (onCloseApproved)
                    onCloseApproved();
                return;
            }

            if (result != 1)
                return;

            const auto saved = [onCloseApproved] (bool success)
            {
                if (success && onCloseApproved)
                    onCloseApproved();
            };
            if (safeThis->currentProjectFile.existsAsFile())
                safeThis->saveProject (safeThis->currentProjectFile, saved);
            else
                safeThis->chooseProjectFile (false, saved);
        });
}

void MainComponent::chooseProjectFile (bool open, std::function<void (bool)> onSaveComplete)
{
    const auto start = currentProjectFile.existsAsFile()
        ? currentProjectFile : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    projectFileChooser = std::make_unique<juce::FileChooser> (
        open ? "Open Project" : "Save Project",
        start, "*.std");
    const auto chooserFlags = open ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   : juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    projectFileChooser->launchAsync (chooserFlags, [this, open, token,
                                                     onSaveComplete = std::move (onSaveComplete)]
                                                   (const juce::FileChooser& chooser) mutable
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load()) return;
        const auto result = chooser.getResult();
        projectFileChooser.reset();
        if (! result.exists() && open)
            return;
        if (result == juce::File())
        {
            if (onSaveComplete)
                onSaveComplete (false);
            return;
        }
        if (open)
            loadProject (result);
        else
            saveProject (result, std::move (onSaveComplete));
    });
}

void MainComponent::saveProject (const juce::File& requestedFile, std::function<void (bool)> onComplete)
{
    if (! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || projectFileOperationInProgress || audioRenderInProgress)
    {
        setProjectStatus ("Wait for loading to finish before saving.");
        if (onComplete)
            onComplete (false);
        return;
    }

    auto file = requestedFile;
    if (file.getFileExtension().compareIgnoreCase (".std") != 0)
        file = file.withFileExtension (".std");

    projectFileOperationInProgress = true;
    auto task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Saving project: ") + file.getFileName(), "global");
    auto snapshot = std::make_shared<ProjectFileData>();
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    if (! juce::MessageManager::callAsync ([this, token, file, task, snapshot,
                                            onComplete = std::move (onComplete)] () mutable
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;

        snapshot->project = project.createState();
        snapshot->plugins = engine.getPluginStates();
        snapshot->bpm = engine.graph().getBpm();
        snapshot->transportPosition = engine.graph().positionBeats();
        snapshot->loopEnabled = engine.graph().isLoop();
        snapshot->metronomeEnabled = engine.graph().isMetronomeEnabled();
        snapshot->computerMidiKeyboardEnabled = computerMidiKeyboardEnabled;
        snapshot->metronomeGain = engine.graph().getMetronomeGain();

        projectFilePool.addJob ([this, token, file, task, snapshot,
                                 onComplete = std::move (onComplete)] () mutable
        {
            const auto result = ProjectFile::save (file, *snapshot, [task] (double progress)
            {
                task->setProgress (progress);
            });

            if (! juce::MessageManager::callAsync ([this, token, file, task, result,
                                                    onComplete = std::move (onComplete)] () mutable
            {
                task->end();
                const auto alive = token.lock();
                if (alive == nullptr || ! alive->load())
                    return;

                projectFileOperationInProgress = false;
                if (result.failed())
                {
                    setProjectStatus ("Failed to save project: " + result.getErrorMessage());
                    if (onComplete)
                        onComplete (false);
                    return;
                }

                currentProjectFile = file;
                ProjectFile::clearRecoveryFiles (ProjectFile::recoveryDirectory());
                setProjectStatus ("Project saved: " + file.getFileName());
                if (onComplete)
                    onComplete (true);
            }))
            {
                juce::Logger::writeToLog ("MainComponent: Could not publish the save result.");
            }
        });
    }))
    {
        task->end();
        projectFileOperationInProgress = false;
        juce::Logger::writeToLog ("MainComponent: Could not start saving the project.");
        setProjectStatus ("Failed to start saving the project.");
        if (onComplete)
            onComplete (false);
    }
}

void MainComponent::saveRecoverySnapshot()
{
    if (recoverySaveInProgress || ! taskRegistry.tasksForTarget ("track").empty()
        || restoringProjectPlugins || pendingAudioLoads > 0
        || projectFileOperationInProgress || audioRenderInProgress)
        return;

    recoverySaveInProgress = true;
    projectFileOperationInProgress = true;
    auto snapshot = std::make_shared<ProjectFileData>();
    snapshot->project = project.createState();
    snapshot->plugins = engine.getPluginStates();
    snapshot->bpm = engine.graph().getBpm();
    snapshot->transportPosition = engine.graph().positionBeats();
    snapshot->loopEnabled = engine.graph().isLoop();
    snapshot->metronomeEnabled = engine.graph().isMetronomeEnabled();
    snapshot->computerMidiKeyboardEnabled = computerMidiKeyboardEnabled;
    snapshot->metronomeGain = engine.graph().getMetronomeGain();

    const auto directory = ProjectFile::recoveryDirectory();
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    projectFilePool.addJob ([this, token, directory, snapshot]
    {
        const auto result = ProjectFile::saveRecovery (directory, *snapshot);
        if (! juce::MessageManager::callAsync ([this, token, result]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            recoverySaveInProgress = false;
            projectFileOperationInProgress = false;
            if (result.failed())
            {
                juce::Logger::writeToLog ("MainComponent: Failed to save recovery copy: "
                                          + result.getErrorMessage());
                setProjectStatus ("Failed to create recovery copy: " + result.getErrorMessage());
            }
        }))
        {
            juce::Logger::writeToLog ("MainComponent: Could not publish the autosave result.");
        }
    });
}

void MainComponent::loadProject (const juce::File& file, bool recovering)
{
    if (! taskRegistry.tasksForTarget ("track").empty() || restoringProjectPlugins
        || pendingAudioLoads > 0 || projectFileOperationInProgress || audioRenderInProgress)
    {
        setProjectStatus ("Wait for loading to finish before opening another project.");
        return;
    }

    projectFileOperationInProgress = true;
    auto task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Opening project: ") + file.getFileName(), "global", -1.0);
    auto data = std::make_shared<ProjectFileData>();
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    projectFilePool.addJob ([this, token, file, task, data, recovering]
    {
        const auto result = ProjectFile::load (file, *data, [task] (double progress)
        {
            task->setProgress (progress < 0.0 ? -1.0 : progress * 0.25);
        });

        if (! juce::MessageManager::callAsync ([this, token, file, task, data, result, recovering]
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load())
                return;

            if (result.failed())
            {
                task->end();
                projectFileOperationInProgress = false;
                setProjectStatus ("Failed to open project: " + result.getErrorMessage());
                return;
            }

            applyLoadedProject (file, std::move (*data), task, recovering);
        }))
        {
            juce::Logger::writeToLog ("MainComponent: Could not publish the project open result.");
        }
    });
}

void MainComponent::applyLoadedProject (const juce::File& file, ProjectFileData data,
                                       const std::shared_ptr<TaskScope>& task, bool recovering)
{
    finishRecording (true);
    pendingMidiLearn.reset();
    lastMidiButtonValues.clear();
    projectLoadTask = task;
    projectLoadWorkDone = 0;
    projectLoadWorkTotal = data.plugins.size();
    std::set<std::string> audioPaths;
    for (const auto& track : data.project.tracks)
    {
        for (const auto& clip : track.audioClips)
            audioPaths.insert (clip->filePath);
        if (track.frozenAudioClip != nullptr)
            audioPaths.insert (track.frozenAudioClip->filePath);
    }
    projectLoadWorkTotal += audioPaths.size();
    task->setProgress (0.25);

    transport.stop();
    engine.stopAudioPreview();
    ++projectLoadToken;
    ++projectGeneration;
    pendingProjectPlugins.clear();
    loadedProjectPlugins.clear();
    nextProjectPlugin = 0;
    missingProjectPlugins = 0;
    waitingForProjectPluginScan = false;
    restoringProjectPlugins = ! data.plugins.empty();

    closeWindowsOfTrack (AudioEngine::masterTrackId);
    for (const auto& device : engine.devices (AudioEngine::masterTrackId))
        engine.removeDevice (AudioEngine::masterTrackId, device.id);

    project.replaceState (std::move (data.project));
    audioAssets.clear();
    for (const auto& track : project.tracks)
        if (track->frozen && project.onTrackFreezeChanged)
            project.onTrackFreezeChanged (track->id);
        else if (track->type == roll::TrackType::Audio && project.onAudioClipsChanged)
            project.onAudioClipsChanged (track->id);

    transport.setBpm (data.bpm);
    transport.setMetronomeState (data.metronomeEnabled, data.metronomeGain);
    transport.setLoopEnabled (data.loopEnabled);
    setComputerMidiKeyboardEnabled (data.computerMidiKeyboardEnabled);
    transportPosition = data.transportPosition;
    pausedPosition = data.transportPosition;
    transportPaused = false;
    engine.graph().seek (transportPosition);
    arrangement.setPlayhead (transportPosition);

    currentProjectFile = recovering ? juce::File() : file;
    if (! recovering)
        ProjectFile::clearRecoveryFiles (ProjectFile::recoveryDirectory());
    pendingProjectPlugins = std::move (data.plugins);
    loadProjectAudio (data);
    setProjectStatus (recovering ? "Session recovered. Use Save As to save the project."
        : file == juce::File() ? "New project created."
                               : "Project opened: " + file.getFileName());
    if (scanningPlugins && ! pendingProjectPlugins.empty())
        waitingForProjectPluginScan = true;
    else
        restoreNextProjectPlugin();

    advanceProjectLoadTask();
}

void MainComponent::advanceProjectLoadTask()
{
    if (projectLoadTask == nullptr)
        return;

    const auto completed = std::min (projectLoadWorkDone, projectLoadWorkTotal);
    const auto fraction = projectLoadWorkTotal > 0
        ? static_cast<double> (completed) / static_cast<double> (projectLoadWorkTotal) : 1.0;
    projectLoadTask->setProgress (0.25 + 0.75 * fraction);

    if (projectLoadWorkDone >= projectLoadWorkTotal && pendingAudioLoads == 0
        && ! restoringProjectPlugins && ! waitingForProjectPluginScan)
        finishProjectLoadTask();
}

void MainComponent::finishProjectLoadTask()
{
    if (projectLoadTask != nullptr)
    {
        projectLoadTask->setProgress (1.0);
        projectLoadTask->end();
        projectLoadTask.reset();
    }
    projectFileOperationInProgress = false;
    projectLoadWorkTotal = projectLoadWorkDone = 0;
}

void MainComponent::restoreNextProjectPlugin()
{
    if (waitingForProjectPluginScan)
        return;

    const int token = projectLoadToken;
    while (nextProjectPlugin < pendingProjectPlugins.size())
    {
        auto plugin = pendingProjectPlugins[nextProjectPlugin++];
        if (AudioEngine::isNativeEffectIdentifier (plugin.identifier))
        {
            const int deviceId = engine.addNativeEffect (plugin.trackId, plugin.identifier);
            if (deviceId <= 0)
                ++missingProjectPlugins;
            else
            {
                if (! engine.restorePluginState (plugin.trackId, deviceId, plugin.state))
                    ++missingProjectPlugins;
                engine.setDeviceBypassed (plugin.trackId, deviceId, plugin.bypassed);
                loadedProjectPlugins.emplace_back (plugin, deviceId);
                refreshDeviceView();
            }
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
            continue;
        }
        if (plugin.identifier == AudioEngine::autoPanPluginIdentifier)
        {
            const int deviceId = engine.addUtility (plugin.trackId);
            if (deviceId <= 0)
                ++missingProjectPlugins;
            else
            {
                if (! engine.restorePluginState (plugin.trackId, deviceId, plugin.state))
                    ++missingProjectPlugins;
                engine.setDeviceBypassed (plugin.trackId, deviceId, plugin.bypassed);
                loadedProjectPlugins.emplace_back (plugin, deviceId);
                refreshDeviceView();
            }
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
            continue;
        }
        if (plugin.identifier == AudioEngine::drumRackPluginIdentifier)
        {
            const int deviceId = engine.addDrumRack (plugin.trackId);
            if (deviceId <= 0)
            {
                ++missingProjectPlugins;
                setProjectStatus ("Could not load the Drum Master.");
            }
            else
            {
                if (! engine.restorePluginState (plugin.trackId, deviceId, plugin.state))
                    ++missingProjectPlugins;
                engine.setDeviceBypassed (plugin.trackId, deviceId, plugin.bypassed);
                loadedProjectPlugins.emplace_back (plugin, deviceId);
                refreshDeviceView();
            }
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
            continue;
        }

        const auto description = scanner.list().getTypeForIdentifierString (plugin.identifier);
        if (description == nullptr)
        {
            ++missingProjectPlugins;
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
            continue;
        }

        engine.addDevice (plugin.trackId, *description,
            [this, token, plugin] (const juce::String& error, int deviceId)
            {
                if (token != projectLoadToken)
                {
                    if (deviceId > 0)
                        engine.removeDevice (plugin.trackId, deviceId);
                    return;
                }

                if (error.isNotEmpty() || deviceId <= 0)
                {
                    ++missingProjectPlugins;
                    setProjectStatus ("Could not load " + plugin.identifier + ": " + error);
                    ++projectLoadWorkDone;
                    advanceProjectLoadTask();
                    restoreNextProjectPlugin();
                    return;
                }

                if (! engine.restorePluginState (plugin.trackId, deviceId, plugin.state))
                    ++missingProjectPlugins;
                engine.setDeviceBypassed (plugin.trackId, deviceId, plugin.bypassed);
                loadedProjectPlugins.emplace_back (plugin, deviceId);
                refreshDeviceView();
                ++projectLoadWorkDone;
                advanceProjectLoadTask();
                restoreNextProjectPlugin();
            }, plugin.order);
        return;
    }

    std::map<std::pair<int, int>, std::vector<int>> groups;
    for (const auto& [plugin, deviceId] : loadedProjectPlugins)
        if (plugin.groupId > 0)
            groups[{ plugin.trackId, plugin.groupId }].push_back (deviceId);
    for (const auto& [key, ids] : groups)
        if (ids.size() > 1)
            engine.groupDevices (key.first, ids);

    loadedProjectPlugins.clear();
    restoringProjectPlugins = false;
    refreshDeviceView();
    if (missingProjectPlugins > 0)
        setProjectStatus ("Project opened; " + juce::String (missingProjectPlugins)
                          + " plugin(s) not found or failed to load.");
    advanceProjectLoadTask();
}

void MainComponent::loadProjectAudio (const ProjectFileData& data)
{
    std::set<std::string> paths;
    for (const auto& track : project.tracks)
    {
        for (const auto& clip : track->audioClips)
            paths.insert (clip->filePath);
        if (track->frozenAudioClip != nullptr)
            paths.insert (track->frozenAudioClip->filePath);
    }

    if (paths.empty())
    {
        if (! data.missingAudioFiles.empty())
            setProjectStatus ("Project opened with missing audio files.");
        return;
    }

    const int generation = projectGeneration;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    for (const auto& path : paths)
    {
        const juce::File file (juce::String::fromUTF8 (path.c_str()));
        ++pendingAudioLoads;
        updateActionIndicator();
        engine.loadAudioFileAsync (file, [this, token, generation, path]
            (const juce::String& error, std::shared_ptr<const AudioFileData> data)
        {
            const auto alive = token.lock();
            if (alive == nullptr || ! alive->load()) return;

            pendingAudioLoads = std::max (0, pendingAudioLoads - 1);
            updateActionIndicator();
            if (generation != projectGeneration)
                return;
            if (error.isNotEmpty() || data == nullptr)
            {
                setProjectStatus ("Missing or invalid audio: " + juce::File (
                    juce::String::fromUTF8 (path.c_str())).getFileName());
                ++projectLoadWorkDone;
                advanceProjectLoadTask();
                return;
            }

            audioAssets[path] = data;
            for (const auto& track : project.tracks)
            {
                bool changed = false;
                for (auto& clip : track->audioClips)
                    if (clip->filePath == path)
                    {
                        clip->waveform = data->waveform;
                        clip->durationSeconds = data->durationSeconds;
                        changed = true;
                    }
                if (track->frozenAudioClip != nullptr && track->frozenAudioClip->filePath == path)
                {
                    track->frozenAudioClip->waveform = data->waveform;
                    track->frozenAudioClip->durationSeconds = data->durationSeconds;
                    changed = true;
                }
                if (changed)
                {
                    if (track->frozen && project.onTrackFreezeChanged)
                        project.onTrackFreezeChanged (track->id);
                    else if (project.onAudioClipsChanged)
                        project.onAudioClipsChanged (track->id);
                }
            }
            refreshAudioClipEditor();
            project.sendChangeMessage();
            ++projectLoadWorkDone;
            advanceProjectLoadTask();
        }, true);
    }
}

void MainComponent::setProjectStatus (const juce::String& message)
{
    browser.setStatus (message);
    audioBrowser.setStatus (message);
}

void MainComponent::onSelectionChanged()
{
    auto* p = project.selectedClip();
    const auto* selectedTrack = project.selectedTrack();
    stopGroovePreview();
    const auto* audio = selectedTrack != nullptr
        ? selectedTrack->findAudioClip (project.selectedAudioClipId) : nullptr;
    roll::Clip* target = p != nullptr ? &p->clip : nullptr;
    const bool changed = (target != (model.hasClip() ? &model.clip() : nullptr));

    model.clipStartBeats = p != nullptr ? p->start : 0.0;
    if (changed)
    {
        model.setClip (target);
        if (target != nullptr)
            editor.fitClipToView();
    }

    if (p != nullptr)
        lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
    lengthBox.setEnabled (p != nullptr);

    if (project.selectedAudioClipId < 0)
        audioClipEditorOpen = false;
    if (audio != nullptr)
    {
        transportPosition = audio->start;
        engine.graph().seek (transportPosition);
        arrangement.setPlayhead (transportPosition);
    }
    refreshAudioClipEditor();
    if (grooveBrowserWindow != nullptr)
    {
        const bool canUseGroove = p != nullptr && selectedTrack != nullptr
                               && selectedTrack->type == roll::TrackType::Midi;
        grooveBrowserWindow->setTargetAvailable (canUseGroove);
        grooveBrowserWindow->setAppliedGrooveName (
            canUseGroove && p->clip.groove()
                ? juce::String (p->clip.groove()->name) : juce::String());
    }
    if (! showingMixer)
    {
        showDetail (showingDevices);
        refreshDeviceView();
    }
}

void MainComponent::openAudioClipEditor (int trackId, int clipId)
{
    project.selectAudioClip (trackId, clipId);
    audioClipEditorOpen = true;
    refreshAudioClipEditor();
    showDetail (false);
    resized();
}

void MainComponent::openGrooveBrowser()
{
    if (grooveBrowserWindow == nullptr)
    {
        grooveBrowserWindow = std::make_unique<GrooveBrowserWindow>();
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        grooveBrowserWindow->onTransportKeyPress = [this, token] (const juce::KeyPress& key)
        {
            const auto alive = token.lock();
            return alive != nullptr && alive->load() && handleTransportKeyPress (key);
        };
        grooveBrowserWindow->onPreview = [this] (const roll::GrooveTemplate& groove)
        {
            previewGroove (groove);
        };
        grooveBrowserWindow->onStopPreview = [this] { stopGroovePreview(); };
        grooveBrowserWindow->onApply = [this] (const roll::GrooveTemplate& groove)
        {
            return applyGrooveToSelectedClip (groove);
        };
        grooveBrowserWindow->onClear = [this] { return removeGrooveFromSelectedClip(); };
    }

    const auto* track = project.selectedTrack();
    const auto* clip = project.selectedClip();
    const bool canUseGroove = clip != nullptr && track != nullptr && track->type == roll::TrackType::Midi;
    grooveBrowserWindow->setTargetAvailable (canUseGroove);
    grooveBrowserWindow->setAppliedGrooveName (
        canUseGroove && clip->clip.groove()
            ? juce::String (clip->clip.groove()->name) : juce::String());
    grooveBrowserWindow->centreAroundComponent (this, 620, 480);
    grooveBrowserWindow->setVisible (true);
    grooveBrowserWindow->toFront (true);
    grooveBrowserWindow->setAutoPreviewEnabled (false);
}

void MainComponent::previewGroove (const roll::GrooveTemplate& groove)
{
    stopGroovePreview();
    if (engine.graph().isPlaying())
    {
        if (grooveBrowserWindow != nullptr)
            grooveBrowserWindow->setStatus ("Stop playback before previewing a groove.");
        return;
    }
    if (! groove.isValid())
    {
        if (grooveBrowserWindow != nullptr)
            grooveBrowserWindow->setStatus ("The selected groove is invalid.");
        return;
    }

    const double bpm = engine.graph().getBpm();
    auto preview = std::isfinite (bpm) && bpm > 0.0
        ? createGroovePreviewAudio (groove, bpm) : nullptr;
    if (preview == nullptr)
    {
        if (grooveBrowserWindow != nullptr)
            grooveBrowserWindow->setStatus ("The project tempo is outside the preview range.");
        return;
    }

    engine.previewAudio (std::move (preview));
    groovePreviewAudioActive = true;
    if (grooveBrowserWindow != nullptr)
        grooveBrowserWindow->setStatus ("Previewing " + juce::String (groove.name) + ".");
}

void MainComponent::stopGroovePreview()
{
    if (! groovePreviewAudioActive)
        return;
    engine.stopAudioPreview();
    groovePreviewAudioActive = false;
}

juce::String MainComponent::applyGrooveToSelectedClip (const roll::GrooveTemplate& groove)
{
    auto* track = project.selectedTrack();
    auto* placement = project.selectedClip();
    if (! groove.isValid())
        return "The groove file contains invalid data.";
    if (track == nullptr || track->type != roll::TrackType::Midi || placement == nullptr)
        return "Select a MIDI clip to apply the groove.";

    stopGroovePreview();
    project.beginEdit();
    placement->clip.setGroove (groove);
    project.endEdit();
    project.notifyTrackNotes (track->id);
    project.sendChangeMessage();
    return "Groove applied without changing the original notes; use Undo to remove it.";
}

juce::String MainComponent::removeGrooveFromSelectedClip()
{
    auto* track = project.selectedTrack();
    auto* placement = project.selectedClip();
    if (track == nullptr || track->type != roll::TrackType::Midi || placement == nullptr)
        return "Select a MIDI clip to remove the groove.";
    if (! placement->clip.groove())
        return "The selected clip has no groove.";

    stopGroovePreview();
    project.beginEdit();
    placement->clip.setGroove (std::nullopt);
    project.endEdit();
    project.notifyTrackNotes (track->id);
    project.sendChangeMessage();
    return "Groove removed from the clip.";
}

void MainComponent::refreshAudioClipEditor()
{
    const auto* track = project.selectedTrack();
    const auto* clip = track != nullptr ? track->findAudioClip (project.selectedAudioClipId) : nullptr;
    std::shared_ptr<const AudioFileData> source;
    if (clip != nullptr)
        if (const auto asset = audioAssets.find (clip->filePath); asset != audioAssets.end())
            source = asset->second;
    audioClipEditor.setClip (clip, std::move (source), engine.graph().getBpm(), arrangement.gridBeats());
}

void MainComponent::refreshDeviceView()
{
    if (auto* t = project.selectedTrack())
        deviceView.setDevices (t->id, juce::String (t->name), engine.devices (t->id));
}

void MainComponent::applyScale()
{
    model.scale = roll::Scale (rootBox.getSelectedId() - 1, static_cast<roll::Mode> (modeBox.getSelectedId() - 1));
    model.rebuildRows();
    model.sendChangeMessage();
}

void MainComponent::applyLength()
{
    auto* t = project.selectedTrack();
    auto* p = project.selectedClip();
    if (t == nullptr || p == nullptr) return;

    const double newLen = lengthBox.getSelectedId() * 4.0;
    if (! t->isFree (p->start, newLen, p->id))
    {
        lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
        return;                                           // Drum rack is in another clip; do not allow this.
    }
    p->clip.setLength (newLen);
    project.growTo (p->end() + 64.0);
    project.notifyTrackNotes (t->id);
    project.sendChangeMessage();
    model.sendChangeMessage();
}

void MainComponent::navigatePianoRollLocator (bool forward)
{
    auto* placement = project.selectedClip();
    if (placement == nullptr)
        return;

    auto& clip = placement->clip;
    const auto& markers = clip.pianoRollLocators();
    if (markers.empty())
        return;

    const auto seekTo = [this, &clip] (double beat, int selectedId)
    {
        clip.selectPianoRollLocator (selectedId);
        project.sendChangeMessage();
        if (editor.onSeek)
            editor.onSeek (beat);
    };
    auto it = std::find_if (markers.begin(), markers.end(),
        [&clip] (const auto& marker) { return marker.id == clip.selectedPianoRollLocatorId(); });
    if (it != markers.end())
    {
        if (forward)
        {
            if (++it == markers.end())
            {
                seekTo (clip.length(), -1);
                return;
            }
        }
        else
        {
            if (it == markers.begin())
            {
                seekTo (0.0, -1);
                return;
            }
            --it;
        }
    }
    else
    {
        const double currentBeat = std::clamp (transportPosition - model.clipStartBeats, 0.0, clip.length());
        if (forward)
        {
            it = std::upper_bound (markers.begin(), markers.end(), currentBeat,
                [] (double beat, const auto& marker) { return beat < marker.beat; });
            if (it == markers.end())
            {
                seekTo (clip.length(), -1);
                return;
            }
        }
        else
        {
            it = std::lower_bound (markers.begin(), markers.end(), currentBeat,
                [] (const auto& marker, double beat) { return marker.beat < beat; });
            if (it == markers.begin())
            {
                seekTo (0.0, -1);
                return;
            }
            --it;
        }
    }

    seekTo (it->beat, it->id);
}

void MainComponent::addPianoRollLocatorAtPlayhead()
{
    auto* placement = project.selectedClip();
    if (placement == nullptr)
        return;

    auto& clip = placement->clip;
    const double localBeat = std::clamp (transportPosition - model.clipStartBeats, 0.0, clip.length());
    const double snappedBeat = std::clamp (model.snapBeats (localBeat), 0.0, clip.length());
    const bool alreadyExists = std::any_of (clip.pianoRollLocators().begin(), clip.pianoRollLocators().end(),
        [snappedBeat] (const auto& marker) { return std::abs (marker.beat - snappedBeat) < 1.0e-9; });
    if (! alreadyExists)
        project.beginEdit();
    clip.addPianoRollLocator (snappedBeat);
    if (! alreadyExists)
        project.endEdit();
    project.sendChangeMessage();
}

void MainComponent::deletePianoRollLocator (int id)
{
    auto* placement = project.selectedClip();
    if (placement == nullptr)
        return;

    auto& clip = placement->clip;
    if (id < 0)
    {
        id = clip.selectedPianoRollLocatorId();
        if (id < 0 && ! clip.pianoRollLocators().empty())
            id = std::max_element (clip.pianoRollLocators().begin(), clip.pianoRollLocators().end(),
                [] (const auto& a, const auto& b) { return a.id < b.id; })->id;
    }
    if (id < 0 || std::none_of (clip.pianoRollLocators().begin(), clip.pianoRollLocators().end(),
                                [id] (const auto& marker) { return marker.id == id; }))
        return;

    project.beginEdit();
    clip.removePianoRollLocator (id);
    project.endEdit();
    project.sendChangeMessage();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &model)
    {
        gridLabel.setText (model.gridLabel() + (model.drawMode ? "   (drawing)" : ""), juce::dontSendNotification);
        drawButton.setToggleState (model.drawMode, juce::dontSendNotification);
        snapButton.setToggleState (model.snap, juce::dontSendNotification);
        highlightButton.setToggleState (model.highlight, juce::dontSendNotification);
    }
    else if (src == &project)
    {
        refreshAudioClipEditor();
        std::vector<std::pair<int, double>> locators;
        const auto* placement = project.selectedClip();
        const auto* clip = placement != nullptr ? &placement->clip : nullptr;
        if (clip != nullptr)
            locators.reserve (clip->pianoRollLocators().size());
        if (clip != nullptr)
            for (const auto& marker : clip->pianoRollLocators())
                locators.emplace_back (marker.id, marker.beat);
        editor.setLocators (std::move (locators),
                            clip != nullptr ? clip->selectedPianoRollLocatorId() : -1);
        const bool anyLocators = clip != nullptr && ! clip->pianoRollLocators().empty();
        pianoLocatorPreviousButton.setEnabled (anyLocators);
        pianoLocatorNextButton.setEnabled (anyLocators);
        pianoLocatorDeleteButton.setEnabled (anyLocators);
        pianoLocatorAddButton.setEnabled (clip != nullptr);
        model.clipStartBeats = project.selectedClip() != nullptr ? project.selectedClip()->start : 0.0;
        if (auto* p = project.selectedClip())
            lengthBox.setSelectedId (static_cast<int> (std::lround (p->clip.length() / 4.0)), juce::dontSendNotification);
        model.sendChangeMessage();                         // The clip may have changed size.
    }
}

void MainComponent::timerCallback()
{
    updateComputerMidiKeyboard();
    updatePluginWindowNavigator();
    std::array<float, SequencerGraph::visualizationSampleCount> leftSamples {};
    std::array<float, SequencerGraph::visualizationSampleCount> rightSamples {};
    double sampleRate = 44100.0;
    if (engine.graph().copyVisualizationSamples (leftSamples, rightSamples, sampleRate))
    {
        spectrumAnalyzer->update (leftSamples, rightSamples, sampleRate);
        stereoImager->update (leftSamples, rightSamples);
    }
    if (engine.graph().isPlaying())
        loudnessHistory->update (engine.rms (AudioEngine::masterTrackId));
    mixerView->refresh();
    for (const auto& gesture : engine.consumePluginParameterGestures())
        handlePluginParameterGesture (gesture.trackId, gesture.deviceId,
                                      gesture.parameterIndex, gesture.gestureIsStarting);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now >= nextRecoverySaveTime)
    {
        nextRecoverySaveTime = now + 120000.0;
        saveRecoverySnapshot();
    }

    std::uint64_t lo, hi;
    engine.activeMask (project.selectedTrackId, lo, hi);
    for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
    {
        if (! computerMidiNoteActive[i] || computerMidiTrackIds[i] != project.selectedTrackId)
            continue;

        const int pitch = computerMidiKeyPitch[i];
        if (pitch < 64)
            lo |= std::uint64_t { 1 } << pitch;
        else
            hi |= std::uint64_t { 1 } << (pitch - 64);
    }
    if (lo != lastLo || hi != lastHi)                     // Light up keys as notes play.
    {
        lastLo = lo; lastHi = hi;
        model.activeLo = lo; model.activeHi = hi;
        editor.repaintKeys();
    }
    updateActionIndicator();
}

void MainComponent::setComputerMidiKeyboardEnabled (bool enabled)
{
    if (computerMidiKeyboardEnabled == enabled)
        return;

    computerMidiKeyboardEnabled = enabled;
    transport.setComputerMidiKeyboardEnabled (enabled);
    if (! enabled)
    {
        releaseComputerMidiNotes();
        hideComputerMidiStatus();
    }
    else
    {
        showComputerMidiStatus ("Computer MIDI Keyboard Active",
            "Vel " + juce::String (computerMidiVelocity) + " | Octave "
                + (computerMidiOctaveOffset > 0 ? "+" : "") + juce::String (computerMidiOctaveOffset),
            false);
    }
    startTimerHz (enabled ? 60 : 30);
}

void MainComponent::releaseComputerMidiNotes()
{
    for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
    {
        if (computerMidiNoteActive[i])
        {
            engine.previewNote (computerMidiTrackIds[i], computerMidiKeyPitch[i], false);
            captureRecordedMidiEvent (computerMidiKeyPitch[i], 1, 0,
                                      engine.graph().positionBeats(), false);
        }

        computerMidiKeyDown[i] = false;
        computerMidiNoteActive[i] = false;
    }
}

void MainComponent::updateComputerMidiKeyboard()
{
    // The timer also covers keys focused in other panels; keyStateChanged reduces latency.
    const auto modifiers = juce::ModifierKeys::getCurrentModifiers();
    const bool hasShortcutModifier = modifiers.isCtrlDown() || modifiers.isCommandDown()
                                  || modifiers.isAltDown() || modifiers.isShiftDown();
    const bool textFocused = isTextEditorFocused();
    const bool hostForeground = juce::Process::isForegroundProcess();
    const bool workerEditorForeground = RemotePluginProcessor::isPluginEditorForeground();
    const bool appForeground = hostForeground || workerEditorForeground;
    const bool textInputFocused = hostForeground && textFocused;

#if defined(_WIN32)
    const bool transportShortcutModifierDown = (GetAsyncKeyState (VK_CONTROL) & 0x8000) != 0
                                            || (GetAsyncKeyState (VK_MENU) & 0x8000) != 0;
    const bool workerSpaceDown = workerEditorForeground && ! audioRenderInProgress
                              && ! transportShortcutModifierDown
                              && (GetAsyncKeyState (VK_SPACE) & 0x8000) != 0;
    if (workerSpaceDown && ! remoteTransportSpaceKeyDown)
    {
        const auto keyModifiers = (GetAsyncKeyState (VK_SHIFT) & 0x8000) != 0
            ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier) : juce::ModifierKeys {};
        handleTransportKeyPress (juce::KeyPress (juce::KeyPress::spaceKey, keyModifiers, ' '));
    }
    remoteTransportSpaceKeyDown = workerSpaceDown;
#else
    remoteTransportSpaceKeyDown = false;
#endif

    const bool modeKeyDown = appForeground && ! audioRenderInProgress && ! textInputFocused && ! hasShortcutModifier
                          && juce::KeyPress::isKeyCurrentlyDown ('M');
    if (modeKeyDown && ! computerMidiModeKeyDown)
        setComputerMidiKeyboardEnabled (! computerMidiKeyboardEnabled);
    computerMidiModeKeyDown = modeKeyDown;

    const bool acceptKeys = computerMidiKeyboardEnabled && appForeground && ! audioRenderInProgress
                         && ! textInputFocused && ! hasShortcutModifier;
    // Z/X change the octave, and C/V adjust velocity in steps of 20.
    const std::array<int, 4> controlKeys { 'Z', 'X', 'C', 'V' };
    bool statusChanged = false;
    bool noteChanged = false;
    for (std::size_t i = 0; i < controlKeys.size(); ++i)
    {
        const bool down = acceptKeys && juce::KeyPress::isKeyCurrentlyDown (controlKeys[i]);
        if (down && ! computerMidiControlKeyDown[i])
        {
            const int oldOctave = computerMidiOctaveOffset;
            const int oldVelocity = computerMidiVelocity;
            if (i == 0) computerMidiOctaveOffset = std::max (-5, computerMidiOctaveOffset - 1);
            if (i == 1) computerMidiOctaveOffset = std::min (5, computerMidiOctaveOffset + 1);
            if (i == 2) computerMidiVelocity = std::max (20, computerMidiVelocity - 20);
            if (i == 3) computerMidiVelocity = std::min (120, computerMidiVelocity + 20);
            statusChanged |= oldOctave != computerMidiOctaveOffset || oldVelocity != computerMidiVelocity;
        }
        computerMidiControlKeyDown[i] = down;
    }

    const auto* selectedTrack = project.track (project.selectedTrackId);
    const int midiTrackId = selectedTrack != nullptr && selectedTrack->type == roll::TrackType::Midi
                          ? selectedTrack->id : -1;

    const auto& keyboardMap = DrumRackProcessor::computerMidiKeyboardMap;
    for (std::size_t i = 0; i < keyboardMap.size(); ++i)
    {
        const auto [keyCode, semitone] = keyboardMap[i];
        const bool down = acceptKeys && juce::KeyPress::isKeyCurrentlyDown (keyCode);

        if (down && ! computerMidiKeyDown[i] && midiTrackId >= 0)
        {
            const int pitch = DrumRackProcessor::firstMidiNote
                            + semitone + computerMidiOctaveOffset * 12;
            if (pitch >= 0 && pitch <= 127)
            {
                engine.previewNote (midiTrackId, pitch, true,
                                    static_cast<float> (computerMidiVelocity) / 127.0f);
                captureRecordedMidiEvent (pitch, 1, computerMidiVelocity,
                                          engine.graph().positionBeats(), true);
                computerMidiKeyPitch[i] = pitch;
                computerMidiTrackIds[i] = midiTrackId;
                computerMidiNoteActive[i] = true;
                lastComputerMidiNote = midiNoteName (pitch);
                statusChanged = noteChanged = true;
            }
        }
        else if (! down && computerMidiKeyDown[i] && computerMidiNoteActive[i])
        {
            engine.previewNote (computerMidiTrackIds[i], computerMidiKeyPitch[i], false);
            captureRecordedMidiEvent (computerMidiKeyPitch[i], 1, 0,
                                      engine.graph().positionBeats(), false);
            computerMidiNoteActive[i] = false;
            lastComputerMidiNote = midiNoteName (computerMidiKeyPitch[i]);
            statusChanged = noteChanged = true;
        }

        computerMidiKeyDown[i] = down;
    }

    if (statusChanged)
    {
        juce::StringArray activeNotes;
        for (std::size_t i = 0; i < computerMidiNoteActive.size(); ++i)
            if (computerMidiNoteActive[i])
                activeNotes.add (midiNoteName (computerMidiKeyPitch[i]));

        const auto primary = activeNotes.isEmpty()
            ? (noteChanged ? "Soltou " + lastComputerMidiNote : "Computer MIDI Keyboard")
            : activeNotes.joinIntoString (" + ");
        const auto details = "Vel " + juce::String (computerMidiVelocity) + " | Octave "
                           + (computerMidiOctaveOffset > 0 ? "+" : "")
                           + juce::String (computerMidiOctaveOffset);
        showComputerMidiStatus (primary, details, ! activeNotes.isEmpty());
    }
}

void MainComponent::showComputerMidiStatus (const juce::String& primary,
                                           const juce::String& secondary,
                                           bool noteHeld)
{
    computerMidiIndicator->showStatus (primary, secondary, noteHeld);
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->showComputerMidiStatus (primary, secondary, noteHeld);
    }
}

void MainComponent::hideComputerMidiStatus()
{
    computerMidiIndicator->hide();
    for (auto& [deviceId, window] : windows)
    {
        juce::ignoreUnused (deviceId);
        window->hideComputerMidiStatus();
    }
}

void MainComponent::startSimulatedTask()
{
    if (taskSimulationThread.joinable())
    {
        taskSimulationThread.request_stop();
        taskSimulationThread.join();
    }

    auto cancelled = std::make_shared<std::atomic<bool>> (false);
    auto* registry = &taskRegistry;
    taskSimulationThread = std::jthread ([registry, cancelled] (std::stop_token stopToken)
    {
        // The simulated task runs outside the UI and only publishes progress to the registry.
        TaskScope task (*registry, theme::ui ("Simulated Task"), "global", 0.0, true,
                        [cancelled] { cancelled->store (true, std::memory_order_relaxed); });
        constexpr int steps = 30;
        for (int step = 1; step <= steps; ++step)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            if (stopToken.stop_requested() || cancelled->load (std::memory_order_relaxed))
                break;
            task.setProgress (static_cast<double> (step) / steps);
        }
    });
}

// ------------------------------------------------------------ dispositivos

void MainComponent::duplicateSelectedTrack (int trackId)
{
    const auto* source = project.track (trackId);
    if (source == nullptr || source->type == roll::TrackType::Group)
        return;
    if (trackDuplicationInProgress || scanningPlugins || restoringProjectPlugins
        || projectFileOperationInProgress || audioRenderInProgress || pendingAudioLoads > 0)
    {
        setProjectStatus ("Wait for the current operation to finish before duplicating the track.");
        return;
    }

    bool pluginStateCaptured = false;
    juce::String stateCaptureError;
    auto pluginStates = engine.getPluginStates (
        { trackId }, {}, &pluginStateCaptured, &stateCaptureError);
    if (! pluginStateCaptured)
    {
        setProjectStatus ("Could not read the current plugin state; duplication was cancelled. "
                          + stateCaptureError);
        return;
    }

    project.beginExternalEdit ({ trackId }, pluginStates);
    const auto duplicateId = project.duplicateTrack (trackId);
    if (duplicateId < 0)
    {
        project.cancelEdit();
        return;
    }
    project.includeTrackInCurrentPluginHistoryScope (duplicateId);

    if (pluginStates.empty())
    {
        project.endEdit();
        setProjectStatus ("Track duplicated.");
        return;
    }

    struct PluginCopyOperation
    {
        int targetTrackId = -1;
        size_t completed = 0;
        int failures = 0;
        std::vector<PluginState> plugins;
        std::map<int, std::vector<int>> groups;
        std::shared_ptr<TaskScope> task;
    };

    auto operation = std::make_shared<PluginCopyOperation>();
    operation->targetTrackId = duplicateId;
    operation->plugins = std::move (pluginStates);
    operation->task = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Copying track devices"), "global", 0.0);
    trackDuplicationInProgress = true;
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;

    auto copyNextPlugin = [this, token, operation] (auto&& copyNextPlugin, size_t index) -> void
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;
        if (project.track (operation->targetTrackId) == nullptr)
        {
            trackDuplicationInProgress = false;
            operation->task->end();
            project.endEdit();
            setProjectStatus ("The duplicated track was removed before its devices could be copied.");
            return;
        }
        if (index >= operation->plugins.size())
        {
            for (const auto& [groupId, devices] : operation->groups)
            {
                juce::ignoreUnused (groupId);
                if (devices.size() >= 2 && ! engine.groupDevices (operation->targetTrackId, devices))
                    ++operation->failures;
            }
            trackDuplicationInProgress = false;
            operation->task->setProgress (1.0);
            operation->task->end();
            project.endEdit();
            refreshDeviceView();
            project.sendChangeMessage();
            setProjectStatus (operation->failures == 0
                ? "Track and devices duplicated."
                : "Track duplicated; " + juce::String (operation->failures)
                    + " device(s) could not be copied.");
            return;
        }

        const auto state = operation->plugins[index];
        if (AudioEngine::isNativeEffectIdentifier (state.identifier))
        {
            const int deviceId = engine.addNativeEffect (operation->targetTrackId, state.identifier);
            if (deviceId <= 0)
                ++operation->failures;
            else
            {
                if (! engine.restorePluginState (operation->targetTrackId, deviceId, state.state))
                    ++operation->failures;
                engine.setDeviceBypassed (operation->targetTrackId, deviceId, state.bypassed);
                if (state.groupId > 0)
                    operation->groups[state.groupId].push_back (deviceId);
            }
            ++operation->completed;
            operation->task->setProgress (static_cast<double> (operation->completed)
                                            / operation->plugins.size());
            copyNextPlugin (copyNextPlugin, index + 1);
            return;
        }
        if (state.identifier == AudioEngine::autoPanPluginIdentifier)
        {
            const int deviceId = engine.addUtility (operation->targetTrackId);
            if (deviceId <= 0)
                ++operation->failures;
            else
            {
                if (! engine.restorePluginState (operation->targetTrackId, deviceId, state.state))
                    ++operation->failures;
                engine.setDeviceBypassed (operation->targetTrackId, deviceId, state.bypassed);
                if (state.groupId > 0)
                    operation->groups[state.groupId].push_back (deviceId);
            }
            ++operation->completed;
            operation->task->setProgress (static_cast<double> (operation->completed)
                                            / operation->plugins.size());
            copyNextPlugin (copyNextPlugin, index + 1);
            return;
        }
        auto description = scanner.list().getTypeForIdentifierString (state.identifier);
        if (description == nullptr)
        {
            ++operation->failures;
            ++operation->completed;
            operation->task->setProgress (static_cast<double> (operation->completed)
                                            / operation->plugins.size());
            copyNextPlugin (copyNextPlugin, index + 1);
            return;
        }

        engine.addDevice (operation->targetTrackId, *description,
            [this, token, operation, state, index, copyNextPlugin] (
                const juce::String& error, int deviceId) mutable
            {
                const auto callbackAlive = token.lock();
                if (callbackAlive == nullptr || ! callbackAlive->load())
                    return;
                if (error.isNotEmpty())
                    ++operation->failures;
                else
                {
                    if (! engine.restorePluginState (operation->targetTrackId, deviceId, state.state))
                        ++operation->failures;
                    engine.setDeviceBypassed (operation->targetTrackId, deviceId, state.bypassed);
                    if (state.groupId > 0)
                        operation->groups[state.groupId].push_back (deviceId);
                }

                ++operation->completed;
                operation->task->setProgress (static_cast<double> (operation->completed)
                                                / operation->plugins.size());
                copyNextPlugin (copyNextPlugin, index + 1);
            }, state.order);
    };
    copyNextPlugin (copyNextPlugin, 0);
}

juce::String MainComponent::addNativeEffectToTrack (
    int trackId, const juce::String& identifier, int insertionIndex)
{
    const auto* track = project.track (trackId);
    if (track == nullptr)
        return "Track does not exist.";
    if (track->frozen)
        return theme::ui ("Unfreeze the track before editing devices.");

    const juce::String effectName = identifier == AudioEngine::compMasterPluginIdentifier ? "Comp Master"
        : identifier == AudioEngine::eqMasterPluginIdentifier ? "EQ Master" : "Volume Master";
    const auto existing = engine.devices (trackId);
    if (std::any_of (existing.begin(), existing.end(), [&identifier] (const DeviceInfo& device)
        {
            return device.pluginIdentifier == identifier;
        }))
        return effectName + " is already on this channel.";

    project.beginExternalEdit ({ trackId });
    const int deviceId = engine.addNativeEffect (trackId, identifier, insertionIndex);
    if (deviceId <= 0)
    {
        project.cancelEdit();
        return "Could not add " + effectName + " to this track.";
    }

    project.endEdit();
    project.selectTrack (trackId);
    showDetail (true);
    refreshDeviceView();
    project.sendChangeMessage();
    openDeviceEditor (trackId, deviceId);
    return effectName + " added to the selected track.";
}

void MainComponent::addNativeDrumRackToTrack (int trackId)
{
    const auto* track = project.track (trackId);
    if (track == nullptr || track->type != roll::TrackType::Midi)
    {
        browser.setStatus ("The Drum Master can only be added to a MIDI channel.");
        return;
    }
    if (track->frozen)
    {
        browser.setStatus (theme::ui ("Unfreeze the track before editing devices."));
        return;
    }

    const auto currentDevices = engine.devices (trackId);
    if (std::any_of (currentDevices.begin(), currentDevices.end(), [] (const DeviceInfo& device)
        {
            return device.pluginIdentifier == AudioEngine::drumRackPluginIdentifier;
        }))
    {
        const auto existing = std::find_if (currentDevices.begin(), currentDevices.end(),
            [] (const DeviceInfo& device)
            {
                return device.pluginIdentifier == AudioEngine::drumRackPluginIdentifier;
            });
        openDeviceEditor (trackId, existing->id);
        return;
    }

    project.beginExternalEdit ({ trackId });
    for (const auto& device : engine.devices (trackId))
        if (device.isInstrument)
        {
            windows.erase (device.id);
            closeWindowsOfRack (trackId, device.id);
        }
    const int deviceId = engine.addDrumRack (trackId);
    if (deviceId <= 0)
    {
        project.cancelEdit();
        browser.setStatus ("Could not add the Drum Master to this channel.");
        return;
    }

    project.endEdit();
    project.selectTrack (trackId);
    showDetail (true);
    refreshDeviceView();
    project.sendChangeMessage();
    openDeviceEditor (trackId, deviceId);
}

void MainComponent::loadDevice (const juce::PluginDescription& desc, int targetTrackId, int insertionIndex)
{
    auto* t = targetTrackId >= 0 ? project.track (targetTrackId) : project.selectedTrack();
    if (t == nullptr) return;
    const int trackId = t->id;
    if (t->frozen)
    {
        browser.setStatus (theme::ui ("Unfreeze the track before editing devices."));
        return;
    }
    if (! engine.canAcceptPlugin (trackId, desc))
    {
        browser.setStatus (trackId == 0 ? theme::ui ("The Master track only accepts effects.")
                                        : theme::ui ("Audio channels only accept effects."));
        return;
    }

    const juce::String pluginIdentifier = desc.createIdentifierString();
    const auto existingDevices = engine.devices (trackId);
    if (const auto existing = std::find_if (existingDevices.begin(), existingDevices.end(),
            [&pluginIdentifier] (const DeviceInfo& device)
            {
                return device.pluginIdentifier == pluginIdentifier;
            });
        existing != existingDevices.end())
    {
        browser.setStatus (theme::ui ("This plugin is already loaded on this track."));
        openDeviceEditor (trackId, existing->id);
        return;
    }

    if (desc.isInstrument)                                // The current instrument will be replaced; close its UI.
        for (const auto& d : existingDevices)
            if (d.isInstrument)
            {
                windows.erase (d.id);
                closeWindowsOfRack (trackId, d.id);
            }

    // Use the same slot index that AudioEngine will use when it inserts the plugin.
    int pluginSlot = 0;
    if (! desc.isInstrument)
    {
        const int firstEffect = ! existingDevices.empty() && existingDevices.front().isInstrument ? 1 : 0;
        pluginSlot = insertionIndex < 0
            ? static_cast<int> (existingDevices.size())
            : std::clamp (insertionIndex, firstEffect, static_cast<int> (existingDevices.size()));
    }

    const auto target = "track:" + juce::String (trackId) + ":plugin:" + juce::String (pluginSlot);
    // Keep the scope alive in the async callback so every success/error path ends the task.
    auto pluginTask = std::make_shared<TaskScope> (
        taskRegistry, theme::ui ("Loading ") + desc.name, target);
    browser.setStatus ("Loading " + desc.name + "...");
    const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
    const auto pluginName = desc.name;
    project.beginExternalEdit ({ trackId });
    engine.addDevice (trackId, desc,
        [this, token, pluginTask, trackId, pluginName] (
            const juce::String& error, int deviceId)
    {
        const auto alive = token.lock();
        if (alive == nullptr || ! alive->load())
            return;

        if (error.isNotEmpty())
        {
            project.cancelEdit();
            browser.setStatus ("Error: " + error);
            return;
        }
        if (deviceId <= 0)
        {
            project.cancelEdit();
            browser.setStatus ("The plugin was not added.");
            return;
        }
        project.endEdit();
        browser.setStatus (juce::String (scanner.list().getNumTypes()) + " plugins found");
        refreshDeviceView();
        project.sendChangeMessage();
        if (engine.graph().isPlaying())
            browser.setStatus ("Added " + pluginName
                               + ". Stop playback to open the plugin interface.");
        else
            openDeviceEditor (trackId, deviceId);
    }, insertionIndex);
}

void MainComponent::updateActionIndicator()
{
    if (actionIndicator == nullptr)
        return;

    if (pendingAudioLoads > 0)
    {
        const auto text = pendingAudioLoads == 1
            ? "Loading audio..."
            : "Loading " + juce::String (pendingAudioLoads) + " audio files...";
        actionIndicator->showMessage (text, icons::Icon::Waveform, true);
    }
    else if (pendingPreviewLoads > 0)
    {
        actionIndicator->showMessage ("Loading audio preview...", icons::Icon::Waveform, true);
    }
    else if (restoringProjectPlugins)
    {
        actionIndicator->showMessage ("Restoring project plugins...", icons::Icon::Mixer, true);
    }
    else
    {
        actionIndicator->hide();
    }
}

void MainComponent::openDeviceEditor (int trackId, int deviceId)
{
    std::vector<std::pair<int, juce::String>> sidechainSources { { -1, "None" } };
    if (trackId != 0 && engine.canUseCompMasterSidechainSource (trackId, 0))
        sidechainSources.emplace_back (0, "Master");
    for (const auto& track : project.tracks)
        if (engine.canUseCompMasterSidechainSource (trackId, track->id))
            sidechainSources.emplace_back (track->id, juce::String (track->name));
    engine.setCompMasterSidechainSources (trackId, deviceId, std::move (sidechainSources));

    const auto editorPosition = nextPluginWindowPosition();
    const auto isolatedResult = engine.showIsolatedDeviceEditor (
        trackId, deviceId, editorPosition.x, editorPosition.y);
    if (isolatedResult == AudioEngine::IsolatedEditorResult::opened)
    {
        updatePluginWindowNavigator();
        return;
    }
    if (isolatedResult == AudioEngine::IsolatedEditorResult::noEditor)
    {
        browser.setStatus ("This plugin has no graphical interface.");
        return;
    }
    if (isolatedResult == AudioEngine::IsolatedEditorResult::failed)
    {
        browser.setStatus ("This plugin's isolated process has stopped.");
        return;
    }

    auto it = windows.find (deviceId);
    if (it != windows.end())
    {
        it->second->setMinimised (false);
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }

    if (auto ed = engine.createDeviceEditor (trackId, deviceId))
    {
        if (auto* drumRackEditor = dynamic_cast<DrumRackEditor*> (ed.get()))
        {
            const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
            drumRackEditor->onTransportKeyPress = [this, token] (const juce::KeyPress& key)
            {
                const auto alive = token.lock();
                return alive != nullptr && alive->load() && handleTransportKeyPress (key);
            };
            const juce::Component::SafePointer<DrumRackEditor> safeEditor (drumRackEditor);
            drumRackEditor->onPadEffectRequested = [this, token, safeEditor, trackId, deviceId]
                (int pad, const juce::String& identifier)
            {
                const auto alive = token.lock();
                if (alive == nullptr || ! alive->load())
                    return;
                auto description = scanner.list().getTypeForIdentifierString (identifier);
                if (description == nullptr || description->isInstrument)
                {
                    AppModalDialog::show (
                        *this, "Pad Effect", "Drag an audio effect plugin onto the pad.",
                        AppModalDialog::Icon::Warning,
                        { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                    return;
                }

                const auto effectName = description->name;
                engine.addPadEffect (trackId, deviceId, pad, *description,
                    [this, token, safeEditor, effectName] (const juce::String& error)
                    {
                        const auto callbackAlive = token.lock();
                        if (callbackAlive == nullptr || ! callbackAlive->load())
                            return;
                        if (error.isNotEmpty())
                        {
                            AppModalDialog::show (
                                *this, "Pad Effect", error, AppModalDialog::Icon::Warning,
                                { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                            return;
                        }
                        if (safeEditor != nullptr)
                            safeEditor->refreshPadEffects (true);
                        browser.setStatus ("Effect " + effectName + " added to the pad.");
                    });
            };
            drumRackEditor->onPadEffectEditorRequested = [this, token, trackId, deviceId]
                (int pad, int effectIndex)
            {
                const auto alive = token.lock();
                if (alive == nullptr || ! alive->load())
                    return;
                const auto editorPosition = nextPluginWindowPosition();
                const auto isolatedResult = engine.showIsolatedPadEffectEditor (
                    trackId, deviceId, pad, effectIndex, editorPosition.x, editorPosition.y);
                if (isolatedResult == AudioEngine::IsolatedEditorResult::opened)
                {
                    updatePluginWindowNavigator();
                    return;
                }
                if (isolatedResult == AudioEngine::IsolatedEditorResult::noEditor)
                {
                    AppModalDialog::show (
                        *this, "Pad Effect", "This effect has no graphical interface.",
                        AppModalDialog::Icon::Question,
                        { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                    return;
                }
                if (isolatedResult == AudioEngine::IsolatedEditorResult::failed)
                {
                    AppModalDialog::show (
                        *this, "Pad Effect", "The isolated process for this effect has stopped.",
                        AppModalDialog::Icon::Error,
                        { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                    return;
                }

                const auto key = std::make_tuple (trackId, deviceId, pad, effectIndex);
                if (const auto existing = padEffectWindowIds.find (key);
                    existing != padEffectWindowIds.end())
                    if (const auto window = windows.find (existing->second); window != windows.end())
                    {
                        window->second->setVisible (true);
                        window->second->toFront (true);
                        return;
                    }

                auto editor = engine.createPadEffectEditor (trackId, deviceId, pad, effectIndex);
                if (editor == nullptr)
                {
                    AppModalDialog::show (
                        *this, "Pad Effect", "This effect has no graphical interface.",
                        AppModalDialog::Icon::Question,
                        { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                    return;
                }

                juce::String title = "Pad Effect " + juce::String (pad + 1);
                if (const auto* track = project.track (trackId))
                    title = juce::String (track->name) + " - " + title;
                const auto& displays = juce::Desktop::getInstance().getDisplays();
                const auto* display = displays.getDisplayForRect (getScreenBounds());
                if (display == nullptr)
                    display = displays.getPrimaryDisplay();
                const auto displayArea = display != nullptr ? display->userBounds.toNearestInt()
                                                            : juce::Rectangle<int> (0, 0, 1280, 720);
                std::vector<roll::Rect> occupiedWindows;
                for (const auto& bounds : pluginWindowBounds())
                    occupiedWindows.push_back ({ bounds.getX() + pluginWindowScrollOffset, bounds.getY(),
                                                 bounds.getWidth(), bounds.getHeight() });
                const int effectWindowId = nextPadEffectWindowId--;
                windows[effectWindowId] = std::make_unique<PluginWindow> (
                    title, std::move (editor), mainWindow, displayArea, pluginWindowScrollOffset,
                    nextPluginCascadeIndex++, occupiedWindows,
                    [this, token]
                    {
                        const auto callbackAlive = token.lock();
                        if (callbackAlive != nullptr && callbackAlive->load())
                            updatePluginWindowZOrder();
                    },
                    [this, token] (const juce::KeyPress& key)
                    {
                        const auto callbackAlive = token.lock();
                        return callbackAlive != nullptr && callbackAlive->load()
                            && handleTransportKeyPress (key);
                    });
                padEffectWindowIds[key] = effectWindowId;
                padEffectWindowOwners[effectWindowId] = { trackId, deviceId, pad };
                windows[effectWindowId]->toFront (true);
                updatePluginWindowZOrder();
            };
            drumRackEditor->onPadEffectsChanged = [this, token, trackId, deviceId] (int pad)
            {
                const auto alive = token.lock();
                if (alive != nullptr && alive->load())
                    closeWindowsOfPad (trackId, deviceId, pad);
            };
        }

        juce::String title;
        for (const auto& d : engine.devices (trackId)) if (d.id == deviceId) title = d.name;
        if (auto* t = project.track (trackId)) title += "  -  " + juce::String (t->name);

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getDisplayForRect (getScreenBounds());
        if (display == nullptr)
            display = displays.getPrimaryDisplay();

        const auto displayArea = display != nullptr ? display->userBounds.toNearestInt()
                                                    : juce::Rectangle<int> (0, 0, 1280, 720);
        std::vector<roll::Rect> occupiedWindows;
        for (const auto& bounds : pluginWindowBounds())
            occupiedWindows.push_back ({ bounds.getX() + pluginWindowScrollOffset, bounds.getY(),
                                         bounds.getWidth(), bounds.getHeight() });
        const std::weak_ptr<std::atomic<bool>> token = lifetimeToken;
        auto window = std::make_unique<PluginWindow> (
            title, std::move (ed), mainWindow, displayArea, pluginWindowScrollOffset,
            nextPluginCascadeIndex++, occupiedWindows,
            [this, token]
            {
                const auto alive = token.lock();
                if (alive != nullptr && alive->load())
                    updatePluginWindowZOrder();
            },
            [this, token] (const juce::KeyPress& key)
            {
                const auto alive = token.lock();
                return alive != nullptr && alive->load() && handleTransportKeyPress (key);
            },
            trackId, deviceId,
            [this, token] (int parameterTrackId, int parameterDeviceId, int parameterIndex,
                           bool gestureIsStarting)
            {
                const auto alive = token.lock();
                if (alive != nullptr && alive->load())
                    handlePluginParameterGesture (parameterTrackId, parameterDeviceId,
                                                  parameterIndex, gestureIsStarting);
            });
        windows[deviceId] = std::move (window);
        if (computerMidiIndicator->isVisible())
            windows[deviceId]->showComputerMidiStatus (
                computerMidiIndicator->getPrimaryText(),
                computerMidiIndicator->getSecondaryText(),
                computerMidiIndicator->isHeld());
        windows[deviceId]->toFront (true);
        updatePluginWindowZOrder();
    }
    else
    {
        juce::String pluginName;
        for (const auto& device : engine.devices (trackId))
            if (device.id == deviceId) { pluginName = device.name; break; }

        browser.setStatus ("Failed to open the interface for " + (pluginName.isNotEmpty() ? pluginName : "plugin") + ".");
    }
}

void MainComponent::closeWindowsOfTrack (int trackId)
{
    for (const auto& d : engine.devices (trackId)) windows.erase (d.id);
    for (auto it = padEffectWindowOwners.begin(); it != padEffectWindowOwners.end();)
    {
        if (std::get<0> (it->second) == trackId)
        {
            windows.erase (it->first);
            it = padEffectWindowOwners.erase (it);
        }
        else
            ++it;
    }
    for (auto it = padEffectWindowIds.begin(); it != padEffectWindowIds.end();)
        if (std::get<0> (it->first) == trackId)
            it = padEffectWindowIds.erase (it);
        else
            ++it;
}

void MainComponent::closeWindowsOfRack (int trackId, int rackDeviceId)
{
    for (auto it = padEffectWindowOwners.begin(); it != padEffectWindowOwners.end();)
    {
        if (std::get<0> (it->second) == trackId && std::get<1> (it->second) == rackDeviceId)
        {
            windows.erase (it->first);
            it = padEffectWindowOwners.erase (it);
        }
        else
            ++it;
    }
    for (auto it = padEffectWindowIds.begin(); it != padEffectWindowIds.end();)
        if (std::get<0> (it->first) == trackId && std::get<1> (it->first) == rackDeviceId)
            it = padEffectWindowIds.erase (it);
        else
            ++it;
}

void MainComponent::closeWindowsOfPad (int trackId, int rackDeviceId, int pad)
{
    for (auto it = padEffectWindowOwners.begin(); it != padEffectWindowOwners.end();)
    {
        if (std::get<0> (it->second) == trackId && std::get<1> (it->second) == rackDeviceId
            && std::get<2> (it->second) == pad)
        {
            windows.erase (it->first);
            it = padEffectWindowOwners.erase (it);
        }
        else
            ++it;
    }
    for (auto it = padEffectWindowIds.begin(); it != padEffectWindowIds.end();)
        if (std::get<0> (it->first) == trackId && std::get<1> (it->first) == rackDeviceId
            && std::get<2> (it->first) == pad)
            it = padEffectWindowIds.erase (it);
        else
            ++it;
}
