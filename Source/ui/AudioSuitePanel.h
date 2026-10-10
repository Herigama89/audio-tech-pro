#pragma once

#include <JuceHeader.h>
#include "../engine/AudioSuite.h"
#include "Theme.h"

namespace riff
{

/**
    The AudioSuite window, as in Pro Tools: pick an effect (one of ours, or any
    VST3 / AU / CLAP plugin), set it up in its own face below, then Preview
    (hear the first selected region through it) or Render (the selected
    regions are processed into new files and play those from now on;
    Undo puts the originals back).

    Above the plugin: Clip or Whole File, the handles kept either side, and a
    tail so reverbs and delays can ring out.
*/
class AudioSuitePanel final : public juce::Component,
                              private juce::Timer,
                              private juce::ComponentListener
{
public:
    AudioSuitePanel();
    ~AudioSuitePanel() override;

    /** Sets the effect, its name and its face (owned here). The effect must outlive nothing but this panel. */
    void setEffect (std::shared_ptr<Effect> effect, const juce::String& name, juce::Component* faceToOwn,
                    std::function<void()> beforeUse = {});

    std::shared_ptr<Effect> getEffect() const { return effect; }
    audiosuite::Options getOptions() const;

    std::function<void (juce::Component& target)> onChooseEffect;
    std::function<void()> onPreview, onRender;
    std::function<juce::String()> selectionText;   // "3 regions selected"
    std::function<bool()> isPreviewing;

    /** Called before the effect is used (a built-in effect takes its knobs' values). */
    void prepareForUse() { if (beforeUse) beforeUse(); }

    void setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int barHeight = 92;

private:
    void timerCallback() override;
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override { if (wasResized) fit(); }
    void fit();

    std::shared_ptr<Effect> effect;
    std::unique_ptr<juce::Component> face;
    std::function<void()> beforeUse;

    juce::TextButton pluginButton { "Choose an effect..." };
    juce::ComboBox modeBox;
    juce::Slider handles, tail;
    juce::TextButton previewButton { "Preview" }, renderButton { "Render" };
    juce::Label status, selection;
};

} // namespace riff
