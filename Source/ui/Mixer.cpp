#include "Mixer.h"
#include "InsertBar.h"
#include "Skin.h"
#include "RoutingMenu.h"
#include "ControlMenu.h"
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

    /** Text on the strip's slots: white on the dark slots, dark ink on Studio White's light ones. */
    juce::Colour slotInk (bool used) { return theme::whiteSkin() ? (used ? theme::stripInk : theme::stripInkDim) : (used ? juce::Colours::white : juce::Colour (0xffd6d9dc)); }

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
    // The strip's face (art, texture, printing) is drawn once and kept: its knobs, keys and meter redraw on their own.
    setBufferedToImage (true);

    if (isMaster())
    {
        look::key (multiMeterKey, look::Key::console);
        multiMeterKey.setTooltip ("MultiMeter: the master's levels, loudness (LUFS), stereo image and spectrum");
        multiMeterKey.setWantsKeyboardFocus (false);
        multiMeterKey.onClick = [this] { if (owner.onOpenMultiMeter) owner.onOpenMultiMeter(); };
        addAndMakeVisible (multiMeterKey);
    }
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

        armKey.setTooltip ("Record enable (the same as the track's R in the tracks area)");
        monitorKey.setTooltip ("Input monitoring: hear the input through this channel (the same as the track's I)");
        armKey.onClick = [this]
        {
            auto& um = owner.getUndoManager();
            um.beginNewTransaction ("Record enable");
            const auto value = ! (bool) track[ids::armed];
            for (auto other : routing::linkedTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray(), "Record"))
                if (! model::isBusTrack (other) && ! model::isVcaTrack (other))
                    other.setProperty (ids::armed, value, &um);
        };
        monitorKey.onClick = [this]
        {
            auto& um = owner.getUndoManager();
            um.beginNewTransaction ("Input monitoring");
            const auto value = ! (bool) track[ids::monitor];
            for (auto other : routing::actingTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray()))
                if (! model::isBusTrack (other) && ! model::isVcaTrack (other))
                    other.setProperty (ids::monitor, value, &um);
        };
        for (auto* k : { &armKey, &monitorKey })
        {
            k->setWantsKeyboardFocus (false);
            addChildComponent (k);
            k->setVisible (canRecord());
        }

        // The two displays under Dynamics: its amount, and pan (drag sideways)
        dynLabel.setJustificationType (juce::Justification::centred);
        dynLabel.setColour (juce::Label::textColourId, theme::stripInk);

        // The knobs, keys and lamps are the picture's (printed on the strip art).
        for (auto* k : std::initializer_list<juce::Component*> { &hfKnob, &hmfKnob, &lmfKnob, &lfKnob, &dynKnob, &panKnob })
            look::onLight (*k);
        for (auto* k : { &hfKnob, &hmfKnob, &lmfKnob, &lfKnob })
            k->useSkin (skin::knobBlack(), juce::Colours::white);
        dynKnob.useSkin (skin::knobWhite(), juce::Colour (0xff141516));
        for (auto* k : { &muteButton, &soloButton, &stripInButton })
            k->onArt = true;
        consoleInButton.onArt = dynInButton.onArt = true;
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
            // The other selected channels' pans move by the same amount (as in Logic).
            panStart.clear();
            panDragStart = panKnob.getValue();
            for (const auto& other : acting())
                if (other != track)
                    panStart[other[ids::id].toString()] = (double) other.getProperty (ids::pan, 0.0);
            if (auto* w = AutomationWriter::current())
                w->begin (track[ids::id].toString(), model::panParam, panKnob.getValue());
        };
        panKnob.onDragEnd = [this]
        {
            panStart.clear();
            if (auto* w = AutomationWriter::current())
                w->end (track[ids::id].toString(), model::panParam);
        };
        panKnob.onValueChange = [this]
        {
            const auto delta = panKnob.getValue() - panDragStart;
            for (const auto& [id, start] : panStart)
                if (auto other = model::findById (project, id); other.isValid())
                    other.setProperty (ids::pan, juce::jlimit (-1.0, 1.0, start + delta), &owner.getUndoManager());
            if (auto* w = AutomationWriter::current(); w != nullptr && w->move (track[ids::id].toString(), model::panParam, panKnob.getValue()))
                return;   // written as automation
            track.setProperty (ids::pan, panKnob.getValue(), &owner.getUndoManager());
        };
        panKnob.onRightClick = [this] (const juce::MouseEvent&)
        {
            showControlMenu (panKnob, model::panParam, "Pan", [this]
            {
                owner.getUndoManager().beginNewTransaction ("Pan");
                for (auto t2 : acting())
                    t2.setProperty (ids::pan, 0.0, &owner.getUndoManager());
            });
        };
        addAndMakeVisible (panKnob);
        panKnob.setVisible (! isVca());

        // Compact mode's EQ tag: lit while the strip's EQ / Dynamics (or strip plugin) is working; click to open it.
        look::key (consoleTag, look::Key::console);
        consoleTag.setColour (juce::TextButton::buttonOnColourId, theme::knobArc);
        consoleTag.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        consoleTag.setTooltip ("The channel strip's EQ and Dynamics: lit when they're working. Click to open them; "
                               "right-click to bypass the strip or change it.");
        consoleTag.setWantsKeyboardFocus (false);
        consoleTag.addMouseListener (this, false);   // (its right-click: the channel strip's menu, with Bypass)
        consoleTag.onClick = [this]
        {
            if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                return;
            if (auto insert = stripInsert(); insert.isValid())
            {
                if (owner.onOpenInsert)
                    owner.onOpenInsert (insert);
            }
            else if (owner.onOpenStripWindow)
                owner.onOpenStripWindow (track);
        };
        addChildComponent (consoleTag);
        consoleTag.setVisible (! isVca());
    }

    // Input and Output slots, as in Logic: where the channel's sound comes from and goes to.
    for (auto* b : { &inputSlot, &outputSlot })
    {
        look::key (*b, look::Key::slot);
        b->getProperties().set ("atpSkin", "slot");
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonColourId, theme::stripSlotOn);
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffbfe0ff));
        addAndMakeVisible (b);
    }
    inputSlot.setTooltip ("Input: what this channel records or plays (an interface input, or the instrument)");
    outputSlot.setTooltip ("Output: where this channel goes (the Stereo Out, a bus, or the interface's other outputs: Output 3-4 and so on)");
    inputSlot.onClick = [this] { showInputMenu(); };

    formatButton.setTooltip ("Channel format: mono (one circle) or stereo (two). Click to switch.");
    formatButton.setWantsKeyboardFocus (false);
    formatButton.onClick = [this] { toggleFormat(); };
    addChildComponent (formatButton);
    formatButton.setVisible (! isMaster() && ! isVca());

    look::key (settingSlot, look::Key::console);
    settingSlot.setWantsKeyboardFocus (false);
    settingSlot.setTooltip ("Setting: copy, paste or reset this channel strip; save it as a patch or load one");
    settingSlot.onClick = [this] { if (owner.onSettingMenu) owner.onSettingMenu (track, settingSlot); };
    addAndMakeVisible (settingSlot);
    settingSlot.setVisible (! isMaster() && ! isVca());
    outputSlot.onClick = [this] { showOutputMenu(); };
    inputSlot.setVisible (! isMaster() && ! isVca());
    outputSlot.setVisible (! isVca());

    for (int i = 0; i < maxInserts; ++i)
    {
        auto* b = slots.add (new juce::TextButton());
        look::key (*b, look::Key::slot);
        b->getProperties().set ("atpSkin", "slot");
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Click to add an effect, or open this one. Right-click for bypass and remove.");
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
        b->setVisible (! isVca());
    }

    for (int i = 0; i < maxSends; ++i)
    {
        auto* b = sendButtons.add (new juce::TextButton());
        look::key (*b, look::Key::slot);
        b->getProperties().set ("atpSkin", "send");
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Send to an aux bus (e.g. a shared reverb). Click to choose the bus.");
        b->onClick = [this, i] { showSendMenu (i); };
        addAndMakeVisible (b);
        b->setVisible (hasSends());

        // Logic's send knob: a ring that lights up to the level, beside the bus's name.
        auto* s = sendLevels.add (new strip::PointerKnob (theme::neon));
        look::onLight (*s);
        s->useSkin (skin::knobBlack(), juce::Colours::white);
        s->setRange (minDb, 6.0, 0.1);
        s->setSkewFactorFromMidPoint (-12.0);
        s->setDoubleClickReturnValue (true, 0.0);
        s->setPopupDisplayEnabled (true, true, nullptr);
        s->setTooltip ("Send level: drag up or down; Option-click for 0 dB. Right-click for more.");
        s->textFromValueFunction = [] (double db) { return db <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
        s->onDragStart = [this, i, s]
        {
            owner.getUndoManager().beginNewTransaction ("Send level");
            // The same send on the other selected channels follows (by the same amount).
            sendStart.clear();
            sendDragStart = s->getValue();
            for (const auto& other : acting())
                if (other != track)
                {
                    const auto sends = model::sendsOf (model::findChain (project, other[ids::id].toString()));
                    if (juce::isPositiveAndBelow (i, sends.size()))
                        sendStart[other[ids::id].toString()] = gainToDb ((double) sends[i].getProperty (ids::level, 0.0));
                }
        };
        s->onDragEnd = [this] { sendStart.clear(); };
        s->onValueChange = [this, i, s]
        {
            auto& um = owner.getUndoManager();
            if (auto send = sendAt (i); send.isValid())
                send.setProperty (ids::level, dbToGain (s->getValue()), &um);
            const auto delta = s->getValue() - sendDragStart;
            for (const auto& [id, startDb] : sendStart)
            {
                const auto sends = model::sendsOf (model::findChain (project, id));
                if (juce::isPositiveAndBelow (i, sends.size()))
                    sends[i].setProperty (ids::level, dbToGain (juce::jlimit (minDb, 6.0, startDb + delta)), &um);
            }
        };
        s->onRightClick = [this, i, s] (const juce::MouseEvent&)
        {
            showControlMenu (*s, {}, "Send " + juce::String (i + 1) + " level", [this, i]
            {
                owner.getUndoManager().beginNewTransaction ("Send level");
                for (const auto& t : acting())
                {
                    const auto sends = model::sendsOf (model::findChain (project, t[ids::id].toString()));
                    if (juce::isPositiveAndBelow (i, sends.size()))
                        sends[i].setProperty (ids::level, 1.0, &owner.getUndoManager());
                }
            }, "send:" + juce::String (i));
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
    fader.onRightClick = [this] (const juce::MouseEvent&)
    {
        if (isMaster())
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("Master volume");
            menu.addItem ("Set to Default", [this]
            {
                owner.getUndoManager().beginNewTransaction ("Master volume");
                project.setProperty (ids::masterGain, 1.0, &owner.getUndoManager());
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fader).withMousePosition());
            return;
        }
        showControlMenu (fader, model::volumeParam, "Volume", [this]
        {
            owner.getUndoManager().beginNewTransaction ("Volume");
            for (auto t2 : routing::linkedTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray(), "Volume"))
                t2.setProperty (ids::volume, 1.0, &owner.getUndoManager());
        });
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
    meter.usePhotoLeds (isMaster());   // the master's meter: the photo's blue LEDs (Studio White)

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
    knob.onDragStart = [this, &knob, property, control, name]
    {
        // The other selected channels move with it, each from where it was (as the faders do).
        consoleStart.clear();
        consoleDragStart = knob.getValue();
        for (const auto& t : acting())
        {
            const auto id = t[ids::id].toString();
            if (t == track)
                continue;
            if (pluginMode)
            {
                if (auto* other = owner.findStrip (id))
                    if (auto* p = other->stripParameterFor (control))
                    {
                        consoleStart[id] = p->getValue();
                        p->beginChangeGesture();
                    }
            }
            else
                consoleStart[id] = (double) t.getProperty (property, 0.0);
        }

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
        {
            if (auto* p = stripParameter (control))
                p->endChangeGesture();
            for (const auto& [id, start] : consoleStart)
                if (auto* other = owner.findStrip (id))
                    if (auto* p = other->stripParameterFor (control))
                        p->endChangeGesture();
        }
        consoleStart.clear();
    };
    knob.onValueChange = [this, &knob, property, control, min, max]
    {
        const auto delta = knob.getValue() - consoleDragStart;
        if (pluginMode)
        {
            if (auto* p = stripParameter (control))
                p->setValueNotifyingHost ((float) knob.getValue());
            for (const auto& [id, start] : consoleStart)
                if (auto* other = owner.findStrip (id))
                    if (auto* p = other->stripParameterFor (control))
                        p->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, start + delta));
        }
        else
        {
            track.setProperty (property, knob.getValue(), &owner.getUndoManager());
            for (const auto& [id, start] : consoleStart)
                if (auto t = model::findById (project, id); t.isValid())
                    t.setProperty (property, juce::jlimit (min, max, start + delta), &owner.getUndoManager());
        }
    };
    if (auto* drag = dynamic_cast<strip::DragSlider*> (&knob))
        drag->onRightClick = [this, &knob, property, name, control] (const juce::MouseEvent&)
        {
            controlmenu::Target t;
            t.project = project;
            t.owner = track[ids::id].toString();
            t.name = name;
            t.learnParam = "-";
            t.um = &owner.getUndoManager();
            if (! pluginMode)
                t.reset = [this, property, name]
                {
                    owner.getUndoManager().beginNewTransaction (name);
                    for (auto t2 : acting())
                        t2.setProperty (property, 0.0, &owner.getUndoManager());
                };
            t.extra = [this] (juce::PopupMenu& m)
            {
                m.addSeparator();
                m.addItem ("Channel Strip Options...", [this] { showConsoleMenu(); });
            };
            juce::ignoreUnused (control);
            controlmenu::show (knob, t);
        };
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

    // This strip (and the other selected ones): built-in, or any channel strip plugin (UA, bx_console, SSL, API, Waves...)
    const auto& candidates = owner.getStripCandidates();
    const auto targets = acting();
    juce::PopupMenu use;
    use.addItem ("Built-in console strip", true, ! insert.isValid(), [this, &um, proj, targets]() mutable
    {
        // The plugin's settings are kept with the track, and come back when you choose it again.
        if (owner.onStorePluginStates)
            owner.onStorePluginStates();
        um.beginNewTransaction ("Built-in console strip");
        for (auto t2 : targets)
        {
            const auto id = t2[ids::id].toString();
            if (const auto old = model::stripPlugin (proj, id); old.isValid())
                if (auto xml = old.createXml())
                    t2.setProperty (ids::parkedStrip, xml->toString (juce::XmlElement::TextFormat().singleLine()), &um);
            t2.setProperty (ids::builtInStrip, true, &um);
            model::setStripPlugin (proj, id, {}, &um);
        }
    });
    const auto currentXml = insert[ids::pluginDescription].toString();
    const auto choose = [&um, proj, targets] (const juce::String& xml)
    {
        return [&um, proj, targets, xml]() mutable
        {
            um.beginNewTransaction ("Console strip plugin");
            for (auto t2 : targets)
            {
                t2.removeProperty (ids::builtInStrip, &um);
                auto added = model::setStripPlugin (proj, t2[ids::id].toString(), xml, &um);
                // Its settings from before you switched to Built-in.
                if (const auto parked = juce::ValueTree::fromXml (t2[ids::parkedStrip].toString()); parked.isValid() && added.isValid()
                    && parked[ids::pluginDescription].toString() == xml)
                    for (int i = 0; i < parked.getNumProperties(); ++i)
                        if (const auto name = parked.getPropertyName (i); name != ids::id)
                            added.setProperty (name, parked[name], &um);
            }
        };
    };
    const auto xmlOf = [] (const juce::PluginDescription& d)
    {
        auto e = d.createXml();
        return e != nullptr ? e->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String();
    };
    const auto label = [] (const juce::PluginDescription& d)
    {
        return d.name + "  (" + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName) + ")";
    };

    // The channel strips found, by maker
    if (! candidates.isEmpty())
    {
        use.addSectionHeader ("Channel strips");
        std::map<juce::String, juce::PopupMenu> byMaker;
        for (const auto& d : candidates)
        {
            const auto xml = xmlOf (d);
            byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")].addItem (label (d), true, currentXml == xml, choose (xml));
        }
        if (byMaker.size() == 1)
            for (const auto& d : candidates)
                use.addItem (label (d), true, currentXml == xmlOf (d), choose (xmlOf (d)));
        else
        {
            juce::String currentMaker;
            for (const auto& d : candidates)
                if (xmlOf (d) == currentXml)
                    currentMaker = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
            for (auto& [maker, sub] : byMaker)
                use.addSubMenu (maker, sub, true, nullptr, maker == currentMaker);
        }
    }
    // ...or any plugin at all
    if (owner.allPluginTypes)
    {
        std::map<juce::String, juce::PopupMenu> byMaker;
        for (const auto& d : owner.allPluginTypes())
            if (! d.isInstrument)
            {
                const auto xml = xmlOf (d);
                byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")].addItem (label (d), true, currentXml == xml, choose (xml));
            }
        juce::PopupMenu all;
        for (auto& [maker, sub] : byMaker)
            all.addSubMenu (maker, sub);
        use.addSeparator();
        use.addSubMenu ("All Plugins", all, ! byMaker.empty());
    }
    menu.addSubMenu (targets.size() > 1 ? "These " + juce::String (targets.size()) + " strips use" : juce::String ("This strip uses"), use);

    // Bypass: the strip (plugin or built-in) out of the signal, on every selected channel
    {
        const bool in = (bool) track.getProperty (ids::stripIn, true);
        menu.addItem ("Bypass Channel Strip", true, ! in, [this, &um, in]
        {
            um.beginNewTransaction (in ? "Bypass Channel Strip" : "Activate Channel Strip");
            for (auto t2 : acting())
                t2.setProperty (ids::stripIn, ! in, &um);
        });
    }

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
    if (auto* drag = dynamic_cast<strip::DragSlider*> (e.eventComponent); drag != nullptr && drag->onRightClick)
        return;   // faders and knobs have their own menu

    // The EQ tag (EQ/Dyn hidden): the channel strip's menu (Bypass, built-in or plugin...).
    if (e.eventComponent == &consoleTag)
    {
        showConsoleMenu();
        return;
    }

    const auto inConsole = e.eventComponent == this ? e.y < topSection()
                                                    : e.eventComponent->getY() < topSection() && e.eventComponent->getParentComponent() == this;
    if (inConsole && showsConsole())
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
    if (isMaster() || isVca() || e.eventComponent != this || e.y >= topSection())
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

