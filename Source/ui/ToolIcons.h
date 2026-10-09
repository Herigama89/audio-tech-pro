#pragma once

#include <JuceHeader.h>
#include "TimelineState.h"

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
            case Tool::zoom:
            case Tool::marquee:
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
        }
        return "";
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
        }
        return "";
    }

    constexpr Tool all[] { Tool::pointer, Tool::pencil, Tool::eraser, Tool::text, Tool::scissors, Tool::glue,
                           Tool::solo, Tool::mute, Tool::zoom, Tool::fade, Tool::marquee };
}
