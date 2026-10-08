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

        if (label.isNotEmpty())
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (label, area.removeFromBottom (16), juce::Justification::centred);
        }

        // Black housing with two LED columns
        auto housing = area.toFloat();
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (housing, 2.5f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (housing.reduced (0.5f), 2.5f, 1.0f);

        auto bars = housing.reduced (2.0f, 3.0f);
        const auto barWidth = (bars.getWidth() - 2.0f) / 2.0f;
        drawBar (g, bars.removeFromLeft (barWidth), levelL);
        bars.removeFromLeft (2.0f);
        drawBar (g, bars.removeFromLeft (barWidth), levelR);
    }

private:
    static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float linear)
    {
        const auto db = juce::Decibels::gainToDecibels (linear, -60.0f);
        const auto lit = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

        constexpr float segment = 3.0f, gap = 1.0f;
        const auto count = juce::jmax (1, (int) ((r.getHeight() + gap) / (segment + gap)));

        for (int i = 0; i < count; ++i)
        {
            const auto proportion = (float) (i + 1) / (float) count;
            const auto segDb = juce::jmap (proportion, -60.0f, 0.0f);
            const auto colour = segDb > -1.0f ? theme::danger : segDb > -12.0f ? theme::meterAmber : theme::meterGreen;
            const auto y = r.getBottom() - (float) (i + 1) * (segment + gap) + gap;
            g.setColour (proportion <= lit + 0.5f / (float) count ? colour : colour.withAlpha (0.12f));
            g.fillRect (r.getX(), y, r.getWidth(), segment);
        }
    }

    juce::String label;
    float levelL = 0, levelR = 0;
};

} // namespace riff
