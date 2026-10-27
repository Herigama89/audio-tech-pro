#pragma once

#include <JuceHeader.h>
#include "../BuiltInEffects.h"   // Biquad

/**
    Nemix's effects rack: distortion, filter, chorus, phaser, delay, a three-band upward/downward compressor
    (the "squash everything" sound), reverb and EQ. Stereo, allocation-free once prepared; each block is
    given its settings (already modulated) by the synth.
*/
namespace riff::nemix::fx
{
    inline float fastTanh (float x) noexcept
    {
        x = juce::jlimit (-4.5f, 4.5f, x);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** A state-variable filter (topology-preserving), stereo, for the FX filter and the band splits. */
    struct Svf
    {
        float g = 0.0f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1[2] {}, ic2[2] {};
        void set (double rate, double freq, double q) noexcept
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, rate * 0.49, freq) / rate);
            k = (float) (1.0 / std::max (0.05, q));
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
        /** low, band, high at once */
        inline void tick (float x, int ch, float& low, float& band, float& high) noexcept
        {
            const auto v3 = x - ic2[ch];
            const auto v1 = a1 * ic1[ch] + a2 * v3;
            const auto v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            low = v2; band = v1; high = x - k * v1 - v2;
        }
    };

    /** A delay line read with interpolation. */
    struct Line
    {
        std::vector<float> buf;
        int pos = 0;
        void prepare (int size) { buf.assign ((size_t) juce::nextPowerOfTwo (std::max (4, size)), 0.0f); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
        inline void push (float x) noexcept { pos = (pos + 1) & ((int) buf.size() - 1); buf[(size_t) pos] = x; }
        inline float read (float delay) const noexcept
        {
            const auto mask = (int) buf.size() - 1;
            delay = juce::jlimit (1.0f, (float) mask - 2.0f, delay);
            const auto d = (int) delay;
            const auto f = delay - (float) d;
            const auto a = buf[(size_t) ((pos - d) & mask)], b = buf[(size_t) ((pos - d - 1) & mask)];
            return a + f * (b - a);
        }
    };

    struct Settings
    {
        bool dsOn = false; int dsType = 0; float dsDrive = 0.3f, dsTone = 0.5f, dsMix = 1.0f;
        bool ffOn = false; int ffType = 0; float ffCutoff = 8000.0f, ffRes = 0.1f, ffMix = 1.0f;
        bool chOn = false; float chRate = 0.8f, chDepth = 0.35f, chDelay = 8.0f, chFeedback = 0.1f, chMix = 0.5f;
        bool phOn = false; float phRate = 0.3f, phDepth = 0.6f, phFreq = 800.0f, phFeedback = 0.5f, phMix = 0.5f;
        bool dlOn = false; double dlSeconds = 0.25; float dlFeedback = 0.35f; bool dlPing = true; float dlTone = 0.5f, dlMix = 0.25f;
        bool otOn = false; float otDepth = 0.5f, otTime = 0.5f, otUp = 0.6f, otDown = 0.6f, otGainDb = 0.0f;
        bool rvOn = false; float rvSize = 0.6f, rvDecay = 2.5f, rvDamp = 0.5f, rvPre = 10.0f, rvWidth = 1.0f, rvMix = 0.2f;
        bool eqOn = false; float eqLow = 0.0f, eqLowFreq = 120.0f, eqHigh = 0.0f, eqHighFreq = 6000.0f;
    };

    class Rack
    {
    public:
        void prepare (double sampleRate);
        void reset();
        /** In place, stereo. */
        void process (float* left, float* right, int n, const Settings& s) noexcept;

    private:
        void distortion (float* l, float* r, int n, const Settings& s) noexcept;
        void filter (float* l, float* r, int n, const Settings& s) noexcept;
        void chorus (float* l, float* r, int n, const Settings& s) noexcept;
        void phaser (float* l, float* r, int n, const Settings& s) noexcept;
        void delay (float* l, float* r, int n, const Settings& s) noexcept;
        void squash (float* l, float* r, int n, const Settings& s) noexcept;
        void reverb (float* l, float* r, int n, const Settings& s) noexcept;
        void eq (float* l, float* r, int n, const Settings& s) noexcept;

        double rate = 48000.0;
        // distortion
        float dsTone[2] {}, crushHold[2] {}; float crushCount = 0.0f;
        // filter
        Svf ff;
        // chorus
        Line chLine[2]; double chPhase = 0.0;
        // phaser
        float phState[2][6] {}, phLast[2] {}; double phPhase = 0.0;
        // delay
        Line dlLine[2]; float dlLow[2] {};
        // squash: three bands, each band's envelope
        Svf split1, split2; float env[3] {};
        // reverb: pre-delay, four diffusers, an eight-line feedback network
        Line pre[2];
        struct Allpass { Line line; int length = 1; } diffusers[4];
        Line fdn[8]; int fdnLength[8] {}; float fdnLow[8] {}; double fdnMod = 0.0;
        // eq
        Biquad lowShelf, highShelf; float lastEq[4] { -1000.0f, -1000.0f, -1000.0f, -1000.0f };
    };
}
