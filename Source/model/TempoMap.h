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

    /** Time signatures: from `startBar` (0-based) on, `beatsPerBar` beats a bar. */
    struct Meter
    {
        int startBar = 0;
        double startBeat = 0.0;
        int beatsPerBar = 4;
    };
    std::vector<Meter> meters { Meter {} };

    void setBaseMeter (int beatsPerBar) { meters.front().beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 4; }

    /** A time signature change at a bar (call in bar order). */
    void addMeterChange (int bar, int beatsPerBar)
    {
        if (beatsPerBar <= 0 || bar <= meters.back().startBar)
        {
            if (bar == meters.back().startBar && beatsPerBar > 0)
                meters.back().beatsPerBar = beatsPerBar;
            return;
        }
        Meter m;
        m.startBar = bar;
        m.startBeat = beatOfBar (bar);
        m.beatsPerBar = beatsPerBar;
        meters.push_back (m);
    }

    /** The beat a bar (0-based) starts on. */
    double beatOfBar (double bar) const noexcept
    {
        const Meter* m = &meters.front();
        for (const auto& candidate : meters)
            if (candidate.startBar <= bar)
                m = &candidate;
        return m->startBeat + (bar - m->startBar) * m->beatsPerBar;
    }

    /** The bar (0-based) a beat is in, and how far into it (beats). */
    std::pair<int, double> barAndBeat (double beat) const noexcept
    {
        const Meter* m = &meters.front();
        for (const auto& candidate : meters)
            if (candidate.startBeat <= beat + 1.0e-9)
                m = &candidate;
        const auto barsIn = std::floor ((beat - m->startBeat) / m->beatsPerBar + 1.0e-9);
        return { m->startBar + (int) barsIn, beat - m->startBeat - barsIn * m->beatsPerBar };
    }

    int beatsPerBarAt (double beat) const noexcept
    {
        const Meter* m = &meters.front();
        for (const auto& candidate : meters)
            if (candidate.startBeat <= beat + 1.0e-9)
                m = &candidate;
        return m->beatsPerBar;
    }

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
