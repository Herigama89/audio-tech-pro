#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The Key Commands window: pick a preset (a DAW's layout), then change any
    key. Click a command's "+" to add a key, or click a key to change or
    remove it. Changes are saved automatically.
*/
class KeyCommandsPanel final : public juce::Component,
                               private juce::ListBoxModel,
                               private juce::ChangeListener
{
public:
    KeyCommandsPanel (juce::KeyPressMappingSet& mappings, const juce::String& currentPreset,
                      std::function<void (const juce::String&)> applyPreset,
                      bool stopReturnsToStart = false, std::function<void (bool)> onStopReturnsToStart = {});

    /** After a preset is applied, its workflow setting shows here. */
    void setStopReturnsToStart (bool on) { returnButton.setToggleState (on, juce::dontSendNotification); }

    ~KeyCommandsPanel() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    friend class SnapshotTool;
    juce::Label presetCaption { {}, "Preset" }, help;
    juce::ComboBox presetBox;
    juce::TextButton applyButton { "Use this preset" };
    juce::ToggleButton returnButton { "Stop returns to where playback started" };
    juce::KeyMappingEditorComponent editor;
    std::function<void (const juce::String&)> onApplyPreset;

    // Search: type part of a command's name, its menu, what it does, or a key ("cmd+t")
    juce::KeyPressMappingSet& mappings;
    juce::TextEditor search;
    juce::ListBox results { "Commands", this };
    juce::TextButton addKey { "Add Key..." }, clearKeys { "Remove Keys" };
    std::vector<juce::CommandID> found;
    void refilter();
    int getNumRows() override { return (int) found.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { updateButtons(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refilter(); }
    void updateButtons();
    void captureKey();
};

} // namespace riff
