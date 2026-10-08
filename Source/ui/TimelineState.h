#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    View state shared by the ruler, track headers and tracks area: zoom,
    scroll and selection. Not part of the project and not undoable.
*/
struct TimelineState : public juce::ChangeBroadcaster
{
    static constexpr int rulerHeight = 28;
    static constexpr int trackHeight = 76;
    static constexpr double minPixelsPerSecond = 2.0;
    static constexpr double maxPixelsPerSecond = 2000.0;

    double pixelsPerSecond = 60.0;
    double scrollSeconds = 0.0;      // time at the left edge of the tracks area
    int scrollY = 0;                 // pixels scrolled down in the track list

    juce::String selectedTrackId;
    juce::String selectedRegionId;

    double xToTime (float x) const noexcept { return scrollSeconds + (double) x / pixelsPerSecond; }
    float timeToX (double t) const noexcept { return (float) ((t - scrollSeconds) * pixelsPerSecond); }
    int trackTop (int index) const noexcept { return rulerHeight + index * trackHeight - scrollY; }
    int trackIndexAt (int y) const noexcept { return y < rulerHeight ? -1 : (y - rulerHeight + scrollY) / trackHeight; }

    /** Call after changing anything above. Synchronous so scrolled views stay in lock-step. */
    void changed() { sendSynchronousChangeMessage(); }
};

} // namespace riff
