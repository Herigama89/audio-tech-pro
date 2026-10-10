#pragma once

#include "Theme.h"
#include "Skin.h"

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
        if (juce::jmax (left, right) >= 0.999f)
            clipHold = 60;   // the clip light stays on for about two seconds
        else if (clipHold > 0)
            --clipHold;
        repaint();
    }

    /** The master meter in Studio White: the photo's blue LED segments and red clip light. */
    void usePhotoLeds (bool shouldUse) { photoLeds = shouldUse; repaint(); }

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
        if (photoLeds && theme::whiteSkin())
        {
            drawPhotoBar (g, bars.removeFromLeft (barWidth), levelL, clipHold > 0);
            bars.removeFromLeft (2.0f);
            drawPhotoBar (g, bars.removeFromLeft (barWidth), levelR, clipHold > 0);
            return;
        }
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
            // Console LEDs: green, brighter green, yellow near the top, red at 0 dB.
            const auto colour = segDb > -1.0f ? theme::danger
                              : segDb > -6.0f ? theme::meterAmber
                              : segDb > -18.0f ? juce::Colour (0xff7ad85a)
                              : theme::meterGreen;
            const auto y = r.getBottom() - (float) (i + 1) * (segment + gap) + gap;
            const bool on = proportion <= lit + 0.5f / (float) count;
            if (on)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillRect (r.getX() - 0.5f, y - 0.5f, r.getWidth() + 1.0f, segment + 1.0f);
            }
            g.setColour (on ? colour : colour.withAlpha (0.13f));
            g.fillRect (r.getX(), y, r.getWidth(), segment);
        }
    }

    static void drawPhotoBar (juce::Graphics& g, juce::Rectangle<float> r, float linear, bool clipped)
    {
        // The clip light on top, then the blue segments (lit ones bright, the rest faint).
        const auto led = skin::quarterTurn (skin::whiteLed()), clip = skin::quarterTurn (skin::whiteClip());
        const auto segHeight = juce::jmax (3.0f, r.getWidth() * (float) led.getHeight() / (float) led.getWidth());
        const auto clipArea = r.removeFromTop (juce::jmax (3.0f, r.getWidth() * (float) clip.getHeight() / (float) clip.getWidth()));
        g.setOpacity (clipped ? 1.0f : 0.22f);
        g.drawImage (clip, clipArea);
        r.removeFromTop (2.0f);

        const auto lit = juce::jmap (juce::Decibels::gainToDecibels (linear, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f);
        const auto count = juce::jmax (1, (int) (r.getHeight() / segHeight));
        for (int i = 0; i < count; ++i)
        {
            const auto proportion = (float) (i + 1) / (float) count;
            const bool on = proportion <= lit + 0.5f / (float) count;
            g.setOpacity (on ? 1.0f : 0.14f);
            g.drawImage (led, juce::Rectangle<float> (r.getX(), r.getBottom() - (float) (i + 1) * segHeight, r.getWidth(), segHeight));
        }
        g.setOpacity (1.0f);
    }

    juce::String label;
    bool photoLeds = false;
    int clipHold = 0;
    float levelL = 0, levelR = 0;
};

} // namespace riff
