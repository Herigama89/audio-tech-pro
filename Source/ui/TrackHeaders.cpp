#include "TrackHeaders.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../model/Project.h"
#include "../engine/BuiltInEffects.h"
#include "../AppInfo.h"
#include "../engine/AutomationWriter.h"
#include "RoutingMenu.h"
#include "ControlMenu.h"
#include "TrackIcons.h"

namespace riff
{

namespace
{
    constexpr double minDb = -60.0;

    double gainToDb (double gain) { return gain <= 0.0 ? minDb : juce::jmax (minDb, juce::Decibels::gainToDecibels (gain)); }
    double dbToGain (double db)   { return db <= minDb + 0.01 ? 0.0 : juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
TrackHeader::TrackHeader (juce::ValueTree t, juce::UndoManager& um, TimelineState& tl,
                          InputNamesFn names, InstrumentMenuFn menuFn)
    : track (t), undoManager (um), timeline (tl), inputNames (std::move (names)), instrumentMenu (std::move (menuFn))
{
    isInstrument = model::isInstrumentTrack (track);
    isBus = model::isBusTrack (track) || model::isVcaTrack (track);   // (nothing to record on either)

    instrumentButton.setWantsKeyboardFocus (false);
    instrumentButton.setTooltip ("Choose the instrument, or open its window");
    instrumentButton.onClick = [this] { if (instrumentMenu) instrumentMenu (track, instrumentButton); };
    look::key (instrumentButton, look::Key::console);
    addChildComponent (instrumentButton);
    instrumentButton.setVisible (isInstrument);
    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, theme::stripInk);
    nameLabel.setColour (juce::Label::textWhenEditingColourId, theme::stripInk);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, theme::stripFace.brighter (0.3f));
    nameLabel.setColour (juce::TextEditor::textColourId, theme::stripInk);
    nameLabel.setColour (juce::TextEditor::highlightColourId, theme::knobArc.withAlpha (0.5f));
    nameLabel.setColour (juce::CaretComponent::caretColourId, theme::stripInk);
    nameLabel.onTextChange = [this]
    {
        undoManager.beginNewTransaction ("Rename track");
        track.setProperty (ids::name, nameLabel.getText(), &undoManager);
    };
    nameLabel.addMouseListener (this, false);
    addAndMakeVisible (nameLabel);

