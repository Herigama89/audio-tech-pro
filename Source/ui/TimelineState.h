#pragma once

#include <JuceHeader.h>
#include "model/Identifiers.h"

namespace riff
{

/**
    View state shared by the ruler, track headers and tracks area: zoom,
    scroll and selection. Not part of the project and not undoable.
*/
struct TimelineState : public juce::ChangeBroadcaster
{
    static constexpr int barRulerHeight = 28;               // the gold bar ruler (cycle in its top half)
    static constexpr int markerHeight = 18;                 // the marker track below it
    static constexpr int rulerHeight = barRulerHeight + markerHeight;   // everything above the tracks
    static constexpr int trackHeight = 76;        // a track's height until you resize it
    static constexpr int minTrackHeight = 30, maxTrackHeight = 400;
    static constexpr double minPixelsPerSecond = 2.0;
    static constexpr double maxPixelsPerSecond = 2000.0;

    double pixelsPerSecond = 60.0;
    double scrollSeconds = 0.0;      // time at the left edge of the tracks area
    int scrollY = 0;                 // pixels scrolled down in the track list

    juce::String selectedTrackId;    // the selected track (with several selected: the one clicked last)
    juce::String selectedRegionId;
    juce::String selectedMarkerId;

    /** Several selected tracks (Shift / Cmd-click). Only counts while it holds selectedTrackId,
        so setting selectedTrackId directly simply selects that one track. */
    juce::StringArray selectedTracks;

    /** Several selected regions (Shift-click, or drag a rectangle over them). Like selectedTracks,
        it only counts while it holds selectedRegionId. */
    juce::StringArray selectedRegions;

    bool isRegionSelected (const juce::String& id) const
    {
        return id.isNotEmpty() && (id == selectedRegionId || (selectedRegions.contains (selectedRegionId) && selectedRegions.contains (id)));
    }

    /** The selected regions (each one a REGION in the project). */
    juce::Array<juce::ValueTree> selectedRegionTrees() const
    {
        juce::Array<juce::ValueTree> result;
        for (const auto& t : project)
            if (t.hasType (ids::TRACK))
                for (const auto& r : t)
                    if (r.hasType (ids::REGION) && isRegionSelected (r[ids::id].toString()))
                        result.add (r);
        return result;
    }

    void selectOnlyRegion (const juce::String& id)
    {
        selectedRegionId = id;
        selectedRegions = { id };
    }

    void selectRegions (const juce::StringArray& regionIds)
    {
        selectedRegions = regionIds;
        selectedRegionId = regionIds[regionIds.size() - 1];
    }

    /** Shift-click on a region: adds or removes it. */
    void toggleRegionSelection (const juce::String& id)
    {
        juce::StringArray current;
        for (const auto& r : selectedRegionTrees())
            current.add (r[ids::id].toString());
        if (current.contains (id))
            current.removeString (id);
        else
            current.add (id);
        selectRegions (current);
    }

    // --- Tools, snap and drag modes (Logic's tool menu and the Snap / Drag menus) ---------
    enum class Tool { pointer, pencil, eraser, scissors, glue, mute, fade };
    Tool tool = Tool::pointer;

    enum class Snap { smart, bar, beat, division, off };
    Snap snapMode = Snap::smart;

    enum class Drag { overlap, noOverlap, xfade, shuffleRight, shuffleLeft };
    Drag dragMode = Drag::overlap;

    /** Show hidden tracks (Logic's H): they're drawn dimmed instead of left out. */
    bool showHiddenTracks = false;

    bool isTrackSelected (const juce::String& id) const
    {
        return id.isNotEmpty() && (id == selectedTrackId || (selectedTracks.contains (selectedTrackId) && selectedTracks.contains (id)));
    }

    /** The selected tracks, top to bottom. */
    juce::StringArray selectedTrackIds() const
    {
        juce::StringArray result;
        for (const auto& t : project)
            if (t.hasType (ids::TRACK) && isTrackSelected (t[ids::id].toString()))
                result.add (t[ids::id].toString());
        return result;
    }

    void selectOnly (const juce::String& id)
    {
        selectedTrackId = id;
        selectedTracks = { id };
    }

    /** Cmd-click: adds or removes one track. */
    void toggleTrackSelection (const juce::String& id)
    {
        auto current = selectedTrackIds();
        if (current.contains (id) && current.size() > 1)
        {
            current.removeString (id);
            selectedTracks = current;
            selectedTrackId = current[current.size() - 1];
        }
        else
        {
            current.addIfNotAlreadyThere (id);
            selectedTracks = current;
            selectedTrackId = id;
        }
    }

    /** Shift-click: everything from the selected track to this one. */
    void extendTrackSelection (const juce::String& id)
    {
        int from = -1, to = -1, i = 0;
        for (const auto& t : project)
        {
            if (! t.hasType (ids::TRACK))
                continue;
            if (t[ids::id].toString() == selectedTrackId) from = i;
            if (t[ids::id].toString() == id) to = i;
            ++i;
        }
        if (from < 0 || to < 0)
        {
            selectOnly (id);
            return;
        }

        selectedTracks.clear();
        for (int k = juce::jmin (from, to); k <= juce::jmax (from, to); ++k)
            if (! isHidden (k))
                selectedTracks.add (project.getChild (k)[ids::id].toString());
        selectedTrackId = id;
    }

    bool isHidden (int index) const
    {
        return ! showHiddenTracks && (bool) project.getChild (index).getProperty (ids::hidden, false);
    }

    /** The project, for each track's own height (its "height" property, set by dragging its lower edge). */
    juce::ValueTree project;

    double xToTime (float x) const noexcept { return scrollSeconds + (double) x / pixelsPerSecond; }
    float timeToX (double t) const noexcept { return (float) ((t - scrollSeconds) * pixelsPerSecond); }

    /** A track's height in pixels (rows past the last track, like "New track", are the default height). */
    int heightOf (int index) const noexcept
    {
        const auto track = project.getChild (index);
        if (! track.hasType (ids::TRACK))
            return trackHeight;
        if (isHidden (index))
            return 0;
        return juce::jlimit (minTrackHeight, maxTrackHeight, (int) track.getProperty (ids::height, trackHeight));
    }

    /** Where a track starts, from the top of the tracks area (ruler included), scrolled. */
    int trackTop (int index) const noexcept
    {
        int y = rulerHeight - scrollY;
        for (int i = 0; i < index; ++i)
            y += heightOf (i);
        return y;
    }

    /** The track at a y position (can be past the last track), or -1 in the ruler. */
    int trackIndexAt (int y) const noexcept
    {
        if (y < rulerHeight)
            return -1;
        int top = rulerHeight - scrollY;
        int i = 0;
        for (; project.getChild (i).hasType (ids::TRACK); ++i)
        {
            const auto h = heightOf (i);
            if (y < top + h)
                return i;
            top += h;
        }
        return i + (y - top) / trackHeight;
    }

    /** All the tracks' heights, plus `extraRows` default rows below. */
    int contentHeight (int extraRows = 1) const noexcept
    {
        int h = extraRows * trackHeight;
        for (int i = 0; project.getChild (i).hasType (ids::TRACK); ++i)
            h += heightOf (i);
        return h;
    }

    /** Call after changing anything above. Synchronous so scrolled views stay in lock-step. */
    void changed() { sendSynchronousChangeMessage(); }
};

} // namespace riff
