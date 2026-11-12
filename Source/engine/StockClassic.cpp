#include "StockDsp.h"

/**
    Classic Compressors: hardware-style compressors and channel strips with faces of their own (a menu section of
    their own). Each is modelled on how a kind of classic circuit behaves - our own DSP, no one's code:

      Vintage Opto        an optical levelling amplifier: a light cell's soft attack and two-stage, memory release
      Vintage FET         a FET limiting amplifier: very fast, input into a fixed threshold, the all-ratios mode
      Bus VCA             a VCA bus compressor: stepped ratio, attack and release (with auto), side-chain high pass, mix
      Punch Bus           a bus compressor with feed-forward or feedback detection, knee and the side chain's thrust
      Vox Tube Strip      a vocal strip: tube preamp, opto compressor, de-esser, EQ and a limiter
      Class A Tube Strip  a tube preamp, an optical compressor (ratio, attack, release) and a four-band EQ
      Dual Comp Channel   a tube preamp and EQ into a FET and an opto compressor: either one, or both in series

    They share the end of their chain: the valve output stage's voice (Clean, Opto, Vari-Mu), a low bump and a
    texture (warm or bright), the meter (gain reduction or the output, with VU ballistics) and the power switch.
*/
namespace riff
{
namespace
{
    using namespace dsp;

    /** The valve stage's voice: Clean (very little, open), Opto (warm 2nd harmonic), Vari-Mu (thicker: 2nd and
        3rd, rounder on the peaks). */
    float valve (float x, int voice, float& dc) noexcept
    {
        float y;
        switch (voice)
        {
            case 1:  y = x + 0.015f * x * x + 0.01f * (std::tanh (x * 1.2f) / 1.2f - x); break;
            case 2:
            {
                const auto t = std::tanh (x * 1.2f);
                y = x + 0.12f * t * t + 0.04f * (std::tanh (x * 1.6f) / 1.6f - x);
                break;
            }
            case 3:
            {
                const auto t = std::tanh (x * 1.4f);
                y = x + 0.16f * t * t + 0.18f * (std::tanh (x * 2.0f) / 2.0f - x);
                break;
            }
            default: return x;
        }
        dc += 0.0005f * (y - dc);   // (even harmonics push a little DC: out, as the transformer would)
        return y - dc;
    }

    /** A tube preamp: gain, and the more of it the rounder and thicker (asymmetric, so 2nd harmonic first). */
    inline float tubePre (float x, float drive) noexcept
    {
        if (drive <= 0.001f)
            return x;
        const auto k = 1.0f + drive * 3.0f;
        const auto y = std::tanh (k * x + 0.08f * drive) - std::tanh (0.08f * drive);
        return y / k * (1.0f + drive * 0.6f);
    }

    /** The light cell of an optical compressor: lit in ~10 ms, it lets go in two stages - half its reduction in
        ~60 ms, the rest over 0.5-5 s, slower the longer and harder it has worked (it "remembers"). */
    struct LightCell
    {
        float cell = 0.0f, fast = 0.0f, slow = 0.0f, memory = 0.0f;
        void reset() noexcept { cell = fast = slow = memory = 0.0f; }
        float step (float target, double rate, float attackMs = 10.0f, float fastMs = 60.0f, float slowScale = 1.0f) noexcept
        {
            const auto perSample = (float) (48000.0 / rate);
            memory = juce::jlimit (0.0f, 1.0f, memory + (cell > 1.0f ? cell * 1.5e-6f * perSample : -4.0e-7f * perSample));
            if (target > cell)
            {
                const auto a = coeffMs (attackMs, rate);
                cell = target + a * (cell - target);
                fast = slow = cell * 0.5f;
            }
            else
            {
                const auto fr = coeffMs (fastMs, rate);
                const auto sr = coeffMs ((500.0 + 4500.0 * memory) * slowScale, rate);
                fast = target * 0.5f + fr * (fast - target * 0.5f);
                slow = target * 0.5f + sr * (slow - target * 0.5f);
                cell = fast + slow;
            }
            return cell;
        }
    };

    /** Gain reduction (dB, positive) for a level over a threshold: a soft knee into the ratio. */
    inline float reductionFor (float overDb, float ratio, float knee) noexcept
    {
        const auto slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (knee <= 0.01f)
            return overDb > 0.0f ? overDb * slope : 0.0f;
        if (overDb < -knee * 0.5f) return 0.0f;
        if (overDb > knee * 0.5f)  return overDb * slope;
        const auto x = overDb + knee * 0.5f;
        return slope * x * x / (2.0f * knee);
    }

    /** A FET limiter's gain: a peak detector into a fixed threshold, attack 20-800 us and release 50 ms-1.1 s. */
    struct FetCore
    {
        float gr = 0.0f, env = 0.0f;
        void reset() noexcept { gr = env = 0.0f; }
        /** in: the side chain (after the input gain), level: its peak. */
        float step (float level, int ratioIndex, float attackKnob, float releaseKnob, double rate) noexcept
        {
            static constexpr float ratios[] { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
            const auto all = ratioIndex >= 4;
            const auto attackMs = 0.8f * std::pow (0.02f / 0.8f, (attackKnob - 1.0f) / 6.0f);
            const auto releaseMs = 1100.0f * std::pow (50.0f / 1100.0f, (releaseKnob - 1.0f) / 6.0f);
            env = std::max (level, env * coeffMs (1.0, rate));   // (a fast peak hold, as the FET's detector)
            const auto db = gainToDb (env) + (all ? 6.0f : 0.0f);
            const auto threshold = -12.0f + (float) juce::jlimit (0, 3, ratioIndex) * 1.5f;   // (higher ratios: a little higher)
            auto target = reductionFor (db - threshold, ratios[juce::jlimit (0, 4, ratioIndex)], all ? 0.0f : 2.0f);
            if (all)
                target *= 1.15f;   // (all buttons in: it overshoots, the famous squash)
            const auto c = coeffMs (target > gr ? attackMs : releaseMs * (all ? 1.4f : 1.0f), rate);
            gr = target + c * (gr - target);
            return gr;
        }
    };

