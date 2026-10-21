#pragma once

#include "BuiltInEffects.h"
#include <cmath>

/**
    Small building blocks shared by the stock effects: delay lines, filters, envelopes, crossovers,
    saturation and tempo-synced note values. Everything here is allocation-free once prepared.
*/
namespace riff::dsp
{
    inline constexpr float pi = juce::MathConstants<float>::pi;
    inline constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float dbToGain (float db) noexcept  { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept   { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }

    /** A one-pole smoothing coefficient for a time constant in milliseconds. */
    inline float coeffMs (double ms, double rate) noexcept
    {
        return (float) std::exp (-1.0 / std::max (1.0e-6, 0.001 * ms * rate));
    }

    /** Note values for tempo-synced times (Logic's list), in beats. */
    inline const std::vector<juce::String>& noteNames()
    {
        static const std::vector<juce::String> names { "1/64", "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.",
                                                       "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1/1", "2/1" };
        return names;
    }
    inline double noteBeats (int index) noexcept
    {
        static const double beats[] { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 3.0 / 8, 1.0 / 3, 1.0 / 2, 3.0 / 4,
                                      2.0 / 3, 1.0, 1.5, 4.0 / 3, 2.0, 3.0, 4.0, 8.0 };
        return beats[juce::jlimit (0, 15, index)];
    }
    inline double noteSeconds (int index, double bpm) noexcept { return noteBeats (index) * 60.0 / std::max (20.0, bpm); }

    /** A delay line read anywhere between samples (cubic), with a fixed most-delay set when prepared. */
    struct DelayLine
    {
        std::vector<float> data;
        int mask = 0, write = 0;

        void prepare (int maxSamples)
        {
            int size = 1;
            while (size < maxSamples + 4)
                size <<= 1;
            data.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }
        void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); }
        void push (float x) noexcept { data[(size_t) write] = x; write = (write + 1) & mask; }

        /** The sample `delay` samples ago (fractional: Hermite interpolation). Call before push(). */
        float read (float delay) const noexcept
        {
            delay = juce::jlimit (1.0f, (float) (mask - 3), delay);
            const auto pos = (float) write - delay;
            const auto base = (int) std::floor (pos);
            const auto t = pos - (float) base;
            const auto ym1 = data[(size_t) ((base - 1) & mask)], y0 = data[(size_t) (base & mask)];
            const auto y1 = data[(size_t) ((base + 1) & mask)], y2 = data[(size_t) ((base + 2) & mask)];
            const auto c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const auto c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * t + c2) * t + c1) * t + y0;
        }
        float readInt (int delay) const noexcept { return data[(size_t) ((write - juce::jlimit (1, mask, delay)) & mask)]; }
    };

    /** One-pole low pass (and the matching high pass), cheap tone controls. */
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void setLowPass (double freq, double rate) noexcept
        {
            a = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (5.0, rate * 0.49, freq) / rate);
        }
        float lowPass (float x) noexcept  { z = x + a * (z - x); return z; }
        float highPass (float x) noexcept { return x - lowPass (x); }
        void reset() noexcept { z = 0.0f; }
    };

    /** A fourth-order Linkwitz-Riley crossover: low + high add back up flat. */
    struct Crossover
    {
        Biquad lp1, lp2, hp1, hp2;
        void set (double freq, double rate)
        {
            lp1.setLowPass (rate, freq, 0.70710678);
            lp2.setLowPass (rate, freq, 0.70710678);
            hp1.setHighPass (rate, freq, 0.70710678);
            hp2.setHighPass (rate, freq, 0.70710678);
        }
        void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
        void split (float x, int ch, float& low, float& high) noexcept
        {
            low = lp2.process (lp1.process (x, ch), ch);
            high = hp2.process (hp1.process (x, ch), ch);   // (fourth order: in phase with the low band, no flip)
        }

        /** The same phase shift as split() without splitting (for bands that don't cross here). */
        float allPass (float x, int ch) noexcept
        {
            float low, high;
            split (x, ch, low, high);
            return low + high;
        }
    };

    /** Butterworth Qs for a fourth-order (24 dB/octave) filter from two sections. */
    inline constexpr double butterQ1 = 0.54119610, butterQ2 = 1.30656296;

    /** Soft saturation: tanh-like, unity gain for small signals. */
    inline float saturate (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** An LFO: sine, from 0..1 phase. */
    struct Lfo
    {
        double phase = 0.0;
        float next (double hz, double rate, double offset = 0.0) noexcept
        {
            const auto v = (float) std::sin (juce::MathConstants<double>::twoPi * (phase + offset));
            phase += hz / rate;
            phase -= std::floor (phase);
            return v;
        }
        float peek (double offset) const noexcept { return (float) std::sin (juce::MathConstants<double>::twoPi * (phase + offset)); }
    };

    /** A cheap noise source (for tape flutter and the like). */
    struct Noise
    {
        juce::uint32 state = 22222;
        float next() noexcept { state = state * 1664525u + 1013904223u; return (float) (state >> 8) / 8388608.0f - 1.0f; }
    };

    /** A state-variable filter (topology-preserving: stays smooth when swept fast). */
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        void set (double freq, double q, double rate) noexcept
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, rate * 0.45, freq) / rate);
            k = (float) (1.0 / std::max (0.3, q));
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { ic1 = ic2 = 0.0f; }
        /** Runs one sample; gives the low-, band- and high-pass outputs. */
        void tick (float x, float& low, float& band, float& high) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            low = v2;
            band = v1;
            high = x - k * v1 - v2;
        }
    };

    /** A first-order all-pass section (phasers, spring dispersion). */
    struct AllPass1
    {
        float z = 0.0f;
        float process (float x, float a) noexcept
        {
            const auto y = a * x + z;
            z = x - a * y;
            return y;
        }
    };

    /** A Schroeder all-pass around a delay (reverb diffusion). */
    struct DiffusionAllPass
    {
        DelayLine line;
        float process (float x, float delay, float g) noexcept
        {
            const auto d = line.read (delay);
            const auto v = x - g * d;
            line.push (v);
            return d + g * v;
        }
    };

    /** The decibel gain of the usual knee'd compressor curve. */
    inline float compressorGainDb (float levelDb, float threshold, float ratio, float kneeDb) noexcept
    {
        const auto over = levelDb - threshold;
        const auto slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (kneeDb > 0.01f && std::abs (over) <= kneeDb * 0.5f)
        {
            const auto x = over + kneeDb * 0.5f;
            return -slope * x * x / (2.0f * kneeDb);
        }
        return over > 0.0f ? -slope * over : 0.0f;
    }
}
