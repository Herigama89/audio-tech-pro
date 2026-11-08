#include "DrumSplitter.h"
#include <complex>
#include <algorithm>

namespace riff::drumsplit
{
namespace
{
    constexpr int order = 11, nfft = 1 << order, hop = nfft / 4, bins = nfft / 2 + 1;
    constexpr int bands = 160;   // the NMF runs on log-spaced bands (quicker, and the templates are smooth anyway)

    enum Part { kick, snare, toms, hihat, cymbals, numParts };
    enum Class { cKick, cSnare, cToms, cMetal, numClasses };

    struct Stft
    {
        int frames = 0;
        std::vector<std::vector<std::complex<float>>> channels;   // per channel: frames x bins
    };

    std::vector<float> hann()
    {
        std::vector<float> w ((size_t) nfft);
        for (int i = 0; i < nfft; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) nfft);
        return w;
    }

    Stft forward (const juce::AudioBuffer<float>& x)
    {
        Stft s;
        const int n = x.getNumSamples();
        s.frames = n / hop + 2;
        juce::dsp::FFT fft (order);
        const auto window = hann();
        std::vector<float> buffer ((size_t) nfft * 2);
        for (int c = 0; c < juce::jmin (2, x.getNumChannels()); ++c)
        {
            auto& out = s.channels.emplace_back ((size_t) s.frames * bins);
            const auto* in = x.getReadPointer (c);
            for (int f = 0; f < s.frames; ++f)
            {
                std::fill (buffer.begin(), buffer.end(), 0.0f);
                const int start = f * hop - nfft / 2;
                for (int i = 0; i < nfft; ++i)
                    if (const auto at = start + i; at >= 0 && at < n)
                        buffer[(size_t) i] = in[at] * window[(size_t) i];
                fft.performRealOnlyForwardTransform (buffer.data(), true);
                for (int b = 0; b < bins; ++b)
                    out[(size_t) f * bins + (size_t) b] = { buffer[(size_t) (2 * b)], buffer[(size_t) (2 * b + 1)] };
            }
        }
        return s;
    }

    std::vector<float> inverse (const std::vector<std::complex<float>>& z, int frames, int length)
    {
        juce::dsp::FFT fft (order);
        const auto window = hann();
        std::vector<float> out ((size_t) length, 0.0f), norm ((size_t) length, 0.0f), buffer ((size_t) nfft * 2);
        for (int f = 0; f < frames; ++f)
        {
            for (int b = 0; b < bins; ++b)
            {
                buffer[(size_t) (2 * b)] = z[(size_t) f * bins + (size_t) b].real();
                buffer[(size_t) (2 * b + 1)] = z[(size_t) f * bins + (size_t) b].imag();
            }
            for (int i = 2 * bins; i < 2 * nfft; ++i)
                buffer[(size_t) i] = 0.0f;
            fft.performRealOnlyInverseTransform (buffer.data());
            const int start = f * hop - nfft / 2;
            for (int i = 0; i < nfft; ++i)
                if (const auto at = start + i; at >= 0 && at < length)
                {
                    out[(size_t) at] += buffer[(size_t) i] * window[(size_t) i];
                    norm[(size_t) at] += window[(size_t) i] * window[(size_t) i];
                }
        }
        for (int i = 0; i < length; ++i)
            out[(size_t) i] /= juce::jmax (1.0e-3f, norm[(size_t) i]);
        return out;
    }

    float gauss (float hz, float centre, float octaves)
    {
        const auto d = std::log2 (juce::jmax (1.0f, hz) / centre) / octaves;
        return std::exp (-0.5f * d * d);
    }
}

juce::StringArray partNames() { return { "Kick", "Snare", "Toms", "Hi-Hat", "Cymbals" }; }