    /** The shared end of the chain and the shared controls (meter, power, colour). */
    class ClassicBase : public BuiltInEffect
    {
    public:
        explicit ClassicBase (const EffectInfo& i) : BuiltInEffect (i)
        {
            iMeter = indexOf ("meterMode"); iPower = indexOf ("power");
            iVoice = indexOf ("voice"); iBump = indexOf ("lowBump"); iTexture = indexOf ("texture");
        }

        void prepare (double rate, int block) override
        {
            sampleRate = rate;
            dcL = dcR = 0.0f;
            bumpLow.reset(); bumpDip.reset(); warmLow.reset(); warmHigh.reset(); brightHigh.reset();
            lastKey = -1.0f;
            outRms = 0.0f;
            prepareCore (rate, block);
        }

        float takeGainReductionDb() noexcept override { return reductionMeter.exchange (0.0f); }
        float displayValue() const noexcept override { return grNow.load (std::memory_order_relaxed); }

        int readMeters (float* dest, int capacity) const noexcept override
        {
            // 0: the needle (0..1 across the meter, with its ballistics), 1: gain reduction (dB),
            // 2: input peak (dB), 3: output peak (dB)
            if (capacity < 4)
                return 0;
            dest[0] = needle.load (std::memory_order_relaxed);
            dest[1] = grNow.load (std::memory_order_relaxed);
            dest[2] = inPeak.load (std::memory_order_relaxed);
            dest[3] = outPeak.load (std::memory_order_relaxed);
            return 4;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto blockSeconds = (float) n / (float) sampleRate;
            if (choice (iPower) == 0)
            {
                // (switched off: the signal passes untouched, the needle falls back)
                moveNeedle (0.0f, blockSeconds);
                grNow.store (0.0f, std::memory_order_relaxed);
                return;
            }
            float inMax = 0.0f;
            for (int i = 0; i < n; ++i)
                inMax = std::max (inMax, std::max (std::abs (l[i]), std::abs (r[i])));

            const auto gr = processCore (l, r, n);

            updateColour();
            const auto voice = choice (iVoice);
            const auto texture = choice (iTexture);
            const auto bump = value (iBump) > 0.01f;
            float outMax = 0.0f;
            const auto rmsCoeff = coeffMs (300.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                auto a = valve (l[i], voice, dcL), b = valve (r[i], voice, dcR);
                if (bump)
                {
                    a = bumpDip.process (bumpLow.process (a, 0), 0);
                    b = bumpDip.process (bumpLow.process (b, 1), 1);
                }
                if (texture == 0)
                {
                    a = warmHigh.process (warmLow.process (a, 0), 0);
                    b = warmHigh.process (warmLow.process (b, 1), 1);
                    a += 0.04f * a * a; b += 0.04f * b * b;
                }
                else if (texture == 2)
                {
                    a = brightHigh.process (a, 0);
                    b = brightHigh.process (b, 1);
                    a += 0.03f * (std::tanh (a * 1.5f) / 1.5f - a);
                    b += 0.03f * (std::tanh (b * 1.5f) / 1.5f - b);
                }
                l[i] = a; r[i] = b;
                outMax = std::max (outMax, std::max (std::abs (a), std::abs (b)));
                const auto sq = 0.5f * (a * a + b * b);
                outRms = sq + rmsCoeff * (outRms - sq);
            }

            // The meter: gain reduction (0..20 dB across), or the output's level (VU: -18 dBFS reads 0 on +4,
            // 10 dB less on +10), with the needle's own swing
            const auto mode = choice (iMeter);
            float target;
            if (mode == 1)
                target = gr / 20.0f;
            else
            {
                const auto vu = 10.0f * std::log10 (std::max (outRms, 1.0e-10f)) + 18.0f - (mode == 0 ? 10.0f : 0.0f);
                target = (vu + 20.0f) / 23.0f;
            }
            moveNeedle (juce::jlimit (0.0f, 1.08f, target), blockSeconds);
            reductionMeter.store (std::max (reductionMeter.load (std::memory_order_relaxed), gr), std::memory_order_relaxed);
            grNow.store (gr, std::memory_order_relaxed);
            inPeak.store (gainToDb (inMax), std::memory_order_relaxed);
            outPeak.store (gainToDb (outMax), std::memory_order_relaxed);
        }

    protected:
        /** The circuit itself: processes the block in place, returns its gain reduction (dB) at the block's end. */
        virtual float processCore (float* l, float* r, int n) noexcept = 0;
        virtual void prepareCore (double, int) {}

        int indexOf (const char* id) const
        {
            for (size_t i = 0; i < info.params.size(); ++i)
                if (info.params[i].id == id)
                    return (int) i;
            jassertfalse;
            return 0;
        }

