#include "SmartControls.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/ConsoleChannel.h"

namespace riff
{

namespace
{
    // Track properties a knob can drive: id, name, min, max, unit.
    struct PropSpec { const char* id; const char* name; double min, max; const char* unit; bool log; };
    const std::vector<PropSpec>& propSpecs()
    {
        static const std::vector<PropSpec> specs {
            { "volume", "Volume", 0.0, 2.0, "", false },
            { "pan", "Pan", -1.0, 1.0, "", false },
            { "synthCutoff", "Cutoff", 40.0, 18000.0, " Hz", true },
            { "synthResonance", "Resonance", 0.0, 1.0, "", false },
            { "synthAttack", "Attack", 0.001, 4.0, " s", true },
            { "synthDecay", "Decay", 0.001, 4.0, " s", true },
            { "synthSustain", "Sustain", 0.0, 1.0, "", false },
            { "synthRelease", "Release", 0.001, 6.0, " s", true },
            { "synthGain", "Synth Level", 0.0, 1.0, "", false },
            { "consoleHF", "HF", -15.0, 15.0, " dB", false },
            { "consoleHMF", "HMF", -15.0, 15.0, " dB", false },
            { "consoleLMF", "LMF", -15.0, 15.0, " dB", false },
            { "consoleLF", "LF", -15.0, 15.0, " dB", false },
            { "consoleDynamics", "Dynamics", 0.0, 10.0, "", false },
            { "consoleInput", "Input", -20.0, 20.0, " dB", false },
            { "consoleOutput", "Output", -20.0, 20.0, " dB", false },
            { "consoleHPF", "High-pass", 0.0, 350.0, " Hz", false },
        };
        return specs;
    }

    const PropSpec* specFor (const juce::String& id)
    {
        for (const auto& s : propSpecs())
            if (id == s.id)
                return &s;
        return nullptr;
    }

    double toNorm (const PropSpec& s, double v)
    {
        v = juce::jlimit (s.min, s.max, v);
        return s.log ? std::log (v / s.min) / std::log (s.max / s.min) : (v - s.min) / (s.max - s.min);
    }

    double fromNorm (const PropSpec& s, double n)
    {
        n = juce::jlimit (0.0, 1.0, n);
        return s.log ? s.min * std::pow (s.max / s.min, n) : s.min + n * (s.max - s.min);
    }
}

SmartControls::SmartControls (juce::UndoManager& um, std::function<juce::AudioPluginInstance* (const juce::String&)> forInsert,
                              std::function<juce::AudioPluginInstance* (const juce::String&)> forInstrument)
    : undoManager (um), insertPlugin (std::move (forInsert)), instrumentPlugin (std::move (forInstrument))
{
    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.slider = std::make_unique<strip::PointerKnob> (theme::neon);
        k.slider->setRange (0.0, 1.0, 0.0);
        k.slider->setPopupDisplayEnabled (true, true, this);
        k.slider->textFromValueFunction = [this, i] (double) { return textFor (knobs[(size_t) i].key); };
        k.slider->onDragStart = [this, i] { undoManager.beginNewTransaction ("Smart Control " + knobs[(size_t) i].name); };
        k.slider->onValueChange = [this, i] { set (knobs[(size_t) i].key, knobs[(size_t) i].slider->getValue()); };
        k.slider->addMouseListener (this, false);
        k.slider->getProperties().set ("index", i);
        addAndMakeVisible (*k.slider);
    }

    look::key (closeButton, look::Key::dark);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    resetButton.onClick = [this]
    {
        if (track.isValid())
        {
            track.removeProperty ("smartControls", nullptr);
            rebuild();
        }
    };
    addAndMakeVisible (resetButton);
    startTimerHz (8);
}

SmartControls::~SmartControls()
{
    track.removeListener (this);
}

void SmartControls::setTrack (juce::ValueTree newProject, juce::ValueTree newTrack)
{
    if (newTrack == track && newProject == project)
        return;
    track.removeListener (this);
    project = newProject;
    track = newTrack;
    track.addListener (this);
    rebuild();
}

