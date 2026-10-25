#pragma once

#include <JuceHeader.h>
#include "ToolIcons.h"
#include "Theme.h"

/**
    The piano roll's tools in FL Studio's order, with their icons and keys:
    P Draw, B Paint, D Delete, T Mute, C Slice, E Select, Z Zoom.
    Shown as icon buttons in the piano roll's toolbar (the one in use lit) and in the
    tool menu that T opens.
*/
namespace riff::notetoolicons
{
    constexpr int count = 7;   // in the order of PianoRoll::Tool

    inline const char* name (int i)
    {
        static const char* names[] { "Draw", "Paint", "Delete", "Mute", "Slice", "Select", "Zoom" };
        return names[juce::jlimit (0, count - 1, i)];
    }

    inline juce::juce_wchar key (int i)
    {
        static const juce::juce_wchar keys[] { 'p', 'b', 'd', 't', 'c', 'e', 'z' };
        return keys[juce::jlimit (0, count - 1, i)];
    }

    inline const char* hint (int i)
    {
        static const char* hints[] { "click to add a note, drag to set its length", "drag to paint notes along the grid",
                                     "click or drag over notes to delete them", "click or drag over notes to mute them",
                                     "drag down across notes to cut them", "drag a box round notes to select them",
                                     "click to zoom in, Option-click out" };
        return hints[juce::jlimit (0, count - 1, i)];
    }

    /** The Edit window icon that matches (Draw is the pencil, Delete the eraser...), or none for Paint and Slice. */
    inline std::optional<toolicons::Tool> shared (int i)
    {
        switch (i)
        {
            case 0: return toolicons::Tool::pencil;
            case 2: return toolicons::Tool::eraser;
            case 3: return toolicons::Tool::mute;
            case 6: return toolicons::Tool::zoom;
            default: return {};
        }
    }

