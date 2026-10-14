#include "KeyCommands.h"
#include "Commands.h"
#include "Theme.h"

namespace riff
{

KeyCommandsPanel::KeyCommandsPanel (juce::KeyPressMappingSet& keyMappings, const juce::String& currentPreset,
                                    std::function<void (const juce::String&)> applyPreset,
                                    bool stopReturnsToStart, std::function<void (bool)> onStopReturnsToStart)
    : editor (keyMappings, true), onApplyPreset (std::move (applyPreset)), mappings (keyMappings)
{
    // Search across every command
    search.setTextToShowWhenEmpty ("Search commands (name, menu, or a key like cmd+t)", theme::dimText);
    search.onTextChange = [this] { refilter(); };
    search.onEscapeKey = [this] { search.clear(); refilter(); };
    addAndMakeVisible (search);
    results.setRowHeight (26);
    results.setColour (juce::ListBox::backgroundColourId, theme::panel);
    addChildComponent (results);
    addKey.setTooltip ("Press the new key for the selected command (it moves from any other command that had it)");
    addKey.onClick = [this] { captureKey(); };
    clearKeys.setTooltip ("Take every key off the selected command");
    clearKeys.onClick = [this]
    {
        const auto row = results.getSelectedRow();
        if (juce::isPositiveAndBelow (row, (int) found.size()))
            mappings.clearAllKeyPresses (found[(size_t) row]);
    };
    for (auto* b : { &addKey, &clearKeys })
        addChildComponent (b);
    mappings.addChangeListener (this);

    returnButton.setToggleState (stopReturnsToStart, juce::dontSendNotification);
    returnButton.setTooltip ("Like Pro Tools, Ableton Live and FL Studio: after you stop, the playhead jumps back to where you pressed play.\n"
                             "Off, like Logic and Cubase: it stays where you stopped. Each preset sets this; you can change it here.");
    returnButton.onClick = [this, cb = std::move (onStopReturnsToStart)]
    {
        if (cb)
            cb (returnButton.getToggleState());
    };
    addAndMakeVisible (returnButton);

    presetCaption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (presetCaption);

    presetBox.addItemList (commands::presetNames(), 1);
    presetBox.setText (currentPreset, juce::dontSendNotification);
    addAndMakeVisible (presetBox);

    applyButton.setTooltip ("Replace every key with the preset's keys (your own changes are lost)");
    applyButton.onClick = [this]
    {
        if (onApplyPreset != nullptr && presetBox.getText().isNotEmpty())
            onApplyPreset (presetBox.getText());
    };
    addAndMakeVisible (applyButton);

    help.setText ("Choose a DAW and click \"Use this preset\" for its keys. Click + to add a key to a command; "
                  "click a key to change or remove it. \"Reset to defaults\" goes back to the preset. Changes are saved as you make them.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (help);

    editor.setColours (theme::panel, theme::text);
    addAndMakeVisible (editor);

    setSize (620, 680);
}

KeyCommandsPanel::~KeyCommandsPanel()
{
    mappings.removeChangeListener (this);
}

void KeyCommandsPanel::refilter()
{
    const auto text = search.getText().trim();
    const bool searching = text.isNotEmpty();
    results.setVisible (searching);
    addKey.setVisible (searching);
    clearKeys.setVisible (searching);
    editor.setVisible (! searching);
    found.clear();
    if (searching)
    {
        const auto words = juce::StringArray::fromTokens (text, " ", "");
        auto& manager = mappings.getCommandManager();
        for (const auto& info : commands::all())
        {
            juce::String keys;
            for (const auto& k : mappings.getKeyPressesAssignedToCommand (info.id))
                keys << k.getTextDescriptionWithIcons() << " " << k.getTextDescription() << " ";
            const auto haystack = juce::String (info.name) + " " + info.category + " " + info.description + " " + keys;
            bool all = true;
            for (const auto& w : words)
                all = all && haystack.containsIgnoreCase (w);
            if (all && manager.getCommandForID (info.id) != nullptr)
                found.push_back (info.id);
        }
    }
    results.updateContent();
    results.repaint();
    if (! found.empty() && results.getSelectedRow() < 0)
        results.selectRow (0);
    updateButtons();
}

void KeyCommandsPanel::updateButtons()
{
    const auto ok = juce::isPositiveAndBelow (results.getSelectedRow(), (int) found.size());
    addKey.setEnabled (ok);
    clearKeys.setEnabled (ok);
}

void KeyCommandsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) found.size()))
        return;
    const auto* info = commands::find (found[(size_t) row]);
    if (info == nullptr)
        return;
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.8f));
        g.fillRect (0, 0, width, height);
    }
    g.setColour (selected ? juce::Colours::white : theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (info->name, 8, 0, width / 2, height, juce::Justification::centredLeft, true);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (info->category, width / 2 - 70, 0, 70, height, juce::Justification::centredLeft, true);
    juce::StringArray keys;
    for (const auto& k : mappings.getKeyPressesAssignedToCommand (info->id))
        keys.add (k.getTextDescriptionWithIcons());
    g.setColour (selected ? juce::Colours::white : theme::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (keys.isEmpty() ? juce::String ("(no key)") : keys.joinIntoString ("   "), width / 2 + 10, 0, width / 2 - 18, height,
                juce::Justification::centredRight, true);
}

void KeyCommandsPanel::captureKey()
{
    const auto row = results.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) found.size()))
        return;
    const auto command = found[(size_t) row];

    // A little window that waits for the key.
    struct Catcher final : juce::Component
    {
        std::function<void (const juce::KeyPress&)> onKey;
        Catcher() { setWantsKeyboardFocus (true); setSize (320, 90); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText ("Press the new key (with Cmd, Shift, Option... as you like).\nEscape to cancel.",
                              getLocalBounds().reduced (12), juce::Justification::centred, 3);
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            auto cb = onKey;
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
            if (k != juce::KeyPress (juce::KeyPress::escapeKey) && cb)
                cb (k);
            return true;
        }
        void visibilityChanged() override { if (isShowing()) grabKeyboardFocus(); }
    };
    auto catcher = std::make_unique<Catcher>();
    catcher->onKey = [this, command] (const juce::KeyPress& key)
    {
        // The key leaves any other command that had it.
        if (const auto other = mappings.findCommandForKeyPress (key); other != 0 && other != command)
            mappings.removeKeyPress (key);
        mappings.addKeyPress (command, key);
    };
    juce::CallOutBox::launchAsynchronously (std::move (catcher), addKey.getScreenBounds(), nullptr);
}

void KeyCommandsPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void KeyCommandsPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (28);
    presetCaption.setBounds (top.removeFromLeft (56));
    presetBox.setBounds (top.removeFromLeft (200));
    top.removeFromLeft (8);
    applyButton.setBounds (top.removeFromLeft (140));

    area.removeFromTop (6);
    returnButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (2);
    help.setBounds (area.removeFromTop (36));
    area.removeFromTop (6);
    search.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    auto buttons = area.removeFromBottom (28);
    clearKeys.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (6);
    addKey.setBounds (buttons.removeFromRight (110));
    if (results.isVisible())
        area.removeFromBottom (6);
    else
        area = area.withBottom (buttons.getBottom());
    editor.setBounds (area);
    results.setBounds (area);
}

} // namespace riff
