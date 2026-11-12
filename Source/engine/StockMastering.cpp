#include "StockDsp.h"

/**
    Mastering Compressors: three stereo mastering compressors, each laid out as its kind of hardware (a menu section of
    their own). Our own DSP, modelled on how each kind of circuit behaves - no one's code:

      Mastering Hill Comp    per channel an optical stage (a light cell: soft, program dependent) into a discrete VCA
                             stage (ratio, attack, recovery with a dual release), and one of three output transformers
                             (Nickel: clean, Iron: rounder and fuller, Steel: thicker still); dual mono or stereo
      Mastering Child Comp   a variable-mu valve limiter: the more it's driven the higher the ratio; six time constants
                             (two with a program dependent release), left/right or lateral/vertical (mid/side),
                             side-chain and control links, side-chain filters, outputs
      Iron Master Comp       a valve compressor with a choice of rectifier in its detector (Germanium soft ..
                             Silicon hard), side-chain EQs, tube bias, AirBass and a tape roll-off

    All three: a power (hardwire bypass) switch and a Mix (dry / wet). The readings for their meters:
    0, 1: the left and right needles (VU dB, with a VU's swing; gain reduction reads as -dB from 0),
    2, 3: left and right gain reduction (dB), 4: input peak, 5: output peak (dBFS).
*/
namespace riff
{
namespace
{
    using namespace dsp;

    inline float overReduction (float overDb, float ratio, float knee) noexcept
    {
        const auto slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (knee <= 0.01f)
            return overDb > 0.0f ? overDb * slope : 0.0f;
        if (overDb < -knee * 0.5f) return 0.0f;
        if (overDb > knee * 0.5f)  return overDb * slope;
        const auto x = overDb + knee * 0.5f;
        return slope * x * x / (2.0f * knee);
    }

    /** A gentle saturation: odd harmonics (tanh) plus a touch of even (asymmetry), the DC it makes taken out. */
    struct Saturator
    {
        float dc = 0.0f;
        float process (float x, float amount, float even) noexcept
        {
            if (amount <= 0.0001f && even <= 0.0001f)
                return x;
            auto y = x + amount * (std::tanh (x * 1.5f) / 1.5f - x);
            const auto t = std::tanh (x);
            y += even * t * t;
            dc += 0.0005f * (y - dc);
            return y - dc;
        }
    };

    /** A two-stage optical cell (as the classic opto's): quick to light, lets go in a fast and a slow stage. */
    struct OptoCell
    {
        float cell = 0.0f, fast = 0.0f, slow = 0.0f;
        void reset() noexcept { cell = fast = slow = 0.0f; }
        float step (float target, double rate) noexcept
        {
            if (target > cell)
            {
                cell = target + coeffMs (12.0, rate) * (cell - target);
                fast = cell * 0.6f; slow = cell * 0.4f;
            }
            else
            {
                fast = target * 0.6f + coeffMs (80.0, rate) * (fast - target * 0.6f);
                slow = target * 0.4f + coeffMs (1200.0, rate) * (slow - target * 0.4f);
                cell = fast + slow;
            }
            return cell;
        }
    };

    //==============================================================================
    /** What the three share: power, mix, the levels, two VU needles. */
    class MasteringBase : public BuiltInEffect
    {
    public:
        explicit MasteringBase (const EffectInfo& i) : BuiltInEffect (i)
        {
            iPower = indexOf ("power"); iMix = indexOf ("mix");
        }

        void prepare (double rate, int block) override
        {
            sampleRate = rate;
            dryL.assign ((size_t) std::max (block, 1), 0.0f);
            dryR.assign ((size_t) std::max (block, 1), 0.0f);
            for (int c = 0; c < 2; ++c) { inRms[c] = outRms[c] = 0.0f; needleAmp[c] = 0.1f; gr[c] = 0.0f; }
            prepareCore (rate);
        }

        float takeGainReductionDb() noexcept override { return reductionMeter.exchange (0.0f); }
        float displayValue() const noexcept override { return std::max (grNow[0].load(), grNow[1].load()); }