//==============================================================================
int ChannelStrip::insertRows() const { return owner.insertRows(); }
int ChannelStrip::sendRows() const   { return owner.sendRows(); }

juce::Array<juce::ValueTree> ChannelStrip::acting() const
{
    if (isMaster())
        return {};
    return routing::actingTracks (project, track, selected ? owner.getSelectedTrackIds() : juce::StringArray());
}

bool ChannelStrip::isStereo() const
{
    return model::isStereoChannel (track);
}

void ChannelStrip::toggleFormat()
{
    auto& um = owner.getUndoManager();
    um.beginNewTransaction ("Channel format");
    const bool toStereo = ! isStereo();
    for (auto t : acting())
        model::setStereoChannel (t, toStereo, &um);
}

void ChannelStrip::FormatButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    // Logic's icon: one ring for a mono channel, two overlapping rings for stereo.
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (down ? 0.45f : over ? 0.25f : 0.35f));
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (juce::Colour (0xffd8dde3));
    const auto d = juce::jmin (r.getWidth(), r.getHeight()) * 0.52f;
    const auto c = r.getCentre();
    if (stereo)
    {
        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c.translated (-d * 0.28f, 0.0f)), 1.2f);
        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c.translated (d * 0.28f, 0.0f)), 1.2f);
    }
    else
        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c), 1.2f);
}