    for (auto* b : { &muteButton, &soloButton, &armButton, &monitorButton, &automationButton })
    {
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        look::key (*b, look::Key::console);
        addAndMakeVisible (b);
    }

    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::mute);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::solo);
    armButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
    monitorButton.setColour (juce::TextButton::buttonOnColourId, theme::meterAmber);
    // A REAPER theme draws these with its own pictures.
    muteButton.getProperties().set ("packKind", "mute");
    soloButton.getProperties().set ("packKind", "solo");
    armButton.getProperties().set ("packKind", "recarm");
    monitorButton.getProperties().set ("packKind", "monitor");
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    monitorButton.setTooltip ("Input monitoring: hear the input through this track while it's armed.\n"
                              "Use headphones, or a mic near speakers will feed back.");

    monitorButton.setVisible (! isInstrument && ! isBus); // instruments are monitored through their sound already
    armButton.setVisible (! isBus);                      // buses have nothing to record

    automationButton.setColour (juce::TextButton::buttonOnColourId, theme::meterGreen);
    automationButton.setTooltip ("Show automation on this track. Right-click to choose what to automate.");
    automationButton.onClick = [this]
    {
        if (! lastClickWasPopup)
            track.setProperty (ids::showAutomation, ! (bool) track[ids::showAutomation], nullptr);
    };
    automationButton.addMouseListener (this, false);

    // Arming and monitoring are live settings, not edits, so they skip undo.
    armButton.onClick = [this]
    {
        if (lastClickWasPopup)
            return;
        const auto value = ! (bool) track[ids::armed];
        // Grouped tracks arm together (when the group links record).
        for (auto other : routing::linkedTracks (track.getParent(), track, {}, "Record"))
            if (! model::isBusTrack (other) && ! model::isVcaTrack (other))
                other.setProperty (ids::armed, value, nullptr);
    };
    monitorButton.onClick = [this] { track.setProperty (ids::monitor, ! (bool) track[ids::monitor], nullptr); };
    armButton.addMouseListener (this, false);

    // On a selected track, mute and solo act on all the selected tracks (as in Logic).
    auto toggle = [this] (const juce::Identifier& flag, const juce::String& undoName)
    {
        undoManager.beginNewTransaction (undoName);
        const auto value = ! (bool) track[flag];
        // The selection (if this track is in it) and their groups follow.
        const auto selected = timeline.isTrackSelected (track[ids::id].toString()) ? timeline.selectedTrackIds() : juce::StringArray();
        for (auto other : routing::linkedTracks (track.getParent(), track, selected, flag == ids::mute ? "Mute" : "Solo"))
            other.setProperty (flag, value, &undoManager);
    };
    muteButton.onClick = [toggle] { toggle (ids::mute, "Mute"); };
    soloButton.onClick = [toggle] { toggle (ids::solo, "Solo"); };

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    look::onLight (volumeSlider);
    volumeSlider.setScrollWheelEnabled (false); // let the wheel scroll the track list
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setRange (minDb, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.textFromValueFunction = [] (double db) { return db <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
    volumeSlider.onDragStart = [this]
    {
        undoManager.beginNewTransaction ("Volume");
        if (auto* w = AutomationWriter::current())
            w->begin (track[ids::id].toString(), model::volumeParam, dbToGain (volumeSlider.getValue()));
    };
    volumeSlider.onDragEnd = [this] { if (auto* w = AutomationWriter::current()) w->end (track[ids::id].toString(), model::volumeParam); };
    volumeSlider.onValueChange = [this]
    {
        if (auto* w = AutomationWriter::current(); w != nullptr && w->move (track[ids::id].toString(), model::volumeParam, dbToGain (volumeSlider.getValue())))
            return;
        track.setProperty (ids::volume, dbToGain (volumeSlider.getValue()), &undoManager);
    };
    addAndMakeVisible (volumeSlider);

    look::knob (panKnob, look::Knob::black, false);
    look::onLight (panKnob);
    panKnob.setScrollWheelEnabled (false);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setPopupDisplayEnabled (true, true, nullptr);
    panKnob.textFromValueFunction = [] (double p)
    {
        const auto amount = juce::roundToInt (std::abs (p) * 64.0);
        return amount == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (amount);
    };
    panKnob.onDragStart = [this]
    {
        undoManager.beginNewTransaction ("Pan");
        if (auto* w = AutomationWriter::current())
            w->begin (track[ids::id].toString(), model::panParam, panKnob.getValue());
    };
    panKnob.onDragEnd = [this] { if (auto* w = AutomationWriter::current()) w->end (track[ids::id].toString(), model::panParam); };

    // Right-click: Logic's control menu (automation, MIDI Learn, default).
    auto controlMenu = [this] (juce::Slider& control, const juce::String& param, const juce::String& name, double resetValue)
    {
        controlmenu::Target t;
        t.project = track.getParent();
        t.owner = track[ids::id].toString();
        t.param = param;
        t.name = name;
        t.um = &undoManager;
        t.value = [this, param] { return param == model::volumeParam ? (double) track.getProperty (ids::volume, 1.0)
                                                                     : (double) track.getProperty (ids::pan, 0.0); };
        t.reset = [&control, resetValue] { control.setValue (resetValue, juce::sendNotificationSync); };
        controlmenu::show (control, t);
    };
    volumeSlider.onRightClick = [this, controlMenu] (const juce::MouseEvent&) { controlMenu (volumeSlider, model::volumeParam, "Volume", 0.0); };
    panKnob.onRightClick = [this, controlMenu] (const juce::MouseEvent&) { controlMenu (panKnob, model::panParam, "Pan", 0.0); };
    panKnob.onValueChange = [this]
    {
        if (auto* w = AutomationWriter::current(); w != nullptr && w->move (track[ids::id].toString(), model::panParam, panKnob.getValue()))
            return;
        track.setProperty (ids::pan, panKnob.getValue(), &undoManager);
    };
    addAndMakeVisible (panKnob);

    track.addListener (this);
    refresh();
}

TrackHeader::~TrackHeader()
{
    track.removeListener (this);
}

void TrackHeader::refresh()
{
    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    nameLabel.setTooltip (track[ids::comment].toString().isNotEmpty() ? track[ids::name].toString() + ": " + track[ids::comment].toString()
                                                                    : juce::String ("Double-click to rename"));
    muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    armButton.setToggleState ((bool) track[ids::armed], juce::dontSendNotification);
    automationButton.setToggleState ((bool) track[ids::showAutomation], juce::dontSendNotification);
    monitorButton.setToggleState ((bool) track[ids::monitor], juce::dontSendNotification);
    if (isInstrument)
    {
        armButton.setTooltip ("Record enable: records your MIDI keyboard");

        juce::String name = app::synthName;
        if (track[ids::instrument].toString() == "drums")
            name = "Drum Kit";
        else if (track[ids::instrument].toString() == "sampler")
            name = juce::File (track[ids::samplerFile].toString()).existsAsFile() ? "Sampler: " + juce::File (track[ids::samplerFile].toString()).getFileNameWithoutExtension()
                                                                                     : juce::String ("Sampler");
        else if (track[ids::instrument].toString() == "plugin")
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (track[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
                name = d.name;
        }
        instrumentButton.setButtonText (name);
    }
    else
    {
        armButton.setTooltip ("Record enable. Input: " + model::inputName (track, inputNames ? inputNames() : juce::StringArray())
                              + "\nRight-click to choose the input.");
    }
    volumeSlider.setValue (gainToDb (track.getProperty (ids::volume, 1.0)), juce::dontSendNotification);
    panKnob.setValue (track.getProperty (ids::pan, 0.0), juce::dontSendNotification);
    repaint();
}

void TrackHeader::followAutomation (double time)
{
    auto* w = AutomationWriter::current();
    if (w == nullptr)
        return;
    if (! volumeSlider.isMouseButtonDown())
    {
        const auto db = gainToDb (w->displayedValue (track, model::volumeParam, time));
        if (std::abs (db - volumeSlider.getValue()) > 0.05)
            volumeSlider.setValue (db, juce::dontSendNotification);
    }
    if (! panKnob.isMouseButtonDown())
    {
        const auto pan = w->displayedValue (track, model::panParam, time);
        if (std::abs (pan - panKnob.getValue()) > 0.005)
            panKnob.setValue (pan, juce::dontSendNotification);
    }
}

void TrackHeader::paint (juce::Graphics& g)
{
    // (follows the theme when it changes)
    if (nameLabel.findColour (juce::Label::textColourId) != theme::stripInk)
        nameLabel.setColour (juce::Label::textColourId, theme::stripInk);

    const bool selected = timeline.isTrackSelected (track[ids::id].toString());
    auto area = getLocalBounds();

    // The grey channel face, as on the mixer strips (a little lighter when selected)
    const auto body = selected ? theme::stripFace.brighter (0.12f) : theme::stripFace;
    g.setGradientFill (juce::ColourGradient (body.brighter (0.05f), 0.0f, 0.0f, body.darker (0.06f), 0.0f, (float) getHeight(), false));
    g.fillRect (area);
    {
        juce::Random grain (track[ids::id].toString().hashCode());
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + grain.nextFloat() * 0.02f));
            g.fillRect (numberWidth, y, getWidth() - numberWidth, 1);
        }
    }

    // Number column: steel blue when selected
    auto number = area.removeFromLeft (numberWidth);
    g.setColour (selected ? theme::knobArc : theme::stripFaceLow);
    g.fillRect (number);
    g.setColour (theme::stripEdge);
    g.fillRect (number.getRight() - 1, 0, 1, getHeight());

    g.setColour (selected ? juce::Colours::white : theme::stripInk);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const int icon = track.getProperty (ids::icon, 0);
    if (icon > 0 && getHeight() >= 56)
    {
        g.drawText (juce::String (track.getParent().indexOf (track) + 1), number.withTrimmedLeft (4).withHeight (getHeight() / 2 + 4),
                    juce::Justification::centred, false);
        icons::draw (g, icon, number.withTrimmedLeft (4).toFloat().withTrimmedTop ((float) getHeight() * 0.5f + 2.0f).withSizeKeepingCentre (16.0f, 16.0f),
                     (selected ? juce::Colours::white : theme::stripInk).withAlpha (0.85f));
    }
    else
        g.drawText (juce::String (track.getParent().indexOf (track) + 1), number.withTrimmedLeft (4), juce::Justification::centred, false);

    // The track's colour, as a tab on the left edge
    g.setColour (model::trackColour (track));
    g.fillRect (0, 0, 4, getHeight());

    // Kind, under the name
    g.setColour (theme::inkDim);
    g.setFont (juce::FontOptions (10.5f));
    const auto stack = track[ids::stack].toString();
    juce::String kind = stack == "folder" ? "Folder stack" : stack == "summing" ? "Summing stack"
                      : model::isVcaTrack (track) ? "VCA" : model::isBusTrack (track) ? "Aux bus" : (isInstrument ? "" : "Audio");
    if ((bool) track[ids::frozen])
        kind = "Frozen";
    if (const int group = track.getProperty (ids::group, 0); group > 0)
        kind << (kind.isEmpty() ? "" : "  ") << "G" << group;
    g.setColour ((bool) track[ids::frozen] ? theme::knobArc.darker (0.3f) : theme::stripInkDim);
    g.drawText (kind, nameLabel.getBounds().translated (3, 18).withHeight (12), juce::Justification::centredLeft, false);

    // A stack's main track: a disclosure triangle to fold its tracks away. Its tracks: a line in the number column.
    if (stack.isNotEmpty())
    {
        const auto r = juce::Rectangle<float> (8.0f, (float) getHeight() - 20.0f, 14.0f, 12.0f);
        juce::Path tri;
        if ((bool) track[ids::collapsed])
            tri.addTriangle (r.getX() + 3.0f, r.getY(), r.getX() + 3.0f, r.getBottom(), r.getRight() - 1.0f, r.getCentreY());
        else
            tri.addTriangle (r.getX(), r.getY() + 2.0f, r.getRight(), r.getY() + 2.0f, r.getCentreX(), r.getBottom());
        g.setColour (theme::stripInk);
        g.fillPath (tri);
    }
    else if (track[ids::stackParent].toString().isNotEmpty())
    {
        g.setColour (theme::stripInk.withAlpha (0.5f));
        g.fillRect (numberWidth - 6, 0, 2, getHeight());
    }

    // A hidden track, shown because Show Hidden Tracks is on: dimmed, with an H.
    if ((bool) track[ids::hidden])
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (getLocalBounds().withTrimmedLeft (numberWidth));
        g.setColour (theme::stripInk);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("H", number.withTrimmedTop (getHeight() - 18), juce::Justification::centred, false);
    }

    // Divider
    g.setColour (theme::stripEdge.darker (0.3f));
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (numberWidth, 0, getWidth() - numberWidth, 1);
}

