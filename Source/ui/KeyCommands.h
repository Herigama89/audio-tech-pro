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
                      std::function<void (const juce::String&)> applyPreset);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label presetCaption { {}, "Preset" }, help;
    juce::ComboBox presetBox;
    juce::TextButton applyButton { "Use this preset" };
    juce::KeyMappingEditorComponent editor;
    std::function<void (const juce::String&)> onApplyPreset;
};

} // namespace riff