    private:
        void moveNeedle (float target, float seconds) noexcept
        {
            // (a VU's movement: about 300 ms to swing to a reading)
            const auto k = 1.0f - std::exp (-seconds / 0.065f);
            needlePos += (target - needlePos) * k;
            needle.store (needlePos, std::memory_order_relaxed);
        }

        void updateColour() noexcept
        {
            const auto bump = value (iBump);
            const auto key = bump * 131.0f + (float) choice (iTexture) * 977.0f;
            if (std::abs (key - lastKey) < 1.0e-5f)
                return;
            lastKey = key;
            bumpLow.setPeak (sampleRate, 85.0, bump, 0.6);
            bumpDip.setPeak (sampleRate, 280.0, -bump * 0.25, 1.0);
            warmLow.setPeak (sampleRate, 220.0, 1.5, 0.7);
            warmHigh.setHighShelf (sampleRate, 9000.0, -1.0, 0.7);
            brightHigh.setHighShelf (sampleRate, 8000.0, 2.0, 0.7);
        }

        int iMeter = 0, iPower = 0, iVoice = 0, iBump = 0, iTexture = 0;
        float dcL = 0.0f, dcR = 0.0f, lastKey = -1.0f, outRms = 0.0f, needlePos = 0.0f;
        Biquad bumpLow, bumpDip, warmLow, warmHigh, brightHigh;
        std::atomic<float> reductionMeter { 0.0f }, grNow { 0.0f }, needle { 0.0f }, inPeak { -100.0f }, outPeak { -100.0f };
    };

    //==============================================================================
    class VintageOpto final : public ClassicBase
    {
    public:
        explicit VintageOpto (const EffectInfo& i) : ClassicBase (i)
        {
            iGain = indexOf ("gain"); iPeak = indexOf ("peak"); iLimit = indexOf ("limit"); iEmphasis = indexOf ("emphasis");
        }
    protected:
        void prepareCore (double, int) override { cell.reset(); detector = 0.0f; emphasisFilter.reset(); lastEmphasis = -1.0f; }
        float processCore (float* l, float* r, int n) noexcept override
        {
            const auto emphasis = value (iEmphasis);
            if (std::abs (emphasis - lastEmphasis) > 1.0e-4f)
            {
                lastEmphasis = emphasis;
                emphasisFilter.setHighShelf (sampleRate, 3000.0, emphasis * 12.0, 0.7);
            }
            const auto driveDb = -10.0f + value (iPeak) / 100.0f * 50.0f;   // (Peak Reduction: the drive into the cell)
            const auto limit = choice (iLimit) == 1;
            const auto ratio = limit ? 12.0f : 3.0f, knee = limit ? 4.0f : 10.0f;
            const auto makeup = dbToGain (value (iGain) * 0.4f - 16.0f);
            const auto det = coeffMs (5.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                auto sc = 0.5f * (l[i] + r[i]);
                if (emphasis > 0.01f)
                    sc = emphasisFilter.process (sc, 0);
                const auto level = sc * sc;
                detector = level + det * (detector - level);
                const auto db = 10.0f * std::log10 (std::max (detector, 1.0e-12f)) + 3.0f + driveDb;
                const auto g = dbToGain (-cell.step (reductionFor (db, ratio, knee), sampleRate)) * makeup;
                l[i] *= g; r[i] *= g;
            }
            return cell.cell;
        }
    private:
        int iGain = 0, iPeak = 0, iLimit = 0, iEmphasis = 0;
        LightCell cell;
        float detector = 0.0f, lastEmphasis = -1.0f;
        Biquad emphasisFilter;
    };

    //==============================================================================
    class VintageFet final : public ClassicBase
    {
    public:
        explicit VintageFet (const EffectInfo& i) : ClassicBase (i)
        {
            iIn = indexOf ("input"); iOut = indexOf ("output"); iAttack = indexOf ("attack"); iRelease = indexOf ("release"); iRatio = indexOf ("ratio");
        }
    protected:
        void prepareCore (double, int) override { fet.reset(); }
        float processCore (float* l, float* r, int n) noexcept override
        {
            const auto in = dbToGain (-12.0f + value (iIn) * 0.45f);   // (input: into the fixed threshold)
            const auto out = dbToGain (-30.0f + value (iOut) * 0.5f);
            const auto ratio = choice (iRatio);
            const auto all = ratio >= 4;
            for (int i = 0; i < n; ++i)
            {
                const auto a = l[i] * in, b = r[i] * in;
                const auto gr = fet.step (std::max (std::abs (a), std::abs (b)), ratio, value (iAttack), value (iRelease), sampleRate);
                const auto g = dbToGain (-gr);
                // the FET's own colour: a little odd harmonic, more as it works (much more with all buttons in)
                const auto k = std::min (1.0f, gr / 20.0f) * (all ? 0.35f : 0.12f);
                auto x = a * g, y = b * g;
                x += k * (std::tanh (x * 2.0f) / 2.0f - x);
                y += k * (std::tanh (y * 2.0f) / 2.0f - y);
                l[i] = x * out; r[i] = y * out;
            }
            return fet.gr;
        }
    private:
        int iIn = 0, iOut = 0, iAttack = 0, iRelease = 0, iRatio = 0;
        FetCore fet;
    };

