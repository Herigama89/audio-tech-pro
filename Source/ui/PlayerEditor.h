#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The Drummer / Session Player editor: the genre, an XY pad (simple to
    complex, soft to loud), fills and swing, which drum pieces play, and a new
    take. Every change writes the region's notes again (one undo step).
*/
class PlayerEditor final : public juce::Component,
                           private juce::ValueTree::Listener
{
public:
    explicit PlayerEditor (juce::UndoManager& um);
    ~PlayerEditor() override;

    void setRegion (juce::ValueTree region);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void set (const juce::Identifier& id, const juce::var& value, bool newStep);
    void refresh();
    juce::Rectangle<int> padArea() const;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); repaint(); }

    juce::UndoManager& undoManager;
    juce::ValueTree region;
    juce::ComboBox genreBox;
    juce::Slider fills, swing;
    juce::Label fillsLabel, swingLabel;
    juce::OwnedArray<juce::ToggleButton> pieces;
    juce::TextButton newTake { "New Take" };
    bool dragging = false;
};

} // namespace riff
