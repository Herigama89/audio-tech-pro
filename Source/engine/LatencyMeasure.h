#pragma once

#include <JuceHeader.h>
#include <vector>

namespace riff::latency
{

/**
    Loopback latency measurement.

    The engine plays a few short test bursts while recording an input. With a
    cable from an output to an input (or a mic in front of a speaker), each
    burst comes back after the true round-trip delay. Cross-correlating the
    recording with the burst finds that delay to the sample.
*/

/** The test burst: a few cycles of a 2 kHz tone under a Hann window. */
std::vector<float> makeBurst (double sampleRate);

/** Where the bursts start in the measurement, in samples. */
std::vector<int> burstPositions (double sampleRate);

/** Total samples to capture. */
int measurementLength (double sampleRate);

struct Result
{
    bool ok = false;
    int delaySamples = 0;
    float confidence = 0.0f;   // normalised correlation of the weakest agreeing burst, 0..1
    juce::String error;
};

/** Finds the round-trip delay in a recording of the bursts (pure function, unit tested). */
Result estimateDelay (const float* recorded, int numSamples, double sampleRate);

} // namespace riff::latency
