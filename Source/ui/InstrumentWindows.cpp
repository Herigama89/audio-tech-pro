#include "InstrumentWindows.h"
#include "ControlMenu.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/AutomationWriter.h"

namespace riff
{

SynthPanel::SynthPanel (juce::ValueTree t, juce::UndoManager& um, const juce::String& k)
    : track (t), undoManager (um), kind (k)
{
    if (kind == "drums" || kind == "sampler")
    {
        waveLabel.setText (kind == "drums" ? "Drum Kit" : "Sound", juce::dontSendNotification);
        waveLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (waveLabel);
        if (kind == "drums")
        {
            addControl (ids::drumTune,  "Tune",   -12.0, 12.0, 0.0, " st");
            addControl (ids::drumDecay, "Decay",  0.3, 3.0, 1.0, "x");
        }
        else
        {
            soundButton.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a sound for the sampler",
                                                               juce::File (track[ids::samplerFile].toString()).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
                {
                    if (c.getResult() == juce::File())
                        return;
                    undoManager.beginNewTransaction ("Sampler sound");
                    track.setProperty (ids::samplerFile, c.getResult().getFullPathName(), &undoManager);
                });
            };
            addAndMakeVisible (soundButton);
            addControl (ids::samplerRoot,  "Root Key", 24.0, 96.0, 60.0, "");
            addControl (ids::synthAttack,  "Attack",  0.001, 2.0, 0.05, " s");
            addControl (ids::synthRelease, "Release", 0.001, 6.0, 0.5, " s");
        }
        addControl (ids::synthGain, "Volume", 0.0, 1.0, 0.5, "");
        track.addListener (this);
        refresh();
        setSize ((controls.size() + 1) * 84 + 64, 150);
        return;
    }

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
    {
        const auto fallback = c->id == ids::drumDecay ? 1.0 : c->id == ids::drumTune ? 0.0 : c->id == ids::samplerRoot ? 60.0
                            : c->id == ids::synthGain ? 0.8 : c->slider.getMinimum();
        c->slider.setValue (track.getProperty (c->id, fallback), juce::dontSendNotification);
        if (c->id == ids::samplerRoot)
            c->slider.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); };
    }
    if (kind == "sampler")
    {
        const juce::File file (track[ids::samplerFile].toString());
        soundButton.setButtonText (file.existsAsFile() ? file.getFileNameWithoutExtension() : juce::String ("Choose Sound..."));
    }
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SynthPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto column = area.removeFromLeft (kind == "synth" ? 84 : 140);
    waveLabel.setBounds (column.removeFromTop (20));
    if (kind == "sampler")
        soundButton.setBounds (column.removeFromTop (26).reduced (2, 0));
    else if (kind == "synth")
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
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
            if (p.isChoice())
            {
                // A switch (circuit, mode, note value...): the knob steps through its settings, shown by name
                k->slider.setRange (p.min, p.max, 1.0);
                k->slider.textFromValueFunction = [p] (double v) { return p.text ((float) v); };
                k->slider.valueFromTextFunction = [p] (const juce::String& t)
                {
                    for (size_t c = 0; c < p.choices.size(); ++c)
                        if (p.choices[c].equalsIgnoreCase (t.trim()))
                            return (double) c;
                    return (double) p.defaultValue;
                };
                k->slider.setMouseDragSensitivity (juce::jmax (60, (int) p.choices.size() * 18));
            }
            else
            {
                k->slider.setRange (p.min, p.max, 0.0);
                if (p.midPoint > p.min && p.midPoint < p.max)
                    k->slider.setSkewFactorFromMidPoint (p.midPoint);
                k->slider.setNumDecimalPlacesToDisplay (p.max > 100.0f ? 0 : (p.max > 10.0f ? 1 : 2));
                k->slider.setTextValueSuffix (p.suffix);
            }
            k->slider.setDoubleClickReturnValue (true, p.defaultValue);
            k->slider.updateText();   // (a switch shows its setting's name from the start)
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
            k->slider.onMenu = [this, k, owner, param, name = juce::String (p.name), def = (double) p.defaultValue]
            {
                const auto info2 = findBuiltInEffect (insert[ids::effect].toString());
                controlmenu::Target t;
                t.project = insert.getParent().getParent().getParent();
                t.owner = owner();
                t.param = param();
                t.name = (info2 != nullptr ? info2->name + " " : juce::String()) + name;
                t.um = &undoManager;
                t.value = [k] { return k->slider.getValue(); };
                t.reset = [k, def] { k->slider.setValue (def, juce::sendNotificationSync); };
                controlmenu::show (k->slider, t);
            };
            addAndMakeVisible (k->slider);

            k->label.setText (p.name, juce::dontSendNotification);
            k->label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (k->label);
        }
    }

    insert.addListener (this);
    refresh();
    auto columns = juce::jlimit (1, knobsPerRow, knobs.size());
    auto rows = (knobs.size() + knobsPerRow - 1) / knobsPerRow;
    if (info != nullptr && ! info->columns.empty())
    {
        columns = (int) info->columns.size();
        rows = *std::max_element (info->columns.begin(), info->columns.end());
    }
    const bool curve = info != nullptr && info->response != nullptr;
    setSize (juce::jmax (curve ? 560 : 260, columns * knobWidth + 24), 24 + 6 + (curve ? curveHeight + 10 : 0) + juce::jmax (1, rows) * knobHeight + 12);
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
    for (auto* k : knobs)
        k->slider.updateText();
    if (! curveArea.isEmpty())
        repaint (curveArea);
}

void EffectPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // The EQ's curve (as Logic's Channel EQ shows it): 20 Hz to 20 kHz, +-24 dB
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    if (info == nullptr || info->response == nullptr || curveArea.isEmpty())
        return;
    const auto area = curveArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (area, 4.0f);
    const auto xOf = [area] (double f) { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
    const auto yOf = [area] (double db) { return area.getCentreY() - (float) (db / 24.0) * area.getHeight() * 0.5f; };
    g.setFont (juce::FontOptions (10.0f));
    for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (xOf (f), area.getY(), 1.0f, area.getHeight());
        g.setColour (theme::dimText.withAlpha (0.7f));
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (xOf (f) + 2.0f, area.getBottom() - 13.0f, 30.0f, 12.0f),
                    juce::Justification::left, false);
    }
    for (const auto db : { -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.18f : 0.06f));
        g.fillRect (area.getX(), yOf (db), area.getWidth(), 1.0f);
    }

    std::array<float, BuiltInEffect::maxParams> v {};
    for (size_t i = 0; i < info->params.size(); ++i)
        v[i] = (float) (double) insert.getProperty (juce::Identifier (info->params[i].id), info->params[i].defaultValue);
    juce::Path curve;
    for (int x = 0; x <= (int) area.getWidth(); x += 2)
    {
        const auto f = 20.0 * std::pow (1000.0, x / (double) area.getWidth());
        const auto y = juce::jlimit (area.getY(), area.getBottom(), yOf (info->response (v.data(), f, 48000.0)));
        if (x == 0)
            curve.startNewSubPath (area.getX(), y);
        else
            curve.lineTo (area.getX() + (float) x, y);
    }
    juce::Path fill (curve);
    fill.lineTo (area.getRight(), yOf (0.0));
    fill.lineTo (area.getX(), yOf (0.0));
    fill.closeSubPath();
    g.setColour (theme::accent.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (theme::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

void EffectPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    bypassButton.setBounds (area.removeFromTop (24).removeFromLeft (100));
    area.removeFromTop (6);

    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    curveArea = {};
    if (info != nullptr && info->response != nullptr)
    {
        curveArea = area.removeFromTop (curveHeight);
        area.removeFromTop (10);
    }

    // Columns of knobs when the effect lays them out so (an EQ's bands)
    if (info != nullptr && ! info->columns.empty())
    {
        auto strip = area.withSizeKeepingCentre ((int) info->columns.size() * knobWidth, area.getHeight());
        int index = 0;
        for (const auto count : info->columns)
        {
            auto column = strip.removeFromLeft (knobWidth);
            for (int c = 0; c < count && index < knobs.size(); ++c, ++index)
            {
                auto cell = column.removeFromTop (knobHeight);
                knobs[index]->label.setBounds (cell.removeFromTop (20));
                knobs[index]->slider.setBounds (cell.reduced (2, 0));
            }
        }
        return;
    }

    // The knobs in rows (as many to a row as fit, at most eight), each row centred
    const auto perRow = juce::jlimit (1, knobsPerRow, area.getWidth() / knobWidth);
    for (int start = 0; start < knobs.size(); start += perRow)
    {
        auto row = area.removeFromTop (knobHeight);
        const auto count = juce::jmin (perRow, knobs.size() - start);
        row = row.withSizeKeepingCentre (count * knobWidth, row.getHeight());
        for (int i = start; i < start + count; ++i)
        {
            auto column = row.removeFromLeft (knobWidth);
            knobs[i]->label.setBounds (column.removeFromTop (20));
            knobs[i]->slider.setBounds (column.reduced (2, 0));
        }
    }
}

} // namespace riff
