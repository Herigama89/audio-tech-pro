#pragma once

#include "BuiltInEffects.h"

namespace riff
{

/**
    An analog mic preamp on each channel, as a classic transformer-coupled, class-A console preamp: its Gain
    knob turns the channel up, and the harder it's driven the more it colours - the input transformer's core
    saturating first in the lows (thickness), the class-A stage's soft, lopsided clipping (mostly 2nd and 3rd
    harmonics, warm rather than harsh), the transformers' gentle low-end lift and their roll-off of the very
    top. At low gain it's nearly clean; past about +18 dB it's audibly driven; near +30 dB it's crunchy.

    Gain is gain: the channel gets louder as you turn it up (the saturation caps it about 6 dB above full
    scale), so pull the fader down to taste. No latency; the clipping is anti-aliased. Real-time safe.
*/
class PreampSaturator
{
public:
    struct Settings
    {
        bool enabled = false;
        float gainDb = 0.0f;   // 0..30

        bool operator== (const Settings&) const = default;
    };

    /** Reads a track's preamp (preOn, preGain in dB). */
    static Settings fromTrack (const juce::ValueTree& track);

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    /** The level the stage clips towards (linear: about +6 dBFS). */
    static constexpr float headroom = 2.0f;

private:
    void update() noexcept;
    float clip (float x, int channel) noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    Biquad lowsIn, lowsOut, lift, top, dcBlock;
    float gain = 1.0f, bias = 0.0f, biasOut = 0.0f;
    std::array<float, 2> previous {}, previousIntegral {};
    bool valid = false;
};

} // namespace riff
