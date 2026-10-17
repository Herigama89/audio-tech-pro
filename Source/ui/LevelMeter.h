#pragma once

#include "Theme.h"
#include "Skin.h"

namespace riff
{

/** A stereo peak meter with instant attack and gentle release, -60..0 dBFS. */
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText))
    {
        // Without a caption it fills itself: its updates (30 a second) don't redraw what's behind it.
        setOpaque (label.isEmpty());
    }

    void push (float left, float right)
    {
        // Silent and already dark: nothing to draw (many quiet meters cost nothing).
        if (left < 1.0e-4f && right < 1.0e-4f && levelL < 1.0e-4f && levelR < 1.0e-4f && clipHold == 0)
            return;
        levelL = juce::jmax (left,  levelL * 0.85f);
        levelR = juce::jmax (right, levelR * 0.85f);
        if (levelL < 1.0e-4f) levelL = 0.0f;
        if (levelR < 1.0e-4f) levelR = 0.0f;
        if (juce::jmax (left, right) >= 0.999f)
            clipHold = 60;   // the clip light stays on for about two seconds
        else if (clipHold > 0)
            --clipHold;
        repaint();
    }

    /** Right-click: the meter's menu (e.g. hide the meters). */
    std::function<void()> onMenu;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onMenu) onMenu(); }

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
        if (isOpaque())
            g.fillRect (housing);
        else
            g.fillRoundedRectangle (housing, 2.5f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (housing.reduced (0.5f), 2.5f, 1.0f);

        auto bars = housing.reduced (2.0f, 3.0f);
        const auto barWidth = (bars.getWidth() - 2.0f) / 2.0f;
        if (photoLeds && theme::xenonSkin())
        {
            drawXenonBar (g, bars.removeFromLeft (barWidth), levelL, clipHold > 0);
            bars.removeFromLeft (2.0f);
            drawXenonBar (g, bars.removeFromLeft (barWidth), levelR, clipHold > 0);
            return;
        }
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
            const auto colour = segDb > -1.0f ? theme::meterRed
                              : segDb > -6.0f ? theme::meterAmber
                              : segDb > -18.0f ? theme::meterGreen.brighter (0.25f)
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

    /** Xenon: the picture's teal meter column, lit from the bottom to the level (the rest faint); red at the top when it clipped. */
    static void drawXenonBar (juce::Graphics& g, juce::Rectangle<float> r, float linear, bool clipped)
    {
        const auto image = skin::xenonMeter();
        const auto clipArea = r.removeFromTop (juce::jmax (3.0f, r.getWidth()));
        g.setColour (theme::meterRed.withAlpha (clipped ? 1.0f : 0.18f));
        g.fillRect (clipArea);
        r.removeFromTop (2.0f);
        const auto lit = juce::jlimit (0.0f, 1.0f, juce::jmap (juce::Decibels::gainToDecibels (linear, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f));
        // The column, faint and lit, made once for its size (a scaled photo 30 times a second was heavy on a Retina screen)
        const auto column = r.toNearestInt();
        auto paintColumn = [image, column] (float opacity)
        {
            return [image, column, opacity] (juce::Graphics& ig)
            {
                ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                ig.setOpacity (opacity);
                ig.drawImage (image, column.withZeroOrigin().toFloat(), juce::RectanglePlacement::stretchToFit);
            };
        };
        juce::Graphics::ScopedSaveState state (g);
        skin::drawCached (g, "xenonMeterFaint", column, paintColumn (0.16f));
        if (lit > 0.0f)
        {
            g.reduceClipRegion (r.withTop (r.getBottom() - r.getHeight() * lit).getSmallestIntegerContainer());
            skin::drawCached (g, "xenonMeterLit", column, paintColumn (1.0f));
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
