#pragma once

#include "AudioFileCache.h"
#include <vector>

namespace riff
{

/**
    An immutable snapshot of everything the audio thread needs to render the
    project, in samples at the device rate.

    Built on the message thread from the project model, handed to the engine,
    and never modified afterwards. A new edit means a new snapshot.
*/
struct PlaybackGraph
{
    struct Region
    {
        SharedAudio audio;          // stereo, at `sampleRate`
        juce::int64 start = 0;      // timeline position
        juce::int64 sourceOffset = 0;
        juce::int64 length = 0;
        float gain = 1.0f;
    };

    struct Track
    {
        int slot = 0;               // stable per-track index for smoothing state in the engine
        float volume = 1.0f;        // linear
        float pan = 0.0f;           // -1 .. +1
        bool audible = true;        // already accounts for mute and solo
        std::vector<Region> regions;
    };

    static constexpr int maxTracks = 256;

    double sampleRate = 44100.0;
    float masterGain = 0.8f;
    std::vector<Track> tracks;
};

} // namespace riff
