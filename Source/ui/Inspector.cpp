#include "Inspector.h"
#include "TrackIcons.h"
#include "Theme.h"
#include "../model/Project.h"
#include "RoutingMenu.h"
#include "../engine/AutomationWriter.h"

namespace riff
{

Inspector::Inspector (MixerPanel& m)
    : mixer (m)
{
    for (auto* box : { &regionBox, &trackBox })
    {
        box->onLayoutChanged = [this] { resized(); };
        holder.addChildComponent (box);
    }

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    stripView.setViewedComponent (&stripHolder, false);
    stripView.setScrollBarsShown (true, false);
    stripView.setScrollBarThickness (8);
    addAndMakeVisible (stripView);

    divider.onStart = [this] { boxesAtDragStart = viewport.getHeight(); };
    divider.onDrag = [this] (int dy) { boxesWanted = juce::jmax (0, boxesAtDragStart + dy); resized(); };
    divider.onReset = [this] { boxesWanted = -1; resized(); };
    divider.setTooltip ("Drag up or down: more room for the channel strips or for the Region and Track boxes. Double-click to reset.");
    addAndMakeVisible (divider);

    // The menus at the top, on the strips' names (as in Logic's Inspector).
    for (auto* b : { &trackMenuButton, &masterMenuButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonColourId, theme::slotDark.brighter (0.15f));
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible (b);
    }

    // With no track selected, say so (and offer to make one) instead of an empty panel.
    emptyLabel.setText ("No track selected.\n\nClick a track to see its Region and Track settings and its channel strip here, "
                        "or make one:", juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::topLeft);
    emptyLabel.setColour (juce::Label::textColourId, theme::text);
    emptyLabel.setFont (juce::FontOptions (13.0f));
    holder.addChildComponent (emptyLabel);
    for (auto* b : { &newAudioButton, &newInstrumentButton })
    {
        b->setWantsKeyboardFocus (false);
        holder.addChildComponent (b);
    }
    newAudioButton.onClick = [this] { if (onNewTrack) onNewTrack (0); };
    newInstrumentButton.onClick = [this] { if (onNewTrack) onNewTrack (1); };
    trackMenuButton.setTooltip ("The track's menu: mute, solo, record, automation, colour, icon, routing, groups, stacks, mono/stereo, freeze, channel strip settings, and what the Inspector shows");
    trackMenuButton.onClick = [this]
    {
        if (onTrackMenu && track.isValid())
            onTrackMenu (track, trackMenuButton);
        else
        {
            juce::PopupMenu menu;
            addInspectorItems (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trackMenuButton));
        }
    };
    masterMenuButton.setTooltip ("The Stereo Out's menu");
    masterMenuButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Stereo Out");
        menu.addItem ("Set Volume to 0 dB", [this]
        {
            if (! project.isValid()) return;
            mixer.getUndoManager().beginNewTransaction ("Master volume");
            project.setProperty (ids::masterGain, 1.0, &mixer.getUndoManager());
        });
        menu.addSeparator();
        addInspectorItems (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&masterMenuButton));
    };
}

void Inspector::addInspectorItems (juce::PopupMenu& menu)
{
    juce::PopupMenu view;
    view.addItem ("Region Box Open", true, ! regionBox.isCollapsed(), [this] { regionBox.setCollapsed (! regionBox.isCollapsed()); });
    view.addItem ("Track Box Open", true, ! trackBox.isCollapsed(), [this] { trackBox.setCollapsed (! trackBox.isCollapsed()); });
    view.addSeparator();
    view.addItem ("Open All Boxes", [this] { regionBox.setCollapsed (false); trackBox.setCollapsed (false); boxesWanted = -1; resized(); });
    view.addItem ("Close All Boxes", [this] { regionBox.setCollapsed (true); trackBox.setCollapsed (true); });
    view.addItem ("Channel Strips Taller (boxes scroll)", [this] { boxesWanted = 0; resized(); });
    view.addItem ("Reset Divider", [this] { boxesWanted = -1; resized(); });
    menu.addSubMenu ("Inspector", view);
}

