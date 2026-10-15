#pragma once

#include <JuceHeader.h>
#include "ToolIcons.h"
#include "Theme.h"

namespace riff
{

/**
    Logic's Tool menu (T): every tool with its icon and its key. Press the key
    (T Pointer, P Pencil, E Eraser, X Text, I Scissors, G Glue, S Solo, M Mute,
    Z Zoom, A Fade, R Marquee, N Gain), or click one.
*/
class ToolChooser final : public juce::Component
{
public:
    ToolChooser (TimelineState::Tool current, std::function<void (TimelineState::Tool)> choose)
        : selected (current), onChoose (std::move (choose))
    {
        setWantsKeyboardFocus (true);
        setSize (230, (int) std::size (toolicons::all) * rowHeight + 8);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        int y = 4;
        for (const auto tool : toolicons::all)
        {
            const juce::Rectangle<int> row (4, y, getWidth() - 8, rowHeight);
            if (tool == hovered || tool == selected)
            {
                g.setColour (tool == hovered ? theme::accent.withAlpha (0.35f) : theme::selection);
                g.fillRoundedRectangle (row.toFloat(), 3.0f);
            }
            toolicons::draw (g, tool, row.toFloat().removeFromLeft (rowHeight).reduced (4.0f), theme::text);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (toolicons::name (tool), row.withTrimmedLeft (rowHeight + 4), juce::Justification::centredLeft, false);
            g.setColour (theme::dimText);
            g.drawText (juce::String::charToString (toolicons::key (tool)).toUpperCase(), row.withTrimmedRight (6), juce::Justification::centredRight, false);
            y += rowHeight;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hovered = toolAt (e.y); repaint(); }
    void mouseUp (const juce::MouseEvent& e) override   { if (auto t = toolAt (e.y); t.has_value()) choose (*t); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        for (const auto tool : toolicons::all)
            if (toolicons::key (tool) == c)
            {
                choose (tool);
                return true;
            }
        if (key == juce::KeyPress::escapeKey)
        {
            dismiss();
            return true;
        }
        return false;
    }

    void visibilityChanged() override
    {
        if (isShowing())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ToolChooser> (this)]
            {
                if (safe != nullptr)
                    safe->grabKeyboardFocus();
            });
    }

private:
    static constexpr int rowHeight = 24;
    TimelineState::Tool selected;
    std::optional<TimelineState::Tool> hovered;
    std::function<void (TimelineState::Tool)> onChoose;

    std::optional<TimelineState::Tool> toolAt (int y) const
    {
        const auto i = (y - 4) / rowHeight;
        if (y < 4 || i < 0 || i >= (int) std::size (toolicons::all))
            return {};
        return toolicons::all[(size_t) i];
    }

    void choose (TimelineState::Tool tool)
    {
        if (onChoose)
            onChoose (tool);
        dismiss();
    }

    void dismiss()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }
};

} // namespace riff
