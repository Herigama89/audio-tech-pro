#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <BinaryData.h>

/**
    The mixer skin's pieces, cut from the reference picture
    (Resources/skin/source.jpg) by Tools/make_skin_from_image.py.
    Each is loaded once and cached.
*/
namespace riff::skin
{
    #define RIFF_SKIN_IMAGE(fn, name) \
        inline juce::Image fn() { static const juce::Image image = juce::ImageCache::getFromMemory (BinaryData::name, BinaryData::name##Size); return image; }
        // (held for good: JUCE's image cache lets go of a picture after a few seconds, and decoding it again --
        //  and redoing everything cached from it -- was a big part of what made the picture skins heavy)

    RIFF_SKIN_IMAGE (stripTop,  strip_top_png)    // the channel strip's top: knob scales, keys, Dynamics (196 x 861)
    RIFF_SKIN_IMAGE (stripFace, strip_face_png)   // a plain stretch of the strip (tiled downwards)
    RIFF_SKIN_IMAGE (slot,      slot_png)         // an insert slot
    RIFF_SKIN_IMAGE (send,      send_png)         // a send slot
    RIFF_SKIN_IMAGE (dbBox,     db_box_png)       // the level box
    RIFF_SKIN_IMAGE (faderCap,  fader_cap_png)
    RIFF_SKIN_IMAGE (knobBlack, knob_black_png)   // knob bodies, without their pointers
    RIFF_SKIN_IMAGE (knobWhite, knob_white_png)
    RIFF_SKIN_IMAGE (knobBus,   knob_bus_png)
    RIFF_SKIN_IMAGE (vuFace,    vu_face_png)
    RIFF_SKIN_IMAGE (panel,     panel_png)        // the black centre panel (tiled)

    // Studio White (Tools/make_white_skin.py): cut from your photos
    RIFF_SKIN_IMAGE (whiteCap,      white_fader_cap_png)        // the long-throw fader's cap, in a white shade
    RIFF_SKIN_IMAGE (whiteCapBlack, white_fader_cap_black_png)  // the same cap as photographed
    RIFF_SKIN_IMAGE (whiteRail,     white_fader_rail_png)       // the fader's rail (tiled down the slot)
    RIFF_SKIN_IMAGE (whiteKnobBody, white_knob_body_png)        // the knob: knurled edge, lit inner ring
    RIFF_SKIN_IMAGE (whiteKnobRing, white_knob_ring_png)        // its LED ring (lit up to the value)
    RIFF_SKIN_IMAGE (whiteLed,      white_led_on_png)           // one lit meter segment
    RIFF_SKIN_IMAGE (whiteClip,     white_led_clip_png)         // the clip light

    // Xenon (Tools/make_xenon_skin.py): cut from your picture
    RIFF_SKIN_IMAGE (xenonKnob,  xenon_knob_png)    // the glass knob, its lit edge on the left
    RIFF_SKIN_IMAGE (xenonCap,   xenon_cap_png)     // the fader cap
    RIFF_SKIN_IMAGE (xenonSlot,  xenon_slot_png)    // a lit insert slot
    RIFF_SKIN_IMAGE (xenonMeter, xenon_meter_png)   // the teal meter column
    RIFF_SKIN_IMAGE (xenonPanel, xenon_panel_png)   // the dark panel texture (tiled)
    RIFF_SKIN_IMAGE (xenonBar,   xenon_bar_png)     // the carved bar ornament

    #undef RIFF_SKIN_IMAGE

    /**
        Studio White's knob: the photo's knob, and its LED ring lit from the start of its travel to the
        value (the rest of the ring dim). `start`/`end` are the rotary range in radians (0 = up, clockwise).
        From the centre for knobs like pan (lit from the top to the value).
    */
    inline void drawPhotoKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, float start, float end,
                               bool fromCentre, bool enabled = true)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto r = area.withSizeKeepingCentre (size, size);
        const auto body = whiteKnobBody(), ring = whiteKnobRing();
        juce::Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        // The ring: dim all round, then lit over the value's arc.
        g.setOpacity (enabled ? 0.18f : 0.1f);
        g.drawImage (ring, r);
        const auto angle = start + proportion * (end - start);
        auto a0 = start, a1 = angle;
        if (fromCentre)
        {
            const auto mid = (start + end) * 0.5f;
            a0 = juce::jmin (mid, angle);
            a1 = juce::jmax (mid, angle);
        }
        if (a1 - a0 > 0.01f)
        {
            juce::Path wedge;
            wedge.addPieSegment (r.expanded (2.0f), a0, a1, 0.0f);
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (wedge);
            g.setOpacity (enabled ? 1.0f : 0.5f);
            g.drawImage (ring, r);
        }

        // The knob itself (round, so it looks the same at any angle).
        g.setOpacity (enabled ? 1.0f : 0.6f);
        g.drawImage (body, r);
    }

