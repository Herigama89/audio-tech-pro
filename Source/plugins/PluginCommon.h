#pragma once

#include <JuceHeader.h>

/*
    What the Audio Tech Pro plug-ins (VST3 / AU: the Tube EQ, the Graphic EQ and the Channel Strip, to use
    in other DAWs) share: the last moment of their output, for the analyzer in their windows.
*/
namespace riff::plugin
{

/** The output's last 16k samples (mono), written by the audio thread, read by the window's analyzer. */
class AnalyzerTap
{
public:
    void push (const float* left, const float* right, int n) noexcept
    {
        auto w = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) (w & mask)] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            ++w;
        }
        write.store (w, std::memory_order_release);
    }

    /** The newest `count` samples (at most the ring's size), oldest first; how many. */
    int read (float* dest, int count) const noexcept
    {
        const auto w = write.load (std::memory_order_acquire);
        const auto n = (int) std::min<juce::int64> ({ (juce::int64) count, (juce::int64) size, (juce::int64) w });
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(size_t) ((w - n + i) & mask)];
        return n;
    }

private:
    static constexpr int size = 1 << 14;
    static constexpr juce::int64 mask = size - 1;
    std::array<float, size> ring {};
    std::atomic<juce::int64> write { 0 };
};

/** A peak level, held a little: written by the audio thread, read (and let fall) by the window. */
struct PeakLevel
{
    std::atomic<float> peak { 0.0f };
    void push (const float* l, const float* r, int n) noexcept
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i)
            m = std::max (m, std::max (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f));
        if (m > peak.load (std::memory_order_relaxed))
            peak.store (m, std::memory_order_relaxed);
    }
    float take() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }
};

} // namespace riff::plugin
