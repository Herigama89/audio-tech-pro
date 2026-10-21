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
            // (Level Compensation: the drive's extra loudness taken back off, so only the colour changes)
            const auto compensate = choice (4) == 1 ? dbToGain (-value (0) * 0.18f) : 1.0f;
            preL.setLowPass (720.0, sampleRate);
            preR.setLowPass (720.0, sampleRate);
            toneL.setLowPass (value (1), sampleRate);
            toneR.setLowPass (value (1), sampleRate);
            const auto out = dbToGain (value (2)) * compensate;
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
            const auto out = dbToGain (value (2)) * (choice (4) == 1 ? dbToGain (-value (0) * 0.22f) : 1.0f);
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
            const auto clip = dbToGain (value (6));   // (Clip Level: where the top is, below full scale)
            const auto limit = [mode, clip] (float x)
            {
                x /= clip;
                return clip * limitUnit (mode, x);
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
        /** Fold (back down from the top), Clip (flat), Wrap (round to the bottom), for a top of 1. */
        static float limitUnit (int mode, float x) noexcept
        {
            if (mode == 0)
            {
                auto t = (x + 1.0f) * 0.25f;
                t -= std::floor (t);
                return 1.0f - 4.0f * std::abs (t - 0.5f);
            }
            if (mode == 2)
                return x - 2.0f * std::floor ((x + 1.0f) * 0.5f);
            return juce::jlimit (-1.0f, 1.0f, x);
        }
    private:
        float counter = 0.0f, holdL = 0.0f, holdR = 0.0f;
    };

    //==============================================================================
    /**
        Clip Distortion (as Logic's): input gain, drive into an asymmetric clip (Symmetry) through its clip
        filter, then a high shelf (frequency and gain) and a low pass, mix and output.
    */
    class ClipDistortion final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        enum Index { iDrive, iSymmetry, iClipFilter, iLpFilter, iMix, iOutput, iInput, iShelfFreq, iShelfGain };
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            gain = dbToGain (value (iDrive));
            symmetry = value (iSymmetry) * 0.01f;
            const auto input = dbToGain (value (iInput));
            // The clip filter: a low pass ahead of the clip (what's clipped is the body, not the fizz)
            preL.setLowPass (value (iClipFilter), sampleRate);
            preR.setLowPass (value (iClipFilter), sampleRate);
            toneL.setLowPass (value (iLpFilter), sampleRate);
            toneR.setLowPass (value (iLpFilter), sampleRate);
            shelfL.setHighShelf (sampleRate, value (iShelfFreq), value (iShelfGain));
            shelfR = shelfL;
            const auto mix = value (iMix) * 0.01f;
            const auto out = dbToGain (value (iOutput));
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
                l[i] = preL.lowPass (l[i] * input);
                r[i] = preR.lowPass (r[i] * input);
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                const auto a = toneL.lowPass (shelfL.process (l[i], 0)), b = toneR.lowPass (shelfR.process (r[i], 1));
                l[i] = dryL[i & 1023] * (1.0f - mix) + a * out * mix;
                r[i] = dryR[i & 1023] * (1.0f - mix) + b * out * mix;
            }
        }
    protected:
        void onPrepare() override { shelfL.reset(); shelfR.reset(); }
        float shape (float x, int) noexcept override
        {
            const auto y = x * gain;
            const auto top = 0.5f, bottom = 0.5f * (1.0f - symmetry);
            return juce::jlimit (-bottom, top, y);
        }
    private:
        float gain = 1.0f, symmetry = 0.0f;
        Biquad shelfL, shelfR;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /**
        Distortion II (as Logic's): pre gain, drive and tone (a high shelf before and after) through one of
        five circuits - Growl (a two-stage tube amp), Bity (an overdriven blues amp), Nasty (hard), Class AB
        soft and hard (transistor crossover distortion) - then output, mix and level compensation.
    */
    class DistortionTwo final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        enum Index { iPreGain, iDrive, iTone, iType, iOutput, iMix, iComp };
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            type = choice (iType);
            gain = dbToGain (value (iPreGain)) * (1.0f + value (iDrive) * 0.01f * 20.0f);
            const auto tone = value (iTone) * 0.01f * 12.0f;
            preShelfL.setHighShelf (sampleRate, 2500.0, tone); preShelfR = preShelfL;
            postShelfL.setHighShelf (sampleRate, 4000.0, tone * 0.5 - 3.0); postShelfR = postShelfL;
            const auto out = dbToGain (value (iOutput)) * (choice (iComp) == 1 ? 1.0f / std::sqrt (gain) : 1.0f);
            const auto mix = value (iMix) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
                l[i] = preShelfL.process (l[i], 0);
                r[i] = preShelfR.process (r[i], 1);
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                const auto a = dcL.highPass (postShelfL.process (l[i], 0)), b = dcR.highPass (postShelfR.process (r[i], 1));
                l[i] = dryL[i & 1023] * (1.0f - mix) + a * out * mix;
                r[i] = dryR[i & 1023] * (1.0f - mix) + b * out * mix;
            }
        }
    protected:
        void onPrepare() override
        {
            dcL.setLowPass (15.0, sampleRate);
            dcR.setLowPass (15.0, sampleRate);
            for (auto* b : { &preShelfL, &preShelfR, &postShelfL, &postShelfR }) b->reset();
        }
        float shape (float x, int) noexcept override
        {
            const auto y = x * gain;
            switch (type)
            {
                case 0:   // Growl: two tube stages, the second biased (even harmonics, a growl when pushed)
                {
                    const auto first = std::tanh (y * 0.8f + 0.1f) - std::tanh (0.1f);
                    return (std::tanh (first * 2.2f - 0.15f) + std::tanh (0.15f)) * 0.55f;
                }
                case 1:   // Bity: a bluesy amp, soft then breaking up
                    return saturate (y * 0.7f + 0.08f * y * y / (1.0f + y * y)) * 0.6f;
                case 2:   // Nasty: hard and square
                    return juce::jlimit (-0.5f, 0.5f, std::tanh (y * 3.0f) * 0.6f);
                default:  // Class AB: the dead zone round zero where neither transistor conducts (soft or hard)
                {
                    const auto dead = type == 3 ? 0.02f : 0.06f;
                    const auto t = std::abs (y) < dead ? 0.0f : std::copysign (std::abs (y) - dead, y);
                    return (type == 3 ? std::tanh (t * 1.5f) : juce::jlimit (-1.0f, 1.0f, t * 1.5f)) * 0.6f;
                }
            }
        }
    private:
        int type = 0;
        float gain = 1.0f;
        Biquad preShelfL, preShelfR, postShelfL, postShelfR;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /**
        Phase Distortion (as Logic's): the input's lows (a resonant low pass at Cutoff) bend a short delay
        on the signal itself, so its own bass moves its phase; Intensity, the delay's reach, Phase Reverse
        (the right channel moves the other way), Monitor (hear just the filter), mix.
    */
    class PhaseDistortion final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iMonitor, iCutoff, iResonance, iIntensity, iReverse, iDelay, iMix };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.012 * rate));
            dr.prepare ((int) (0.012 * rate));
            filter.reset();
        }
        void process (float* l, float* r, int n) noexcept override
        {
            filter.set (value (iCutoff), 0.5 + value (iResonance) * 0.01 * 9.5, sampleRate);
            const auto intensity = value (iIntensity) * 0.01f;
            const auto reach = value (iDelay) * 0.001f * (float) sampleRate;
            const auto reverse = choice (iReverse) == 1;
            const auto monitor = choice (iMonitor) == 1;
            const auto mix = value (iMix) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                float lows, band, high;
                filter.tick (0.5f * (l[i] + r[i]), lows, band, high);
                const auto m = juce::jlimit (-1.0f, 1.0f, lows * intensity * 4.0f);
                dl.push (l[i]);
                dr.push (r[i]);
                const auto a = dl.read (1.0f + reach * 0.5f * (1.0f + m));
                const auto b = dr.read (1.0f + reach * 0.5f * (1.0f + (reverse ? -m : m)));
                if (monitor) { l[i] = lows; r[i] = lows; continue; }
                l[i] = l[i] * (1.0f - mix) + a * mix;
                r[i] = r[i] * (1.0f - mix) + b * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Svf filter;
    };

    //==============================================================================
    /**
        Saturator (as Logic's ChromaGlow): five models - Retro Tube, Modern Tube, Magnetic (tape), Squeeze
        (pushed compression) and Analog Preamp - each with two styles (clean or colourful; soft or hard press),
        drive, Bypass Below (the lows left untouched under a frequency), level in and out, mix, and low and
        high cut filters (slope, resonance, before or after the saturation).
    */
    class Saturator final : public ShaperEffect
    {
    public:
        using ShaperEffect::ShaperEffect;
        enum Index { iModel, iDrive, iStyle, iMix, iOutput, iInput, iBypassOn, iBypassFreq,
                     iLcOn, iLcSlope, iLcFreq, iLcRes, iLcPost, iHcOn, iHcSlope, iHcFreq, iHcRes, iHcPost };
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            model = choice (iModel);
            colourful = choice (iStyle) == 1;
            drive = 1.0f + value (iDrive) * 0.01f * 10.0f;
            const auto input = dbToGain (value (iInput));
            const auto mix = value (iMix) * 0.01f;
            const auto out = dbToGain (value (iOutput));
            const auto bypassBelow = choice (iBypassOn) == 1;
            for (auto* f : { &bypassL, &bypassR }) f->setLowPass (value (iBypassFreq), sampleRate);
            updateCuts();
            const auto lcPost = choice (iLcPost) == 1, hcPost = choice (iHcPost) == 1;
            for (int i = 0; i < n; ++i)
            {
                auto a = l[i] * input, b = r[i] * input;
                dryL[i & 1023] = l[i];
                dryR[i & 1023] = r[i];
                if (! lcPost) { a = cut (lowCut, a, 0); b = cut (lowCut, b, 1); }
                if (! hcPost) { a = cut (highCut, a, 0); b = cut (highCut, b, 1); }
                if (bypassBelow)
                {
                    lowsL[i & 1023] = bypassL.lowPass (a);
                    lowsR[i & 1023] = bypassR.lowPass (b);
                    a -= lowsL[i & 1023];
                    b -= lowsR[i & 1023];
                }
                l[i] = a;
                r[i] = b;
            }
            shapeOversampled (l, r, n);
            for (int i = 0; i < n; ++i)
            {
                auto a = dcL.highPass (l[i]), b = dcR.highPass (r[i]);
                if (bypassBelow) { a += lowsL[i & 1023]; b += lowsR[i & 1023]; }
                if (lcPost) { a = cut (lowCut, a, 0); b = cut (lowCut, b, 1); }
                if (hcPost) { a = cut (highCut, a, 0); b = cut (highCut, b, 1); }
                l[i] = (dryL[i & 1023] * (1.0f - mix) + a * mix) * out;
                r[i] = (dryR[i & 1023] * (1.0f - mix) + b * mix) * out;
            }
        }
    protected:
        void onPrepare() override
        {
            dcL.setLowPass (10.0, sampleRate);
            dcR.setLowPass (10.0, sampleRate);
            for (auto& f : lowCut) f.reset();
            for (auto& f : highCut) f.reset();
            for (auto* f : { &bypassL, &bypassR }) f->reset();
            env = 0.0f;
            lastCutKey = -1.0f;
        }
        float shape (float x, int) noexcept override
        {
            const auto y = x * drive;
            float s;
            switch (model)
            {
                case 0:   // Retro Tube: warm, mostly even harmonics (colourful: more of them)
                    s = saturate (y + (colourful ? 0.35f : 0.18f) * y * y / (1.0f + y * y));
                    break;
                case 1:   // Modern Tube: rich, gently compressing, clearer top
                    s = std::tanh (y * 0.9f) * (1.0f - (colourful ? 0.08f : 0.03f) * std::abs (std::tanh (y))) + (colourful ? 0.06f : 0.0f) * y * y / (1.0f + y * y);
                    break;
                case 2:   // Magnetic: tape - soft, compressed; colourful adds the transformer's grit
                    s = std::tanh (y) * (1.0f - 0.06f * std::abs (std::tanh (y)));
                    if (colourful) s += 0.04f * y * std::exp (-y * y) + 0.05f * s * s;
                    break;
                case 3:   // Squeeze: pushed hard into compression (soft or hard press)
                {
                    const auto k = colourful ? 3.0f : 1.6f;
                    s = y / std::pow (1.0f + std::pow (std::abs (y), k), 1.0f / k);
                    break;
                }
                default:  // Analog Preamp: punchy transistor saturation (clean) or woolly soft clipping (colourful)
                    s = colourful ? saturate (y * 0.8f) + 0.05f * y * y / (1.0f + y * y) : y / (1.0f + std::abs (y) * 0.7f);
                    break;
            }
            return s / std::max (1.0f, std::sqrt (drive));
        }
    private:
        int model = 0;
        bool colourful = false;
        float drive = 1.0f, env = 0.0f, lastCutKey = -1.0f;
        std::array<Biquad, 2> lowCut, highCut;
        OnePole bypassL, bypassR;
        std::array<float, 1024> dryL {}, dryR {}, lowsL {}, lowsR {};

        static float cut (std::array<Biquad, 2>& f, float x, int ch) noexcept { return f[1].process (f[0].process (x, ch), ch); }

        void updateCuts() noexcept
        {
            const auto key = value (iLcOn) * 3.0f + value (iLcSlope) * 7.0f + value (iLcFreq) * 0.01f + value (iLcRes) * 0.13f
                           + value (iHcOn) * 11.0f + value (iHcSlope) * 17.0f + value (iHcFreq) * 0.0003f + value (iHcRes) * 0.19f;
            if (key == lastCutKey) return;
            lastCutKey = key;
            const auto set = [this] (std::array<Biquad, 2>& f, bool on, int slope, float freq, float res, bool high)
            {
                const auto q = 0.707 + res * 0.01 * 6.0;
                if (! on) { f[0].setIdentity(); f[1].setIdentity(); return; }
                // Slopes: 6, 12 or 24 dB per octave
                if (slope == 0) { if (high) f[0].setFirstOrderLowPass (sampleRate, freq); else f[0].setFirstOrderHighPass (sampleRate, freq); f[1].setIdentity(); }
                else if (slope == 1) { if (high) f[0].setLowPass (sampleRate, freq, q); else f[0].setHighPass (sampleRate, freq, q); f[1].setIdentity(); }
                else
                {
                    if (high) { f[0].setLowPass (sampleRate, freq, butterQ1); f[1].setLowPass (sampleRate, freq, butterQ2 * q / 0.707); }
                    else { f[0].setHighPass (sampleRate, freq, butterQ1); f[1].setHighPass (sampleRate, freq, butterQ2 * q / 0.707); }
                }
            };
            set (lowCut, choice (iLcOn) == 1, choice (iLcSlope), value (iLcFreq), value (iLcRes), false);
            set (highCut, choice (iHcOn) == 1, choice (iHcSlope), value (iHcFreq), value (iHcRes), true);
        }
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockDistortionEffects()
{
    const juce::String dB (" dB"), hz (" Hz"), pc (" %"), ms (" ms");
    static const std::vector<StockEffect> effects {
        { { "overdrive", "Overdrive", {
              { "drive", "Drive", dB, 0.0f, 60.0f, 24.0f, 24.0f },
              { "tone", "Tone", hz, 500.0f, 12000.0f, 3500.0f, 3000.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -6.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "levelComp", "Level Compensation", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Distortion" },
          make<Overdrive> },
        { { "distortion", "Distortion", {
              { "drive", "Drive", dB, 0.0f, 60.0f, 30.0f, 30.0f },
              { "tone", "Tone", hz, 500.0f, 12000.0f, 4000.0f, 3000.0f },
              { "output", "Output", dB, -30.0f, 12.0f, -8.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "levelComp", "Level Compensation", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Distortion" },
          make<Distortion> },

        { { "distortion2", "Distortion II", {
              { "preGain", "PreGain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "drive", "Drive", pc, 0.0f, 100.0f, 40.0f, 40.0f },
              { "tone", "Tone", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "type", "Type", "", 0.0f, 4.0f, 0.0f, 2.0f, { "Growl", "Bity", "Nasty", "Class AB Soft", "Class AB Hard" } },
              { "output", "Output Level", dB, -30.0f, 12.0f, -6.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "levelComp", "Level Compensation", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Distortion" },
          make<DistortionTwo> },

        { { "phaseDistortion", "Phase Distortion", {
              { "monitor", "Monitor", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "cutoff", "Cutoff", hz, 20.0f, 2000.0f, 300.0f, 300.0f },
              { "resonance", "Resonance", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "intensity", "Intensity", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "reverse", "Phase Reverse", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "delay", "Delay", ms, 0.1f, 10.0f, 2.0f, 2.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Distortion" },
          make<PhaseDistortion> },
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
              { "mode", "Mode", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Fold", "Clip", "Wrap" } },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "clipLevel", "Clip Level", dB, -24.0f, 0.0f, 0.0f, -6.0f } }, "Distortion" },
          make<Bitcrusher> },
        { { "clipDistortion", "Clip Distortion", {
              { "drive", "Drive", dB, 0.0f, 48.0f, 18.0f, 18.0f },
              { "symmetry", "Symmetry", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "clipFilter", "Clip Filter", hz, 200.0f, 20000.0f, 8000.0f, 3000.0f },
              { "lpFilter", "LP Filter", hz, 500.0f, 20000.0f, 12000.0f, 5000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output Gain", dB, -30.0f, 12.0f, -4.0f, 0.0f },
              { "input", "Input Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "shelfFreq", "High Shelving", hz, 500.0f, 16000.0f, 3000.0f, 3000.0f },
              { "shelfGain", "Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Distortion" },
          make<ClipDistortion> },
        { { "saturator", "Saturator", {
              { "model", "Model", "", 0.0f, 4.0f, 0.0f, 2.0f, { "Retro Tube", "Modern Tube", "Magnetic", "Squeeze", "Analog Preamp" } },
              { "drive", "Drive", pc, 0.0f, 100.0f, 25.0f, 30.0f },
              { "style", "Style", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Clean", "Colorful" } },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Level Out", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "input", "Level In", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "bypassOn", "Bypass Below", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "bypassFreq", "Bypass Below Freq", hz, 20.0f, 500.0f, 80.0f, 100.0f },
              { "lcOn", "Low Cut", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "lcSlope", "Low Cut Slope", "", 0.0f, 2.0f, 1.0f, 1.0f, { "6 dB/Oct", "12 dB/Oct", "24 dB/Oct" } },
              { "lcFreq", "Low Cut Freq", hz, 20.0f, 2000.0f, 40.0f, 150.0f },
              { "lcRes", "Low Cut Resonance", pc, 0.0f, 100.0f, 0.0f, 30.0f },
              { "lcPost", "Low Cut Pre/Post", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Pre", "Post" } },
              { "hcOn", "High Cut", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "hcSlope", "High Cut Slope", "", 0.0f, 2.0f, 1.0f, 1.0f, { "6 dB/Oct", "12 dB/Oct", "24 dB/Oct" } },
              { "hcFreq", "High Cut Freq", hz, 1000.0f, 20000.0f, 16000.0f, 8000.0f },
              { "hcRes", "High Cut Resonance", pc, 0.0f, 100.0f, 0.0f, 30.0f },
              { "hcPost", "High Cut Pre/Post", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Pre", "Post" } } }, "Distortion" },
          make<Saturator> },
    };
    return effects;
}

} // namespace riff
