#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The Key Commands window: pick a preset (a DAW's layout), then change any
    key. Click a command's "+" to add a key, or click a key to change or
    remove it. Changes are saved automatically.
*/
class KeyCommandsPanel final : public juce::Component
{
public:
    KeyCommandsPanel (juce::KeyPressMappingSet& mappings, const juce::String& currentPreset,
                      std::function<void (const juce::String&)> applyPreset,
                      bool stopReturnsToStart = false, std::function<void (bool)> onStopReturnsToStart = {});

    /** After a preset is applied, its workflow setting shows here. */
    void setStopReturnsToStart (bool on) { returnButton.setToggleState (on, juce::dontSendNotification); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label presetCaption { {}, "Preset" }, help;
    juce::ComboBox presetBox;
    juce::TextButton applyButton { "Use this preset" };
    juce::ToggleButton returnButton { "Stop returns to where playback started" };
    juce::KeyMappingEditorComponent editor;
    std::function<void (const juce::String&)> onApplyPreset;
};

} // namespace riff
