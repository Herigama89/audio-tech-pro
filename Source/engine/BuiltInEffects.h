#pragma once

#include <complex>

#include "Effect.h"
#include <array>
#include <atomic>
#include <vector>

namespace riff
{

/** Describes one parameter of a built-in effect (for the UI and for the project file). */
struct ParamInfo
{
    juce::String id, name, suffix;
    float min, max, defaultValue, midPoint;   // midPoint: value at the centre of the knob
    std::vector<juce::String> choices {};     // a switch: values 0..n-1, shown by name (min 0, max n-1)

    bool isChoice() const noexcept { return ! choices.empty(); }
    juce::String text (float value) const
    {
        if (isChoice())
            return choices[(size_t) juce::jlimit (0, (int) choices.size() - 1, juce::roundToInt (value))];
        return {};
    }
};

struct EffectInfo
{
    juce::String type, name;
    std::vector<ParamInfo> params;
    juce::String category {};   // as Logic's plug-in menu: EQ, Dynamics, Delay, Reverb, Modulation...

    /** For EQs: the curve (dB at a frequency) for a set of parameter values, drawn in the effect's window. */
    std::function<double (const float* values, double freq, double sampleRate)> response {};

    /** How the window lays its knobs out, when not simply in rows: how many knobs stack in each column
        (an EQ's bands side by side, each band's frequency, gain and Q above one another). */
    std::vector<int> columns {};
};

/** All built-in effects, in menu order. */
const std::vector<EffectInfo>& builtInEffects();
const EffectInfo* findBuiltInEffect (const juce::String& type);

/** Logic's plug-in categories, in menu order. */
const juce::StringArray& effectCategories();

class BuiltInEffect;
/** One stock effect: its description and how to make it. Each family (EQ, Dynamics...) lists its own. */
struct StockEffect
{
    EffectInfo info;
    std::function<std::unique_ptr<BuiltInEffect> (const EffectInfo&)> make;
};
const std::vector<StockEffect>& stockEqEffects();
const std::vector<StockEffect>& stockDynamicsEffects();
const std::vector<StockEffect>& stockDelayEffects();
const std::vector<StockEffect>& stockReverbEffects();
const std::vector<StockEffect>& stockModulationEffects();
const std::vector<StockEffect>& stockDistortionEffects();
const std::vector<StockEffect>& stockFilterEffects();
const std::vector<StockEffect>& stockAmpEffects();
const std::vector<StockEffect>& stockPitchEffects();
const std::vector<StockEffect>& stockUtilityEffects();
const std::vector<StockEffect>& stockExtraEffects();
const std::vector<StockEffect>& stockPedalboardEffects();
const std::vector<StockEffect>& stockMeteringEffects();
/** Pitch Correction's scales and chords: their names, and each one's notes from the root (12 flags). */
const std::vector<juce::String>& pitchScaleNames();
const int* pitchScaleMask (int scale);
/** The Pedalboard: a pedal type's three knob names. */
std::vector<juce::String> pedalboardKnobNames (int type);

/**
    Base for Riff's own effects. Parameter values are atomics written by the
    message thread and read by the audio thread once per block.
*/
class BuiltInEffect : public Effect
{
public:
    static std::unique_ptr<BuiltInEffect> create (const juce::String& type);

    explicit BuiltInEffect (const EffectInfo& info);

    const EffectInfo& getInfo() const noexcept   { return info; }
    juce::String getName() const override         { return info.name; }

    void setParameter (int index, float value) noexcept;
    void setAutomatedParameter (int index, float value) noexcept override { setParameter (index, value); }
    float getParameter (int index) const noexcept;

    void setPosition (juce::int64, bool isPlaying, double beatsPerMinute) noexcept override
    {
        bpm.store (beatsPerMinute > 1.0 ? beatsPerMinute : 120.0, std::memory_order_relaxed);
        playing.store (isPlaying, std::memory_order_relaxed);
    }

    /** A file the effect works from (Space Designer's impulse response), from the insert's "irFile" property.
        Message thread; empty for none. */
    virtual void setAttachedFile (const juce::String& /*path*/) {}

    /** Called on the message thread after the knobs were set (the Pedalboard builds its pedals here). */
    virtual void messageThreadUpdate() {}

    /** More readings for its window (meters, loudness, a goniometer's points): fills dest, gives how many. Message thread. */
    virtual int readMeters (float* /*dest*/, int /*capacity*/) const noexcept { return 0; }
    static constexpr int meterCapacity = 1024;

    /** What its window's meter shows (a compressor's gain reduction in dB, a pitch correction's cents). */
    virtual float displayValue() const noexcept { return 0.0f; }

    static constexpr int maxParams = 64;

    //==============================================================================
    // The analyzer's feed: while a window shows the spectrum, the last few thousand output samples (mono)
    static constexpr int tapSize = 4096;
    void setAnalyzerOn (bool on) noexcept   { tapOn.store (on, std::memory_order_relaxed); }
    void tapOutput (const float* left, const float* right, int numSamples) noexcept override
    {
        if (! tapOn.load (std::memory_order_relaxed))
            return;
        auto at = tapPos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            tap[(size_t) at].store (0.5f * (left[i] + (right != nullptr ? right[i] : left[i])), std::memory_order_relaxed);
            at = (at + 1) & (tapSize - 1);
        }
        tapPos.store (at, std::memory_order_release);
    }
    /** The latest count samples, oldest first (message thread). */
    void readTap (float* dest, int count) const noexcept
    {
        auto at = (tapPos.load (std::memory_order_acquire) - count) & (tapSize - 1);
        for (int i = 0; i < count; ++i, at = (at + 1) & (tapSize - 1))
            dest[i] = tap[(size_t) at].load (std::memory_order_relaxed);
    }
    double getSampleRate() const noexcept   { return sampleRate; }

protected:
    const EffectInfo& info;
    std::array<std::atomic<float>, maxParams> values;
    double sampleRate = 44100.0;
    std::atomic<double> bpm { 120.0 };   // the song's tempo, for delays in time with it
    std::atomic<bool> playing { false };
    std::atomic<bool> tapOn { false };
    std::array<std::atomic<float>, tapSize> tap {};
    std::atomic<int> tapPos { 0 };

    float value (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
    int choice (int index) const noexcept  { return juce::roundToInt (value (index)); }
};

/** A second-order IIR section (RBJ cookbook), allocation-free. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<float, 2> z1 {}, z2 {};

    void setLowShelf (double rate, double freq, double gainDb, double q = 0.707);
    void setHighShelf (double rate, double freq, double gainDb, double q = 0.707);
    void setPeak (double rate, double freq, double gainDb, double q);
    void setHighPass (double rate, double freq, double q = 0.707);
    void setLowPass (double rate, double freq, double q = 0.707);
    void setBandPass (double rate, double freq, double q);
    void setNotch (double rate, double freq, double q);
    void setFirstOrderHighPass (double rate, double freq);   // (6 dB per octave)
    void setFirstOrderLowPass (double rate, double freq);
    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void reset() noexcept { z1 = {}; z2 = {}; }

    /** The filter's gain at a frequency, in dB (for drawing EQ curves). */
    double magnitudeDb (double freq, double rate) const noexcept
    {
        const auto w = juce::MathConstants<double>::twoPi * freq / rate;
        const std::complex<double> z = std::polar (1.0, -w), z2c = z * z;
        const auto num = (double) b0 + (double) b1 * z + (double) b2 * z2c;
        const auto den = 1.0 + (double) a1 * z + (double) a2 * z2c;
        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (num / den)));
    }

    inline float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return y;
    }
};

} // namespace riff
