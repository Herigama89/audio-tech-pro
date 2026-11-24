#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{
/**
    The look of the New Project and Delete Unused Audio windows (Liquid Glass): flat frosted keys with a hairline
    edge, the accent for the lit and the main ones; fields a shade lighter than the window; a plain chevron on menus.
    A button's "primary" property (true) makes it the accent key; "list" (true) a row in a side list (left-aligned).
*/
class GlassDialogLook final : public juce::LookAndFeel_V4
{
public:
    static juce::Colour line()     { return theme::text.withAlpha (0.11f); }
    static juce::Colour fill()     { return theme::text.withAlpha (0.05f); }
    static juce::Colour onAccent() { return theme::accent.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff1a1206) : juce::Colours::white; }

    GlassDialogLook()
    {
        setColour (juce::PopupMenu::backgroundColourId, theme::panel);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, onAccent());
        setColour (juce::ScrollBar::thumbColourId, theme::text.withAlpha (0.25f));
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, theme::text);
        setColour (juce::TooltipWindow::backgroundColourId, theme::panel);
        setColour (juce::TooltipWindow::textColourId, theme::text);
    }

    static bool isPrimary (const juce::Component& c) { return (bool) c.getProperties().getWithDefault ("primary", false); }
    static bool isList (const juce::Component& c)    { return (bool) c.getProperties().getWithDefault ("list", false); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = isPrimary (b) || b.getToggleState();
        if (isList (b))
        {
            if (lit || over)
            {
                g.setColour (lit ? fill().withMultipliedAlpha (1.6f) : fill());
                g.fillRoundedRectangle (r, 8.0f);
                if (lit)
                {
                    g.setColour (line());
                    g.drawRoundedRectangle (r, 8.0f, 1.0f);
                }
            }
            return;
        }
        auto colour = lit ? theme::accent : fill();
        if (! b.isEnabled())
            colour = fill().withMultipliedAlpha (0.6f);
        else if (down)
            colour = lit ? colour.darker (0.15f) : theme::text.withAlpha (0.12f);
        else if (over)
            colour = lit ? colour.brighter (0.08f) : theme::text.withAlpha (0.08f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 8.0f);
        if (! lit)
        {
            g.setColour (line());
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool lit = (isPrimary (b) || b.getToggleState()) && ! isList (b) && b.isEnabled();
        auto colour = b.findColour (juce::TextButton::textColourOffId);
        if (! b.getProperties().contains ("ownText"))
            colour = lit ? onAccent() : (b.isEnabled() ? theme::text : theme::dimText);
        g.setColour (colour);
        g.setFont (juce::FontOptions (13.0f, lit && isPrimary (b) ? juce::Font::bold : juce::Font::plain));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (isList (b) ? 10 : 6, 0),
                    isList (b) ? juce::Justification::centredLeft : juce::Justification::centred, true);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (theme::text.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (line());
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        juce::Path chevron;
        const auto cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (theme::dimText);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (2, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, theme::text);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        g.setColour (theme::text.withAlpha (0.06f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), 7.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.hasKeyboardFocus (true) ? theme::accent.withAlpha (0.7f) : line());
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), 7.0f, 1.0f);
    }

    void drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool, bool, bool) override
    {
        const auto box = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
        g.setColour (ticked ? theme::accent : theme::text.withAlpha (0.06f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (ticked ? theme::accent : line());
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        if (ticked)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + box.getWidth() * 0.25f, box.getCentreY());
            tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.27f);
            tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.27f);
            g.setColour (onAccent());
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (theme::panel);
        g.setColour (line());
        g.drawRect (0, 0, w, h);
    }
};
}
