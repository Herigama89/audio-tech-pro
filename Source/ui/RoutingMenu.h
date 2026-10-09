#pragma once

#include <JuceHeader.h>
#include "../model/Project.h"

namespace riff::routing
{

/** Set by the main window: freezes or unfreezes a track (rendering takes a moment, with progress). */
inline std::function<void (juce::ValueTree track)> toggleFreeze;

/** Set by the main window: the next controller you move on a MIDI device controls this (MIDI Learn). */
inline std::function<void (juce::ValueTree track, juce::String param)> learnController;

/** Set by the main window: opens the Group Settings window. */
inline std::function<void()> openGroupSettings;

/** Set by the main window: makes a track stack from the selected tracks. */
inline std::function<void (bool summing)> createStack;

/** The tracks a mute, solo or arm on `track` acts on: the selection (when the track is in it) and their groups. */
inline juce::Array<juce::ValueTree> linkedTracks (const juce::ValueTree& project, const juce::ValueTree& track,
                                                  const juce::StringArray& selectedIds, const juce::String& what = {})
{
    juce::Array<juce::ValueTree> result;
    juce::Array<juce::ValueTree> start { track };
    if (selectedIds.contains (track[ids::id].toString()))
        for (const auto& id : selectedIds)
            if (auto t = model::findById (project, id); t.isValid())
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

/**
    Output, group, VCA, stack and freeze items for a track, shared by the
    track header's menu and the mixer strip's.
*/
inline void addRoutingItems (juce::PopupMenu& menu, juce::ValueTree project, juce::ValueTree track, juce::UndoManager& um)
{
    const auto trackId = track[ids::id].toString();
    const bool isBus = model::isBusTrack (track), isVcaFlag = model::isVcaTrack (track);
    const auto isVca = [isVcaFlag] { return isVcaFlag; };

    // Output: the main output or a bus.
    if (! isBus && ! isVca())
    {
        juce::PopupMenu output;
        const auto current = track[ids::output].toString();
        output.addItem ("Main Output", true, current.isEmpty(), [t = track, &um]() mutable
        {
            um.beginNewTransaction ("Output");
            t.removeProperty (ids::output, &um);
        });
        for (const auto& bus : project)
            if (bus.hasType (ids::TRACK) && model::isBusTrack (bus))
                output.addItem (bus[ids::name].toString(), true, current == bus[ids::id].toString(), [t = track, id = bus[ids::id].toString(), &um]() mutable
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