void Inspector::updateHeaderButtons()
{
    const auto arrow = juce::String::fromUTF8 (" \xe2\x96\xbe");
    auto name = track.isValid() ? track[ids::name].toString() : juce::String ("No track");
    if (name.length() > 11)
        name = name.substring (0, 10).trimEnd() + juce::String::fromUTF8 ("\xe2\x80\xa6");   // (the full name is in the menu)
    trackMenuButton.setButtonText (name + arrow);
    trackMenuButton.setEnabled (true);
    masterMenuButton.setButtonText ("Stereo Out" + arrow);
}

Inspector::~Inspector()
{
    track.removeListener (&trackWatcher);
    clear();
}

void Inspector::clear()
{
    for (auto* s : { trackStrip.get(), masterStrip.get() })
        if (s != nullptr)
            mixer.removeExtraStrip (s);
    trackStrip.reset();
    masterStrip.reset();
}

void Inspector::setTrack (juce::ValueTree newProject, juce::ValueTree newTrack)
{
    if (newProject == project && newTrack == track && masterStrip != nullptr)
    {
        updateHeaderButtons();   // (the name may have changed)
        return;
    }

    clear();
    track.removeListener (&trackWatcher);
    project = newProject;
    track = newTrack;
    track.addListener (&trackWatcher);
    trackWatcher.onChange = [this] { regionBox.refresh(); };

    if (project.isValid())
    {
        if (track.isValid())
        {
            trackStrip = std::make_unique<ChannelStrip> (mixer, project, track);
            stripHolder.addAndMakeVisible (*trackStrip);
            mixer.addExtraStrip (trackStrip.get());
        }
        masterStrip = std::make_unique<ChannelStrip> (mixer, project, juce::ValueTree());
        stripHolder.addAndMakeVisible (*masterStrip);
        mixer.addExtraStrip (masterStrip.get());
    }

    buildTrackBox();
    buildRegionBox();
    updateHeaderButtons();
    resized();
    repaint();
}

namespace
{
    /** The Track box's icon: the track's picture on a blue key (click for the others). */
    struct IconKey final : juce::Button
    {
        IconKey() : juce::Button ("Icon") {}
        int icon = 0;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().withWidth (juce::jmin ((float) getWidth(), 40.0f)).reduced (1.0f);
            g.setColour (theme::accent.withMultipliedBrightness (down ? 0.8f : over ? 1.15f : 1.0f));
            g.fillRoundedRectangle (r, 5.0f);
            icons::draw (g, icon > 0 ? icon : 9, r.reduced (r.getHeight() * 0.22f), juce::Colours::white);
        }
    };

    juce::String channelName (const juce::ValueTree& project, const juce::ValueTree& track)
    {
        // Logic's channel names: Audio 1, Inst 1, Aux 1, VCA 1 (numbered by kind).
        const auto kindOf = [] (const juce::ValueTree& t)
        {
            return model::isVcaTrack (t) ? juce::String ("VCA") : model::isBusTrack (t) ? juce::String ("Aux")
                 : model::isInstrumentTrack (t) ? juce::String ("Inst") : juce::String ("Audio");
        };
        int n = 0;
        for (const auto& t : project)
            if (t.hasType (ids::TRACK) && kindOf (t) == kindOf (track))
            {
                ++n;
                if (t == track)
                    break;
            }
        return kindOf (track) + " " + juce::String (n);
    }

    juce::String capitalised (const juce::String& s) { return s.substring (0, 1).toUpperCase() + s.substring (1); }
}

