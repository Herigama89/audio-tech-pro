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

        bool monitorInput = false;  // armed + monitor: pass the input through this track
        int inputChannel = 0;       // index among the device's active inputs
        bool inputStereo = false;
    };

    static constexpr int maxTracks = 256;

    double sampleRate = 44100.0;
    float masterGain = 0.8f;

    bool metronome = false;
    bool cycleEnabled = false;
    juce::int64 cycleStart = 0, cycleEnd = 0;   // samples; playback wraps from end to start
    double secondsPerBeat = 0.5;
    int beatsPerBar = 4;
    std::vector<Track> tracks;
};

} // namespace riff