std::vector<juce::AudioBuffer<float>> split (const juce::AudioBuffer<float>& drums, double rate, const std::function<bool (float)>& progress)
{
    const int length = drums.getNumSamples();
    if (length < nfft || drums.getNumChannels() == 0)
        return {};
    juce::AudioBuffer<float> stereo (2, length);
    stereo.copyFrom (0, 0, drums, 0, 0, length);
    stereo.copyFrom (1, 0, drums, juce::jmin (1, drums.getNumChannels() - 1), 0, length);
    const auto keepGoing = [&] (float p) { return ! progress || progress (p); };

    const auto spec = forward (stereo);
    const int frames = spec.frames;
    if (! keepGoing (0.1f))
        return {};

    // Bands: log-spaced from 25 Hz to the top, each a range of bins
    std::vector<int> bandOf ((size_t) bins);
    std::vector<float> bandHz ((size_t) bands);
    const auto binHz = (float) rate / (float) nfft;
    const auto lo = std::log (25.0f), hi = std::log ((float) rate * 0.5f);
    for (int k = 0; k < bands; ++k)
        bandHz[(size_t) k] = std::exp (lo + (hi - lo) * ((float) k + 0.5f) / (float) bands);
    for (int b = 0; b < bins; ++b)
    {
        const auto hz = juce::jmax (25.0f, (float) b * binHz);
        bandOf[(size_t) b] = juce::jlimit (0, bands - 1, (int) ((std::log (hz) - lo) / (hi - lo) * (float) bands));
    }

    // The magnitude spectrogram in bands (both channels): V, bands x frames
    std::vector<float> v ((size_t) bands * frames, 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int b = 0; b < bins; ++b)
        {
            float m = 0.0f;
            for (const auto& ch : spec.channels)
                m += std::abs (ch[(size_t) f * bins + (size_t) b]);
            v[(size_t) bandOf[(size_t) b] * frames + (size_t) f] += m;
        }
    float total = 0.0f;
    for (const auto x : v) total += x;
    if (total <= 1.0e-6f)
        return {};

    // The starting sounds: two per class (so each can learn a little of the actual kit)
    struct Comp { Class cls; std::function<float (float)> shape; };
    const std::vector<Comp> comps {
        { cKick,  [] (float f) { return gauss (f, 58.0f, 0.55f) + 0.25f * gauss (f, 110.0f, 0.5f) + 0.06f * gauss (f, 3000.0f, 1.0f); } },
        { cKick,  [] (float f) { return gauss (f, 75.0f, 0.8f) + 0.12f * gauss (f, 4000.0f, 1.2f); } },
        { cSnare, [] (float f) { return gauss (f, 200.0f, 0.45f) + 0.55f * gauss (f, 2500.0f, 1.4f) + 0.3f * gauss (f, 6000.0f, 1.0f); } },
        { cSnare, [] (float f) { return 0.6f * gauss (f, 240.0f, 0.4f) + gauss (f, 4000.0f, 1.3f); } },
        { cToms,  [] (float f) { return gauss (f, 100.0f, 0.3f) + 0.35f * gauss (f, 200.0f, 0.3f) + 0.05f * gauss (f, 2000.0f, 1.5f); } },
        { cToms,  [] (float f) { return gauss (f, 160.0f, 0.3f) + 0.35f * gauss (f, 320.0f, 0.3f) + 0.05f * gauss (f, 2500.0f, 1.5f); } },
        { cMetal, [] (float f) { return gauss (f, 9000.0f, 0.7f) + 0.2f * gauss (f, 14000.0f, 0.5f); } },
        { cMetal, [] (float f) { return gauss (f, 5500.0f, 1.2f) + 0.15f * gauss (f, 900.0f, 0.8f); } },
    };
    const int k = (int) comps.size();
    std::vector<float> w0 ((size_t) bands * k), w, h ((size_t) k * frames);
    for (int c = 0; c < k; ++c)
    {
        float sum = 0.0f;
        for (int b = 0; b < bands; ++b)
            sum += w0[(size_t) b * k + (size_t) c] = comps[(size_t) c].shape (bandHz[(size_t) b]) + 1.0e-3f;
        for (int b = 0; b < bands; ++b)
            w0[(size_t) b * k + (size_t) c] /= sum;
    }
    w = w0;
    const auto mean = total / (float) (bands * frames);
    for (auto& x : h)
        x = mean * (float) bands / (float) k;

    // KL-NMF, multiplicative updates; the sounds stay near what they started as (the parts keep their identity)
    std::vector<float> ratio ((size_t) bands * frames), wh ((size_t) bands * frames);
    const auto product = [&]
    {
        for (int b = 0; b < bands; ++b)
            for (int f = 0; f < frames; ++f)
            {
                float s = 1.0e-9f;
                for (int c = 0; c < k; ++c)
                    s += w[(size_t) b * k + (size_t) c] * h[(size_t) c * frames + (size_t) f];
                wh[(size_t) b * frames + (size_t) f] = s;
            }
    };
    constexpr int iterations = 70;
    for (int it = 0; it < iterations; ++it)
    {
        product();
        for (size_t i = 0; i < v.size(); ++i)
            ratio[i] = v[i] / wh[i];
        // H
        std::vector<float> colSum ((size_t) k, 0.0f);
        for (int c = 0; c < k; ++c)
            for (int b = 0; b < bands; ++b)
                colSum[(size_t) c] += w[(size_t) b * k + (size_t) c];
        for (int c = 0; c < k; ++c)
            for (int f = 0; f < frames; ++f)
            {
                float num = 0.0f;
                for (int b = 0; b < bands; ++b)
                    num += w[(size_t) b * k + (size_t) c] * ratio[(size_t) b * frames + (size_t) f];
                h[(size_t) c * frames + (size_t) f] *= num / juce::jmax (1.0e-9f, colSum[(size_t) c]);
            }
        // W (after the activations have settled), pulled back towards its starting shape
        if (it >= 15)
        {
            product();
            for (size_t i = 0; i < v.size(); ++i)
                ratio[i] = v[i] / wh[i];
            std::vector<float> rowSum ((size_t) k, 0.0f);
            for (int c = 0; c < k; ++c)
                for (int f = 0; f < frames; ++f)
                    rowSum[(size_t) c] += h[(size_t) c * frames + (size_t) f];
            for (int c = 0; c < k; ++c)
            {
                float norm = 0.0f;
                for (int b = 0; b < bands; ++b)
                {
                    float num = 0.0f;
                    for (int f = 0; f < frames; ++f)
                        num += h[(size_t) c * frames + (size_t) f] * ratio[(size_t) b * frames + (size_t) f];
                    auto& x = w[(size_t) b * k + (size_t) c];
                    x *= num / juce::jmax (1.0e-9f, rowSum[(size_t) c]);
                    // (its own region of the spectrum: what it learns far from where its part lives counts for little)
                    const auto start = w0[(size_t) b * k + (size_t) c];
                    x = 0.65f * x * juce::jlimit (0.05f, 1.0f, start * (float) bands * 0.6f) + 0.35f * start;
                    norm += x;
                }
                for (int b = 0; b < bands; ++b)
                    w[(size_t) b * k + (size_t) c] /= juce::jmax (1.0e-9f, norm);
                for (int f = 0; f < frames; ++f)
                    h[(size_t) c * frames + (size_t) f] *= norm;
            }
        }
        if (! keepGoing (0.1f + 0.6f * (float) (it + 1) / iterations))
            return {};
    }

    // Each class's share of each band and frame (soft masks, squared: cleaner parts, still adding up to the whole)
    std::vector<float> classMag ((size_t) numClasses * bands * frames, 0.0f);
    for (int b = 0; b < bands; ++b)
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < k; ++c)
                classMag[((size_t) comps[(size_t) c].cls * bands + (size_t) b) * frames + (size_t) f]
                    += w[(size_t) b * k + (size_t) c] * h[(size_t) c * frames + (size_t) f];

    // The metal: short (hi-hat) against sustained (cymbals), by a running median along time (~250 ms)
    std::vector<float> sustained ((size_t) bands * frames);
    {
        const int half = juce::jmax (2, (int) std::lround (0.125 * rate / hop));
        std::vector<float> window;
        for (int b = 0; b < bands; ++b)
            for (int f = 0; f < frames; ++f)
            {
                window.clear();
                for (int j = juce::jmax (0, f - half); j <= juce::jmin (frames - 1, f + half); ++j)
                    window.push_back (classMag[((size_t) cMetal * bands + (size_t) b) * frames + (size_t) j]);
                std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
                sustained[(size_t) b * frames + (size_t) f] = window[window.size() / 2];
            }
    }
    if (! keepGoing (0.8f))
        return {};

    std::vector<std::vector<std::complex<float>>> parts ((size_t) numParts * spec.channels.size());
    for (auto& p : parts)
        p.assign ((size_t) frames * bins, {});
    for (int f = 0; f < frames; ++f)
        for (int b = 0; b < bins; ++b)
        {
            const auto band = (size_t) bandOf[(size_t) b];
            float m[numParts];
            const auto at = [&] (Class c) { return classMag[((size_t) c * bands + band) * frames + (size_t) f]; };
            const auto metal = at (cMetal);
            const auto sus = juce::jmin (metal, sustained[band * frames + (size_t) f]);
            const auto hz = (float) b * binHz;
            // (cymbals ring lower too; the hi-hat lives up high)
            const auto cymbalShare = juce::jlimit (0.0f, 1.0f, sus / juce::jmax (1.0e-9f, metal) * (hz < 5000.0f ? 1.3f : 1.0f));
            m[kick] = at (cKick); m[snare] = at (cSnare); m[toms] = at (cToms);
            m[hihat] = metal * (1.0f - cymbalShare); m[cymbals] = metal * cymbalShare;
            float sum = 1.0e-12f;
            for (auto& x : m) { x *= x; sum += x; }
            for (int p = 0; p < numParts; ++p)
                for (size_t c = 0; c < spec.channels.size(); ++c)
                    parts[(size_t) p * spec.channels.size() + c][(size_t) f * bins + (size_t) b]
                        = spec.channels[c][(size_t) f * bins + (size_t) b] * (m[p] / sum);
        }

    std::vector<juce::AudioBuffer<float>> out;
    for (int p = 0; p < numParts; ++p)
    {
        juce::AudioBuffer<float> part (2, length);
        for (size_t c = 0; c < spec.channels.size(); ++c)
        {
            const auto samples = inverse (parts[(size_t) p * spec.channels.size() + c], frames, length);
            part.copyFrom ((int) c, 0, samples.data(), length);
        }
        out.push_back (std::move (part));
        if (! keepGoing (0.8f + 0.2f * (float) (p + 1) / numParts))
            return {};
    }
    return out;
}
}