void Inspector::buildTrackBox()
{
    trackBox.setVisible (track.isValid());
    if (! track.isValid())
        return;

    auto& um = mixer.getUndoManager();
    auto t = track;
    auto p = project;
    trackBox.reset ("Track: " + track[ids::name].toString(), track);
    const bool vca = model::isVcaTrack (track), bus = model::isBusTrack (track), inst = model::isInstrumentTrack (track);

    // Icon, as in Logic: the picture on a key; click it for the others.
    {
        auto* key = new IconKey();
        key->onClick = [t, &um, key]() mutable
        {
            juce::PopupMenu menu;
            for (int i = 0; i < icons::names().size(); ++i)
                menu.addItem (icons::names()[i], true, (int) t.getProperty (ids::icon, 0) == i,
                              [t, &um, i]() mutable { um.beginNewTransaction ("Icon"); t.setProperty (ids::icon, i, &um); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (key));
        };
        trackBox.addComponent ("Icon", key, 36, [key, t] { key->icon = t.getProperty (ids::icon, 0); key->repaint(); });
    }

    trackBox.addText ("Channel", [t, p] { return channelName (p, t); });

    if (! bus && ! vca)
    {
        trackBox.addMenu ("Freeze Mode", [t] { return t.getProperty (ids::freezeMode, "preFader").toString() == "sourceOnly" ? juce::String ("Source Only") : juce::String ("Pre Fader"); },
                          [t, &um] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            const auto mode = t.getProperty (ids::freezeMode, "preFader").toString();
            for (auto [value, name] : { std::pair { "preFader", "Pre Fader" }, std::pair { "sourceOnly", "Source Only" } })
                menu.addItem (name, true, mode == value, [t, &um, v = juce::String (value)]() mutable
                {
                    um.beginNewTransaction ("Freeze Mode");
                    t.setProperty (ids::freezeMode, v, &um);
                });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });
        trackBox.addSwitch ("Q-Reference", ids::qReference, true);
    }

    if (! inst && ! vca)
        trackBox.addMenu ("Flex Mode", [t] { const auto m = t.getProperty (ids::flexMode, "off").toString(); return capitalised (m); },
                          [t, &um] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            const auto mode = t.getProperty (ids::flexMode, "off").toString();
            for (auto m : { "off", "monophonic", "slicing", "rhythmic", "polyphonic", "speed", "varispeed" })
            {
                if (juce::String (m) == "monophonic")
                    menu.addSeparator();
                menu.addItem (capitalised (m), true, mode == m, [t, &um, v = juce::String (m)]() mutable
                {
                    um.beginNewTransaction ("Flex Mode");
                    t.setProperty (ids::flexMode, v, &um);
                });
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });

    // More: the rest of the track's settings.
    trackBox.beginMore();

    if (! vca)
        trackBox.addMenu ("Automation", [t] { return capitalised (AutomationWriter::modeOf (t)); },
                          [t] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            for (auto m : { "off", "read", "touch", "latch", "write" })
                menu.addItem (capitalised (m), true, AutomationWriter::modeOf (t) == m,
                              [t, value = juce::String (m)]() mutable { t.setProperty (ids::automationMode, value, nullptr); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });

    trackBox.addMenu ("Routing", [t, p]
    {
        juce::String text = "Stereo Out";
        if (const auto out = model::findById (p, t[ids::output].toString()); out.isValid())
            text = out[ids::name].toString();
        if (const int g = t.getProperty (ids::group, 0); g > 0)
            text << ", G" << g;
        return text;
    }, [t, p, &um] (juce::Component& target)
    {
        juce::PopupMenu menu;
        routing::addRoutingItems (menu, p, t, um);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    });

    if (! vca)
    {
        trackBox.addMenu ("Format", [t] { return model::isStereoChannel (t) ? juce::String ("Stereo") : juce::String ("Mono"); },
                          [t, &um] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            for (bool stereo : { false, true })
                menu.addItem (stereo ? "Stereo" : "Mono", true, model::isStereoChannel (t) == stereo, [t, &um, stereo]() mutable
                {
                    um.beginNewTransaction ("Channel format");
                    model::setStereoChannel (t, stereo, &um);
                });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });
        trackBox.addNumber ("Width", ids::panWidth, 0.0, 200.0, 1.0, 100.0,
                            [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; },
                            [] (double v) { return v / 100.0; }, [] (double v) { return v * 100.0; });
    }

    if (inst)
    {
        const auto signedInt = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); };
        const auto noteName = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3); };
        trackBox.addNumber ("Transpose", ids::transpose, -48.0, 48.0, 1.0, 0.0, signedInt);
        trackBox.addNumber ("Velocity", ids::velocityOffset, -64.0, 64.0, 1.0, 0.0, signedInt);
        trackBox.addNumber ("Key Limit Lo", ids::keyLow, 0.0, 127.0, 1.0, 0.0, noteName);
        trackBox.addNumber ("Key Limit Hi", ids::keyHigh, 0.0, 127.0, 1.0, 127.0, noteName);
        trackBox.addNumber ("Vel Limit Lo", ids::velLow, 1.0, 127.0, 1.0, 1.0, [] (double v) { return juce::String ((int) v); });
        trackBox.addNumber ("Vel Limit Hi", ids::velHigh, 1.0, 127.0, 1.0, 127.0, [] (double v) { return juce::String ((int) v); });
        trackBox.addNumber ("Delay", ids::playDelay, -500.0, 500.0, 1.0, 0.0,
                            [] (double v) { return std::abs (v) < 0.5 ? juce::String ("0 ms") : (v > 0 ? "+" : "") + juce::String ((int) v) + " ms"; });
        trackBox.addSwitch ("No Transpose", ids::noTranspose, false);
    }

    if (! bus && ! vca)
        trackBox.addSwitch ("Freeze", ids::frozen, false, [t] (bool) { if (routing::toggleFreeze) routing::toggleFreeze (t); });

    trackBox.resized();
}