void TrackHeader::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (numberWidth + 6).withTrimmedRight (8).withTrimmedBottom (resizeEdge);

    // The insert column (Pro Tools' Edit window), for tracks that have inserts
    const bool withInserts = showInserts && ! model::isVcaTrack (track) && track.getParent().isValid();
    if (withInserts && inserts == nullptr)
    {
        inserts = std::make_unique<HeaderInserts> (track, undoManager);
        addAndMakeVisible (*inserts);
    }
    if (inserts != nullptr)
    {
        inserts->setVisible (withInserts);
        if (withInserts)
        {
            inserts->setBounds (getLocalBounds().removeFromRight (insertsWidth).withTrimmedBottom (resizeEdge).reduced (3, 3));
            area.removeFromRight (insertsWidth - 4);
        }
    }

    // Short tracks show just the name and keys, as in Logic.
    const bool roomy = getHeight() >= 64;
    volumeSlider.setVisible (roomy);
    panKnob.setVisible (roomy);
    instrumentButton.setVisible (roomy && isInstrument);

    auto top = area.removeFromTop (36).withTrimmedTop (8).withHeight (20);
    for (auto* b : { &automationButton, &monitorButton, &armButton, &soloButton, &muteButton })
    {
        if (! b->isVisible())
            continue;
        b->setBounds (top.removeFromRight (20));
        top.removeFromRight (3);
    }
    top.removeFromRight (3);
    nameLabel.setBounds (top);

    auto bottom = area.withTrimmedTop (4).withHeight (30);
    panKnob.setBounds (bottom.removeFromRight (30));
    bottom.removeFromRight (6);

    if (isInstrument)
    {
        instrumentButton.setBounds (bottom.removeFromLeft (96).reduced (0, 4));
        bottom.removeFromLeft (6);
    }

    volumeSlider.setBounds (bottom);
}

