#include "Mixer.h"
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

        muteButton.setTooltip ("Mute");
        soloButton.setTooltip ("Solo");
        muteButton.onClick = [this]
        {
            owner.getUndoManager().beginNewTransaction ("Mute");
            track.setProperty (ids::mute, ! (bool) track[ids::mute], &owner.getUndoManager());
        };
        soloButton.onClick = [this]
        {
            owner.getUndoManager().beginNewTransaction ("Solo");
            track.setProperty (ids::solo, ! (bool) track[ids::solo], &owner.getUndoManager());
        };
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);

        // The two displays under Dynamics: its amount, and pan (drag sideways)
        dynLabel.setJustificationType (juce::Justification::centred);
        dynLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe9e6dc));
        dynLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (dynLabel);

        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.textFromValueFunction = [] (double value)
        {
            const auto amount = juce::roundToInt (std::abs (value) * 64.0);
            return amount == 0 ? juce::String ("C") : (value < 0 ? "L" : "R") + juce::String (amount);
        };
        panKnob.setDoubleClickReturnValue (true, 0.0);
        panKnob.setTooltip ("Pan: drag left or right; double-click to centre");
        panKnob.onDragStart = [this] { owner.getUndoManager().beginNewTransaction ("Pan"); };
        panKnob.onValueChange = [this] { track.setProperty (ids::pan, panKnob.getValue(), &owner.getUndoManager()); };
        addAndMakeVisible (panKnob);
    }

    for (int i = 0; i < numSlots; ++i)
    {
        auto* b = slots.add (new juce::TextButton());
        look::key (*b, look::Key::slot);
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Click to add an effect, or open this one. Right-click for bypass and remove.");
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
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
    fader.onDragStart = [this] { owner.getUndoManager().beginNewTransaction (isMaster() ? "Master volume" : "Volume"); };
    fader.onValueChange = [this]
    {
        const auto gain = dbToGain (fader.getValue());
        if (isMaster())
            project.setProperty (ids::masterGain, gain, &owner.getUndoManager());
        else
            track.setProperty (ids::volume, gain, &owner.getUndoManager());
    };
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::FontOptions (11.0f));
    dbLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe9e6dc));
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

void ChannelStrip::SelectOnClick::mouseDown (const juce::MouseEvent&)
{
    if (strip.owner.onSelectTrack && ! strip.isMaster())
        strip.owner.onSelectTrack (strip.track[ids::id].toString());
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
        showConsoleMenu();
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
    return ! isMaster() && ! model::isBusTrack (track);
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
        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    }

    for (int i = 0; i < numSends && hasSends(); ++i)
    {
        const auto send = sendAt (i);
        const auto bus = model::findById (project, send[ids::target].toString());
        sendButtons[i]->setButtonText (bus.isValid() ? (bus[ids::name].toString() + ((bool) send[ids::pre] ? " (pre)" : ""))
                                                     : juce::String ("Send"));
        sendButtons[i]->setColour (juce::TextButton::textColourOffId, bus.isValid() ? theme::text : theme::dimText);
        sendButtons[i]->setColour (juce::TextButton::buttonColourId, bus.isValid() ? juce::Colour (0xff3a4a3f) : theme::slotDark);
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
            b->setColour (juce::TextButton::buttonColourId, off ? theme::slotDark : juce::Colour (0xff35466e));
            b->setColour (juce::TextButton::textColourOffId, off ? theme::dimText : theme::text);
        }
        else
        {
            b->setButtonText ({});
            b->setColour (juce::TextButton::buttonColourId, theme::slotDark);
            b->setColour (juce::TextButton::textColourOffId, theme::dimText);
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
    const auto top = topHeight + 6 + numSlots * 17 + 6 + numSends * 18 + 4;
    return { 0, top, getWidth(), getHeight() - nameHeight - 22 - top };
}

void ChannelStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // Metal plate the whole way down, the artwork on top
    g.setOpacity (1.0f);
    g.drawImage (strip::plateArt(), bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
    if (! isMaster())
        g.drawImage (strip::topArt(), juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) topHeight),
                     juce::RectanglePlacement::stretchToFit);

    const auto ink = theme::ink;
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (artScale));

        if (isMaster())
        {
            g.setColour (ink);
            g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
            g.drawText ("MASTER", juce::Rectangle<float> (0.0f, 90.0f, strip::artWidth, 50.0f), juce::Justification::centred, false);
            g.setFont (juce::FontOptions (30.0f));
            g.drawText ("Stereo Out", juce::Rectangle<float> (0.0f, 140.0f, strip::artWidth, 40.0f), juce::Justification::centred, false);
        }
        else
        {
            // Dots around the knobs, band names beside them, lamp captions
            struct Spot { juce::Slider* knob; const char* band; const char* freq; };
            const Spot spots[] = { { &hfKnob, "HF", "8k" }, { &hmfKnob, "HMF", "2.5k" }, { &lmfKnob, "LMF", "400" }, { &lfKnob, "LF", "100" },
                                   { &dynKnob, nullptr, nullptr } };

            for (const auto& spot : spots)
            {
                const auto r = spot.knob->getBounds().toFloat().transformedBy (juce::AffineTransform::scale (1.0f / artScale));
                const auto c = r.getCentre();
                const auto dots = r.getWidth() * 0.5f + 9.0f;
                g.setColour (ink.withAlpha (0.8f));
                for (int d = 0; d <= 10; ++d)
                {
                    const auto a = juce::degreesToRadians (-140.0f + 28.0f * (float) d);
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c + juce::Point<float> (dots * std::sin (a), -dots * std::cos (a))));
                }

                if (spot.band != nullptr)
                {
                    g.setColour (ink);
                    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
                    g.drawText (spot.band, juce::Rectangle<float> (131.0f, c.y - 22.0f, 62.0f, 26.0f), juce::Justification::centredLeft, false);
                    g.setColour (ink.withAlpha (0.75f));
                    g.setFont (juce::FontOptions (20.0f));
                    g.drawText (spot.freq, juce::Rectangle<float> (131.0f, c.y + 2.0f, 62.0f, 22.0f), juce::Justification::centredLeft, false);
                }
            }

            // Console mode: which strip this is, engraved top left
            if (pluginMode)
            {
                g.setColour (ink.withAlpha (0.85f));
                g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
                g.drawText (stripInstance() != nullptr ? "UAD" : "UAD...", juce::Rectangle<float> (14.0f, 30.0f, 80.0f, 24.0f),
                            juce::Justification::centredLeft, false);
            }

            g.setColour (ink);
            g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawText ("EQ", juce::Rectangle<float> (100.0f, 38.0f, 42.0f, 24.0f), juce::Justification::centredRight, false);
            g.drawText ("DYN", juce::Rectangle<float> (92.0f, 445.0f, 50.0f, 24.0f), juce::Justification::centredRight, false);

            // Section divider and title
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (4.0f, 659.0f, strip::artWidth - 8.0f, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.fillRect (4.0f, 661.5f, strip::artWidth - 8.0f, 1.5f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (34.0f));
            g.drawText ("Dynamics", juce::Rectangle<float> (0.0f, 672.0f, strip::artWidth, 40.0f), juce::Justification::centred, false);
        }
    }

    // Fader scale between the slot and the meter
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
            g.setColour (ink.withAlpha (0.75f));
            g.fillRect ((float) fader.getRight() - 6.0f, y - 0.5f, 5.0f, 1.0f);
            g.drawText (juce::String (std::abs (db)),
                        juce::Rectangle<float> ((float) fader.getRight() - 1.0f, y - 6.0f, 18.0f, 12.0f), juce::Justification::centred, false);
        }
    }

    // Dark display boxes for the level and (on the master) where the console section would be
    auto darkBox = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff48474a), r.getX(), r.getY(), juce::Colour (0xff2c2b2e), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    };
    darkBox (dbLabel.getBounds().toFloat());

    // Name plate in the track's colour, like a strip of console tape
    const auto plate = bounds.toFloat().withTop ((float) (getHeight() - nameHeight)).reduced (2.0f, 2.0f);
    const auto base = isMaster() ? juce::Colour (0xff2c3150) : model::trackColour (track);
    const auto plateColour = base.interpolatedWith (juce::Colour (0xffd8cf8a), isMaster() ? 0.0f : 0.25f);
    g.setGradientFill (juce::ColourGradient (plateColour.brighter (0.15f), 0.0f, plate.getY(), plateColour.darker (0.15f), 0.0f, plate.getBottom(), false));
    g.fillRect (plate);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRect (plate, 1.0f);

    // The selected track: a lit name plate with a gold frame, as Logic lights the selected strip.
    if (selected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (plate);
        g.setColour (theme::gold);
        g.drawRect (plate.expanded (1.0f), 2.0f);
        g.setColour (theme::gold.withAlpha (0.9f));
        g.drawRect (bounds.toFloat().reduced (1.0f), 2.0f);
    }

    const auto name = isMaster() ? juce::String ("Master") : track[ids::name].toString();
    g.setColour (plateColour.getPerceivedBrightness() > 0.55f ? ink : juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (name, plate.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);

    // Edges between strips
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (juce::Colour (0xff1e1d1b));
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
    inButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff3d36b));
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
    look::onLight (knob);
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

    g.setGradientFill (juce::ColourGradient (theme::console.brighter (0.08f), 0.0f, 0.0f,
                                             theme::consoleShade, (float) getWidth(), (float) getHeight(), false));
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
    g.setColour (on ? juce::Colour (0xfff7d548) : juce::Colour (0xff6b6040));
    g.fillRoundedRectangle (lamp, 2.0f);
    if (on)
    {
        g.setColour (juce::Colour (0xfff7d548).withAlpha (0.3f));
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
    refreshConsoleBox();
    rebuild();
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

void MixerPanel::setSelectedTrack (const juce::String& trackId)
{
    selectedTrackId = trackId;

    for (auto* s : strips)
    {
        const bool on = s->getTrack()[ids::id].toString() == trackId;
        s->setSelected (on);

        // Scroll it into view.
        if (on)
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

    for (const auto& track : project)
        if (track.hasType (ids::TRACK))
        {
            auto* strip = strips.add (new ChannelStrip (*this, project, track));
            strip->setSelected (track[ids::id].toString() == selectedTrackId);
            strip->setComponentID ("strip" + juce::String (strips.size() - 1));
            stripHolder.addAndMakeVisible (strip);
        }

    masterStrip = std::make_unique<ChannelStrip> (*this, project, juce::ValueTree());
    body.addAndMakeVisible (*masterStrip);
    resized();
}

void MixerPanel::timerCallback()
{
    if (! isShowing())
        return;

    for (auto* s : strips)
    {
        const auto [l, r] = engine.takeTrackLevel (player.slotOf (s->getOwnerId()));
        s->pushLevels (l, r);
    }

    if (consolePanel.isVisible())
        consolePanel.update();
    else
        busComp.updateMeter();

    // Console mode: follow knobs turned in the plugins' own windows (10 times a second).
    if (++syncCounter % 3 == 0)
        for (auto* s : strips)
            s->syncFromPlugin();
}

void MixerPanel::setMasterLevels (float left, float right)
{
    if (masterStrip != nullptr)
        masterStrip->pushLevels (left, right);
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
    title.setBounds (header);

    // Everything scrolls up and down together if the pane is shorter than a strip.
    outer.setBounds (area);
    const auto tall = area.getHeight() < ChannelStrip::minimumHeight;
    const auto width = area.getWidth() - (tall ? outer.getScrollBarThickness() : 0);
    body.setSize (width, juce::jmax (area.getHeight(), ChannelStrip::minimumHeight));

    auto content = body.getLocalBounds();

    if (masterStrip != nullptr)
        masterStrip->setBounds (content.removeFromRight (ChannelStrip::stripWidth));

    // Console mode shows the user's own bus compressor; otherwise the built-in one.
    const bool console = consolePanel.isAvailable();
    consolePanel.setVisible (console);
    busComp.setVisible (! console);

    if (console)
        consolePanel.setBounds (content.removeFromRight (consolePanel.widthForHeight (content.getHeight())));
    else
        busComp.setBounds (content.removeFromRight (MasterBusCompressorPanel::panelWidth));

    viewport.setBounds (content);

    const auto wide = strips.size() * ChannelStrip::stripWidth > content.getWidth();
    const auto height = content.getHeight() - (wide ? viewport.getScrollBarThickness() : 0);
    stripHolder.setSize (juce::jmax (content.getWidth(), strips.size() * ChannelStrip::stripWidth), height);

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::stripWidth, 0, ChannelStrip::stripWidth, stripHolder.getHeight());
}

} // namespace riff
