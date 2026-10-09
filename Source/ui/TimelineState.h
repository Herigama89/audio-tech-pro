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
    static constexpr int rulerHeight = 28;
    static constexpr int trackHeight = 76;        // a track's height until you resize it
    static constexpr int minTrackHeight = 30, maxTrackHeight = 400;
    static constexpr double minPixelsPerSecond = 2.0;
    static constexpr double maxPixelsPerSecond = 2000.0;

    double pixelsPerSecond = 60.0;
    double scrollSeconds = 0.0;      // time at the left edge of the tracks area
    int scrollY = 0;                 // pixels scrolled down in the track list

    juce::String selectedTrackId;
    juce::String selectedRegionId;

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
