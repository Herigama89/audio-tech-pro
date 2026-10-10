#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

/**
    The mixer skin's pieces, cut from the reference picture
    (Resources/skin/source.jpg) by Tools/make_skin_from_image.py.
    Each is loaded once and cached.
*/
namespace riff::skin
{
    #define RIFF_SKIN_IMAGE(fn, name) \
        inline juce::Image fn() { return juce::ImageCache::getFromMemory (BinaryData::name, BinaryData::name##Size); }

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