void ChannelStrip::showControlMenu (juce::Slider& control, const juce::String& param, const juce::String& name,
                                    std::function<void()> reset, const juce::String& learn)
{
    controlmenu::Target t;
    t.project = project;
    t.owner = track[ids::id].toString();
    t.param = param;
    t.name = name;
    t.learnParam = learn;
    t.um = &owner.getUndoManager();
    t.reset = std::move (reset);
    if (param == model::volumeParam)
        t.value = [this] { return dbToGain (fader.getValue()); };
    else if (param == model::panParam)
        t.value = [this] { return panKnob.getValue(); };
    if (param == model::volumeParam)
        t.extra = [this] (juce::PopupMenu& m)
        {
            // The channel's routing, as on a right-click below the console section.
            juce::PopupMenu routingMenu;
            routing::addRoutingItems (routingMenu, project, track, owner.getUndoManager());
            m.addSeparator();
            m.addSubMenu ("Routing, Groups and Stacks", routingMenu);
        };
    controlmenu::show (control, t);
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
    return model::insertAtSlot (model::findChain (project, getOwnerId()), slot);
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
    // Every selected channel gets the same send (as in Logic); a bus never sends to itself.
    juce::StringArray owners;
    for (const auto& t : acting())
        if (! model::isBusTrack (t))
            owners.add (t[ids::id].toString());
    owners.addIfNotAlreadyThere (ownerId);
    auto setTarget = [&um, proj, owners, index] (const juce::String& busId) mutable
    {
        um.beginNewTransaction ("Send");
        for (const auto& id : owners)
        {
            const auto sends = model::sendsOf (model::findChain (proj, id));
            if (juce::isPositiveAndBelow (index, sends.size()))
                sends[index].setProperty (ids::target, busId, &um);
            else
                model::addSend (model::chainFor (proj, id), busId, juce::Decibels::decibelsToGain (-6.0), &um);
        }
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

    updateConsoleTag();

    // Input and Output slots
    if (isMaster())
        outputSlot.setButtonText ("Out 1-2");
    else if (! isVca())
    {
        if (model::isBusTrack (track))
            inputSlot.setButtonText (track[ids::name].toString());
        else if (model::isInstrumentTrack (track))
        {
            const auto kind = track[ids::instrument].toString();
            juce::String name = kind == "drums" ? "Drum Kit" : kind == "sampler" ? "Sampler" : "Synth";
            if (kind == "plugin")
            {
                juce::PluginDescription d;
                if (auto xml = juce::parseXML (track[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
                    name = d.name;
            }
            inputSlot.setButtonText (name);
        }
        else if (const auto source = model::findById (project, track[ids::inputTrack].toString()); source.isValid())
            inputSlot.setButtonText (source[ids::name].toString());   // another track's output
        else
            inputSlot.setButtonText (model::inputName (track, owner.inputNames ? owner.inputNames() : juce::StringArray()));
        const auto out = model::findById (project, track[ids::output].toString());
        if (const auto [first, count] = model::hardwareOutput (track[ids::output].toString()); first >= 0)
            outputSlot.setButtonText ("Out " + juce::String (first + 1) + (count == 2 ? "-" + juce::String (first + 2) : juce::String()));
        else
            outputSlot.setButtonText (out.isValid() ? out[ids::name].toString() : juce::String ("St Out"));
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
        armKey.setToggleState ((bool) track[ids::armed], juce::dontSendNotification);
        monitorKey.setToggleState ((bool) track[ids::monitor], juce::dontSendNotification);
    }

    // Sends and insert slots: as many as the mixer shows now (more appear as they fill up).
    for (int i = 0; i < maxSends; ++i)
    {
        const bool shown = hasSends() && i < sendRows();
        sendButtons[i]->setVisible (shown);
        sendLevels[i]->setVisible (shown);
    }
    for (int i = 0; i < maxInserts; ++i)
        slots[i]->setVisible (! isVca() && i < insertRows());

    if (! isMaster() && ! isVca())
    {
        formatButton.stereo = isStereo();
        formatButton.repaint();
    }

    for (int i = 0; i < sendRows() && hasSends(); ++i)
    {
        const auto send = sendAt (i);
        const auto bus = model::findById (project, send[ids::target].toString());
        sendButtons[i]->setButtonText (bus.isValid() ? (bus[ids::name].toString() + ((bool) send[ids::pre] ? " (pre)" : ""))
                                                     : juce::String ("Send"));
        sendButtons[i]->setColour (juce::TextButton::textColourOffId, slotInk (bus.isValid()));
        sendButtons[i]->setColour (juce::TextButton::buttonColourId, bus.isValid() ? theme::stripSlotOn : theme::stripSlot);
        sendLevels[i]->setEnabled (bus.isValid());
        sendLevels[i]->setValue (gainToDb (send.getProperty (ids::level, 0.0)), juce::dontSendNotification);
    }

    for (int i = 0; i < insertRows(); ++i)
    {
        const auto insert = insertAt (i);
        auto* b = slots[i];

        if (insert.isValid())
        {
            const bool off = insert[ids::bypass];
            const bool printed = insert[ids::print];
            b->setButtonText ((printed ? juce::String ("REC ") : juce::String()) + insertName (insert));
            b->setTooltip (insertName (insert) + (off ? " (bypassed)" : "")
                           + (printed ? ". Printed: it's recorded into the audio (and left out on playback)" : "")
                           + ". Click to open; right-click for bypass, print, replace and remove.");
            b->setColour (juce::TextButton::buttonColourId, off ? theme::stripSlot : printed ? juce::Colour (0xff7a2422) : theme::stripSlotOn);
            b->setColour (juce::TextButton::textColourOffId, off ? juce::Colour (0xff8d9196) : printed ? juce::Colour (0xffffd9d6) : slotInk (true));
        }
        else
        {
            b->setButtonText ({});
            b->setColour (juce::TextButton::buttonColourId, theme::stripSlot);
            b->setColour (juce::TextButton::textColourOffId, slotInk (false));
        }
    }

    // Input and Output slots follow the skin too.
    for (auto* b : { &inputSlot, &outputSlot })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::stripSlotOn);
        b->setColour (juce::TextButton::textColourOffId, theme::whiteSkin() ? theme::stripInk : juce::Colour (0xffbfe0ff));
    }

    repaint();
}

void ChannelStrip::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging a slot's plugin: to another slot, up and down, or onto another channel.
    const auto index = slots.indexOf (dynamic_cast<juce::TextButton*> (e.eventComponent));
    if (index < 0 || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
        return;
    const auto insert = insertAt (index);
    if (! insert.isValid())
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
    {
        auto* slot = slots[index];
        juce::ScaledImage image (slot->createComponentSnapshot (slot->getLocalBounds()));
        container->startDragging (juce::String (insertDragPrefix) + insert[ids::id].toString(), slot, image, true);
    }
}

int ChannelStrip::slotAt (juce::Point<int> position) const
{
    for (int i = 0; i < slots.size(); ++i)
        if (slots[i]->isVisible() && slots[i]->getBounds().expanded (0, 1).contains (position))
            return i;
    return -1;
}

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& details)
{
    return ! isVca() && details.description.toString().startsWith (insertDragPrefix);
}

void ChannelStrip::itemDragMove (const SourceDetails& details)
{
    const auto slot = slotAt (details.localPosition);
    if (slot != dropSlot)
    {
        dropSlot = slot;
        repaint();
    }
}

void ChannelStrip::itemDragExit (const SourceDetails&)
{
    dropSlot = -1;
    repaint();
}

void ChannelStrip::itemDropped (const SourceDetails& details)
{
    const auto slot = slotAt (details.localPosition);
    dropSlot = -1;
    repaint();
    if (slot < 0 || ! owner.onMoveInsert)
        return;
    const auto id = details.description.toString().fromFirstOccurrenceOf (insertDragPrefix, false, false);
    for (const auto& chain : model::mixerOf (project))
        for (const auto& insert : chain)
            if (insert.hasType (ids::INSERT) && insert[ids::id].toString() == id)
            {
                owner.onMoveInsert (insert, getOwnerId(), slot, juce::ModifierKeys::getCurrentModifiers().isAltDown());
                return;
            }
}

void ChannelStrip::paintOverChildren (juce::Graphics& g)
{
    if (! juce::isPositiveAndBelow (dropSlot, slots.size()))
        return;
    const auto r = slots[dropSlot]->getBounds().toFloat().expanded (1.0f);
    g.setColour (theme::accent);
    g.drawRoundedRectangle (r, 3.0f, 2.0f);
    g.fillRect (r.withHeight (2.0f).translated (0.0f, -2.0f));   // it goes in above what's there
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
        if (insert.isValid() && e.mods.isAltDown() && owner.onCopyInsertToSelected)
            owner.onCopyInsertToSelected (insert);
        else if (insert.isValid() && owner.onOpenInsert)
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
    // Bypass and Remove act on this slot of every selected channel.
    juce::Array<juce::ValueTree> same { insert };
    if (! isMaster())
        for (const auto& t : acting())
            if (auto other = model::insertAtSlot (model::findChain (project, t[ids::id].toString()), index); other.isValid())
                same.addIfNotAlreadyThere (other);
    menu.addItem ("Bypass", true, (bool) insert[ids::bypass], [same, &um, value = ! (bool) insert[ids::bypass]]() mutable
    {
        um.beginNewTransaction ("Bypass");
        for (auto& i : same)
            i.setProperty (ids::bypass, value, &um);
    });
    if (! isMaster() && ! model::isBusTrack (track) && ! model::isInstrumentTrack (track))
        menu.addItem ("Print While Recording (record it into the audio)", true, (bool) insert[ids::print],
                      [same, &um, value = ! (bool) insert[ids::print]]() mutable
        {
            um.beginNewTransaction (value ? "Print While Recording" : "Don't Print");
            for (auto& i : same)
                i.setProperty (ids::print, value, &um);
        });
    menu.addItem ("Replace...", [this, ownerId, index, insert, button]
    {
        if (owner.onChooseEffect)
            owner.onChooseEffect (ownerId, index, insert, *button);
    });
    menu.addItem ("Copy to Selected Channels (Option-click)", [this, insert] { if (owner.onCopyInsertToSelected) owner.onCopyInsertToSelected (insert); });
    {
        juce::PopupMenu side;
        InsertBar::addSidechainItems (side, insert, project, um);
        const auto source = model::findById (project, insert[ids::sidechain].toString());
        menu.addSubMenu ("Side Chain" + (source.isValid() ? ": " + source[ids::name].toString() : juce::String()), side);
    }
    menu.addSeparator();
    menu.addItem (same.size() > 1 ? "Remove (" + juce::String (same.size()) + " channels)" : juce::String ("Remove"), [same, &um]
    {
        um.beginNewTransaction ("Remove effect");
        for (const auto& i : same)
            model::removeInsert (i, &um);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button));
}

juce::Rectangle<int> ChannelStrip::faderArea() const
{
    // Below the sends, above the level display and the name plate.
    const auto top = topSection() + 6 + 2 * ioRow + insertRows() * insertRowHeight + 6 + sendRows() * sendRowHeight + ioRow + 4 + automationRow;
    return { 0, top, getWidth(), getHeight() - nameHeight - 22 - top };
}

void ChannelStrip::paint (juce::Graphics& g)
{
    // Labels follow the theme when it changes
    if (dbLabel.findColour (juce::Label::textColourId) != theme::lcdText)
        dbLabel.setColour (juce::Label::textColourId, theme::lcdText);
    if (dynLabel.findColour (juce::Label::textColourId) != theme::stripInk)
        dynLabel.setColour (juce::Label::textColourId, theme::stripInk);

    const auto bounds = getLocalBounds();
    const auto w = (float) getWidth();
    const bool channel = ! isMaster() && ! isVca();

    // The strip, from the picture: its top (knob scales, keys, Dynamics), then its plain face below.
    if (theme::imageSkin())
        skin::tileDown (g, skin::stripFace(), bounds.toFloat());
    else
    {
        // The flat skin: a plain panel, lit a touch at the top, with a dark gap between strips
        auto face = [height = getHeight()] (juce::Graphics& fg, juce::Rectangle<int> area)
        {
            fg.setGradientFill (juce::ColourGradient (theme::stripFace.brighter (0.06f), 0.0f, 0.0f, theme::stripFaceLow, 0.0f, (float) height, false));
            fg.fillRect (area);
        };
        if (theme::xenonSkin())
        {
            // the face with the picture's dark grain, made once for this size (every strip shares it)
            const auto tex = skin::xenonPanel();
            skin::drawCached (g, "stripFace" + theme::stripFace.toString() + theme::stripFaceLow.toString(), bounds,
                              [face, tex, bounds] (juce::Graphics& ig)
            {
                face (ig, bounds.withZeroOrigin());
                skin::tileTexture (ig, tex, bounds.withZeroOrigin(), 0.35f);
            }, true);
        }
        else
            face (g, bounds);
        g.setColour (theme::stripEdge);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }
    const bool console = showsConsole();
    if (channel && console && theme::imageSkin())
        g.setOpacity (1.0f), g.drawImage (skin::stripTop(), juce::Rectangle<float> (0.0f, 0.0f, w, (float) topHeight + 1.0f));

    // Without the console section: the channel number, and the pan value beside its knob.
    if (! console)
    {
        g.setColour (theme::stripInk);
        if (channel || isVca())
        {
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (isVca() ? juce::String ("VCA") : juce::String (track.getParent().indexOf (track) + 1),
                        juce::Rectangle<int> (5, 3, 34, 16), juce::Justification::centredLeft, false);
        }
        else
        {
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText ("MAIN", juce::Rectangle<int> (0, 4, getWidth(), 16), juce::Justification::centred, false);
            g.setColour (theme::stripInkDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("Stereo Out", juce::Rectangle<int> (0, 20, getWidth(), 14), juce::Justification::centred, false);
        }
        if (channel)
        {
            g.setColour (theme::stripInk);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (panKnob.getTextFromValue (panKnob.getValue()), panKnob.getBounds().translated (panKnob.getWidth() + 2, 0).withWidth (30),
                        juce::Justification::centredLeft, false);
            g.setColour (theme::stripInkDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("Pan", panKnob.getBounds().translated (panKnob.getWidth() + 2, -9).withWidth (30), juce::Justification::centredLeft, false);
        }
        // A line under the section
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (4, compactTopHeight - 2, getWidth() - 8, 1);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (4, compactTopHeight - 1, getWidth() - 8, 1);
    }

    const auto text = theme::stripInk, dim = theme::stripInkDim;
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (artScale));

        if (! console)
        {
        }
        else if (! channel)
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
            // Printed on the face, as in the picture: the channel number, the band names and frequencies.
            g.setColour (text);
            g.setFont (juce::FontOptions (44.0f));
            g.drawText (juce::String (track.getParent().indexOf (track) + 1), juce::Rectangle<float> (4.0f, 16.0f, 60.0f, 50.0f),
                        juce::Justification::centredLeft, false);

            struct Spot { juce::Slider* knob; const char* band; const char* freq; };
            const Spot spots[] = { { &hfKnob, "HF", "8k" }, { &hmfKnob, "HMF", "2.5k" }, { &lmfKnob, "LMF", "400" }, { &lfKnob, "LF", "100" } };
            for (const auto& spot : spots)
            {
                const auto c = spot.knob->getBounds().toFloat().transformedBy (juce::AffineTransform::scale (1.0f / artScale)).getCentre();
                g.setColour (text);
                g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
                g.drawText (spot.band, juce::Rectangle<float> (120.0f, c.y - 30.0f, 76.0f, 30.0f), juce::Justification::centredLeft, false);
                g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
                g.drawText (spot.freq, juce::Rectangle<float> (120.0f, c.y + 2.0f, 76.0f, 28.0f), juce::Justification::centredLeft, false);
            }

            // Console mode: which strip this is; and the DYN lamp's name.
            g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
            g.setColour (pluginMode ? theme::knobArc.darker (0.4f) : text);
            g.drawText (pluginMode ? (stripInstance() != nullptr ? "PLUG" : "...") : juce::String ("EQ"),
                        juce::Rectangle<float> (112.0f, 28.0f, 32.0f, 36.0f), juce::Justification::centred, false);
            g.setColour (text);
            g.drawText ("DYN", juce::Rectangle<float> (88.0f, 434.0f, 54.0f, 34.0f), juce::Justification::centredRight, false);
        }
    }

    // Fader scale beside the slot
    {
        const auto area = faderArea();
        g.setFont (juce::FontOptions (10.0f));
        float lastY = -100.0f;
        for (const auto db : { 6, 0, -5, -10, -15, -20, -30, -40, -60 })
        {
            const auto y = (float) fader.getY() + fader.yForValue (db);
            if (y < (float) area.getY() || y > (float) area.getBottom() || y - lastY < 12.0f)
                continue;   // only the numbers there's room for (none on top of each other)
            lastY = y;
            g.setColour (text.withAlpha (0.85f));
            g.fillRect ((float) fader.getRight() - 6.0f, y - 0.5f, 5.0f, 1.0f);
            g.drawText (db == 0 ? juce::String ("0") : juce::String (db),
                        juce::Rectangle<float> ((float) fader.getRight() - 1.0f, y - 6.0f, 22.0f, 12.0f), juce::Justification::centredLeft, false);
        }
    }

    // Gain reduction: amber LEDs from the top, 0 to 20 dB
    {
        const auto r = grArea().toFloat();
        g.setColour (juce::Colour (0xff141616));
        g.fillRoundedRectangle (r.expanded (1.0f), 1.5f);
        const int leds = juce::jmax (4, (int) (r.getHeight() / 4.0f));
        const auto lit = (int) std::ceil (juce::jlimit (0.0f, 20.0f, grShown) / 20.0f * (float) leds);
        for (int i = 0; i < leds; ++i)
        {
            const auto y = r.getY() + (float) i * r.getHeight() / (float) leds;
            g.setColour (i < lit ? juce::Colour (0xffffa62b) : juce::Colour (0xff3a2a14));
            g.fillRect (r.getX(), y, r.getWidth(), r.getHeight() / (float) leds - 1.0f);
        }
    }

    // The level box, from the picture
    if (theme::imageSkin())
    {
        g.setOpacity (1.0f);
        g.drawImage (skin::dbBox(), dbLabel.getBounds().toFloat());
    }
    else
    {
        g.setColour (theme::lcd);
        g.fillRoundedRectangle (dbLabel.getBounds().toFloat(), 2.5f);
    }

    // The name, printed at the foot of the strip (a thin line of the track's colour under it)
    const auto plate = bounds.toFloat().withTop ((float) (getHeight() - nameHeight)).reduced (3.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (plate.getX(), plate.getY(), plate.getWidth(), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (plate.getX(), plate.getY() + 1.0f, plate.getWidth(), 1.0f);
    const auto base = isMaster() ? theme::knobArc : model::trackColour (track);
    g.setColour (base.withAlpha (selected ? 1.0f : 0.8f));
    g.fillRect (plate.getX() + 4.0f, plate.getBottom() - 2.5f, plate.getWidth() - 8.0f, 2.0f);

    const auto name = isMaster() ? juce::String ("Master") : track[ids::name].toString();
    g.setColour (text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText (name, plate.withTrimmedBottom (3.0f).reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

    // The selected track: a steel-blue frame round the strip
    if (selected)
    {
        g.setColour (theme::knobArc.withAlpha (0.3f));
        g.drawRect (bounds.toFloat().reduced (1.0f), 4.0f);
        g.setColour (theme::knobArc);
        g.drawRect (bounds.toFloat().reduced (1.5f), 1.6f);
    }
}

juce::Rectangle<int> ChannelStrip::grArea() const
{
    return { meter.getX() - 6, meter.getY(), 4, meter.getHeight() };
}

void ChannelStrip::pushGainReduction (float db)
{
    // Instant up, about 300 ms down.
    const auto next = db >= grShown ? db : grShown * 0.82f;
    if (std::abs (next - grShown) > 0.05f || (next < 0.05f && grShown > 0.0f))
    {
        grShown = next < 0.05f ? 0.0f : next;
        repaint (grArea().expanded (1));
    }
}

void ChannelStrip::showInputMenu()
{
    if (isMaster() || isVca())
        return;
    if (model::isInstrumentTrack (track))
    {
        if (owner.onInstrumentMenu)
            owner.onInstrumentMenu (track, inputSlot);
        return;
    }

    juce::PopupMenu menu;
    if (model::isBusTrack (track))
    {
        menu.addSectionHeader (track[ids::name].toString() + ": what tracks send to it");
        for (const auto& t : project)
            if (t.hasType (ids::TRACK))
                for (const auto& send : model::sendsOf (model::findChain (project, t[ids::id].toString())))
                    if (send[ids::target].toString() == track[ids::id].toString())
                        menu.addItem (t[ids::name].toString(), false, false, [] {});
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputSlot));
        return;
    }

    const auto names = owner.inputNames ? owner.inputNames() : juce::StringArray();
    const int current = track.getProperty (ids::input, 0);
    const bool stereo = track.getProperty (ids::inputStereo, false);
    menu.addSectionHeader ("Input");
    if (names.isEmpty())
        menu.addItem ("No inputs. Check Audio settings.", false, false, [] {});
    auto setInput = [targets = acting()] (int channel, bool isStereo) mutable
    {
        // Every selected audio channel takes the same input (Logic steps them along when asked; here they share it).
        for (auto& t : targets)
            if (! model::isBusTrack (t) && ! model::isInstrumentTrack (t))
            {
                t.removeProperty (ids::inputTrack, nullptr);
                t.setProperty (ids::input, channel, nullptr);
                t.setProperty (ids::inputStereo, isStereo, nullptr);
                t.setProperty (ids::channelFormat, isStereo ? "stereo" : "mono", nullptr);
            }
    };
    juce::PopupMenu mono, both;
    const bool fromTrack = track[ids::inputTrack].toString().isNotEmpty();
    for (int i = 0; i < names.size(); ++i)
        mono.addItem (names[i], true, ! fromTrack && ! stereo && current == i, [setInput, i]() mutable { setInput (i, false); });
    for (int i = 0; i + 1 < names.size(); i += 2)
        both.addItem (names[i] + " + " + names[i + 1], true, ! fromTrack && stereo && current == i, [setInput, i]() mutable { setInput (i, true); });
    menu.addSubMenu ("Mono", mono);
    menu.addSubMenu ("Stereo", both, names.size() >= 2);
    menu.addSeparator();
    routing::addTrackInputItems (menu, project, track, acting());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputSlot));
}

