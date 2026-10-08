#include "BuiltInEffects.h"

namespace riff
{

//==============================================================================
// Catalogue
//==============================================================================
const std::vector<EffectInfo>& builtInEffects()
{
    static const std::vector<EffectInfo> effects {
        { "eq", "EQ", {
            { "lowFreq",  "Low Freq",  " Hz", 20.0f,   1000.0f,  100.0f,  150.0f },
            { "lowGain",  "Low",       " dB", -18.0f,  18.0f,    0.0f,    0.0f },
            { "midFreq",  "Mid Freq",  " Hz", 100.0f,  10000.0f, 1000.0f, 1000.0f },
            { "midGain",  "Mid",       " dB", -18.0f,  18.0f,    0.0f,    0.0f },
            { "midQ",     "Mid Q",     "",    0.2f,    10.0f,    1.0f,    1.2f },
            { "highFreq", "High Freq", " Hz", 1000.0f, 18000.0f, 8000.0f, 5000.0f },
            { "highGain", "High",      " dB", -18.0f,  18.0f,    0.0f,    0.0f } } },

        { "compressor", "Compressor", {
            { "threshold", "Threshold", " dB", -60.0f, 0.0f,    -18.0f, -18.0f },
            { "ratio",     "Ratio",     ":1",   1.0f,   20.0f,  4.0f,   4.0f },
            { "attack",    "Attack",    " ms",  0.1f,   100.0f, 10.0f,  10.0f },
            { "release",   "Release",   " ms",  10.0f,  1000.0f, 120.0f, 150.0f },
            { "makeup",    "Makeup",    " dB",  0.0f,   24.0f,  0.0f,   6.0f } } },

        { "delay", "Delay", {
            { "time",     "Time",     " ms", 1.0f, 2000.0f, 375.0f, 400.0f },
            { "feedback", "Feedback", "",    0.0f, 0.95f,   0.35f,  0.4f },
            { "mix",      "Mix",      "",    0.0f, 1.0f,    0.3f,   0.5f } } },

        { "reverb", "Reverb", {
            { "size",    "Size",    "", 0.0f, 1.0f, 0.6f,  0.5f },
            { "damping", "Damping", "", 0.0f, 1.0f, 0.5f,  0.5f },
            { "width",   "Width",   "", 0.0f, 1.0f, 1.0f,  0.5f },
            { "mix",     "Mix",     "", 0.0f, 1.0f, 0.25f, 0.5f } } },

        { "limiter", "Limiter", {
            { "ceiling", "Ceiling", " dB", -12.0f, 0.0f,    -0.3f, -3.0f },
            { "release", "Release", " ms", 10.0f,  1000.0f, 100.0f, 150.0f } } },
    };

    return effects;
}

const EffectInfo* findBuiltInEffect (const juce::String& type)
{
    for (const auto& e : builtInEffects())
        if (e.type == type)
            return &e;
    return nullptr;
}

//==============================================================================
// Base
//==============================================================================
BuiltInEffect::BuiltInEffect (const EffectInfo& i) : info (i)
{
    jassert ((int) info.params.size() <= maxParams);

    for (size_t p = 0; p < values.size(); ++p)
        values[p].store (p < info.params.size() ? info.params[p].defaultValue : 0.0f);
}

void BuiltInEffect::setParameter (int index, float value) noexcept
{
    if (juce::isPositiveAndBelow (index, (int) info.params.size()))
    {
        const auto& p = info.params[(size_t) index];
        values[(size_t) index].store (juce::jlimit (p.min, p.max, value));
    }
}

float BuiltInEffect::getParameter (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, maxParams) ? values[(size_t) index].load() : 0.0f;
}

//==============================================================================
// Biquad (RBJ Audio EQ Cookbook)
//==============================================================================
namespace
{
    void normalise (Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2)
    {
        f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b2 / a0);
        f.a1 = (float) (a1 / a0); f.a2 = (float) (a2 / a0);
    }

    double clampFreq (double freq, double rate) { return juce::jlimit (10.0, rate * 0.49, freq); }
}

void Biquad::setLowShelf (double rate, double freq, double gainDb, double q)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi * clampFreq (freq, rate) / rate;
    const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const auto s = 2.0 * std::sqrt (A) * alpha;
    normalise (*this,
               A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s),
               (A + 1) + (A - 1) * cw + s, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s);
}

void Biquad::setHighShelf (double rate, double freq, double gainDb, double q)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi * clampFreq (freq, rate) / rate;
    const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const auto s = 2.0 * std::sqrt (A) * alpha;
    normalise (*this,
               A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s),
               (A + 1) - (A - 1) * cw + s, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s);
}

void Biquad::setPeak (double rate, double freq, double gainDb, double q)
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi * clampFreq (freq, rate) / rate;
    const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * juce::jmax (0.05, q));
    normalise (*this, 1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
}

//==============================================================================
// Effects
//==============================================================================
namespace
{

class EqEffect final : public BuiltInEffect
{
public:
    using BuiltInEffect::BuiltInEffect;

    void prepare (double rate, int) override
    {
        sampleRate = preparedRate = rate;
        for (auto* f : { &low, &mid, &high })
            f->reset();
        lastValues.fill (-1.0e9f);
    }

    void process (float* l, float* r, int n) noexcept override
    {
        // Recalculate only when a knob moved.
        std::array<float, 7> v;
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = values[i].load();

        if (v != lastValues)
        {
            low.setLowShelf (sampleRate, v[0], v[1]);
            mid.setPeak (sampleRate, v[2], v[3], v[4]);
            high.setHighShelf (sampleRate, v[5], v[6]);
            lastValues = v;
        }

        for (int i = 0; i < n; ++i)
        {
            l[i] = high.process (mid.process (low.process (l[i], 0), 0), 0);
            r[i] = high.process (mid.process (low.process (r[i], 1), 1), 1);
        }
    }

private:
    Biquad low, mid, high;
    std::array<float, 7> lastValues {};
};

class CompressorEffect final : public BuiltInEffect
{
public:
    using BuiltInEffect::BuiltInEffect;

