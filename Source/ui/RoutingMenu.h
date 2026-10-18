#pragma once

#include <JuceHeader.h>
#include "../model/Project.h"

namespace riff::routing
{

/** Set by the main window: freezes or unfreezes a track (rendering takes a moment, with progress). */
inline std::function<void (juce::ValueTree track)> toggleFreeze;

/** Set by the main window: the next controller you move on a MIDI device controls this (MIDI Learn). */
inline std::function<void (juce::ValueTree track, juce::String param)> learnController;

/** Set by the main window: the audio interface's output names (all of them), and switching them on up to a number. */
inline std::function<juce::StringArray()> hardwareOutputNames;
inline std::function<void (int upTo)> enableHardwareOutputs;

/** The interface's other outputs (as Logic's Output 3-4 and so on; 1-2 is the Stereo Out), and a Mono submenu. */
inline void addHardwareOutputItems (juce::PopupMenu& menu, const juce::String& current, std::function<void (const juce::String&)> setOutput)
{
    const auto names = hardwareOutputNames ? hardwareOutputNames() : juce::StringArray();
    const auto count = juce::jmin (names.size(), 64);
    menu.addSectionHeader ("Interface Outputs");
    if (count <= 2)
    {
        menu.addItem ("(this interface has no other outputs)", false, false, nullptr);
        return;
    }
    const auto choose = [setOutput] (int first, int width)
    {
        return [setOutput, first, width]
        {
            if (enableHardwareOutputs)
                enableHardwareOutputs (first + width);
            setOutput (model::hardwareOutputId (first, width == 1));
        };
    };
    for (int ch = 2; ch + 1 < count; ch += 2)
    {
        const auto id = model::hardwareOutputId (ch, false);
        menu.addItem (model::hardwareOutputName (id, names), true, current == id, choose (ch, 2));
    }
    juce::PopupMenu mono;
    for (int ch = 0; ch < count; ++ch)
    {
        const auto id = model::hardwareOutputId (ch, true);
        mono.addItem (model::hardwareOutputName (id, names), true, current == id, choose (ch, 1));
    }
    menu.addSubMenu ("Mono", mono, true, nullptr, current.startsWith ("hwm:"));
}

/** Set by the main window: opens the Group Settings window. */
inline std::function<void()> openGroupSettings;

/** Set by the main window: makes a track stack from the selected tracks. */
inline std::function<void (bool summing)> createStack;

/** "Track Output": record (or hear) another track's or bus's output on these tracks instead of an interface input
    (the other track as it sounds, after its fader). Choosing an interface input clears it. */
inline void addTrackInputItems (juce::PopupMenu& menu, juce::ValueTree project, juce::ValueTree track, juce::Array<juce::ValueTree> targets)
{
    const auto current = track[ids::inputTrack].toString();
    juce::PopupMenu outputs;
    for (const auto& t : project)
    {
        if (! t.hasType (ids::TRACK) || model::isVcaTrack (t) || t == track)
            continue;
        const auto id = t[ids::id].toString();
        outputs.addItem ((model::isBusTrack (t) ? "Bus: " : "") + t[ids::name].toString(), true, id == current, [targets, id]() mutable
        {
            for (auto& target : targets)
                if (! model::isBusTrack (target) && ! model::isInstrumentTrack (target) && target[ids::id].toString() != id)
                {
                    target.setProperty (ids::inputTrack, id, nullptr);
                    target.setProperty (ids::inputStereo, true, nullptr);
                    target.setProperty (ids::channelFormat, "stereo", nullptr);
                }
        });
    }
    menu.addSubMenu ("Track or Bus Output", outputs, outputs.getNumItems() > 0, nullptr, current.isNotEmpty());
}

/** The tracks a mute, solo or arm on `track` acts on: the selection (when the track is in it) and their groups. */
inline juce::Array<juce::ValueTree> linkedTracks (const juce::ValueTree& project, const juce::ValueTree& track,
                                                  const juce::StringArray& selectedIds, const juce::String& what = {})
{
    juce::Array<juce::ValueTree> result;
    juce::Array<juce::ValueTree> start { track };
    // The other selected channels join in, but only channels of the same sort: a bus's fader
    // never moves track faders (or the other way round), and a VCA only moves VCAs.
    const auto sortOf = [] (const juce::ValueTree& t) { return model::isVcaTrack (t) ? 2 : model::isBusTrack (t) ? 1 : 0; };
    if (selectedIds.contains (track[ids::id].toString()))
        for (const auto& id : selectedIds)
            if (auto t = model::findById (project, id); t.isValid() && sortOf (t) == sortOf (track))
                start.addIfNotAlreadyThere (t);

    for (const auto& t : start)
    {
        result.addIfNotAlreadyThere (t);
        if (what.isEmpty() || model::groupLinks (project, (int) t.getProperty (ids::group, 0), what))
            for (const auto& member : model::groupMembers (project, t))
                result.addIfNotAlreadyThere (member);
    }
    return result;
}

/** The tracks an edit on `track` applies to, as in Logic: every selected channel when this one is
    selected (inserts, sends, pan, input, output...), else just this one. VCAs only join VCAs. */
inline juce::Array<juce::ValueTree> actingTracks (const juce::ValueTree& project, const juce::ValueTree& track,
                                                  const juce::StringArray& selectedIds)
{
    juce::Array<juce::ValueTree> result { track };
    if (! track.isValid() || ! selectedIds.contains (track[ids::id].toString()))
        return result;
    for (const auto& id : selectedIds)
        if (auto t = model::findById (project, id); t.isValid() && model::isVcaTrack (t) == model::isVcaTrack (track))
            result.addIfNotAlreadyThere (t);
    return result;
}

/** Where a track's output goes, as a short label: "St Out", a bus's name, or "Out 3-4". */
inline juce::String outputLabel (const juce::ValueTree& project, const juce::ValueTree& track)
{
    const auto id = track[ids::output].toString();
    if (const auto [first, count] = model::hardwareOutput (id); first >= 0)
        return "Out " + juce::String (first + 1) + (count == 2 ? "-" + juce::String (first + 2) : juce::String());
    if (const auto bus = model::findById (project, id); bus.isValid())
        return bus[ids::name].toString();
    return "St Out";
}

/** The Output menu (the Stereo Out, buses, the interface's other outputs, a new bus), as on the mixer strip. */
inline void addOutputItems (juce::PopupMenu& menu, juce::ValueTree project, juce::ValueTree track, juce::UndoManager& um)
{
    const auto current = track[ids::output].toString();
    auto set = [t = track, &um] (const juce::String& id) mutable
    {
        um.beginNewTransaction ("Output");
        if (id.isEmpty())
            t.removeProperty (ids::output, &um);
        else if (t[ids::id].toString() != id && ! model::outputWouldLoop (t.getParent(), t, id))
            t.setProperty (ids::output, id, &um);
    };
    menu.addSectionHeader ("Output");
    menu.addItem ("Stereo Out", true, current.isEmpty(), [set]() mutable { set ({}); });
    for (const auto& bus : project)
        if (bus.hasType (ids::TRACK) && model::isBusTrack (bus) && bus != track && ! model::outputWouldLoop (project, track, bus[ids::id].toString()))
            menu.addItem (bus[ids::name].toString(), true, current == bus[ids::id].toString(), [set, id = bus[ids::id].toString()]() mutable { set (id); });
    addHardwareOutputItems (menu, current, [set] (const juce::String& id) mutable { set (id); });
    menu.addSeparator();
    menu.addItem ("New Bus", [project, set, &um]() mutable
    {
        int buses = 0;
        for (const auto& c : project)
            buses += (c.hasType (ids::TRACK) && model::isBusTrack (c)) ? 1 : 0;
        um.beginNewTransaction ("Output to a new bus");
        const auto bus = model::addTrack (project, "Bus " + juce::String (buses + 1), &um, -1, model::busKind);
        set (bus[ids::id].toString());
    });
}

/**
    Output, group, VCA, stack and freeze items for a track, shared by the
    track header's menu and the mixer strip's.
*/
inline void addRoutingItems (juce::PopupMenu& menu, juce::ValueTree project, juce::ValueTree track, juce::UndoManager& um)
{
    const auto trackId = track[ids::id].toString();
    const bool isBus = model::isBusTrack (track), isVcaFlag = model::isVcaTrack (track);
    const auto isVca = [isVcaFlag] { return isVcaFlag; };

    // Output: the main output or a bus (buses and summing stacks too: into another bus).
    juce::ignoreUnused (isBus);
    if (! isVca())
    {
        juce::PopupMenu output;
        const auto current = track[ids::output].toString();
        output.addItem ("Main Output", true, current.isEmpty(), [t = track, &um]() mutable
        {
            um.beginNewTransaction ("Output");
            t.removeProperty (ids::output, &um);
        });
        for (const auto& bus : project)
            if (bus.hasType (ids::TRACK) && model::isBusTrack (bus) && bus != track
                && ! model::outputWouldLoop (project, track, bus[ids::id].toString()))
                output.addItem (bus[ids::name].toString(), true, current == bus[ids::id].toString(), [t = track, id = bus[ids::id].toString(), &um]() mutable
                {
                    um.beginNewTransaction ("Output");
                    t.setProperty (ids::output, id, &um);
                });
        addHardwareOutputItems (output, current, [t = track, &um] (const juce::String& id) mutable
        {
            um.beginNewTransaction ("Output");
            t.setProperty (ids::output, id, &um);
        });
        menu.addSubMenu ("Output", output);
    }

    // Group: linked volume, mute, solo and record.
    {
        juce::PopupMenu groups;
        const int current = track.getProperty (ids::group, 0);
        groups.addItem ("No Group", true, current == 0, [t = track, &um]() mutable
        {
            um.beginNewTransaction ("Group");
            t.removeProperty (ids::group, &um);
        });
        for (int g = 1; g <= 8; ++g)
        {
            int members = 0;
            for (const auto& other : project)
                members += other.hasType (ids::TRACK) && (int) other.getProperty (ids::group, 0) == g ? 1 : 0;
            groups.addItem (model::groupName (project, g) + (members > 0 ? "  (" + juce::String (members) + ")" : juce::String()),
                            true, current == g, [t = track, g, &um]() mutable
            {
                um.beginNewTransaction ("Group");
                t.setProperty (ids::group, g, &um);
            });
        }
        if (openGroupSettings)
        {
            groups.addSeparator();
            groups.addItem ("Group Settings...", [] { openGroupSettings(); });
        }
        menu.addSubMenu (current > 0 ? model::groupName (project, current) : juce::String ("Group"), groups);
    }

    // VCA
    {
        juce::PopupMenu vcas;
        const auto current = track[ids::vca].toString();
        vcas.addItem ("No VCA", true, current.isEmpty(), [t = track, &um]() mutable
        {
            um.beginNewTransaction ("VCA");
            t.removeProperty (ids::vca, &um);
        });
        for (const auto& vca : project)
            if (vca.hasType (ids::TRACK) && model::isVcaTrack (vca) && vca != track)
                vcas.addItem (vca[ids::name].toString(), true, current == vca[ids::id].toString(), [t = track, id = vca[ids::id].toString(), &um]() mutable
                {
                    um.beginNewTransaction ("VCA");
                    t.setProperty (ids::vca, id, &um);
                });
        vcas.addSeparator();
        vcas.addItem ("New VCA for this track", [project, t = track, &um]() mutable
        {
            um.beginNewTransaction ("New VCA");
            auto vca = model::addTrack (project, "VCA", &um, -1, model::vcaKind);
            t.setProperty (ids::vca, vca[ids::id].toString(), &um);
        });
        menu.addSubMenu ("VCA", vcas);
    }

    // MIDI Learn
    if (learnController)
    {
        juce::PopupMenu learn;
        for (auto param : { "volume", "pan", "mute", "solo" })
            learn.addItem (juce::String (param).substring (0, 1).toUpperCase() + juce::String (param).substring (1),
                           [t = track, p = juce::String (param)] { learnController (t, p); });
        menu.addSubMenu ("Learn MIDI Controller", learn);
    }

    // Track stacks
    menu.addSeparator();
    if (createStack)
    {
        menu.addItem ("Create Folder Stack (selected tracks)", [] { createStack (false); });
        menu.addItem ("Create Summing Stack (selected tracks)", [] { createStack (true); });
    }
    if (track[ids::stack].toString().isNotEmpty())
        menu.addItem ("Flatten Stack", [project, t = track, &um]() mutable
        {
            um.beginNewTransaction ("Flatten stack");
            model::flattenStack (project, t, &um);
        });

    // Freeze
    if (! isBus && ! isVca() && toggleFreeze)
        menu.addItem ((bool) track[ids::frozen] ? "Unfreeze Track" : "Freeze Track", [t = track] { toggleFreeze (t); });
}

} // namespace riff::routing
