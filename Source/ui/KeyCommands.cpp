#include "KeyCommands.h"
#include "Commands.h"
#include "Theme.h"

namespace riff
{

KeyCommandsPanel::KeyCommandsPanel (juce::KeyPressMappingSet& mappings, const juce::String& currentPreset,
                                    std::function<void (const juce::String&)> applyPreset,
                                    bool stopReturnsToStart, std::function<void (bool)> onStopReturnsToStart)
    : editor (mappings, true), onApplyPreset (std::move (applyPreset))
{
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
    editor.setBounds (area);
}

} // namespace riff