        int readMeters (float* dest, int capacity) const noexcept override
        {
            if (capacity < 6)
                return 0;
            dest[0] = needle[0].load (std::memory_order_relaxed);
            dest[1] = needle[1].load (std::memory_order_relaxed);
            dest[2] = grNow[0].load (std::memory_order_relaxed);
            dest[3] = grNow[1].load (std::memory_order_relaxed);
            dest[4] = inPeak.load (std::memory_order_relaxed);
            dest[5] = outPeak.load (std::memory_order_relaxed);
            return 6;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto seconds = (float) n / (float) sampleRate;
            if (choice (iPower) == 0)
            {
                // (hardwire bypass: untouched, the needles back at rest)
                gr[0] = gr[1] = 0.0f;
                moveNeedles (-40.0f, -40.0f, seconds);
                grNow[0].store (0.0f); grNow[1].store (0.0f);
                return;
            }
            const auto mix = value (iMix) / 100.0f;
            const bool blend = mix < 0.999f && (size_t) n <= dryL.size();
            if (blend)
            {
                std::copy (l, l + n, dryL.begin());
                std::copy (r, r + n, dryR.begin());
            }
            const auto rms = coeffMs (300.0, sampleRate);
            float inMax = 0.0f, outMax = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                inMax = std::max (inMax, std::max (std::abs (l[i]), std::abs (r[i])));
                inRms[0] = l[i] * l[i] + rms * (inRms[0] - l[i] * l[i]);
                inRms[1] = r[i] * r[i] + rms * (inRms[1] - r[i] * r[i]);
            }

            processCore (l, r, n);

            for (int i = 0; i < n; ++i)
            {
                if (blend)
                {
                    l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * mix;
                    r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * mix;
                }
                outMax = std::max (outMax, std::max (std::abs (l[i]), std::abs (r[i])));
                outRms[0] = l[i] * l[i] + rms * (outRms[0] - l[i] * l[i]);
                outRms[1] = r[i] * r[i] + rms * (outRms[1] - r[i] * r[i]);
            }
            float target[2];
            for (int c = 0; c < 2; ++c)
            {
                // (the level meters: -18 dBFS RMS reads 0 VU)
                const auto inVu = 10.0f * std::log10 (std::max (inRms[c], 1.0e-10f)) + 18.0f;
                const auto outVu = 10.0f * std::log10 (std::max (outRms[c], 1.0e-10f)) + 18.0f;
                target[c] = meterReading (c, inVu, outVu, gr[c]);
                grNow[c].store (gr[c], std::memory_order_relaxed);
            }
            moveNeedles (target[0], target[1], seconds);
            reductionMeter.store (std::max (reductionMeter.load (std::memory_order_relaxed), std::max (gr[0], gr[1])), std::memory_order_relaxed);
            inPeak.store (gainToDb (inMax), std::memory_order_relaxed);
            outPeak.store (gainToDb (outMax), std::memory_order_relaxed);
        }

    protected:
        /** The circuit: processes the block in place, leaves each channel's gain reduction (dB) in gr. */
        virtual void processCore (float* l, float* r, int n) noexcept = 0;
        virtual void prepareCore (double) {}
        /** What channel c's meter reads (VU dB): its own choice of input, gain reduction (-dB) or output. */
        virtual float meterReading (int c, float inVu, float outVu, float reduction) const noexcept = 0;

        int indexOf (const char* id) const
        {
            for (size_t i = 0; i < info.params.size(); ++i)
                if (info.params[i].id == id)
                    return (int) i;
            jassertfalse;
            return 0;
        }
        /** A parameter of channel c ("thresh" -> "threshL" / "threshR"). */
        std::array<int, 2> pair (const char* id) const
        {
            return { indexOf ((juce::String (id) + "L").toRawUTF8()), indexOf ((juce::String (id) + "R").toRawUTF8()) };
        }

        float gr[2] {};

    private:
        void moveNeedles (float dbL, float dbR, float seconds) noexcept
        {
            // a VU's swing (about 300 ms to a reading), on the needle's own scale (it follows the voltage)
            const auto k = 1.0f - std::exp (-seconds / 0.065f);
            const float targets[] { dbL, dbR };
            for (int c = 0; c < 2; ++c)
            {
                const auto amp = dbToGain (juce::jlimit (-40.0f, 6.0f, targets[c]));
                needleAmp[c] += (amp - needleAmp[c]) * k;
                needle[c].store (gainToDb (needleAmp[c]), std::memory_order_relaxed);
            }
        }

