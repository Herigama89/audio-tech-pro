#pragma once

#include <JuceHeader.h>

/**
    Time stretching that keeps the pitch, for the Stretch tool, Flex, transposing and the Audio File Editor.

    The audio is cut at its attacks (drum hits, plucks, the starts of notes). Each attack's first few
    milliseconds are kept exactly as they are, at their new place in time, so hits stay sharp and on the
    grid; the rest of each piece is stretched to fill the gap with a phase vocoder that keeps the phases
    of each partial locked to its peak (so chords and voices stay clear rather than phasey). Stereo is
    stretched as mid and side, so the image holds.
*/
namespace riff::stretch
{
    struct Options
    {
        bool keepAttacks = true;     // keep transients sharp (off: the phase vocoder throughout)
        double attackMs = 10.0;      // how much of each attack is kept as it is
    };

    /** `ratio` > 1 makes it longer (2: twice as long); kept within 0.1 .. 10. */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double ratio, double sampleRate, const Options& options = {});

    /** Where the attacks are (sample positions, in order). */
    std::vector<int> findAttacks (const juce::AudioBuffer<float>& input, double sampleRate);
}
