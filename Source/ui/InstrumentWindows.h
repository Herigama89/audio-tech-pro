#pragma once

#include <JuceHeader.h>

namespace riff
{

/** A slider with a right-click menu (Logic's: automation, MIDI Learn, default) instead of a drag. */
class MenuSlider : public juce::Slider
{
public:
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        menuClick = e.mods.isPopupMenu() && onMenu != nullptr;
        if (menuClick) onMenu(); else juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! menuClick) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! std::exchange (menuClick, false)) juce::Slider::mouseUp (e); }
private:
    bool menuClick = false;
};

/** A floating window for a plugin's editor or the synth controls. */
class EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& title, juce::Component* contentToOwn, bool canResize,
                  std::function<void()> onCloseRequested)
        : DocumentWindow (title, juce::Colour (0xff1c1d21), DocumentWindow::closeButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (contentToOwn, true);
        setResizable (canResize, false);
        centreAroundComponent (nullptr, getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose(); // the owner deletes this window
    }

private:
    std::function<void()> onClose;
};

/** Controls for the built-in instruments (Tech Synth, Drum Kit, Sampler), bound (undoably) to the track's properties. */
class SynthPanel final : public juce::Component,
                         private juce::ValueTree::Listener
{
public:
    SynthPanel (juce::ValueTree track, juce::UndoManager& um, const juce::String& kind = "synth");
    ~SynthPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Control
    {
        juce::Identifier id;
        juce::Slider slider;
        juce::Label label;
    };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();
    void addControl (const juce::Identifier& id, const juce::String& name, double min, double max,
                     double mid, const juce::String& suffix);

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    juce::ComboBox waveBox;
    juce::Label waveLabel;
    juce::TextButton soundButton { "Choose Sound..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String kind;
    juce::OwnedArray<Control> controls;
};

/** Knobs for one of Riff's built-in effects, bound (undoably) to its INSERT node. */
class EffectPanel final : public juce::Component,
                          private juce::ValueTree::Listener
{
public:
    EffectPanel (juce::ValueTree insert, juce::UndoManager& um);
    ~EffectPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        MenuSlider slider;
        juce::Label label;
    };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();

    juce::ValueTree insert;
    juce::UndoManager& undoManager;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::OwnedArray<Knob> knobs;
};

} // namespace riff