        int iPower = 0, iMix = 0;
        std::vector<float> dryL, dryR;
        float inRms[2] {}, outRms[2] {}, needleAmp[2] { 0.1f, 0.1f };
        std::atomic<float> reductionMeter { 0.0f }, inPeak { -100.0f }, outPeak { -100.0f };
        std::atomic<float> grNow[2] { 0.0f, 0.0f }, needle[2] { -20.0f, -20.0f };
    };

    //==============================================================================
    class MasteringHill final : public MasteringBase
    {
    public:
        explicit MasteringHill (const EffectInfo& i) : MasteringBase (i)
        {
            optThresh = pair ("optThresh"); optGain = pair ("optGain"); optIn = pair ("optIn");
            discThresh = pair ("discThresh"); discRatio = pair ("discRatio"); discAttack = pair ("discAttack");
            discRecover = pair ("discRecover"); discGain = pair ("discGain"); discIn = pair ("discIn");
            scFilter = pair ("scFilter"); meter = pair ("meter"); xfmr = pair ("xfmr");
            iStereo = indexOf ("stereo");
        }
    protected:
        void prepareCore (double rate) override
        {
            for (int c = 0; c < 2; ++c)
            {
                opto[c].reset(); optDet[c] = discEnv[c] = discGr[c] = discSlow[c] = 0.0f; sat[c] = {};
                optGr[c] = 0.0f;
            }
            scHp.setHighPass (rate, 90.0, 0.7); scHp.reset();
            ironLow.setLowShelf (rate, 70.0, 0.8, 0.7); steelLow.setLowShelf (rate, 60.0, 1.6, 0.7);
            steelHigh.setHighShelf (rate, std::min (14000.0, rate * 0.45), -0.8, 0.7);
            ironLow.reset(); steelLow.reset(); steelHigh.reset();
        }

        void processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float ratios[] { 1.2f, 2.0f, 3.0f, 4.0f, 10.0f };
            static constexpr float attacks[] { 1.0f, 3.0f, 5.0f, 10.0f, 15.0f, 30.0f };
            static constexpr float recovers[] { 100.0f, 250.0f, 500.0f, 800.0f, 1200.0f, 100.0f };
            const auto stereo = choice (iStereo) == 1;
            // (stereo: the left channel's controls work both, and the two sides hear the same)
            const auto at = [&] (const std::array<int, 2>& p, int c) { return p[(size_t) (stereo ? 0 : c)]; };
            const auto optDetC = coeffMs (10.0, sampleRate), discEnvC = coeffMs (1.5, sampleRate);
            const auto slowC = coeffMs (1500.0, sampleRate);
            float* io[] { l, r };
            for (int i = 0; i < n; ++i)
            {
                float sc[2];
                for (int c = 0; c < 2; ++c)
                    sc[c] = value (at (scFilter, c)) > 0.5f ? scHp.process (io[c][i], c) : io[c][i];
                if (stereo)
                    sc[0] = sc[1] = std::abs (sc[0]) > std::abs (sc[1]) ? sc[0] : sc[1];
                for (int c = 0; c < 2; ++c)
                {
                    auto x = io[c][i];
                    // the optical stage: soft, program dependent (about 2:1, a wide knee)
                    if (value (at (optIn, c)) > 0.5f)
                    {
                        optDet[c] = sc[c] * sc[c] + optDetC * (optDet[c] - sc[c] * sc[c]);
                        const auto db = 10.0f * std::log10 (std::max (optDet[c], 1.0e-12f)) + 3.0f;
                        const auto threshold = -2.0f - 1.6f * value (at (optThresh, c));
                        optGr[c] = opto[c].step (overReduction (db - threshold, 2.2f, 12.0f), sampleRate);
                        x *= dbToGain (value (at (optGain, c)) - optGr[c]);
                    }
                    else
                        optGr[c] = 0.0f;
                    // the discrete stage: a feed-forward VCA, its detector after the optical stage's gain
                    if (value (at (discIn, c)) > 0.5f)
                    {
                        const auto s = sc[c] * dbToGain (-optGr[c]);
                        discEnv[c] = s * s + discEnvC * (discEnv[c] - s * s);
                        const auto db = 10.0f * std::log10 (std::max (discEnv[c], 1.0e-12f)) + 3.0f;
                        const auto threshold = -2.0f - 1.6f * value (at (discThresh, c));
                        const auto ratioIndex = juce::jlimit (0, 4, choice (at (discRatio, c)));
                        const auto target = overReduction (db - threshold, ratios[ratioIndex], ratioIndex == 4 ? 0.0f : 4.0f);
                        const auto recoverIndex = juce::jlimit (0, 5, choice (at (discRecover, c)));
                        const auto attack = coeffMs (attacks[juce::jlimit (0, 5, choice (at (discAttack, c)))], sampleRate);
                        const auto release = coeffMs (recovers[recoverIndex], sampleRate);
                        discGr[c] = target > discGr[c] ? target + attack * (discGr[c] - target) : target + release * (discGr[c] - target);
                        auto total = discGr[c];
                        if (recoverIndex == 5)
                        {
                            // Dual: a fast recovery for the peaks, a slow one under it for what's sustained
                            discSlow[c] = target > discSlow[c] ? target + attack * (discSlow[c] - target) : target + slowC * (discSlow[c] - target);
                            total = std::max (total, discSlow[c] * 0.7f);
                        }
                        auto g = dbToGain (value (at (discGain, c)) - total);
                        if (ratioIndex == 4)
                            g = 0.5f * (g + dbToGain (value (at (discGain, c)) - total * 0.15f));   // (Flood: the squashed and the open together)
                        x *= g;
                        lastDisc[c] = total;
                    }
                    else
                        lastDisc[c] = 0.0f;
                    // the output transformer
                    switch (juce::jlimit (0, 2, choice (at (xfmr, c))))
                    {
                        case 0:  x = sat[c].process (x, 0.02f, 0.003f); break;
                        case 1:  x = ironLow.process (sat[c].process (x, 0.12f, 0.02f), c); break;
                        default: x = steelHigh.process (steelLow.process (sat[c].process (x, 0.28f, 0.04f), c), c); break;
                    }
                    io[c][i] = x;
                }
            }
            for (int c = 0; c < 2; ++c)
                gr[c] = optGr[c] + lastDisc[c];
        }

