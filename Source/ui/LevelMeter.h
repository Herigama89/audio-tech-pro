#pragma once

#include "Theme.h"
#include "Skin.h"
#include "Pictures.h"

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

    /** How every meter's peak marks behave (as Pro Tools' meter options, right-click any meter): how long a peak
        stays (ticks of 1/30 s; -1 for ever, 0 none), and how fast the bars fall back (a factor a tick). */
    struct Ballistics { int holdTicks = 60; float release = 0.85f; };
    static Ballistics& ballistics() { static Ballistics b; return b; }
    /** Called when they're changed from a meter's menu (the app keeps them). */
    static inline std::function<void()> onBallisticsChanged;
    /** Clears every meter's held peaks and clip lights. */
    static void resetAllPeaks() { ++resetGeneration(); }

    void push (float left, float right)
    {
        const auto& b = ballistics();
        if (seenReset != resetGeneration())
        {
            seenReset = resetGeneration();
            peakL = peakR = 0.0f;
            clipHold = 0;
            repaint();
        }
        // Silent and already dark: nothing to draw (many quiet meters cost nothing).
        if (left < 1.0e-4f && right < 1.0e-4f && levelL < 1.0e-4f && levelR < 1.0e-4f && clipHold == 0
            && (b.holdTicks < 0 || (peakL < 1.0e-4f && peakR < 1.0e-4f)))
            return;
        levelL = juce::jmax (left,  levelL * b.release);
        levelR = juce::jmax (right, levelR * b.release);
        if (levelL < 1.0e-4f) levelL = 0.0f;
        if (levelR < 1.0e-4f) levelR = 0.0f;
        const auto wasClipped = clipHold != 0;
        if (juce::jmax (left, right) >= 0.999f)
            clipHold = b.holdTicks < 0 ? -1 : juce::jmax (60, b.holdTicks);   // the clip light: as long as the peaks (2 s at least)
        else if (clipHold > 0)
            --clipHold;

        // The peak marks: held, then falling back with the bars
        auto updatePeak = [&b] (float level, float& peak, int& age)
        {
            if (b.holdTicks == 0) { peak = 0.0f; return; }
            if (level >= peak) { peak = level; age = 0; return; }
            if (b.holdTicks < 0 || ++age <= b.holdTicks) return;
            peak = juce::jmax (level, peak * b.release);
            if (peak < 1.0e-4f) peak = 0.0f;
        };
        updatePeak (levelL, peakL, peakAgeL);
        updatePeak (levelR, peakR, peakAgeR);

        // Redrawn only when what it shows changes: a segment more or less lit, a peak mark, the clip light (with
        // dozens of strips, redrawing every meter 30 times a second whether it moved or not made the mixer crawl)
        const auto shownL = segmentsLit (levelL), shownR = segmentsLit (levelR);
        const auto markL = segmentsLit (peakL), markR = segmentsLit (peakR);
        if (shownL != litL || shownR != litR || markL != peakSegL || markR != peakSegR || wasClipped != (clipHold != 0) || photoLeds)
        {
            litL = shownL;
            litR = shownR;
            peakSegL = markL;
            peakSegR = markR;
            repaint();
        }
    }

    /** Right-click: the meter's menu - peak hold and release (every meter), and anything `addToMenu` adds
        (e.g. hide the meters). A click clears the peaks. */
    std::function<void (juce::PopupMenu&)> addToMenu;
    std::function<void()> onMenu;   // (instead of the menu, when set)
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            resetAllPeaks(), repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return;
        if (onMenu) { onMenu(); return; }
        showBallisticsMenu (addToMenu);
    }

    static void showBallisticsMenu (const std::function<void (juce::PopupMenu&)>& extra = {})
    {
        auto& b = ballistics();
        juce::PopupMenu hold, release, menu;
        const std::pair<const char*, int> holds[] { { "Off", 0 }, { "1 second", 30 }, { "2 seconds", 60 }, { "3 seconds", 90 },
                                                     { "5 seconds", 150 }, { "10 seconds", 300 }, { "Infinite (until cleared)", -1 } };
        for (const auto& [name, ticks] : holds)
            hold.addItem (name, true, b.holdTicks == ticks, [t = ticks] { ballistics().holdTicks = t; resetAllPeaks(); if (onBallisticsChanged) onBallisticsChanged(); });
        const std::pair<const char*, float> releases[] { { "Fast", 0.75f }, { "Medium", 0.85f }, { "Slow", 0.92f }, { "Very slow", 0.96f } };
        for (const auto& [name, factor] : releases)
            release.addItem (name, true, juce::approximatelyEqual (b.release, factor),
                             [f = factor] { ballistics().release = f; if (onBallisticsChanged) onBallisticsChanged(); });
        menu.addSectionHeader ("Meters");
        menu.addSubMenu ("Peak Hold", hold);
        menu.addSubMenu ("Release (how fast they fall)", release);
        menu.addItem ("Clear Peaks and Clip Lights", [] { resetAllPeaks(); });
        if (extra)
        {
            menu.addSeparator();
            extra (menu);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
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
        if (isOpaque())
            g.fillRect (housing);
        else
            g.fillRoundedRectangle (housing, 2.5f);
        g.setColour (juce::Colours::black);
        if (isOpaque())
            g.drawRect (area, 1);   // (a plain edge: a rounded one was a path to fill, every frame)
        else
            g.drawRoundedRectangle (housing.reduced (0.5f), 2.5f, 1.0f);

        auto bars = housing.reduced (2.0f, 3.0f);
        const auto barWidth = (bars.getWidth() - 2.0f) / 2.0f;
        if (photoLeds && theme::xenonSkin())
        {
            drawXenonBar (g, bars.removeFromLeft (barWidth), levelL, clipHold != 0);
            bars.removeFromLeft (2.0f);
            drawXenonBar (g, bars.removeFromLeft (barWidth), levelR, clipHold != 0);
            return;
        }
        if (photoLeds && theme::whiteSkin())
        {
            drawPhotoBar (g, bars.removeFromLeft (barWidth), levelL, clipHold != 0);
            bars.removeFromLeft (2.0f);
            drawPhotoBar (g, bars.removeFromLeft (barWidth), levelR, clipHold != 0);
            return;
        }
        // The LED columns: drawn once (dark and lit) for this size, then each frame just the two pictures,
        // the lit one up to the level
        const auto inner = getLocalBounds().withTrimmedBottom (label.isNotEmpty() ? 16 : 0).reduced (2, 3);
        const auto columnWidth = juce::jmax (1, (inner.getWidth() - 2) / 2);
        drawColumn (g, inner.withWidth (columnWidth), litL, peakSegL);
        drawColumn (g, inner.withTrimmedLeft (columnWidth + 2).withWidth (columnWidth), litR, peakSegR);
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

    void drawColumn (juce::Graphics& g, juce::Rectangle<int> r, int lit, int peak) const
    {
        if (r.isEmpty())
            return;
        // (keyed by a number, not a string built every frame: dozens of meters move 30 times a second)
        const auto scale = pictures::scaleOf (g);
        if (std::abs (scale - std::round (scale)) > 1.0e-3f)
        {
            // Drawn scaled (the mixer's Narrow): pictures would be resampled, which is slow - the LEDs drawn directly are quicker
            drawSegments (g, r.toFloat(), lit, peak);
            return;
        }
        auto key = theme::paletteKey();
        pictures::mix (key, (juce::uint64) (juce::uint32) r.getWidth() << 32 | (juce::uint64) (juce::uint32) r.getHeight());
        pictures::mix (key, (juce::uint64) juce::roundToInt (scale * 1000.0f));
        pictures::mix (key, theme::meterGreen.getARGB());
        pictures::mix (key, theme::meterAmber.getARGB());
        pictures::mix (key, theme::meterRed.getARGB());
        auto keyOn = key;
        pictures::mix (keyOn, 0x5151u);
        const auto local = r.withZeroOrigin().toFloat();
        pictures::drawArea (g, key, r, scale, true, [local] (juce::Graphics& ig) { drawBar (ig, local, 0.0f); });
        const auto& onPicture = pictures::pictureOf (keyOn, r, scale, true, [local] (juce::Graphics& ig) { drawBar (ig, local, 1.0f); });
        auto lightUpTo = [&] (int from, int to)   // segments [from, to) lit, counted from the bottom
        {
            if (to <= from)
                return;
            const auto top = (float) r.getHeight() - (float) to * (segment + gap) + gap - 0.5f;
            const auto bottom = (float) r.getHeight() - (float) from * (segment + gap) + 0.5f;
            pictures::drawRows (g, onPicture, r, (int) std::floor (top), (int) std::ceil (bottom));
        };
        lightUpTo (0, lit);
        if (peak > lit)
            lightUpTo (peak - 1, peak);   // the held peak: its one segment lit above the bar
    }

    /** The column's LEDs, `lit` of them on from the bottom, and the held peak's. */
    static void drawSegments (juce::Graphics& g, juce::Rectangle<float> r, int lit, int peak)
    {
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRect (r);
        const auto count = juce::jmax (1, (int) ((r.getHeight() + gap) / (segment + gap)));
        for (int i = 0; i < count; ++i)
        {
            const auto segDb = juce::jmap ((float) (i + 1) / (float) count, -60.0f, 0.0f);
            const auto colour = segDb > -1.0f ? theme::meterRed
                              : segDb > -6.0f ? theme::meterAmber
                              : segDb > -18.0f ? theme::meterGreen.brighter (0.25f)
                              : theme::meterGreen;
            const bool on = i < lit || i == peak - 1;
            g.setColour (on ? colour : colour.withAlpha (0.13f));
            g.fillRect (r.getX(), r.getBottom() - (float) (i + 1) * (segment + gap) + gap, r.getWidth(), segment);
        }
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
    int clipHold = 0, litL = 0, litR = 0, peakSegL = 0, peakSegR = 0, peakAgeL = 0, peakAgeR = 0;
    float levelL = 0, levelR = 0, peakL = 0, peakR = 0;
    static int& resetGeneration() { static int g = 0; return g; }
    int seenReset = 0;
};

} // namespace riff
