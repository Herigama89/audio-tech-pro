#include "InstrumentWindows.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/AutomationWriter.h"

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

//==============================================================================
EffectPanel::EffectPanel (juce::ValueTree i, juce::UndoManager& um)
    : insert (i), undoManager (um)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());

    bypassButton.onClick = [this]
    {
        undoManager.beginNewTransaction ("Bypass");
        insert.setProperty (ids::bypass, bypassButton.getToggleState(), &undoManager);
    };
    addAndMakeVisible (bypassButton);

    if (info != nullptr)
    {
        for (const auto& p : info->params)
        {
            auto* k = knobs.add (new Knob());
            k->id = p.id;

            k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
            k->slider.setRange (p.min, p.max, 0.0);
            if (p.midPoint > p.min && p.midPoint < p.max)
                k->slider.setSkewFactorFromMidPoint (p.midPoint);
            k->slider.setDoubleClickReturnValue (true, p.defaultValue);
            k->slider.setNumDecimalPlacesToDisplay (p.max > 100.0f ? 0 : (p.max > 10.0f ? 1 : 2));
            k->slider.setTextValueSuffix (p.suffix);
            // Touch, Latch or Write while playing: the knob is written as automation too.
            const auto owner = [this] { return insert.getParent()[ids::owner].toString(); };
            const auto param = [this, k] { return model::insertParam (insert[ids::id].toString(), k->id); };
            k->slider.onDragStart = [this, k, owner, param, name = p.name]
            {
                undoManager.beginNewTransaction (name);
                if (auto* w = AutomationWriter::current())
                    w->begin (owner(), param(), k->slider.getValue());
            };
            k->slider.onDragEnd = [k, owner, param] { juce::ignoreUnused (k); if (auto* w = AutomationWriter::current()) w->end (owner(), param()); };
            k->slider.onValueChange = [this, k, owner, param]
            {
                if (auto* w = AutomationWriter::current())
                    w->move (owner(), param(), k->slider.getValue());
                insert.setProperty (juce::Identifier (k->id), k->slider.getValue(), &undoManager);
            };
            addAndMakeVisible (k->slider);

            k->label.setText (p.name, juce::dontSendNotification);
            k->label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (k->label);
        }
    }

    insert.addListener (this);
    refresh();
    setSize (juce::jmax (260, knobs.size() * 86 + 24), 170);
}

EffectPanel::~EffectPanel()
{
    insert.removeListener (this);
}

void EffectPanel::refresh()
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    bypassButton.setToggleState ((bool) insert[ids::bypass], juce::dontSendNotification);

    for (int i = 0; i < knobs.size() && info != nullptr; ++i)
        knobs[i]->slider.setValue (insert.getProperty (juce::Identifier (knobs[i]->id), info->params[(size_t) i].defaultValue),
                                   juce::dontSendNotification);
}

void EffectPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void EffectPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    bypassButton.setBounds (area.removeFromTop (24).removeFromLeft (100));
    area.removeFromTop (6);

    for (auto* k : knobs)
    {
        auto column = area.removeFromLeft (86);
        k->label.setBounds (column.removeFromTop (20));
        k->slider.setBounds (column);
    }
}

} // namespace riff