    //==============================================================================
    class BusVca final : public ClassicBase
    {
    public:
        explicit BusVca (const EffectInfo& i) : ClassicBase (i)
        {
            iThreshold = indexOf ("threshold"); iRatio = indexOf ("ratio"); iAttack = indexOf ("attack"); iRelease = indexOf ("release");
            iMakeup = indexOf ("makeup"); iHpf = indexOf ("scHpf"); iMix = indexOf ("mix");
        }
    protected:
        void prepareCore (double, int) override { gr = 0.0f; slowGr = 0.0f; env = 0.0f; hpf.reset(); lastHpf = -1; }
        float processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float ratios[] { 2.0f, 4.0f, 10.0f };
            static constexpr float attacks[] { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
            static constexpr float releases[] { 100.0f, 300.0f, 600.0f, 1200.0f };
            static constexpr float hpfs[] { 0.0f, 60.0f, 90.0f, 150.0f };
            const auto hpfIndex = juce::jlimit (0, 3, choice (iHpf));
            if (hpfIndex != lastHpf)
            {
                lastHpf = hpfIndex;
                if (hpfIndex == 0) hpf.setIdentity(); else hpf.setHighPass (sampleRate, hpfs[hpfIndex], 0.7);
            }
            const auto threshold = value (iThreshold) - 10.0f;
            const auto ratio = ratios[juce::jlimit (0, 2, choice (iRatio))];
            const auto attack = coeffMs (attacks[juce::jlimit (0, 5, choice (iAttack))], sampleRate);
            const auto releaseIndex = choice (iRelease);
            const auto autoRelease = releaseIndex >= 4;
            const auto release = coeffMs (autoRelease ? 100.0f : releases[juce::jlimit (0, 3, releaseIndex)], sampleRate);
            const auto slowRelease = coeffMs (1500.0, sampleRate);
            const auto makeup = dbToGain (value (iMakeup));
            const auto mix = value (iMix) / 100.0f;
            const auto envCoeff = coeffMs (2.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto sc = hpf.process (0.5f * (l[i] + r[i]), 0);
                const auto level = sc * sc;
                env = level + envCoeff * (env - level);   // (an RMS-ish detector, linked)
                const auto db = 10.0f * std::log10 (std::max (env, 1.0e-12f)) + 3.0f;
                const auto target = reductionFor (db - threshold, ratio, 2.0f);
                if (target > gr)
                    gr = target + attack * (gr - target);
                else
                    gr = target + release * (gr - target);
                float total = gr;
                if (autoRelease)
                {
                    // (auto: a fast release for the transients, a slow one for the sustained reduction)
                    slowGr = target > slowGr ? target + attack * (slowGr - target) : target + slowRelease * (slowGr - target);
                    total = std::max (gr, slowGr * 0.6f);
                }
                const auto g = dbToGain (-total) * makeup;
                l[i] = l[i] * (mix * g + (1.0f - mix)) ;
                r[i] = r[i] * (mix * g + (1.0f - mix));
                lastGr = total;
            }
            return lastGr;
        }
    private:
        int iThreshold = 0, iRatio = 0, iAttack = 0, iRelease = 0, iMakeup = 0, iHpf = 0, iMix = 0, lastHpf = -1;
        float gr = 0.0f, slowGr = 0.0f, env = 0.0f, lastGr = 0.0f;
        Biquad hpf;
    };

    //==============================================================================
    class PunchBus final : public ClassicBase
    {
    public:
        explicit PunchBus (const EffectInfo& i) : ClassicBase (i)
        {
            iThreshold = indexOf ("threshold"); iAttack = indexOf ("attack"); iRatio = indexOf ("ratio"); iRelease = indexOf ("release");
            iKnee = indexOf ("knee"); iThrust = indexOf ("thrust"); iType = indexOf ("type"); iMakeup = indexOf ("makeup");
        }
    protected:
        void prepareCore (double, int) override { gr = 0.0f; env = 0.0f; lastOut = 0.0f; thrustHp.reset(); thrustShelf.reset(); lastThrust = -1; }
        float processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float attacks[] { 0.03f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
            static constexpr float ratios[] { 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 50.0f };
            static constexpr float releases[] { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f };
            static constexpr float knees[] { 0.0f, 6.0f, 12.0f };
            const auto thrust = juce::jlimit (0, 2, choice (iThrust));
            if (thrust != lastThrust)
            {
                // Thrust: the side chain hears less of the lows (Med), and more of the highs too (Loud)
                lastThrust = thrust;
                if (thrust == 0) thrustHp.setIdentity(); else thrustHp.setFirstOrderHighPass (sampleRate, 120.0);
                if (thrust == 2) thrustShelf.setHighShelf (sampleRate, 2000.0, 6.0, 0.6); else thrustShelf.setIdentity();
            }
            const auto threshold = value (iThreshold) - 10.0f;
            const auto ratio = ratios[juce::jlimit (0, 6, choice (iRatio))];
            const auto knee = knees[juce::jlimit (0, 2, choice (iKnee))];
            const auto attack = coeffMs (attacks[juce::jlimit (0, 6, choice (iAttack))], sampleRate);
            const auto release = coeffMs (releases[juce::jlimit (0, 5, choice (iRelease))], sampleRate);
            const auto feedback = choice (iType) == 1;   // Old: it hears its own output
            const auto makeup = dbToGain (value (iMakeup));
            const auto envCoeff = coeffMs (1.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto in = 0.5f * (l[i] + r[i]);
                auto sc = feedback ? lastOut : in;
                sc = thrustShelf.process (thrustHp.process (sc, 0), 0);
                const auto level = sc * sc;
                env = level + envCoeff * (env - level);
                auto db = 10.0f * std::log10 (std::max (env, 1.0e-12f)) + 3.0f;
                // (feedback: the reduction it already applies is part of what it hears, so it works more gently)
                // (feedback: it hears its output - over the threshold by over/ratio - so the reduction is that times
                //  ratio - 1; smoother and more forgiving, as the old circuits)
                const auto target = feedback ? std::min (40.0f, reductionFor (db - threshold, 2.0f, knee) * 2.0f * (std::min (ratio, 20.0f) - 1.0f))
                                             : reductionFor (db - threshold, ratio, knee);
                gr = target > gr ? target + attack * (gr - target) : target + release * (gr - target);
                const auto g = dbToGain (-gr);
                l[i] *= g; r[i] *= g;
                lastOut = 0.5f * (l[i] + r[i]);
                l[i] *= makeup; r[i] *= makeup;
            }
            return gr;
        }
    private:
        int iThreshold = 0, iAttack = 0, iRatio = 0, iRelease = 0, iKnee = 0, iThrust = 0, iType = 0, iMakeup = 0, lastThrust = -1;
        float gr = 0.0f, env = 0.0f, lastOut = 0.0f;
        Biquad thrustHp, thrustShelf;
    };

