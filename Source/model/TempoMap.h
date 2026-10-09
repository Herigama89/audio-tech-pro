#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

namespace riff
{

/**
    The song's tempo along the timeline: a base tempo and step changes placed
    on beats (quarter notes from the start). Converts between seconds and
    beats. Plain data, so the audio thread can use a copy (the metronome).
*/
struct TempoMap
{
    struct Segment
    {
        double startBeat = 0.0;       // where this tempo begins, in beats
        double startSeconds = 0.0;    // ... and in seconds
        double secondsPerBeat = 0.5;
    };

    std::vector<Segment> segments { Segment {} };   // always at least one, starting at 0

    static TempoMap constant (double secondsPerBeat)
    {
        TempoMap m;
        m.segments.front().secondsPerBeat = secondsPerBeat > 0.0 ? secondsPerBeat : 0.5;
        return m;
    }

    /** Adds a change (call in order of beat, after the first segment is set). */
    void addChange (double beat, double secondsPerBeat)
    {
        if (secondsPerBeat <= 0.0 || beat <= segments.back().startBeat + 1.0e-9)
        {
            if (std::abs (beat - segments.back().startBeat) < 1.0e-9 && secondsPerBeat > 0.0)
                segments.back().secondsPerBeat = secondsPerBeat;   // the same place: the later one wins
            return;
        }
        Segment s;
        s.startBeat = beat;
        s.startSeconds = secondsAt (beat);
        s.secondsPerBeat = secondsPerBeat;
        segments.push_back (s);
    }

    bool isConstant() const noexcept { return segments.size() == 1; }

    double beatAt (double seconds) const noexcept
    {
        const auto& s = segmentForSeconds (seconds);
        return s.startBeat + (seconds - s.startSeconds) / s.secondsPerBeat;
    }

    double secondsAt (double beat) const noexcept
    {
        const auto& s = segmentForBeat (beat);
        return s.startSeconds + (beat - s.startBeat) * s.secondsPerBeat;
    }

    double secondsPerBeatAt (double seconds) const noexcept { return segmentForSeconds (seconds).secondsPerBeat; }
    double bpmAt (double seconds) const noexcept            { return 60.0 / secondsPerBeatAt (seconds); }

private:
    const Segment& segmentForSeconds (double seconds) const noexcept
    {
        auto it = std::upper_bound (segments.begin(), segments.end(), seconds,
                                    [] (double t, const Segment& s) { return t < s.startSeconds; });
        return it == segments.begin() ? segments.front() : *(it - 1);
    }

    const Segment& segmentForBeat (double beat) const noexcept
    {
        auto it = std::upper_bound (segments.begin(), segments.end(), beat,
                                    [] (double b, const Segment& s) { return b < s.startBeat; });
        return it == segments.begin() ? segments.front() : *(it - 1);
    }
};

} // namespace riff
