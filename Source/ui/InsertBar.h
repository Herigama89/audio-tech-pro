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

    static constexpr int preferredWidth = 330;

    /** The side-chain menu: no side chain, then every other track and bus. */
    static void addSidechainItems (juce::PopupMenu& menu, juce::ValueTree insert, juce::ValueTree project, juce::UndoManager& um);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();
    void parentHierarchyChanged() override { refresh(); }

    juce::ValueTree insert, project;
    juce::UndoManager& undoManager;
    bool sidechainable;

    juce::TextButton pinButton { "Pin" }, bypassButton { "Bypass" }, sideButton;
};

} // namespace riff