bool TrackHeader::isOnResizeEdge (const juce::MouseEvent& e) const
{
    return e.eventComponent == this && e.y >= getHeight() - resizeEdge;
}

void TrackHeader::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (isOnResizeEdge (e) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void TrackHeader::mouseExit (const juce::MouseEvent&)
{
    if (! resizing)
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (! resizing)
    {
        // Drag the header up or down to move the track (and the other selected ones).
        if (e.mods.isPopupMenu() || e.eventComponent != this)
            return;
        if (! reordering && std::abs (e.getDistanceFromDragStartY()) > 4)
            reordering = true;
        if (reordering)
            if (auto* list = findParentComponentOfClass<TrackHeaderList>())
                list->reorderDrag (*this, e);
        return;
    }

    const auto h = juce::jlimit (TimelineState::minTrackHeight, TimelineState::maxTrackHeight,
                                 heightAtDragStart + e.getDistanceFromDragStartY());
    auto project = track.getParent();

    // Height is a view setting: saved with the project, but not an undo step.
    if (resizingAll)
    {
        for (auto t : project)
            if (t.hasType (ids::TRACK))
                t.setProperty (ids::height, h, nullptr);
    }
    else
    {
        track.setProperty (ids::height, h, nullptr);
    }
    timeline.changed();
}

void TrackHeader::mouseUp (const juce::MouseEvent& e)
{
    if (reordering)
    {
        if (auto* list = findParentComponentOfClass<TrackHeaderList>())
            list->reorderDrop (*this, e);
    }
    else if (clickKeptSelection && ! resizing && e.mouseWasClicked())
    {
        // A plain click on one of several selected tracks (not a drag): just this one.
        timeline.selectOnly (track[ids::id].toString());
        timeline.changed();
    }

    resizing = reordering = clickKeptSelection = false;
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && isOnResizeEdge (e))
    {
        resizing = true;
        resizingAll = e.mods.isAltDown();
        heightAtDragStart = getHeight();
        return;
    }

    lastClickWasPopup = e.mods.isPopupMenu();

    // A stack's disclosure triangle.
    if (! e.mods.isPopupMenu() && track[ids::stack].toString().isNotEmpty() && e.eventComponent == this
        && e.x < numberWidth && e.y > getHeight() - 24)
    {
        track.setProperty (ids::collapsed, ! (bool) track[ids::collapsed], nullptr);
        timeline.changed();
        return;
    }

    // Selecting: Cmd-click adds or removes, Shift-click selects a range, a plain click selects
    // just this one (unless it's part of the selection, so all of it can be dragged).
    const auto id = track[ids::id].toString();
    if (e.mods.isCommandDown() && ! e.mods.isPopupMenu())
        timeline.toggleTrackSelection (id);
    else if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
        timeline.extendTrackSelection (id);
    else if (timeline.isTrackSelected (id) && timeline.selectedTrackIds().size() > 1)
    {
        timeline.selectedTrackId = id;
        clickKeptSelection = ! e.mods.isPopupMenu() && e.eventComponent == this;
    }
    else
        timeline.selectOnly (id);
    timeline.selectedRegionId = {};
    timeline.changed();

    if (e.mods.isPopupMenu())
    {
        if (e.eventComponent == &armButton && ! isInstrument)
            showInputMenu();
        else if (e.eventComponent == &automationButton)
            showAutomationMenu();
        else
            showMenu();
    }
}

