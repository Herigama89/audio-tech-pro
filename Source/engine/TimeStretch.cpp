#include "TimeStretch.h"
#include <thread>
#include <atomic>

namespace riff::stretch
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float wrap (float x) noexcept { return x - twoPi * std::round (x / twoPi); }

    /** One phase vocoder (its FFT and working buffers), reused for every piece. */
    struct Vocoder
    {
        explicit Vocoder (double sampleRate)
        {
            // ~85 ms frames (4096 at 44.1 and 48 kHz), overlapping four times
            order = juce::jlimit (9, 15, (int) std::ceil (std::log2 (sampleRate * 0.085)));
            size = 1 << order;
            hop = size / 4;
            bins = size / 2 + 1;
            fft = std::make_unique<juce::dsp::FFT> (order);
            window.resize ((size_t) size);
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) size);
            work.assign ((size_t) size * 2, 0.0f);
            for (auto* v : { &magnitude, &phase, &lastPhase, &outPhase, &lastOutPhase })
                v->assign ((size_t) bins, 0.0f);
            peakOf.assign ((size_t) bins, 0);

            // The inverse transform's scale (it differs between FFT engines): measured once.
            std::fill (work.begin(), work.end(), 0.0f);
            work[0] = 1.0f;
            fft->performRealOnlyForwardTransform (work.data(), true);
            fft->performRealOnlyInverseTransform (work.data());
            inverseScale = std::abs (work[0]) > 1.0e-9f ? 1.0f / work[0] : 1.0f;
        }

        /** Stretches x[from, to) to `outLength` samples (the audio beyond the ends mirrored, so nothing outside
            leaks in). The start comes out exactly as it was, phases taken from there. */
        void run (const float* x, int from, int to, int outLength, float* out)
        {
            const auto length = to - from;
            if (length <= 0 || outLength <= 0)
                return;
            const auto ratio = (double) outLength / (double) length;
            auto read = [x, from, length] (juce::int64 i)
            {
                const auto period = 2 * (juce::int64) length;
                auto j = ((i - from) % period + period) % period;
                if (j >= length)
                    j = period - 1 - j;
                return x[from + j];
            };

            const auto pad = size;
            std::vector<float> acc ((size_t) (outLength + 2 * pad), 0.0f), norm ((size_t) (outLength + 2 * pad), 0.0f);
            const int firstFrame = -(size / hop), lastFrame = outLength / hop + size / hop;
            juce::int64 lastAt = 0;
            for (int k = firstFrame; k <= lastFrame; ++k)
            {
                const auto centre = (juce::int64) k * hop;
                const auto at = (juce::int64) std::llround ((double) from + (double) centre / ratio);

                for (int i = 0; i < size; ++i)
                    work[(size_t) i] = read (at - size / 2 + i) * window[(size_t) i];
                std::fill (work.begin() + size, work.end(), 0.0f);
                fft->performRealOnlyForwardTransform (work.data(), true);
                for (int b = 0; b < bins; ++b)
                {
                    const auto re = work[(size_t) (2 * b)], im = work[(size_t) (2 * b + 1)];
                    magnitude[(size_t) b] = std::sqrt (re * re + im * im);
                    phase[(size_t) b] = std::atan2 (im, re);
                }

                if (k <= 0)
                    outPhase = phase;   // (the start: as it was)
                else
                    advance ((int) (at - lastAt));

                for (int b = 0; b < bins; ++b)
                {
                    work[(size_t) (2 * b)] = magnitude[(size_t) b] * std::cos (outPhase[(size_t) b]);
                    work[(size_t) (2 * b + 1)] = magnitude[(size_t) b] * std::sin (outPhase[(size_t) b]);
                }
                std::fill (work.begin() + 2 * bins, work.end(), 0.0f);
                fft->performRealOnlyInverseTransform (work.data());

                const auto base = centre - size / 2 + pad;
                for (int i = 0; i < size; ++i)
                {
                    const auto o = base + i;
                    if (o < 0 || o >= (juce::int64) acc.size())
                        continue;
                    acc[(size_t) o] += work[(size_t) i] * inverseScale * window[(size_t) i];
                    norm[(size_t) o] += window[(size_t) i] * window[(size_t) i];
                }
                lastPhase = phase;
                lastOutPhase = outPhase;
                lastAt = at;
            }
            for (int t = 0; t < outLength; ++t)
                out[t] = acc[(size_t) (t + pad)] / std::max (norm[(size_t) (t + pad)], 0.1f);
        }

    private:
        /** The new phases: each peak's moves at its own true frequency; the bins round it keep their offset from it. */
        void advance (int analysisHop)
        {
            // Peaks: louder than two bins either side
            int peaks = 0;
            int last = -1;
            for (int b = 0; b < bins; ++b)
            {
                const auto m = magnitude[(size_t) b];
                bool peak = m > 1.0e-9f;
                for (int d = -2; d <= 2 && peak; ++d)
                    if (d != 0 && b + d >= 0 && b + d < bins && magnitude[(size_t) (b + d)] > m)
                        peak = false;
                if (! peak)
                    continue;
                ++peaks;
                const auto omega = twoPi * (float) b / (float) size;
                const auto expected = omega * (float) analysisHop;
                const auto deviation = wrap (phase[(size_t) b] - lastPhase[(size_t) b] - expected);
                const auto frequency = analysisHop > 0 ? omega + deviation / (float) analysisHop : omega;
                outPhase[(size_t) b] = lastOutPhase[(size_t) b] + frequency * (float) hop;
                // the bins between the last peak and this one belong to the nearer
                for (int j = last + 1; j < b; ++j)
                    peakOf[(size_t) j] = (last >= 0 && j - last < b - j) ? last : b;
                peakOf[(size_t) b] = b;
                last = b;
            }
            if (peaks == 0)
            {
                outPhase = phase;
                return;
            }
            for (int j = last + 1; j < bins; ++j)
                peakOf[(size_t) j] = last;
            for (int b = 0; b < bins; ++b)
            {
                const auto p = peakOf[(size_t) b];
                if (p != b)
                    outPhase[(size_t) b] = outPhase[(size_t) p] + (phase[(size_t) b] - phase[(size_t) p]);
            }
        }

        int order = 12, size = 4096, hop = 1024, bins = 2049;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, work, magnitude, phase, lastPhase, outPhase, lastOutPhase;
        std::vector<int> peakOf;
        float inverseScale = 1.0f;
    };

    /** How far ahead of an attack's onset its piece is cut (so none of the onset is stretched). */
    inline int attackLead (double sampleRate) noexcept { return juce::jmax (16, juce::roundToInt (sampleRate * 0.0013)); }

    /** A short raised-cosine blend from `a` into `b` over n samples. */
    inline float blend (float a, float b, int i, int n) noexcept
    {
        const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * ((float) i + 0.5f) / (float) n);
        return a + (b - a) * w;
    }
}

