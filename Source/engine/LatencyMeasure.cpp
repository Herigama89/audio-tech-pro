#include "LatencyMeasure.h"

namespace riff::latency
{

namespace
{
    constexpr int numBursts = 4;
    constexpr double firstBurstAt = 0.25;   // seconds
    constexpr double burstSpacing = 0.4;    // seconds
    constexpr double maxDelay = 0.2;        // seconds: anything longer isn't a sane round trip
    constexpr double burstFrequency = 2000.0;
    constexpr int burstCycles = 6;
}

std::vector<float> makeBurst (double sampleRate)
{
    const auto length = juce::jmax (16, (int) std::round (burstCycles * sampleRate / burstFrequency));
    std::vector<float> burst ((size_t) length);

    for (int i = 0; i < length; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
        burst[(size_t) i] = (float) (0.5 * window * std::sin (juce::MathConstants<double>::twoPi * burstFrequency * i / sampleRate));
    }

    return burst;
}

std::vector<int> burstPositions (double sampleRate)
{
    std::vector<int> positions;
    for (int k = 0; k < numBursts; ++k)
        positions.push_back ((int) std::round ((firstBurstAt + k * burstSpacing) * sampleRate));
    return positions;
}

int measurementLength (double sampleRate)
{
    return (int) std::round ((firstBurstAt + numBursts * burstSpacing + maxDelay) * sampleRate);
}

Result estimateDelay (const float* recorded, int numSamples, double sampleRate)
{
    const auto burst = makeBurst (sampleRate);
    const auto burstLength = (int) burst.size();
    const auto maxLag = (int) (maxDelay * sampleRate);

    double burstEnergy = 0.0;
    for (auto b : burst)
        burstEnergy += (double) b * b;
    const auto burstNorm = std::sqrt (burstEnergy);

    std::vector<int> delays;
    std::vector<float> scores;

    for (const auto position : burstPositions (sampleRate))
    {
        int bestLag = -1;
        double best = 0.0;

        for (int lag = 0; lag <= maxLag; ++lag)
        {
            const auto start = position + lag;
            if (start + burstLength > numSamples)
                break;

            double dot = 0.0, energy = 0.0;
            for (int i = 0; i < burstLength; ++i)
            {
                const auto x = (double) recorded[start + i];
                dot += x * burst[(size_t) i];
                energy += x * x;
            }

            // Normalised so loudness doesn't matter; abs() so an inverted signal still matches.
            const auto score = std::abs (dot) / (burstNorm * std::sqrt (energy) + 1e-12);

            if (score > best)
            {
                best = score;
                bestLag = lag;
            }
        }

        if (bestLag >= 0)
        {
            delays.push_back (bestLag);
            scores.push_back ((float) best);
        }
    }

    Result result;

    if (delays.size() < 3)
    {
        result.error = "The measurement was too short.";
        return result;
    }

    // The bursts must agree with each other, and match the burst's shape well.
    auto sorted = delays;
    std::sort (sorted.begin(), sorted.end());
    const auto median = sorted[sorted.size() / 2];

    int agreeing = 0;
    float weakest = 1.0f;

    for (size_t i = 0; i < delays.size(); ++i)
    {
        if (std::abs (delays[i] - median) <= 2)
        {
            ++agreeing;
            weakest = juce::jmin (weakest, scores[i]);
        }
    }

    if (agreeing < 3 || weakest < 0.5f)
    {
        result.error = "Riff couldn't hear the test signal clearly. Check the cable from the output to the input "
                       "(or turn the speaker and mic up), make sure the input level isn't clipping, and try again.";
        result.confidence = weakest;
        return result;
    }

    result.ok = true;
    result.delaySamples = median;
    result.confidence = weakest;
    return result;
}

} // namespace riff::latency
