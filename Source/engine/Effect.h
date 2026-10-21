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

    /** The most gain reduction (dB, positive) since the last call, for the strip's gain reduction meter.
        Compressors and limiters report it; other effects say 0. Message thread. */
    virtual float takeGainReductionDb() noexcept { return 0.0f; }

    /** Where the song is for the coming block (audio thread): plugins read it as their host's playhead
        (tempo-synced delays, and ARA plugins that play the regions themselves). */
    virtual void setPosition (juce::int64 /*samplePosition*/, bool /*playing*/, double /*bpm*/) noexcept {}

    /** The side chain for the coming block (audio thread): another track's output, or nullptr for none.
        Effects with a side-chain input (plugins with a second input bus) hear it there. */
    virtual void setSidechain (const float* /*left*/, const float* /*right*/) noexcept {}

    /** True if it has a side-chain input to feed. */
    virtual bool acceptsSidechain() const { return false; }

    /** Hands the block it just produced to anything watching it (audio thread): the window's analyzer. */
    virtual void tapOutput (const float* /*left*/, const float* /*right*/, int /*numSamples*/) noexcept {}

    /** How many samples late this effect's output is (for delay compensation). */
    virtual int getLatencySamples() const { return 0; }

    double getPreparedSampleRate() const noexcept { return preparedRate; }

protected:
    double preparedRate = 0.0;
};

} // namespace riff
