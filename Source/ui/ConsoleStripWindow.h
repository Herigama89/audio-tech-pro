#pragma once

#include <JuceHeader.h>
#include "StripParts.h"
#include "../engine/ConsoleChannel.h"

namespace riff
{

/**
    The built-in channel strip in full, in its own window (as LUNA opens a
    console strip): input trim, high- and low-pass filters, the four-band EQ
    with its frequencies, Qs and bell switches, the compressor (amount, ratio,
    release, fast attack) and the gate/expander, output trim, and the EQ
    curve on top. The knobs on the mixer strip are the same controls.
*/
class ConsoleStripWindow final : public juce::Component,
                                 private juce::ValueTree::Listener
{
public:
    ConsoleStripWindow (juce::ValueTree track, juce::UndoManager& um);
    ~ConsoleStripWindow() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Control
    {
        juce::Identifier property;
        juce::String name;
        std::unique_ptr<strip::PointerKnob> knob;
        std::function<juce::String (double)> text;
        std::function<double (double)> toProperty, fromProperty;   // slider value <-> stored value
        juce::Rectangle<int> labelArea;
    };

    struct Switch
    {
        juce::Identifier property;
        bool defaultOn;
        std::unique_ptr<strip::LampKey> key;
        juce::String name;
    };

    Control& addKnob (const juce::Identifier& property, const juce::String& name, double min, double max, double fallback,
                      double skewMid, std::function<juce::String (double)> text, juce::Colour ring);
    void addSwitch (const juce::Identifier& property, const juce::String& name, bool defaultOn);
    void refresh();
    juce::Rectangle<int> curveArea() const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); repaint(); }

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<Switch>> switches;

    struct Section { juce::String name; juce::Rectangle<int> area; };
    std::vector<Section> sections;

    static constexpr int knobSize = 46;
};

} // namespace riff
