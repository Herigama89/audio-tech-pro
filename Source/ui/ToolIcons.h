#pragma once

#include <JuceHeader.h>
#include "TimelineState.h"
#include <map>

/**
    Icons for the Edit window's tools (Pointer, Pencil, Eraser...), drawn in
    code on a 24 x 24 grid and scaled to fit, for the tool menus and the Tool
    box in the control bar.
*/
namespace riff::toolicons
{
    using Tool = TimelineState::Tool;

    inline juce::Path speaker()
    {
        juce::Path p;
        p.startNewSubPath (3.0f, 9.0f);
        p.lineTo (7.0f, 9.0f);
        p.lineTo (12.0f, 4.5f);
        p.lineTo (12.0f, 19.5f);
        p.lineTo (7.0f, 15.0f);
        p.lineTo (3.0f, 15.0f);
        p.closeSubPath();
        return p;
    }

    /** The filled part of a tool's icon, in a 24 x 24 box. */
    inline juce::Path filled (Tool tool)
    {
        juce::Path p;
        switch (tool)
        {
            case Tool::pointer:
                p.startNewSubPath (6.0f, 2.5f);
                p.lineTo (6.0f, 19.0f);
                p.lineTo (10.0f, 15.2f);
                p.lineTo (12.8f, 21.5f);
                p.lineTo (15.6f, 20.3f);
                p.lineTo (12.8f, 14.0f);
                p.lineTo (18.2f, 14.0f);
                p.closeSubPath();
                break;
            case Tool::pencil:
            {
                juce::Path body;
                body.addRectangle (-2.4f, -9.0f, 4.8f, 14.5f);
                body.startNewSubPath (-2.4f, 5.5f);
                body.lineTo (2.4f, 5.5f);
                body.lineTo (0.0f, 10.5f);
                body.closeSubPath();
                body.addRectangle (-2.4f, -12.0f, 4.8f, 2.2f);
                body.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (40.0f)).translated (12.0f, 12.0f));
                p = body;
                break;
            }
            case Tool::eraser:
            {
                juce::Path body;
                body.addRoundedRectangle (-4.5f, -9.0f, 9.0f, 18.0f, 1.8f);
                body.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (45.0f)).translated (12.0f, 11.0f));
                p = body;
                p.addRectangle (4.0f, 20.0f, 16.0f, 1.6f);
                break;
            }
            case Tool::scissors:
                p.addEllipse (3.0f, 14.0f, 6.0f, 6.0f);
                p.addEllipse (15.0f, 14.0f, 6.0f, 6.0f);
                break;
            case Tool::glue:
                p.addRoundedRectangle (4.0f, 9.0f, 11.0f, 9.0f, 1.5f);
                p.startNewSubPath (15.0f, 11.0f);
                p.lineTo (19.0f, 12.5f);
                p.lineTo (19.0f, 14.5f);
                p.lineTo (15.0f, 16.0f);
                p.closeSubPath();
                p.addEllipse (18.5f, 17.0f, 2.8f, 3.6f);
                break;
            case Tool::mute:
            case Tool::solo:
                p = speaker();
                break;
            case Tool::fade:
                p.startNewSubPath (3.0f, 19.0f);
                p.lineTo (21.0f, 5.0f);
                p.lineTo (21.0f, 19.0f);
                p.closeSubPath();
                break;
            case Tool::text:
            {
                juce::GlyphArrangement glyphs;
                glyphs.addLineOfText (juce::FontOptions (20.0f, juce::Font::bold), "A", 0.0f, 0.0f);
                glyphs.createPath (p);
                p.applyTransform (p.getTransformToScaleToFit (4.0f, 3.0f, 16.0f, 18.0f, true));
                break;
            }
            case Tool::gain:
                // A fader cap on its slot
                p.addRoundedRectangle (7.0f, 10.0f, 10.0f, 5.0f, 1.0f);
                break;
            case Tool::stretch:
                // A short waveform between two arrows
                for (const auto [x, h] : { std::pair { 9.0f, 4.0f }, { 11.0f, 9.0f }, { 13.0f, 6.0f }, { 15.0f, 10.0f } })
                    p.addRectangle (x - 0.75f, 12.0f - h * 0.5f, 1.5f, h);
                break;
            case Tool::zoom:
            case Tool::marquee:
            case Tool::select:
                break;
        }
        return p;
    }

    /** The outlined part (lines), in a 24 x 24 box. */
    inline juce::Path stroked (Tool tool)
    {
        juce::Path p;
        switch (tool)
        {
            case Tool::scissors:
                p.startNewSubPath (8.0f, 15.0f);
                p.lineTo (17.5f, 2.5f);
                p.startNewSubPath (16.0f, 15.0f);
                p.lineTo (6.5f, 2.5f);
                break;
            case Tool::mute:
                p.startNewSubPath (15.0f, 9.0f);
                p.lineTo (21.0f, 15.0f);
                p.startNewSubPath (21.0f, 9.0f);
                p.lineTo (15.0f, 15.0f);
                break;
            case Tool::solo:
                p.addCentredArc (12.0f, 12.0f, 4.5f, 4.5f, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (140.0f), true);
                p.addCentredArc (12.0f, 12.0f, 8.5f, 8.5f, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (140.0f), true);
                break;
            case Tool::zoom:
                p.addEllipse (3.5f, 3.5f, 12.0f, 12.0f);
                p.startNewSubPath (14.0f, 14.0f);
                p.lineTo (20.5f, 20.5f);
                p.startNewSubPath (6.5f, 9.5f);
                p.lineTo (12.5f, 9.5f);
                p.startNewSubPath (9.5f, 6.5f);
                p.lineTo (9.5f, 12.5f);
                break;
            case Tool::marquee:
                p.startNewSubPath (12.0f, 2.0f);
                p.lineTo (12.0f, 9.0f);
                p.startNewSubPath (12.0f, 15.0f);
                p.lineTo (12.0f, 22.0f);
                p.startNewSubPath (2.0f, 12.0f);
                p.lineTo (9.0f, 12.0f);
                p.startNewSubPath (15.0f, 12.0f);
                p.lineTo (22.0f, 12.0f);
                break;
            case Tool::fade:
                p.startNewSubPath (3.0f, 21.0f);
                p.lineTo (21.0f, 21.0f);
                break;
            case Tool::gain:
                p.startNewSubPath (12.0f, 3.0f);
                p.lineTo (12.0f, 21.0f);
                p.startNewSubPath (8.5f, 6.0f);  p.lineTo (12.0f, 2.5f);  p.lineTo (15.5f, 6.0f);
                p.startNewSubPath (8.5f, 18.0f); p.lineTo (12.0f, 21.5f); p.lineTo (15.5f, 18.0f);
                break;
            case Tool::select:
            {
                // A dashed box with a small arrow in its corner
                const float d = 2.6f;
                for (float x = 2.5f; x < 17.5f; x += d * 2.0f) { p.startNewSubPath (x, 3.0f); p.lineTo (juce::jmin (17.5f, x + d), 3.0f); }
                for (float x = 2.5f; x < 12.0f; x += d * 2.0f) { p.startNewSubPath (x, 17.5f); p.lineTo (juce::jmin (12.0f, x + d), 17.5f); }
                for (float y = 3.0f; y < 17.5f; y += d * 2.0f) { p.startNewSubPath (2.5f, y); p.lineTo (2.5f, juce::jmin (17.5f, y + d)); }
                for (float y = 3.0f; y < 12.0f; y += d * 2.0f) { p.startNewSubPath (17.5f, y); p.lineTo (17.5f, juce::jmin (12.0f, y + d)); }
                p.startNewSubPath (14.0f, 13.0f);
                p.lineTo (14.0f, 22.0f);
                p.lineTo (16.6f, 19.6f);
                p.lineTo (18.3f, 23.0f);
                p.startNewSubPath (16.6f, 19.6f);
                p.lineTo (20.5f, 19.6f);
                p.closeSubPath();
                break;
            }
            case Tool::stretch:
                p.startNewSubPath (2.0f, 12.0f); p.lineTo (6.0f, 12.0f);
                p.startNewSubPath (4.5f, 9.0f);  p.lineTo (1.5f, 12.0f); p.lineTo (4.5f, 15.0f);
                p.startNewSubPath (18.0f, 12.0f); p.lineTo (22.0f, 12.0f);
                p.startNewSubPath (19.5f, 9.0f); p.lineTo (22.5f, 12.0f); p.lineTo (19.5f, 15.0f);
                p.startNewSubPath (7.0f, 5.0f); p.lineTo (7.0f, 19.0f);
                p.startNewSubPath (17.0f, 5.0f); p.lineTo (17.0f, 19.0f);
                break;
            case Tool::pointer: case Tool::pencil: case Tool::eraser: case Tool::glue: case Tool::text:
                break;
        }
        return p;
    }

    inline void draw (juce::Graphics& g, Tool tool, juce::Rectangle<float> area, juce::Colour colour)
    {
        const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()), juce::jmin (area.getWidth(), area.getHeight()));
        const auto t = juce::AffineTransform::scale (box.getWidth() / 24.0f).translated (box.getX(), box.getY());
        g.setColour (colour);
        auto f = filled (tool);
        f.applyTransform (t);
        g.fillPath (f);
        auto s = stroked (tool);
        s.applyTransform (t);
        g.strokePath (s, juce::PathStrokeType (juce::jmax (1.2f, box.getWidth() / 12.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** The icon as a Drawable (for menu items). */
    inline std::unique_ptr<juce::Drawable> drawable (Tool tool, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, 48, 48, true);
        {
            juce::Graphics g (image);
            draw (g, tool, { 4.0f, 4.0f, 40.0f, 40.0f }, colour);
        }
        auto d = std::make_unique<juce::DrawableImage>();
        d->setImage (image);
        return d;
    }

    inline const char* name (Tool tool)
    {
        switch (tool)
        {
            case Tool::pointer:  return "Pointer";
            case Tool::pencil:   return "Pencil";
            case Tool::eraser:   return "Eraser";
            case Tool::scissors: return "Scissors";
            case Tool::glue:     return "Glue";
            case Tool::mute:     return "Mute";
            case Tool::fade:     return "Fade";
            case Tool::marquee:  return "Marquee";
            case Tool::zoom:     return "Zoom";
            case Tool::solo:     return "Solo";
            case Tool::text:     return "Text";
            case Tool::gain:     return "Gain";
            case Tool::select:   return "Select";
            case Tool::stretch:  return "Stretch";
        }
        return "";
    }

    /** Logic's keys for the tools, pressed while the Tool menu (T) is open. */
    inline juce::juce_wchar key (Tool tool)
    {
        switch (tool)
        {
            case Tool::pointer:  return 't';
            case Tool::pencil:   return 'p';
            case Tool::eraser:   return 'e';
            case Tool::text:     return 'x';
            case Tool::scissors: return 'i';
            case Tool::glue:     return 'g';
            case Tool::solo:     return 's';
            case Tool::mute:     return 'm';
            case Tool::zoom:     return 'z';
            case Tool::fade:     return 'a';
            case Tool::marquee:  return 'r';
            case Tool::gain:     return 'n';
            case Tool::select:   return 'l';
            case Tool::stretch:  return 'w';
        }
        return 0;
    }

    /** What each tool does, for the menu. */
    inline const char* hint (Tool tool)
    {
        switch (tool)
        {
            case Tool::pointer:  return "select, move, resize";
            case Tool::pencil:   return "draw MIDI regions";
            case Tool::eraser:   return "delete regions";
            case Tool::scissors: return "split regions";
            case Tool::glue:     return "join regions";
            case Tool::mute:     return "mute regions";
            case Tool::fade:     return "drag a region's edge";
            case Tool::marquee:  return "select a stretch of time";
            case Tool::zoom:     return "drag to zoom in";
            case Tool::solo:     return "hear one region";
            case Tool::text:     return "rename";
            case Tool::gain:     return "drag a region up or down to change its level";
            case Tool::select:   return "drag a box round regions to select them (even starting on one)";
            case Tool::stretch:  return "drag a region's edge to stretch its audio (Option-drag with the Pointer too)";
        }
        return "";
    }

    /** The mouse pointer for a tool, as in Logic: the tool's icon, with its working point on the spot clicked. */
    inline juce::MouseCursor cursor (Tool tool)
    {
        switch (tool)
        {
            case Tool::pointer: return juce::MouseCursor::NormalCursor;
            case Tool::text:    return juce::MouseCursor::IBeamCursor;
            case Tool::marquee: return juce::MouseCursor::CrosshairCursor;
            case Tool::select:  return juce::MouseCursor::CrosshairCursor;
            case Tool::stretch: return juce::MouseCursor::LeftRightResizeCursor;
            default: break;
        }

        static std::map<int, juce::MouseCursor> cache;
        if (auto it = cache.find ((int) tool); it != cache.end())
            return it->second;

        // Drawn at 2x for sharp screens: a black outline round a white icon, so it shows on any colour.
        constexpr int size = 32, scale = 2;
        juce::Image image (juce::Image::ARGB, size * scale, size * scale, true);
        {
            juce::Graphics g (image);
            const auto area = juce::Rectangle<float> (2.0f, 2.0f, 28.0f, 28.0f) * (float) scale;
            for (const auto d : { juce::Point<float> (-2.0f, 0.0f), { 2.0f, 0.0f }, { 0.0f, -2.0f }, { 0.0f, 2.0f },
                                  { -1.5f, -1.5f }, { 1.5f, 1.5f }, { -1.5f, 1.5f }, { 1.5f, -1.5f } })
                draw (g, tool, area.translated (d.x, d.y), juce::Colours::black);
            draw (g, tool, area, juce::Colours::white);
        }

        // Where the tool works, on the 24 grid (the pencil's tip, the zoom glass's centre...).
        juce::Point<float> hot (12.0f, 12.0f);
        switch (tool)
        {
            case Tool::pencil:   hot = { 5.3f, 20.0f }; break;
            case Tool::eraser:   hot = { 5.0f, 18.5f }; break;
            case Tool::scissors: hot = { 12.0f, 9.0f }; break;
            case Tool::glue:     hot = { 20.0f, 19.0f }; break;
            case Tool::fade:     hot = { 3.0f, 19.0f }; break;
            case Tool::zoom:     hot = { 9.5f, 9.5f }; break;
            default: break;
        }
        const auto hx = juce::roundToInt (2.0f + hot.x * 28.0f / 24.0f), hy = juce::roundToInt (2.0f + hot.y * 28.0f / 24.0f);
        juce::ScaledImage scaled (image, (double) scale);
        return cache.emplace ((int) tool, juce::MouseCursor (scaled, { hx, hy })).first->second;
    }

    constexpr Tool all[] { Tool::pointer, Tool::pencil, Tool::eraser, Tool::text, Tool::scissors, Tool::glue,
                           Tool::solo, Tool::mute, Tool::zoom, Tool::fade, Tool::marquee, Tool::select, Tool::gain, Tool::stretch };
}
