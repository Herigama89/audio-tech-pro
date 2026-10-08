#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Something that turns MIDI into stereo audio: the built-in synth or a hosted plugin.

    prepare() runs on the message thread while the audio thread isn't using
    the instrument. render() runs on the audio thread and must not allocate,
    lock or block.
*/
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Overwrites `left` / `right` with `numSamples` of output for the events in `midi`. */
    virtual void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept = 0;

    virtual juce::String getName() const = 0;

    /** How many samples late this instrument's output is (for delay compensation). */
    virtual int getLatencySamples() const { return 0; }

    double getPreparedSampleRate() const noexcept { return preparedRate; }

protected:
    double preparedRate = 0.0;
};

} // namespace riff
