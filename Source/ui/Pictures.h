#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include "Theme.h"

/**
    Pictures of small controls (keys, knobs, value boxes), drawn once for each look they can have and copied after.
    The mixer has hundreds of them; drawing each one's gradients, paths and text every time it was painted made
    resizing and scrolling slow. A control's look is summed up in a 64-bit key: whatever changes it goes in.
*/
namespace riff::pictures
{
    inline void mix (juce::uint64& h, juce::uint64 v) noexcept
    {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }

    /** A key to start from: the theme, the control's size and the screen's scale. */
    inline juce::uint64 start (const juce::Component& c, float scale) noexcept
    {
        juce::uint64 h = theme::paletteKey();
        mix (h, (juce::uint64) (juce::uint32) c.getWidth() << 32 | (juce::uint64) (juce::uint32) c.getHeight());
        mix (h, (juce::uint64) juce::roundToInt (scale * 1000.0f));
        mix (h, c.isEnabled() ? 1u : 0u);
        return h;
    }

    inline juce::uint64 ofText (const juce::String& s) noexcept { return (juce::uint64) s.hashCode64(); }

    /** The screen's scale for pictures (so they're copied pixel for pixel). */
    inline float scaleOf (juce::Graphics& g) noexcept
    {
        return juce::jlimit (0.25f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    /** The picture for `key`, of a `size` area at `scale`, made with `paint` (in the area's own coordinates) the first
        time. `opaque`: it covers its area (so it's copied rather than blended). */
    template <typename Painter>
    const juce::Image& pictureOf (juce::uint64 key, juce::Rectangle<int> size, float scale, bool opaque, Painter&& paint)
    {
        static std::unordered_map<juce::uint64, juce::Image> store;
        auto it = store.find (key);
        if (it == store.end())
        {
            if (store.size() > 4000)
                store.clear();
            juce::Image image (opaque ? juce::Image::RGB : juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) size.getWidth() * scale)),
                               juce::jmax (1, juce::roundToInt ((float) size.getHeight() * scale)), true);
            {
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::scale ((float) image.getWidth() / (float) juce::jmax (1, size.getWidth()),
                                                               (float) image.getHeight() / (float) juce::jmax (1, size.getHeight())));
                paint (ig);
            }
            it = store.emplace (key, std::move (image)).first;
        }
        return it->second;
    }

    /** A picture of `area` (in g's coordinates) for `key` (see pictureOf). */
    template <typename Painter>
    void drawArea (juce::Graphics& g, juce::uint64 key, juce::Rectangle<int> area, float scale, bool opaque, Painter&& paint)
    {
        if (area.isEmpty())
            return;
        const auto& image = pictureOf (key, area, scale, opaque, std::forward<Painter> (paint));
        if (std::abs (scale - 1.0f) < 1.0e-3f)
            g.drawImageAt (image, area.getX(), area.getY());
        else
            g.drawImage (image, area.toFloat());
    }

    /** Rows [fromY, toY) of an area's picture (in the area's coordinates), drawn without clipping (clipping to a
        rectangle under a scaled view, the mixer's Narrow, was very slow). */
    inline void drawRows (juce::Graphics& g, const juce::Image& image, juce::Rectangle<int> area, int fromY, int toY)
    {
        fromY = juce::jlimit (0, area.getHeight(), fromY);
        toY = juce::jlimit (fromY, area.getHeight(), toY);
        if (toY <= fromY || ! image.isValid())
            return;
        const auto ky = (float) image.getHeight() / (float) juce::jmax (1, area.getHeight());
        const auto py0 = juce::jlimit (0, image.getHeight(), juce::roundToInt ((float) fromY * ky));
        const auto py1 = juce::jlimit (py0, image.getHeight(), juce::roundToInt ((float) toY * ky));
        if (py1 <= py0)
            return;
        g.drawImage (image.getClippedImage ({ 0, py0, image.getWidth(), py1 - py0 }),
                     juce::Rectangle<float> ((float) area.getX(), (float) area.getY() + (float) py0 / ky, (float) area.getWidth(), (float) (py1 - py0) / ky));
    }

    /** Draws the control's picture for `key` (made with `paint`, in the control's own coordinates, the first time). */
    template <typename Painter>
    void draw (juce::Graphics& g, juce::uint64 key, const juce::Component& c, float scale, Painter&& paint)
    {
        static std::unordered_map<juce::uint64, juce::Image> store;
        if (c.getWidth() <= 0 || c.getHeight() <= 0)
            return;
        auto it = store.find (key);
        if (it == store.end())
        {
            if (store.size() > 8000)   // (a few MB at most; started again when full)
                store.clear();
            juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) c.getWidth() * scale)),
                               juce::jmax (1, juce::roundToInt ((float) c.getHeight() * scale)), true);
            {
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::scale ((float) image.getWidth() / (float) c.getWidth(),
                                                               (float) image.getHeight() / (float) c.getHeight()));
                paint (ig);
            }
            it = store.emplace (key, std::move (image)).first;
        }
        if (std::abs (scale - 1.0f) < 1.0e-3f)
            g.drawImageAt (it->second, 0, 0);
        else
            g.drawImage (it->second, c.getLocalBounds().toFloat());
    }
}
