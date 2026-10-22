#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

/**
    The analyzers' bands (the MultiMeter's, the master's MM window): third (or sixth) octaves from 20 Hz to
    20 kHz, every octave the same width on screen - 20 to 200 Hz takes as much room as 2 to 20 kHz - and each
    band's power the spectrum integrated over exactly its range (partial bins counted by how much of them it
    covers), so a low band narrower than an FFT bin reads its share, not a whole bin.

    What a bar shows (as Logic's MultiMeter, T-RackS's analyzer and hardware real-time analyzers): each band's
    own signal, through a third- (or sixth-) octave band-pass filter, read for its peak (or its RMS) - so a
    sine reads its level, pink noise reads level across the range, and a band of a loud master reads as high
    as its peaks really go (bass near -5 dB, the top near -10 dB). The tilt, if any, is dB per octave about
    1 kHz, added on top.
*/
namespace riff::bands
{
    constexpr double lowHz = 20.0, highHz = 20000.0;

    /** Band b's edges and its centre, for `count` bands across 20 Hz - 20 kHz. */
    inline double edge (int b, int count) noexcept { return lowHz * std::pow (highHz / lowHz, (double) b / (double) count); }
    inline double centre (int b, int count) noexcept { return std::sqrt (edge (b, count) * edge (b + 1, count)); }

    /** Where a frequency sits across, 0..1 (log: equal octaves). */
    inline double position (double hz) noexcept { return std::log (std::max (hz, 1.0) / lowHz) / std::log (highHz / lowHz); }

    /** The tilt's lift (dB) for a band centred at `hz`: dB per octave about 1 kHz. */
    inline double displayOffsetDb (double hz, double dbPerOctave) noexcept
    {
        return dbPerOctave * std::log2 (std::max (hz, 1.0) / 1000.0);
    }

    /** The tilt choices offered (as Logic-style menu text) and their slopes. */
    inline const juce::StringArray& tiltNames()
    {
        static const juce::StringArray names { "Off", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
        return names;
    }
    constexpr int defaultTilt = 0;   // none, as Logic and T-RackS
    inline double tiltSlope (int choice) noexcept { return 1.5 * juce::jlimit (0, 4, choice); }

    /** Each band's power from a power spectrum (power[k] for bins 0..n/2, binHz wide), into `out` (count bands). */
    inline void integrate (const float* power, int bins, double binHz, int count, std::vector<float>& out)
    {
        out.assign ((size_t) count, 0.0f);
        for (int b = 0; b < count; ++b)
        {
            const auto lo = edge (b, count), hi = edge (b + 1, count);
            // bin k covers (k - 0.5, k + 0.5) x binHz
            const auto k0 = std::max (1, (int) std::floor (lo / binHz + 0.5)), k1 = std::min (bins - 1, (int) std::floor (hi / binHz + 0.5));
            double sum = 0.0;
            for (int k = k0; k <= k1; ++k)
            {
                const auto binLo = ((double) k - 0.5) * binHz, binHi = ((double) k + 0.5) * binHz;
                const auto overlap = std::max (0.0, std::min (hi, binHi) - std::max (lo, binLo)) / binHz;
                sum += (double) power[k] * overlap;
            }
            out[(size_t) b] = (float) sum;
        }
    }
    /**
        The analyzer's filters: one band-pass per band (two 2nd-order sections, so a band's skirts fall away
        at 24 dB an octave), each band's peak and mean square gathered until they're taken. One thread feeds
        it (the audio's), another may take (the window's): values are atomics, so nothing locks.
    */
    class FilterBank
    {
    public:
        void prepare (double rate, int count)
        {
            sections.assign ((size_t) count * 2, {});
            peaks = std::make_unique<std::atomic<float>[]> ((size_t) count);
            squares = std::make_unique<std::atomic<double>[]> ((size_t) count);
            numBands = count;
            const auto perOctave = (double) count / std::log2 (highHz / lowHz);
            const auto ratio = std::pow (2.0, 1.0 / perOctave);
            const auto q = std::sqrt (ratio) / (ratio - 1.0) * 0.6436;   // two sections: -3 dB at the band's edges
            for (int b = 0; b < count; ++b)
            {
                const auto w0 = juce::MathConstants<double>::twoPi * std::min (centre (b, count), rate * 0.49) / rate;
                // (the bandwidth in octaves, kept as it is near the top, where the bilinear transform squeezes it)
                const auto octaves = 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q));
                const auto alpha = std::sin (w0) * std::sinh (std::log (2.0) / 2.0 * octaves * w0 / std::sin (w0));
                const auto a0 = 1.0 + alpha;
                Section s;
                s.b0 = alpha / a0; s.b2 = -alpha / a0;
                s.a1 = -2.0 * std::cos (w0) / a0; s.a2 = (1.0 - alpha) / a0;
                sections[(size_t) b * 2] = s;
                sections[(size_t) b * 2 + 1] = s;
                peaks[(size_t) b].store (0.0f);
                squares[(size_t) b].store (0.0);
            }
            samples.store (0);
        }

        int count() const noexcept { return numBands; }

        void process (const float* x, int n) noexcept
        {
            for (int b = 0; b < numBands; ++b)
            {
                auto& s1 = sections[(size_t) b * 2];
                auto& s2 = sections[(size_t) b * 2 + 1];
                float peak = 0.0f;
                double sum = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const auto y = s2.run (s1.run ((double) x[i]));
                    peak = std::max (peak, (float) std::abs (y));
                    sum += y * y;
                }
                if (peak > peaks[(size_t) b].load (std::memory_order_relaxed))
                    peaks[(size_t) b].store (peak, std::memory_order_relaxed);
                squares[(size_t) b].store (squares[(size_t) b].load (std::memory_order_relaxed) + sum, std::memory_order_relaxed);
            }
            samples.fetch_add (n, std::memory_order_release);
        }

        /** Each band's peak (linear) and mean square since the last take; false when no audio came. */
        bool take (float* peak, double* meanSquare) noexcept
        {
            const auto n = samples.exchange (0, std::memory_order_acquire);
            for (int b = 0; b < numBands; ++b)
            {
                peak[b] = peaks[(size_t) b].exchange (0.0f, std::memory_order_relaxed);
                const auto sum = squares[(size_t) b].exchange (0.0, std::memory_order_relaxed);
                meanSquare[b] = n > 0 ? sum / (double) n : 0.0;
            }
            return n > 0;
        }

    private:
        struct Section
        {
            double b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            double run (double x) noexcept
            {
                // (b1 is 0 for a band-pass)
                const auto y = b0 * x + z1;
                z1 = -a1 * y + z2;
                z2 = b2 * x - a2 * y;
                if (std::abs (z1) < 1.0e-20) z1 = 0.0;   // no denormals in the quiet
                if (std::abs (z2) < 1.0e-20) z2 = 0.0;
                return y;
            }
        };
        std::vector<Section> sections;
        std::unique_ptr<std::atomic<float>[]> peaks;
        std::unique_ptr<std::atomic<double>[]> squares;
        std::atomic<int> samples { 0 };
        int numBands = 0;
    };
}