void ChannelStrip::showOutputMenu()
{
    if (isMaster() || isVca())
        return;   // the master is the Stereo Out

    auto& um = owner.getUndoManager();
    const auto current = track[ids::output].toString();
    juce::Array<juce::ValueTree> targets;
    for (const auto& t : acting())
        targets.add (t);   // every selected channel goes to the same place (as in Logic)
    auto setOutput = [targets, &um] (const juce::String& id) mutable
    {
        um.beginNewTransaction ("Output");
        for (auto& t : targets)
        {
            if (id.isEmpty())
                t.removeProperty (ids::output, &um);
            else if (t[ids::id].toString() != id && ! model::outputWouldLoop (t.getParent(), t, id))
                t.setProperty (ids::output, id, &um);
        }
    };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Output");
    menu.addItem ("Stereo Out", true, current.isEmpty(), [setOutput]() mutable { setOutput ({}); });
    for (const auto& bus : project)
        if (bus.hasType (ids::TRACK) && model::isBusTrack (bus) && bus != track && ! model::outputWouldLoop (project, track, bus[ids::id].toString()))
            menu.addItem (bus[ids::name].toString(), true, current == bus[ids::id].toString(),
                          [setOutput, id = bus[ids::id].toString()]() mutable { setOutput (id); });
    routing::addHardwareOutputItems (menu, current, [setOutput] (const juce::String& id) mutable { setOutput (id); });
    menu.addSeparator();
    menu.addItem ("New Bus", [proj = project, setOutput, &um]() mutable
    {
        int buses = 0;
        for (const auto& c : proj)
            buses += (c.hasType (ids::TRACK) && model::isBusTrack (c)) ? 1 : 0;
        um.beginNewTransaction ("Output to a new bus");
        const auto bus = model::addTrack (proj, "Bus " + juce::String (buses + 1), &um, -1, model::busKind);
        setOutput (bus[ids::id].toString());
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputSlot));
}

