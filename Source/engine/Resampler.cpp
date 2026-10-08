#include "Resampler.h"
#include <array>
#include <vector>

namespace riff::resampler
{

namespace
{
    constexpr int taps = halfWidth * 2;
    constexpr int phases = 512;
    constexpr double cutoff = 0.94;   // of Nyquist: leaves room so downsampling barely aliases

    /** table[phase][tap]: the filter for a sample `phase / phases` of the way between two inputs. */
    struct Table
    {
        std::vector<float> data = std::vector<float> ((size_t) (phases + 1) * taps);

        Table()
        {
            for (int p = 0; p <= phases; ++p)
            {
                const auto frac = (double) p / phases;
                double sum = 0.0;
                std::array<double, taps> h {};

                for (int k = 0; k < taps; ++k)
                {
                    // tap k reads input i + (k - halfWidth + 1); its distance from the wanted point:
                    const auto x = (double) (k - halfWidth + 1) - frac;
                    const auto sinc = std::abs (x) < 1.0e-9 ? 1.0
                                                           : std::sin (juce::MathConstants<double>::pi * cutoff * x)
                                                               / (juce::MathConstants<double>::pi * cutoff * x);
                    // Blackman-Harris window over the filter's span
                    const auto t = (x + halfWidth) / (2.0 * halfWidth);
                    const auto w = t <= 0.0 || t >= 1.0 ? 0.0
                                 : 0.35875 - 0.48829 * std::cos (2 * juce::MathConstants<double>::pi * t)
                                   + 0.14128 * std::cos (4 * juce::MathConstants<double>::pi * t)
                                   - 0.01168 * std::cos (6 * juce::MathConstants<double>::pi * t);
                    h[(size_t) k] = cutoff * sinc * w;
                    sum += h[(size_t) k];
                }

                // Unity gain at DC for every phase
                for (int k = 0; k < taps; ++k)
                    data[(size_t) (p * taps + k)] = (float) (h[(size_t) k] / sum);
            }
        }
    };

    const Table& table()
    {
        static const Table t;
        return t;
    }

    inline float tap (const float* x, juce::int64 i, juce::int64 n) noexcept
    {
        return (i >= 0 && i < n) ? x[i] : 0.0f;
    }
}

void initialise()
{
    juce::ignoreUnused (table());
}

void addResampled (const juce::AudioBuffer<float>& source, double position, double step, float gain,
                   float* left, float* right, int numSamples) noexcept
{
    const auto& coefficients = table().data;
    const auto* inL = source.getReadPointer (0);
    const auto* inR = source.getReadPointer (source.getNumChannels() > 1 ? 1 : 0);
    const auto n = (juce::int64) source.getNumSamples();

    for (int s = 0; s < numSamples; ++s, position += step)
    {
        const auto base = (juce::int64) std::floor (position);
        const auto frac = position - (double) base;
        const auto phasePos = frac * phases;
        const auto phase = juce::jmin (phases - 1, (int) phasePos);
        const auto blend = (float) (phasePos - phase);

        const auto* h0 = coefficients.data() + (size_t) phase * taps;
        const auto* h1 = h0 + taps;
        const auto first = base - halfWidth + 1;

        float l = 0.0f, r = 0.0f;

        if (first >= 0 && first + taps <= n)
        {
            // Fast path: the whole filter is inside the file.
            const auto* a = inL + first;
            const auto* b = inR + first;
            for (int k = 0; k < taps; ++k)
            {
                const auto h = h0[k] + blend * (h1[k] - h0[k]);
                l += a[k] * h;
                r += b[k] * h;
            }
        }
        else
        {
            if (first >= n || first + taps <= 0)
                continue;   // entirely outside the file: silence

            for (int k = 0; k < taps; ++k)
            {
                const auto h = h0[k] + blend * (h1[k] - h0[k]);
                l += tap (inL, first + k, n) * h;
                r += tap (inR, first + k, n) * h;
            }
        }

        left[s] += l * gain;
        right[s] += r * gain;
    }
}

} // namespace riff::resampler
