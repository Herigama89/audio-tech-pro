#include "PluginPicker.h"
#include "Theme.h"

namespace riff
{

PluginPicker::PluginPicker (std::vector<Entry> e, std::function<void()> scan, std::function<void()> remove)
    : entries (std::move (e)), onScan (std::move (scan)), onRemove (std::move (remove))
{
    // Built-ins first, then by maker and name.
    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.format.isEmpty() != b.format.isEmpty())
            return a.format.isEmpty();
        if (a.maker.compareIgnoreCase (b.maker) != 0)
            return a.maker.compareIgnoreCase (b.maker) < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    search.setTextToShowWhenEmpty ("Search plugins", theme::dimText);
    search.setColour (juce::TextEditor::backgroundColourId, theme::toolbarDark);
    search.setColour (juce::TextEditor::outlineColourId, theme::outline);
    search.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    search.setColour (juce::TextEditor::textColourId, theme::text);
    search.setFont (juce::FontOptions (14.0f));
    search.setIndents (8, 6);
    search.addListener (this);
    search.addKeyListener (this);
    addAndMakeVisible (search);

    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    list.setOutlineThickness (0);
    list.addKeyListener (this);
    addAndMakeVisible (list);

    for (auto* b : { &scanButton, &removeButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    scanButton.onClick = [this] { auto f = onScan; close(); if (f) f(); };
    removeButton.onClick = [this] { auto f = onRemove; close(); if (f) f(); };
    removeButton.setVisible (onRemove != nullptr);

    refilter();
    setSize (340, 420);
}

std::vector<int> PluginPicker::filter (const std::vector<Entry>& entries, const juce::String& text)
{
    const auto words = juce::StringArray::fromTokens (text.trim(), " ", "");
    std::vector<int> result;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        const auto haystack = e.name + " " + e.maker + " " + e.format;
        bool all = true;
        for (const auto& w : words)
            if (w.isNotEmpty() && ! haystack.containsIgnoreCase (w))
                all = false;
        if (all)
            result.push_back (i);
    }

    // Names that start with what was typed come first.
    if (words.size() > 0)
        std::stable_sort (result.begin(), result.end(), [&] (int a, int b)
        {
            const bool sa = entries[(size_t) a].name.startsWithIgnoreCase (words[0]);
            const bool sb = entries[(size_t) b].name.startsWithIgnoreCase (words[0]);
            return sa && ! sb;
        });
    return result;
}

void PluginPicker::refilter()
{
    shown = filter (entries, search.getText());
    list.updateContent();
    list.selectRow (shown.empty() ? -1 : 0);
    list.repaint();
}

void PluginPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    const auto& e = entries[(size_t) shown[(size_t) row]];
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, (float) width - 4.0f, (float) height - 2.0f), 3.0f);
    }
    g.setColour (selected ? juce::Colours::white : theme::text);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (e.name, 10, 0, width - 120, height, juce::Justification::centredLeft, true);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (e.format.isEmpty() ? juce::String ("Built-in") : e.maker + "  " + e.format, width - 116, 0, 110, height,
                juce::Justification::centredRight, true);
}

void PluginPicker::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    choose (row);
}

bool PluginPicker::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto row = list.getSelectedRow();
    if (key == juce::KeyPress::downKey)
    {
        list.selectRow (juce::jmin ((int) shown.size() - 1, row + 1));
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        list.selectRow (juce::jmax (0, row - 1));
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        choose (row);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void PluginPicker::choose (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    auto action = entries[(size_t) shown[(size_t) row]].choose;
    close();
    if (action)
        action();
}

void PluginPicker::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void PluginPicker::visibilityChanged()
{
    if (isShowing())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::TextEditor> (&search)]
        {
            if (safe != nullptr)
                safe->grabKeyboardFocus();
        });
}

void PluginPicker::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (shown.empty())
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (entries.empty() ? "No plugins found yet." : "Nothing matches.", list.getBounds(), juce::Justification::centred);
    }
}

void PluginPicker::resized()
{
    auto r = getLocalBounds().reduced (8);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (26);
    scanButton.setBounds (bottom.removeFromLeft (150));
    if (removeButton.isVisible())
        removeButton.setBounds (bottom.removeFromRight (90));
    r.removeFromBottom (6);
    list.setBounds (r);
}

} // namespace riff
