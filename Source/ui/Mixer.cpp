#include "Mixer.h"
#include "RoutingMenu.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/MasterBusCompressor.h"
#include "../engine/ConsoleChannel.h"

namespace riff
{

namespace
{
    constexpr double minDb = -60.0;
    double gainToDb (double g) { return g <= 0.0 ? minDb : juce::jmax (minDb, juce::Decibels::gainToDecibels (g)); }
    double dbToGain (double db) { return db <= minDb + 0.01 ? 0.0 : juce::Decibels::decibelsToGain (db); }

    juce::String insertName (const juce::ValueTree& insert)
    {
        if (insert[ids::kind].toString() == model::pluginKind)
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
                return d.name;
            return "Plugin";
        }

        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            return info->name;

        return "?";
    }
}

//==============================================================================
// ChannelStrip
//==============================================================================
ChannelStrip::ChannelStrip (MixerPanel& o, juce::ValueTree p, juce::ValueTree t)
    : owner (o), project (p), track (t)
{
    if (track.isValid())
        addMouseListener (&selectOnClick, true);

    if (! isMaster())
    {
        // EQ and Dynamics knobs on the artwork
        using S = console::StripControl;
        setupConsoleKnob (hfKnob,  ids::consoleHF,  S::hf,  -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "HF 8 kHz", " dB");
        setupConsoleKnob (hmfKnob, ids::consoleHMF, S::hmf, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "HMF 2.5 kHz", " dB");
        setupConsoleKnob (lmfKnob, ids::consoleLMF, S::lmf, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "LMF 400 Hz", " dB");
        setupConsoleKnob (lfKnob,  ids::consoleLF,  S::lf,  -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "LF 100 Hz", " dB");
        setupConsoleKnob (dynKnob, ids::consoleDynamics, S::dynamics, 0.0, ConsoleChannel::maxDynamics, "Dynamics", "");

        consoleInButton.setTooltip ("EQ in");
        consoleInButton.onClick = [this] { toggleStripSwitch (console::StripControl::eqIn, ids::consoleIn, "EQ in"); };
        dynInButton.setTooltip ("Dynamics in");
        dynInButton.onClick = [this] { toggleStripSwitch (console::StripControl::dynIn, ids::consoleDynIn, "Dynamics in"); };
        consoleInButton.addMouseListener (this, false);
        dynInButton.addMouseListener (this, false);
        addAndMakeVisible (consoleInButton);
        addAndMakeVisible (dynInButton);

        // The strip's IN key: the whole channel strip (built-in or plugin) in or out; a plugin stays loaded.
        stripInButton.setTooltip ("Channel strip IN: switch the whole strip (EQ, dynamics, or your strip plugin) in or out");
        stripInButton.onClick = [this]
        {
            auto& um = owner.getUndoManager();
            um.beginNewTransaction ("Channel strip in");
            track.setProperty (ids::stripIn, ! (bool) track.getProperty (ids::stripIn, true), &um);
        };
        addAndMakeVisible (stripInButton);

        // A VCA (or folder stack) strip is just a fader with mute and solo.
        if (isVca())
            for (auto* c : std::initializer_list<juce::Component*> { &hfKnob, &hmfKnob, &lmfKnob, &lfKnob, &dynKnob, &consoleInButton, &dynInButton, &stripInButton })
                c->setVisible (false);

        muteButton.setTooltip ("Mute");
        soloButton.setTooltip ("Solo");
        // Mute and solo act on every selected track when this one is among them (as in Logic).
        auto toggle = [this] (const juce::Identifier& flag, const juce::String& undoName)
        {
            auto& um = owner.getUndoManager();
            um.beginNewTransaction (undoName);
            const auto value = ! (bool) track[flag];
            for (auto other : routing::linkedTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray(),
                                                     flag == ids::mute ? "Mute" : "Solo"))
                other.setProperty (flag, value, &um);
        };
        muteButton.onClick = [toggle] { toggle (ids::mute, "Mute"); };
        soloButton.onClick = [toggle] { toggle (ids::solo, "Solo"); };
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);

        // The two displays under Dynamics: its amount, and pan (drag sideways)
        dynLabel.setJustificationType (juce::Justification::centred);
        dynLabel.setColour (juce::Label::textColourId, theme::stripInk);

        // The knobs sit on the light grey face.
        for (auto* k : std::initializer_list<juce::Component*> { &hfKnob, &hmfKnob, &lmfKnob, &lfKnob, &dynKnob, &panKnob })
            look::onLight (*k);
        dynLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (dynLabel);
        dynLabel.setVisible (! isVca());

        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.textFromValueFunction = [] (double value)
        {
            const auto amount = juce::roundToInt (std::abs (value) * 64.0);
            return amount == 0 ? juce::String ("C") : (value < 0 ? "L" : "R") + juce::String (amount);
        };
        panKnob.setDoubleClickReturnValue (true, 0.0);
        panKnob.setTooltip ("Pan: drag left or right; double-click to centre");
        panKnob.onDragStart = [this]
        {
            owner.getUndoManager().beginNewTransaction ("Pan");
            if (auto* w = AutomationWriter::current())
                w->begin (track[ids::id].toString(), model::panParam, panKnob.getValue());
        };
        panKnob.onDragEnd = [this]
        {
            if (auto* w = AutomationWriter::current())
                w->end (track[ids::id].toString(), model::panParam);
        };
        panKnob.onValueChange = [this]
        {
            if (auto* w = AutomationWriter::current(); w != nullptr && w->move (track[ids::id].toString(), model::panParam, panKnob.getValue()))
                return;   // written as automation
            track.setProperty (ids::pan, panKnob.getValue(), &owner.getUndoManager());
        };
        addAndMakeVisible (panKnob);
        panKnob.setVisible (! isVca());
    }

    for (int i = 0; i < numSlots; ++i)
    {
        auto* b = slots.add (new juce::TextButton());
        look::key (*b, look::Key::slot);
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Click to add an effect, or open this one. Right-click for bypass and remove.");
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
        b->setVisible (! isVca());
    }

    for (int i = 0; i < numSends; ++i)
    {
        auto* b = sendButtons.add (new juce::TextButton());
        look::key (*b, look::Key::slot);
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Send to an aux bus (e.g. a shared reverb). Click to choose the bus.");
        b->onClick = [this, i] { showSendMenu (i); };
        addAndMakeVisible (b);
        b->setVisible (hasSends());

        auto* s = sendLevels.add (new juce::Slider());
        look::knob (*s, look::Knob::black, false);
        look::onLight (*s);
        s->setRange (minDb, 6.0, 0.1);
        s->setSkewFactorFromMidPoint (-12.0);
        s->setDoubleClickReturnValue (true, 0.0);
        s->setScrollWheelEnabled (false);
        s->setPopupDisplayEnabled (true, true, nullptr);
        s->textFromValueFunction = [] (double db) { return db <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
        s->onDragStart = [this] { owner.getUndoManager().beginNewTransaction ("Send level"); };
        s->onValueChange = [this, i, s]
        {
            if (auto send = sendAt (i); send.isValid())
                send.setProperty (ids::level, dbToGain (s->getValue()), &owner.getUndoManager());
        };
        addAndMakeVisible (s);
        s->setVisible (hasSends());
    }

    fader.setRange (minDb, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setPopupDisplayEnabled (true, true, nullptr);
    fader.textFromValueFunction = [] (double db) { return db <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
    fader.onDragStart = [this]
    {
        owner.getUndoManager().beginNewTransaction (isMaster() ? "Master volume" : "Volume");

        // The other selected faders, and the ones in this track's group, follow this one (as in Logic).
        groupStart.clear();
        dragStartDb = fader.getValue();
        if (! isMaster())
            for (const auto& other : routing::linkedTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray(), "Volume"))
                if (other != track)
                    groupStart[other[ids::id].toString()] = gainToDb ((double) other.getProperty (ids::volume, 1.0));

        if (auto* w = AutomationWriter::current(); w != nullptr && ! isMaster())
            w->begin (track[ids::id].toString(), model::volumeParam, dbToGain (fader.getValue()));
    };
    fader.onDragEnd = [this]
    {
        groupStart.clear();
        if (auto* w = AutomationWriter::current(); w != nullptr && ! isMaster())
            w->end (track[ids::id].toString(), model::volumeParam);
    };
    fader.onValueChange = [this]
    {
        const auto gain = dbToGain (fader.getValue());
        if (isMaster())
        {
            project.setProperty (ids::masterGain, gain, &owner.getUndoManager());
            return;
        }

        // Touch, Latch or Write while playing: the move becomes automation.
        if (auto* w = AutomationWriter::current(); w != nullptr && w->move (track[ids::id].toString(), model::volumeParam, gain))
        {
            const auto db = fader.getValue();
            dbLabel.setText (db <= minDb + 0.01 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
            return;
        }

        track.setProperty (ids::volume, gain, &owner.getUndoManager());

        const auto delta = fader.getValue() - dragStartDb;
        for (const auto& [id, startDb] : groupStart)
            if (auto other = model::findById (project, id); other.isValid() && startDb > minDb + 0.01)
                other.setProperty (ids::volume, dbToGain (juce::jlimit (minDb, 6.0, startDb + delta)), &owner.getUndoManager());
    };
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);

    if (! isMaster() && ! isVca())
    {
        look::key (automationButton, look::Key::slot);
        automationButton.setWantsKeyboardFocus (false);
        automationButton.setTooltip ("Automation mode: Off, Read, Touch (writes while you hold a control), "
                                     "Latch (writes from your first touch until you stop), Write (writes everything from the start)");
        automationButton.onClick = [this] { showAutomationModeMenu(); };
        addAndMakeVisible (automationButton);
        updateAutomationButton();
    }

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::FontOptions (11.0f));
    dbLabel.setColour (juce::Label::textColourId, theme::lcdText);
    dbLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (dbLabel);

    refresh();
}

