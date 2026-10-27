#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

/**
    Nemix's wavetables: each a row of frames (single cycles) that the Position knob moves through.

    Every table is made once, on first use, from harmonic spectra, and kept at several band limits (mip levels:
    512 harmonics, 256, ... 2), so a high note reads a level without harmonics above half the sample rate and
    nothing aliases. Reading is lock-free; making one happens on the message thread (get() before playing).
*/
namespace riff::nemix::wt
{
    constexpr int frameSize = 1024;   // samples per cycle
    constexpr int frames = 32;        // frames per table
    constexpr int levels = 9;         // band limits: 512 >> level harmonics
    constexpr int maxHarmonic = 512;

    struct Table
    {
        // [level][frame * (frameSize + 1) + sample], the last sample of each frame repeating its first (for interpolation)
        std::array<std::vector<float>, levels> data;
        juce::String name;
    };

    /** The table (made now if it hasn't been). Message thread, or any thread that may take a while. */
    const Table& get (int index);

    /** The table if it's ready, else nullptr (audio thread). */
    const Table* ready (int index) noexcept;

    /** A table made from your own audio (a single cycle, or several cut into frames): replaces the slot `index`. */
    void makeFromAudio (int index, const float* samples, int numSamples, int cycleLength);

    /** The level to read for a phase step (cycles per sample). */
    inline int levelFor (double increment) noexcept
    {
        // highest harmonic heard = maxHarmonic >> level; keep it under half the rate: (512 >> level) * inc < 0.5
        const auto most = 0.5 / std::max (1.0e-9, increment);
        int level = 0;
        while (level < levels - 1 && (double) (maxHarmonic >> level) > most)
            ++level;
        return level;
    }

    /** A sample from a table: `position` across its frames (0..1), `phase` through the cycle (0..1). */
    inline float read (const Table& t, int level, float position, double phase) noexcept
    {
        const auto& d = t.data[(size_t) level];
        const auto fp = juce::jlimit (0.0f, 1.0f, position) * (float) (frames - 1);
        const auto f0 = std::min ((int) fp, frames - 2);
        const auto ff = fp - (float) f0;
        const auto sp = (float) (phase * frameSize);
        const auto s0 = std::min ((int) sp, frameSize - 1);
        const auto sf = sp - (float) s0;
        const auto* a = d.data() + (size_t) f0 * (frameSize + 1) + (size_t) s0;
        const auto* b = a + (frameSize + 1);
        const auto va = a[0] + sf * (a[1] - a[0]);
        const auto vb = b[0] + sf * (b[1] - b[0]);
        return va + ff * (vb - va);
    }

    /** One frame at full band for drawing (frameSize values). */
    void frameForDisplay (int index, float position, float* dest);
}