void Inspector::setRegion (juce::ValueTree newRegion)
{
    if (newRegion == region && regionBoxTrack == track)
        return;
    region = newRegion;
    buildRegionBox();
}

void Inspector::buildRegionBox()
{
    regionBoxTrack = track;

    // With no region selected: the track's defaults for new regions ("Audio Defaults" / "MIDI Defaults"), as in Logic.
    const bool defaults = ! region.isValid();
    regionBox.setVisible (region.isValid() || track.isValid());
    if (! regionBox.isVisible())
    {
        resized();
        return;
    }

    auto& um = mixer.getUndoManager();
    auto r = defaults ? track : region;
    auto p = project;
    auto owner = defaults ? track : region.getParent();
    const bool midi = defaults ? model::isInstrumentTrack (track) : model::isMidiRegion (region);
    const auto id = [defaults] (const juce::Identifier& property) { return defaults ? model::regionDefaultId (property) : property; };

    regionBox.reset (defaults ? (midi ? juce::String ("Region: MIDI Defaults") : juce::String ("Region: Audio Defaults"))
                              : "Region: " + region[ids::name].toString(), r);

    regionBox.addSwitch ("Mute", id (ids::mute), false);
    if (defaults)
        regionBox.enableLastWhen ([] { return false; });   // (a new region is never muted)
    regionBox.addSwitch ("Loop", id (ids::loop), false, [r, &um, defaults, id] (bool on) mutable
    {
        um.beginNewTransaction ("Loop region");
        if (defaults)
            r.setProperty (id (ids::loop), on, &um);
        else
            model::setLoop (r, on, &um);
    });

    // Quantize and Q-Swing (MIDI regions as they play)
    struct Grid { const char* name; double beats; };
    static const Grid grids[] { { "Off", 0.0 }, { "1/4 Note", 1.0 }, { "1/8 Note", 0.5 }, { "1/16 Note", 0.25 }, { "1/32 Note", 0.125 },
                                { "1/8 Triplet", 1.0 / 3.0 }, { "1/16 Triplet", 1.0 / 6.0 } };
    const auto qId = id (ids::playQuantize);
    regionBox.addMenu ("Quantize", [r, qId]
    {
        const double q = r.getProperty (qId, 0.0);
        for (const auto& grid : grids)
            if (std::abs (grid.beats - q) < 1.0e-6)
                return juce::String (grid.name);
        return juce::String (q, 3) + " beat";
    }, [r, &um, qId] (juce::Component& target) mutable
    {
        juce::PopupMenu menu;
        for (const auto& grid : grids)
            menu.addItem (grid.name, true, std::abs ((double) r.getProperty (qId, 0.0) - grid.beats) < 1.0e-6,
                          [r, &um, qId, beats = grid.beats]() mutable { um.beginNewTransaction ("Quantize"); r.setProperty (qId, beats, &um); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    });
    if (! midi)
        regionBox.enableLastWhen ([] { return false; });   // (audio: use Flex and the Audio File Editor's Flex Time)
    regionBox.addNumber ("Q-Swing", id (ids::playSwing), 50.0, 75.0, 1.0, 50.0, [] (double v) { return juce::String ((int) v) + "%"; });
    regionBox.enableLastWhen ([r, qId, midi] { return midi && (double) r.getProperty (qId, 0.0) > 0.0; });

    const auto signedInt = [] (double v) { return std::abs (v) < 0.5 ? juce::String ("0") : (v > 0 ? "+" : "") + juce::String ((int) v); };
    regionBox.addNumber ("Transpose", id (ids::transpose), -48.0, 48.0, 1.0, 0.0, signedInt);

    if (midi)
        regionBox.addNumber ("Velocity", id (ids::velocityOffset), -64.0, 64.0, 1.0, 0.0, signedInt);
    else
    {
        regionBox.addNumber ("Fine Tune", id (ids::fineTune), -50.0, 50.0, 1.0, 0.0,
                             [] (double v) { return std::abs (v) < 0.5 ? juce::String ("0 c") : (v > 0 ? "+" : "") + juce::String ((int) v) + " c"; });

        // Pitch Source: follow the key track from the key it's in now.
        const auto psId = id (ids::pitchSource), keyId = id (ids::pitchSourceKey);
        regionBox.addMenu ("Pitch Source", [r, psId] { return (bool) r[psId] ? juce::String ("Key Track") : juce::String ("Off"); },
                           [r, p, &um, psId, keyId, defaults] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            menu.addItem ("Off", true, ! (bool) r[psId], [r, &um, psId]() mutable { um.beginNewTransaction ("Pitch Source"); r.setProperty (psId, false, &um); });
            menu.addItem ("Follow the Key Track", true, (bool) r[psId], [r, p, &um, psId, keyId, defaults]() mutable
            {
                um.beginNewTransaction ("Pitch Source");
                r.setProperty (psId, true, &um);
                if (! defaults)
                    r.setProperty (keyId, model::keyAt (p, (double) r[ids::start]).first, &um);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });

        const auto flexId = id (ids::flex);
        auto trackOf = [owner] { return owner; };
        regionBox.addSwitch ("Flex", flexId, false);
        regionBox.enableLastWhen ([trackOf] { return trackOf().getProperty (ids::flexMode, "off").toString() != "off"; });
        const auto stId = id (ids::smartTempo);
        regionBox.addMenu ("Smart Tempo", [r, stId] { return r.getProperty (stId, "on").toString() == "off" ? juce::String ("Off") : juce::String ("On"); },
                           [r, &um, stId] (juce::Component& target) mutable
        {
            juce::PopupMenu menu;
            for (auto [value, name] : { std::pair { "on", "On: follow the song's tempo" }, std::pair { "off", "Off: play at its own speed" } })
                menu.addItem (name, true, r.getProperty (stId, "on").toString() == value,
                              [r, &um, stId, v = juce::String (value)]() mutable { um.beginNewTransaction ("Smart Tempo"); r.setProperty (stId, v, &um); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        });
        regionBox.enableLastWhen ([r, flexId, trackOf] { return (bool) r[flexId] && trackOf().getProperty (ids::flexMode, "off").toString() != "off"; });
    }

    if (! midi)
        regionBox.addNumber ("Gain", id (ids::gain), -30.0, 30.0, 0.1, 0.0,
                             [] (double v) { return std::abs (v) < 0.05 ? juce::String ("0 dB") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; },
                             [] (double db) { return juce::Decibels::decibelsToGain (db, -96.0); },
                             [] (double gain) { return juce::Decibels::gainToDecibels (gain, -30.0); });

    // More
    regionBox.beginMore();
    const auto delayText = [] (double v) { return std::abs (v) < 0.5 ? juce::String ("0 ms") : (v > 0 ? "+" : "") + juce::String ((int) v) + " ms"; };
    if (midi)
    {
        regionBox.addNumber ("Dynamics", id (ids::playDynamics), 0.0, 200.0, 1.0, 100.0,
                             [] (double v) { return v < 0.5 ? juce::String ("Fixed") : juce::String ((int) v) + "%"; });
        regionBox.addNumber ("Gate Time", id (ids::playGate), 10.0, 200.0, 1.0, 100.0, [] (double v) { return juce::String ((int) v) + "%"; });
    }
    else
    {
        // Its own tempo for Smart Tempo (0: found by listening).
        auto* variants = &mixer.getPlayer().getVariants();
        const auto file = juce::File (r[ids::file].toString());
        regionBox.addNumber ("Source Tempo", id (ids::sourceBpm), 0.0, 240.0, 0.1, 0.0, [variants, file, defaults] (double v)
        {
            if (v > 0.0)
                return juce::String (v, 1) + " BPM";
            const auto found = defaults ? 0.0 : variants->tempoOf (file);
            return found > 0.0 ? "Auto (" + juce::String (found, 1) + ")" : juce::String ("Auto");
        });
        const auto ms = [] (double v) { return v <= 0.0 ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " ms"; };
        regionBox.addNumber ("Fade In", id (ids::fadeIn), 0.0, 10000.0, 1.0, 0.0, ms,
                             [] (double v) { return v / 1000.0; }, [] (double v) { return v * 1000.0; });
        regionBox.addNumber ("Fade Out", id (ids::fadeOut), 0.0, 10000.0, 1.0, 0.0, ms,
                             [] (double v) { return v / 1000.0; }, [] (double v) { return v * 1000.0; });
    }
    regionBox.addNumber ("Delay", id (ids::playDelay), -500.0, 500.0, 1.0, 0.0, delayText);

    regionBox.refresh();
    regionBox.resized();
    resized();
}

int Inspector::boxesHeight() const
{
    if (! track.isValid())
        return 170;   // the "no track" message and its buttons
    int h = 0;
    for (const auto* box : { &regionBox, &trackBox })
        if (box->isVisible())
            h += box->preferredHeight() + 4;
    return h;
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2a2d));

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (theme::toolbarDark);
    g.fillRect (header);
    g.setColour (juce::Colours::black);
    g.fillRect (header.removeFromBottom (1));

    // (Each strip's name above it is a menu button.)

    g.setColour (juce::Colours::black);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void Inspector::resized()
{
    {
        auto names = getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (4).reduced (0, 3);
        trackMenuButton.setBounds (names.removeFromLeft (ChannelStrip::stripWidth));
        names.removeFromLeft (4);
        masterMenuButton.setBounds (names.removeFromLeft (ChannelStrip::stripWidth));
    }
    auto area = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedRight (1);

    // The Region and Track boxes on top (scrolling if the divider makes them short); their title bars always show.
    const auto boxes = boxesHeight();
    int titles = 0;
    for (const auto* box : { &regionBox, &trackBox })
        if (box->isVisible())
            titles += ParameterBox::titleBarHeight() + 4;
    if (! track.isValid())
        titles = boxes;   // the "no track" message always shows whole
    const auto top = juce::jlimit (juce::jmin (titles, boxes), juce::jmax (titles, area.getHeight() - 120), boxesWanted >= 0 ? boxesWanted : boxes);
    viewport.setBounds (area.removeFromTop (top));
    holder.setSize (viewport.getWidth() - (boxes > top ? viewport.getScrollBarThickness() : 0), boxes);
    auto all = holder.getLocalBounds();
    for (auto* box : { &regionBox, &trackBox })
        if (box->isVisible())
        {
            box->setBounds (all.removeFromTop (box->preferredHeight()).reduced (2, 0));
            all.removeFromTop (4);
        }
    const bool empty = ! track.isValid();
    for (auto* c : std::initializer_list<juce::Component*> { &emptyLabel, &newAudioButton, &newInstrumentButton })
        c->setVisible (empty);
    if (empty)
    {
        auto r = holder.getLocalBounds().reduced (8, 6);
        emptyLabel.setBounds (r.removeFromTop (96));
        newAudioButton.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        newInstrumentButton.setBounds (r.removeFromTop (26));
    }

    divider.setBounds (area.removeFromTop (7));

    // ...then the two strips, as tall as the rest (their faders grow with it).
    stripView.setBounds (area);
    const auto minStrip = mixer.stripMinimumHeight();
    const auto stripHeight = juce::jmax (area.getHeight(), minStrip);
    stripHolder.setSize (area.getWidth() - (stripHeight > area.getHeight() ? stripView.getScrollBarThickness() : 0), stripHeight);
    auto strips = stripHolder.getLocalBounds().reduced (4, 0);
    if (trackStrip != nullptr)
        trackStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
    strips.removeFromLeft (4);
    if (masterStrip != nullptr)
        masterStrip->setBounds (strips.removeFromLeft (ChannelStrip::stripWidth));
}

} // namespace riff