std::vector<int> findAttacks (const juce::AudioBuffer<float>& input, double sampleRate)
{
    const auto length = input.getNumSamples();
    const auto channels = input.getNumChannels();
    std::vector<int> attacks;
    if (length < 4096 || channels == 0)
        return attacks;

    std::vector<float> mono ((size_t) length, 0.0f);
    for (int c = 0; c < channels; ++c)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), input.getReadPointer (c), 1.0f / (float) channels, length);

    // Spectral flux (rises in level, more weight up high where attacks are), every ~5 ms
    constexpr int order = 10, size = 1 << order, bins = size / 2 + 1;
    const int hop = juce::jmax (64, juce::roundToInt (sampleRate * 0.0053));
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) size), work ((size_t) size * 2), last ((size_t) bins, 0.0f), flux, level;
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) size);
    for (int start = 0; start + size <= length; start += hop)
    {
        float energy = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            const auto v = mono[(size_t) (start + i)];
            work[(size_t) i] = v * window[(size_t) i];
            energy += v * v;
        }
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        float f = 0.0f;
        for (int b = 1; b < bins; ++b)
        {
            const auto m = std::log1p (100.0f * work[(size_t) b]);
            f += std::max (0.0f, m - last[(size_t) b]) * (1.0f + (float) b / (float) bins);
            last[(size_t) b] = m;
        }
        flux.push_back (f);
        level.push_back (std::sqrt (energy / (float) size));
    }

    const auto frames = (int) flux.size();
    const auto peakLevel = level.empty() ? 0.0f : *std::max_element (level.begin(), level.end());
    const auto biggest = flux.empty() ? 0.0f : *std::max_element (flux.begin(), flux.end());
    const int around = juce::jmax (4, juce::roundToInt (0.05 * sampleRate / hop));
    const int gap = juce::roundToInt (0.05 * sampleRate);   // (attacks at least 50 ms apart)
    for (int k = 1; k < frames; ++k)
    {
        float mean = 0.0f;
        int n = 0;
        bool isMax = true;
        for (int j = std::max (0, k - 2 * around); j <= std::min (frames - 1, k + around); ++j)
        {
            mean += flux[(size_t) j];
            ++n;
            if (std::abs (j - k) <= around / 2 && j != k && flux[(size_t) j] > flux[(size_t) k])
                isMax = false;
        }
        mean /= (float) std::max (1, n);
        if (! isMax || flux[(size_t) k] < mean * 1.5f + 0.06f * biggest || level[(size_t) k] < peakLevel * 0.01f)
            continue;
        // ... and the level jumps (a steady chord's beating changes the spectrum, but not the level, much)
        float before = 1.0e9f, after = 0.0f;
        for (int j = std::max (0, k - around); j < k; ++j)
            before = std::min (before, level[(size_t) j]);
        for (int j = k; j <= std::min (frames - 1, k + 2); ++j)
            after = std::max (after, level[(size_t) j]);
        if (k > 0 && after < before * 1.3f)
            continue;

        // The attack's exact start: where the level begins to climb before its peak (in ~1.3 ms steps)
        const int from = std::max (0, (k - 1) * hop), to = std::min (length, k * hop + size);
        const int step = juce::jmax (16, juce::roundToInt (sampleRate * 0.0013));
        std::vector<float> env;
        for (int i = from; i + step <= to; i += step)
        {
            float m = 0.0f;
            for (int j = 0; j < step; ++j)
                m = std::max (m, std::abs (mono[(size_t) (i + j)]));
            env.push_back (m);
        }
        if (env.empty())
            continue;
        const auto top = (int) (std::max_element (env.begin(), env.end()) - env.begin());
        int rise = top;
        while (rise > 0 && env[(size_t) (rise - 1)] < env[(size_t) rise] && env[(size_t) (rise - 1)] > env[(size_t) top] * 0.12f)
            --rise;
        const auto at = std::max (0, from + rise * step - attackLead (sampleRate));
        if (attacks.empty() || at - attacks.back() >= gap)
            attacks.push_back (at);
    }
    return attacks;
}

juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double ratio, double sampleRate, const Options& options)
{
    ratio = juce::jlimit (0.1, 10.0, ratio);
    const auto channels = input.getNumChannels();
    const auto inLength = input.getNumSamples();
    const auto outLength = juce::jmax (1, (int) std::llround ((double) inLength * ratio));
    if (std::abs (ratio - 1.0) < 1.0e-6 || channels == 0)
        return input;

    juce::AudioBuffer<float> output (channels, outLength);
    output.clear();
    if (inLength < 256)
    {
        // Too short to stretch: resampled instead.
        for (int c = 0; c < channels; ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.process (1.0 / ratio, input.getReadPointer (c), output.getWritePointer (c), outLength, inLength, 0);
        }
        return output;
    }

    // Stereo as mid and side (so both sides move together)
    const bool stereo = channels == 2;
    juce::AudioBuffer<float> source (channels, inLength);
    if (stereo)
    {
        for (int i = 0; i < inLength; ++i)
        {
            const auto l = input.getSample (0, i), r = input.getSample (1, i);
            source.setSample (0, i, 0.5f * (l + r));
            source.setSample (1, i, 0.5f * (l - r));
        }
    }
    else
        source.makeCopyOf (input);

    // The pieces: from each attack to the next
    std::vector<int> cuts { 0 };
    bool startsWithAttack = false;   // (a loop that starts on a hit)
    if (options.keepAttacks)
        for (const auto a : findAttacks (input, sampleRate))
        {
            if (a < juce::roundToInt (0.01 * sampleRate) && cuts.size() == 1)
                startsWithAttack = true;
            else if (a > cuts.back() + 64 && a < inLength - 64)
                cuts.push_back (a);
        }
    const auto attackAt = [&] (size_t j) { return j > 0 || startsWithAttack; };
    cuts.push_back (inLength);

    // Where each piece starts in the output: an attack's onset (just after the cut, which is a little ahead of it)
    // lands exactly where it should, the cut the same little way ahead of that.
    const auto lead = attackLead (sampleRate);
    const auto placeOf = [&] (size_t j)
    {
        if (j == 0)
            return 0;
        if (j + 1 == cuts.size())
            return outLength;
        return juce::jlimit (0, outLength, (int) std::llround ((double) (cuts[j] + lead) * ratio) - lead);
    };

    const int keep = juce::jmax (0, juce::roundToInt (options.attackMs * 0.001 * sampleRate));
    const int seam = juce::jmax (16, juce::roundToInt (0.0007 * sampleRate));   // (~0.7 ms blends where pieces meet)

    // One piece (each writes only its own stretch of the output, so pieces can be done side by side)
    const auto piece = [&] (size_t j, Vocoder& vocoder)
    {
        const auto inStart = cuts[j], inEnd = cuts[j + 1];
        const auto outStart = placeOf (j), outEnd = j + 2 == cuts.size() ? outLength : placeOf (j + 1);
        const auto inLen = inEnd - inStart, outLen = outEnd - outStart;
        if (inLen <= 0 || outLen <= 0)
            return;

        // The attack, kept as it is (not for the stretch before the first one)
        const auto attack = attackAt (j) && options.keepAttacks ? std::min ({ keep, inLen / 2, outLen / 2 }) : 0;
        for (int c = 0; c < channels; ++c)
        {
            const auto* in = input.getReadPointer (c);
            auto* out = output.getWritePointer (c);
            for (int i = 0; i < attack; ++i)
                out[outStart + i] = in[inStart + i];
        }

        // The rest, stretched to fill until the next attack
        const auto bodyIn = inLen - attack, bodyOut = outLen - attack;
        if (bodyIn <= 0 || bodyOut <= 0)
            return;
        std::vector<std::vector<float>> parts ((size_t) channels);
        for (int c = 0; c < channels; ++c)
        {
            parts[(size_t) c].assign ((size_t) bodyOut, 0.0f);
            vocoder.run (source.getReadPointer (c), inStart + attack, inEnd, bodyOut, parts[(size_t) c].data());
        }
        if (stereo)
            for (int i = 0; i < bodyOut; ++i)
            {
                const auto m = parts[0][(size_t) i], s = parts[1][(size_t) i];
                parts[0][(size_t) i] = m + s;
                parts[1][(size_t) i] = m - s;
            }

        for (int c = 0; c < channels; ++c)
        {
            const auto* in = input.getReadPointer (c);
            auto* out = output.getWritePointer (c);
            const auto& p = parts[(size_t) c];
            const auto o = outStart + attack;
            for (int i = 0; i < bodyOut; ++i)
                out[o + i] = p[(size_t) i];

            // Seams: out of the kept attack into the stretched part (which starts as the audio did) ...
            if (attack > 0)
                for (int i = 0; i < std::min (seam, bodyOut) && inStart + attack + i < inLength; ++i)
                    out[o + i] = blend (in[inStart + attack + i], p[(size_t) i], i, std::min (seam, bodyOut));
            // ... and, before the next attack, into the audio just before it as it was
            if (j + 2 < cuts.size())
            {
                const auto n = std::min (seam, bodyOut);
                for (int i = 0; i < n; ++i)
                {
                    const auto from = inEnd - n + i;
                    if (from >= 0)
                        out[outEnd - n + i] = blend (p[(size_t) (bodyOut - n + i)], in[from], i, n);
                }
            }
        }
    };

    const auto count = cuts.size() - 1;
   #if JUCE_WASM
    const size_t workers = 1;
   #else
    const auto workers = (size_t) juce::jlimit (1, 8, (int) std::thread::hardware_concurrency() - 1);
   #endif
    if (workers <= 1 || count < 4)
    {
        Vocoder vocoder (sampleRate);
        for (size_t j = 0; j < count; ++j)
            piece (j, vocoder);
    }
    else
    {
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> threads;
        for (size_t w = 0; w < std::min (workers, count); ++w)
            threads.emplace_back ([&]
            {
                Vocoder vocoder (sampleRate);
                for (auto j = next.fetch_add (1); j < count; j = next.fetch_add (1))
                    piece (j, vocoder);
            });
        for (auto& t : threads)
            t.join();
    }
    return output;
}

} // namespace riff::stretch
