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
        const auto wasClipped = clipHold > 0;
        if (juce::jmax (left, right) >= 0.999f)
            clipHold = 60;   // the clip light stays on for about two seconds
        else if (clipHold > 0)
            --clipHold;

        // Redrawn only when what it shows changes: a segment more or less lit, the clip light (with dozens of
        // strips, redrawing every meter 30 times a second whether it moved or not made the mixer crawl)
        const auto shownL = segmentsLit (levelL), shownR = segmentsLit (levelR);
        if (shownL != litL || shownR != litR || wasClipped != (clipHold > 0) || photoLeds)
        {
            litL = shownL;
            litR = shownR;
            repaint();
        }
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
        // The LED columns: drawn once (dark and lit) for this size, then each frame just the two pictures,
        // the lit one up to the level
        const auto inner = getLocalBounds().withTrimmedBottom (label.isNotEmpty() ? 16 : 0).reduced (2, 3);
        const auto columnWidth = juce::jmax (1, (inner.getWidth() - 2) / 2);
        drawColumn (g, inner.withWidth (columnWidth), litL);
        drawColumn (g, inner.withTrimmedLeft (columnWidth + 2).withWidth (columnWidth), litR);
    }

private:
    static constexpr float segment = 3.0f, gap = 1.0f;
    int segmentCount() const
    {
        const auto h = (float) getHeight() - (label.isNotEmpty() ? 16.0f : 0.0f) - 6.0f;
        return juce::jmax (1, (int) ((h + gap) / (segment + gap)));
    }
    int segmentsLit (float linear) const
    {
        const auto count = segmentCount();
        const auto lit = juce::jmap (juce::Decibels::gainToDecibels (linear, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f);
        return juce::jlimit (0, count, (int) std::floor (lit * (float) count + 0.5f));
    }

    void drawColumn (juce::Graphics& g, juce::Rectangle<int> r, int lit) const
    {
        if (r.isEmpty())
            return;
        const auto key = "meter" + juce::String (r.getWidth()) + "x" + juce::String (r.getHeight()) + "/"
                       + theme::meterGreen.toString() + theme::meterAmber.toString() + theme::meterRed.toString();
        const auto local = r.withZeroOrigin().toFloat();
        skin::drawCached (g, key + "off", r, [local] (juce::Graphics& ig) { drawBar (ig, local, 0.0f); }, true);
        if (lit <= 0)
            return;
        const auto count = segmentCount();
        const auto top = (float) r.getBottom() - (float) lit * (segment + gap) + gap - 0.5f;
        juce::Graphics::ScopedSaveState state (g);
        if (g.reduceClipRegion (r.withTop (juce::jmax (r.getY(), (int) std::floor (top)))))
            skin::drawCached (g, key + "on", r, [local] (juce::Graphics& ig) { drawBar (ig, local, 1.0f); }, true);
        juce::ignoreUnused (count);
    }

    static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float linear)
    {
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRect (r);
        const auto db = juce::Decibels::gainToDecibels (linear, -60.0f);
        const auto lit = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

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
    int clipHold = 0, litL = 0, litR = 0;
    float levelL = 0, levelR = 0;
};

} // namespace riff
