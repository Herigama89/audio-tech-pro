#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Loudness as broadcasters measure it (ITU-R BS.1770 / EBU R128), for the
    MultiMeter: K-weighted, in LUFS. Momentary (the last 400 ms), Short-term
    (the last 3 s) and Integrated (everything since the last reset, gated:
    blocks quieter than -70 LUFS, then those 10 LU under the average, are left out).
*/
class LoudnessMeter
{
public:
    void prepare (double rate)
    {
        sampleRate = rate > 0.0 ? rate : 48000.0;
        // Stage 1: the head's high shelf (+4 dB above ~1.7 kHz); stage 2: a high pass at ~38 Hz.
        {
            const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const auto vh = std::pow (10.0, gain / 20.0), vb = std::pow (vh, 0.4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            shelf = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                      2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const auto a0 = 1.0 + k / q + k * k;
            highPass = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        subBlockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
        reset();
    }

    void reset()
    {
        for (auto& c : channels)
            c = {};
        subBlocks.clear();
        blocks.clear();
        subSum = 0.0;
        subCount = 0;
    }

    void process (const float* left, const float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto l = channels[0].filter (left[i], shelf, highPass);
            const auto r = channels[1].filter (right != nullptr ? right[i] : left[i], shelf, highPass);
            subSum += l * l + r * r;
            if (++subCount >= subBlockLength)
                finishSubBlock();
        }
    }

    double momentary() const  { return loudnessOfLast (4); }
    double shortTerm() const  { return loudnessOfLast (30); }

    double integrated() const
    {
        // Gated: above -70 LUFS, then above (their average - 10 LU).
        double sum = 0.0;
        int count = 0;
        for (const auto e : blocks)
            if (toLufs (e) > -70.0)
            {
                sum += e;
                ++count;
            }
        if (count == 0)
            return -std::numeric_limits<double>::infinity();
        const auto relativeGate = toLufs (sum / count) - 10.0;
        sum = 0.0;
        count = 0;
        for (const auto e : blocks)
            if (toLufs (e) > -70.0 && toLufs (e) > relativeGate)
            {
                sum += e;
                ++count;
            }
        return count > 0 ? toLufs (sum / count) : -std::numeric_limits<double>::infinity();
    }

    /** The loudness range (LRA): how much the short-term loudness moves (10th to 95th percentile, gated), in LU. */
    double range() const
    {
        std::vector<double> values;
        for (size_t i = 30; i <= subBlocks.size(); i += 10)   // 3 s windows every second
        {
            double sum = 0.0;
            for (size_t k = i - 30; k < i; ++k)
                sum += subBlocks[k];
            const auto l = toLufs (sum / 30.0);
            if (l > -70.0)
                values.push_back (l);
        }
        if (values.size() < 2)
            return 0.0;
        double mean = 0.0;
        for (auto v : values)
            mean += std::pow (10.0, (v + 0.691) / 10.0);
        const auto gate = toLufs (mean / (double) values.size()) - 20.0;
        values.erase (std::remove_if (values.begin(), values.end(), [gate] (double v) { return v <= gate; }), values.end());
        if (values.size() < 2)
            return 0.0;
        std::sort (values.begin(), values.end());
        const auto at = [&values] (double p) { return values[(size_t) juce::jlimit (0.0, (double) values.size() - 1.0, std::round (p * (double) (values.size() - 1)))]; };
        return at (0.95) - at (0.10);
    }

    static double toLufs (double meanSquare)
    {
        return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -std::numeric_limits<double>::infinity();
    }

private:
    struct Coefficients { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Channel
    {
        double s1 = 0, s2 = 0, h1 = 0, h2 = 0;   // the two filters' states
        double filter (float x, const Coefficients& a, const Coefficients& b)
        {
            const auto y1 = a.b0 * x + s1;
            s1 = a.b1 * x - a.a1 * y1 + s2;
            s2 = a.b2 * x - a.a2 * y1;
            const auto y2 = b.b0 * y1 + h1;
            h1 = b.b1 * y1 - b.a1 * y2 + h2;
            h2 = b.b2 * y1 - b.a2 * y2;
            return y2;
        }
    };

    void finishSubBlock()
    {
        subBlocks.push_back (subSum / subBlockLength);
        subSum = 0.0;
        subCount = 0;
        if (subBlocks.size() > maxSubBlocks)
            subBlocks.erase (subBlocks.begin(), subBlocks.begin() + (std::ptrdiff_t) (subBlocks.size() - maxSubBlocks));
        // A 400 ms gating block every 100 ms (75% overlap)
        if (subBlocks.size() >= 4)
        {
            double sum = 0.0;
            for (size_t k = subBlocks.size() - 4; k < subBlocks.size(); ++k)
                sum += subBlocks[k];
            blocks.push_back (sum / 4.0);
        }
    }

    double loudnessOfLast (int count) const
    {
        if ((int) subBlocks.size() < count)
            count = (int) subBlocks.size();
        if (count == 0)
            return -std::numeric_limits<double>::infinity();
        double sum = 0.0;
        for (size_t k = subBlocks.size() - (size_t) count; k < subBlocks.size(); ++k)
            sum += subBlocks[k];
        return toLufs (sum / count);
    }

    static constexpr size_t maxSubBlocks = 10 * 60 * 60 * 3;   // three hours of 100 ms
    double sampleRate = 48000.0;
    Coefficients shelf, highPass;
    std::array<Channel, 2> channels {};
    int subBlockLength = 4800, subCount = 0;
    double subSum = 0.0;
    std::vector<double> subBlocks, blocks;
};

} // namespace riff
