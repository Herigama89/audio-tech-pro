#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    An insert effect: processes stereo audio in place.

    prepare() runs on the message thread while the audio thread isn't using
    the effect. process() runs on the audio thread and must not allocate,
    lock or block.
*/
class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (float* left, float* right, int numSamples) noexcept = 0;
    virtual juce::String getName() const = 0;

    double getPreparedSampleRate() const noexcept { return preparedRate; }

protected:
    double preparedRate = 0.0;
};

} // namespace riff
