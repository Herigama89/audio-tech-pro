#include "PluginPicker.h"
#include "Theme.h"

namespace riff
{

PluginPicker::PluginPicker (std::vector<Entry> e, std::function<void()> scan, std::function<void()> remove,
                            Settings s, std::function<void (const Settings&)> onChanged)
    : entries (std::move (e)), settings (std::move (s)), onSettingsChanged (std::move (onChanged)),
      onScan (std::move (scan)), onRemove (std::move (remove))
{
    for (auto& entry : entries)
    {
        if (entry.key.isEmpty())
            entry.key = entry.format + "|" + entry.maker + "|" + entry.name;
        if (entry.category.isEmpty())
            entry.category = categoryOf ({}, entry.name);
    }

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

    // How the list is grouped
    int index = 0;
    for (const auto* name : { "Favorites", "Maker", "Category", "Format" })
    {
        auto* b = viewButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->onClick = [this, v = (View) index] { setView (v); };
        addAndMakeVisible (b);
        ++index;
    }

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

    setView (settings.view == View::favorites && settings.favorites.isEmpty() ? View::maker : settings.view);
    setSize (360, 460);
}

juce::String PluginPicker::categoryOf (const juce::String& pluginCategory, const juce::String& name)
{
    const auto text = (pluginCategory + " " + name).toLowerCase();
    struct Rule { const char* category; std::initializer_list<const char*> words; };
    const Rule rules[] {
        { "Dynamics",   { "dynamics", "compress", "limit", "gate", "expander", "de-ess", "deess", "1176", "la-2a", "la2a", "transient", "leveler" } },
        { "EQ",         { "eq", "equaliz", "pro-q", "pultec", "filter bank" } },
        { "Reverb",     { "reverb", "verb", "room", "hall", "plate", "spring" } },
        { "Delay",      { "delay", "echo", "tape echo" } },
        { "Modulation", { "chorus", "flang", "phase", "tremolo", "vibrato", "rotary", "modulat" } },
        { "Distortion", { "distort", "saturat", "overdrive", "fuzz", "tape", "amp", "drive", "crush" } },
        { "Filter",     { "filter", "wah" } },
        { "Pitch",      { "pitch", "tune", "harmon" } },
        { "Analyzer",   { "analy", "meter", "scope", "spectrum", "tuner" } },
        { "Imaging",    { "stereo", "imager", "width" } },
    };
    for (const auto& rule : rules)
        for (const auto* w : rule.words)
            if (text.contains (w))
                return rule.category;
    return "Other";
}

std::vector<int> PluginPicker::filter (const std::vector<Entry>& entries, const juce::String& text)
{
    const auto words = juce::StringArray::fromTokens (text.trim(), " ", "");
    std::vector<int> result;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        const auto haystack = e.name + " " + e.maker + " " + e.format + " " + e.category;
        bool all = true;
        for (const auto& w : words)
            if (w.isNotEmpty() && ! haystack.containsIgnoreCase (w))
                all = false;
        if (all)
            result.push_back (i);
    }
    return result;
}

juce::String PluginPicker::groupOf (const Entry& e) const
{
    switch (settings.view)
    {
        case View::category: return e.category;
        case View::format:   return e.format.isEmpty() ? juce::String ("Built in") : e.format;
        case View::favorites:
        case View::maker:    break;
    }
    return e.format.isEmpty() ? e.maker + " (built in)" : e.maker;
}

void PluginPicker::setView (View v)
{
    settings.view = v;
    for (int i = 0; i < viewButtons.size(); ++i)
        viewButtons[i]->setToggleState (i == (int) v, juce::dontSendNotification);
    if (onSettingsChanged)
        onSettingsChanged (settings);
    refilter();
}

