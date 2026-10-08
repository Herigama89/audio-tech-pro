#pragma once

#include <JuceHeader.h>

namespace riff::resampler
{
    /**
        Adds `numSamples` of a file's audio, read at fractional positions, into
        left/right. Position `startPosition` is in the file's own samples, and
        each output sample moves `step` file samples on (file rate / engine
        rate). A 32-point windowed-sinc filter keeps it clean; positions before
        the start or past the end of the file read as silence.

        This is what lets a 44.1 kHz file play in a 48 kHz session the moment
        it's loaded, with nothing converted in advance. Real-time safe.
    */
    void addResampled (const juce::AudioBuffer<float>& source, double startPosition, double step, float gain,
                       float* left, float* right, int numSamples) noexcept;

    /** Builds the filter table. Call once on the message thread before audio runs. */
    void initialise();

    /** Points either side of the centre the filter reads. */
    constexpr int halfWidth = 16;
}
