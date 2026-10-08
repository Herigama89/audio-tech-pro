#pragma once

#include <JuceHeader.h>

namespace riff
{

/** A stereo delay of a whole number of samples, for delay compensation. Allocation-free once built. */
class DelayLine
{
public:
    static constexpr int maxDelay = 8191; // about 170 ms at 48 kHz

    DelayLine() : buffer (2, maxDelay + 1) { buffer.clear(); }

    void clear() noexcept { buffer.clear(); position = 0; }

    void process (float* left, float* right, int numSamples, int delay) noexcept
    {
        delay = juce::jlimit (0, maxDelay, delay);
        if (delay == 0)
            return;

        auto* bl = buffer.getWritePointer (0);
        auto* br = buffer.getWritePointer (1);
        constexpr int size = maxDelay + 1;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto read = (position - delay + size) % size;
            const auto l = bl[read], r = br[read];
            bl[position] = left[i];
            br[position] = right[i];
            left[i] = l;
            right[i] = r;
            position = (position + 1) % size;
        }
    }

private:
    juce::AudioBuffer<float> buffer;
    int position = 0;
};

} // namespace riff
