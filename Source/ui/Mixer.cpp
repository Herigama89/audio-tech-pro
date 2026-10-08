#include "Mixer.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"

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
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    addAndMakeVisible (nameLabel);

    for (int i = 0; i < numSlots; ++i)
    {
        auto* b = slots.add (new juce::TextButton());
        b->setWantsKeyboardFocus (false);
        b->setTooltip ("Click to add an effect, or open this one. Right-click for bypass and remove.");
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
    }

    panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setScrollWheelEnabled (false);
    panKnob.onDragStart = [this] { owner.getUndoManager().beginNewTransaction ("Pan"); };
    panKnob.onValueChange = [this] { track.setProperty (ids::pan, panKnob.getValue(), &owner.getUndoManager()); };
    addAndMakeVisible (panKnob);
    panKnob.setVisible (! isMaster());

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
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
    dbLabel.setFont (juce::FontOptions (11.0f));
    dbLabel.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (dbLabel);

    for (auto* b : { &muteButton, &soloButton })
    {
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

juce::String ChannelStrip::getOwnerId() const
{
    return isMaster() ? model::masterOwner : track[ids::id].toString();
}

juce::ValueTree ChannelStrip::insertAt (int slot) const
{
    const auto chain = model::findChain (project, getOwnerId());
    return chain.isValid() ? chain.getChild (slot) : juce::ValueTree();
}

void ChannelStrip::refresh()
{
    nameLabel.setText (isMaster() ? juce::String ("Master") : track[ids::name].toString(), juce::dontSendNotification);

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

    for (int i = 0; i < numSlots; ++i)
    {
        const auto insert = insertAt (i);
        auto* b = slots[i];

        if (insert.isValid())
        {
            const bool off = insert[ids::bypass];
            b->setButtonText (insertName (insert));
            b->setColour (juce::TextButton::buttonColourId, off ? theme::panel : theme::selection.brighter (0.15f));
            b->setColour (juce::TextButton::textColourOffId, off ? theme::dimText : theme::text);
        }
        else
        {
            b->setButtonText ({});
            b->setColour (juce::TextButton::buttonColourId, theme::panel.darker (0.2f));
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
    g.fillAll (isMaster() ? theme::toolbar : theme::panel);

    const auto colour = isMaster() ? theme::text : model::trackColour (track);
    g.setColour (colour);
    g.fillRect (0, 0, getWidth(), 3);

    g.setColour (theme::background);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 4).withTrimmedTop (2);

    nameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    for (auto* b : slots)
    {
        b->setBounds (area.removeFromTop (20));
        area.removeFromTop (2);
    }

    area.removeFromTop (4);

    if (! isMaster())
    {
        panKnob.setBounds (area.removeFromTop (34).withSizeKeepingCentre (34, 34));

        auto buttons = area.removeFromBottom (22);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
        buttons.removeFromLeft (4);
        soloButton.setBounds (buttons);
        area.removeFromBottom (4);
    }

    dbLabel.setBounds (area.removeFromBottom (16));

    auto faderArea = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 64), area.getHeight());
    meter.setBounds (faderArea.removeFromRight (22));
    fader.setBounds (faderArea);
}

//==============================================================================
// MixerPanel
//==============================================================================
MixerPanel::MixerPanel (juce::UndoManager& um, AudioEngine& e, ProjectPlayer& p)
    : undoManager (um), engine (e), player (p)
{
    title.setText ("Mixer", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);

    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

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
}

void MixerPanel::setMasterLevels (float left, float right)
{
    if (masterStrip != nullptr)
        masterStrip->pushLevels (left, right);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromTop (28));
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, 27, getWidth(), 1);
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (28).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (60));
    title.setBounds (header);

    if (masterStrip != nullptr)
        masterStrip->setBounds (area.removeFromRight (ChannelStrip::stripWidth + 10));

    viewport.setBounds (area);

    const auto height = area.getHeight() - (stripHolder.getWidth() > area.getWidth() ? viewport.getScrollBarThickness() : 0);
    stripHolder.setSize (juce::jmax (area.getWidth(), strips.size() * ChannelStrip::stripWidth), juce::jmax (100, height));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::stripWidth, 0, ChannelStrip::stripWidth, stripHolder.getHeight());
}

} // namespace riff
