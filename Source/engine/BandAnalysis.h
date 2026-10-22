#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

/**
    The analyzers' bands (the MultiMeter's, the master's MM window): third (or sixth) octaves from 20 Hz to
    20 kHz, every octave the same width on screen - 20 to 200 Hz takes as much room as 2 to 20 kHz - and each
    band's power the spectrum integrated over exactly its range (partial bins counted by how much of them it
    covers), so a low band narrower than an FFT bin reads its share, not a whole bin.

    Tilt: dB per octave about 1 kHz, added to each band, so a balanced mix (which has less energy per band as
    it goes up) reads level from left to right; 0 shows the raw band powers, where pink noise is flat.
*/
namespace riff::bands
{
    constexpr double lowHz = 20.0, highHz = 20000.0;

    /** Band b's edges and its centre, for `count` bands across 20 Hz - 20 kHz. */
    inline double edge (int b, int count) noexcept { return lowHz * std::pow (highHz / lowHz, (double) b / (double) count); }
    inline double centre (int b, int count) noexcept { return std::sqrt (edge (b, count) * edge (b + 1, count)); }

    /** Where a frequency sits across, 0..1 (log: equal octaves). */
    inline double position (double hz) noexcept { return std::log (std::max (hz, 1.0) / lowHz) / std::log (highHz / lowHz); }

    /** The tilt's lift at a frequency (dB per octave about 1 kHz). */
    inline double tiltDb (double hz, double dbPerOctave) noexcept { return dbPerOctave * std::log2 (std::max (hz, 1.0) / 1000.0); }

    /** The tilt choices offered (as Logic-style menu text) and their slopes. */
    inline const juce::StringArray& tiltNames()
    {
        static const juce::StringArray names { "Off", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
        return names;
    }
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
}
