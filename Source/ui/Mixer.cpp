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
    // Console section: EQ (cream, blue, cream, red caps as on the classic desks), Dynamics, IN
    if (! isMaster())
    {
        setupConsoleKnob (hfKnob,  ids::consoleHF,  look::Knob::cream, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "HF 8 kHz", " dB");
        setupConsoleKnob (hmfKnob, ids::consoleHMF, look::Knob::blue,  -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "HMF 2.5 kHz", " dB");
        setupConsoleKnob (lmfKnob, ids::consoleLMF, look::Knob::cream, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "LMF 400 Hz", " dB");
        setupConsoleKnob (lfKnob,  ids::consoleLF,  look::Knob::red,   -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb, "LF 100 Hz", " dB");
        setupConsoleKnob (dynKnob, ids::consoleDynamics, look::Knob::black, 0.0, ConsoleChannel::maxDynamics, "Dynamics", "");

        look::key (consoleInButton, look::Key::console);
        consoleInButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff0b43c));
        consoleInButton.setWantsKeyboardFocus (false);
        consoleInButton.setTooltip ("Switch the strip's EQ and Dynamics in or out");
        consoleInButton.onClick = [this]
        {
            owner.getUndoManager().beginNewTransaction ("Console in");
            track.setProperty (ids::consoleIn, ! (bool) track.getProperty (ids::consoleIn, true), &owner.getUndoManager());
        };
        addAndMakeVisible (consoleInButton);
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

    look::knob (panKnob, look::Knob::cream);
    look::onLight (panKnob);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setPopupDisplayEnabled (true, true, nullptr);
    panKnob.textFromValueFunction = [] (double value)
    {
        const auto amount = juce::roundToInt (std::abs (value) * 64.0);
        return amount == 0 ? juce::String ("C") : (value < 0 ? "L" : "R") + juce::String (amount);
    };
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setScrollWheelEnabled (false);
    panKnob.onDragStart = [this] { owner.getUndoManager().beginNewTransaction ("Pan"); };
    panKnob.onValueChange = [this] { track.setProperty (ids::pan, panKnob.getValue(), &owner.getUndoManager()); };
    addAndMakeVisible (panKnob);
    panKnob.setVisible (! isMaster());

    fader.setSliderStyle (juce::Slider::LinearVertical);
    look::fader (fader);
    look::onLight (fader);
    fader.setRange (minDb, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setScrollWheelEnabled (false);
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
    dbLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    dbLabel.setColour (juce::Label::textColourId, theme::lcdText);
    dbLabel.setColour (juce::Label::backgroundColourId, theme::lcd);
    dbLabel.setColour (juce::Label::outlineColourId, juce::Colours::black);
    addAndMakeVisible (dbLabel);

    for (auto* b : { &muteButton, &soloButton })
    {
        look::key (*b, look::Key::console);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
        b->setVisible (! isMaster());
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::mute);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::solo);
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

    refresh();
}

