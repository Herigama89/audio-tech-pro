#include "KeyCommands.h"
#include "Commands.h"
#include "Theme.h"

namespace riff
{

KeyCommandsPanel::KeyCommandsPanel (juce::KeyPressMappingSet& mappings, const juce::String& currentPreset,
                                    std::function<void (const juce::String&)> applyPreset)
    : editor (mappings, true), onApplyPreset (std::move (applyPreset))
{
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

    help.setText ("Click + to add a key to a command; click a key to change or remove it. "
                  "\"Reset to defaults\" goes back to the preset. Changes are saved as you make them.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (help);

    editor.setColours (theme::panel, theme::text);
    addAndMakeVisible (editor);

    setSize (620, 640);
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
    help.setBounds (area.removeFromTop (36));
    area.removeFromTop (6);
    editor.setBounds (area);
}

} // namespace riff
