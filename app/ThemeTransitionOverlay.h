#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <vector>

class ThemeTransitionOverlay final : public juce::Component, private juce::Timer
{
public:
    ThemeTransitionOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
    }

    void start (juce::Component& parent)
    {
        buttonBounds.clear();
        collectButtonBounds (parent, {});
        startedAt = juce::Time::getMillisecondCounterHiRes();
        opacity = 0.0f;
        setVisible (true);
        toFront (false);
        startTimerHz (30);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (opacity <= 0.0f)
            return;

        constexpr double durationMs = 1850.0;
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startedAt;
        const float progress = static_cast<float> (
            std::clamp (elapsed / durationMs, 0.0, 1.0));
        const auto accent = theme::accent();

        juce::Graphics::ScopedSaveState saveState (g);
        g.setOpacity (opacity);
        for (const auto& buttonBoundsInParent : buttonBounds)
            drawButtonSweep (g, buttonBoundsInParent, progress, accent);

        const float textFadeIn = std::clamp (static_cast<float> (elapsed / 120.0), 0.0f, 1.0f);
        const float textFadeOut = std::clamp (
            static_cast<float> ((durationMs - elapsed) / 300.0), 0.0f, 1.0f);
        const float textOpacity = std::min (textFadeIn, textFadeOut);
        if (textOpacity <= 0.0f)
            return;

        g.setOpacity (opacity * textOpacity);
        const auto titleBounds = getLocalBounds().toFloat().reduced (24.0f)
            .withHeight (88.0f).withCentre (getLocalBounds().toFloat().getCentre());
        const auto title = theme::paletteName (theme::current()).toUpperCase()
            + "  /  " + theme::accentName (theme::accentStyle()).toUpperCase();
        g.setFont (juce::FontOptions ("Arial", 38.0f,
                                      juce::Font::bold | juce::Font::italic));
        for (int depth = 6; depth > 0; --depth)
        {
            g.setColour (accent.darker (0.07f * static_cast<float> (depth)));
            g.drawFittedText (title, titleBounds.translated (
                static_cast<float> (depth), static_cast<float> (depth)).toNearestInt(),
                juce::Justification::centred, 1);
        }
        g.setColour (accent);
        g.drawFittedText (title, titleBounds.toNearestInt(), juce::Justification::centred, 1);
    }

private:
    void collectButtonBounds (juce::Component& component, juce::Point<int> parentOffset)
    {
        if (&component != this && component.isShowing()
            && (dynamic_cast<juce::Button*> (&component) != nullptr
                || dynamic_cast<juce::ComboBox*> (&component) != nullptr))
        {
            const auto bounds = component.getLocalBounds().toFloat()
                .translated (static_cast<float> (parentOffset.x),
                             static_cast<float> (parentOffset.y));
            if (bounds.getWidth() >= 24.0f && bounds.getHeight() >= 18.0f)
                buttonBounds.push_back (bounds);
        }

        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i);
                child != nullptr && child->isVisible())
                collectButtonBounds (*child, parentOffset + child->getPosition());
    }

    static void drawButtonSweep (juce::Graphics& g, juce::Rectangle<float> bounds,
                                 float progress, juce::Colour accent)
    {
        bounds = bounds.reduced (1.0f);
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();
        if (width < 10.0f || height < 10.0f)
            return;

        const float perimeter = 2.0f * (width + height);
        const float segmentLength = std::min (42.0f, perimeter * 0.2f);
        const auto pointAtDistance = [&] (float distance)
        {
            distance = std::fmod (distance, perimeter);
            if (distance < width)
                return juce::Point<float> (bounds.getX() + distance, bounds.getY());
            distance -= width;
            if (distance < height)
                return juce::Point<float> (bounds.getRight(), bounds.getY() + distance);
            distance -= height;
            if (distance < width)
                return juce::Point<float> (bounds.getRight() - distance, bounds.getBottom());
            distance -= width;
            return juce::Point<float> (bounds.getX(), bounds.getBottom() - distance);
        };

        g.setColour (accent.withAlpha (0.12f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        float distance = progress * perimeter;
        float remaining = segmentLength;
        while (remaining > 0.01f)
        {
            const float edgePosition = std::fmod (distance, perimeter);
            const float edgeLength = edgePosition < width ? width
                : edgePosition < width + height ? height
                : edgePosition < 2.0f * width + height ? width : height;
            const float localPosition = edgePosition < width ? edgePosition
                : edgePosition < width + height ? edgePosition - width
                : edgePosition < 2.0f * width + height
                    ? edgePosition - width - height
                    : edgePosition - 2.0f * width - height;
            const float pieceLength = std::min (remaining, edgeLength - localPosition);
            const auto from = pointAtDistance (distance);
            const auto to = pointAtDistance (distance + pieceLength);
            g.setColour (accent.withAlpha (0.24f));
            g.drawLine (from.x, from.y, to.x, to.y, 5.0f);
            g.setColour (accent.withAlpha (0.7f));
            g.drawLine (from.x, from.y, to.x, to.y, 2.0f);
            g.setColour (accent);
            g.drawLine (from.x, from.y, to.x, to.y, 1.0f);
            distance += pieceLength;
            remaining -= pieceLength;
        }
    }

    void timerCallback() override
    {
        constexpr double durationMs = 1850.0;
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startedAt;
        const float fadeIn = static_cast<float> (std::clamp (elapsed / 100.0, 0.0, 1.0));
        const float fadeOut = static_cast<float> (
            std::clamp ((durationMs - elapsed) / 300.0, 0.0, 1.0));
        opacity = std::min (fadeIn, fadeOut);
        if (elapsed >= durationMs)
        {
            stopTimer();
            buttonBounds.clear();
            setVisible (false);
        }
        repaint();
    }

    std::vector<juce::Rectangle<float>> buttonBounds;
    double startedAt = 0.0;
    float opacity = 0.0f;
};