void PluginPicker::refilter()
{
    shown = filter (entries, search.getText());
    if (settings.view == View::favorites)
        shown.erase (std::remove_if (shown.begin(), shown.end(), [this] (int i) { return ! isFavorite (entries[(size_t) i]); }), shown.end());

    // Grouped (built-ins first), then by name; names that start with what was typed come first in their group.
    const auto first = juce::StringArray::fromTokens (search.getText().trim(), " ", "")[0];
    std::stable_sort (shown.begin(), shown.end(), [&] (int a, int b)
    {
        const auto& ea = entries[(size_t) a];
        const auto& eb = entries[(size_t) b];
        if ((settings.view == View::maker || settings.view == View::favorites) && ea.format.isEmpty() != eb.format.isEmpty())
            return ea.format.isEmpty();
        if (const auto c = groupOf (ea).compareIgnoreCase (groupOf (eb)); c != 0)
            return c < 0;
        if (first.isNotEmpty())
        {
            const bool sa = ea.name.startsWithIgnoreCase (first), sb = eb.name.startsWithIgnoreCase (first);
            if (sa != sb)
                return sa;
        }
        return ea.name.compareIgnoreCase (eb.name) < 0;
    });

    rows.clear();
    juce::String group;
    for (auto i : shown)
    {
        const auto g = groupOf (entries[(size_t) i]);
        if (g != group || rows.empty())
        {
            rows.push_back ({ -1, g });
            group = g;
        }
        rows.push_back ({ i, {} });
    }
    list.updateContent();
    list.deselectAllRows();
    selectEntryRow (0, 1);
    list.repaint();
    repaint();
}

void PluginPicker::selectEntryRow (int from, int direction)
{
    for (int r = from; juce::isPositiveAndBelow (r, (int) rows.size()); r += direction)
        if (rows[(size_t) r].entry >= 0)
        {
            list.selectRow (r);
            return;
        }
}

void PluginPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;
    if (rows[(size_t) row].entry < 0)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (rows[(size_t) row].header.toUpperCase(), 8, 0, width - 16, height, juce::Justification::bottomLeft, true);
        g.setColour (theme::outline);
        g.fillRect (8, height - 1, width - 16, 1);
        return;
    }
    const auto& e = entries[(size_t) rows[(size_t) row].entry];
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, (float) width - 4.0f, (float) height - 2.0f), 3.0f);
    }

    // The favorite star (click it)
    {
        juce::Path star;
        star.addStar ({ 13.0f, (float) height * 0.5f }, 5, 2.6f, 6.2f);
        if (isFavorite (e))
        {
            g.setColour (juce::Colour (0xfff2c94e));
            g.fillPath (star);
        }
        else
        {
            g.setColour ((selected ? juce::Colours::white : theme::dimText).withAlpha (0.55f));
            g.strokePath (star, juce::PathStrokeType (1.0f));
        }
    }

    g.setColour (selected ? juce::Colours::white : theme::text);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (e.name, 26, 0, width - 140, height, juce::Justification::centredLeft, true);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    const auto detail = settings.view == View::maker ? e.format
                      : settings.view == View::format ? e.maker
                      : (e.format.isEmpty() ? e.maker : e.maker + "  " + e.format);
    g.drawText (detail, width - 112, 0, 106, height, juce::Justification::centredRight, true);
}

void PluginPicker::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].entry < 0)
        return;

    // The star: favorite or not (the list stays open).
    if (e.x < 24)
    {
        const auto key = entries[(size_t) rows[(size_t) row].entry].key;
        if (settings.favorites.contains (key))
            settings.favorites.removeString (key);
        else
            settings.favorites.add (key);
        if (onSettingsChanged)
            onSettingsChanged (settings);
        if (settings.view == View::favorites)
            refilter();
        list.repaintRow (row);
        return;
    }
    choose (row);
}

bool PluginPicker::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto row = list.getSelectedRow();
    if (key == juce::KeyPress::downKey)
    {
        selectEntryRow (row + 1, 1);
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        selectEntryRow (row - 1, -1);
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
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].entry < 0)
        return;
    auto action = entries[(size_t) rows[(size_t) row].entry].choose;
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
    if (rows.empty())
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (13.0f));
        const auto message = settings.view == View::favorites && search.isEmpty()
                                 ? "No favorites yet: click the star beside a plugin."
                                 : entries.empty() ? "No plugins found yet." : "Nothing matches.";
        g.drawFittedText (message, list.getBounds().reduced (20, 0), juce::Justification::centred, 3);
    }
}

void PluginPicker::resized()
{
    auto r = getLocalBounds().reduced (8);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    auto tabs = r.removeFromTop (24);
    const auto w = tabs.getWidth() / juce::jmax (1, viewButtons.size());
    for (auto* b : viewButtons)
        b->setBounds (tabs.removeFromLeft (w).reduced (1, 0));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (26);
    scanButton.setBounds (bottom.removeFromLeft (150));
    if (removeButton.isVisible())
        removeButton.setBounds (bottom.removeFromRight (90));
    r.removeFromBottom (6);
    list.setBounds (r);
}

} // namespace riff