void ChannelStrip::setupConsoleKnob (juce::Slider& knob, const juce::Identifier& property, console::StripControl control,
                                     double min, double max, const juce::String& name, const juce::String& suffix)
{
    knob.getProperties().set ("min", min);
    knob.getProperties().set ("max", max);
    knob.setRange (min, max, 0.1);
    knob.setDoubleClickReturnValue (true, 0.0);
    knob.setPopupDisplayEnabled (true, true, nullptr);
    knob.setTooltip (name);

    // Built in: the track's own setting. Console mode: the plugin's parameter.
    knob.textFromValueFunction = [this, control, name, suffix] (double v)
    {
        if (pluginMode)
            if (auto* p = stripParameter (control))
                return (p->getName (32) + "  " + p->getText ((float) v, 24) + " " + p->getLabel()).trim();
        return name + "  " + (v > 0.05 && suffix.isNotEmpty() ? "+" : "") + juce::String (v, 1) + suffix;
    };
    knob.onDragStart = [this, control, name]
    {
        if (pluginMode)
        {
            if (auto* p = stripParameter (control))
                p->beginChangeGesture();
        }
        else
        {
            owner.getUndoManager().beginNewTransaction (name);
        }
    };
    knob.onDragEnd = [this, control]
    {
        if (pluginMode)
            if (auto* p = stripParameter (control))
                p->endChangeGesture();
    };
    knob.onValueChange = [this, &knob, property, control]
    {
        if (pluginMode)
        {
            if (auto* p = stripParameter (control))
                p->setValueNotifyingHost ((float) knob.getValue());
        }
        else
        {
            track.setProperty (property, knob.getValue(), &owner.getUndoManager());
        }
    };
    knob.addMouseListener (this, false);   // right-click: the console menu
    addAndMakeVisible (knob);
}

//==============================================================================
// Console mode
//==============================================================================
juce::ValueTree ChannelStrip::stripInsert() const
{
    return isMaster() ? juce::ValueTree() : model::stripPlugin (project, track[ids::id].toString());
}

juce::AudioPluginInstance* ChannelStrip::stripInstance() const
{
    const auto insert = stripInsert();
    return insert.isValid() ? owner.getEffects().getPlugin (insert[ids::id].toString()) : nullptr;
}

int ChannelStrip::stripParameterIndex (console::StripControl c) const
{
    auto* p = stripInstance();
    if (p == nullptr)
        return -1;

    const auto& params = p->getParameters();
    const auto insert = stripInsert();
    const juce::Identifier key ("map" + juce::String ((int) c));

    if (insert.hasProperty (key))
    {
        const int index = insert[key];
        return juce::isPositiveAndBelow (index, params.size()) ? index : -1;
    }

    juce::StringArray names;
    for (auto* param : params)
        names.add (param->getName (64));
    return console::matchStripParameter (c, names);
}

juce::AudioProcessorParameter* ChannelStrip::stripParameter (console::StripControl c) const
{
    const auto index = stripParameterIndex (c);
    auto* p = stripInstance();
    return (p != nullptr && index >= 0) ? p->getParameters()[index] : nullptr;
}

juce::Slider* ChannelStrip::knobFor (console::StripControl c)
{
    // (the two switches are lamp keys, not knobs)
    switch (c)
    {
        case console::StripControl::hf:       return &hfKnob;
        case console::StripControl::hmf:      return &hmfKnob;
        case console::StripControl::lmf:      return &lmfKnob;
        case console::StripControl::lf:       return &lfKnob;
        case console::StripControl::dynamics: return &dynKnob;
        case console::StripControl::eqIn:
        case console::StripControl::dynIn:
        case console::StripControl::numControls: break;
    }
    return nullptr;
}

void ChannelStrip::applyMode()
{
    // Knobs work in the plugin's 0..1 range in console mode, in dB otherwise.
    for (int c = 0; c <= (int) console::StripControl::dynamics; ++c)
    {
        auto* knob = knobFor ((console::StripControl) c);
        if (knob == nullptr)
            continue;

        if (pluginMode)
        {
            auto* p = stripParameter ((console::StripControl) c);
            knob->setRange (0.0, 1.0, 0.0);
            knob->setDoubleClickReturnValue (p != nullptr, p != nullptr ? p->getDefaultValue() : 0.5);
            knob->setEnabled (p != nullptr || stripInstance() == nullptr);
            knob->setTooltip (p != nullptr ? p->getName (64) + "  (right-click for the console menu)"
                                           : juce::String ("Not assigned: right-click to choose a parameter"));
        }
        else
        {
            knob->setRange (knob->getProperties()["min"], knob->getProperties()["max"], 0.1);
            knob->setDoubleClickReturnValue (true, 0.0);
            knob->setEnabled (true);
        }
    }
}