int ChannelStrip::topSection() const
{
    return showsConsole() ? topHeight : compactTopHeight;
}

bool ChannelStrip::showsConsole() const
{
    return owner.showsConsole();
}

void ChannelStrip::updateConsoleTag()
{
    if (isMaster() || isVca())
        return;
    const bool plugin = usesPlugin();
    const bool eqWorking = (bool) track.getProperty (ids::consoleIn, true)
                           && (std::abs ((double) track.getProperty (ids::consoleHF, 0.0)) > 0.05 || std::abs ((double) track.getProperty (ids::consoleHMF, 0.0)) > 0.05
                               || std::abs ((double) track.getProperty (ids::consoleLMF, 0.0)) > 0.05 || std::abs ((double) track.getProperty (ids::consoleLF, 0.0)) > 0.05);
    const bool dynWorking = (bool) track.getProperty (ids::consoleDynIn, true) && (double) track.getProperty (ids::consoleDynamics, 0.0) > 0.05;
    const bool on = (bool) track.getProperty (ids::stripIn, true) && (plugin || eqWorking || dynWorking);
    consoleTag.setButtonText (plugin ? "PLUG" : eqWorking && dynWorking ? "EQ DYN" : dynWorking ? "DYN" : "EQ");
    consoleTag.setToggleState (on, juce::dontSendNotification);
}