    inline void draw (juce::Graphics& g, int i, juce::Rectangle<float> area, juce::Colour colour)
    {
        if (const auto t = shared (i))
        {
            toolicons::draw (g, *t, area, colour);
            return;
        }

        const auto side = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = area.withSizeKeepingCentre (side, side);
        const auto t = juce::AffineTransform::scale (side / 24.0f).translated (box.getX(), box.getY());
        juce::Path filled, stroked;
        if (i == 1)
        {
            // Paint: a brush - handle, ferrule, and the bristles with a stroke of paint
            juce::Path brush;
            brush.addRoundedRectangle (-1.6f, -11.0f, 3.2f, 11.0f, 1.4f);
            brush.addRectangle (-2.6f, 0.4f, 5.2f, 2.6f);
            brush.startNewSubPath (-2.8f, 3.4f);
            brush.lineTo (2.8f, 3.4f);
            brush.quadraticTo (3.2f, 8.0f, 0.0f, 10.5f);
            brush.quadraticTo (-3.2f, 8.0f, -2.8f, 3.4f);
            brush.closeSubPath();
            brush.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (35.0f)).translated (13.5f, 10.5f));
            filled = brush;
            stroked.startNewSubPath (3.0f, 21.0f);
            stroked.quadraticTo (6.0f, 18.5f, 9.5f, 20.5f);
        }
        else if (i == 4)
        {
            // Slice: a knife, the blade cutting down to the left
            juce::Path knife;
            knife.addRoundedRectangle (-1.8f, -11.5f, 3.6f, 8.5f, 1.4f);   // handle
            knife.startNewSubPath (-1.8f, -2.0f);                          // a wide blade: straight back, curved edge to the point
            knife.lineTo (1.8f, -2.0f);
            knife.lineTo (1.8f, 11.5f);
            knife.quadraticTo (-5.5f, 6.0f, -5.0f, -2.0f);
            knife.closeSubPath();
            knife.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (40.0f)).translated (12.5f, 11.0f));
            filled = knife;
        }
        else
        {
            // Select: a dashed box with the pointer at its corner
            juce::Path box;
            box.addRectangle (2.5f, 2.5f, 15.0f, 12.0f);
            const float dashes[] { 2.6f, 2.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (filled, box, dashes, 2);
            filled.startNewSubPath (13.5f, 10.5f);
            filled.lineTo (13.5f, 22.0f);
            filled.lineTo (16.3f, 19.4f);
            filled.lineTo (18.2f, 23.0f);
            filled.lineTo (20.0f, 22.2f);
            filled.lineTo (18.2f, 18.6f);
            filled.lineTo (21.8f, 18.4f);
            filled.closeSubPath();
        }
        filled.applyTransform (t);
        stroked.applyTransform (t);
        g.setColour (colour);
        g.fillPath (filled);
        g.strokePath (stroked, juce::PathStrokeType (juce::jmax (1.2f, side / 12.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** The icon as a Drawable (for menu items). */
    inline std::unique_ptr<juce::Drawable> drawable (int i, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, 48, 48, true);
        {
            juce::Graphics g (image);
            draw (g, i, { 4.0f, 4.0f, 40.0f, 40.0f }, colour);
        }
        auto d = std::make_unique<juce::DrawableImage>();
        d->setImage (image);
        return d;
    }

    /** The mouse pointer for the tool: the Edit window's tool pointer where there is one. */
    inline juce::MouseCursor cursor (int i)
    {
        if (i == 0) return juce::MouseCursor::NormalCursor;   // (Draw: also moves and resizes notes, so the plain arrow)
        if (const auto t = shared (i))
            return toolicons::cursor (*t);
        return i == 4 || i == 5 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor;
    }

    /** A toolbar button: the tool's icon, lit while it's the tool in use, its key in the corner. */
    class Button final : public juce::Button
    {
    public:
        explicit Button (int toolIndex) : juce::Button (name (toolIndex)), index (toolIndex) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            if (on || over || down)
            {
                g.setColour (on ? theme::accent : theme::text.withAlpha (down ? 0.25f : 0.12f));
                g.fillRoundedRectangle (r, 4.0f);
            }
            const auto ink = on ? theme::accent.contrasting (0.9f) : theme::text.withAlpha (over ? 1.0f : 0.8f);
            draw (g, index, r.reduced (r.getHeight() * 0.12f).translated (-1.5f, -1.0f), ink);

            g.setColour (ink.withAlpha (0.65f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (juce::String::charToString (key (index)).toUpperCase(), r.reduced (2.0f, 0.5f), juce::Justification::bottomRight, false);
        }

    private:
        int index;
    };

    /**
        The tool menu (T in the piano roll): every tool with its icon and key, the one in use lit.
        Press a tool's key (T again: Mute) or click it; the last row opens the Tools menu (Chop, Glue, Strum...).
    */
    class Chooser final : public juce::Component
    {
    public:
        Chooser (int current, std::function<void (int)> choose, std::function<void()> moreTools)
            : selected (current), onChoose (std::move (choose)), onMore (std::move (moreTools))
        {
            setWantsKeyboardFocus (true);
            setSize (250, (count + 1) * rowHeight + 14);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel);
            for (int i = 0; i <= count; ++i)
            {
                const auto row = rowBounds (i);
                if (i == hovered || i == selected)
                {
                    g.setColour (i == hovered ? theme::accent.withAlpha (0.35f) : theme::selection);
                    g.fillRoundedRectangle (row.toFloat(), 3.0f);
                }
                g.setFont (juce::FontOptions (13.0f));
                if (i == count)
                {
                    g.setColour (theme::dimText.withAlpha (0.4f));
                    g.fillRect (6, row.getY() - 4, getWidth() - 12, 1);
                    g.setColour (theme::text);
                    g.drawText ("Tools menu (Chop, Glue, Strum...)", row.withTrimmedLeft (rowHeight + 4), juce::Justification::centredLeft, false);
                    continue;
                }
                draw (g, i, row.toFloat().removeFromLeft ((float) rowHeight).reduced (4.0f), theme::text);
                g.setColour (theme::text);
                g.drawText (name (i), row.withTrimmedLeft (rowHeight + 4), juce::Justification::centredLeft, false);
                g.setColour (theme::dimText);
                g.drawText (juce::String::charToString (key (i)).toUpperCase(), row.withTrimmedRight (6), juce::Justification::centredRight, false);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { hovered = rowAt (e.y); repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { hovered = -1; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (const auto i = rowAt (e.y); i == count)
                more();
            else if (i >= 0)
                choose (i);
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
            for (int i = 0; i < count; ++i)
                if (key (i) == c)
                {
                    choose (i);
                    return true;
                }
            if (k == juce::KeyPress::returnKey && hovered >= 0)
            {
                hovered == count ? more() : choose (hovered);
                return true;
            }
            if (k == juce::KeyPress::escapeKey)
            {
                dismiss();
                return true;
            }
            return false;
        }

        void visibilityChanged() override
        {
            if (isShowing())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Chooser> (this)]
                {
                    if (safe != nullptr)
                        safe->grabKeyboardFocus();
                });
        }

    private:
        static constexpr int rowHeight = 24;
        int selected, hovered = -1;
        std::function<void (int)> onChoose;
        std::function<void()> onMore;

        juce::Rectangle<int> rowBounds (int i) const
        {
            return { 4, 4 + i * rowHeight + (i == count ? 6 : 0), getWidth() - 8, rowHeight };
        }

        int rowAt (int y) const
        {
            for (int i = 0; i <= count; ++i)
                if (rowBounds (i).contains (8, y))
                    return i;
            return -1;
        }

        void choose (int i)
        {
            auto f = onChoose;
            dismiss();
            if (f) f (i);
        }

        void more()
        {
            auto f = onMore;
            dismiss();
            if (f) juce::MessageManager::callAsync (f);   // (after the box has gone)
        }

        void dismiss()
        {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        }
    };
}
