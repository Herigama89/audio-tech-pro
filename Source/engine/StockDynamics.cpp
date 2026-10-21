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
    /**
        Multipressor (as Logic's): four bands split by three crossovers (phase-matched), each with its own
        compression (threshold, ratio), downward expansion (threshold, ratio, reduction), peak/RMS
        detection time, attack, release, make-up gain, on/off and solo; auto gain, lookahead and output.
    */
    class MultibandCompressor final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        static constexpr int perBand = 11;
        enum Global { iX1, iX2, iX3, iAutoGain, iLookahead, iOut, bandBase };
        enum Band { bCompThr, bCompRatio, bExpThr, bExpRatio, bExpRed, bPeakRms, bAttack, bRelease, bGain, bOn, bSolo };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* c : { &x1, &x2, &x3, &ap1, &ap3 })
                c->reset();
            env.fill (0.0f);
            rms.fill (0.0f);
            for (auto& d : look) d.prepare ((int) (0.021 * rate) + 4);
            lastKey = -1.0f;
        }

        int getLatencySamples() const override { return juce::roundToInt (value (iLookahead) * 0.001 * sampleRate); }

        float bandValue (int band, int which) const noexcept { return value (bandBase + band * perBand + which); }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto f1 = value (iX1), f2 = std::max (value (iX2), f1 * 1.2f), f3 = std::max (value (iX3), f2 * 1.2f);
            const auto key = f1 + f2 * 3.0f + f3 * 7.0f;
            if (key != lastKey)
            {
                x1.set (f1, sampleRate); ap1.set (f1, sampleRate);
                x2.set (f2, sampleRate);
                x3.set (f3, sampleRate); ap3.set (f3, sampleRate);
                lastKey = key;
            }
            std::array<float, 4> attack, release, rmsCoeff, compThr, compRatio, expThr, expRatio, expRed, makeup;
            std::array<bool, 4> on, solo;
            bool anySolo = false;
            for (int b = 0; b < 4; ++b)
            {
                attack[(size_t) b] = coeffMs (bandValue (b, bAttack), sampleRate);
                release[(size_t) b] = coeffMs (bandValue (b, bRelease), sampleRate);
                rmsCoeff[(size_t) b] = coeffMs (std::max (0.01f, bandValue (b, bPeakRms)), sampleRate);
                compThr[(size_t) b] = bandValue (b, bCompThr);
                compRatio[(size_t) b] = std::max (1.0f, bandValue (b, bCompRatio));
                expThr[(size_t) b] = bandValue (b, bExpThr);
                expRatio[(size_t) b] = std::max (1.0f, bandValue (b, bExpRatio));
                expRed[(size_t) b] = bandValue (b, bExpRed);
                makeup[(size_t) b] = bandValue (b, bGain);
                on[(size_t) b] = juce::roundToInt (bandValue (b, bOn)) == 1;
                solo[(size_t) b] = juce::roundToInt (bandValue (b, bSolo)) == 1;
                anySolo = anySolo || solo[(size_t) b];
                // Auto gain: each band's make-up raised by half its compression at 0 dB in
                if (choice (iAutoGain) == 1)
                    makeup[(size_t) b] += -0.5f * compressorGainDb (0.0f, compThr[(size_t) b], compRatio[(size_t) b], 6.0f);
            }
            const auto output = dbToGain (value (iOut));
            const auto lookSamples = (float) getLatencySamples();

            for (int i = 0; i < n; ++i)
            {
                // Four bands: split at the middle crossover, align each half to the other's, split again
                float lowL, highL, lowR, highR;
                x2.split (l[i], 0, lowL, highL);
                x2.split (r[i], 1, lowR, highR);
                lowL = ap3.allPass (lowL, 0);  lowR = ap3.allPass (lowR, 1);
                highL = ap1.allPass (highL, 0); highR = ap1.allPass (highR, 1);
                std::array<float, 4> bl, br;
                x1.split (lowL, 0, bl[0], bl[1]);
                x1.split (lowR, 1, br[0], br[1]);
                x3.split (highL, 0, bl[2], bl[3]);
                x3.split (highR, 1, br[2], br[3]);

                float outL = 0.0f, outR = 0.0f, most = 0.0f;
                for (size_t b = 0; b < 4; ++b)
                {
                    // The detector hears now; the audio is held back by the lookahead
                    const auto x = std::max (std::abs (bl[b]), std::abs (br[b]));
                    rms[b] = x * x + rmsCoeff[b] * (rms[b] - x * x);
                    const auto levelDb = 10.0f * std::log10 (std::max (rms[b] * 2.0f, 1.0e-12f));
                    auto target = on[b] ? -compressorGainDb (levelDb, compThr[b], compRatio[b], 6.0f) : 0.0f;
                    if (on[b] && levelDb < expThr[b])
                        target += std::min (expRed[b], (expThr[b] - levelDb) * (expRatio[b] - 1.0f));
                    auto& e = env[b];
                    e = target + (target > e ? attack[b] : release[b]) * (e - target);
                    most = std::max (most, e);
                    look[b * 2].push (bl[b]);
                    look[b * 2 + 1].push (br[b]);
                    const auto dl = lookSamples >= 1.0f ? look[b * 2].read (lookSamples) : bl[b];
                    const auto dr = lookSamples >= 1.0f ? look[b * 2 + 1].read (lookSamples) : br[b];
                    const auto g = on[b] ? dbToGain (makeup[b] - e) : 1.0f;
                    if (anySolo && ! solo[b]) continue;
                    outL += dl * g;
                    outR += dr * g;
                }
                report (most);
                l[i] = outL * output;
                r[i] = outR * output;
            }
            publish();
        }

    private:
        Crossover x1, x2, x3, ap1, ap3;
        std::array<float, 4> env {}, rms {};
        std::array<DelayLine, 8> look;
        float lastKey = -1.0f;
    };

    //==============================================================================
    /**
        DeEsser 2 (as Logic's): a detection filter (high shelf or peak) at Frequency; Relative mode compares
        the band with the whole signal (steady whatever the level), Absolute with a fixed threshold; Max
        Reduction caps it; Split turns down just the band, Wide the whole signal; Filter Solo plays the band.
    */
    class DeEsser final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Index { iFreq, iThreshold, iRange, iMode, iListen, iDetect, iShape };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            split.reset();
            detector.reset();
            bandL.reset(); bandR.reset();
            env = 0.0f;
            bandLevel = fullLevel = 0.0f;
            lastKey = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto freq = value (iFreq);
            const auto peak = choice (iShape) == 1;
            const auto key = freq + (peak ? 100000.0f : 0.0f);
            if (key != lastKey)
            {
                split.set (freq, sampleRate);
                // The detection filter: everything above (shelf) or a band round the frequency (peak)
                if (peak) detector.setBandPass (sampleRate, freq, 1.4);
                else detector.setHighPass (sampleRate, freq * 0.9, 0.8);
                lastKey = key;
            }
            const auto threshold = value (iThreshold), range = value (iRange);
            const bool wideband = choice (iMode) == 1, listen = choice (iListen) == 1, relative = choice (iDetect) == 0;
            const auto attack = coeffMs (0.5, sampleRate), release = coeffMs (60.0, sampleRate);
            const auto follow = coeffMs (5.0, sampleRate), slow = coeffMs (50.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto mono = 0.5f * (l[i] + r[i]);
                const auto keyed = detector.process (mono, 0);
                const auto kb = std::abs (keyed), kf = std::abs (mono);
                bandLevel = kb + (kb > bandLevel ? follow : slow) * (bandLevel - kb);
                fullLevel = kf + (kf > fullLevel ? follow : slow) * (fullLevel - kf);
                // Relative: how far the band stands out from the whole signal (steady whatever the level);
                // Absolute: the band's own level against the threshold
                const auto over = relative ? gainToDb (bandLevel) - gainToDb (std::max (fullLevel, 1.0e-6f)) - threshold * 0.25f
                                           : gainToDb (bandLevel) - threshold;
                const auto target = std::min (range, std::max (0.0f, over * 0.8f));
                env = target + (target > env ? attack : release) * (env - target);
                report (env);
                const auto g = dbToGain (-env);

                float lowL, highL, lowR, highR;
                if (peak)
                {
                    // (peak: the band itself, taken out by a peak filter cut as deep as the reduction)
                    if ((i & 15) == 0)
                    {
                        Biquad fresh;
                        fresh.setPeak (sampleRate, value (iFreq), -env, 1.4);
                        for (auto* f : { &bandL, &bandR })
                        {
                            f->b0 = fresh.b0; f->b1 = fresh.b1; f->b2 = fresh.b2; f->a1 = fresh.a1; f->a2 = fresh.a2;
                        }
                    }
                    if (listen) { l[i] = keyed; r[i] = keyed; continue; }
                    if (wideband) { l[i] *= g; r[i] *= g; continue; }
                    l[i] = bandL.process (l[i], 0);
                    r[i] = bandR.process (r[i], 1);
                    continue;
                }
                split.split (l[i], 0, lowL, highL);
                split.split (r[i], 1, lowR, highR);
                if (listen) { l[i] = highL; r[i] = highR; }
                else if (wideband) { l[i] = (lowL + highL) * g; r[i] = (lowR + highR) * g; }
                else { l[i] = lowL + highL * g; r[i] = lowR + highR * g; }
            }
            publish();
        }

    private:
        Crossover split;
        Biquad detector, bandL, bandR;
        float env = 0.0f, lastKey = -1.0f, bandLevel = 0.0f, fullLevel = 0.0f;
    };

    //==============================================================================
    /**
        Noise Gate (as Logic's): Gate (closes below the threshold) or Ducker (turns down while the key is
        loud); threshold, reduction, hysteresis, attack, hold, release, lookahead; the key's filter (band pass
        or band reject between its low and high cutoff) with Monitor to hear it.
    */
    class NoiseGate final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Index { iThreshold, iRange, iAttack, iHold, iRelease, iHysteresis, iLowCut, iHighCut, iMode, iLookahead, iFilter, iFilterType, iMonitor };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            gain = 0.0f;
            holdLeft = 0;
            open = false;
            lastKey = -1.0f;
            hp.reset(); lp.reset(); rejectLow.reset(); rejectHigh.reset();
            env = 0.0f;
            lookL.prepare ((int) (0.021 * rate) + 4);
            lookR.prepare ((int) (0.021 * rate) + 4);
        }

        int getLatencySamples() const override { return juce::roundToInt (value (iLookahead) * 0.001 * sampleRate); }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto ducker = choice (iMode) == 1;
            const auto threshold = value (iThreshold), floor = dbToGain (value (iRange));
            const auto attack = coeffMs (value (iAttack), sampleRate), release = coeffMs (value (iRelease), sampleRate);
            const auto hold = (int) (value (iHold) * 0.001 * sampleRate);
            const auto hysteresis = value (iHysteresis);
            const auto filter = choice (iFilter) == 1, reject = choice (iFilterType) == 1, monitor = choice (iMonitor) == 1;
            const auto key = value (iLowCut) + value (iHighCut) * 3.0f;
            if (key != lastKey)
            {
                hp.setHighPass (sampleRate, std::max (20.0f, value (iLowCut)), 0.707);
                lp.setLowPass (sampleRate, std::min ((float) (sampleRate * 0.45), value (iHighCut)), 0.707);
                rejectLow.setLowPass (sampleRate, std::max (20.0f, value (iLowCut)), 0.707);
                rejectHigh.setHighPass (sampleRate, std::min ((float) (sampleRate * 0.45), value (iHighCut)), 0.707);
                lastKey = key;
            }
            const auto envRelease = coeffMs (5.0, sampleRate);
            const auto look = (float) getLatencySamples();

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                auto k = 0.5f * (kl + kr);
                if (filter)
                    k = reject ? rejectLow.process (k, 0) + rejectHigh.process (k, 0) : lp.process (hp.process (k, 0), 0);
                env = std::abs (k) > env ? std::abs (k) : std::abs (k) + envRelease * (env - std::abs (k));
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
                // Gate: open lets it through; Ducker: open (the key is loud) turns it down
                const auto target = ducker ? (open ? floor : 1.0f) : (open ? 1.0f : floor);
                gain = target + (target > gain ? attack : release) * (gain - target);
                report (-gainToDb (std::max (gain, 1.0e-5f)));
                lookL.push (l[i]);
                lookR.push (r[i]);
                if (monitor) { l[i] = k; r[i] = k; continue; }
                const auto a = look >= 1.0f ? lookL.read (look) : l[i], b = look >= 1.0f ? lookR.read (look) : r[i];
                l[i] = a * gain;
                r[i] = b * gain;
            }
            publish();
        }

    private:
        Biquad hp, lp, rejectLow, rejectHigh;
        DelayLine lookL, lookR;
        float gain = 0.0f, env = 0.0f, lastKey = -1.0f;
        int holdLeft = 0;
        bool open = false;
    };

    //==============================================================================
    /**
        Expander (as Logic's): above the threshold the signal is made louder still (upward expansion, by the
        ratio) - or, Downward, below it quieter, down to the range; knee, attack, release, Peak or RMS
        detection, gain with Auto Gain, and an output clip (Off, Soft, Hard, Clip).
    */
    class Expander final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Index { iThreshold, iRatio, iAttack, iRelease, iKnee, iRange, iOutput, iDirection, iAutoGain, iPeakRms, iClip };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            rms = 0.0f;
            envDb = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = value (iThreshold), ratio = std::max (1.0f, value (iRatio));
            const auto attack = coeffMs (value (iAttack), sampleRate), release = coeffMs (value (iRelease), sampleRate);
            const auto knee = value (iKnee), range = -value (iRange);
            const auto upward = choice (iDirection) == 0;
            auto outputDb = value (iOutput);
            if (upward && choice (iAutoGain) == 1)
                outputDb -= std::max (0.0f, -threshold) * (ratio - 1.0f) * 0.5f;   // (the extra level at 0 dB in, halved)
            const auto output = dbToGain (outputDb);
            const auto rmsDetect = choice (iPeakRms) == 1;
            const auto clip = choice (iClip);
            const auto rmsCoeff = coeffMs (5.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                float levelDb;
                if (rmsDetect)
                {
                    const auto x = 0.5f * (kl + kr);
                    rms = x * x + rmsCoeff * (rms - x * x);
                    levelDb = 10.0f * std::log10 (std::max (rms * 2.0f, 1.0e-12f));
                }
                else
                    levelDb = gainToDb (std::max (std::abs (kl), std::abs (kr)));
                // How far past the threshold, through the knee
                auto past = upward ? levelDb - threshold : threshold - levelDb;
                if (knee > 0.01f && std::abs (past) < knee * 0.5f)
                    past = (past + knee * 0.5f) * (past + knee * 0.5f) / (2.0f * knee);
                else if (past < 0.0f)
                    past = 0.0f;
                // (gain in dB: positive boosts above the threshold, negative cuts below it)
                const auto target = upward ? std::min (24.0f, past * (ratio - 1.0f)) : -std::min (range, past * (ratio - 1.0f));
                envDb = target + (std::abs (target) > std::abs (envDb) ? attack : release) * (envDb - target);
                report (std::max (0.0f, -envDb));
                const auto g = dbToGain (envDb) * output;
                auto a = l[i] * g, b = r[i] * g;
                switch (clip)
                {
                    case 1: a = std::tanh (a); b = std::tanh (b); break;
                    case 2: a = std::abs (a) < 0.9f ? a : std::copysign (0.9f + 0.1f * std::tanh ((std::abs (a) - 0.9f) * 10.0f), a);
                            b = std::abs (b) < 0.9f ? b : std::copysign (0.9f + 0.1f * std::tanh ((std::abs (b) - 0.9f) * 10.0f), b); break;
                    case 3: a = juce::jlimit (-1.0f, 1.0f, a); b = juce::jlimit (-1.0f, 1.0f, b); break;
                    default: break;
                }
                l[i] = a;
                r[i] = b;
            }
            publish();
        }

    private:
        float rms = 0.0f, envDb = 0.0f;
    };

    //==============================================================================
    /**
        Enveloper (as Logic's): turns the attack of each sound up or down, and its release up or down, whatever
        its level: the threshold sets what counts, Lookahead reads ahead, Release Time how long the tail
        lasts, Out Level the result.
    */
    class Enveloper final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iThreshold, iAttackGain, iAttackTime, iReleaseGain, iReleaseTime, iLookahead, iOut };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            fast = slow = tailFast = tailSlow = 0.0f;
            lookL.prepare ((int) (0.021 * rate) + 4);
            lookR.prepare ((int) (0.021 * rate) + 4);
        }

        int getLatencySamples() const override { return juce::roundToInt (value (iLookahead) * 0.001 * sampleRate); }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = dbToGain (value (iThreshold));
            const auto attackGain = value (iAttackGain) * 0.01f, releaseGain = value (iReleaseGain) * 0.01f;
            const auto quick = coeffMs (0.3, sampleRate), slowAttack = coeffMs (value (iAttackTime), sampleRate);
            const auto releaseFast = coeffMs (20.0, sampleRate), releaseSlow = coeffMs (value (iReleaseTime), sampleRate);
            const auto out = dbToGain (value (iOut));
            const auto look = (float) getLatencySamples();
            for (int i = 0; i < n; ++i)
            {
                const auto x = std::max (std::abs (l[i]), std::abs (r[i]));
                fast = x + (x > fast ? quick : releaseSlow) * (fast - x);
                slow = x + (x > slow ? slowAttack : releaseSlow) * (slow - x);
                tailFast = x + (x > tailFast ? quick : releaseFast) * (tailFast - x);
                tailSlow = x + (x > tailSlow ? quick : releaseSlow) * (tailSlow - x);
                const auto active = slow > threshold ? 1.0f : slow / std::max (threshold, 1.0e-6f);
                const auto hit = std::max (0.0f, gainToDb (fast) - gainToDb (slow));
                const auto tail = std::max (0.0f, gainToDb (tailSlow) - gainToDb (tailFast));
                const auto db = juce::jlimit (-60.0f, 24.0f, active * (attackGain * hit * 1.5f + releaseGain * tail));
                const auto g = dbToGain (db) * out;
                lookL.push (l[i]);
                lookR.push (r[i]);
                l[i] = (look >= 1.0f ? lookL.read (look) : l[i]) * g;
                r[i] = (look >= 1.0f ? lookR.read (look) : r[i]) * g;
            }
        }

    private:
        DelayLine lookL, lookR;
        float fast = 0.0f, slow = 0.0f, tailFast = 0.0f, tailSlow = 0.0f;
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
    /**
        Limiter (as Logic's): looks ahead so peaks are caught before they arrive - nothing goes over the
        output level. Gain in, release, lookahead; Legacy (with an optional soft knee) or Precision (with
        optional true peak detection, catching the peaks between samples); a Warm character option.
    */
    class BrickwallLimiter final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Index { iGain, iCeiling, iRelease, iWarm, iLookahead, iMode, iSoftKnee, iTruePeak };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            const auto most = (int) std::ceil (0.0105 * rate) + 2;
            delayL.prepare (most + 2);
            delayR.prepare (most + 2);
            window.assign ((size_t) (most + 2), {});
            head = tail = 0;
            counter = 0;
            gain = 1.0f;
            prevL = prevR = prev2L = prev2R = 0.0f;
        }

        int getLatencySamples() const override { return std::max (1, juce::roundToInt (value (iLookahead) * 0.001 * sampleRate)); }

        /** The peak between this sample and the last (a cubic through four), for true peak detection. */
        static float interSample (float y0, float y1, float y2, float y3) noexcept
        {
            float most = 0.0f;
            for (const auto t : { 0.25f, 0.5f, 0.75f })
            {
                const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
                const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const auto c = -0.5f * y0 + 0.5f * y2;
                most = std::max (most, std::abs (((a * t + b) * t + c) * t + y1));
            }
            return most;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto input = dbToGain (value (iGain)), ceiling = dbToGain (value (iCeiling));
            const auto release = coeffMs (value (iRelease), sampleRate);
            const bool warm = choice (iWarm) == 1;
            const auto precision = choice (iMode) == 1;
            const auto softKnee = ! precision && choice (iSoftKnee) == 1;
            const auto truePeak = precision && choice (iTruePeak) == 1;
            const auto look = getLatencySamples();
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
                auto peak = std::max (std::abs (a), std::abs (b));
                if (truePeak)
                    peak = std::max ({ peak, interSample (prev2L, prevL, a, a), interSample (prev2R, prevR, b, b) });
                prev2L = prevL; prevL = a; prev2R = prevR; prevR = b;
                // (soft knee: starts easing in 3 dB under the ceiling)
                const auto kneeStart = softKnee ? ceiling * 0.708f : ceiling;
                float need = 1.0f;
                if (peak > kneeStart)
                    need = softKnee ? (kneeStart + (ceiling - kneeStart) * std::tanh ((peak - kneeStart) / (ceiling - kneeStart))) / peak
                                    : ceiling / peak;
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
        int head = 0, tail = 0;
        juce::int64 counter = 0;
        float gain = 1.0f, prevL = 0.0f, prevR = 0.0f, prev2L = 0.0f, prev2R = 0.0f;
    };

    //==============================================================================
    /**
        Adaptive Limiter (as Logic's): raises the level and rounds the peaks off like a well-driven analog
        stage, never above the ceiling; its release adapts (quick for transients, slower for dense music).
        Gain, Out Ceiling, Lookahead, Remove DC Offset, True Peak Detection.
    */
    class AdaptiveLimiter final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Index { iGain, iCeiling, iLookahead, iRemoveDc, iTruePeak };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            const auto most = (int) std::ceil (0.021 * rate) + 2;
            delayL.prepare (most + 2);
            delayR.prepare (most + 2);
            gain = 1.0f;
            slowGain = 1.0f;
            dcL.reset(); dcR.reset();
            peakHold = 0.0f;
            holdLeft = 0;
        }

        int getLatencySamples() const override { return std::max (1, juce::roundToInt (value (iLookahead) * 0.001 * sampleRate)); }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto input = dbToGain (value (iGain)), ceiling = dbToGain (value (iCeiling));
            const auto look = getLatencySamples();
            const auto removeDc = choice (iRemoveDc) == 1;
            const auto truePeak = choice (iTruePeak) == 1;
            dcL.setLowPass (5.0, sampleRate);
            dcR.setLowPass (5.0, sampleRate);
            const auto fastRelease = coeffMs (30.0, sampleRate), slowRelease = coeffMs (400.0, sampleRate);
            const auto attack = coeffMs (1000.0 * look / sampleRate / 3.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                auto a = l[i] * input, b = r[i] * input;
                if (removeDc) { a = dcL.highPass (a); b = dcR.highPass (b); }
                // A peak held over the lookahead, then the gain it needs (soft: the top 4 dB bent round)
                auto peak = std::max (std::abs (a), std::abs (b)) * (truePeak ? 1.06f : 1.0f);
                if (peak >= peakHold) { peakHold = peak; holdLeft = look; }
                else if (--holdLeft <= 0) peakHold = peak;
                const auto knee = ceiling * 0.63f;
                const auto shaped = peakHold <= knee ? peakHold : knee + (ceiling - knee) * std::tanh ((peakHold - knee) / (ceiling - knee));
                const auto need = peakHold > 0.0f ? std::min (1.0f, shaped / peakHold) : 1.0f;
                // Adaptive release: dense, sustained limiting lets go slowly; a lone transient quickly
                slowGain = need + slowRelease * (slowGain - need);
                const auto rel = slowGain < 0.8f ? slowRelease : fastRelease;
                gain = need < gain ? need + attack * (gain - need) : need + rel * (gain - need);
                delayL.push (a);
                delayR.push (b);
                const auto dl = delayL.readInt (look + 1), dr = delayR.readInt (look + 1);
                l[i] = juce::jlimit (-ceiling, ceiling, dl * gain);
                r[i] = juce::jlimit (-ceiling, ceiling, dr * gain);
                report (-gainToDb (gain));
            }
            publish();
        }

    private:
        DelayLine delayL, delayR;
        OnePole dcL, dcR;
        float gain = 1.0f, slowGain = 1.0f, peakHold = 0.0f;
        int holdLeft = 0;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

