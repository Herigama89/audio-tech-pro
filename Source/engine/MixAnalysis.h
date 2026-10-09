#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace riff
{

/**
    Measures a signal for mix advice: peak, RMS and how its energy splits
    between lows (below ~200 Hz), mids and highs (above ~4 kHz).
    The band split uses gentle one-pole filters: a guide, not a spectrum analyser.
*/
struct LevelStats
{
    float peak = 0.0f;
    double sumSquares = 0.0, low = 0.0, mid = 0.0, high = 0.0;
    juce::int64 samples = 0;

    void reset()
    {
        peak = 0.0f;
        sumSquares = low = mid = high = 0.0;
        samples = 0;
        lowState = highState = 0.0f;
    }

    void prepare (double sampleRate)
    {
        lowCoeff = coefficient (200.0, sampleRate);
        highCoeff = coefficient (4000.0, sampleRate);
    }

    void add (const float* left, const float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto l = left[i], r = right[i];
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            sumSquares += 0.5 * ((double) l * l + (double) r * r);

            const auto m = 0.5f * (l + r);
            lowState += lowCoeff * (m - lowState);
            highState += highCoeff * (m - highState);
            const auto lows = lowState, mids = highState - lowState, highs = m - highState;
            low += (double) lows * lows;
            mid += (double) mids * mids;
            high += (double) highs * highs;
        }
        samples += n;
    }

    static double toDb (double gain) { return gain > 1.0e-6 ? 20.0 * std::log10 (gain) : -120.0; }

    double peakDb() const { return toDb (peak); }
    double rmsDb() const  { return samples > 0 ? toDb (std::sqrt (sumSquares / (double) samples)) : -120.0; }
    bool isSilent() const { return peak < 1.0e-5f; }

    /** Share of the energy in each band, in percent (they add up to about 100). */
    void bandShares (double& lowPercent, double& midPercent, double& highPercent) const
    {
        const auto total = low + mid + high;
        lowPercent = total > 0.0 ? 100.0 * low / total : 0.0;
        midPercent = total > 0.0 ? 100.0 * mid / total : 0.0;
        highPercent = total > 0.0 ? 100.0 * high / total : 0.0;
    }

private:
    float lowCoeff = 0.03f, highCoeff = 0.4f, lowState = 0.0f, highState = 0.0f;

    static float coefficient (double cutoff, double sampleRate)
    {
        return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / juce::jmax (1.0, sampleRate)));
    }
};

} // namespace riff