void ChannelStrip::resized()
{
    const bool console = showsConsole();
    if (! isMaster() && ! isVca())
    {
        for (auto* c : std::initializer_list<juce::Component*> { &hfKnob, &hmfKnob, &lmfKnob, &lfKnob, &dynKnob, &consoleInButton, &dynInButton, &stripInButton, &dynLabel })
            c->setVisible (console);
        consoleTag.setVisible (! console);
        muteButton.onArt = soloButton.onArt = console && theme::imageSkin();
        stripInButton.onArt = consoleInButton.onArt = dynInButton.onArt = theme::imageSkin();
        panKnob.asKnob = ! console;
    }

    if (isMaster())
        multiMeterKey.setBounds (getWidth() - 36, 3, 32, 16);

    if (! isMaster() && ! console)
    {
        // Compact: number and EQ tag, the pan knob, then mute and solo.
        consoleTag.setBounds (getWidth() - 40, 3, 36, 16);
        panKnob.setBounds (8, 24, 30, 30);
        muteButton.setBounds (6, 60, getWidth() / 2 - 8, 24);
        soloButton.setBounds (getWidth() / 2 + 2, 60, getWidth() / 2 - 8, 24);
    }
    else if (! isMaster())
    {
        // Where things are on the picture's strip (art coordinates, 196 wide).
        auto knobAt = [this] (juce::Slider& k, float x, float y, float r) { k.setBounds (fromArt (x - r, y - r, r * 2.0f, r * 2.0f)); };
        knobAt (hfKnob, 76.3f, 140.7f, 31.5f);
        knobAt (hmfKnob, 75.6f, 264.6f, 31.5f);
        knobAt (lmfKnob, 76.3f, 384.3f, 31.5f);
        knobAt (lfKnob, 75.9f, 517.3f, 31.5f);
        knobAt (dynKnob, 98.0f, 752.0f, 43.0f);

        stripInButton.setBounds (fromArt (66.0f, 26.0f, 53.0f, 41.0f));
        consoleInButton.setBounds (fromArt (140.0f, 22.0f, 42.0f, 45.0f));
        dynInButton.setBounds (fromArt (140.0f, 432.0f, 42.0f, 36.0f));
        muteButton.setBounds (fromArt (28.0f, 591.0f, 71.0f, 62.0f));
        soloButton.setBounds (fromArt (98.0f, 591.0f, 71.0f, 62.0f));
        dynLabel.setBounds (fromArt (6.0f, 790.0f, 92.0f, 50.0f));
        dynLabel.setFont (juce::FontOptions ((float) dynLabel.getHeight() * 0.62f));
        panKnob.setBounds (fromArt (104.0f, 790.0f, 86.0f, 50.0f));
    }

    auto area = getLocalBounds().reduced (3, 0).withTrimmedTop (topSection() + 4);

    settingSlot.setBounds (area.removeFromTop (15));
    area.removeFromTop (2);
    {
        auto row = area.removeFromTop (15);
        if (formatButton.isVisible())
        {
            formatButton.setBounds (row.removeFromLeft (15));   // mono / stereo, left of the Input slot (as in Logic)
            row.removeFromLeft (2);
        }
        inputSlot.setBounds (row);
    }
    area.removeFromTop (2);
    for (int i = 0; i < slots.size(); ++i)
    {
        if (i < insertRows())
            slots[i]->setBounds (area.removeFromTop (insertRowHeight - 2));
        else
            slots[i]->setBounds ({});
        area.removeFromTop (i < insertRows() ? 2 : 0);
    }

    area.removeFromTop (4);

    for (int i = 0; i < sendRows(); ++i)
    {
        auto row = area.removeFromTop (sendRowHeight - 2);
        if (hasSends())
        {
            // Logic's send: the bus name, and a knob the full height of the row beside it.
            sendLevels[i]->setBounds (row.removeFromRight (row.getHeight() + 1));
            row.removeFromRight (2);
            sendButtons[i]->setBounds (row.withSizeKeepingCentre (row.getWidth(), 16));
        }
        area.removeFromTop (2);
    }
    outputSlot.setBounds (area.removeFromTop (15));

    // Fader on the left, the scale, then the meter; the level box under the meter (as in the picture)
    const auto fa = faderArea();
    fader.setBounds (juce::Rectangle<int> (2, fa.getY(), (int) (getWidth() * 0.46f), fa.getHeight()));
    meter.setBounds (juce::Rectangle<int> (getWidth() - 16, fa.getY() + 6, 12, fa.getHeight() - 12));
    dbLabel.setBounds (juce::Rectangle<int> (getWidth() - 46, fa.getBottom() + 2, 42, 19));
    if (! isMaster())
    {
        auto row = juce::Rectangle<int> (6, fa.getY() - automationRow + 1, getWidth() - 12, automationRow - 3);
        if (canRecord())
        {
            armKey.setBounds (row.removeFromLeft (row.getHeight() + 2));
            row.removeFromLeft (2);
            monitorKey.setBounds (row.removeFromLeft (row.getHeight() + 2));
            row.removeFromLeft (3);
        }
        automationButton.setBounds (row);
        updateAutomationButton();
    }
}

bool ChannelStrip::canRecord() const
{
    return ! isMaster() && ! isVca() && track.isValid() && ! model::isBusTrack (track);
}