juce::Array<SmartControls::Target> SmartControls::targetsFor (const juce::ValueTree& t) const
{
    juce::Array<Target> targets;
    if (! t.isValid())
        return targets;

    targets.add ({ "prop:volume", "Volume" });
    targets.add ({ "prop:pan", "Pan" });

    if (model::isInstrumentTrack (t))
    {
        if (t[ids::instrument].toString() == "plugin")
        {
            if (auto* p = instrumentPlugin (t[ids::id].toString()))
                for (int i = 0; i < juce::jmin (64, p->getParameters().size()); ++i)
                    targets.add ({ "instrument:#" + juce::String (i), p->getParameters()[i]->getName (30) });
        }
        else
            for (const auto& s : propSpecs())
                if (juce::String (s.id).startsWith ("synth"))
                    targets.add ({ "prop:" + juce::String (s.id), s.name });
    }

    for (const auto& s : propSpecs())
        if (juce::String (s.id).startsWith ("console"))
            targets.add ({ "prop:" + juce::String (s.id), juce::String ("Strip ") + s.name });

    for (const auto& insert : model::insertsOf (model::findChain (project, t[ids::id].toString())))
    {
        const auto id = insert[ids::id].toString();
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            for (const auto& p : info->params)
                targets.add ({ "insert:" + id + ":" + p.id, info->name + " " + p.name });
        else if (auto* plugin = insertPlugin (id))
            for (int i = 0; i < juce::jmin (64, plugin->getParameters().size()); ++i)
                targets.add ({ "plugin:" + id + ":#" + juce::String (i), plugin->getName() + " " + plugin->getParameters()[i]->getName (24) });
    }
    return targets;
}

juce::StringArray SmartControls::defaultMapping (const juce::ValueTree& t) const
{
    if (model::isInstrumentTrack (t))
    {
        if (t[ids::instrument].toString() == "plugin")
        {
            juce::StringArray keys;
            if (auto* p = instrumentPlugin (t[ids::id].toString()))
                for (int i = 0; i < juce::jmin (numKnobs - 1, p->getParameters().size()); ++i)
                    keys.add ("instrument:#" + juce::String (i));
            while (keys.size() < numKnobs - 1)
                keys.add ("prop:pan");
            keys.add ("prop:volume");
            return keys;
        }
        return { "prop:synthCutoff", "prop:synthResonance", "prop:synthAttack", "prop:synthDecay",
                 "prop:synthSustain", "prop:synthRelease", "prop:pan", "prop:volume" };
    }
    return { "prop:consoleHPF", "prop:consoleLF", "prop:consoleLMF", "prop:consoleHMF",
             "prop:consoleHF", "prop:consoleDynamics", "prop:pan", "prop:volume" };
}

void SmartControls::rebuild()
{
    auto keys = juce::StringArray::fromTokens (track.getProperty ("smartControls").toString(), "|", {});
    if (keys.size() != numKnobs)
        keys = defaultMapping (track);

    const auto targets = targetsFor (track);
    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.key = keys[i];
        k.name = k.key;
        for (const auto& t : targets)
            if (t.key == k.key)
                k.name = t.name;
        k.slider->setEnabled (track.isValid());
        k.slider->setTooltip (k.name + " (right-click to map this knob)");
    }
    refreshValues();
    repaint();
}

void SmartControls::assign (int index, const juce::String& key)
{
    juce::StringArray keys;
    for (const auto& k : knobs)
        keys.add (k.key);
    keys.set (index, key);
    track.setProperty ("smartControls", keys.joinIntoString ("|"), nullptr);
    rebuild();
}

double SmartControls::get (const juce::String& key) const
{
    if (! track.isValid())
        return 0.0;
    if (key.startsWith ("prop:"))
    {
        const auto id = key.fromFirstOccurrenceOf ("prop:", false, false);
        if (const auto* s = specFor (id))
        {
            const auto fallback = id == "volume" ? 1.0 : id == "synthCutoff" ? 3000.0 : id.contains ("Freq") ? 1000.0 : 0.0;
            return toNorm (*s, (double) track.getProperty (juce::Identifier (id), fallback));
        }
    }
    if (key.startsWith ("instrument:#"))
        if (auto* p = instrumentPlugin (track[ids::id].toString()))
            if (const auto i = key.fromFirstOccurrenceOf ("#", false, false).getIntValue(); juce::isPositiveAndBelow (i, p->getParameters().size()))
                return p->getParameters()[i]->getValue();
    if (key.startsWith ("plugin:"))
    {
        const auto id = key.fromFirstOccurrenceOf ("plugin:", false, false).upToFirstOccurrenceOf (":", false, false);
        if (auto* p = insertPlugin (id))
            if (const auto i = key.fromFirstOccurrenceOf ("#", false, false).getIntValue(); juce::isPositiveAndBelow (i, p->getParameters().size()))
                return p->getParameters()[i]->getValue();
    }
    if (key.startsWith ("insert:"))
    {
        const auto parts = juce::StringArray::fromTokens (key, ":", {});
        const auto insert = model::findChain (project, track[ids::id].toString()).getChildWithProperty (ids::id, parts[1]);
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            for (const auto& p : info->params)
                if (p.id == parts[2])
                    return (juce::jlimit ((double) p.min, (double) p.max, (double) insert.getProperty (p.id, p.defaultValue)) - p.min) / (p.max - p.min);
    }
    return 0.0;
}

