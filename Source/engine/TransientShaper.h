#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace riff
{

/**
    A channel's transient shaper (the mixer's TRANS knob): a fast and a slow envelope of the sound; where the fast
    one runs ahead of the slow one is a hit's front. Turned up (to +100) the fronts come up to 12 dB louder - more
    snap; turned down (to -100) they go down and the body that follows stays - softer, more sustain.
    Audio thread only.
*/
class TransientShaper
{
public:
    void reset() noexcept { fast = slow = 0.0f; gain = 1.0f; }

    /** amount -1..1. */
    void process (float* l, float* r, int n, float amount, double rate) noexcept
    {
        if (std::abs (amount) < 1.0e-3f)
        {
            reset();
            return;
        }
        if (rate != preparedRate)
        {
            preparedRate = rate;
            const auto coeff = [rate] (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) rate)); };
            fastUp = coeff (0.3f);   fastDown = coeff (30.0f);
            slowUp = coeff (25.0f);  slowDown = coeff (250.0f);
            smooth = coeff (1.5f);
        }
        for (int i = 0; i < n; ++i)
        {
            const auto x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            fast += (x > fast ? fastUp : fastDown) * (x - fast);
            slow += (x > slow ? slowUp : slowDown) * (x - slow);
            const auto front = juce::jlimit (0.0f, 1.0f, (fast - slow) / (slow + 1.0e-3f));
            const auto target = std::pow (10.0f, amount * 12.0f * front * 0.05f);
            gain += smooth * (target - gain);
            l[i] *= gain;
            r[i] *= gain;
        }
    }

private:
    float fast = 0.0f, slow = 0.0f, gain = 1.0f;
    float fastUp = 0.1f, fastDown = 0.01f, slowUp = 0.01f, slowDown = 0.001f, smooth = 0.02f;
    double preparedRate = 0.0;
};

} // namespace riff
