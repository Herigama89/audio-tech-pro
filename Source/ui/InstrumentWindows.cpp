#include "InstrumentWindows.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

SynthPanel::SynthPanel (juce::ValueTree t, juce::UndoManager& um)
    : track (t), undoManager (um)
{
    waveLabel.setText ("Wave", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (waveLabel);

    waveBox.addItemList ({ "Saw", "Square", "Triangle", "Sine" }, 1);
    waveBox.onChange = [this]
    {
        undoManager.beginNewTransaction ("Synth wave");
        track.setProperty (ids::synthWave, waveBox.getSelectedId() - 1, &undoManager);
    };
    addAndMakeVisible (waveBox);

    addControl (ids::synthCutoff,    "Cutoff",    40.0, 18000.0, 1500.0, " Hz");
    addControl (ids::synthResonance, "Resonance", 0.0,  1.0,     0.5,    "");
    addControl (ids::synthAttack,    "Attack",    0.001, 4.0,    0.2,    " s");
    addControl (ids::synthDecay,     "Decay",     0.001, 4.0,    0.4,    " s");
    addControl (ids::synthSustain,   "Sustain",   0.0,  1.0,     0.5,    "");
    addControl (ids::synthRelease,   "Release",   0.001, 6.0,    0.5,    " s");
    addControl (ids::synthGain,      "Volume",    0.0,  1.0,     0.5,    "");

    track.addListener (this);
    refresh();
    setSize (8 * 84 + 24, 150);
}

SynthPanel::~SynthPanel()
{
    track.removeListener (this);
}

void SynthPanel::addControl (const juce::Identifier& id, const juce::String& name, double min, double max,
                             double mid, const juce::String& suffix)
{
    auto* c = controls.add (new Control());
    c->id = id;

    c->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    c->slider.setRange (min, max, 0.0);
    c->slider.setSkewFactorFromMidPoint (mid);
    c->slider.setNumDecimalPlacesToDisplay (max > 100.0 ? 0 : 2);
    c->slider.setTextValueSuffix (suffix);
    c->slider.onDragStart = [this, name] { undoManager.beginNewTransaction ("Synth " + name); };
    c->slider.onValueChange = [this, c] { track.setProperty (c->id, c->slider.getValue(), &undoManager); };
    addAndMakeVisible (c->slider);

    c->label.setText (name, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c->label);
}

void SynthPanel::refresh()
{
    waveBox.setSelectedId ((int) track.getProperty (ids::synthWave, 0) + 1, juce::dontSendNotification);
    for (auto* c : controls)
        c->slider.setValue (track.getProperty (c->id, c->slider.getMinimum()), juce::dontSendNotification);
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SynthPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto column = area.removeFromLeft (84);
    waveLabel.setBounds (column.removeFromTop (20));
    waveBox.setBounds (column.removeFromTop (26).reduced (2, 0));

    for (auto* c : controls)
    {
        column = area.removeFromLeft (84);
        c->label.setBounds (column.removeFromTop (20));
        c->slider.setBounds (column);
    }
}

} // namespace riff