void TrackHeader::select()
{
    timeline.selectOnly (track[ids::id].toString());
    timeline.changed();
}

void TrackHeader::showInputMenu()
{
    const auto names = inputNames ? inputNames() : juce::StringArray();
    const int current = track.getProperty (ids::input, 0);
    const bool stereo = track.getProperty (ids::inputStereo, false);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Record from");

    if (names.isEmpty())
        menu.addItem ("No inputs. Check Audio settings.", false, false, [] {});

    auto setInput = [t = track] (int channel, bool isStereo) mutable
    {
        t.setProperty (ids::input, channel, nullptr);
        t.setProperty (ids::inputStereo, isStereo, nullptr);
    };

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (names[i], true, ! stereo && current == i, [setInput, i]() mutable { setInput (i, false); });

    if (names.size() >= 2)
    {
        menu.addSeparator();
        for (int i = 0; i + 1 < names.size(); i += 2)
            menu.addItem (names[i] + " + " + names[i + 1] + " (stereo)", true, stereo && current == i,
                          [setInput, i]() mutable { setInput (i, true); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&armButton));
}

void TrackHeader::showAutomationMenu()
{
    // Volume, pan, and every knob of the built-in effects on this track.
    const auto project = track.getParent();
    const auto current = track.getProperty (ids::automationParam, model::volumeParam).toString();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Automate");

    auto choose = [t = track] (const juce::String& param) mutable
    {
        t.setProperty (ids::automationParam, param, nullptr);
        t.setProperty (ids::showAutomation, true, nullptr);
    };

    // The mode first (as on the mixer strip).
    {
        const auto mode = AutomationWriter::modeOf (track);
        juce::PopupMenu modes;
        for (auto [m, name] : { std::pair { "off", "Off" }, std::pair { "read", "Read" }, std::pair { "touch", "Touch" },
                                std::pair { "latch", "Latch" }, std::pair { "write", "Write" } })
            modes.addItem (name, true, mode == m, [t = track, value = juce::String (m)]() mutable { t.setProperty (ids::automationMode, value, nullptr); });
        menu.addSubMenu ("Mode: " + mode.substring (0, 1).toUpperCase() + mode.substring (1), modes);
        menu.addSeparator();
    }

    menu.addItem ("Volume", true, current == model::volumeParam, [choose]() mutable { choose (model::volumeParam); });
    menu.addItem ("Pan", true, current == model::panParam, [choose]() mutable { choose (model::panParam); });

    for (const auto& insert : model::insertsOf (model::findChain (project, track[ids::id].toString())))
    {
        const auto* info = findBuiltInEffect (insert[ids::effect].toString());
        if (info == nullptr)
        {
            // A hosted plugin: its parameters by index.
            const auto names = pluginParameterNames ? pluginParameterNames (insert[ids::id].toString()) : juce::StringArray();
            if (names.isEmpty())
                continue;
            juce::PopupMenu params;
            const auto xml = juce::parseXML (insert[ids::pluginDescription].toString());
            const auto pluginName = xml != nullptr ? xml->getStringAttribute ("name") : juce::String();
            for (int i = 0; i < names.size() && i < 512; ++i)
            {
                const auto param = model::insertParam (insert[ids::id].toString(), "#" + juce::String (i));
                params.addItem (names[i], true, current == param, [choose, param, project, owner = track[ids::id].toString(), label = names[i]]() mutable
                {
                    model::laneFor (project, owner, param, true).setProperty (ids::name, label, nullptr);
                    choose (param);
                });
            }
            menu.addSubMenu (pluginName.isNotEmpty() ? pluginName : juce::String ("Plug-in"), params);
            continue;
        }

        juce::PopupMenu params;
        for (const auto& p : info->params)
        {
            const auto param = model::insertParam (insert[ids::id].toString(), p.id);
            params.addItem (p.name, true, current == param, [choose, param]() mutable { choose (param); });
        }
        menu.addSubMenu (info->name, params);
    }

    // Clearing
    const auto owner = track[ids::id].toString();
    const auto currentLane = model::laneFor (project, owner, current, false);
    bool anyLanes = false;
    for (const auto& lane : model::mixerOf (project))
        if (lane.hasType (ids::LANE) && lane[ids::owner].toString() == owner && lane.getNumChildren() > 0)
            anyLanes = true;

    menu.addSeparator();
    menu.addItem ("Clear " + automationParamName (project, current) + " automation",
                  currentLane.isValid() && currentLane.getNumChildren() > 0,
                  false,
                  [project, owner, current, &um = undoManager]
                  {
                      um.beginNewTransaction ("Clear automation");
                      model::clearAutomation (project, owner, current, &um);
                  });
    menu.addItem ("Clear all automation on this track", anyLanes, false,
                  [project, owner, &um = undoManager]
                  {
                      um.beginNewTransaction ("Clear all automation");
                      model::clearAllAutomation (project, owner, &um);
                  });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automationButton));
}

