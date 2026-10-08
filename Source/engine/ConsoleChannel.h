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

        bool operator== (const Settings&) const = default;

        /** True if it would change the sound at all. */
        bool isActive() const noexcept
        {
            return enabled && (std::abs (hf) > 0.01f || std::abs (hmf) > 0.01f || std::abs (lmf) > 0.01f
                                || std::abs (lf) > 0.01f || (dynEnabled && dynamics > 0.01f));
        }
    };

    /** Reads a TRACK's console settings. */
    static Settings fromTrack (const juce::ValueTree& track);

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Recomputes the filters only when something changed. Audio thread. */
    void setSettings (const Settings& s) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    /** Gain reduction of the Dynamics section, dB (positive), for tests and meters. */
    float getGainReductionDb() const noexcept { return envelopeDb; }

    static constexpr double hfFrequency = 8000.0, hmfFrequency = 2500.0, lmfFrequency = 400.0, lfFrequency = 100.0;

private:
    void updateFilters() noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    Biquad hfFilter, hmfFilter, lmfFilter, lfFilter;
    float envelopeDb = 0.0f;
    bool filtersValid = false;
};

} // namespace riff