void ChannelStrip::syncFromPlugin()
{
    if (! pluginMode || stripInstance() == nullptr)
        return;

    for (int c = 0; c <= (int) console::StripControl::dynamics; ++c)
        if (auto* knob = knobFor ((console::StripControl) c))
            if (auto* p = stripParameter ((console::StripControl) c))
                if (! knob->isMouseButtonDown() && std::abs (knob->getValue() - p->getValue()) > 1.0e-4)
                    knob->setValue (p->getValue(), juce::dontSendNotification);

    auto lamp = [this] (console::StripControl c, juce::Button& b)
    {
        if (auto* p = stripParameter (c))
        {
            const bool on = p->getValue() >= 0.5f;
            b.setToggleState (console::isBypassParameter (p->getName (64)) ? ! on : on, juce::dontSendNotification);
        }
        else
        {
            b.setToggleState (! (bool) stripInsert()[ids::bypass], juce::dontSendNotification);
        }
    };
    lamp (console::StripControl::eqIn, consoleInButton);
    lamp (console::StripControl::dynIn, dynInButton);

    if (auto* p = stripParameter (console::StripControl::dynamics))
        dynLabel.setText (p->getCurrentValueAsText().upToFirstOccurrenceOf (" ", false, false).substring (0, 6), juce::dontSendNotification);
}

void ChannelStrip::toggleStripSwitch (console::StripControl c, const juce::Identifier& builtInProperty, const juce::String& undoName)
{
    auto& um = owner.getUndoManager();

    if (! pluginMode)
    {
        um.beginNewTransaction (undoName);
        track.setProperty (builtInProperty, ! (bool) track.getProperty (builtInProperty, true), &um);
        return;
    }

    if (auto* p = stripParameter (c))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    else if (c == console::StripControl::eqIn)
    {
        // No switch on the plugin: take the whole plugin in or out.
        auto insert = stripInsert();
        um.beginNewTransaction (undoName);
        insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &um);
    }

    syncFromPlugin();
}

void ChannelStrip::showConsoleMenu()
{
    if (isMaster())
        return;

    auto& um = owner.getUndoManager();
    juce::ValueTree insert = stripInsert();
    const auto trackId = track[ids::id].toString();
    auto proj = project;
    auto t = track;
    juce::PopupMenu menu;

    if (insert.isValid())
        menu.addItem ("Open plugin window", [this, insert] { if (owner.onOpenInsert) owner.onOpenInsert (insert); });
    else
        menu.addItem ("Open channel strip window (all controls)...", [this] { if (owner.onOpenStripWindow) owner.onOpenStripWindow (track); });

    // This strip: built-in or a plugin
    const auto& candidates = owner.getStripCandidates();
    juce::PopupMenu use;
    use.addItem ("Built-in console strip", true, ! insert.isValid(), [&um, t, proj, trackId]() mutable
    {
        um.beginNewTransaction ("Built-in console strip");
        t.setProperty (ids::builtInStrip, true, &um);
        model::setStripPlugin (proj, trackId, {}, &um);
    });
    for (const auto& d : candidates)
    {
        const auto xml = d.createXml()->toString (juce::XmlElement::TextFormat().singleLine());
        use.addItem (d.name, true, insert[ids::pluginDescription].toString() == xml, [&um, t, proj, trackId, xml]() mutable
        {
            um.beginNewTransaction ("Console strip plugin");
            t.removeProperty (ids::builtInStrip, &um);
            model::setStripPlugin (proj, trackId, xml, &um);
        });
    }
    menu.addSubMenu ("This strip uses", use);

    if (auto* p = stripInstance())
    {
        juce::PopupMenu knobs;
        const auto& params = p->getParameters();
        for (int c = 0; c < (int) console::StripControl::numControls; ++c)
        {
            const auto control = (console::StripControl) c;
            const auto chosen = stripParameterIndex (control);
            juce::PopupMenu list;
            const juce::Identifier key ("map" + juce::String (c));
            list.addItem ("Nothing", true, chosen < 0, [&um, insert, key]() mutable
            {
                um.beginNewTransaction ("Assign knob");
                insert.setProperty (key, -1, &um);
            });
            for (int i = 0; i < params.size(); ++i)
                list.addItem (params[i]->getName (64), true, i == chosen, [&um, insert, key, i]() mutable
                {
                    um.beginNewTransaction ("Assign knob");
                    insert.setProperty (key, i, &um);
                });
            knobs.addSubMenu (console::stripControlName (control), list);
        }
        knobs.addSeparator();
        knobs.addItem ("Match by name again", [&um, insert]() mutable
        {
            um.beginNewTransaction ("Reset knob assignments");
            for (int c = 0; c < (int) console::StripControl::numControls; ++c)
                insert.removeProperty (juce::Identifier ("map" + juce::String (c)), &um);
        });
        menu.addSubMenu ("Knob assignments", knobs);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelStrip::SelectOnClick::mouseDown (const juce::MouseEvent& e)
{
    if (strip.owner.onSelectTrack && ! strip.isMaster() && ! e.mods.isPopupMenu())
        strip.owner.onSelectTrack (strip.track[ids::id].toString(), e.mods);
}

void ChannelStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    // Right-click anywhere in the console section (knobs, lamps, the metal around them).
    if (! e.mods.isPopupMenu() || isMaster())
        return;

    const auto inConsole = e.eventComponent == this ? e.y < topHeight
                                                    : e.eventComponent->getY() < topHeight && e.eventComponent->getParentComponent() == this;
    if (inConsole)
    {
        showConsoleMenu();
        return;
    }

    // Below it (the fader, the name): output, group, VCA, stacks and freeze.
    if (e.eventComponent == this || e.eventComponent == &fader || e.eventComponent == &meter)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (track[ids::name].toString());
        routing::addRoutingItems (menu, project, track, owner.getUndoManager());
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click the console section's panel: the whole strip in its own window.
    if (isMaster() || isVca() || e.eventComponent != this || e.y >= topHeight)
        return;
    if (auto insert = stripInsert(); insert.isValid())
    {
        if (owner.onOpenInsert)
            owner.onOpenInsert (insert);
    }
    else if (owner.onOpenStripWindow)
        owner.onOpenStripWindow (track);
}

juce::Rectangle<int> ChannelStrip::fromArt (float x, float y, float w, float h) const
{
    return juce::Rectangle<float> (x, y, w, h).transformedBy (juce::AffineTransform::scale (artScale)).getSmallestIntegerContainer();
}

juce::String ChannelStrip::getOwnerId() const
{
    return isMaster() ? model::masterOwner : track[ids::id].toString();
}

bool ChannelStrip::hasSends() const
{
    return ! isMaster() && ! model::isBusTrack (track) && ! isVca();
}

juce::ValueTree ChannelStrip::insertAt (int slot) const
{
    const auto inserts = model::insertsOf (model::findChain (project, getOwnerId()));
    return juce::isPositiveAndBelow (slot, inserts.size()) ? inserts[slot] : juce::ValueTree();
}

juce::ValueTree ChannelStrip::sendAt (int index) const
{
    const auto sends = model::sendsOf (model::findChain (project, getOwnerId()));
    return juce::isPositiveAndBelow (index, sends.size()) ? sends[index] : juce::ValueTree();
}

void ChannelStrip::showSendMenu (int index)
{
    auto& um = owner.getUndoManager();
    auto send = sendAt (index);
    const auto current = send[ids::target].toString();
    const auto ownerId = getOwnerId();
    auto proj = project;

    // Point the send at a bus (creating the send if this slot is empty).
    auto setTarget = [&um, proj, ownerId, send] (const juce::String& busId) mutable
    {
        um.beginNewTransaction ("Send");
        if (send.isValid())
            send.setProperty (ids::target, busId, &um);
        else
            model::addSend (model::chainFor (proj, ownerId), busId, juce::Decibels::decibelsToGain (-6.0), &um);
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Send to");

    for (const auto& t : project)
        if (t.hasType (ids::TRACK) && model::isBusTrack (t))
        {
            const auto busId = t[ids::id].toString();
            menu.addItem (t[ids::name].toString(), true, busId == current, [setTarget, busId]() mutable { setTarget (busId); });
        }

    menu.addItem ("New aux bus", [&um, proj, setTarget]() mutable
    {
        um.beginNewTransaction ("New aux bus");
        int buses = 0;
        for (const auto& t : proj)
            buses += (t.hasType (ids::TRACK) && model::isBusTrack (t)) ? 1 : 0;
        const auto bus = model::addTrack (proj, "Bus " + juce::String (buses + 1), &um, -1, model::busKind);
        setTarget (bus[ids::id].toString());
    });

    if (send.isValid())
    {
        menu.addSeparator();
        menu.addItem ("Pre-fader", true, (bool) send[ids::pre], [&um, send]() mutable
        {
            um.beginNewTransaction ("Pre-fader send");
            send.setProperty (ids::pre, ! (bool) send[ids::pre], &um);
        });
        menu.addItem ("Remove send", [&um, send]() mutable
        {
            um.beginNewTransaction ("Remove send");
            if (auto parent = send.getParent(); parent.isValid())
                parent.removeChild (send, &um);
        });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sendButtons[index]));
}

void ChannelStrip::refresh()
{
    // Console mode or the built-in strip?
    if (! isMaster())
    {
        const bool plugin = usesPlugin();
        if (plugin != pluginMode)
        {
            pluginMode = plugin;
            applyMode();
        }
        else if (pluginMode)
        {
            applyMode();   // the plugin may have just loaded
        }
    }

    if (! isMaster() && pluginMode)
    {
        for (int c = 0; c <= (int) console::StripControl::dynamics; ++c)
            if (auto* knob = knobFor ((console::StripControl) c))
            {
                auto* p = stripParameter ((console::StripControl) c);
                knob->setValue (p != nullptr ? p->getValue() : 0.5, juce::dontSendNotification);
            }
        syncFromPlugin();
        if (stripInstance() == nullptr)
            dynLabel.setText ("...", juce::dontSendNotification);
    }
    else if (! isMaster())
    {
        hfKnob.setValue (track.getProperty (ids::consoleHF, 0.0), juce::dontSendNotification);
        hmfKnob.setValue (track.getProperty (ids::consoleHMF, 0.0), juce::dontSendNotification);
        lmfKnob.setValue (track.getProperty (ids::consoleLMF, 0.0), juce::dontSendNotification);
        lfKnob.setValue (track.getProperty (ids::consoleLF, 0.0), juce::dontSendNotification);
        dynKnob.setValue (track.getProperty (ids::consoleDynamics, 0.0), juce::dontSendNotification);
        consoleInButton.setToggleState ((bool) track.getProperty (ids::consoleIn, true), juce::dontSendNotification);
        dynInButton.setToggleState ((bool) track.getProperty (ids::consoleDynIn, true), juce::dontSendNotification);
        dynLabel.setText (juce::String ((double) track.getProperty (ids::consoleDynamics, 0.0), 1), juce::dontSendNotification);
    }

    const auto gain = isMaster() ? (double) project.getProperty (ids::masterGain, 0.8)
                                 : (double) track.getProperty (ids::volume, 1.0);
    fader.setValue (gainToDb (gain), juce::dontSendNotification);
    const auto db = gainToDb (gain);
    dbLabel.setText (db <= minDb + 0.01 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);

    if (! isMaster())
    {
        panKnob.setValue (track.getProperty (ids::pan, 0.0), juce::dontSendNotification);
        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        stripInButton.setToggleState ((bool) track.getProperty (ids::stripIn, true), juce::dontSendNotification);
        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    }

    for (int i = 0; i < numSends && hasSends(); ++i)
    {
        const auto send = sendAt (i);
        const auto bus = model::findById (project, send[ids::target].toString());
        sendButtons[i]->setButtonText (bus.isValid() ? (bus[ids::name].toString() + ((bool) send[ids::pre] ? " (pre)" : ""))
                                                     : juce::String ("Send"));
        sendButtons[i]->setColour (juce::TextButton::textColourOffId, bus.isValid() ? juce::Colours::white : juce::Colour (0xffd6d9dc));
        sendButtons[i]->setColour (juce::TextButton::buttonColourId, bus.isValid() ? theme::stripSlotOn : theme::stripSlot);
        sendLevels[i]->setEnabled (bus.isValid());
        sendLevels[i]->setValue (gainToDb (send.getProperty (ids::level, 0.0)), juce::dontSendNotification);
    }

    for (int i = 0; i < numSlots; ++i)
    {
        const auto insert = insertAt (i);
        auto* b = slots[i];

        if (insert.isValid())
        {
            const bool off = insert[ids::bypass];
            b->setButtonText (insertName (insert));
            b->setColour (juce::TextButton::buttonColourId, off ? theme::stripSlot : theme::stripSlotOn);
            b->setColour (juce::TextButton::textColourOffId, off ? juce::Colour (0xff8d9196) : juce::Colours::white);
        }
        else
        {
            b->setButtonText ({});
            b->setColour (juce::TextButton::buttonColourId, theme::stripSlot);
            b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd6d9dc));
        }
    }

    repaint();
}