juce::String TrackHeader::automationParamName (const juce::ValueTree& project, const juce::String& param)
{
    if (param == model::volumeParam) return "Volume";
    if (param == model::panParam)    return "Pan";
    if (param.contains (":#"))
        return model::rangeOf (project, param).name;

    for (const auto& insert : model::insertsOf (model::findChain (project, track[ids::id].toString())))
        if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
            for (const auto& p : info->params)
                if (model::insertParam (insert[ids::id].toString(), p.id) == param)
                    return info->name + " " + p.name;

    return "this";
}

void TrackHeader::showMenu()
{
    juce::PopupMenu menu;
    if (! isInstrument)
        menu.addItem ("Choose input...", [this] { showInputMenu(); });
    menu.addItem ("Rename", [this] { nameLabel.showEditor(); });
    menu.addItem ("Colour...", [this] { showColours(); });
    menu.addItem ("Comments...", [this]
    {
        // Pro Tools' Comments: your notes on the track (also under its channel in the mixer).
        auto* window = new juce::AlertWindow ("Comments: " + track[ids::name].toString(), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("text", track[ids::comment].toString());
        if (auto* editor = window->getTextEditor ("text"))
        {
            editor->setMultiLine (true, true);
            editor->setReturnKeyStartsNewLine (true);
            editor->setSize (360, 110);
        }
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result != 1)
                return;
            undoManager.beginNewTransaction ("Comments");
            track.setProperty (ids::comment, window->getTextEditorContents ("text"), &undoManager);
        }), true);
    });
    {
        juce::PopupMenu iconMenu;
        const int current = track.getProperty (ids::icon, 0);
        for (int i = 0; i < icons::names().size(); ++i)
            iconMenu.addItem (icons::names()[i], true, current == i, [this, i]
            {
                undoManager.beginNewTransaction ("Track icon");
                track.setProperty (ids::icon, i, &undoManager);
            });
        menu.addSubMenu ("Icon", iconMenu);
    }
    menu.addSeparator();
    routing::addRoutingItems (menu, track.getParent(), track, undoManager);
    menu.addSeparator();

    const auto selection = timeline.selectedTrackIds();
    const auto plural = selection.size() > 1 ? juce::String (" ") + juce::String (selection.size()) + " tracks" : juce::String (" track");
    const bool hidden = track[ids::hidden];
    menu.addItem ((hidden ? "Show" : "Hide") + plural, [this, selection, hidden]
    {
        auto project = track.getParent();
        undoManager.beginNewTransaction (hidden ? "Show tracks" : "Hide tracks");
        for (const auto& id : selection)
            model::findById (project, id).setProperty (ids::hidden, ! hidden, &undoManager);
        timeline.changed();
    });
    menu.addSeparator();
    menu.addItem ("Delete" + plural, [this, selection]
    {
        auto project = track.getParent();
        undoManager.beginNewTransaction ("Delete track");
        for (const auto& id : selection)
            model::deleteTrack (model::findById (project, id), &undoManager);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void TrackHeader::showColours()
{
    auto project = track.getParent();
    auto selection = timeline.selectedTrackIds();
    auto& um = undoManager;
    auto palette = std::make_unique<TrackColourPalette> ([project, selection, &um] (juce::Colour c)
    {
        um.beginNewTransaction ("Track colour");
        for (const auto& id : selection)
            model::findById (project, id).setProperty (ids::colour, c.toString(), &um);
    });
    juce::CallOutBox::launchAsynchronously (std::move (palette), nameLabel.getScreenBounds(), nullptr);
}

//==============================================================================
TrackHeaderList::TrackHeaderList (juce::UndoManager& um, TimelineState& tl)
    : undoManager (um), timeline (tl)
{
    addAndMakeVisible (content);
    timeline.addChangeListener (this);
}

TrackHeaderList::~TrackHeaderList()
{
    timeline.removeChangeListener (this);
    project.removeListener (this);
}

void TrackHeaderList::setProject (juce::ValueTree newProject)
{
    project.removeListener (this);
    project = newProject;
    project.addListener (this);
    rebuild();
}

void TrackHeaderList::rebuild()
{
    headers.clear();

    for (const auto& track : project)
        if (track.hasType (ids::TRACK))
            content.addAndMakeVisible (headers.add (new TrackHeader (track, undoManager, timeline, inputNames, instrumentMenu)));

    resized();
    repaint();
}

void TrackHeaderList::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Corner above the headers, level with the ruler.
    g.setColour (theme::toolbarDark);
    g.fillRect (0, 0, getWidth(), TimelineState::rulerHeight);
    g.setColour (juce::Colours::black);
    g.fillRect (0, TimelineState::rulerHeight - 1, getWidth(), 1);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (model::numTracks (project)) + (model::numTracks (project) == 1 ? " track" : " tracks"),
                getLocalBounds().withY (TimelineState::barRulerHeight).withHeight (TimelineState::markerHeight).reduced (12, 0),
                juce::Justification::centredLeft);   // (the top row has the Edit, Functions and View menus)
    g.drawText ("Markers", getLocalBounds().withY (TimelineState::barRulerHeight).withHeight (TimelineState::markerHeight).reduced (12, 0),
                juce::Justification::centredRight);
    g.drawText ("Tempo", getLocalBounds().withY (TimelineState::tempoTop).withHeight (TimelineState::tempoHeight).reduced (12, 0),
                juce::Justification::centredRight);
}

