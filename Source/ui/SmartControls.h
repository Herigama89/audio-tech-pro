#pragma once

#include <JuceHeader.h>
#include "StripParts.h"

namespace riff
{

/**
    Logic's Smart Controls (B): eight knobs for the selected track's sound,
    mapped to the parameters that matter most (the synth's filter and
    envelope, an instrument plugin's first controls, or the channel strip's
    EQ for audio). Right-click a knob to map it to any parameter of the track.
*/
class SmartControls final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::Timer
{
public:
    SmartControls (juce::UndoManager& um, std::function<juce::AudioPluginInstance* (const juce::String& id)> pluginForInsert,
                   std::function<juce::AudioPluginInstance* (const juce::String& trackId)> pluginForInstrument);
    ~SmartControls() override;

    void setTrack (juce::ValueTree project, juce::ValueTree track);
    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    static constexpr int numKnobs = 8;

    /** The parameters a knob can drive on this track: key ("prop:...", "insert:id:param", "plugin:id:#n", "instrument:#n") and name. */
    struct Target { juce::String key, name; };
    juce::Array<Target> targetsFor (const juce::ValueTree& track) const;
    juce::StringArray defaultMapping (const juce::ValueTree& track) const;

private:
    struct Knob
    {
        std::unique_ptr<strip::PointerKnob> slider;
        juce::String key, name;
    };

    void rebuild();
    void assign (int index, const juce::String& key);
    double get (const juce::String& key) const;          // 0..1
    void set (const juce::String& key, double normalised);
    juce::String textFor (const juce::String& key) const;
    void showMapMenu (int index);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refreshValues(); }
    void timerCallback() override { refreshValues(); }   // plugin parameters change without telling us
    void refreshValues();

    juce::UndoManager& undoManager;
    std::function<juce::AudioPluginInstance* (const juce::String&)> insertPlugin, instrumentPlugin;
    juce::ValueTree project, track;
    std::array<Knob, numKnobs> knobs;
    juce::TextButton closeButton { "Close" }, resetButton { "Default Mapping" };
};

} // namespace riff
