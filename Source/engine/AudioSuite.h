#pragma once

#include <JuceHeader.h>
#include "Effect.h"

namespace riff::audiosuite
{

/**
    Pro Tools' AudioSuite: a region's audio is run through an effect (one of
    ours, or a VST3 / AU / CLAP plugin) offline and written to a new file,
    which the region then plays. The original file is never touched.

    The new file covers the region plus "handles" (extra audio either side,
    so the region can still be trimmed longer), and an optional tail so a
    reverb or delay can ring out past the end. Plugin latency is removed.
*/
struct Options
{
    double handleSeconds = 2.0;   // extra audio kept either side of the region
    double tailSeconds = 0.0;     // extra time after the end for the effect to ring out
    bool wholeFile = false;       // process the region's whole file instead of the region (and its handles)
};

struct Result
{
    juce::File file;
    double offset = 0.0;          // where the region's audio now starts in the new file (seconds)
    juce::String error;
};

/** Runs `effect` over the region's audio and writes `outFile` (32-bit float WAV). The effect is prepared here. */
Result render (juce::AudioFormatManager& formats, const juce::File& source, double regionOffset, double regionLength,
               Effect& effect, const Options& options, const juce::File& outFile,
               std::function<bool (double)> progress = {});

} // namespace riff::audiosuite
