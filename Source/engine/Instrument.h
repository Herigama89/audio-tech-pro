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

    /** Where the song is and its tempo, before each render (audio thread): for LFOs, delays and arpeggios in time. */
    virtual void setPosition (juce::int64 /*samplePosition*/, bool /*playing*/, double /*bpm*/) noexcept {}

    /** Multi-output (as Logic's multi-output instruments): stereo pairs beyond the main one (outputs 3-4, 5-6...).
        After render() (same audio thread, same block), getExtraOutput gives each pair's channel; nullptr = silence. */
    virtual int getNumExtraOutputs() const noexcept { return 0; }
    virtual const float* getExtraOutput (int /*pair: 1 = outputs 3-4*/, int /*channel*/) const noexcept { return nullptr; }
    /** The name of an output pair (0 = the main one), for the mixer: "Kick", "Out 3-4"... (message thread). */
    virtual juce::String getOutputName (int pair) const { return pair == 0 ? juce::String ("Main") : "Out " + juce::String (pair * 2 + 1) + "-" + juce::String (pair * 2 + 2); }

    /** The studio instruments' parts' insert chains for the coming block (audio thread; a studio::PartChains, or null). */
    virtual void setPartChains (const void* /*chains*/) noexcept {}

    /** Automation of one of its own parameters (audio thread): `normalised` 0..1 across the parameter's range. */
    virtual void setAutomatedParameter (int /*index*/, float /*normalised*/) noexcept {}

    /** How many samples late this instrument's output is (for delay compensation). */
    virtual int getLatencySamples() const { return 0; }

    double getPreparedSampleRate() const noexcept { return preparedRate; }

protected:
    double preparedRate = 0.0;
};

} // namespace riff