void TrackHeaderList::resized()
{
    // Headers live in a child below the ruler so scrolled ones get clipped.
    content.setBounds (getLocalBounds().withTrimmedTop (TimelineState::rulerHeight));

    for (int i = 0; i < headers.size(); ++i)
    {
        const auto h = timeline.heightOf (i);
        headers[i]->setVisible (h > 0);
        headers[i]->setBounds (0, timeline.trackTop (i) - TimelineState::rulerHeight, getWidth(), h);
    }
}

void TrackHeaderList::showColoursForSelection()
{
    for (auto* h : headers)
        if (h->getTrack()[ids::id].toString() == timeline.selectedTrackId && h->isVisible())
        {
            h->showColours();
            return;
        }
}

int TrackHeaderList::dropIndexAt (int yInList) const
{
    // The gap nearest the mouse: before track i, or after the last one.
    const auto count = headers.size();
    for (int i = 0; i < count; ++i)
    {
        const auto h = timeline.heightOf (i);
        if (h > 0 && yInList < timeline.trackTop (i) + h / 2)
            return i;
    }
    return count;
}

void TrackHeaderList::reorderDrag (TrackHeader& header, const juce::MouseEvent& e)
{
    dropIndex = dropIndexAt (e.getEventRelativeTo (this).y);
    juce::ignoreUnused (header);
    repaint();
}

void TrackHeaderList::reorderDrop (TrackHeader& header, const juce::MouseEvent& e)
{
    const auto index = dropIndexAt (e.getEventRelativeTo (this).y);
    dropIndex = -1;
    repaint();

    juce::Array<juce::ValueTree> moving;
    for (const auto& id : timeline.selectedTrackIds())
        moving.add (model::findById (project, id));
    if (moving.isEmpty())
        moving.add (header.getTrack());

    undoManager.beginNewTransaction (moving.size() > 1 ? "Move tracks" : "Move track");
    model::moveTracks (project, moving, index, &undoManager);
}

void TrackHeaderList::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0)
        return;

    const auto y = dropIndex < headers.size() ? timeline.trackTop (dropIndex) : timeline.trackTop (headers.size());
    g.setColour (theme::gold);
    g.fillRect (0, juce::jmax (TimelineState::rulerHeight, y - 2), getWidth(), 4);
}

//==============================================================================
TrackColourPalette::TrackColourPalette (std::function<void (juce::Colour)> onPick)
    : pick (std::move (onPick))
{
    const auto count = model::trackPalette().size();
    const auto rows = (count + columns - 1) / columns;
    setSize (columns * (cellSize + gap) + gap, rows * (cellSize + gap) + gap);
}

juce::Rectangle<int> TrackColourPalette::cell (int index) const
{
    return { gap + (index % columns) * (cellSize + gap), gap + (index / columns) * (cellSize + gap), cellSize, cellSize };
}