void ChannelStrip::mouseUp (const juce::MouseEvent& e)
{
    const auto index = slots.indexOf (dynamic_cast<juce::TextButton*> (e.eventComponent));
    if (index < 0 || ! e.mouseWasClicked())
        return;

    auto* button = slots[index];
    auto insert = insertAt (index);
    const auto ownerId = getOwnerId();

    if (! e.mods.isPopupMenu())
    {
        if (insert.isValid() && owner.onOpenInsert)
            owner.onOpenInsert (insert);
        else if (owner.onChooseEffect)
            owner.onChooseEffect (ownerId, index, {}, *button);
        return;
    }

    if (! insert.isValid())
    {
        if (owner.onChooseEffect)
            owner.onChooseEffect (ownerId, index, {}, *button);
        return;
    }

    auto& um = owner.getUndoManager();
    juce::PopupMenu menu;
    menu.addItem ("Open", [this, insert] { if (owner.onOpenInsert) owner.onOpenInsert (insert); });
    menu.addItem ("Bypass", true, (bool) insert[ids::bypass], [insert, &um]() mutable
    {
        um.beginNewTransaction ("Bypass");
        insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &um);
    });
    menu.addItem ("Replace...", [this, ownerId, index, insert, button]
    {
        if (owner.onChooseEffect)
            owner.onChooseEffect (ownerId, index, insert, *button);
    });
    menu.addSeparator();
    menu.addItem ("Remove", [insert, &um]
    {
        um.beginNewTransaction ("Remove effect");
        model::removeInsert (insert, &um);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button));
}

juce::Rectangle<int> ChannelStrip::faderArea() const
{
    // Below the sends, above the level display and the name plate.
    const auto top = topHeight + 6 + numSlots * 17 + 6 + numSends * 18 + 4 + automationRow;
    return { 0, top, getWidth(), getHeight() - nameHeight - 22 - top };
}

void ChannelStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto w = (float) getWidth();

    // The strip: a light grey brushed face, lit a touch from the left.
    {
        juce::ColourGradient face (theme::stripFace.brighter (0.06f), 0.0f, 0.0f, theme::stripFaceLow, w, 0.0f, false);
        face.addColour (0.35, theme::stripFace);
        g.setGradientFill (face);
        g.fillRect (bounds);

        // Brushed grain: faint horizontal lines.
        juce::Random grain (getOwnerId().hashCode());
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + grain.nextFloat() * 0.02f));
            g.fillRect (0, y, getWidth(), 1);
        }

        // Shade towards the bottom, and a raised edge at the top.
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, (float) getHeight() * 0.55f,
                                                 juce::Colours::black.withAlpha (0.08f), 0.0f, (float) getHeight(), false));
        g.fillRect (bounds);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillRect (1, 0, getWidth() - 3, 1);
    }

    const auto text = theme::stripInk, dim = theme::stripInkDim;
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (artScale));

        if (isMaster() || isVca())
        {
            const bool folder = isVca() && track[ids::stack].toString() == "folder";
            g.setColour (text);
            g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
            g.drawText (isMaster() ? "MAIN" : folder ? "STACK" : "VCA", juce::Rectangle<float> (0.0f, 90.0f, strip::artWidth, 50.0f), juce::Justification::centred, false);
            g.setColour (dim);
            g.setFont (juce::FontOptions (28.0f));
            juce::String sub = "Stereo Out";
            if (isVca())
            {
                const auto n = model::vcaMembers (project, track).size();
                sub = juce::String (n) + (n == 1 ? " track" : " tracks");
            }
            g.drawText (sub, juce::Rectangle<float> (0.0f, 140.0f, strip::artWidth, 40.0f), juce::Justification::centred, false);
        }
        else
        {
            // Band names beside the knobs
            struct Spot { juce::Slider* knob; const char* band; const char* freq; };
            const Spot spots[] = { { &hfKnob, "HF", "8k" }, { &hmfKnob, "HMF", "2.5k" }, { &lmfKnob, "LMF", "400" }, { &lfKnob, "LF", "100" } };

            for (const auto& spot : spots)
            {
                const auto c = spot.knob->getBounds().toFloat().transformedBy (juce::AffineTransform::scale (1.0f / artScale)).getCentre();
                g.setColour (text);
                g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
                g.drawText (spot.band, juce::Rectangle<float> (131.0f, c.y - 22.0f, 62.0f, 26.0f), juce::Justification::centredLeft, false);
                g.setColour (dim);
                g.setFont (juce::FontOptions (20.0f));
                g.drawText (spot.freq, juce::Rectangle<float> (131.0f, c.y + 2.0f, 62.0f, 22.0f), juce::Justification::centredLeft, false);
            }

            // The channel number, top left (as printed on a console)
            if (! isVca())
            {
                g.setColour (text);
                g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
                g.drawText (juce::String (track.getParent().indexOf (track) + 1), juce::Rectangle<float> (14.0f, 74.0f, 60.0f, 32.0f),
                            juce::Justification::centredLeft, false);
            }

            // Console mode: which strip this is
            g.setColour (pluginMode ? theme::knobArc.darker (0.3f) : dim);
            g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawText (pluginMode ? (stripInstance() != nullptr ? "PLUG" : "...") : juce::String ("EQ"),
                        juce::Rectangle<float> (64.0f, 30.0f, 76.0f, 40.0f), juce::Justification::centredRight, false);
            g.setColour (dim);
            g.drawText ("DYN", juce::Rectangle<float> (90.0f, 440.0f, 50.0f, 34.0f), juce::Justification::centredRight, false);

            // Section line, then Dynamics
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (10.0f, 659.0f, strip::artWidth - 20.0f, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillRect (10.0f, 661.0f, strip::artWidth - 20.0f, 1.5f);
            g.setColour (text);
            g.setFont (juce::FontOptions (30.0f));
            g.drawText ("Dynamics", juce::Rectangle<float> (0.0f, 672.0f, strip::artWidth, 40.0f), juce::Justification::centred, false);
        }
    }

    // Fader scale beside the slot
    {
        const auto area = faderArea();
        g.setFont (juce::FontOptions (9.5f));
        const bool roomy = fader.getHeight() > 170;
        for (const auto db : { 6, 0, -5, -10, -15, -20, -30, -40, -60 })
        {
            if (! roomy && (db == -5 || db == -15 || db == -30))
                continue;   // short fader: fewer numbers
            const auto y = (float) fader.getY() + fader.yForValue (db);
            if (y < (float) area.getY() || y > (float) area.getBottom())
                continue;
            g.setColour ((db == 0 ? text : dim).withAlpha (0.9f));
            g.fillRect ((float) fader.getRight() - 6.0f, y - 0.5f, 5.0f, 1.0f);
            g.drawText (db == 0 ? juce::String ("U") : juce::String (std::abs (db)),
                        juce::Rectangle<float> ((float) fader.getRight() - 1.0f, y - 6.0f, 18.0f, 12.0f), juce::Justification::centred, false);
        }
    }

    // The level display: a dark window in the face
    {
        const auto r = dbLabel.getBounds().toFloat();
        g.setColour (juce::Colour (0xff2a2d30));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (r.getX() + 2.0f, r.getBottom() + 0.5f, r.getRight() - 2.0f, r.getBottom() + 0.5f, 1.0f);
    }

    // The name, printed at the foot of the strip, with the track's colour as a stripe above it
    const auto plate = bounds.toFloat().withTop ((float) (getHeight() - nameHeight)).reduced (3.0f, 2.0f);
    const auto base = isMaster() ? theme::knobArc : model::trackColour (track);
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.fillRoundedRectangle (plate, 2.0f);
    g.setColour (base.withAlpha (selected ? 1.0f : 0.85f));
    g.fillRoundedRectangle (plate.withHeight (3.0f), 1.0f);

    const auto name = isMaster() ? juce::String ("Master") : track[ids::name].toString();
    g.setColour (text);
    g.setFont (juce::FontOptions (13.5f));
    g.drawFittedText (name, plate.withTrimmedTop (3.0f).reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);

    // The selected track: a blue frame round the strip
    if (selected)
    {
        g.setColour (theme::knobArc.withAlpha (0.3f));
        g.drawRect (bounds.toFloat().reduced (1.0f), 4.0f);
        g.setColour (theme::knobArc);
        g.drawRect (bounds.toFloat().reduced (1.5f), 1.6f);
    }

    // Edges between strips: a light bevel and a dark gap
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colour (0xff2a2c2f));
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
}

