#pragma once

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
};

struct EffectInfo
{
    juce::String type, name;
    std::vector<ParamInfo> params;
};

/** All built-in effects, in menu order. */
const std::vector<EffectInfo>& builtInEffects();
const EffectInfo* findBuiltInEffect (const juce::String& type);

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

protected:
    static constexpr int maxParams = 12;
    const EffectInfo& info;
    std::array<std::atomic<float>, maxParams> values;
    double sampleRate = 44100.0;
};

/** A second-order IIR section (RBJ cookbook), allocation-free. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<float, 2> z1 {}, z2 {};

    void setLowShelf (double rate, double freq, double gainDb, double q = 0.707);
    void setHighShelf (double rate, double freq, double gainDb, double q = 0.707);
    void setPeak (double rate, double freq, double gainDb, double q);
    void reset() noexcept { z1 = {}; z2 = {}; }

    inline float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return y;
    }
};

} // namespace riff