void SmartControls::set (const juce::String& key, double n)
{
    if (! track.isValid())
        return;
    if (key.startsWith ("prop:"))
    {
        const auto id = key.fromFirstOccurrenceOf ("prop:", false, false);
        if (const auto* s = specFor (id))
            track.setProperty (juce::Identifier (id), fromNorm (*s, n), &undoManager);
        return;
    }
    const auto setPluginParam = [n] (juce::AudioPluginInstance* p, int i)
    {
        if (p != nullptr && juce::isPositiveAndBelow (i, p->getParameters().size()))
            p->getParameters()[i]->setValueNotifyingHost ((float) n);
    };
    if (key.startsWith ("instrument:#"))
        setPluginParam (instrumentPlugin (track[ids::id].toString()), key.fromFirstOccurrenceOf ("#", false, false).getIntValue());
    else if (key.startsWith ("plugin:"))
        setPluginParam (insertPlugin (key.fromFirstOccurrenceOf ("plugin:", false, false).upToFirstOccurrenceOf (":", false, false)),
                        key.fromFirstOccurrenceOf ("#", false, false).getIntValue());
    else if (key.startsWith ("insert:"))
    {
        const auto parts = juce::StringArray::fromTokens (key, ":", {});
        auto insert = model::findChain (project, track[ids::id].toString()).getChildWithProperty (ids::id, parts[1]);
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            for (const auto& p : info->params)
                if (p.id == parts[2])
                    insert.setProperty (juce::Identifier (p.id), p.min + n * (p.max - p.min), &undoManager);
    }
}

juce::String SmartControls::textFor (const juce::String& key) const
{
    const auto n = get (key);
    if (key.startsWith ("prop:"))
    {
        const auto id = key.fromFirstOccurrenceOf ("prop:", false, false);
        if (const auto* s = specFor (id))
        {
            const auto v = fromNorm (*s, n);
            if (id == "volume")
                return v <= 0.0001 ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB";
            if (id == "pan")
                return std::abs (v) < 0.01 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 64.0));
            if (id == "consoleHPF" && v < 16.5)
                return "Off";
            return juce::String (v, v >= 100.0 ? 0 : 2) + s->unit;
        }
    }
    if (key.startsWith ("instrument:#") || key.startsWith ("plugin:"))
    {
        const auto i = key.fromFirstOccurrenceOf ("#", false, false).getIntValue();
        auto* p = key.startsWith ("plugin:") ? insertPlugin (key.fromFirstOccurrenceOf ("plugin:", false, false).upToFirstOccurrenceOf (":", false, false))
                                             : instrumentPlugin (track[ids::id].toString());
        if (p != nullptr && juce::isPositiveAndBelow (i, p->getParameters().size()))
            return p->getParameters()[i]->getCurrentValueAsText();
    }
    return juce::String (juce::roundToInt (n * 100.0)) + "%";
}

void SmartControls::refreshValues()
{
    for (auto& k : knobs)
        if (! k.slider->isMouseButtonDown())
            k.slider->setValue (get (k.key), juce::dontSendNotification);
    repaint();
}

void SmartControls::showMapMenu (int index)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Map knob " + juce::String (index + 1) + " to");
    for (const auto& t : targetsFor (track))
        menu.addItem (t.name, true, t.key == knobs[(size_t) index].key, [this, index, key = t.key] { assign (index, key); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void SmartControls::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! track.isValid())
        return;
    for (int i = 0; i < numKnobs; ++i)
        if (e.eventComponent == knobs[(size_t) i].slider.get())
            showMapMenu (i);
}

void SmartControls::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff232427));
    auto header = getLocalBounds().removeFromTop (28);
    g.setColour (theme::toolbarDark);
    g.fillRect (header);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Smart Controls" + (track.isValid() ? juce::String (":  ") + track[ids::name].toString() : juce::String ("  (select a track)")),
                header.reduced (10, 0), juce::Justification::centredLeft, true);

    for (const auto& k : knobs)
    {
        const auto b = k.slider->getBounds();
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (k.name, b.withY (b.getBottom() + 4).withHeight (16).expanded (16, 0), juce::Justification::centred, 1, 0.7f);
        g.setColour (theme::lcdDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (textFor (k.key), b.withY (b.getBottom() + 20).withHeight (14).expanded (16, 0), juce::Justification::centred, false);
    }
}

void SmartControls::resized()
{
    auto header = getLocalBounds().removeFromTop (28).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (6);
    resetButton.setBounds (header.removeFromRight (130));

    auto area = getLocalBounds().withTrimmedTop (36).reduced (12, 0);
    const auto cell = area.getWidth() / numKnobs;
    const auto size = juce::jlimit (40, 76, juce::jmin (cell - 20, area.getHeight() - 50));
    for (int i = 0; i < numKnobs; ++i)
        knobs[(size_t) i].slider->setBounds (juce::Rectangle<int> (area.getX() + i * cell, area.getY(), cell, size + 4).withSizeKeepingCentre (size, size));
}

} // namespace riff