        float meterReading (int c, float, float outVu, float) const noexcept override
        {
            const auto stereo = choice (iStereo) == 1;
            switch (choice (meter[(size_t) (stereo ? 0 : c)]))
            {
                case 0:  return -optGr[c];
                case 1:  return -lastDisc[c];
                default: return outVu;
            }
        }
    private:
        std::array<int, 2> optThresh {}, optGain {}, optIn {}, discThresh {}, discRatio {}, discAttack {}, discRecover {},
                           discGain {}, discIn {}, scFilter {}, meter {}, xfmr {};
        int iStereo = 0;
        OptoCell opto[2];
        Saturator sat[2];
        float optDet[2] {}, optGr[2] {}, discEnv[2] {}, discGr[2] {}, discSlow[2] {}, lastDisc[2] {};
        Biquad scHp, ironLow, steelLow, steelHigh;
    };

    //==============================================================================
    class MasteringChild final : public MasteringBase
    {
    public:
        explicit MasteringChild (const EffectInfo& i) : MasteringBase (i)
        {
            input = pair ("input"); thresh = pair ("thresh"); timeConst = pair ("time"); meter = pair ("meter");
            scHpf = pair ("scHpf"); out = pair ("out");
            iAgc = indexOf ("agc"); iLinkSc = indexOf ("linkSc"); iLinkCtl = indexOf ("linkCtl");
        }
    protected:
        void prepareCore (double) override
        {
            for (int c = 0; c < 2; ++c) { env[c] = grFast[c] = grSlow[c] = 0.0f; sat[c] = {}; lastHpf[c] = -1.0f; hpf[c].reset(); }
        }

        void processCore (float* l, float* r, int n) noexcept override
        {
            // the six time constants: attack (ms), release (ms); 5 and 6 let go at two speeds (program dependent)
            static constexpr float attacks[] { 0.2f, 0.2f, 0.4f, 0.8f, 0.2f, 0.4f };
            static constexpr float releases[] { 300.0f, 800.0f, 2000.0f, 5000.0f, 2000.0f, 300.0f };
            static constexpr float slows[] { 0.0f, 0.0f, 0.0f, 0.0f, 10000.0f, 10000.0f };
            const auto linked = value (iLinkCtl) > 0.5f, linkSc = value (iLinkSc) > 0.5f;
            const auto latVert = choice (iAgc) == 1;
            const auto at = [&] (const std::array<int, 2>& p, int c) { return p[(size_t) (linked ? 0 : c)]; };
            for (int c = 0; c < 2; ++c)
            {
                const auto f = value (at (scHpf, c));
                if (std::abs (f - lastHpf[c]) > 0.5f)
                {
                    lastHpf[c] = f;
                    if (f < 20.0f) hpf[c].setIdentity(); else hpf[c].setHighPass (sampleRate, f, 0.7);
                }
            }
            float inGain[2], threshold[2], outGain[2], attack[2], release[2], slow[2];
            bool dual[2];
            for (int c = 0; c < 2; ++c)
            {
                inGain[c] = dbToGain (6.0f - value (at (input, c)));   // (the input attenuator: 6 dB is unity)
                threshold[c] = -2.0f - 3.0f * value (at (thresh, c));
                outGain[c] = dbToGain (value (out[(size_t) c]));        // (the outputs stay each their own)
                const auto t = juce::jlimit (0, 5, choice (at (timeConst, c)));
                attack[c] = coeffMs (attacks[t], sampleRate);
                release[c] = coeffMs (releases[t], sampleRate);
                dual[c] = slows[t] > 0.0f;
                slow[c] = dual[c] ? coeffMs (slows[t], sampleRate) : 0.0f;
            }
            const auto envC = coeffMs (3.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                float x[2] { l[i], r[i] };
                if (latVert)
                {
                    // lateral / vertical: the middle and the sides (as cut into a record's groove)
                    const auto m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]);
                    x[0] = m; x[1] = s;
                }
                float level[2];
                for (int c = 0; c < 2; ++c)
                {
                    x[c] *= inGain[c];
                    const auto s = hpf[c].process (x[c], c);
                    env[c] = s * s + envC * (env[c] - s * s);
                    level[c] = 10.0f * std::log10 (std::max (env[c], 1.0e-12f)) + 3.0f;
                }
                if (linkSc)
                    level[0] = level[1] = std::max (level[0], level[1]);
                for (int c = 0; c < 2; ++c)
                {
                    // variable mu: the further over, the higher the ratio (about 2:1 rising to 20:1), a wide knee
                    const auto over = level[c] - threshold[c];
                    const auto ratio = juce::jlimit (1.5f, 20.0f, 2.0f + std::max (0.0f, over) * 0.6f);
                    const auto target = overReduction (over, ratio, 14.0f);
                    grFast[c] = target > grFast[c] ? target + attack[c] * (grFast[c] - target) : target + release[c] * (grFast[c] - target);
                    auto total = grFast[c];
                    if (dual[c])
                    {
                        grSlow[c] = target > grSlow[c] ? target + attack[c] * (grSlow[c] - target) : target + slow[c] * (grSlow[c] - target);
                        total = std::max (total, grSlow[c] * 0.6f);
                    }
                    gr[c] = total;
                    // the valves: push-pull (mostly odd harmonics), a little more colour the harder they work
                    x[c] = sat[c].process (x[c] * dbToGain (-total), 0.08f + std::min (0.25f, total * 0.015f), 0.01f);
                }
                if (latVert)
                {
                    const auto m = x[0], s = x[1];
                    x[0] = m + s; x[1] = m - s;
                }
                l[i] = x[0] * outGain[0];
                r[i] = x[1] * outGain[1];
            }
        }

        float meterReading (int c, float inVu, float outVu, float reduction) const noexcept override
        {
            switch (choice (meter[(size_t) c]))
            {
                case 0:  return inVu;
                case 1:  return -reduction;
                default: return outVu;
            }
        }
    private:
        std::array<int, 2> input {}, thresh {}, timeConst {}, meter {}, scHpf {}, out {};
        int iAgc = 0, iLinkSc = 0, iLinkCtl = 0;
        float env[2] {}, grFast[2] {}, grSlow[2] {}, lastHpf[2] { -1.0f, -1.0f };
        Saturator sat[2];
        Biquad hpf[2];
    };

    //==============================================================================
    class IronMaster final : public MasteringBase
    {
    public:
        explicit IronMaster (const EffectInfo& i) : MasteringBase (i)
        {
            attackP = pair ("attack"); releaseP = pair ("release"); thresh = pair ("thresh"); input = pair ("input");
            output = pair ("output"); rect = pair ("rect"); scEq = pair ("scEq"); bias = pair ("bias"); meter = pair ("meter");
            iAir = indexOf ("airBass"); iTape = indexOf ("tape"); iLink = indexOf ("link");
        }
    protected:
        void prepareCore (double) override
        {
            for (int c = 0; c < 2; ++c) { env[c] = peak[c] = gr[c] = 0.0f; sat[c] = {}; lastEq[c] = -1; scA[c].reset(); scB[c].reset(); }
            lastAir = -1; lastTape = -1.0f;
            airLow.reset(); airHigh.reset(); tapeShelf.reset(); tapeLp.reset();
        }

        void processCore (float* l, float* r, int n) noexcept override
        {
            static constexpr float attacks[] { 0.3f, 1.0f, 3.0f, 10.0f, 20.0f, 40.0f };
            static constexpr float releases[] { 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f };
            // the rectifiers: Germanium soft and gentle, LED in between, Silicon hard, Ge/Si soft at first then firm
            static constexpr float knees[] { 14.0f, 8.0f, 2.0f, 10.0f };
            static constexpr float ratios[] { 2.2f, 3.0f, 4.0f, 3.5f };
            static constexpr float peakShare[] { 0.1f, 0.3f, 0.6f, 0.4f };
            static constexpr float biasSat[] { 0.26f, 0.13f, 0.06f }, biasEven[] { 0.1f, 0.045f, 0.015f };
            for (int c = 0; c < 2; ++c)
            {
                const auto eq = juce::jlimit (0, 4, choice (scEq[(size_t) c]));
                if (eq != lastEq[c])
                {
                    // the side chain's EQs: what the detector hears (lows less, the mids more...)
                    lastEq[c] = eq;
                    switch (eq)
                    {
                        case 1:  scA[c].setHighPass (sampleRate, 100.0, 0.7); scB[c].setIdentity(); break;
                        case 2:  scA[c].setHighPass (sampleRate, 200.0, 0.7); scB[c].setPeak (sampleRate, 3000.0, 3.0, 0.8); break;
                        case 3:  scA[c].setLowShelf (sampleRate, 300.0, -6.0, 0.7); scB[c].setHighShelf (sampleRate, 5000.0, 3.0, 0.7); break;
                        case 4:  scA[c].setHighPass (sampleRate, 150.0, 0.7); scB[c].setPeak (sampleRate, 2500.0, 6.0, 0.9); break;
                        default: scA[c].setIdentity(); scB[c].setIdentity(); break;
                    }
                }
            }
            const auto air = choice (iAir);
            if (air != lastAir)
            {
                lastAir = air;
                if (air == 1) { airLow.setLowShelf (sampleRate, 80.0, 1.5, 0.7); airHigh.setHighShelf (sampleRate, std::min (12000.0, sampleRate * 0.45), 1.5, 0.7); }
                else { airLow.setIdentity(); airHigh.setIdentity(); }
            }
            const auto tape = value (iTape);
            if (std::abs (tape - lastTape) > 0.05f)
            {
                lastTape = tape;
                if (tape < 0.5f) { tapeShelf.setIdentity(); tapeLp.setIdentity(); }
                else
                {
                    // tape roll-off: the very top softened, as off a tape machine (more: lower and deeper)
                    tapeShelf.setHighShelf (sampleRate, std::min (16000.0, sampleRate * 0.45) - tape * 80.0, -tape * 0.06, 0.6);
                    tapeLp.setLowPass (sampleRate, std::min (sampleRate * 0.45, 30000.0 - tape * 200.0), 0.6);
                }
            }
            const auto link = value (iLink) > 0.5f;
            float inG[2], outG[2], thr[2], att[2], rel[2], knee[2], ratio[2], share[2], bs[2], be[2];
            for (int c = 0; c < 2; ++c)
            {
                inG[c] = dbToGain (value (input[(size_t) c]) - 7.0f);
                outG[c] = dbToGain (value (output[(size_t) c]) - 7.0f);
                thr[c] = -0.9f * value (thresh[(size_t) c]);
                att[c] = coeffMs (attacks[juce::jlimit (0, 5, choice (attackP[(size_t) c]))], sampleRate);
                rel[c] = coeffMs (releases[juce::jlimit (0, 5, choice (releaseP[(size_t) c]))], sampleRate);
                const auto k = juce::jlimit (0, 3, choice (rect[(size_t) c]));
                knee[c] = knees[k]; ratio[c] = ratios[k]; share[c] = peakShare[k];
                const auto b = juce::jlimit (0, 2, choice (bias[(size_t) c]));
                bs[c] = biasSat[b]; be[c] = biasEven[b];
            }
            const auto envC = coeffMs (5.0, sampleRate), peakC = coeffMs (30.0, sampleRate);
            float* io[] { l, r };
            for (int i = 0; i < n; ++i)
            {
                float level[2];
                for (int c = 0; c < 2; ++c)
                {
                    io[c][i] *= inG[c];
                    const auto s = scB[c].process (scA[c].process (io[c][i], c), c);
                    env[c] = s * s + envC * (env[c] - s * s);
                    peak[c] = std::max (std::abs (s), peak[c] * peakC);
                    const auto rmsDb = 10.0f * std::log10 (std::max (env[c], 1.0e-12f)) + 3.0f;
                    level[c] = rmsDb + (gainToDb (peak[c]) - rmsDb) * share[c];
                }
                if (link)
                    level[0] = level[1] = std::max (level[0], level[1]);
                for (int c = 0; c < 2; ++c)
                {
                    const auto target = overReduction (level[c] - thr[c], ratio[c], knee[c]);
                    gr[c] = target > gr[c] ? target + att[c] * (gr[c] - target) : target + rel[c] * (gr[c] - target);
                    auto x = sat[c].process (io[c][i] * dbToGain (-gr[c]), bs[c], be[c]);
                    x = airHigh.process (airLow.process (x, c), c);
                    x = tapeLp.process (tapeShelf.process (x, c), c);
                    io[c][i] = x * outG[c];
                }
            }
        }

        float meterReading (int c, float, float outVu, float reduction) const noexcept override
        {
            switch (choice (meter[(size_t) c]))
            {
                case 0:  return -reduction;
                case 1:  return outVu;
                default: return outVu - 10.0f;
            }
        }
    private:
        std::array<int, 2> attackP {}, releaseP {}, thresh {}, input {}, output {}, rect {}, scEq {}, bias {}, meter {};
        int iAir = 0, iTape = 0, iLink = 0, lastEq[2] { -1, -1 }, lastAir = -1;
        float env[2] {}, peak[2] {}, lastTape = -1.0f;
        Saturator sat[2];
        Biquad scA[2], scB[2], airLow, airHigh, tapeShelf, tapeLp;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }

    /** One control for each channel: "Name L" and "Name R". */
    void both (std::vector<ParamInfo>& list, ParamInfo p)
    {
        for (const auto* side : { "L", "R" })
        {
            auto q = p;
            q.id = p.id + side;
            q.name = p.name + " " + side;
            list.push_back (q);
        }
    }

    std::vector<ParamInfo> finish (std::vector<ParamInfo> list)
    {
        list.push_back ({ "power", "Power", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } });
        list.push_back ({ "mix", "Mix", "%", 0.0f, 100.0f, 100.0f, 50.0f });
        return list;
    }

    std::vector<ParamInfo> hillParams()
    {
        std::vector<ParamInfo> p;
        both (p, { "optThresh", "Optical Threshold", "", 0.0f, 20.0f, 6.0f, 10.0f });
        both (p, { "optGain", "Optical Gain", " dB", 0.0f, 20.0f, 2.0f, 10.0f });
        both (p, { "optIn", "Optical", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Bypass", "In" } });
        both (p, { "discThresh", "Discrete Threshold", "", 0.0f, 20.0f, 6.0f, 10.0f });
        both (p, { "discRatio", "Discrete Ratio", "", 0.0f, 4.0f, 1.0f, 2.0f, { "1.2:1", "2:1", "3:1", "4:1", "Flood" } });
        both (p, { "discAttack", "Discrete Attack", "", 0.0f, 5.0f, 2.0f, 2.5f, { "1 ms", "3 ms", "5 ms", "10 ms", "15 ms", "30 ms" } });
        both (p, { "discRecover", "Discrete Recovery", "", 0.0f, 5.0f, 2.0f, 2.5f, { "0.1 s", "0.25 s", "0.5 s", "0.8 s", "1.2 s", "Dual" } });
        both (p, { "discGain", "Discrete Gain", " dB", 0.0f, 20.0f, 2.0f, 10.0f });
        both (p, { "discIn", "Discrete", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Bypass", "In" } });
        both (p, { "scFilter", "Sidechain Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } });
        both (p, { "meter", "Meter", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Optical", "Discrete", "Output" } });
        both (p, { "xfmr", "Transformer", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Nickel", "Iron", "Steel" } });
        p.push_back ({ "stereo", "Mode", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Dual Mono", "Stereo" } });
        return finish (p);
    }

    std::vector<ParamInfo> childParams()
    {
        std::vector<ParamInfo> p;
        both (p, { "meter", "Metering", "", 0.0f, 2.0f, 1.0f, 1.0f, { "In", "GR", "Out" } });
        both (p, { "input", "Input Gain", "", 0.0f, 20.0f, 6.0f, 10.0f });
        both (p, { "thresh", "Threshold", "", 0.0f, 10.0f, 3.0f, 5.0f });
        both (p, { "time", "Time Constant", "", 0.0f, 5.0f, 1.0f, 2.5f, { "1", "2", "3", "4", "5", "6" } });
        both (p, { "scHpf", "Sidechain Filter", " Hz", 0.0f, 500.0f, 0.0f, 250.0f });
        both (p, { "out", "Output", " dB", -20.0f, 20.0f, 0.0f, 0.0f });
        p.push_back ({ "agc", "AGC", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Left/Right", "Lat/Vert" } });
        p.push_back ({ "linkSc", "Link Sidechain", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Unlink", "Link" } });
        p.push_back ({ "linkCtl", "Link Controls", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Unlink", "Link" } });
        return finish (p);
    }

    std::vector<ParamInfo> ironParams()
    {
        std::vector<ParamInfo> p;
        both (p, { "attack", "Attack", "", 0.0f, 5.0f, 2.0f, 2.5f, { "0.3 ms", "1 ms", "3 ms", "10 ms", "20 ms", "40 ms" } });
        both (p, { "release", "Release", "", 0.0f, 5.0f, 2.0f, 2.5f, { "50 ms", "100 ms", "200 ms", "400 ms", "800 ms", "1.6 s" } });
        both (p, { "thresh", "Threshold", "", 0.0f, 40.0f, 20.0f, 20.0f });
        both (p, { "input", "Input", " dB", 2.0f, 12.0f, 7.0f, 7.0f });
        both (p, { "output", "Output", " dB", 2.0f, 12.0f, 7.0f, 7.0f });
        both (p, { "rect", "Rectifier", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Germanium", "LED", "Silicon", "Ge/Si" } });
        both (p, { "scEq", "Side Chain EQ", "", 0.0f, 4.0f, 0.0f, 2.0f, { "Off", "EQ1", "EQ2", "EQ3", "EQ4" } });
        both (p, { "bias", "Tube Bias", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Low", "Mid", "High" } });
        both (p, { "meter", "Meter", "", 0.0f, 2.0f, 0.0f, 1.0f, { "GR", "VU 0", "VU +10" } });
        p.push_back ({ "airBass", "AirBass", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Bypass", "On" } });
        p.push_back ({ "tape", "Tape Roll-Off", "", 0.0f, 100.0f, 0.0f, 50.0f });
        p.push_back ({ "link", "SC Link", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } });
        return finish (p);
    }

    const juce::String mastering = "Mastering Compressors";
}

const std::vector<StockEffect>& stockMasteringEffects()
{
    static const std::vector<StockEffect> effects {
        { { "masterHill", "Mastering Hill Comp", hillParams(), mastering }, make<MasteringHill> },
        { { "masterChild", "Mastering Child Comp", childParams(), mastering }, make<MasteringChild> },
        { { "masterIron", "Iron Master Comp", ironParams(), mastering }, make<IronMaster> },
    };
    return effects;
}
}