void ChannelStrip::resized()
{
    if (! isMaster())
    {
        auto knobAt = [this] (juce::Slider& k, float x, float y, float r) { k.setBounds (fromArt (x - r, y - r, r * 2.0f, r * 2.0f)); };
        knobAt (hfKnob, 79.0f, 146.0f, 41.0f);
        knobAt (hmfKnob, 76.0f, 276.0f, 41.0f);
        knobAt (lmfKnob, 77.0f, 391.0f, 41.0f);
        knobAt (lfKnob, 76.0f, 529.0f, 41.0f);
        knobAt (dynKnob, 98.0f, 762.0f, 43.0f);

        consoleInButton.setBounds (fromArt (145.0f, 33.0f, 34.0f, 34.0f));
        stripInButton.setBounds (fromArt (14.0f, 31.0f, 52.0f, 38.0f));
        dynInButton.setBounds (fromArt (145.0f, 440.0f, 34.0f, 34.0f));
        muteButton.setBounds (fromArt (32.0f, 610.0f, 61.0f, 40.0f));
        soloButton.setBounds (fromArt (101.0f, 610.0f, 60.0f, 40.0f));
        dynLabel.setBounds (fromArt (24.0f, 816.0f, 71.0f, 44.0f));
        dynLabel.setFont (juce::FontOptions ((float) dynLabel.getHeight() * 0.56f));
        panKnob.setBounds (fromArt (107.0f, 816.0f, 66.0f, 44.0f));
    }

    auto area = getLocalBounds().reduced (4, 0).withTrimmedTop (topHeight + 6);

    for (auto* b : slots)
    {
        b->setBounds (area.removeFromTop (15));
        area.removeFromTop (2);
    }

    area.removeFromTop (4);

    for (int i = 0; i < numSends; ++i)
    {
        auto row = area.removeFromTop (16);
        if (hasSends())
        {
            sendLevels[i]->setBounds (row.removeFromRight (16));
            row.removeFromRight (2);
            sendButtons[i]->setBounds (row);
        }
        area.removeFromTop (2);
    }

    // Fader on the left half, meter on the right, the level display under the meter
    const auto fa = faderArea();
    fader.setBounds (juce::Rectangle<int> (4, fa.getY(), (int) (getWidth() * 0.5f), fa.getHeight()));
    meter.setBounds (juce::Rectangle<int> ((int) (getWidth() * 0.73f), fa.getY() + 6, 12, fa.getHeight() - 12));
    dbLabel.setBounds (juce::Rectangle<int> (getWidth() - 46, fa.getBottom() + 2, 40, 17));
    if (! isMaster())
        automationButton.setBounds (juce::Rectangle<int> (6, fa.getY() - automationRow + 1, getWidth() - 12, automationRow - 3));
}

void ChannelStrip::updateAutomationButton()
{
    if (isMaster())
        return;
    const auto mode = AutomationWriter::modeOf (track);
    automationButton.setButtonText (mode.substring (0, 1).toUpperCase() + mode.substring (1));
    const auto colour = mode == "read"  ? juce::Colour (0xff2e9b4f)
                      : mode == "touch" ? juce::Colour (0xffd08a1c)
                      : mode == "latch" ? juce::Colour (0xff8a4fd0)
                      : mode == "write" ? juce::Colour (0xffd23b36)
                                        : theme::slotDark;
    automationButton.setColour (juce::TextButton::buttonColourId, colour);
    automationButton.setColour (juce::TextButton::textColourOffId, mode == "off" ? theme::dimText : juce::Colours::white);
}

void ChannelStrip::showAutomationModeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Automation");
    const auto current = AutomationWriter::modeOf (track);
    for (auto [mode, name] : { std::pair { "off", "Off" }, std::pair { "read", "Read" }, std::pair { "touch", "Touch" },
                               std::pair { "latch", "Latch" }, std::pair { "write", "Write" } })
    {
        menu.addItem (name, true, current == mode, [this, m = juce::String (mode)]
        {
            // Every selected track follows, as in Logic.
            auto chosen = owner.getSelectedTrackIds();
            if (! selected || ! chosen.contains (track[ids::id].toString()))
                chosen = { track[ids::id].toString() };
            for (const auto& id : chosen)
                if (auto t = model::findById (project, id); t.isValid())
                    t.setProperty (ids::automationMode, m, nullptr);
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automationButton));
}

void ChannelStrip::followAutomation (double time)
{
    if (isMaster())
        return;
    updateAutomationButton();

    auto* w = AutomationWriter::current();
    if (w == nullptr)
        return;

    const auto id = track[ids::id].toString();
    if (! fader.isMouseButtonDown())
    {
        const auto gain = w->displayedValue (track, model::volumeParam, time);
        const auto db = gainToDb (gain);
        if (std::abs (db - fader.getValue()) > 0.05)
        {
            fader.setValue (db, juce::dontSendNotification);
            dbLabel.setText (db <= minDb + 0.01 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
        }
    }
    if (! panKnob.isMouseButtonDown())
    {
        const auto pan = w->displayedValue (track, model::panParam, time);
        if (std::abs (pan - panKnob.getValue()) > 0.005)
            panKnob.setValue (pan, juce::dontSendNotification);
    }
}

//==============================================================================
// Master Bus Compressor panel
//==============================================================================
MasterBusCompressorPanel::MasterBusCompressorPanel (juce::UndoManager& um, AudioEngine& e)
    : undoManager (um), engine (e)
{
    addAndMakeVisible (vu);

    using C = MasterBusCompressor;
    setup (threshold, ids::busCompThreshold, look::Knob::blue, C::minThresholdDb, C::maxThresholdDb, 0.1,
           [] (double v) { return juce::String (v, 1) + " dB"; }, "Threshold");
    setup (makeup, ids::busCompMakeup, look::Knob::black, 0.0, C::maxMakeupDb, 0.1,
           [] (double v) { return "+" + juce::String (v, 1) + " dB"; }, "Make-up");
    setup (attack, ids::busCompAttack, look::Knob::black, 0.0, (double) C::attacksMs.size() - 1, 1.0,
           [] (double v) { return C::attackText ((int) v) + " ms"; }, "Attack");
    setup (release, ids::busCompRelease, look::Knob::black, 0.0, (double) C::releasesS.size() - 1, 1.0,
           [] (double v) { return (int) v == C::autoRelease ? juce::String ("Auto") : C::releaseText ((int) v) + " s"; }, "Release");
    setup (ratio, ids::busCompRatio, look::Knob::black, 0.0, (double) C::ratios.size() - 1, 1.0,
           [] (double v) { return C::ratioText ((int) v) + ":1"; }, "Ratio");
    setup (mix, ids::busCompMix, look::Knob::cream, 0.0, 1.0, 0.01,
           [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "% wet"; }, "Mix");

    threshold.setDoubleClickReturnValue (true, -10.0);
    makeup.setDoubleClickReturnValue (true, 0.0);
    mix.setDoubleClickReturnValue (true, 1.0);

    look::key (inButton, look::Key::cream);
    inButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    inButton.setColour (juce::TextButton::buttonOnColourId, theme::knobArc);
    inButton.setWantsKeyboardFocus (false);
    inButton.setTooltip ("Switch the Master Bus Compressor in or out");
    inButton.onClick = [this]
    {
        undoManager.beginNewTransaction ("Master Bus Compressor in");
        MasterBusCompressor::setInProject (project, ids::busCompOn, ! MasterBusCompressor::fromProject (project).enabled, &undoManager);
    };
    addAndMakeVisible (inButton);
}

void MasterBusCompressorPanel::setup (juce::Slider& knob, const juce::Identifier& property, look::Knob cap, double min, double max,
                                      double step, std::function<juce::String (double)> text, const juce::String& name)
{
    look::knob (knob, cap);
    look::onLight (knob, false);
    knob.setRange (min, max, step);
    knob.setScrollWheelEnabled (false);
    knob.setPopupDisplayEnabled (true, true, nullptr);
    knob.textFromValueFunction = text;
    knob.setTooltip (name);
    knob.onDragStart = [this, name] { undoManager.beginNewTransaction ("Master Bus Compressor " + name.toLowerCase()); };
    knob.onValueChange = [this, &knob, property, step]
    {
        const auto value = knob.getValue();
        MasterBusCompressor::setInProject (project, property, step >= 1.0 ? juce::var ((int) value) : juce::var (value), &undoManager);
        repaint();
    };
    addAndMakeVisible (knob);
    captions.push_back ({ &knob, name, std::move (text) });
}

void MasterBusCompressorPanel::setProject (juce::ValueTree newProject)
{
    project = newProject;
    refresh();
}

void MasterBusCompressorPanel::refresh()
{
    if (! project.isValid())
        return;

    const auto s = MasterBusCompressor::fromProject (project);
    threshold.setValue (s.thresholdDb, juce::dontSendNotification);
    makeup.setValue (s.makeupDb, juce::dontSendNotification);
    attack.setValue (s.attack, juce::dontSendNotification);
    release.setValue (s.release, juce::dontSendNotification);
    ratio.setValue (s.ratio, juce::dontSendNotification);
    mix.setValue (s.mix, juce::dontSendNotification);
    inButton.setToggleState (s.enabled, juce::dontSendNotification);
    repaint();
}

void MasterBusCompressorPanel::updateMeter()
{
    vu.push (engine.takeBusCompGainReduction());
}

void MasterBusCompressorPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // The centre section: a black panel.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222326), 0.0f, 0.0f,
                                             juce::Colour (0xff141517), (float) getWidth(), (float) getHeight(), false));
    g.fillRect (bounds);
    g.setColour (theme::consoleDark);
    g.fillRect (0, 0, 1, getHeight());
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    // Title under the meter
    g.setColour (theme::ink);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("MASTER BUS", juce::Rectangle<int> (0, vu.getBottom() + 6, getWidth(), 15), juce::Justification::centred, false);
    g.drawText ("COMPRESSOR", juce::Rectangle<int> (0, vu.getBottom() + 21, getWidth(), 15), juce::Justification::centred, false);

    // The control box
    const auto box = juce::Rectangle<int> (10, threshold.getY() - 10, getWidth() - 20, mix.getBottom() + 34 - (threshold.getY() - 10)).toFloat();
    g.setColour (theme::consoleShade.darker (0.06f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (theme::consoleDark);
    g.drawRoundedRectangle (box, 8.0f, 1.2f);

    for (const auto& c : captions)
    {
        const auto r = c.knob->getBounds();
        g.setColour (theme::ink);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (c.name, r.translated (0, r.getHeight() - 2).withHeight (13).expanded (12, 0), juce::Justification::centred, false);
        g.setColour (theme::inkDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (c.text (c.knob->getValue()), r.translated (0, r.getHeight() + 10).withHeight (12).expanded (12, 0),
                    juce::Justification::centred, false);
    }

    // IN lamp
    const auto lamp = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) inButton.getRight() + 22.0f, (float) inButton.getBounds().getCentreY() });
    const bool on = inButton.getToggleState();
    g.setColour (on ? theme::knobArc.brighter (0.4f) : juce::Colour (0xff1c2a3d));
    g.fillRoundedRectangle (lamp, 2.0f);
    if (on)
    {
        g.setColour (theme::knobArc.withAlpha (0.35f));
        g.fillRoundedRectangle (lamp.expanded (3.0f), 4.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (lamp, 2.0f, 1.0f);

    g.setColour (theme::ink);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("Bypass / In", inButton.getBounds().translated (0, inButton.getHeight() + 2).withHeight (12).expanded (20, 0),
                juce::Justification::centred, false);
}

void MasterBusCompressorPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    vu.setBounds (area.removeFromTop (118).withSizeKeepingCentre (juce::jmin (area.getWidth(), 220), 118));
    area.removeFromTop (48);

    constexpr int knob = 46, rowHeight = 74;
    auto column = [&] (juce::Rectangle<int> row, bool left)
    {
        return (left ? row.removeFromLeft (row.getWidth() / 2) : row.withTrimmedLeft (row.getWidth() / 2)).withSizeKeepingCentre (knob, knob);
    };

    for (auto [l, r] : { std::pair { &threshold, &makeup }, std::pair { &attack, &release }, std::pair { &ratio, &mix } })
    {
        auto row = area.removeFromTop (rowHeight);
        l->setBounds (column (row, true).withY (row.getY()));
        r->setBounds (column (row, false).withY (row.getY()));
    }

    area.removeFromTop (30);
    inButton.setBounds (area.removeFromTop (34).withSizeKeepingCentre (46, 32).translated (-18, 0));
}

