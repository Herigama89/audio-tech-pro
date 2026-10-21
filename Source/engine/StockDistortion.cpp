#include "StockDsp.h"

/*
    The stock distortions (Logic's Distortion folder): overdrive, distortion, fuzz, bitcrusher, clip
    distortion and a tube/tape saturator. Each drives a shaper between tone filters; the harder ones
    run at twice the sample rate so the harmonics they add don't fold back as aliasing.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** Base: a shaper run at twice the rate (a short half-band filter either side), with tone and mix. */
    class ShaperEffect : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* f : { &upL, &upR, &downL, &downR })
            {
                f->setLowPass (rate * 2.0, rate * 0.45, butterQ1);
                f->reset();
            }
            for (auto* f : { &upL2, &upR2, &downL2, &downR2 })
            {
                f->setLowPass (rate * 2.0, rate * 0.45, butterQ2);
                f->reset();
            }
            for (auto* f : { &preL, &preR, &toneL, &toneR, &dcL, &dcR })
                f->reset();
            onPrepare();
        }

    protected:
        virtual void onPrepare() {}
        virtual float shape (float x, int channel) noexcept = 0;

        /** Runs the shaper on both channels at twice the rate. */
        void shapeOversampled (float* l, float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                l[i] = twice (l[i], 0, upL, upL2, downL, downL2, lastL);
                r[i] = twice (r[i], 1, upR, upR2, downR, downR2, lastR);
            }
        }

        Biquad upL, upR, upL2, upR2, downL, downR, downL2, downR2;
        OnePole preL, preR, toneL, toneR, dcL, dcR;
        float lastL = 0.0f, lastR = 0.0f;

    private:
        float twice (float x, int ch, Biquad& u1, Biquad& u2, Biquad& d1, Biquad& d2, float& last) noexcept
        {
            juce::ignoreUnused (last);
            // Zero-stuffed: x, 0 - filtered back to a smooth signal at twice the rate (gain 2 to make up)
            const auto a = u2.process (u1.process (x * 2.0f, 0), 0);
            const auto b = u2.process (u1.process (0.0f, 0), 0);
            const auto sa = d2.process (d1.process (shape (a, ch), 0), 0);
            d2.process (d1.process (shape (b, ch), 0), 0);
            return sa;
        }
    };

    //==============================================================================
    /** A pedal-style overdrive: mids pushed into a soft, slightly uneven clip, then a tone control. */
    class Overdrive final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            gain = dbToGain (value (0) * 0.4f);
            preL.setLowPass (720.0, sampleRate);
            preR.setLowPass (720.0, sampleRate);
            toneL.setLowPass (value (1), sampleRate);
            toneR.setLowPass (value (1), sampleRate);
            const auto out = dbToGain (value (2));
            const auto mix = value (3) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
                // (the lows are kept clean: only what's above 720 Hz is driven hard)
                const auto lowL = preL.lowPass (l[i]), lowR = preR.lowPass (r[i]);
                l[i] = lowL * 0.4f + (l[i] - lowL);
                r[i] = lowR * 0.4f + (r[i] - lowR);
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                const auto a = toneL.lowPass (l[i]) * out, b = toneR.lowPass (r[i]) * out;
                l[i] = dryL[i & 1023] * (1.0f - mix) + a * mix;
                r[i] = dryR[i & 1023] * (1.0f - mix) + b * mix;
            }
        }
    protected:
        float shape (float x, int) noexcept override
        {
            const auto y = x * gain;
            return saturate (y + 0.15f * y * y / (1.0f + std::abs (y))) * 0.7f;   // (a little even harmonic)
        }
    private:
        float gain = 1.0f;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /** Harder distortion: lots of gain into a sharper clip. */
    class Distortion final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            gain = dbToGain (value (0) * 0.6f);
            toneL.setLowPass (value (1), sampleRate);
            toneR.setLowPass (value (1), sampleRate);
            const auto out = dbToGain (value (2));
            const auto mix = value (3) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                const auto a = toneL.lowPass (l[i]) * out, b = toneR.lowPass (r[i]) * out;
                l[i] = dryL[i & 1023] * (1.0f - mix) + a * mix;
                r[i] = dryR[i & 1023] * (1.0f - mix) + b * mix;
            }
        }
    protected:
        float shape (float x, int) noexcept override
        {
            const auto y = std::tanh (x * gain);
            return std::copysign (std::pow (std::abs (y), 0.7f), y) * 0.6f;
        }
    private:
        float gain = 1.0f;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /** Fuzz: a biased, nearly square clip with a gate for the hiss between notes. */
    class Fuzz final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            gain = dbToGain (value (0) * 0.7f);
            bias = value (1) * 0.01f * 0.4f;
            toneL.setLowPass (value (2), sampleRate);
            toneR.setLowPass (value (2), sampleRate);
            const auto gate = value (3) > -95.0f ? dbToGain (value (3)) : 0.0f;
            const auto out = dbToGain (value (4));
            const auto envCoeff = coeffMs (30.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                env = std::max (std::abs (l[i]), std::abs (r[i])) + envCoeff * (env - std::max (std::abs (l[i]), std::abs (r[i])));
                gateGain[i & 1023] = env > gate ? 1.0f : env / std::max (gate, 1.0e-6f);
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                l[i] = dcL.highPass (toneL.lowPass (l[i])) * out * gateGain[i & 1023];
                r[i] = dcR.highPass (toneR.lowPass (r[i])) * out * gateGain[i & 1023];
            }
        }
    protected:
        void onPrepare() override
        {
            dcL.setLowPass (20.0, sampleRate);
            dcR.setLowPass (20.0, sampleRate);
        }
        float shape (float x, int) noexcept override
        {
            const auto y = x * gain + bias;
            return (std::tanh (y * 3.0f) - std::tanh (bias * 3.0f)) * 0.5f;
        }
    private:
        float gain = 1.0f, bias = 0.0f, env = 0.0f;
        std::array<float, 1024> gateGain {};
    };

    //==============================================================================
    /** Fewer bits and a lower sample rate (aliasing on purpose), with a drive and fold/clip/wrap at the top. */
    class Bitcrusher final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; counter = 0.0f; holdL = holdR = 0.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto drive = dbToGain (value (0));
            const auto bits = juce::jlimit (1.0f, 24.0f, value (1));
            const auto steps = std::pow (2.0f, bits - 1.0f);
            const auto downsample = std::max (1.0f, value (2));
            const auto mode = choice (3);
            const auto mix = value (4) * 0.01f;
            const auto out = dbToGain (value (5));
            const auto limit = [mode] (float x)
            {
                if (mode == 1)   // fold back from the top
                {
                    while (x > 1.0f || x < -1.0f)
                        x = x > 1.0f ? 2.0f - x : -2.0f - x;
                    return x;
                }
                if (mode == 2)   // wrap round
                    return x - 2.0f * std::floor ((x + 1.0f) * 0.5f);
                return juce::jlimit (-1.0f, 1.0f, x);
            };
            for (int i = 0; i < n; ++i)
            {
                counter += 1.0f;
                if (counter >= downsample)
                {
                    counter -= downsample;
                    holdL = std::round (limit (l[i] * drive) * steps) / steps;
                    holdR = std::round (limit (r[i] * drive) * steps) / steps;
                }
                l[i] = (l[i] * (1.0f - mix) + holdL * mix) * out;
                r[i] = (r[i] * (1.0f - mix) + holdR * mix) * out;
            }
        }
    private:
        float counter = 0.0f, holdL = 0.0f, holdR = 0.0f;
    };

    //==============================================================================
    /** Clip Distortion: drive into a hard clip, with a filter before (what's clipped) and after. */
    class ClipDistortion final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            gain = dbToGain (value (0));
            symmetry = value (1) * 0.01f;
            preL.setLowPass (value (2), sampleRate);
            preR.setLowPass (value (2), sampleRate);
            toneL.setLowPass (value (3), sampleRate);
            toneR.setLowPass (value (3), sampleRate);
            const auto mix = value (4) * 0.01f;
            const auto out = dbToGain (value (5));
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
                l[i] = preL.highPass (l[i]);
                r[i] = preR.highPass (r[i]);
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                l[i] = dryL[i & 1023] * (1.0f - mix) + toneL.lowPass (l[i]) * out * mix;
                r[i] = dryR[i & 1023] * (1.0f - mix) + toneR.lowPass (r[i]) * out * mix;
            }
        }
    protected:
        float shape (float x, int) noexcept override
        {
            const auto y = x * gain;
            const auto top = 0.5f, bottom = 0.5f * (1.0f - symmetry);
            return juce::jlimit (-bottom, top, y);
        }
    private:
        float gain = 1.0f, symmetry = 0.0f;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /** Warmth: four saturating circuits (two tubes, tape, transformer), gentle to heavy. */
    class Saturator final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            model = choice (0);
            drive = 1.0f + value (1) * 0.01f * 8.0f;
            toneL.setLowPass (value (2), sampleRate);
            toneR.setLowPass (value (2), sampleRate);
            const auto mix = value (3) * 0.01f;
            const auto out = dbToGain (value (4));
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                const auto a = dcL.highPass (toneL.lowPass (l[i])), b = dcR.highPass (toneR.lowPass (r[i]));
                l[i] = (dryL[i & 1023] * (1.0f - mix) + a * mix) * out;
                r[i] = (dryR[i & 1023] * (1.0f - mix) + b * mix) * out;
            }
        }
    protected:
        void onPrepare() override
        {
            dcL.setLowPass (10.0, sampleRate);
            dcR.setLowPass (10.0, sampleRate);
        }
        float shape (float x, int) noexcept override
        {
            const auto y = x * drive;
            float s;
            switch (model)
            {
                case 1:  s = saturate (y + 0.1f * y * y) * 0.95f; break;                 // bright tube: some even harmonics, keeps the top
                case 2:  s = std::tanh (y) * (1.0f - 0.05f * std::abs (std::tanh (y))); break;   // tape: soft, compresses
                case 3:  s = y / (1.0f + std::abs (y)) + 0.05f * y * std::exp (-y * y); break;   // transformer: odd, a little grit
                default: s = saturate (y + 0.2f * y * y / (1.0f + y * y)); break;           // warm tube: mostly even harmonics
            }
            return s / std::max (1.0f, std::sqrt (drive));
        }
    private:
        int model = 0;
        float drive = 1.0f;
        std::array<float, 1024> dryL {}, dryR {};
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockDistortionEffects()
{
    const juce::String dB (" dB"), hz (" Hz"), pc (" %");
    static const std::vector<StockEffect> effects {
        { { "overdrive", "Overdrive", {
              { "drive", "Drive", dB, 0.0f, 60.0f, 24.0f, 24.0f },
              { "tone", "Tone", hz, 500.0f, 12000.0f, 3500.0f, 3000.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -6.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Distortion" },
          make<Overdrive> },
        { { "distortion", "Distortion", {
              { "drive", "Drive", dB, 0.0f, 60.0f, 30.0f, 30.0f },
              { "tone", "Tone", hz, 500.0f, 12000.0f, 4000.0f, 3000.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -8.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Distortion" },
          make<Distortion> },
        { { "fuzz", "Fuzz", {
              { "fuzz", "Fuzz", dB, 0.0f, 60.0f, 36.0f, 30.0f },
              { "bias", "Bias", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "tone", "Tone", hz, 500.0f, 12000.0f, 3000.0f, 3000.0f },
              { "gate", "Gate", dB, -100.0f, -20.0f, -70.0f, -60.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -8.0f, 0.0f } }, "Distortion" },
          make<Fuzz> },
        { { "bitcrusher", "Bitcrusher", {
              { "drive", "Drive", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "bits", "Resolution", " bit", 1.0f, 24.0f, 8.0f, 8.0f },
              { "downsample", "Downsampling", "x", 1.0f, 50.0f, 4.0f, 6.0f },
              { "mode", "Clip Mode", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Clip", "Fold", "Wrap" } },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Distortion" },
          make<Bitcrusher> },
        { { "clipDistortion", "Clip Distortion", {
              { "drive", "Drive", dB, 0.0f, 48.0f, 18.0f, 18.0f },
              { "symmetry", "Symmetry", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "preHp", "Pre High Pass", hz, 10.0f, 2000.0f, 80.0f, 200.0f },
              { "postLp", "Post Low Pass", hz, 500.0f, 20000.0f, 8000.0f, 5000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -4.0f, 0.0f } }, "Distortion" },
          make<ClipDistortion> },
        { { "saturator", "Saturator", {
              { "model", "Model", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Warm Tube", "Bright Tube", "Tape", "Transformer" } },
              { "drive", "Drive", pc, 0.0f, 100.0f, 25.0f, 30.0f },
              { "tone", "Tone", hz, 1000.0f, 20000.0f, 16000.0f, 8000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Distortion" },
          make<Saturator> },
    };
    return effects;
}

} // namespace riff