    /**
        Xenon's knob: the picture's glass knob turned so its lit edge points at the value
        (`start`/`end`: the rotary range in radians, 0 = up, clockwise).
    */
    inline void drawXenonKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, float start, float end,
                               bool enabled = true)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto r = area.withSizeKeepingCentre (size, size);
        const auto image = xenonKnob();
        if (! image.isValid() || size < 2.0f)
            return;
        const auto angle = start + proportion * (end - start);

        // Drawn once per size and angle (in 1/256 turns) and kept: knobs redraw often.
        static std::map<juce::int64, juce::Image> cache;
        const auto pixels = juce::roundToInt (size * 2.0f);   // (2x for sharp screens)
        const auto step = (juce::roundToInt (angle / juce::MathConstants<float>::twoPi * 256.0f) % 256 + 256) % 256;
        const auto key = (juce::int64) pixels * 1024 + step;
        auto it = cache.find (key);
        if (it == cache.end())
        {
            if (cache.size() > 4096)
                cache.clear();
            juce::Image frame (juce::Image::ARGB, pixels, pixels, true);
            {
                juce::Graphics fg (frame);
                fg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                const auto turn = (float) step / 256.0f * juce::MathConstants<float>::twoPi + juce::MathConstants<float>::halfPi;   // its lit edge sits at the left
                fg.drawImageTransformed (image, juce::AffineTransform::translation (-image.getWidth() * 0.5f, -image.getHeight() * 0.5f)
                                                    .rotated (turn)
                                                    .scaled ((float) pixels / (float) image.getWidth())
                                                    .translated ((float) pixels * 0.5f, (float) pixels * 0.5f));
            }
            it = cache.emplace (key, frame).first;
        }
        juce::Graphics::ScopedSaveState state (g);
        g.setOpacity (enabled ? 1.0f : 0.55f);
        g.drawImage (it->second, r);
    }

    /** An image with its mirror images beside and below it (2 x 2): it tiles without seams. Cached. */
    inline juce::Image mirroredTile (const juce::Image& source)
    {
        static std::map<juce::String, juce::Image> cache;
        const auto key = juce::String ((juce::int64) source.getPixelData().get());
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
        const auto w = source.getWidth(), h = source.getHeight();
        juce::Image out (juce::Image::ARGB, w * 2, h * 2, true);
        {
            juce::Graphics g (out);
            g.drawImageAt (source, 0, 0);
            g.drawImageTransformed (source, juce::AffineTransform::scale (-1.0f, 1.0f).translated ((float) w * 2.0f, 0.0f));
            g.drawImageTransformed (source, juce::AffineTransform::scale (1.0f, -1.0f).translated (0.0f, (float) h * 2.0f));
            g.drawImageTransformed (source, juce::AffineTransform::scale (-1.0f, -1.0f).translated ((float) w * 2.0f, (float) h * 2.0f));
        }
        return cache[key] = out;
    }

    /**
        Draws something expensive (a texture laid over a whole panel, a picture repeated along a bar) from a
        picture of it made once, at the screen's own resolution: after the first time it's one plain copy,
        which matters on a Retina screen, where a tiled or scaled image fill is slow. A few sizes are kept
        (the least recently used go). `key` says what's drawn; the size and the screen's scale are added.
    */
    inline void drawCached (juce::Graphics& g, const juce::String& key, juce::Rectangle<int> area,
                            const std::function<void (juce::Graphics&)>& paint, bool opaque = false)
    {
        if (area.isEmpty())
            return;
        struct Entry { juce::Image image; juce::uint64 lastUse = 0; };
        static std::map<juce::String, Entry> cache;
        static juce::uint64 uses = 0;

        // (at the screen's own scale - smaller too, in a strip drawn narrow - so it's copied, not resampled)
        const auto scale = juce::jlimit (0.25f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto fullKey = key + (opaque ? "|o|" : "|") + juce::String (area.getWidth()) + "x" + juce::String (area.getHeight()) + "@" + juce::String (scale, 3);
        auto it = cache.find (fullKey);
        if (it == cache.end())
        {
            // Room for many small pictures (meter columns, knobs' faces) and a few big ones (panels):
            // the least recently used go when there are too many, or too many pixels.
            const auto pixelsOf = [] (const Entry& e) { return (juce::int64) e.image.getWidth() * e.image.getHeight(); };
            juce::int64 pixels = 0;
            for (const auto& [k, e] : cache)
                pixels += pixelsOf (e);
            while (! cache.empty() && (cache.size() >= 96 || pixels > 24'000'000))
            {
                const auto oldest = std::min_element (cache.begin(), cache.end(),
                                                      [] (const auto& a, const auto& b) { return a.second.lastUse < b.second.lastUse; });
                pixels -= pixelsOf (oldest->second);
                cache.erase (oldest);
            }
            Entry e;
            e.image = juce::Image (opaque ? juce::Image::RGB : juce::Image::ARGB, juce::roundToInt ((float) area.getWidth() * scale),
                                   juce::roundToInt ((float) area.getHeight() * scale), true);
            {
                juce::Graphics ig (e.image);
                ig.addTransform (juce::AffineTransform::scale (scale));
                paint (ig);
            }
            it = cache.emplace (fullKey, std::move (e)).first;
        }
        it->second.lastUse = ++uses;
        juce::Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.setOpacity (1.0f);   // (an image is drawn at the current colour's opacity: the caller's colour mustn't fade it)
        g.drawImageTransformed (it->second.image, juce::AffineTransform::scale (1.0f / scale).translated ((float) area.getX(), (float) area.getY()));
    }

    /** Lays a texture over an area: the picture's piece tiled, every other tile mirrored so no seams show (cached: see drawCached). */
    inline void tileTexture (juce::Graphics& g, const juce::Image& image, juce::Rectangle<int> area, float opacity = 1.0f)
    {
        if (! image.isValid() || area.isEmpty())
            return;
        const auto key = "tile" + juce::String ((juce::int64) image.getPixelData().get()) + "/" + juce::String (opacity, 3);
        drawCached (g, key, area, [&image, area, opacity] (juce::Graphics& ig)
        {
            ig.setTiledImageFill (mirroredTile (image), 0, 0, opacity);
            ig.fillRect (area.withZeroOrigin());
        });
    }

    /** An image turned a quarter turn (the photo's meter runs sideways; ours run upwards). Cached. */
    inline juce::Image quarterTurn (const juce::Image& source)
    {
        static std::map<juce::String, juce::Image> cache;
        const auto key = juce::String ((juce::int64) source.getPixelData().get());
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
        juce::Image out (juce::Image::ARGB, source.getHeight(), source.getWidth(), true);
        {
            juce::Graphics g (out);
            g.drawImageTransformed (source, juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                                .translated (0.0f, (float) source.getWidth()));
        }
        return cache[key] = out;
    }

    /** The art's scale: the picture's strip (56 px wide) is drawn 3.5x into a 196-wide space. */
    constexpr float artScale = 3.5f;
    constexpr float stripX = 60.0f, stripY = 183.0f;   // where the strip sits in the picture

    /** Picture coordinates -> strip art coordinates. */
    constexpr float artX (float x) { return (x - stripX) * artScale; }
    constexpr float artY (float y) { return (y - stripY) * artScale; }

    /** Tiles an image downwards to fill an area (its width stretched to the area's). */
    inline void tileDown (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area)
    {
        if (! image.isValid())
            return;
        const auto h = area.getWidth() * (float) image.getHeight() / (float) image.getWidth();
        juce::Graphics::ScopedSaveState state (g);
        g.setOpacity (1.0f);
        g.reduceClipRegion (area.toNearestInt());
        for (auto y = area.getY(); y < area.getBottom(); y += h - 0.5f)
            g.drawImage (image, juce::Rectangle<float> (area.getX(), y, area.getWidth(), h));
    }

    /** A knob from the picture: its body, then a pointer line at `angle`. */
    inline void drawKnob (juce::Graphics& g, const juce::Image& body, juce::Rectangle<float> area, float angle,
                          juce::Colour pointer, float alpha = 1.0f)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto r = area.withSizeKeepingCentre (size, size);
        g.setOpacity (alpha);
        g.drawImage (body, r);
        const auto c = r.getCentre();
        const auto radius = size * 0.5f;
        auto at = [&] (float f) { return c + juce::Point<float> (radius * f * std::sin (angle), -radius * f * std::cos (angle)); };
        g.setColour (pointer.withAlpha (0.95f * alpha));
        g.drawLine (juce::Line<float> (at (0.12f), at (0.8f)), juce::jmax (1.2f, size * 0.085f));
    }
}
