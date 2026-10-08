#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "../engine/EffectRack.h"
#include "../engine/ConsolePlugins.h"

namespace riff
{

/**
    Console mode's Master Bus Compressor: a hardware-style face for the
    user's own Universal Audio G-series bus compressor plugin.

    The artwork comes from the design mockup (see Tools/make_bus_comp_panel.py);
    the pointers, the meter's needle and scale, the LEDs, the lamp and all the
    lettering are drawn here so they can move and say the right thing.

    The knobs drive the plugin's own parameters, so the sound is UA's. Shown
    only when such a plugin is installed (or the project already uses one).
*/
class BusCompConsolePanel final : public juce::Component
{
public:
    BusCompConsolePanel (juce::UndoManager& um, AudioEngine& engine, EffectRack& effects);
    ~BusCompConsolePanel() override;

    void setProject (juce::ValueTree newProject);

    /** The bus compressors found on this computer, best first. */
    void setCandidates (const juce::Array<juce::PluginDescription>& found);

    /** True if the panel has something to control: an installed plugin, or one already in the project. */
    bool isAvailable() const;

    /** Re-reads the project (plugin chosen, knob assignments). */
    void refresh();

    /** Called ~30 times a second: follows the plugin's knobs and moves the meters. */
    void update();

    /** The master's output peak, for the right-hand LED ladder. */
    void setOutputLevel (float peak);

    /** The width the panel needs at a given height (it keeps the artwork's shape). */
    int widthForHeight (int height) const;

    /** Load a plugin into the master's bus compressor place. */
    std::function<void (const juce::PluginDescription&)> onLoadPlugin;
    /** Take the plugin out (back to the built-in compressor). */
    std::function<void()> onRemovePlugin;
    /** Show the plugin's own window. */
    std::function<void (juce::ValueTree insert)> onOpenPluginWindow;

    /** For the snapshot tool: shows the panel as if a plugin were running. */
    void showPreview (const std::array<float, 7>& knobValues, float reductionDb, float outputLevel, bool in,
                      const std::array<juce::StringArray, 7>& texts = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int imageWidth = 636, imageHeight = 1484;

private:
    using Control = console::Control;
    static constexpr int numKnobs = 7;

    class Knob;

    juce::AudioPluginInstance* plugin() const;
    juce::AudioProcessorParameter* parameterFor (Control c) const;
    int parameterIndexFor (Control c) const;
    void assign (Control c, int parameterIndex);
    bool isIn() const;
    void toggleIn();
    void showMenu();
    float gainReduction();

    juce::Point<float> toImage (juce::Point<float> p) const;
    juce::Rectangle<int> fromImage (juce::Rectangle<float> r) const;

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    EffectRack& effects;
    juce::ValueTree project;
    juce::Array<juce::PluginDescription> candidates;

    juce::Image artwork;
    float scale = 1.0f;
    juce::Point<float> origin;

    std::array<std::unique_ptr<Knob>, numKnobs> knobs;

    float needleDb = 0.0f, reductionDb = 0.0f, outputDb = -100.0f;
    bool previewing = false, previewIn = false;
    std::array<juce::StringArray, 7> previewTexts;
};

} // namespace riff
