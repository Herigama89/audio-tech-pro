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
    look = std::make_unique<effectface::FaceLook> (effectface::accentFor (info != nullptr ? info->category : juce::String()));
    setLookAndFeel (look.get());
    bypassButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.8f));
    bypassButton.setColour (juce::ToggleButton::tickColourId, look->accent);

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
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 17);
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
            k->label.setFont (juce::FontOptions (11.5f));
            k->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            k->label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (k->label);
        }
    }

    insert.addListener (this);
    refresh();
    setSize (faceWidth, layoutFace (false));
}

EffectPanel::~EffectPanel()
{
    setLookAndFeel (nullptr);
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
    if (! displayArea.isEmpty())
        repaint (displayArea);
}

void EffectPanel::paint (juce::Graphics& g)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    const auto accent = look->accent;

    // The face: brushed dark metal, a title bar with the effect's name and family
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3038), 0.0f, 0.0f, juce::Colour (0xff1a1d22), 0.0f, (float) getHeight(), false));
    g.fillAll();
    for (int y = 0; y < getHeight(); y += 3)
    {
        g.setColour (juce::Colours::white.withAlpha ((y / 3) % 2 == 0 ? 0.012f : 0.0f));
        g.fillRect (0, y, getWidth(), 1);
    }
    auto header = getLocalBounds().removeFromTop (headerHeight).toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4049), 0.0f, 0.0f, juce::Colour (0xff23272d), 0.0f, header.getBottom(), false));
    g.fillRect (header);
    g.setColour (accent);
    g.fillRect (header.removeFromBottom (2.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, (float) headerHeight, (float) getWidth(), 1.0f);
    auto title = getLocalBounds().removeFromTop (headerHeight - 2).reduced (14, 0).toFloat();
    g.setColour (accent);
    g.fillEllipse (title.getX(), title.getCentreY() - 4.0f, 8.0f, 8.0f);
    title.removeFromLeft (16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    const auto name = info != nullptr ? info->name : juce::String ("Effect");
    g.drawText (name, title, juce::Justification::centredLeft, false);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (info != nullptr ? info->category.toUpperCase() : juce::String(), title.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (juce::FontOptions (16.0f, juce::Font::bold), name) + 12.0f),
                juce::Justification::centredLeft, false);

    // The display
    if (info != nullptr && ! displayArea.isEmpty())
    {
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < info->params.size(); ++i)
            v[i] = (float) (double) insert.getProperty (juce::Identifier (info->params[i].id), info->params[i].defaultValue);
        effectface::drawDisplay (g, displayArea.toFloat(), *info, v.data(), accent);
    }

    // The sections: inset panels with their names
    for (const auto& box : sectionBoxes)
    {
        auto b = box.area.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        if (box.title.isNotEmpty())
        {
            g.setColour (accent.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (box.title.toUpperCase(), b.removeFromTop ((float) sectionTitleHeight).reduced (8.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

int EffectPanel::layoutFace (bool apply)
{
    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    if (! apply && info != nullptr)
    {
        // As wide as all the sections side by side (up to about a thousand pixels; then they wrap)
        int total = 0, index = 0;
        for (const auto& section : effectface::sectionsFor (*info))
        {
            const auto count = juce::jmin (section.count, knobs.size() - index);
            index += juce::jmax (0, count);
            total += (section.vertical ? 1 : juce::jmin (count, 12)) * cellWidth + 2 * sectionPad + sectionGap;
        }
        faceWidth = juce::jlimit (effectface::hasDisplay (*info) ? 520 : 360, 1000, total - sectionGap + 28);
    }
    const auto width = apply ? getWidth() : faceWidth;
    auto y = headerHeight + 10;
    if (apply)
    {
        bypassButton.setBounds (width - 108, (headerHeight - 24) / 2, 96, 24);
        displayArea = {};
    }
    if (info != nullptr && effectface::hasDisplay (*info))
    {
        if (apply)
            displayArea = { 14, y, width - 28, displayHeight };
        y += displayHeight + 10;
    }
    if (info == nullptr)
        return y + 10;

    // Sections flow left to right, wrapping; a long one breaks into rows of its own
    if (apply)
        sectionBoxes.clear();
    const auto inner = width - 28;
    const auto perRow = juce::jmax (1, (inner - 2 * sectionPad) / cellWidth);
    int x = 14, rowHeight = 0, index = 0, widest = 0;
    for (const auto& section : effectface::sectionsFor (*info))
    {
        const auto count = juce::jmin (section.count, knobs.size() - index);
        if (count <= 0)
            break;
        const auto across = section.vertical ? 1 : juce::jmin (count, perRow);
        const auto down = section.vertical ? count : (count + perRow - 1) / perRow;
        const auto w = across * cellWidth + 2 * sectionPad;
        const auto h = sectionTitleHeight + down * cellHeight + sectionPad;
        if (x > 14 && x + w > 14 + inner)
        {
            x = 14;
            y += rowHeight + sectionGap;
            rowHeight = 0;
        }
        if (apply)
        {
            sectionBoxes.push_back ({ section.title, { x, y, w, h } });
            for (int k = 0; k < count; ++k)
            {
                const auto col = section.vertical ? 0 : k % perRow, row = section.vertical ? k : k / perRow;
                auto cell = juce::Rectangle<int> (x + sectionPad + col * cellWidth, y + sectionTitleHeight + row * cellHeight, cellWidth, cellHeight);
                auto* knob = knobs[index + k];
                knob->label.setBounds (cell.removeFromTop (16));
                knob->slider.setBounds (cell.reduced (4, 0));
            }
        }
        index += count;
        x += w + sectionGap;
        widest = juce::jmax (widest, x - sectionGap);
        rowHeight = juce::jmax (rowHeight, h);
    }
    juce::ignoreUnused (widest);
    return y + rowHeight + 14;
}

void EffectPanel::resized()
{
    layoutFace (true);
}

} // namespace riff