int TrackColourPalette::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < model::trackPalette().size(); ++i)
        if (cell (i).contains (p))
            return i;
    return -1;
}

void TrackColourPalette::paint (juce::Graphics& g)
{
    const auto& colours = model::trackPalette();
    for (int i = 0; i < colours.size(); ++i)
    {
        const auto r = cell (i).toFloat();
        g.setColour (colours[i]);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (i == hover ? juce::Colours::white : juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, i == hover ? 2.0f : 1.0f);
    }
}

void TrackColourPalette::mouseMove (const juce::MouseEvent& e)
{
    hover = indexAt (e.getPosition());
    repaint();
}

void TrackColourPalette::mouseDown (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());
    if (index < 0)
        return;
    if (pick)
        pick (model::trackPalette()[index]);
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void TrackHeaderList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (onWheel)
        onWheel (e, wheel);
}

} // namespace riff

namespace riff
{
//==============================================================================
HeaderInserts::HeaderInserts (juce::ValueTree t, juce::UndoManager& um)
    : track (t), mixer (model::mixerOf (t.getParent())), undoManager (um)
{
    mixer.addListener (this);
    setTooltip ("Inserts: click an empty slot to add an effect, a plugin to open it. Option-click copies it to the selected tracks; right-click for more.");
}

HeaderInserts::~HeaderInserts()
{
    mixer.removeListener (this);
}

juce::ValueTree HeaderInserts::chain() const
{
    return model::findChain (track.getParent(), track[ids::id].toString());
}

bool HeaderInserts::concernsUs (const juce::ValueTree& tree) const
{
    for (auto t = tree; t.isValid(); t = t.getParent())
        if (t.hasType (ids::CHAIN))
            return t[ids::owner].toString() == track[ids::id].toString();
    return tree == mixer;
}

juce::Rectangle<int> HeaderInserts::slotBounds (int index) const
{
    return { 0, index * slotHeight, getWidth(), slotHeight - 1 };
}

int HeaderInserts::slotAt (int y) const
{
    const auto count = juce::jlimit (1, maxSlots, getHeight() / slotHeight);
    const auto index = y / slotHeight;
    return index >= 0 && index < count ? index : -1;
}

void HeaderInserts::paint (juce::Graphics& g)
{
    const auto c = chain();
    const auto count = juce::jlimit (1, maxSlots, getHeight() / slotHeight);
    for (int i = 0; i < count; ++i)
    {
        const auto r = slotBounds (i).toFloat();
        const auto insert = model::insertAtSlot (c, i);
        g.setColour (insert.isValid() ? theme::stripSlotOn.brighter (0.15f) : theme::toolbarDark.darker (0.25f));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);

        // a..j, as Pro Tools labels them
        g.setColour (theme::dimText.withAlpha (0.8f));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('a' + i)), r.withWidth (11.0f), juce::Justification::centred, false);

        if (! insert.isValid())
            continue;
        juce::String name = insert[ids::effect].toString();
        for (const auto& info : builtInEffects())
            if (info.type == name)
                name = info.name;
        if (insert[ids::kind].toString() == model::pluginKind)
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
                name = d.name;
        }
        const bool bypassed = insert[ids::bypass];
        g.setColour (bypassed ? theme::dimText : theme::text);
        g.setFont (juce::FontOptions (10.5f, bypassed ? juce::Font::italic : juce::Font::plain));
        g.drawText (name, r.withTrimmedLeft (12.0f).reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void HeaderInserts::mouseUp (const juce::MouseEvent& e)
{
    const auto index = slotAt (e.y);
    if (index < 0 || ! e.mouseWasClicked())
        return;
    const auto owner = track[ids::id].toString();
    const auto insert = model::insertAtSlot (chain(), index);

    if (! insert.isValid())
    {
        if (chooseEffect)
            chooseEffect (owner, index, {}, *this);
        return;
    }
    if (! e.mods.isPopupMenu())
    {
        if (e.mods.isAltDown() && copyToSelected)
            copyToSelected (insert);
        else if (openInsert)
            openInsert (insert);
        return;
    }

    juce::PopupMenu menu;
    menu.addItem ("Open", [insert] { if (openInsert) openInsert (insert); });
    menu.addItem ("Bypass", true, (bool) insert[ids::bypass], [this, target = insert]() mutable
    {
        auto insert = target;
        undoManager.beginNewTransaction ("Bypass");
        insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &undoManager);
    });
    menu.addItem ("Replace...", [this, owner, index, insert] { if (chooseEffect) chooseEffect (owner, index, insert, *this); });
    menu.addItem ("Copy to Selected Tracks (Option-click)", [insert] { if (copyToSelected) copyToSelected (insert); });
    menu.addSeparator();
    menu.addItem ("Remove", [this, target = insert]
    {
        undoManager.beginNewTransaction ("Remove effect");
        model::removeInsert (target, &undoManager);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
} // namespace riff
