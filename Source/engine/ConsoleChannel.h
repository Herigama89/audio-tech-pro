#pragma once

#include "BuiltInEffects.h"

namespace riff
{

/**
    The console section at the top of every mixer strip: a four-band EQ
    (HF and LF shelves, HMF and LMF bells) and a one-knob compressor
    ("Dynamics"). It runs before the insert effects.

    One per track slot, owned by the engine, so its filter and envelope
    state survive graph rebuilds. Real-time safe.
*/
class ConsoleChannel
{
public:
    struct Band { const char* id; const char* name; double frequency; double q; };

    static constexpr float maxGainDb = 15.0f;
    static constexpr float maxDynamics = 10.0f;

    struct Settings
    {
        bool enabled = true;            // the EQ (and the whole section) in
        bool dynEnabled = true;         // the Dynamics in
        float hf = 0.0f, hmf = 0.0f, lmf = 0.0f, lf = 0.0f;   // dB
        float dynamics = 0.0f;                               // 0 (off) .. 10

        // The full strip (its window). The defaults sound exactly like the knobs alone.
        float inputDb = 0.0f, outputDb = 0.0f;
        float hpf = 0.0f, lpf = 0.0f;                        // Hz, 0 = off
        float hfFreq = 8000.0f, hmfFreq = 2500.0f, lmfFreq = 400.0f, lfFreq = 100.0f;
        float hmfQ = 1.0f, lmfQ = 1.0f;
        bool hfBell = false, lfBell = false;
        float compRatio = 0.0f;                              // 0: auto, from the Dynamics knob
        float compRelease = 0.15f;                           // seconds
        bool compFast = false;
        float gateThreshold = -80.0f, gateRange = 20.0f, gateRelease = 0.25f;   // dB, dB, s (-80: off)
        bool gateExpander = false;

        // The rest of the panel (the strip window's photographed face)
        bool eqIn = true;                   // the EQ in (EQ IN); `enabled` is the whole strip (its power)
        bool pad = false, phase = false;    // -20 dB in; the polarity turned over
        bool line = true;                   // line in; off: the mic preamp, its gain and its colour
        float micGain = 20.0f;              // dB, +20..+70 (the mic preamp)
        bool deEss = false;
        float deEssThreshold = 0.0f;        // as printed, +10..-20 (dBFS: 20 below)
        bool compKnee = false, compLinear = false, link = true, externalKey = false;
        bool gateFast = false;
        bool filtersToSidechain = false, filtersAtInput = true;
        bool blackEq = false, eqToSidechain = false;
        bool dynPostEq = true, deEssPostEq = true;

        bool operator== (const Settings&) const = default;

        bool gateOn() const noexcept { return gateThreshold > -79.9f; }
        bool eqActive() const noexcept
        {
            return eqIn && (std::abs (hf) > 0.01f || std::abs (hmf) > 0.01f || std::abs (lmf) > 0.01f || std::abs (lf) > 0.01f);
        }
        bool filtersActive() const noexcept { return hpf > 0.0f || lpf > 0.0f; }
        bool dynActive() const noexcept { return dynEnabled && (dynamics > 0.01f || gateOn()); }

        /** True if it would change the sound at all. */
        bool isActive() const noexcept
        {
            return enabled && (eqActive() || dynActive() || deEss || pad || phase || ! line
                                || std::abs (inputDb) > 0.01f || std::abs (outputDb) > 0.01f || filtersActive());
        }
    };

    /** The EQ and filters' combined response at a frequency, dB (for the strip window's curve). */
    static double responseDb (const Settings& s, double frequency, double sampleRate = 48000.0);

    /** Reads a TRACK's console settings. */
    static Settings fromTrack (const juce::ValueTree& track);

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Recomputes the filters only when something changed. Audio thread. */
    void setSettings (const Settings& s) noexcept;

    /** `keyLeft/keyRight`: another track's sound, keying the dynamics (with externalKey; nullptr: its own). */
    void process (float* left, float* right, int numSamples, const float* keyLeft = nullptr, const float* keyRight = nullptr) noexcept;

    /** Gain reduction of the Dynamics section, dB (positive), for tests and meters. */
    float getGainReductionDb() const noexcept { return envelopeDb; }
    /** ... of the de-esser and of the gate (dB, positive). */
    float getDeEssReductionDb() const noexcept { return deEssDb; }
    float getGateReductionDb() const noexcept { return -gateGainDb; }

    static constexpr double hfFrequency = 8000.0, hmfFrequency = 2500.0, lmfFrequency = 400.0, lfFrequency = 100.0;

private:
    void updateFilters() noexcept;
    void runEq (float* l, float* r, int n) noexcept;
    void runFilters (float* l, float* r, int n) noexcept;
    void runDeEss (float* l, float* r, int n) noexcept;
    void runDynamics (float* l, float* r, int n, const float* keyL, const float* keyR) noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    Biquad hfFilter, hmfFilter, lmfFilter, lfFilter, hpf1, hpf2, lpf1;
    // (the same filters again, for the side chain: their own memory)
    Biquad sideHf, sideHmf, sideLmf, sideLf, sideHpf1, sideHpf2, sideLpf;
    float envelopeDb = 0.0f, gateGainDb = 0.0f;
    std::array<float, 2> channelEnvelope {};   // (unlinked: each side's own)
    float deEssLow[2] {}, deEssEnv = 0.0f, deEssDb = 0.0f;
    bool filtersValid = false;
};

} // namespace riff
