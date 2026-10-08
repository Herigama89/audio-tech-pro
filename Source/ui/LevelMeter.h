#pragma once

#include "Theme.h"

namespace riff
{

/** A stereo peak meter with instant attack and gentle release, -60..0 dBFS. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

    void push (float left, float right)
    {
        levelL = juce::jmax (left,  levelL * 0.85f);
        levelR = juce::jmax (right, levelR * 0.85f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, area.removeFromBottom (16), juce::Justification::centred);

        auto bars = area.toFloat().reduced (2.0f, 0.0f);
        const auto barWidth = (bars.getWidth() - 3.0f) / 2.0f;
        drawBar (g, bars.removeFromLeft (barWidth), levelL);
        bars.removeFromLeft (3.0f);
        drawBar (g, bars.removeFromLeft (barWidth), levelR);
    }

private:
    static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float linear)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 2.0f);

        const auto db = juce::Decibels::gainToDecibels (linear, -60.0f);
        const auto proportion = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

        g.setColour (db > -0.5f ? theme::danger : db > -12.0f ? theme::meterAmber : theme::meterGreen);
        g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * proportion), 2.0f);
    }

    juce::String label;
    float levelL = 0, levelR = 0;
};

} // namespace riff
