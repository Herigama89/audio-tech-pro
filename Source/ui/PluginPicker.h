#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The insert slot's plugin chooser, as in Logic and other DAWs: a search
    field at the top (type part of a name or a maker), the matches below,
    grouped by maker. Up/Down to move, Return to choose, Escape to close.
*/
class PluginPicker final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::TextEditor::Listener,
                           private juce::KeyListener
{
public:
    struct Entry
    {
        juce::String name, maker, format;   // format: "VST3", "AU", "CLAP", or empty for a built-in
        std::function<void()> choose;
    };

    PluginPicker (std::vector<Entry> entries, std::function<void()> onScan, std::function<void()> onRemove);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void visibilityChanged() override;

    /** The matches for a search: every word must be in the name, maker or format (any case). */
    static std::vector<int> filter (const std::vector<Entry>& entries, const juce::String& search);

private:
    friend class SnapshotTool;
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { choose (row); }
    void textEditorTextChanged (juce::TextEditor&) override { refilter(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override { choose (list.getSelectedRow()); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;

    void refilter();
    void choose (int row);
    void close();

    std::vector<Entry> entries;
    std::vector<int> shown;
    juce::TextEditor search;
    juce::ListBox list { "Plugins", this };
    juce::TextButton scanButton { "Scan for plugins..." }, removeButton { "Remove" };
    std::function<void()> onScan, onRemove;
};

} // namespace riff
