#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

/** Small DSP pieces the studio instruments share: a state-variable filter, envelopes, a room, a chorus. */
namespace riff::studio::dsp
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float dbToGain (float db) noexcept { return db <= -95.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }

    /** The TPT state-variable filter (Zavalishin): low, band and high pass from one set of states. */
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f, g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        void set (float cutoff, float q, double rate) noexcept
        {
            cutoff = juce::jlimit (16.0f, (float) rate * 0.45f, cutoff);
            g = std::tan (juce::MathConstants<float>::pi * cutoff / (float) rate);
            k = 1.0f / juce::jmax (0.3f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { ic1 = ic2 = 0.0f; }
        struct Out { float low, band, high; };
        Out run (float x) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }
        float low (float x) noexcept { return run (x).low; }
        float high (float x) noexcept { return run (x).high; }
        float band (float x) noexcept { return run (x).band; }
    };

    /** A one-pole low pass (for smoothing and gentle tone). */
    struct OnePole
    {
        float z = 0.0f, a = 0.5f;
        void set (float cutoff, double rate) noexcept { a = 1.0f - std::exp (-twoPi * juce::jlimit (10.0f, 20000.0f, cutoff) / (float) rate); }
        float run (float x) noexcept { z += a * (x - z); return z; }
    };

    /** A shelf made from a one-pole split: gain above or below `freq`. */
    struct Tilt
    {
        OnePole lp;
        float lowGain = 1.0f, highGain = 1.0f;
        void set (float freq, float lowDb, float highDb, double rate) noexcept { lp.set (freq, rate); lowGain = dbToGain (lowDb); highGain = dbToGain (highDb); }
        float run (float x) noexcept { const auto l = lp.run (x); return l * lowGain + (x - l) * highGain; }
    };

    /** ADSR in seconds (linear attack, exponential decay and release). */
    struct Adsr
    {
        enum Stage { idle, attack, decay, sustain, release };
        Stage stage = idle;
        float level = 0.0f, a = 0.001f, d = 0.2f, s = 1.0f, r = 0.2f;
        double rate = 48000.0;
        void set (float attackS, float decayS, float sustainLevel, float releaseS, double sampleRate) noexcept
        {
            a = juce::jmax (0.0005f, attackS); d = juce::jmax (0.002f, decayS); s = juce::jlimit (0.0f, 1.0f, sustainLevel); r = juce::jmax (0.002f, releaseS);
            rate = sampleRate;
        }
        void start() noexcept { stage = attack; }
        void stop() noexcept { if (stage != idle) stage = release; }
        void kill() noexcept { stage = idle; level = 0.0f; }
        bool active() const noexcept { return stage != idle; }
        float next() noexcept
        {
            switch (stage)
            {
                case attack:
                    level += 1.0f / (a * (float) rate);
                    if (level >= 1.0f) { level = 1.0f; stage = decay; }
                    break;
                case decay:
                    level = s + (level - s) * std::exp (-1.0f / (d * 0.25f * (float) rate));
                    if (level - s < 1.0e-4f) { level = s; stage = sustain; }
                    if (s <= 0.0f && level < 1.0e-4f) { level = 0.0f; stage = idle; }
                    break;
                case sustain: break;
                case release:
                    level *= std::exp (-1.0f / (r * 0.25f * (float) rate));
                    if (level < 1.0e-4f) { level = 0.0f; stage = idle; }
                    break;
                case idle: default: break;
            }
            return level;
        }
    };

    /** A small stereo room (Schroeder-Moorer: combs with damping into allpasses), for Room and Ambience. */
    class Room
    {
    public:
        void prepare (double sampleRate)
        {
            rate = sampleRate;
            const int combMs[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            const int apMs[4] { 556, 441, 341, 225 };
            const auto s = (float) sampleRate / 44100.0f;
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < 8; ++i)
                {
                    combs[c][i].buf.assign ((size_t) juce::jmax (8, (int) ((float) (combMs[i] + c * 23) * s)), 0.0f);
                    combs[c][i].pos = 0;
                    combs[c][i].store = 0.0f;
                }
                for (int i = 0; i < 4; ++i)
                {
                    aps[c][i].buf.assign ((size_t) juce::jmax (8, (int) ((float) (apMs[i] + c * 23) * s)), 0.0f);
                    aps[c][i].pos = 0;
                }
            }
            pre.assign ((size_t) (sampleRate * 0.1), 0.0f);
            prePos = 0;
        }
        /** size 0..1 (small to large), damping 0..1 (bright to dark), predelay ms. */
        void set (float size, float damping, float predelayMs) noexcept
        {
            feedback = 0.70f + 0.27f * juce::jlimit (0.0f, 1.0f, size);
            damp = 0.05f + 0.6f * juce::jlimit (0.0f, 1.0f, damping);
            preSamples = juce::jlimit (0, (int) pre.size() - 1, (int) (predelayMs * 0.001f * (float) rate));
        }
        void reset() noexcept
        {
            for (auto& side : combs) for (auto& c : side) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.store = 0.0f; }
            for (auto& side : aps) for (auto& a : side) std::fill (a.buf.begin(), a.buf.end(), 0.0f);
            std::fill (pre.begin(), pre.end(), 0.0f);
        }
        /** Adds the room's sound of (inL, inR) to outL/outR at `wet`. */
        void process (const float* inL, const float* inR, float* outL, float* outR, int n, float wet) noexcept
        {
            if (pre.empty() || wet <= 0.0f)
                return;
            for (int i = 0; i < n; ++i)
            {
                const auto mono = (inL[i] + inR[i]) * 0.5f;
                pre[(size_t) prePos] = mono;
                const auto read = (prePos - preSamples + (int) pre.size()) % (int) pre.size();
                prePos = (prePos + 1) % (int) pre.size();
                const auto x = pre[(size_t) read] * 0.03f;
                for (int c = 0; c < 2; ++c)
                {
                    float sum = 0.0f;
                    for (auto& cb : combs[c])
                    {
                        auto& y = cb.buf[(size_t) cb.pos];
                        cb.store = y * (1.0f - damp) + cb.store * damp;
                        const auto out = y;
                        y = x + cb.store * feedback;
                        cb.pos = (cb.pos + 1) % (int) cb.buf.size();
                        sum += out;
                    }
                    for (auto& ap : aps[c])
                    {
                        auto& b = ap.buf[(size_t) ap.pos];
                        const auto out = b - sum;
                        b = sum + b * 0.5f;
                        ap.pos = (ap.pos + 1) % (int) ap.buf.size();
                        sum = out;
                    }
                    (c == 0 ? outL : outR)[i] += sum * wet;
                }
            }
        }
    private:
        struct Comb { std::vector<float> buf; int pos = 0; float store = 0.0f; };
        struct Ap { std::vector<float> buf; int pos = 0; };
        std::array<std::array<Comb, 8>, 2> combs;
        std::array<std::array<Ap, 4>, 2> aps;
        std::vector<float> pre;
        int prePos = 0, preSamples = 0;
        float feedback = 0.84f, damp = 0.3f;
        double rate = 48000.0;
    };

    /** A stereo chorus (two modulated delays, as the classic polysynths' chorus I and II). */
    class Chorus
    {
    public:
        void prepare (double sampleRate) { rate = sampleRate; buf[0].assign ((size_t) (sampleRate * 0.05), 0.0f); buf[1] = buf[0]; pos = 0; }
        void process (float* l, float* r, int n, float rateHz, float depthMs, float mix) noexcept
        {
            if (buf[0].empty() || mix <= 0.0f) return;
            const auto size = (int) buf[0].size();
            const auto inc = rateHz / (float) rate;
            for (int i = 0; i < n; ++i)
            {
                const auto mono = (l[i] + r[i]) * 0.5f;
                buf[0][(size_t) pos] = mono;
                phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                const auto tri = std::abs (phase * 4.0f - 2.0f) - 1.0f;   // (a triangle, as theirs)
                for (int c = 0; c < 2; ++c)
                {
                    const auto delayMs = 3.5f + depthMs * 0.5f * (1.0f + (c == 0 ? tri : -tri));
                    const auto d = delayMs * 0.001f * (float) rate;
                    auto readPos = (float) pos - d;
                    while (readPos < 0.0f) readPos += (float) size;
                    const auto i0 = (int) readPos, i1 = (i0 + 1) % size;
                    const auto f = readPos - (float) i0;
                    const auto wet = buf[0][(size_t) i0] + (buf[0][(size_t) i1] - buf[0][(size_t) i0]) * f;
                    auto& out = c == 0 ? l[i] : r[i];
                    out = out * (1.0f - mix * 0.5f) + wet * mix;
                }
                pos = (pos + 1) % size;
            }
        }
    private:
        std::array<std::vector<float>, 2> buf;
        int pos = 0;
        float phase = 0.0f;
        double rate = 48000.0;
    };

    /** A soft clipper with drive (0..1): the amps' and the drums' grit. */
    inline float drive (float x, float amount) noexcept
    {
        if (amount <= 0.001f) return x;
        const auto k = 1.0f + amount * 18.0f;
        return std::tanh (x * k) / std::tanh (k) * (1.0f + amount * 0.5f) / (1.0f + amount * 0.5f);
    }

    /** Velocity curves: Linear, Soft (louder early), Hard (needs more), Fixed. */
    inline float velocityCurve (float v, int curve) noexcept
    {
        switch (curve)
        {
            case 1: return std::pow (v, 0.6f);
            case 2: return std::pow (v, 1.6f);
            case 3: return 0.8f;
            default: return v;
        }
    }
}
