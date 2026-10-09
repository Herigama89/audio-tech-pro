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

    /** Automation sets a parameter (audio thread): built-in effects by their own range,
        plugins by the parameter's normalised 0..1 value. */
    virtual void setAutomatedParameter (int /*index*/, float /*value*/) noexcept {}

    /** How many samples late this effect's output is (for delay compensation). */
    virtual int getLatencySamples() const { return 0; }

    double getPreparedSampleRate() const noexcept { return preparedRate; }

protected:
    double preparedRate = 0.0;
};

} // namespace riff
