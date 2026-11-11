#include "DrumSplitter.h"
#include <complex>
#include <algorithm>
#include <array>
#include <iostream>

namespace riff::drumsplit
{
namespace
{
    constexpr int order = 11, nfft = 1 << order, hop = nfft / 4, bins = nfft / 2 + 1;

    enum Part { kick, snare, toms, hihat, cymbals, numParts };

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

namespace
{
    constexpr int bands = 128;       // log-spaced, 30 Hz up
    constexpr int span = 8;          // frames in a sound's picture (~85 ms): its attack and how it dies away
    constexpr int perPart = 2;       // pictures per part: one learnt from the kit's own hits, one general

    /** A general picture of a part: where in the spectrum it sounds, and how fast it dies away. */
    float shapeOf (int part, float hz, int frame)
    {
        const auto t = (float) frame;
        switch (part)
        {
            case kick:    return (gauss (hz, 60.0f, 0.5f) + 0.3f * gauss (hz, 110.0f, 0.5f)) * std::exp (-t * 0.35f)
                                 + 0.08f * gauss (hz, 3500.0f, 1.0f) * (frame == 0 ? 1.0f : 0.1f);   // (the beater's click)
            case snare:   return (gauss (hz, 200.0f, 0.4f) * std::exp (-t * 0.5f) + 0.7f * gauss (hz, 3500.0f, 1.5f) * std::exp (-t * 0.45f));
            case toms:    return (gauss (hz, 120.0f, 0.5f) + 0.3f * gauss (hz, 240.0f, 0.4f)) * std::exp (-t * 0.18f)
                                 + 0.05f * gauss (hz, 2000.0f, 1.5f) * (frame == 0 ? 1.0f : 0.1f);
            case hihat:   return (gauss (hz, 9000.0f, 0.7f) + 0.25f * gauss (hz, 14000.0f, 0.5f)) * std::exp (-t * 0.8f);
            default:      return (gauss (hz, 7000.0f, 1.1f) + 0.2f * gauss (hz, 1500.0f, 0.8f)) * std::exp (-t * 0.04f);
        }
    }
}

std::vector<juce::AudioBuffer<float>> split (const juce::AudioBuffer<float>& drums, double rate, const std::function<bool (float)>& progress)
{
    /*  The hits first, then the sounds. Each hit is found where the spectrum suddenly rises (an onset), and told
        apart by what rose and how it dies away: a kick's low thump, a snare's body with its wires' noise, a tom's
        pitched ring without wires, a hi-hat's short tick up high, a cymbal's long wash. Each part's sound is then
        learnt from the kit's own clean hits (its picture over ~85 ms, as it attacks and decays) and the recording is
        explained by those sounds struck where the hits are (convolutive NMF, guided by the hits) - so a part takes
        its whole sound, all its frequencies, at its own hits, not just a frequency range. */
    const int length = drums.getNumSamples();
    if (length < nfft || drums.getNumChannels() == 0)
        return {};
    juce::AudioBuffer<float> stereo (2, length);
    stereo.copyFrom (0, 0, drums, 0, 0, length);
    stereo.copyFrom (1, 0, drums, juce::jmin (1, drums.getNumChannels() - 1), 0, length);
    const auto keepGoing = [&] (float p) { return ! progress || progress (p); };

    const auto spec = forward (stereo);
    const int frames = spec.frames;
    if (! keepGoing (0.05f))
        return {};

    // Bands
    std::vector<int> bandOf ((size_t) bins);
    std::vector<float> bandHz ((size_t) bands);
    const auto binHz = (float) rate / (float) nfft;
    const auto lo = std::log (30.0f), hi = std::log ((float) rate * 0.5f);
    for (int k = 0; k < bands; ++k)
        bandHz[(size_t) k] = std::exp (lo + (hi - lo) * ((float) k + 0.5f) / (float) bands);
    for (int b = 0; b < bins; ++b)
    {
        const auto hz = juce::jmax (30.0f, (float) b * binHz);
        bandOf[(size_t) b] = juce::jlimit (0, bands - 1, (int) ((std::log (hz) - lo) / (hi - lo) * (float) bands));
    }
    const auto bandAt = [&] (float hz) { return juce::jlimit (0, bands - 1, (int) ((std::log (juce::jmax (30.0f, hz)) - lo) / (hi - lo) * (float) bands)); };

    // V: magnitudes in bands, bands x frames
    std::vector<float> v ((size_t) bands * frames, 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int b = 0; b < bins; ++b)
        {
            float m = 0.0f;
            for (const auto& ch : spec.channels)
                m += std::abs (ch[(size_t) f * bins + (size_t) b]);
            v[(size_t) bandOf[(size_t) b] * frames + (size_t) f] += m;
        }
    const auto vAt = [&] (int b, int f) { return v[(size_t) b * frames + (size_t) f]; };
    float vmax = 0.0f;
    for (const auto x : v) vmax = juce::jmax (vmax, x);
    if (vmax <= 1.0e-9f)
        return {};

    // ---- The hits ---------------------------------------------------------------------------------------------
    enum Group { gLow, gLowMid, gMid, gHigh, gAir, numGroups };
    const int groupEdges[numGroups + 1] { 0, bandAt (150.0f), bandAt (400.0f), bandAt (2000.0f), bandAt (7000.0f), bands };
    std::vector<float> flux ((size_t) numGroups * frames, 0.0f);
    {
        std::vector<float> logV (v.size());
        for (size_t i = 0; i < v.size(); ++i)
            logV[i] = std::log1p (1000.0f * v[i] / vmax);
        for (int g = 0; g < numGroups; ++g)
            for (int f = 2; f < frames; ++f)
            {
                float rise = 0.0f;
                for (int b = groupEdges[g]; b < groupEdges[g + 1]; ++b)
                {
                    const auto* row = logV.data() + (size_t) b * frames;
                    rise += juce::jmax (0.0f, row[f] - juce::jmax (row[f - 1], row[f - 2]));
                }
                flux[(size_t) g * frames + (size_t) f] = rise / (float) juce::jmax (1, groupEdges[g + 1] - groupEdges[g]);
            }
    }
    const auto fluxAt = [&] (int g, int f) { return f >= 0 && f < frames ? flux[(size_t) g * frames + (size_t) f] : 0.0f; };
    // (each group against its own loudest rises: a quiet hi-hat counts as much as a loud kick)
    float groupScale[numGroups];
    for (int g = 0; g < numGroups; ++g)
    {
        std::vector<float> sorted (flux.begin() + (long) g * frames, flux.begin() + (long) (g + 1) * frames);
        std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() * 98 / 100), sorted.end());
        groupScale[g] = juce::jmax (1.0e-6f, sorted[sorted.size() * 98 / 100]);
    }
    std::vector<float> novelty ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int g = 0; g < numGroups; ++g)
            novelty[(size_t) f] += fluxAt (g, f) / groupScale[g];

    std::vector<int> onsets;
    {
        const int near = juce::jmax (2, (int) std::lround (0.03 * rate / hop)), around = juce::jmax (4, (int) std::lround (0.4 * rate / hop));
        std::vector<double> prefix ((size_t) frames + 1, 0.0);
        for (int f = 0; f < frames; ++f)
            prefix[(size_t) f + 1] = prefix[(size_t) f] + novelty[(size_t) f];
        for (int f = 2; f < frames; ++f)
        {
            const auto x = novelty[(size_t) f];
            if (x < 0.35f)
                continue;
            bool peak = true;
            for (int j = juce::jmax (0, f - near); j <= juce::jmin (frames - 1, f + near) && peak; ++j)
                peak = j == f || novelty[(size_t) j] < x || (novelty[(size_t) j] == x && j > f);
            const int a = juce::jmax (0, f - around), b = juce::jmin (frames, f + around + 1);
            const auto localMean = (float) ((prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a));
            if (peak && x > localMean * 1.3f + 0.15f)
                onsets.push_back (f);
        }
    }

    // What each hit is (it can be more than one: a kick and a hi-hat together)
    struct Hit { int frame; std::array<bool, numParts> is {}; bool clean = false; std::array<float, 8> features {}; };
    std::vector<Hit> hits;
    const int ringFrames = juce::jmax (4, (int) std::lround (0.12 * rate / hop));
    for (const auto f0 : onsets)
    {
        Hit h { f0 };
        float jump[numGroups];
        for (int g = 0; g < numGroups; ++g)
            jump[g] = (fluxAt (g, f0) + fluxAt (g, f0 + 1)) / groupScale[g];
        // What rose, over the hit's first ~50 ms against just before it, by region of the spectrum
        const int window = juce::jmax (3, (int) std::lround (0.05 * rate / hop));
        const auto riseIn = [&] (float fromHz, float toHz, double* centre = nullptr)
        {
            double e = 0.0, c = 0.0;
            for (int b = bandAt (fromHz); b <= bandAt (toHz) && b < bands; ++b)
            {
                float now = 0.0f;
                for (int f = f0; f < f0 + window; ++f)
                    now += vAt (b, juce::jmin (frames - 1, f));
                const auto rise = juce::jmax (0.0f, now / (float) window - vAt (b, juce::jmax (0, f0 - 2)));
                e += rise;
                c += rise * std::log (bandHz[(size_t) b]);
            }
            if (centre != nullptr)
                *centre = e > 0.0 ? std::exp (c / e) : 0.0;
            return (float) e;
        };
        double lowCentreD = 0.0;
        riseIn (30.0f, 400.0f, &lowCentreD);
        const auto lowCentre = (float) lowCentreD;
        const auto sub = riseIn (30.0f, 100.0f), body = riseIn (100.0f, 400.0f), wiresE = riseIn (2000.0f, 7000.0f), airE = riseIn (7000.0f, 20000.0f);
        // how the highs die away: still ringing ~120 ms on (a cymbal), or gone (a hi-hat, a snare's wires)
        const auto highAt = [&] (int f)
        {
            float e = 0.0f;
            for (int b = bandAt (3000.0f); b < bands; ++b)
                e += vAt (b, juce::jlimit (0, frames - 1, f));
            return e;
        };
        const auto highPeak = juce::jmax (highAt (f0), highAt (f0 + 1), highAt (f0 + 2));
        const auto highBefore = highAt (f0 - 2);
        const auto ring = (highAt (f0 + ringFrames) - highBefore * 0.8f) / juce::jmax (1.0e-9f, highPeak - highBefore * 0.8f);
        // and the lows: a tom rings on, a kick is a shorter thump
        const auto lowAt = [&] (int f)
        {
            float e = 0.0f;
            for (int b = 0; b < groupEdges[gMid]; ++b)
                e += vAt (b, juce::jlimit (0, frames - 1, f));
            return e;
        };
        const auto lowRing = lowAt (f0 + ringFrames) / juce::jmax (1.0e-9f, juce::jmax (lowAt (f0), lowAt (f0 + 1), lowAt (f0 + 2)));

        // a kick's pitch falls fast (its centre in the lows drops within ~50 ms); a tom's stays
        const auto lowCentreOver = [&] (int from, int to)
        {
            double e = 0.0, c = 0.0;
            for (int b = 0; b <= bandAt (400.0f); ++b)
                for (int f = from; f <= to; ++f)
                {
                    const auto x = vAt (b, juce::jlimit (0, frames - 1, f));
                    e += x;
                    c += x * std::log (bandHz[(size_t) b]);
                }
            return e > 0.0 ? std::exp (c / e) : 1.0;
        };
        const auto drop = (float) (lowCentreOver (f0, f0 + 1) / lowCentreOver (f0 + window, f0 + 2 * window));
        h.features = { sub, body, wiresE, airE, lowCentre, ring, lowRing, drop };
        juce::ignoreUnused (jump);
        hits.push_back (h);
    }
    // Told apart, each region's rise against the loudest hits' (a quiet hi-hat counts as much as a loud kick)
    {
        float top[4] {};
        for (int q = 0; q < 4; ++q)
        {
            std::vector<float> all;
            for (const auto& hit : hits)
                all.push_back (hit.features[(size_t) q]);
            if (! all.empty())
            {
                std::nth_element (all.begin(), all.begin() + (long) (all.size() * 9 / 10), all.end());
                top[q] = juce::jmax (1.0e-9f, all[all.size() * 9 / 10]);
            }
        }
        for (auto& hit : hits)
        {
            const auto [sub, body, wiresE, airE, lowCentre, ring, lowRing, drop] = hit.features;
            const auto subN = sub / top[0], bodyN = body / top[1], wiresN = wiresE / top[2], airN = airE / top[3];
            auto& is = hit.is;
            // a kick: the lows rise, mostly below 100 Hz (its click up high doesn't make it a snare)
            is[kick] = subN > 0.25f && sub > 0.5f * body && (drop > 1.3f || lowCentre < 90.0f);
            // a snare: its body (100-400 Hz) and the wires' noise (2-7 kHz) together
            is[snare] = bodyN > 0.2f && wiresN > 0.3f && body > 0.5f * sub;
            // a tom: a pitched body that rings on, without wires
            is[toms] = ! is[kick] && ! is[snare] && (bodyN > 0.25f || subN > 0.25f) && drop < 1.3f && lowRing > 0.2f && wiresN < 0.5f;
            // the metal: up high, short (a hi-hat) or ringing on (a cymbal)
            const bool metal = airN > 0.15f || (wiresN > 0.3f && ! is[snare]);
            is[cymbals] = metal && ring > 0.5f;
            is[hihat] = metal && ! is[cymbals];
            if (std::none_of (is.begin(), is.end(), [] (bool x) { return x; }))
            {
                // (unclear: the biggest rise decides)
                const float r[] { subN, bodyN, wiresN, airN };
                const auto best = (int) (std::max_element (r, r + 4) - r);
                is[best == 0 ? kick : best == 1 ? (lowRing > 0.25f ? toms : snare) : best == 2 ? snare : ring > 0.5f ? cymbals : hihat] = true;
            }
            hit.clean = std::count (is.begin(), is.end(), true) == 1;
            if (std::getenv ("RIFF_DRUM_DEBUG") != nullptr && hit.frame * hop < rate * 5.0)
                std::cout << "hit " << juce::String (hit.frame * hop / rate, 3) << " k" << is[kick] << " s" << is[snare] << " t" << is[toms] << " h" << is[hihat] << " c" << is[cymbals]
                          << "  sub " << juce::String (subN, 2) << " body " << juce::String (bodyN, 2) << " wires " << juce::String (wiresN, 2) << " air " << juce::String (airN, 2)
                          << " lowC " << juce::String (lowCentre, 0) << " ring " << juce::String (ring, 2) << " lowRing " << juce::String (lowRing, 2) << " drop " << juce::String (drop, 2) << std::endl;
        }
    }
    if (! keepGoing (0.12f))
        return {};

    // ---- The sounds -------------------------------------------------------------------------------------------
    const int k = numParts * perPart;
    // W: k x span x bands; H: k x frames
    std::vector<float> w ((size_t) k * span * bands), w0, h ((size_t) k * frames, 0.0f), gate ((size_t) k * frames, 0.0f);
    const auto W = [&] (int c, int t, int b) -> float& { return w[((size_t) c * span + (size_t) t) * bands + (size_t) b]; };
    for (int part = 0; part < numParts; ++part)
    {
        // the general picture, and one from the kit's clean hits of this part (if any)
        // (its hits, the ones with nothing else at once counting most; what else was struck with it is mostly
        // elsewhere in the spectrum, and the general shape below keeps the learnt picture to its own region)
        std::vector<float> learnt ((size_t) span * bands, 0.0f);
        float count = 0.0f;
        for (const auto& hit : hits)
            if (hit.is[part] && hit.frame + span < frames)
            {
                const auto weight = hit.clean ? 1.0f : 0.35f;
                for (int t = 0; t < span; ++t)
                    for (int b = 0; b < bands; ++b)
                        learnt[(size_t) t * bands + (size_t) b] += weight * vAt (b, hit.frame + t);
                count += weight;
            }
        for (int which = 0; which < perPart; ++which)
        {
            const int c = part * perPart + which;
            float sum = 0.0f;
            for (int t = 0; t < span; ++t)
                for (int b = 0; b < bands; ++b)
                {
                    const auto general = shapeOf (part, bandHz[(size_t) b], t) + 1.0e-4f;
                    // (the learnt one keeps a little of the general shape: a stray sound in the clean hits doesn't own it)
                    auto x = (which == 0 && count > 0.0f) ? learnt[(size_t) t * bands + (size_t) b] / count / vmax * general / (general + 0.02f) + 0.02f * general
                                                       : general;
                    sum += W (c, t, b) = x;
                }
            for (int t = 0; t < span; ++t)
                for (int b = 0; b < bands; ++b)
                    W (c, t, b) /= juce::jmax (1.0e-12f, sum);
        }
    }
    w0 = w;

    // Where each part may strike: at its hits (a few frames' slack) - elsewhere only faintly (a missed hit can
    // still be explained). A cymbal's wash is struck again and again while it rings, so it may sound all along.
    float total = 0.0f;
    for (const auto x : v) total += x;
    const auto meanStrike = total / (float) frames / (float) k;
    for (int c = 0; c < k; ++c)
        for (int f = 0; f < frames; ++f)
            gate[(size_t) c * frames + (size_t) f] = (c / perPart == cymbals) ? 0.25f : 0.02f;
    for (const auto& hit : hits)
        for (int part = 0; part < numParts; ++part)
            if (hit.is[part])
                for (int which = 0; which < perPart; ++which)
                {
                    const int c = part * perPart + which;
                    const int until = part == cymbals ? juce::jmax (ringFrames * 8, 1) : 2;
                    for (int f = juce::jmax (0, hit.frame - 1); f <= juce::jmin (frames - 1, hit.frame + until); ++f)
                        gate[(size_t) c * frames + (size_t) f] = 1.0f;
                }
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = meanStrike * gate[i];

    std::vector<float> model ((size_t) bands * frames), ratio ((size_t) bands * frames), num ((size_t) k * frames);
    const auto rebuild = [&]
    {
        std::fill (model.begin(), model.end(), 1.0e-9f);
        for (int c = 0; c < k; ++c)
            for (int t = 0; t < span; ++t)
            {
                const auto* hc = h.data() + (size_t) c * frames;
                for (int b = 0; b < bands; ++b)
                {
                    const auto x = W (c, t, b);
                    if (x < 1.0e-7f)
                        continue;
                    auto* row = model.data() + (size_t) b * frames;
                    for (int f = t; f < frames; ++f)
                        row[f] += x * hc[f - t];
                }
            }
        for (size_t i = 0; i < v.size(); ++i)
            ratio[i] = v[i] / model[i];
    };

    constexpr int iterations = 36;
    for (int it = 0; it < iterations; ++it)
    {
        rebuild();
        // H: each strike as much as the recording asks for, over its picture's whole length
        std::fill (num.begin(), num.end(), 0.0f);
        for (int c = 0; c < k; ++c)
        {
            auto* nc = num.data() + (size_t) c * frames;
            float wsum = 0.0f;
            for (int t = 0; t < span; ++t)
                for (int b = 0; b < bands; ++b)
                {
                    const auto x = W (c, t, b);
                    wsum += x;
                    if (x < 1.0e-7f)
                        continue;
                    const auto* row = ratio.data() + (size_t) b * frames;
                    for (int f = 0; f + t < frames; ++f)
                        nc[f] += x * row[f + t];
                }
            auto* hc = h.data() + (size_t) c * frames;
            const auto* gc = gate.data() + (size_t) c * frames;
            for (int f = 0; f < frames; ++f)
                hc[f] *= nc[f] / juce::jmax (1.0e-9f, wsum) * (gc[f] < 1.0f ? 0.7f + 0.3f * gc[f] : 1.0f);
        }
        // W (once the strikes have settled): learns the kit, held near where each picture started
        if (it >= 12 && it % 3 == 0)
        {
            rebuild();
            for (int c = 0; c < k; ++c)
            {
                const auto* hc = h.data() + (size_t) c * frames;
                float strikes = 0.0f;
                for (int f = 0; f < frames; ++f)
                    strikes += hc[f];
                float norm = 0.0f;
                for (int t = 0; t < span; ++t)
                    for (int b = 0; b < bands; ++b)
                    {
                        const auto* row = ratio.data() + (size_t) b * frames;
                        double dot = 0.0;
                        for (int f = t; f < frames; ++f)
                            dot += hc[f - t] * row[f];
                        auto& x = W (c, t, b);
                        const auto start = w0[((size_t) c * span + (size_t) t) * bands + (size_t) b];
                        x = 0.7f * x * (float) dot / juce::jmax (1.0e-9f, strikes) + 0.3f * start;
                        norm += x;
                    }
                for (int t = 0; t < span; ++t)
                    for (int b = 0; b < bands; ++b)
                        W (c, t, b) /= juce::jmax (1.0e-12f, norm);
                auto* hm = h.data() + (size_t) c * frames;
                for (int f = 0; f < frames; ++f)
                    hm[f] *= norm;
            }
        }
        if (! keepGoing (0.12f + 0.68f * (float) (it + 1) / iterations))
            return {};
    }

    // Each part's model of the recording (its sounds where they struck), for its share of each band and frame
    std::vector<float> partMag ((size_t) numParts * bands * frames, 0.0f);
    for (int c = 0; c < k; ++c)
    {
        const int part = c / perPart;
        const auto* hc = h.data() + (size_t) c * frames;
        for (int t = 0; t < span; ++t)
            for (int b = 0; b < bands; ++b)
            {
                const auto x = W (c, t, b);
                if (x < 1.0e-7f)
                    continue;
                auto* row = partMag.data() + ((size_t) part * bands + (size_t) b) * frames;
                for (int f = t; f < frames; ++f)
                    row[f] += x * hc[f - t];
            }
    }
    // (where nothing explains a sound, it goes by where in the spectrum it is)
    std::vector<float> fallback ((size_t) numParts * bands);
    for (int part = 0; part < numParts; ++part)
        for (int b = 0; b < bands; ++b)
            fallback[(size_t) part * bands + (size_t) b] = 1.0e-9f * shapeOf (part, bandHz[(size_t) b], 0);
    if (! keepGoing (0.82f))
        return {};

    std::vector<std::vector<std::complex<float>>> parts ((size_t) numParts * spec.channels.size());
    for (auto& p : parts)
        p.assign ((size_t) frames * bins, {});
    for (int f = 0; f < frames; ++f)
        for (int b = 0; b < bins; ++b)
        {
            const auto band = (size_t) bandOf[(size_t) b];
            float m[numParts];
            float sum = 1.0e-30f;
            for (int p = 0; p < numParts; ++p)
            {
                const auto x = partMag[((size_t) p * bands + band) * frames + (size_t) f] + fallback[(size_t) p * bands + band];
                m[p] = x * x;   // (squared shares: cleaner parts, still adding up to the whole)
                sum += m[p];
            }
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
        if (! keepGoing (0.82f + 0.18f * (float) (p + 1) / numParts))
            return {};
    }
    return out;
}
}