    void prepare (double rate, int) override
    {
        sampleRate = preparedRate = rate;
        reductionDb = 0.0f;
    }

    void process (float* l, float* r, int n) noexcept override
    {
        const auto threshold = values[0].load(), ratio = juce::jmax (1.0f, values[1].load());
        const auto attack  = (float) std::exp (-1.0 / (0.001 * values[2].load() * sampleRate));
        const auto release = (float) std::exp (-1.0 / (0.001 * values[3].load() * sampleRate));
        const auto makeup = values[4].load();
        const auto slope = 1.0f - 1.0f / ratio;

        for (int i = 0; i < n; ++i)
        {
            // Stereo-linked peak detection, smoothed in decibels.
            const auto level = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const auto levelDb = juce::Decibels::gainToDecibels (level, -120.0f);
            const auto target = juce::jmax (0.0f, levelDb - threshold) * slope;
            const auto coeff = target > reductionDb ? attack : release;
            reductionDb = target + coeff * (reductionDb - target);

            const auto gain = juce::Decibels::decibelsToGain (makeup - reductionDb);
            l[i] *= gain;
            r[i] *= gain;
        }
    }

private:
    float reductionDb = 0.0f;
};

class DelayEffect final : public BuiltInEffect
{
public:
    using BuiltInEffect::BuiltInEffect;

    void prepare (double rate, int) override
    {
        sampleRate = preparedRate = rate;
        buffer.setSize (2, (int) (rate * 2.1) + 4);
        buffer.clear();
        writePos = 0;
        delaySamples.reset (rate, 0.05);
        delaySamples.setCurrentAndTargetValue ((float) (values[0].load() * 0.001 * rate));
    }

    void process (float* l, float* r, int n) noexcept override
    {
        const auto size = buffer.getNumSamples();
        auto* bl = buffer.getWritePointer (0);
        auto* br = buffer.getWritePointer (1);
        const auto feedback = values[1].load(), mix = values[2].load();
        delaySamples.setTargetValue (juce::jlimit (1.0f, (float) (size - 2), (float) (values[0].load() * 0.001 * sampleRate)));

        for (int i = 0; i < n; ++i)
        {
            // Read with linear interpolation, so changing the time glides instead of clicking.
            const auto d = delaySamples.getNextValue();
            auto readPos = (float) writePos - d;
            if (readPos < 0.0f)
                readPos += (float) size;

            const auto i0 = (int) readPos;
            const auto i1 = (i0 + 1) % size;
            const auto frac = readPos - (float) i0;
            const auto dl = bl[i0] + frac * (bl[i1] - bl[i0]);
            const auto dr = br[i0] + frac * (br[i1] - br[i0]);

            bl[writePos] = l[i] + dl * feedback;
            br[writePos] = r[i] + dr * feedback;
            writePos = (writePos + 1) % size;

            l[i] = l[i] * (1.0f - mix) + dl * mix;
            r[i] = r[i] * (1.0f - mix) + dr * mix;
        }
    }

private:
    juce::AudioBuffer<float> buffer;
    int writePos = 0;
    juce::SmoothedValue<float> delaySamples;
};

class ReverbEffect final : public BuiltInEffect
{
public:
    using BuiltInEffect::BuiltInEffect;

    void prepare (double rate, int) override
    {
        sampleRate = preparedRate = rate;
        reverb.setSampleRate (rate);
        reverb.reset();
    }

    void process (float* l, float* r, int n) noexcept override
    {
        juce::Reverb::Parameters p;
        p.roomSize = values[0].load();
        p.damping = values[1].load();
        p.width = values[2].load();
        p.wetLevel = values[3].load();
        p.dryLevel = 1.0f - values[3].load();
        reverb.setParameters (p);
        reverb.processStereo (l, r, n);
    }

private:
    juce::Reverb reverb;
};

class LimiterEffect final : public BuiltInEffect
{
public:
    using BuiltInEffect::BuiltInEffect;

    void prepare (double rate, int) override
    {
        sampleRate = preparedRate = rate;
        gain = 1.0f;
    }

    void process (float* l, float* r, int n) noexcept override
    {
        const auto ceiling = juce::Decibels::decibelsToGain (values[0].load());
        const auto release = (float) (1.0 - std::exp (-1.0 / (0.001 * values[1].load() * sampleRate)));

        for (int i = 0; i < n; ++i)
        {
            // Instant attack, smooth release, and a hard ceiling as a last resort.
            const auto peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const auto target = peak > ceiling ? ceiling / peak : 1.0f;
            gain = target < gain ? target : gain + (target - gain) * release;

            l[i] = juce::jlimit (-ceiling, ceiling, l[i] * gain);
            r[i] = juce::jlimit (-ceiling, ceiling, r[i] * gain);
        }
    }

private:
    float gain = 1.0f;
};

} // namespace

std::unique_ptr<BuiltInEffect> BuiltInEffect::create (const juce::String& type)
{
    const auto* info = findBuiltInEffect (type);
    if (info == nullptr)
        return nullptr;

    if (type == "eq")         return std::make_unique<EqEffect> (*info);
    if (type == "compressor") return std::make_unique<CompressorEffect> (*info);
    if (type == "delay")      return std::make_unique<DelayEffect> (*info);
    if (type == "reverb")     return std::make_unique<ReverbEffect> (*info);
    if (type == "limiter")    return std::make_unique<LimiterEffect> (*info);
    return nullptr;
}

} // namespace riff