void ChannelStrip::updateAutomationButton()
{
    if (isMaster())
        return;
    const auto mode = AutomationWriter::modeOf (track);
    // Beside the R and I keys there's less room: short names (as on a console).
    const bool narrow = automationButton.getWidth() > 0 && automationButton.getWidth() < 48;
    const auto shortName = mode == "read" ? "Rd" : mode == "touch" ? "Tch" : mode == "latch" ? "Ltch" : mode == "write" ? "Wr" : "Off";
    automationButton.setButtonText (narrow ? juce::String (shortName) : mode.substring (0, 1).toUpperCase() + mode.substring (1));
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
    inButton.getProperties().set ("channelKey", true);   // (Themes > Buttons)
    inButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    inButton.setColour (juce::TextButton::buttonOnColourId, theme::knobArc);
    inButton.setWantsKeyboardFocus (false);
    inButton.setTooltip ("Switch the Master Bus Compressor in or out");

    chooseButton.setTooltip ("The compressor: the built-in one, or any compressor plugin you have");
    chooseButton.onClick = [this] { if (onChoose) onChoose (chooseButton); };
    openButton.setTooltip ("Open the compressor plugin's window");
    openButton.onClick = [this] { if (onOpenPlugin) onOpenPlugin(); };
    for (auto* b : { &chooseButton, &openButton })
    {
        look::key (*b, look::Key::dark);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
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

    // A plugin in use: its name on the chooser; the built-in knobs rest.
    juce::String name;
    if (const auto insert = model::busCompPlugin (project); insert.isValid())
    {
        juce::PluginDescription d;
        if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
            name = d.name;
        else
            name = "Plugin";
    }
    chooseButton.setButtonText ((name.isNotEmpty() ? name : juce::String ("Built-in")) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    openButton.setVisible (name.isNotEmpty());
    for (auto* k : { &threshold, &makeup, &attack, &release, &ratio, &mix })
        k->setEnabled (name.isEmpty());
    inButton.setEnabled (name.isEmpty());
    resized();
    repaint();
}

void MasterBusCompressorPanel::updateMeter()
{
    vu.push (engine.takeBusCompGainReduction());
}

void MasterBusCompressorPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // The centre section: the picture's black panel.
    if (theme::imageSkin())
    {
        g.setOpacity (1.0f);
        g.drawImage (skin::panel(), bounds.toFloat());
    }
    else if (theme::xenonSkin())
        skin::tileTexture (g, skin::xenonPanel(), bounds);
    else
        g.fillAll (theme::consoleShade);
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
    area.removeFromTop (34);
    {
        auto row = area.removeFromTop (22).reduced (14, 0);
        openButton.setBounds (row.removeFromRight (openButton.isVisible() ? 52 : 0));
        if (openButton.isVisible())
            row.removeFromRight (4);
        chooseButton.setBounds (row);
    }
    area.removeFromTop (8);

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
    setBufferedToImage (true);
    busComp.setBufferedToImage (true);
    // Console mode for the strips: the built-in strip, or a channel strip plugin you own
    consoleCaption.setFont (juce::FontOptions (12.0f));
    consoleCaption.setColour (juce::Label::textColourId, theme::dimText);
    consoleCaption.setJustificationType (juce::Justification::centredRight);
    addChildComponent (consoleCaption);
    consoleBox.setTooltip ("The channel strip every track uses: the built-in one, or a channel strip plugin you own "
                           "(UA, bx_console, SSL, API, Waves...). Right-click a strip's EQ tag to give one channel its own.");
    consoleBox.onChange = [this]
    {
        if (consoleBox.getSelectedId() == 98)
        {
            // Any plugin as every track's strip, by maker
            refreshConsoleBox();
            if (! allPluginTypes)
                return;
            std::map<juce::String, juce::PopupMenu> byMaker;
            auto types = std::make_shared<juce::Array<juce::PluginDescription>> (allPluginTypes());
            for (int i = 0; i < types->size(); ++i)
                if (const auto& d = types->getReference (i); ! d.isInstrument)
                    byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")]
                        .addItem (d.name, [this, types, i] { if (onChooseConsole) onChooseConsole (&types->getReference (i)); });
            juce::PopupMenu menu;
            menu.addSectionHeader ("Every track's channel strip");
            for (auto& [maker, sub] : byMaker)
                menu.addSubMenu (maker, sub);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&consoleBox));
            return;
        }
        const auto id = consoleBox.getSelectedId();
        if (id >= 97)
            return;   // (the plugin already in use)
        if (onChooseConsole)
            onChooseConsole (id >= 2 && id - 2 < stripCandidates.size() ? &stripCandidates.getReference (id - 2) : nullptr);
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

    for (auto [button, property, tip] : { std::tuple { &consoleButton, "mixerConsole", "Show the EQ and Dynamics section on every strip (off: as in Logic, each strip's EQ tag opens them)" },
                                          std::tuple { &busCompButton, "mixerBusComp", "Show the Master Bus Compressor beside the master strip" } })
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        button->setWantsKeyboardFocus (false);
        button->onClick = [this, button, property = juce::Identifier (property)]
        {
            project.setProperty (property, button->getToggleState(), nullptr);   // (kept with the project, not an undo step)
            for (auto* s : strips)
                s->resized(), s->repaint();
            if (masterStrip != nullptr)
                masterStrip->resized(), masterStrip->repaint();
            for (auto* s : extraStrips)
                s->resized(), s->repaint();
            if (onLayoutChanged)
                onLayoutChanged();
            resized();
        };
        addAndMakeVisible (button);
    }

    automixButton.setTooltip ("Automix: set the faders to a balanced starting mix for a genre");
    automixButton.setWantsKeyboardFocus (false);
    automixButton.onClick = [this] { if (onAutomix) onAutomix(); };
    addAndMakeVisible (automixButton);

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
    consoleButton.setToggleState (showsConsole(), juce::dontSendNotification);
    busCompButton.setToggleState (showsBusComp(), juce::dontSendNotification);
    refreshConsoleBox();
    updateRows();
    rebuild();
}

std::pair<int, int> MixerPanel::rowsFor (const juce::ValueTree& project)
{
    // One empty insert slot below the lowest one used, and one empty send below the sends (as in Logic).
    int inserts = ChannelStrip::defaultInserts, sends = ChannelStrip::defaultSends;
    for (const auto& chain : project.getChildWithName (ids::MIXER))
    {
        if (! chain.hasType (ids::CHAIN) || chain[ids::owner].toString().startsWith ("strip:")
            || chain[ids::owner].toString() == model::busCompOwner)
            continue;
        for (const auto& insert : model::insertsOf (chain))
            inserts = juce::jmax (inserts, model::insertSlot (insert) + 2);
        sends = juce::jmax (sends, model::sendsOf (chain).size() + 1);
    }
    return { juce::jmin (inserts, ChannelStrip::maxInserts), juce::jmin (sends, ChannelStrip::maxSends) };
}

bool MixerPanel::updateRows()
{
    const auto now = rowsFor (project);
    if (now == rows)
        return false;
    rows = now;
    return true;
}

float MixerPanel::stripScale() const
{
    return narrowButton.getToggleState() ? 0.7f : 1.0f;
}

void MixerPanel::changed (const juce::ValueTree& parent)
{
    if (parent == project)
        needsRebuild = true; // a track was added, removed or moved

    touched (parent, {}, true);
}

void MixerPanel::touched (const juce::ValueTree& tree, const juce::Identifier& property, bool structural)
{
    if (tree == project)
    {
        if (property == ids::masterGain)
            dirtyOwners.insert (model::masterOwner);
        else if (! structural || needsRebuild)
            refreshAll = true;
        triggerAsyncUpdate();
        return;
    }

    // Which part of the project: a track (or something in it), or the mixer's chains.
    juce::ValueTree t = tree;
    while (t.isValid() && t.getParent() != project)
        t = t.getParent();
    if (! t.isValid())
        return;

    if (t.hasType (ids::TRACK))
    {
        if (tree != t)
            return;   // a region or a note: nothing on the strips changes
        if (property == ids::name || property == ids::kind || property == ids::colour)
            refreshAll = true;   // (bus names show on other strips' sends and outputs)
        else
            dirtyOwners.insert (t[ids::id].toString());
        triggerAsyncUpdate();
        return;
    }

    if (t.hasType (ids::MIXER))
    {
        // An insert, a send or a chain: the strip it belongs to.
        juce::ValueTree chain = tree;
        while (chain.isValid() && ! chain.hasType (ids::CHAIN))
            chain = chain.getParent();
        if (! chain.isValid())
        {
            if (tree.hasType (ids::LANE) || (tree.getParent().isValid() && tree.getParent().hasType (ids::LANE)))
                return;   // automation points: the strips follow them on their own
            refreshAll = true;
        }
        else
        {
            auto owner = chain[ids::owner].toString();
            if (owner.startsWith ("strip:"))
                owner = owner.fromFirstOccurrenceOf ("strip:", false, false);
            if (owner == model::busCompOwner)
                refreshAll = true;
            else
                dirtyOwners.insert (owner);
        }
        if (structural || tree.hasType (ids::CHAIN))
            rowsMayChange = true;
        triggerAsyncUpdate();
    }
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
        // A plugin chosen from the full list, or one this computer doesn't have
        juce::PluginDescription d;
        bool installed = false;
        if (allPluginTypes)
            for (const auto& t : allPluginTypes())
                if (auto xml = t.createXml(); xml != nullptr && xml->toString (juce::XmlElement::TextFormat().singleLine()) == current)
                    installed = true;
        if (auto xml = juce::parseXML (current); xml != nullptr && d.loadFromXml (*xml))
            consoleBox.addItem (d.name + (installed ? juce::String() : juce::String (" (not installed)")), 99);
        selected = 99;
    }

    if (allPluginTypes)
    {
        consoleBox.addSeparator();
        consoleBox.addItem ("Other plugin...", 98);
    }
    consoleBox.setSelectedId (selected, juce::dontSendNotification);
    const bool show = ! stripCandidates.isEmpty() || current.isNotEmpty() || allPluginTypes != nullptr;
    consoleBox.setVisible (show);
    consoleCaption.setVisible (show);
}