    //==============================================================================
    class VoxStrip final : public ClassicBase
    {
    public:
        explicit VoxStrip (const EffectInfo& i) : ClassicBase (i)
        {
            iDrive = indexOf ("drive"); iComp = indexOf ("comp"); iLimit = indexOf ("limit"); iDeEss = indexOf ("deEss");
            iDeEssFreq = indexOf ("deEssFreq"); iLow = indexOf ("low"); iMid = indexOf ("mid"); iMidFreq = indexOf ("midFreq");
            iHigh = indexOf ("high"); iOut = indexOf ("output"); iRatio = indexOf ("ratio");
        }
    protected:
        void prepareCore (double, int) override
        {
            cell.reset(); det = 0.0f; sEnv = 0.0f; limGr = 0.0f; lastKey = -1.0f;
            for (auto* f : { &low, &mid, &high, &sBand, &sSplit }) f->reset();
        }
        float processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float mids[] { 500.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f };
            static constexpr float esses[] { 3000.0f, 5000.0f, 7000.0f, 9000.0f };
            const auto key = value (iLow) * 3.0f + value (iMid) * 7.0f + value (iHigh) * 11.0f + (float) choice (iMidFreq) * 101.0f + (float) choice (iDeEssFreq) * 1009.0f;
            if (std::abs (key - lastKey) > 1.0e-5f)
            {
                lastKey = key;
                low.setLowShelf (sampleRate, 80.0, value (iLow), 0.7);
                mid.setPeak (sampleRate, mids[juce::jlimit (0, 4, choice (iMidFreq))], value (iMid), 0.9);
                high.setHighShelf (sampleRate, std::min (10000.0, sampleRate * 0.4), value (iHigh), 0.7);
                const auto f = esses[juce::jlimit (0, 3, choice (iDeEssFreq))];
                sBand.setBandPass (sampleRate, f, 1.4);
                sSplit.setHighPass (sampleRate, f * 0.8, 0.7);
            }
            const auto drive = value (iDrive) / 100.0f;
            const auto driveDb = -8.0f + value (iComp) / 100.0f * 40.0f;
            const auto deEss = value (iDeEss) / 10.0f;
            const auto limitOn = value (iLimit) > 0.5f;
            const auto ceiling = dbToGain (-value (iLimit) / 100.0f * 10.0f - 1.0f);
            const auto out = dbToGain (value (iOut));
            const auto detC = coeffMs (5.0, sampleRate), sC = coeffMs (3.0, sampleRate);
            const auto limAttack = coeffMs (0.3, sampleRate), limRelease = coeffMs (80.0, sampleRate);
            float gr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                // the tube preamp
                auto a = tubePre (l[i], drive), b = tubePre (r[i], drive);
                // the opto
                const auto m = 0.5f * (a + b);
                det = m * m + detC * (det - m * m);
                const auto db = 10.0f * std::log10 (std::max (det, 1.0e-12f)) + 3.0f + driveDb;
                static constexpr float ratios[] { 2.0f, 3.0f, 4.0f, 6.0f, 10.0f };
                const auto cg = dbToGain (-cell.step (reductionFor (db, ratios[juce::jlimit (0, 4, choice (iRatio))], 8.0f), sampleRate));
                a *= cg; b *= cg;
                // the de-esser: the band's level over the whole's, and the highs pulled down by as much
                if (deEss > 0.01f)
                {
                    const auto s = sBand.process (0.5f * (a + b), 0);
                    sEnv = s * s + sC * (sEnv - s * s);
                    const auto overDb = 10.0f * std::log10 (std::max (sEnv, 1.0e-12f)) + 30.0f - (1.0f - deEss) * 20.0f;
                    const auto sg = dbToGain (-std::min (12.0f, reductionFor (overDb, 4.0f, 6.0f) * deEss));
                    const auto ha = sSplit.process (a, 0), hb = sSplit.process (b, 1);
                    a += ha * (sg - 1.0f); b += hb * (sg - 1.0f);
                }
                // the EQ
                a = high.process (mid.process (low.process (a, 0), 0), 0);
                b = high.process (mid.process (low.process (b, 1), 1), 1);
                // the limiter
                float lg = 1.0f;
                if (limitOn)
                {
                    const auto peak = std::max (std::abs (a), std::abs (b));
                    const auto target = peak > ceiling ? gainToDb (peak / ceiling) : 0.0f;
                    limGr = target > limGr ? target + limAttack * (limGr - target) : target + limRelease * (limGr - target);
                    lg = dbToGain (-limGr);
                }
                l[i] = a * lg * out; r[i] = b * lg * out;
                gr = cell.cell + (limitOn ? limGr : 0.0f);
            }
            return gr;
        }
    private:
        int iDrive = 0, iComp = 0, iLimit = 0, iDeEss = 0, iDeEssFreq = 0, iLow = 0, iMid = 0, iMidFreq = 0, iHigh = 0, iOut = 0, iRatio = 0;
        LightCell cell;
        float det = 0.0f, sEnv = 0.0f, limGr = 0.0f, lastKey = -1.0f;
        Biquad low, mid, high, sBand, sSplit;
    };