//==============================================================================
// MixerPanel
//==============================================================================
MixerPanel::MixerPanel (juce::UndoManager& um, AudioEngine& e, ProjectPlayer& p, EffectRack& fx)
    : undoManager (um), engine (e), player (p), effects (fx), busComp (um, e), consolePanel (um, e, fx)
{
    // Console mode for the strips: the built-in strip, or a channel strip plugin you own
    consoleCaption.setFont (juce::FontOptions (12.0f));
    consoleCaption.setColour (juce::Label::textColourId, theme::dimText);
    consoleCaption.setJustificationType (juce::Justification::centredRight);
    addChildComponent (consoleCaption);
    consoleBox.setTooltip ("The channel strip every track uses: the built-in one, or your own UA channel strip plugin");
    consoleBox.onChange = [this]
    {
        const auto index = consoleBox.getSelectedItemIndex();
        if (onChooseConsole)
            onChooseConsole (index > 0 && index - 1 < stripCandidates.size() ? &stripCandidates.getReference (index - 1) : nullptr);
    };
    addChildComponent (consoleBox);

    filterBox.addItemList ({ "All", "Audio", "Instrument", "Aux", "VCA" }, 1);
    filterBox.setSelectedId (1, juce::dontSendNotification);
    filterBox.setTooltip ("Show every strip, or only one kind (as Logic's mixer filters)");
    filterBox.setWantsKeyboardFocus (false);
    filterBox.onChange = [this] { rebuild(); };
    addAndMakeVisible (filterBox);

    narrowButton.setClickingTogglesState (true);
    narrowButton.setTooltip ("Narrow strips: more channels on screen at once");
    narrowButton.setWantsKeyboardFocus (false);
    narrowButton.onClick = [this]
    {
        project.setProperty ("mixerNarrow", narrowButton.getToggleState(), nullptr);   // (kept with the project, not an undo step)
        resized();
    };
    addAndMakeVisible (narrowButton);

    title.setText ("Mixer", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);

    look::key (closeButton, look::Key::dark);
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    body.addAndMakeVisible (viewport);
    body.addAndMakeVisible (busComp);
    body.addChildComponent (consolePanel);

    outer.setViewedComponent (&body, false);
    outer.setScrollBarsShown (true, false);
    addAndMakeVisible (outer);

    startTimerHz (30);
}

MixerPanel::~MixerPanel()
{
    project.removeListener (this);
}

void MixerPanel::setProject (juce::ValueTree newProject)
{
    project.removeListener (this);
    project = newProject;
    project.addListener (this);
    busComp.setProject (project);
    consolePanel.setProject (project);
    narrowButton.setToggleState ((bool) project.getProperty ("mixerNarrow", false), juce::dontSendNotification);
    refreshConsoleBox();
    rebuild();
}

float MixerPanel::stripScale() const
{
    return narrowButton.getToggleState() ? 0.7f : 1.0f;
}

void MixerPanel::changed (const juce::ValueTree& parent)
{
    if (parent == project)
        needsRebuild = true; // a track was added, removed or moved

    triggerAsyncUpdate();
}

void MixerPanel::setStripCandidates (const juce::Array<juce::PluginDescription>& found)
{
    stripCandidates = found;
    refreshConsoleBox();
    resized();
}