void MixerPanel::handleAsyncUpdate()
{
    const bool all = std::exchange (refreshAll, false) || needsRebuild;
    const auto owners = std::exchange (dirtyOwners, {});

    // A fader, pan, mute, insert or send on some tracks: just their strips.
    if (! all && ! std::exchange (rowsMayChange, false))
    {
        auto refreshIfDirty = [&owners] (ChannelStrip* s)
        {
            if (s != nullptr && owners.count (s->getOwnerId()) > 0)
                s->refresh();
        };
        for (auto* s : strips)
            refreshIfDirty (s);
        refreshIfDirty (masterStrip.get());
        for (auto* s : extraStrips)
            refreshIfDirty (s);
        if (owners.count (model::busCompOwner) > 0)
        {
            busComp.refresh();
            consolePanel.refresh();
            resized();   // built-in knobs, or the plugin's console face
        }
        return;
    }

    // An insert went in the last slot (or a send in the last send): every strip gets another.
    if (updateRows())
    {
        for (auto* s : strips)
            s->refresh();
        for (auto* s : extraStrips)
            s->refresh();
        relayoutStrips();
        if (onLayoutChanged)
            onLayoutChanged();
    }

    busComp.refresh();
    consolePanel.refresh();
    refreshConsoleBox();

    // The console face appears once a plugin is chosen or found.
    if (consolePanel.isAvailable() != consolePanel.isVisible())
        resized();

    if (std::exchange (needsRebuild, false))
    {
        rebuild();
        for (auto* s : extraStrips)
            s->refresh();
        return;
    }

    for (auto* s : strips)
        s->refresh();
    if (masterStrip != nullptr)
        masterStrip->refresh();
    for (auto* s : extraStrips)   // the Inspector's strips show the same
        s->refresh();
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

    // Gain reduction: the strip's Dynamics and every insert that reports it (the most of them).
    std::map<juce::String, float> reductions;
    auto reductionOf = [this, &reductions] (const juce::String& owner)
    {
        if (auto it = reductions.find (owner); it != reductions.end())
            return it->second;
        float gr = owner == model::masterOwner ? 0.0f : engine.getConsoleGainReduction (player.slotOf (owner));
        for (const auto& insert : model::insertsOf (model::findChain (project, owner)))
            if (! (bool) insert[ids::bypass])
                gr = juce::jmax (gr, effects.takeGainReduction (insert[ids::id].toString()));
        return reductions[owner] = gr;
    };

    const auto playhead = engine.getPlayheadSeconds();
    for (auto* s : strips)
    {
        if (! isShowing())
            break;   // (just the Inspector's strips while the mixer is closed)
        const auto [l, r] = levelOf (s->getOwnerId());
        s->pushLevels (l, r);
        s->pushGainReduction (reductionOf (s->getOwnerId()));
        s->followAutomation (playhead);
    }
    if (masterStrip != nullptr)
        masterStrip->pushGainReduction (reductionOf (model::masterOwner));
    for (auto* s : extraStrips)
        s->pushGainReduction (reductionOf (s->getOwnerId()));
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

void MixerPanel::relayoutStrips()
{
    for (auto* s : strips)
        s->resized(), s->repaint();
    if (masterStrip != nullptr)
        masterStrip->resized(), masterStrip->repaint();
    for (auto* s : extraStrips)
        s->resized(), s->repaint();
    resized();
    repaint();
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
    const auto frame = theme::whiteSkin() ? theme::consoleDark : juce::Colour (0xff2b2a2d);   // the console's frame, behind the strips
    if (theme::xenonSkin())
    {
        // The texture with the frame colour under it, as one opaque picture (a copy to draw, not a blend).
        const auto tex = skin::xenonPanel();
        skin::drawCached (g, "mixerBack" + frame.toString(), getLocalBounds(), [frame, tex, size = getLocalBounds()] (juce::Graphics& ig)
        {
            ig.fillAll (frame);
            skin::tileTexture (ig, tex, size, 0.55f);
        }, true);
    }
    else
        g.fillAll (frame);

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
    header.removeFromLeft (6);
    consoleButton.setBounds (header.removeFromLeft (64));
    header.removeFromLeft (6);
    busCompButton.setBounds (header.removeFromLeft (76));
    header.removeFromLeft (6);
    automixButton.setBounds (header.removeFromLeft (70));

    // Narrow strips are the same strips, drawn smaller.
    const auto scale = stripScale();
    const auto stripW = (int) std::ceil (ChannelStrip::stripWidth * scale);
    const auto minimumHeight = (int) std::ceil (stripMinimumHeight() * scale);

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

    // The bus compressor, when shown: the user's own (console mode) or the built-in one.
    const bool console = consolePanel.isAvailable();
    consolePanel.setVisible (console && showsBusComp());
    busComp.setVisible (! console && showsBusComp());

    if (consolePanel.isVisible())
        consolePanel.setBounds (content.removeFromRight (consolePanel.widthForHeight (content.getHeight())));
    else if (busComp.isVisible())
        busComp.setBounds (content.removeFromRight (MasterBusCompressorPanel::panelWidth));

    viewport.setBounds (content);

    const auto wide = strips.size() * stripW > content.getWidth();
    const auto height = content.getHeight() - (wide ? viewport.getScrollBarThickness() : 0);
    stripHolder.setSize (juce::jmax (content.getWidth(), strips.size() * stripW), height);

    const auto stripHeight = stripHolder.getHeight() - (showComments ? commentsHeight : 0);
    for (int i = 0; i < strips.size(); ++i)
        place (*strips[i], { i * stripW, 0, stripW, stripHeight });
    layOutComments (stripW, stripHeight);
}

void MixerPanel::layOutComments (int stripW, int top)
{
    while (commentBoxes.size() < (showComments ? strips.size() : 0))
    {
        auto* box = commentBoxes.add (new juce::TextEditor());
        box->setMultiLine (true, true);
        box->setReturnKeyStartsNewLine (false);
        box->setFont (juce::FontOptions (11.0f));
        box->setTextToShowWhenEmpty ("Comments", theme::dimText.withAlpha (0.6f));
        box->setColour (juce::TextEditor::backgroundColourId, theme::slotDark);
        box->setColour (juce::TextEditor::textColourId, theme::text);
        box->setColour (juce::TextEditor::outlineColourId, theme::outline);
        box->setIndents (4, 3);
        stripHolder.addAndMakeVisible (box);
    }
    while (commentBoxes.size() > (showComments ? strips.size() : 0))
        commentBoxes.removeLast();

    for (int i = 0; i < commentBoxes.size(); ++i)
    {
        auto* box = commentBoxes[i];
        auto track = strips[i]->getTrack();
        box->setBounds (i * stripW + 1, top + 2, stripW - 2, commentsHeight - 4);
        if (! box->hasKeyboardFocus (true))
            box->setText (track[ids::comment].toString(), juce::dontSendNotification);
        box->setTooltip (track[ids::name].toString() + ": your notes on this track");
        auto save = [this, box, track]() mutable
        {
            if (box->getText() == track[ids::comment].toString())
                return;
            undoManager.beginNewTransaction ("Comments");
            track.setProperty (ids::comment, box->getText(), &undoManager);
        };
        box->onFocusLost = save;
        box->onReturnKey = [box, save]() mutable { save(); box->giveAwayKeyboardFocus(); };
        box->onEscapeKey = [box, track] { box->setText (track[ids::comment].toString(), juce::dontSendNotification); box->giveAwayKeyboardFocus(); };
    }
}

} // namespace riff