    //==============================================================================
    class TubeStrip final : public ClassicBase
    {
    public:
        explicit TubeStrip (const EffectInfo& i) : ClassicBase (i)
        {
            iGain = indexOf ("gain"); iThreshold = indexOf ("threshold"); iAttack = indexOf ("attack"); iRatio = indexOf ("ratio");
            iRelease = indexOf ("release"); iOut = indexOf ("output");
            iLowF = indexOf ("lowFreq"); iMidF = indexOf ("midFreq"); iHighF = indexOf ("highFreq");
            iLow = indexOf ("low"); iMid = indexOf ("mid"); iHigh = indexOf ("high");
        }
    protected:
        void prepareCore (double, int) override { cell.reset(); det = 0.0f; lastKey = -1.0f; low.reset(); mid.reset(); high.reset(); }
        float processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float lows[] { 15.0f, 30.0f, 60.0f, 150.0f };
            static constexpr float mids[] { 220.0f, 500.0f, 1500.0f, 3000.0f, 6000.0f };
            static constexpr float highs[] { 5000.0f, 10000.0f, 20000.0f, 32000.0f };
            const auto key = value (iLow) * 3.0f + value (iMid) * 7.0f + value (iHigh) * 11.0f
                           + (float) (choice (iLowF) * 101 + choice (iMidF) * 1009 + choice (iHighF) * 10007);
            if (std::abs (key - lastKey) > 1.0e-5f)
            {
                lastKey = key;
                low.setLowShelf (sampleRate, lows[juce::jlimit (0, 3, choice (iLowF))], value (iLow), 0.6);
                mid.setPeak (sampleRate, mids[juce::jlimit (0, 4, choice (iMidF))], value (iMid), 0.8);
                high.setHighShelf (sampleRate, std::min ((double) highs[juce::jlimit (0, 3, choice (iHighF))], sampleRate * 0.45), value (iHigh), 0.6);
            }
            const auto drive = value (iGain) / 100.0f;
            const auto threshold = value (iThreshold) - 10.0f;
            const auto ratio = value (iRatio);
            static constexpr float attacks[] { 2.0f, 10.0f, 40.0f };
            static constexpr float fasts[] { 40.0f, 100.0f, 300.0f, 80.0f };
            static constexpr float slowScales[] { 0.3f, 0.7f, 1.6f, 1.0f };
            const auto attackMs = attacks[juce::jlimit (0, 2, choice (iAttack))];
            const auto releaseIndex = juce::jlimit (0, 3, choice (iRelease));
            const auto out = dbToGain (value (iOut));
            const auto detC = coeffMs (4.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                auto a = tubePre (l[i], drive), b = tubePre (r[i], drive);
                const auto m = 0.5f * (a + b);
                det = m * m + detC * (det - m * m);
                const auto db = 10.0f * std::log10 (std::max (det, 1.0e-12f)) + 3.0f;
                const auto g = dbToGain (-cell.step (reductionFor (db - threshold, ratio, 6.0f), sampleRate, attackMs, fasts[releaseIndex], slowScales[releaseIndex]));
                a *= g; b *= g;
                a = high.process (mid.process (low.process (a, 0), 0), 0);
                b = high.process (mid.process (low.process (b, 1), 1), 1);
                l[i] = a * out; r[i] = b * out;
            }
            return cell.cell;
        }
    private:
        int iGain = 0, iThreshold = 0, iAttack = 0, iRatio = 0, iRelease = 0, iOut = 0, iLowF = 0, iMidF = 0, iHighF = 0, iLow = 0, iMid = 0, iHigh = 0;
        LightCell cell;
        float det = 0.0f, lastKey = -1.0f;
        Biquad low, mid, high;
    };

    //==============================================================================
    class DualChannel final : public ClassicBase
    {
    public:
        explicit DualChannel (const EffectInfo& i) : ClassicBase (i)
        {
            iLow = indexOf ("low"); iHigh = indexOf ("high"); iPre = indexOf ("preamp"); iFetIn = indexOf ("fetInput");
            iPeak = indexOf ("optoPeak"); iMode = indexOf ("mode"); iRatio = indexOf ("ratio"); iAttack = indexOf ("attack");
            iRelease = indexOf ("release"); iOut = indexOf ("output");
        }
    protected:
        void prepareCore (double, int) override { fet.reset(); cell.reset(); det = 0.0f; lastKey = -1.0f; low.reset(); high.reset(); }
        float processCore (float* l, float* r, int n) noexcept override
        {
            const auto key = value (iLow) * 3.0f + value (iHigh) * 7.0f;
            if (std::abs (key - lastKey) > 1.0e-5f)
            {
                lastKey = key;
                low.setLowShelf (sampleRate, 100.0, value (iLow), 0.6);
                high.setHighShelf (sampleRate, std::min (10000.0, sampleRate * 0.4), value (iHigh), 0.6);
            }
            const auto mode = juce::jlimit (0, 2, choice (iMode));   // FET, Both, Opto
            const auto useFet = mode != 2, useOpto = mode != 0;
            const auto drive = value (iPre) / 100.0f;
            const auto fetInDb = -12.0f + value (iFetIn) * 0.45f;
            const auto fetIn = dbToGain (fetInDb), fetBack = dbToGain (-fetInDb * 0.85f);
            const auto optoDrive = -10.0f + value (iPeak) / 100.0f * 50.0f;
            const auto ratio = choice (iRatio);
            const auto out = dbToGain (value (iOut));
            const auto detC = coeffMs (5.0, sampleRate);
            float gr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                auto a = tubePre (l[i], drive), b = tubePre (r[i], drive);
                a = high.process (low.process (a, 0), 0);
                b = high.process (low.process (b, 1), 1);
                float fetGr = 0.0f;
                if (useFet)
                {
                    a *= fetIn; b *= fetIn;
                    fetGr = fet.step (std::max (std::abs (a), std::abs (b)), ratio, value (iAttack), value (iRelease), sampleRate);
                    const auto g = dbToGain (-fetGr) * fetBack;
                    a *= g; b *= g;
                }
                if (useOpto)
                {
                    const auto m = 0.5f * (a + b);
                    det = m * m + detC * (det - m * m);
                    const auto db = 10.0f * std::log10 (std::max (det, 1.0e-12f)) + 3.0f + optoDrive;
                    const auto g = dbToGain (-cell.step (reductionFor (db, 3.0f, 10.0f), sampleRate));
                    a *= g; b *= g;
                }
                l[i] = a * out; r[i] = b * out;
                gr = fetGr + (useOpto ? cell.cell : 0.0f);
            }
            return gr;
        }
    private:
        int iLow = 0, iHigh = 0, iPre = 0, iFetIn = 0, iPeak = 0, iMode = 0, iRatio = 0, iAttack = 0, iRelease = 0, iOut = 0;
        FetCore fet;
        LightCell cell;
        float det = 0.0f, lastKey = -1.0f;
        Biquad low, high;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }

    /** Every face's shared controls, after its own (the meter's and texture's settings in the knob's order, left to
        right: so dragging turns it one way all along). */
    std::vector<ParamInfo> withCommon (std::vector<ParamInfo> own, float voice)
    {
        own.push_back ({ "meterMode", "Meter", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Output +10", "Gain Reduction", "Output +4" } });
        own.push_back ({ "power", "Power", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } });
        own.push_back ({ "voice", "Tube Voice", "", 0.0f, 3.0f, voice, 1.5f, { "Off", "Clean", "Opto", "Vari-Mu" } });
        own.push_back ({ "lowBump", "Low Bump", " dB", 0.0f, 6.0f, 0.0f, 3.0f });
        own.push_back ({ "texture", "Texture", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Warm", "Off", "Bright" } });
        return own;
    }

    const juce::String classic = "Classic Compressors";
}

const std::vector<StockEffect>& stockClassicEffects()
{
    static const std::vector<StockEffect> effects {
        { { "classicOpto", "Vintage Opto", withCommon ({
              { "gain", "Gain", "", 0.0f, 100.0f, 40.0f, 50.0f },
              { "peak", "Peak Reduction", "", 0.0f, 100.0f, 40.0f, 50.0f },
              { "limit", "Compress/Limit", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Compress", "Limit" } },
              { "emphasis", "HF Emphasis", "", 0.0f, 1.0f, 0.0f, 0.5f } }, 2.0f), classic },
          make<VintageOpto> },
        { { "classicFet", "Vintage FET", withCommon ({
              { "input", "Input", "", 0.0f, 100.0f, 40.0f, 50.0f },
              { "output", "Output", "", 0.0f, 100.0f, 52.0f, 50.0f },
              { "attack", "Attack", "", 1.0f, 7.0f, 4.0f, 4.0f },
              { "release", "Release", "", 1.0f, 7.0f, 4.0f, 4.0f },
              { "ratio", "Ratio", "", 0.0f, 4.0f, 0.0f, 2.0f, { "4:1", "8:1", "12:1", "20:1", "All" } } }, 0.0f), classic },
          make<VintageFet> },
        { { "classicVca", "Bus VCA", withCommon ({
              { "threshold", "Threshold", " dB", -20.0f, 20.0f, 0.0f, 0.0f },
              { "ratio", "Ratio", "", 0.0f, 2.0f, 1.0f, 1.0f, { "2:1", "4:1", "10:1" } },
              { "attack", "Attack", "", 0.0f, 5.0f, 3.0f, 2.5f, { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" } },
              { "release", "Release", "", 0.0f, 4.0f, 4.0f, 2.0f, { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" } },
              { "scHpf", "S/C HPF", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Off", "60 Hz", "90 Hz", "150 Hz" } },
              { "makeup", "Make-Up", " dB", 0.0f, 20.0f, 0.0f, 10.0f },
              { "mix", "Mix", "%", 0.0f, 100.0f, 100.0f, 50.0f } }, 0.0f), classic },
          make<BusVca> },
        { { "classicPunch", "Punch Bus", withCommon ({
              { "threshold", "Threshold", " dB", -20.0f, 10.0f, 0.0f, -5.0f },
              { "attack", "Attack", "", 0.0f, 6.0f, 3.0f, 3.0f, { "0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" } },
              { "ratio", "Ratio", "", 0.0f, 6.0f, 2.0f, 3.0f, { "1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1", "Inf:1" } },
              { "release", "Release", "", 0.0f, 5.0f, 2.0f, 2.5f, { "0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s" } },
              { "knee", "Knee", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Hard", "Med", "Soft" } },
              { "thrust", "Thrust", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Norm", "Med", "Loud" } },
              { "type", "Type", "", 0.0f, 1.0f, 0.0f, 0.5f, { "New", "Old" } },
              { "makeup", "Output", " dB", 0.0f, 20.0f, 0.0f, 10.0f } }, 0.0f), classic },
          make<PunchBus> },
        { { "classicVox", "Vox Tube Strip", withCommon ({
              { "low", "Low 80", " dB", -10.0f, 10.0f, 0.0f, 0.0f },
              { "mid", "Mid", " dB", -10.0f, 10.0f, 0.0f, 0.0f },
              { "midFreq", "Mid Freq", "", 0.0f, 4.0f, 2.0f, 2.0f, { "500 Hz", "1 kHz", "2 kHz", "3 kHz", "5 kHz" } },
              { "high", "High 10k", " dB", -10.0f, 10.0f, 0.0f, 0.0f },
              { "deEss", "De-Ess", "", 0.0f, 10.0f, 0.0f, 5.0f },
              { "deEssFreq", "S Freq", "", 0.0f, 3.0f, 1.0f, 1.5f, { "3 kHz", "5 kHz", "7 kHz", "9 kHz" } },
              { "drive", "Tube Drive", "", 0.0f, 100.0f, 20.0f, 50.0f },
              { "comp", "Compress", "", 0.0f, 100.0f, 30.0f, 50.0f },
              { "ratio", "Ratio", "", 0.0f, 4.0f, 2.0f, 2.0f, { "2:1", "3:1", "4:1", "6:1", "10:1" } },
              { "limit", "Limit", "", 0.0f, 100.0f, 0.0f, 50.0f },
              { "output", "Output", " dB", -12.0f, 12.0f, 0.0f, 0.0f } }, 1.0f), classic },
          make<VoxStrip> },
        { { "classicTube", "Class A Tube Strip", withCommon ({
              { "lowFreq", "Low Freq", "", 0.0f, 3.0f, 2.0f, 1.5f, { "15 Hz", "30 Hz", "60 Hz", "150 Hz" } },
              { "midFreq", "Mid Freq", "", 0.0f, 4.0f, 2.0f, 2.0f, { "220 Hz", "500 Hz", "1.5 kHz", "3 kHz", "6 kHz" } },
              { "highFreq", "High Freq", "", 0.0f, 3.0f, 1.0f, 1.5f, { "5 kHz", "10 kHz", "20 kHz", "32 kHz" } },
              { "low", "Low", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
              { "mid", "Mid", " dB", -16.0f, 16.0f, 0.0f, 0.0f },
              { "high", "High", " dB", -20.0f, 20.0f, 0.0f, 0.0f },
              { "gain", "Tube Gain", "", 0.0f, 100.0f, 20.0f, 50.0f },
              { "threshold", "Threshold", " dB", -30.0f, 10.0f, 0.0f, -10.0f },
              { "attack", "Attack", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Fast", "Med", "Slow" } },
              { "ratio", "Ratio", ":1", 2.0f, 20.0f, 4.0f, 6.0f },
              { "release", "Release", "", 0.0f, 3.0f, 1.0f, 1.5f, { "Fast", "Med", "Slow", "Auto" } },
              { "output", "Output", " dB", -30.0f, 10.0f, 0.0f, 0.0f } }, 1.0f), classic },
          make<TubeStrip> },
        { { "classicDual", "Dual Comp Channel", withCommon ({
              { "low", "Low 100", " dB", -12.0f, 12.0f, 0.0f, 0.0f },
              { "high", "High 10k", " dB", -12.0f, 12.0f, 0.0f, 0.0f },
              { "preamp", "Tube Gain", "", 0.0f, 100.0f, 20.0f, 50.0f },
              { "fetInput", "FET Input", "", 0.0f, 100.0f, 35.0f, 50.0f },
              { "optoPeak", "Opto Peak", "", 0.0f, 100.0f, 35.0f, 50.0f },
              { "mode", "Mode", "", 0.0f, 2.0f, 1.0f, 1.0f, { "FET", "Both", "Opto" } },
              { "ratio", "FET Ratio", "", 0.0f, 4.0f, 0.0f, 2.0f, { "4:1", "8:1", "12:1", "20:1", "All" } },
              { "attack", "Attack", "", 1.0f, 7.0f, 4.0f, 4.0f },
              { "release", "Release", "", 1.0f, 7.0f, 4.0f, 4.0f },
              { "output", "Output", " dB", -20.0f, 20.0f, 0.0f, 0.0f } }, 1.0f), classic },
          make<DualChannel> },
    };
    return effects;
}
}
