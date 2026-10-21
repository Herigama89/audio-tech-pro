#include "StockDsp.h"

/*
    The stock dynamics (Logic's Dynamics folder): a compressor with seven circuit models, a three-band
    multiband compressor, de-esser, noise gate, expander, transient shaper and a look-ahead brickwall
    limiter. Every one reports its gain reduction to the strip's meter.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** Base: the strip's gain reduction meter, and the side chain when one is routed in. */
    class DynamicsBase : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        float takeGainReductionDb() noexcept override { return meter.exchange (0.0f); }
        float displayValue() const noexcept override { return display.load (std::memory_order_relaxed); }
        void setSidechain (const float* left, const float* right) noexcept override { keyL = left; keyR = right != nullptr ? right : left; }
        bool acceptsSidechain() const override { return true; }

    protected:
        void report (float reductionDb) noexcept
        {
            peak = std::max (peak, reductionDb);
        }
        void publish() noexcept
        {
            meter.store (std::max (meter.load (std::memory_order_relaxed), peak), std::memory_order_relaxed);
            display.store (peak, std::memory_order_relaxed);
            peak = 0.0f;
        }
        const float* keyL = nullptr;
        const float* keyR = nullptr;

    private:
        std::atomic<float> meter { 0.0f }, display { 0.0f };
        float peak = 0.0f;
    };

    //==============================================================================
    /**
        The studio compressor (as Logic's Compressor): seven circuits, each with its own detector, timing,
        knee, topology and colour, and the controls each circuit has (Vintage VCA's stepped ratios and times,
        Vintage FET's ratio buttons, the opto's fixed attack and two-stage release, the classic VCA's
        program-dependent timing). Side chain: Max or Sum, Peak or RMS, a filter (LP, BP, HP, ParEQ, HS)
        with Listen. Output: a limiter with its threshold, Soft/Hard/Clip distortion, Mix.
    */
    class StudioCompressor final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Model { platinum, studioVca, studioFet, classicVca, vintageVca, vintageFet, vintageOpto };
        enum Index { iModel, iThreshold, iRatio, iAttack, iRelease, iKnee, iMakeup, iAutoGain, iScFreq, iMix, iDistortion, iOutput,
                     iLimiter, iLimThr, iInput, iAutoRelease, iDetect, iPeakRms, iScFilter, iScMode, iScQ, iScGain, iListen };

        /** What a circuit makes of the Ratio, Attack and Release knobs (the vintage ones step). */
        static float circuitRatio (int model, float ratio) noexcept
        {
            if (model == vintageVca)  return ratio < 3.0f ? 2.0f : ratio < 7.0f ? 4.0f : 10.0f;
            if (model == vintageFet)  return ratio < 6.0f ? 4.0f : ratio < 10.0f ? 8.0f : ratio < 16.0f ? 12.0f : ratio < 25.0f ? 20.0f : 30.0f;   // (30: all buttons in)
            if (model == vintageOpto) return 1.0f + (ratio - 1.0f) * 0.75f;
            return ratio;
        }
        static float circuitAttack (int model, float ms) noexcept
        {
            if (model == vintageVca)
            {
                for (const auto step : { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f })
                    if (ms < step * 1.7f) return step;
                return 30.0f;
            }
            if (model == vintageFet)  return juce::jmap (std::sqrt (juce::jlimit (0.0f, 200.0f, ms) / 200.0f), 0.02f, 0.8f);   // 20 µs to 800 µs
            if (model == vintageOpto) return 10.0f;
            return ms;
        }
        static float circuitRelease (int model, float ms) noexcept
        {
            if (model == vintageVca)
            {
                for (const auto step : { 100.0f, 300.0f, 600.0f })
                    if (ms < step * 1.4f) return step;
                return 1200.0f;
            }
            if (model == vintageFet)  return juce::jmap (std::sqrt (juce::jlimit (5.0f, 3000.0f, ms) / 3000.0f), 50.0f, 1100.0f);
            return ms;
        }
        /** Which controls the circuit has (the others are greyed out in its window). */
        static bool circuitHas (int model, const juce::String& id) noexcept
        {
            if (id == "knee")       return model == platinum || model == studioVca || model == studioFet;
            if (id == "attack")     return model != vintageOpto && model != classicVca;
            if (id == "release")    return model != classicVca;
            if (id == "peakRms")    return model == platinum;
            if (id == "autoRelease") return model != classicVca && model != vintageOpto;
            return true;
        }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            envDb = rms = slowDb = 0.0f;
            heldDb = 0.0f;
            lastGain = 1.0f;
            scFilter.reset();
            dcL = dcR = 0.0f;
            lastScKey = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto model = (Model) juce::jlimit (0, 6, choice (iModel));
            const auto threshold = value (iThreshold);
            const auto ratio = circuitRatio (model, std::max (1.0f, value (iRatio)));
            const auto attackMs = circuitAttack (model, value (iAttack));
            const auto releaseMs = circuitRelease (model, value (iRelease));
            const auto autoRelease = choice (iAutoRelease) == 1 && circuitHas (model, "autoRelease");
            const auto input = dbToGain (value (iInput));
            const auto mix = value (iMix) * 0.01f;
            const auto distortion = choice (iDistortion);
            const auto output = dbToGain (value (iOutput));
            const auto limit = choice (iLimiter) == 1;
            const auto limitGain = dbToGain (value (iLimThr));
            const auto sum = choice (iDetect) == 1;
            const auto filterOn = choice (iScFilter) == 1;
            const auto listen = choice (iListen) == 1;

            // Each circuit's detector, knee, topology and colour
            bool rmsDetect = false, feedback = false;
            float knee = value (iKnee) * 18.0f;
            switch (model)
            {
                case platinum:    rmsDetect = choice (iPeakRms) == 1; break;
                case studioVca:   rmsDetect = true; break;
                case studioFet:   feedback = true; break;
                case classicVca:  rmsDetect = true; knee = 6.0f; break;        // (its "over easy" knee)
                case vintageVca:  rmsDetect = true; feedback = true; knee = 3.0f; break;
                case vintageFet:  feedback = true; knee = ratio >= 30.0f ? 0.0f : 2.0f; break;
                case vintageOpto: rmsDetect = true; feedback = true; knee = 14.0f; break;
            }
            const auto attack = coeffMs (attackMs, sampleRate);
            const auto rmsCoeff = coeffMs (model == vintageOpto ? 20.0 : model == classicVca ? 15.0 : 8.0, sampleRate);

            // Auto gain: 0 dB brings the compressed level back to where 0 dB in would sit; -12 dB to -12 dB
            auto makeup = value (iMakeup);
            const auto autoGain = choice (iAutoGain);
            if (autoGain > 0)
            {
                const auto reference = autoGain == 1 ? 0.0f : -12.0f;
                makeup += -compressorGainDb (reference, threshold, ratio, knee) * (autoGain == 1 ? 0.5f : 1.0f);
            }
            updateKeyFilter();

            for (int i = 0; i < n; ++i)
            {
                const auto inL = l[i] * input, inR = r[i] * input;
                const auto external = keyL != nullptr;
                auto kl = external ? keyL[i] : inL, kr = external ? keyR[i] : inR;
                // Feedback circuits hear their own output (unless a side chain is routed in)
                if (feedback && ! external)
                {
                    kl *= lastGain;
                    kr *= lastGain;
                }
                if (filterOn)
                {
                    kl = scFilter.process (kl, 0) * scLevel;
                    kr = scFilter.process (kr, 1) * scLevel;
                }
                if (listen)
                {
                    l[i] = kl;
                    r[i] = kr;
                    continue;
                }
                // Max: the louder channel decides; Sum: both together
                const auto key = sum ? 0.5f * (std::abs (kl) + std::abs (kr)) : std::max (std::abs (kl), std::abs (kr));
                float levelDb;
                if (rmsDetect)
                {
                    rms = key * key + rmsCoeff * (rms - key * key);
                    levelDb = 10.0f * std::log10 (std::max (rms * 2.0f, 1.0e-12f));
                }
                else
                    levelDb = gainToDb (key);

                auto target = -compressorGainDb (levelDb, threshold, ratio, knee);
                // All-buttons-in: the FET's ratio overshoots, a harder bite on loud peaks
                if (model == vintageFet && ratio >= 30.0f && levelDb > threshold)
                    target *= 1.15f;

                // Release: auto release follows the material (fast on transients, slow on sustained);
                // the opto's two-stage release (quick at first, slower the longer it's been working)
                auto rel = releaseMs;
                if (model == vintageOpto)
                {
                    heldDb = std::max (heldDb * 0.99997f, envDb);
                    rel = envDb > heldDb * 0.5f ? 60.0f : releaseMs * (1.0f + juce::jlimit (0.0f, 10.0f, heldDb * 0.4f));
                }
                if (autoRelease)
                {
                    slowDb = target + coeffMs (releaseMs * 4.0, sampleRate) * (slowDb - target);
                    rel = (envDb - slowDb) > 3.0f ? releaseMs * 0.25f : releaseMs * 1.5f;
                }
                float att = attack;
                if (model == classicVca)
                {
                    // Program-dependent: the further over, the faster it acts; releases at a steady 120 dB a second
                    att = coeffMs (juce::jlimit (3.0f, 30.0f, 15.0f * 10.0f / std::max (1.0f, target - envDb)), sampleRate);
                    rel = juce::jmax (8.0f, envDb / 120.0f * 1000.0f);
                }
                const auto coeff = target > envDb ? att : coeffMs (rel, sampleRate);
                envDb = target + coeff * (envDb - target);
                report (envDb);

                const auto gain = dbToGain (-envDb);
                lastGain = gain;
                const auto g = gain * dbToGain (makeup);
                auto a = colour (model, inL * g, envDb, dcL);
                auto b = colour (model, inR * g, envDb, dcR);
                switch (distortion)
                {
                    case 1: a = softClip (a); b = softClip (b); break;
                    case 2: a = hardClip (a); b = hardClip (b); break;
                    case 3: a = juce::jlimit (-1.0f, 1.0f, a); b = juce::jlimit (-1.0f, 1.0f, b); break;
                    default: break;
                }
                a = inL + mix * (a - inL);
                b = inR + mix * (b - inR);
                a *= output;
                b *= output;
                if (limit)
                {
                    a = limiter (a, limitGain);
                    b = limiter (b, limitGain);
                }
                l[i] = a;
                r[i] = b;
            }
            publish();
        }

    private:
        float envDb = 0.0f, rms = 0.0f, heldDb = 0.0f, slowDb = 0.0f, lastGain = 1.0f;
        float dcL = 0.0f, dcR = 0.0f;
        float lastScKey = -1.0f, scLevel = 1.0f;
        Biquad scFilter;

        void updateKeyFilter() noexcept
        {
            const auto mode = choice (iScMode);
            const auto freq = juce::jlimit (20.0f, 20000.0f, value (iScFreq)), q = std::max (0.1f, value (iScQ)), gain = value (iScGain);
            const auto key = (float) mode * 1.0e7f + freq * 10.0f + q * 1000.0f + gain;
            if (key == lastScKey)
                return;
            lastScKey = key;
            const auto f = std::min ((double) freq, sampleRate * 0.45);
            scLevel = 1.0f;
            switch (mode)
            {
                case 0:  scFilter.setLowPass (sampleRate, f, q); scLevel = dbToGain (gain); break;
                case 1:  scFilter.setBandPass (sampleRate, f, q); scLevel = dbToGain (gain); break;
                case 2:  scFilter.setHighPass (sampleRate, f, q); scLevel = dbToGain (gain); break;
                case 3:  scFilter.setPeak (sampleRate, f, gain, q); break;
                default: scFilter.setHighShelf (sampleRate, f, gain, q); break;
            }
        }

        /**
            Each circuit's colour, more as it works harder:
              Platinum Digital  none
              Studio VCA        a trace of odd harmonics
              Studio FET        even-leaning, a forward midrange edge
              Classic VCA       clean, slightly firm odd harmonics when pushed
              Vintage VCA       a little odd-order "glue"
              Vintage FET       crunchy, asymmetric, the most colour of all
              Vintage Opto      warm tube-like, mostly second harmonic
        */
        static float colour (int model, float x, float workingDb, float& dc) noexcept
        {
            const auto work = 1.0f + juce::jlimit (0.0f, 20.0f, workingDb) * 0.06f;
            float y = x;
            switch (model)
            {
                case studioVca:   y = x + 0.015f * work * (std::tanh (x * 1.5f) / 1.5f - x) * 40.0f * std::abs (x); break;
                case studioFet:   y = x + 0.06f * work * (x * std::abs (x)) + 0.03f * work * (x * x); break;
                case classicVca:  y = x + 0.03f * work * (std::tanh (x * 2.0f) * 0.5f - x); break;
                case vintageVca:  y = x + 0.05f * work * (std::tanh (x * 1.8f) / 1.8f - x) * 2.0f; break;
                case vintageFet:
                {
                    const auto d = 1.0f + 0.25f * work;
                    y = std::tanh (d * x + 0.12f * work * x * x) / d;
                    break;
                }
                case vintageOpto: y = x + 0.08f * work * x * x - 0.02f * work * x * x * x; break;
                default: return x;
            }
            // (even harmonics push a little DC: a slow high pass takes it out, as the transformers would)
            dc += 0.0005f * (y - dc);
            return y - dc;
        }
        /** Soft: rounds the signal off as it nears 0 dB. */
        static float softClip (float x) noexcept
        {
            const auto ax = std::abs (x);
            if (ax < 0.5f) return x;
            return std::copysign (0.5f + 0.5f * std::tanh ((ax - 0.5f) * 2.0f), x);
        }
        /** Hard: a transistor stage, flat until near 0 dB, then abruptly limited. */
        static float hardClip (float x) noexcept
        {
            const auto ax = std::abs (x);
            if (ax < 0.85f) return x;
            return std::copysign (0.85f + 0.15f * std::tanh ((ax - 0.85f) / 0.15f), x);
        }
        /** The output limiter: transparent below its threshold, a firm soft knee over it. */
        static float limiter (float x, float ceiling) noexcept
        {
            const auto knee = ceiling * 0.85f, ax = std::abs (x);
            if (ax <= knee) return x;
            return std::copysign (knee + (ceiling - knee) * std::tanh ((ax - knee) / (ceiling - knee)), x);
        }
    };

    //==============================================================================
    /** Three bands, each compressed on its own, split by phase-matched crossovers. */
    class MultibandCompressor final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* c : { &low, &high, &lowAlign })
                c->reset();
            env.fill (0.0f);
            lastX1 = lastX2 = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto x1 = value (0), x2 = std::max (value (1), value (0) * 1.5f);
            if (x1 != lastX1 || x2 != lastX2)
            {
                low.set (x1, sampleRate);
                high.set (x2, sampleRate);
                lowAlign.set (x2, sampleRate);
                lastX1 = x1;
                lastX2 = x2;
            }
            const auto attack = coeffMs (value (8), sampleRate), release = coeffMs (value (9), sampleRate);
            const float thr[3] { value (2), value (4), value (6) }, ratio[3] { value (3), value (5), value (7) };
            const float gain[3] { value (10), value (11), value (12) };
            const auto output = dbToGain (value (13));

            for (int i = 0; i < n; ++i)
            {
                float bandsL[3], bandsR[3], restL, restR;
                low.split (l[i], 0, bandsL[0], restL);
                low.split (r[i], 1, bandsR[0], restR);
                high.split (restL, 0, bandsL[1], bandsL[2]);
                high.split (restR, 1, bandsR[1], bandsR[2]);
                bandsL[0] = lowAlign.allPass (bandsL[0], 0);
                bandsR[0] = lowAlign.allPass (bandsR[0], 1);

                float outL = 0.0f, outR = 0.0f, most = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const auto levelDb = gainToDb (std::max (std::abs (bandsL[b]), std::abs (bandsR[b])));
                    const auto target = -compressorGainDb (levelDb, thr[b], ratio[b], 6.0f);
                    auto& e = env[(size_t) b];
                    e = target + (target > e ? attack : release) * (e - target);
                    most = std::max (most, e);
                    const auto g = dbToGain (gain[b] - e);
                    outL += bandsL[b] * g;
                    outR += bandsR[b] * g;
                }
                report (most);
                l[i] = outL * output;
                r[i] = outR * output;
            }
            publish();
        }

    private:
        Crossover low, high, lowAlign;
        std::array<float, 3> env {};
        float lastX1 = -1.0f, lastX2 = -1.0f;
    };

    //==============================================================================
    /** Turns sibilance down: just the highs (split band) or the whole voice (wideband). */
    class DeEsser final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            split.reset();
            detector.reset();
            env = 0.0f;
            lastFreq = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto freq = value (0);
            if (freq != lastFreq)
            {
                split.set (freq, sampleRate);
                detector.setHighPass (sampleRate, freq * 0.9, 0.8);
                lastFreq = freq;
            }
            const auto threshold = value (1), range = value (2);
            const bool wideband = choice (3) == 1, listen = choice (4) == 1;
            const auto attack = coeffMs (0.5, sampleRate), release = coeffMs (60.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto key = detector.process (0.5f * (l[i] + r[i]), 0);
                const auto levelDb = gainToDb (std::abs (key));
                const auto target = std::min (range, std::max (0.0f, (levelDb - threshold) * 0.8f));
                env = target + (target > env ? attack : release) * (env - target);
                report (env);
                const auto g = dbToGain (-env);

                float lowL, highL, lowR, highR;
                split.split (l[i], 0, lowL, highL);
                split.split (r[i], 1, lowR, highR);
                if (listen)
                {
                    l[i] = highL;
                    r[i] = highR;
                }
                else if (wideband)
                {
                    l[i] = (lowL + highL) * g;
                    r[i] = (lowR + highR) * g;
                }
                else
                {
                    l[i] = lowL + highL * g;
                    r[i] = lowR + highR * g;
                }
            }
            publish();
        }

    private:
        Crossover split;
        Biquad detector;
        float env = 0.0f, lastFreq = -1.0f;
    };

    //==============================================================================
    /** Opens above the threshold, holds, then closes down to the range (with hysteresis, and key filters). */
    class NoiseGate final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            gain = 0.0f;
            holdLeft = 0;
            open = false;
            lastHp = lastLp = -1.0f;
            hp.reset();
            lp.reset();
            env = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = value (0), floor = dbToGain (value (1));
            const auto attack = coeffMs (value (2), sampleRate), release = coeffMs (value (4), sampleRate);
            const auto hold = (int) (value (3) * 0.001 * sampleRate);
            const auto hysteresis = value (5);
            if (value (6) != lastHp) { hp.setHighPass (sampleRate, std::max (20.0f, value (6)), 0.707); lastHp = value (6); }
            if (value (7) != lastLp) { lp.setLowPass (sampleRate, std::min ((float) (sampleRate * 0.45), value (7)), 0.707); lastLp = value (7); }
            const auto envRelease = coeffMs (5.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                const auto key = std::abs (lp.process (hp.process (0.5f * (kl + kr), 0), 0));
                env = key > env ? key : key + envRelease * (env - key);
                const auto levelDb = gainToDb (env);
                if (levelDb > threshold)
                {
                    open = true;
                    holdLeft = hold;
                }
                else if (open && levelDb < threshold - hysteresis)
                {
                    if (holdLeft > 0)
                        --holdLeft;
                    else
                        open = false;
                }
                const auto target = open ? 1.0f : floor;
                gain = target + (target > gain ? attack : release) * (gain - target);
                report (-gainToDb (std::max (gain, 1.0e-5f)));
                l[i] *= gain;
                r[i] *= gain;
            }
            publish();
        }

    private:
        Biquad hp, lp;
        float gain = 0.0f, env = 0.0f, lastHp = -1.0f, lastLp = -1.0f;
        int holdLeft = 0;
        bool open = false;
    };

    //==============================================================================
    /** Downward expansion: quiet parts made quieter, by the ratio, down to the range. */
    class Expander final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            rms = 0.0f;
            envDb = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = value (0), ratio = std::max (1.0f, value (1));
            const auto attack = coeffMs (value (2), sampleRate), release = coeffMs (value (3), sampleRate);
            const auto knee = value (4), range = -value (5);
            const auto output = dbToGain (value (6));
            const auto rmsCoeff = coeffMs (5.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                const auto x = 0.5f * (kl + kr);
                rms = x * x + rmsCoeff * (rms - x * x);
                const auto levelDb = 10.0f * std::log10 (std::max (rms * 2.0f, 1.0e-12f));
                auto under = threshold - levelDb;
                if (knee > 0.01f && std::abs (under) < knee * 0.5f)
                    under = (under + knee * 0.5f) * (under + knee * 0.5f) / (2.0f * knee);
                else if (under < 0.0f)
                    under = 0.0f;
                const auto target = std::min (range, under * (ratio - 1.0f));
                // (opening is the attack here: the reduction falls quickly when the sound comes back)
                envDb = target + (target < envDb ? attack : release) * (envDb - target);
                report (envDb);
                const auto g = dbToGain (-envDb) * output;
                l[i] *= g;
                r[i] *= g;
            }
            publish();
        }

    private:
        float rms = 0.0f, envDb = 0.0f;
    };

    //==============================================================================
    /** More or less attack and sustain, whatever the level (two envelope followers, compared). */
    class TransientShaper final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            fast = slow = fastTail = slowTail = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto attackAmount = value (0) * 0.01f, sustainAmount = value (1) * 0.01f;
            const auto quick = coeffMs (0.3, sampleRate), slowAttack = coeffMs (value (2), sampleRate);
            const auto releaseFast = coeffMs (20.0, sampleRate), releaseSlow = coeffMs (value (3), sampleRate);
            const auto output = dbToGain (value (4));

            for (int i = 0; i < n; ++i)
            {
                const auto x = std::max (std::abs (l[i]), std::abs (r[i]));
                // Attack: a fast follower ahead of a slow one is a hit starting
                fast = x + (x > fast ? quick : releaseSlow) * (fast - x);
                slow = x + (x > slow ? slowAttack : releaseSlow) * (slow - x);
                // Sustain: a slow-releasing follower above a fast-releasing one is a tail ringing on
                fastTail = x + (x > fastTail ? quick : releaseFast) * (fastTail - x);
                slowTail = x + (x > slowTail ? quick : releaseSlow) * (slowTail - x);
                const auto hit = std::max (0.0f, gainToDb (fast) - gainToDb (slow));
                const auto tail = std::max (0.0f, gainToDb (slowTail) - gainToDb (fastTail));
                const auto db = juce::jlimit (-24.0f, 18.0f, attackAmount * hit * 1.5f + sustainAmount * tail);
                const auto g = dbToGain (db) * output;
                l[i] *= g;
                r[i] *= g;
            }
        }

    private:
        float fast = 0.0f, slow = 0.0f, fastTail = 0.0f, slowTail = 0.0f;
    };

    //==============================================================================
    /** Looks ahead 2 ms so peaks are caught before they arrive: nothing goes over the ceiling. */
    class BrickwallLimiter final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            look = std::max (1, (int) std::round (0.002 * rate));
            delayL.prepare (look + 2);
            delayR.prepare (look + 2);
            window.assign ((size_t) (look + 2), {});
            head = tail = 0;
            counter = 0;
            gain = 1.0f;
        }

        int getLatencySamples() const override { return look; }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto input = dbToGain (value (0)), ceiling = dbToGain (value (1));
            const auto release = coeffMs (value (2), sampleRate);
            const bool warm = choice (3) == 1;
            const auto attack = coeffMs (1000.0 * look / sampleRate / 4.0, sampleRate);
            const auto size = (int) window.size();

            for (int i = 0; i < n; ++i)
            {
                auto a = l[i] * input, b = r[i] * input;
                if (warm)
                {
                    a = saturate (a / ceiling) * ceiling * 1.02f;
                    b = saturate (b / ceiling) * ceiling * 1.02f;
                }
                // The gain this sample needs, and the least needed over the next `look` samples (a sliding minimum)
                const auto peak = std::max (std::abs (a), std::abs (b));
                const auto need = peak > ceiling ? ceiling / peak : 1.0f;
                while (head != tail && window[(size_t) ((tail - 1 + size) % size)].need >= need)
                    tail = (tail - 1 + size) % size;
                window[(size_t) tail] = { counter, need };
                tail = (tail + 1) % size;
                while (window[(size_t) head].index <= counter - look - 1)
                    head = (head + 1) % size;
                const auto target = window[(size_t) head].need;
                ++counter;

                gain = target < gain ? target + attack * (gain - target) : target + release * (gain - target);
                gain = std::min (gain, 1.0f);

                const auto dl = delayL.readInt (look), dr = delayR.readInt (look);
                delayL.push (a);
                delayR.push (b);
                l[i] = juce::jlimit (-ceiling, ceiling, dl * gain);
                r[i] = juce::jlimit (-ceiling, ceiling, dr * gain);
                report (-gainToDb (gain));
            }
            publish();
        }

    private:
        struct Slot { juce::int64 index = 0; float need = 1.0f; };
        DelayLine delayL, delayR;
        std::vector<Slot> window;
        int head = 0, tail = 0, look = 96;
        juce::int64 counter = 0;
        float gain = 1.0f;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockDynamicsEffects()
{
    const juce::String dB (" dB"), ms (" ms"), hz (" Hz"), pc (" %");
    static const std::vector<StockEffect> effects {
        { { "studioComp", "Studio Compressor", {
              { "model", "Circuit", "", 0.0f, 6.0f, 1.0f, 3.0f,
                { "Platinum Digital", "Studio VCA", "Studio FET", "Classic VCA", "Vintage VCA", "Vintage FET", "Vintage Opto" } },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "ratio", "Ratio", ":1", 1.0f, 30.0f, 4.0f, 4.0f },
              { "attack", "Attack", ms, 0.05f, 200.0f, 10.0f, 10.0f },
              { "release", "Release", ms, 5.0f, 3000.0f, 150.0f, 150.0f },
              { "knee", "Knee", "", 0.0f, 1.0f, 0.3f, 0.5f },
              { "makeup", "Make Up", dB, -30.0f, 30.0f, 0.0f, 0.0f },
              { "autoGain", "Auto Gain", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Off", "0 dB", "-12 dB" } },
              { "scHpf", "Filter Freq", hz, 20.0f, 20000.0f, 1000.0f, 1000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "distortion", "Distortion", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Off", "Soft", "Hard", "Clip" } },
              { "output", "Output Gain", dB, -30.0f, 30.0f, 0.0f, 0.0f },
              { "limiter", "Limiter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "limThr", "Limiter Threshold", dB, -20.0f, 0.0f, 0.0f, -6.0f },
              { "input", "Input Gain", dB, -30.0f, 30.0f, 0.0f, 0.0f },
              { "autoRelease", "Auto Release", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "detect", "Detection", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Max", "Sum" } },
              { "peakRms", "Peak/RMS", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Peak", "RMS" } },
              { "scFilter", "Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "scMode", "Filter Mode", "", 0.0f, 4.0f, 2.0f, 2.0f, { "LP", "BP", "HP", "ParEQ", "HS" } },
              { "scQ", "Q", "", 0.1f, 10.0f, 0.7f, 1.0f },
              { "scGain", "Filter Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "listen", "Listen", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Dynamics" },
          make<StudioCompressor> },

        { { "multiband", "Multiband Compressor", {
              { "lowX", "Low/Mid", hz, 40.0f, 1000.0f, 200.0f, 200.0f },
              { "highX", "Mid/High", hz, 1000.0f, 12000.0f, 3000.0f, 3000.0f },
              { "lowThr", "Low Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "lowRatio", "Low Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "midThr", "Mid Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "midRatio", "Mid Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "highThr", "High Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "highRatio", "High Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "attack", "Attack", ms, 0.1f, 200.0f, 20.0f, 20.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 200.0f, 200.0f },
              { "lowGain", "Low Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "midGain", "Mid Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "highGain", "High Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<MultibandCompressor> },

        { { "deesser", "DeEsser", {
              { "freq", "Frequency", hz, 2000.0f, 12000.0f, 6000.0f, 6000.0f },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -30.0f, -30.0f },
              { "range", "Reduction", dB, 0.0f, 24.0f, 8.0f, 8.0f },
              { "mode", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Split Band", "Wideband" } },
              { "listen", "Listen", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "Sibilance" } } }, "Dynamics" },
          make<DeEsser> },

        { { "noiseGate", "Noise Gate", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "range", "Reduction", dB, -100.0f, 0.0f, -80.0f, -40.0f },
              { "attack", "Attack", ms, 0.01f, 100.0f, 0.5f, 5.0f },
              { "hold", "Hold", ms, 0.0f, 500.0f, 20.0f, 50.0f },
              { "release", "Release", ms, 5.0f, 2000.0f, 100.0f, 150.0f },
              { "hysteresis", "Hysteresis", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "hp", "Key High Cut", hz, 20.0f, 2000.0f, 20.0f, 200.0f },
              { "lp", "Key Low Cut", hz, 500.0f, 20000.0f, 20000.0f, 5000.0f } }, "Dynamics" },
          make<NoiseGate> },

        { { "expander", "Expander", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "ratio", "Ratio", ":1", 1.0f, 10.0f, 2.0f, 3.0f },
              { "attack", "Attack", ms, 0.1f, 100.0f, 1.0f, 5.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 100.0f, 150.0f },
              { "knee", "Knee", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "range", "Range", dB, -80.0f, 0.0f, -40.0f, -30.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<Expander> },

        { { "transient", "Transient Shaper", {
              { "attack", "Attack", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "sustain", "Sustain", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "speed", "Attack Time", ms, 1.0f, 50.0f, 10.0f, 10.0f },
              { "release", "Release", ms, 20.0f, 1000.0f, 200.0f, 200.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<TransientShaper> },

        { { "brickwall", "Brickwall Limiter", {
              { "gain", "Gain", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "ceiling", "Ceiling", dB, -12.0f, 0.0f, -0.3f, -3.0f },
              { "release", "Release", ms, 1.0f, 1000.0f, 50.0f, 100.0f },
              { "mode", "Character", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Clean", "Warm" } } }, "Dynamics" },
          make<BrickwallLimiter> },
    };
    return effects;
}

} // namespace riff