void ChannelStrip::setupConsoleKnob (juce::Slider& knob, const juce::Identifier& property, look::Knob cap,
                                     double min, double max, const juce::String& name, const juce::String& suffix)
{
    look::knob (knob, cap);
    look::onLight (knob);
    knob.setRange (min, max, 0.1);
    knob.setDoubleClickReturnValue (true, 0.0);
    knob.setScrollWheelEnabled (false);
    knob.setPopupDisplayEnabled (true, true, nullptr);
    knob.setTooltip (name);
    knob.textFromValueFunction = [name, suffix] (double v)
    {
        return name + "  " + (v > 0.05 && suffix.isNotEmpty() ? "+" : "") + juce::String (v, 1) + suffix;
    };
    knob.onDragStart = [this, name] { owner.getUndoManager().beginNewTransaction (name); };
    knob.onValueChange = [this, &knob, property] { track.setProperty (property, knob.getValue(), &owner.getUndoManager()); };
    addAndMakeVisible (knob);
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
    if (! isMaster())
    {
        hfKnob.setValue (track.getProperty (ids::consoleHF, 0.0), juce::dontSendNotification);
        hmfKnob.setValue (track.getProperty (ids::consoleHMF, 0.0), juce::dontSendNotification);
        lmfKnob.setValue (track.getProperty (ids::consoleLMF, 0.0), juce::dontSendNotification);
        lfKnob.setValue (track.getProperty (ids::consoleLF, 0.0), juce::dontSendNotification);
        dynKnob.setValue (track.getProperty (ids::consoleDynamics, 0.0), juce::dontSendNotification);
        consoleInButton.setToggleState ((bool) track.getProperty (ids::consoleIn, true), juce::dontSendNotification);
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

void ChannelStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    // Console metal: light grey with a soft vertical sheen
    g.setGradientFill (juce::ColourGradient (theme::console.brighter (0.06f), 0.0f, 0.0f,
                                             theme::consoleShade, (float) getWidth(), 0.0f, false));
    g.fillRect (bounds);

    // Section dividers and edges
    g.setColour (theme::consoleDark.withAlpha (0.6f));
    g.fillRect (0, consoleHeight + 4, getWidth(), 1);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (0, consoleHeight + 5, getWidth(), 1);
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (theme::consoleDark);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    g.setColour (theme::ink);
    if (isMaster())
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("STEREO", juce::Rectangle<int> (0, 40, getWidth(), 20), juce::Justification::centred, false);
        g.drawText ("OUT", juce::Rectangle<int> (0, 60, getWidth(), 20), juce::Justification::centred, false);
    }
    else
    {
        // Engraved captions under the console knobs
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        auto caption = [&g] (const juce::Component& knob, const juce::String& text)
        {
            g.drawText (text, knob.getBounds().translated (0, knob.getHeight() - 3).withHeight (11).expanded (6, 0),
                        juce::Justification::centred, false);
        };
        caption (hfKnob, "HF");
        caption (hmfKnob, "HMF");
        caption (lmfKnob, "LMF");
        caption (lfKnob, "LF");
        caption (dynKnob, "DYN");
        caption (panKnob, "PAN");

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("EQ", juce::Rectangle<int> (8, 4, 30, 14), juce::Justification::centredLeft, false);
    }

    // Name plate in the track's colour
    const auto plate = bounds.withTop (bounds.getBottom() - 24).reduced (3, 2).toFloat();
    const auto plateColour = isMaster() ? theme::toolbar : model::trackColour (track).withMultipliedSaturation (0.85f);
    g.setColour (plateColour);
    g.fillRoundedRectangle (plate, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (plate, 2.0f, 1.0f);

    const auto name = isMaster() ? juce::String ("Master") : track[ids::name].toString();
    g.setColour (plateColour.getPerceivedBrightness() > 0.55f ? theme::ink : juce::Colours::white);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (name, plate.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 0);

    // Console section
    auto console = area.removeFromTop (consoleHeight).withTrimmedTop (4);
    if (! isMaster())
    {
        consoleInButton.setBounds (console.getRight() - 30, console.getY(), 30, 15);
        console.removeFromTop (16);

        auto grid = console.removeFromTop (88);
        auto top = grid.removeFromTop (44), bottom = grid;
        const auto knobSize = 36;
        hfKnob.setBounds (top.removeFromLeft (top.getWidth() / 2).withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));
        hmfKnob.setBounds (top.withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));
        lmfKnob.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));
        lfKnob.setBounds (bottom.withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));

        auto row = console.removeFromTop (44);
        dynKnob.setBounds (row.removeFromLeft (row.getWidth() / 2).withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));
        panKnob.setBounds (row.withSizeKeepingCentre (knobSize, knobSize).translated (0, -4));
    }

    area.removeFromTop (8);

    for (auto* b : slots)
    {
        b->setBounds (area.removeFromTop (16));
        area.removeFromTop (2);
    }

    area.removeFromTop (3);

    for (int i = 0; i < numSends; ++i)
    {
        auto row = area.removeFromTop (18);
        if (hasSends())
        {
            sendLevels[i]->setBounds (row.removeFromRight (20));
            row.removeFromRight (3);
            sendButtons[i]->setBounds (row);
        }
        area.removeFromTop (2);
    }

    area.removeFromBottom (26);   // name plate

    if (! isMaster())
    {
        auto buttons = area.removeFromBottom (20);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
        buttons.removeFromLeft (4);
        soloButton.setBounds (buttons);
        area.removeFromBottom (5);
    }

    area.removeFromTop (3);
    dbLabel.setBounds (area.removeFromTop (16).withSizeKeepingCentre (52, 16));
    area.removeFromTop (6);

    auto faderArea = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 72), area.getHeight() - 4);
    meter.setBounds (faderArea.removeFromRight (14).withTrimmedBottom (2));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
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
MixerPanel::MixerPanel (juce::UndoManager& um, AudioEngine& e, ProjectPlayer& p)
    : undoManager (um), engine (e), player (p), busComp (um, e)
{
    title.setText ("Mixer", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);

    look::key (closeButton, look::Key::dark);
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (busComp);

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
    rebuild();
}

void MixerPanel::changed (const juce::ValueTree& parent)
{
    if (parent == project)
        needsRebuild = true; // a track was added, removed or moved

    triggerAsyncUpdate();
}

void MixerPanel::handleAsyncUpdate()
{
    busComp.refresh();

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

void MixerPanel::rebuild()
{
    strips.clear();

    for (const auto& track : project)
        if (track.hasType (ids::TRACK))
            stripHolder.addAndMakeVisible (strips.add (new ChannelStrip (*this, project, track)));

    masterStrip = std::make_unique<ChannelStrip> (*this, project, juce::ValueTree());
    addAndMakeVisible (*masterStrip);
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

    busComp.updateMeter();
}

void MixerPanel::setMasterLevels (float left, float right)
{
    if (masterStrip != nullptr)
        masterStrip->pushLevels (left, right);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::consoleDark);

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
    title.setBounds (header);

    // The master section is as tall as the strips need, and scrolls with them if the pane is short.
    const auto stripHeight = juce::jmax (ChannelStrip::minimumHeight, area.getHeight());

    if (masterStrip != nullptr)
        masterStrip->setBounds (area.removeFromRight (ChannelStrip::stripWidth).withHeight (juce::jmin (stripHeight, area.getHeight())));

    busComp.setBounds (area.removeFromRight (MasterBusCompressorPanel::panelWidth));

    viewport.setBounds (area);

    const auto wide = strips.size() * ChannelStrip::stripWidth > area.getWidth();
    const auto height = juce::jmax (ChannelStrip::minimumHeight, area.getHeight() - (wide ? viewport.getScrollBarThickness() : 0));
    stripHolder.setSize (juce::jmax (area.getWidth(), strips.size() * ChannelStrip::stripWidth), height);

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::stripWidth, 0, ChannelStrip::stripWidth, stripHolder.getHeight());
}

} // namespace riff