void MixerPanel::refreshConsoleBox()
{
    consoleBox.clear (juce::dontSendNotification);
    consoleBox.addItem ("Built-in", 1);

    const auto current = model::consolePlugin (project);
    int selected = 1;
    for (int i = 0; i < stripCandidates.size(); ++i)
    {
        consoleBox.addItem (stripCandidates[i].name, i + 2);
        if (auto xml = stripCandidates[i].createXml(); xml != nullptr && xml->toString (juce::XmlElement::TextFormat().singleLine()) == current)
            selected = i + 2;
    }

    if (current.isNotEmpty() && selected == 1)
    {
        // The project uses a plugin this computer doesn't have.
        juce::PluginDescription d;
        if (auto xml = juce::parseXML (current); xml != nullptr && d.loadFromXml (*xml))
            consoleBox.addItem (d.name + " (not installed)", 99);
        selected = 99;
    }

    consoleBox.setSelectedId (selected, juce::dontSendNotification);
    const bool show = ! stripCandidates.isEmpty() || current.isNotEmpty();
    consoleBox.setVisible (show);
    consoleCaption.setVisible (show);
}

void MixerPanel::handleAsyncUpdate()
{
    busComp.refresh();
    consolePanel.refresh();
    refreshConsoleBox();

    // The console face appears once a plugin is chosen or found.
    if (consolePanel.isAvailable() != consolePanel.isVisible())
        resized();

    if (std::exchange (needsRebuild, false))
    {
        rebuild();
        return;
    }

    for (auto* s : strips)
        s->refresh();
    if (masterStrip != nullptr)
        masterStrip->refresh();
}

void MixerPanel::setSelectedTracks (const juce::StringArray& trackIds, const juce::String& trackId)
{
    selectedTrackId = trackId;
    selectedTrackIds = trackIds;

    for (auto* s : strips)
    {
        const auto id = s->getTrack()[ids::id].toString();
        s->setSelected (trackIds.contains (id) || id == trackId);

        // Scroll the main one into view.
        if (id == trackId)
        {
            const auto x = s->getX(), right = s->getRight();
            const auto viewX = viewport.getViewPositionX(), viewW = viewport.getViewWidth();
            if (x < viewX)
                viewport.setViewPosition (x, viewport.getViewPositionY());
            else if (right > viewX + viewW)
                viewport.setViewPosition (right - viewW, viewport.getViewPositionY());
        }
    }
}

void MixerPanel::rebuild()
{
    strips.clear();

    const auto filter = filterBox.getSelectedId();
    const auto shows = [filter] (const juce::ValueTree& track)
    {
        switch (filter)
        {
            case 2: return track[ids::kind].toString() == model::audioKind || track[ids::kind].toString().isEmpty();
            case 3: return model::isInstrumentTrack (track);
            case 4: return model::isBusTrack (track);
            case 5: return model::isVcaTrack (track);
            default: return true;
        }
    };

    for (const auto& track : project)
        if (track.hasType (ids::TRACK) && shows (track))
        {
            auto* strip = strips.add (new ChannelStrip (*this, project, track));
            strip->setSelected (track[ids::id].toString() == selectedTrackId || selectedTrackIds.contains (track[ids::id].toString()));
            strip->setComponentID ("strip" + juce::String (strips.size() - 1));
            stripHolder.addAndMakeVisible (strip);
        }

    masterStrip = std::make_unique<ChannelStrip> (*this, project, juce::ValueTree());
    body.addAndMakeVisible (*masterStrip);
    resized();
}

void MixerPanel::timerCallback()
{
    bool extraShowing = false;
    for (auto* s : extraStrips)
        extraShowing = extraShowing || s->isShowing();

    if (! isShowing() && ! extraShowing)
        return;

    // Each track's level is read once and goes to every strip showing that track.
    std::map<juce::String, std::pair<float, float>> levels;
    auto levelOf = [this, &levels] (const juce::String& owner)
    {
        if (auto it = levels.find (owner); it != levels.end())
            return it->second;
        return levels[owner] = engine.takeTrackLevel (player.slotOf (owner));
    };

    const auto playhead = engine.getPlayheadSeconds();
    for (auto* s : strips)
    {
        const auto [l, r] = levelOf (s->getOwnerId());
        s->pushLevels (l, r);
        s->followAutomation (playhead);
    }
    for (auto* s : extraStrips)
        if (s->getTrack().isValid())
        {
            const auto [l, r] = levelOf (s->getOwnerId());
            s->pushLevels (l, r);
            s->followAutomation (playhead);
        }

    if (! isShowing())
    {
        if (++syncCounter % 3 == 0)
            for (auto* s : extraStrips)
                s->syncFromPlugin();
        return;
    }

    if (consolePanel.isVisible())
        consolePanel.update();
    else
        busComp.updateMeter();

    // Console mode: follow knobs turned in the plugins' own windows (10 times a second).
    if (++syncCounter % 3 == 0)
    {
        for (auto* s : strips)
            s->syncFromPlugin();
        for (auto* s : extraStrips)
            s->syncFromPlugin();
    }
}

void MixerPanel::setMasterLevels (float left, float right)
{
    if (masterStrip != nullptr)
        masterStrip->pushLevels (left, right);
    for (auto* s : extraStrips)
        if (! s->getTrack().isValid())
            s->pushLevels (left, right);
    consolePanel.setOutputLevel (juce::jmax (left, right));
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2a2d));   // the console's frame, behind the strips

    const auto header = getLocalBounds().removeFromTop (headerHeight).toFloat();
    g.setGradientFill (juce::ColourGradient (theme::toolbar.brighter (0.1f), 0.0f, 0.0f, theme::toolbarDark, 0.0f, header.getBottom(), false));
    g.fillRect (header);
    g.setColour (juce::Colours::black);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (16);
    consoleBox.setBounds (header.removeFromRight (240));
    consoleCaption.setBounds (header.removeFromRight (60));
    title.setBounds (header.removeFromLeft (60));
    filterBox.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (8);
    narrowButton.setBounds (header.removeFromLeft (64));

    // Narrow strips are the same strips, drawn smaller.
    const auto scale = stripScale();
    const auto stripW = (int) std::ceil (ChannelStrip::stripWidth * scale);
    const auto minimumHeight = (int) std::ceil (ChannelStrip::minimumHeight * scale);

    // Everything scrolls up and down together if the pane is shorter than a strip.
    outer.setBounds (area);
    const auto tall = area.getHeight() < minimumHeight;
    const auto width = area.getWidth() - (tall ? outer.getScrollBarThickness() : 0);
    body.setSize (width, juce::jmax (area.getHeight(), minimumHeight));

    auto content = body.getLocalBounds();

    auto place = [scale] (juce::Component& c, juce::Rectangle<int> r)
    {
        c.setTransform (juce::AffineTransform::scale (scale));
        c.setBounds (juce::Rectangle<float> ((float) r.getX() / scale, (float) r.getY() / scale,
                                             (float) ChannelStrip::stripWidth, (float) r.getHeight() / scale).toNearestInt());
    };

    if (masterStrip != nullptr)
        place (*masterStrip, content.removeFromRight (stripW));

    // Console mode shows the user's own bus compressor; otherwise the built-in one.
    const bool console = consolePanel.isAvailable();
    consolePanel.setVisible (console);
    busComp.setVisible (! console);

    if (console)
        consolePanel.setBounds (content.removeFromRight (consolePanel.widthForHeight (content.getHeight())));
    else
        busComp.setBounds (content.removeFromRight (MasterBusCompressorPanel::panelWidth));

    viewport.setBounds (content);

    const auto wide = strips.size() * stripW > content.getWidth();
    const auto height = content.getHeight() - (wide ? viewport.getScrollBarThickness() : 0);
    stripHolder.setSize (juce::jmax (content.getWidth(), strips.size() * stripW), height);

    for (int i = 0; i < strips.size(); ++i)
        place (*strips[i], { i * stripW, 0, stripW, stripHolder.getHeight() });
}

} // namespace riff