namespace
{
    /** Multipressor's: the crossovers, auto gain, lookahead, output, then eleven per band. */
    std::vector<ParamInfo> multipressorParams()
    {
        const juce::String dB (" dB"), ms (" ms"), hz (" Hz");
        std::vector<ParamInfo> p {
            { "x1", "Crossover 1", hz, 30.0f, 1000.0f, 150.0f, 200.0f },
            { "x2", "Crossover 2", hz, 200.0f, 5000.0f, 1000.0f, 1000.0f },
            { "x3", "Crossover 3", hz, 1000.0f, 16000.0f, 5000.0f, 5000.0f },
            { "autoGain", "Auto Gain", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
            { "lookahead", "Lookahead", ms, 0.0f, 20.0f, 0.0f, 5.0f },
            { "output", "Out", dB, -24.0f, 24.0f, 0.0f, 0.0f } };
        const char* names[] { "Low", "Low Mid", "High Mid", "High" };
        for (int b = 0; b < 4; ++b)
        {
            const juce::String n (names[b]), id ("b" + juce::String (b + 1));
            p.push_back ({ id + "Thr", n + " Comp Threshold", dB, -60.0f, 0.0f, -20.0f, -20.0f });
            p.push_back ({ id + "Ratio", n + " Comp Ratio", ":1", 1.0f, 20.0f, 2.0f, 4.0f });
            p.push_back ({ id + "ExpThr", n + " Exp Threshold", dB, -100.0f, 0.0f, -80.0f, -60.0f });
            p.push_back ({ id + "ExpRatio", n + " Exp Ratio", ":1", 1.0f, 10.0f, 1.0f, 2.0f });
            p.push_back ({ id + "ExpRed", n + " Exp Reduction", dB, 0.0f, 60.0f, 0.0f, 20.0f });
            p.push_back ({ id + "Rms", n + " Peak/RMS", ms, 0.0f, 100.0f, 5.0f, 10.0f });
            p.push_back ({ id + "Attack", n + " Attack", ms, 0.1f, 200.0f, 20.0f, 20.0f });
            p.push_back ({ id + "Release", n + " Release", ms, 10.0f, 2000.0f, 200.0f, 200.0f });
            p.push_back ({ id + "Gain", n + " Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f });
            p.push_back ({ id + "On", n + " Band", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } });
            p.push_back ({ id + "Solo", n + " Solo", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } });
        }
        return p;
    }
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

        { { "multiband", "Multipressor", multipressorParams(), "Dynamics" },
          make<MultibandCompressor> },

        { { "deesser", "DeEsser 2", {
              { "freq", "Frequency", hz, 2000.0f, 12000.0f, 6000.0f, 6000.0f },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -30.0f, -30.0f },
              { "range", "Max Reduction", dB, 0.0f, 24.0f, 8.0f, 8.0f },
              { "mode", "Range", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Split", "Wide" } },
              { "listen", "Filter Solo", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "detect", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Relative", "Absolute" } },
              { "shape", "Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Shelf", "Peak" } } }, "Dynamics" },
          make<DeEsser> },

        { { "noiseGate", "Noise Gate", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "range", "Reduction", dB, -100.0f, 0.0f, -80.0f, -40.0f },
              { "attack", "Attack", ms, 0.01f, 100.0f, 0.5f, 5.0f },
              { "hold", "Hold", ms, 0.0f, 500.0f, 20.0f, 50.0f },
              { "release", "Release", ms, 5.0f, 2000.0f, 100.0f, 150.0f },
              { "hysteresis", "Hysteresis", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "hp", "Low Cutoff", hz, 20.0f, 2000.0f, 20.0f, 200.0f },
              { "lp", "High Cutoff", hz, 500.0f, 20000.0f, 20000.0f, 5000.0f },
              { "gateMode", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Gate", "Ducker" } },
              { "lookahead", "Lookahead", ms, 0.0f, 20.0f, 0.0f, 5.0f },
              { "filter", "Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "filterType", "Characteristics", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Bandpass", "Band Reject" } },
              { "monitor", "Monitor", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Dynamics" },
          make<NoiseGate> },

        { { "expander", "Expander", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "ratio", "Ratio", ":1", 1.0f, 10.0f, 2.0f, 3.0f },
              { "attack", "Attack", ms, 0.1f, 100.0f, 1.0f, 5.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 100.0f, 150.0f },
              { "knee", "Knee", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "range", "Range", dB, -80.0f, 0.0f, -40.0f, -30.0f },
              { "output", "Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "direction", "Direction", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Upward", "Downward" } },
              { "autoGain", "Auto Gain", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "peakRms", "Peak/RMS", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Peak", "RMS" } },
              { "clip", "Output Clip", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Off", "Soft", "Hard", "Clip" } } }, "Dynamics" },
          make<Expander> },

        { { "enveloper", "Enveloper", {
              { "threshold", "Threshold", dB, -100.0f, 0.0f, -60.0f, -40.0f },
              { "attackGain", "Attack Gain", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "attackTime", "Attack Time", ms, 1.0f, 200.0f, 20.0f, 20.0f },
              { "releaseGain", "Release Gain", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "releaseTime", "Release Time", ms, 20.0f, 5000.0f, 300.0f, 300.0f },
              { "lookahead", "Lookahead", ms, 0.0f, 20.0f, 0.0f, 5.0f },
              { "out", "Out Level", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<Enveloper> },

        { { "transient", "Transient Shaper", {
              { "attack", "Attack", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "sustain", "Sustain", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "speed", "Attack Time", ms, 1.0f, 50.0f, 10.0f, 10.0f },
              { "release", "Release", ms, 20.0f, 1000.0f, 200.0f, 200.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<TransientShaper> },

        { { "brickwall", "Limiter", {
              { "gain", "Gain", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "ceiling", "Output Level", dB, -12.0f, 0.0f, -0.3f, -3.0f },
              { "release", "Release", ms, 1.0f, 1000.0f, 50.0f, 100.0f },
              { "mode", "Character", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Clean", "Warm" } },
              { "lookahead", "Lookahead", ms, 0.1f, 10.0f, 2.0f, 2.0f },
              { "algorithm", "Mode", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Legacy", "Precision" } },
              { "softKnee", "Soft Knee", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "truePeak", "True Peak Detection", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Dynamics" },
          make<BrickwallLimiter> },

        { { "adaptiveLimiter", "Adaptive Limiter", {
              { "gain", "Gain", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "ceiling", "Out Ceiling", dB, -12.0f, 0.0f, -0.3f, -3.0f },
              { "lookahead", "Lookahead", ms, 0.5f, 20.0f, 5.0f, 5.0f },
              { "removeDc", "Remove DC Offset", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "truePeak", "True Peak Detection", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } } }, "Dynamics" },
          make<AdaptiveLimiter> },
    };
    return effects;
}

} // namespace riff
