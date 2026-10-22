#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The keys along the top of an insert's window, as in Logic's plugin header:
      Pin        the window floats above the main window and stays open (Logic's link key's opposite)
      Bypass     the insert out of the signal (undoable)
      Side Chain which track's output keys it (a compressor ducking under the kick...)
*/
class InsertBar final : public juce::Component,
                        private juce::ValueTree::Listener
{
public:
    InsertBar (juce::ValueTree insert, juce::ValueTree project, juce::UndoManager& um, bool acceptsSidechain);
    ~InsertBar() override;

    void resized() override;

    static constexpr int preferredWidth = 560;

    /** The running plug-in's state (for a preset) and putting a preset's into it: the app connects these to its plug-ins. */
    static inline std::function<juce::String (const juce::ValueTree& insert)> captureState;
    static inline std::function<void (const juce::ValueTree& insert, const juce::String& state)> applyState;

    /** The side-chain menu: no side chain, then every other track and bus. */
    static void addSidechainItems (juce::PopupMenu& menu, juce::ValueTree insert, juce::ValueTree project, juce::UndoManager& um);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();
    void parentHierarchyChanged() override { refresh(); }

    juce::ValueTree insert, project;
    juce::UndoManager& undoManager;
    bool sidechainable;

    juce::TextButton pinButton { "Pin" }, bypassButton { "Bypass" }, printButton { "Print" }, sideButton;
    juce::TextButton presetButton;   // the preset: its name; the menu to save, load, set as Default
    juce::TextButton previousButton { "<" }, nextButton { ">" };
    void showPresetMenu();
    void stepPreset (int direction);
    void loadPreset (const juce::String& name);
    juce::String currentState() const;
    bool printable = false;   // an audio track's insert: it can be printed while recording
};

} // namespace riff
