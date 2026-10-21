#include "StockDsp.h"

/*
    Logic's Legacy effects: AVerb, GoldVerb, Silver Compressor, Silver Gate, the original DeEsser, Speech Enhancer,
    Grooveshifter, Guitar Amp Pro and Bass Amp. (The older EQ, Denoiser and Ducker are elsewhere.)
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    const std::vector<juce::String> onOff { "Off", "On" };

    float indexIn (const EffectInfo& info, const char* id)
    {
        for (size_t i = 0; i < info.params.size(); ++i)
            if (info.params[i].id == id)
                return (float) i;
        jassertfalse;
        return 0.0f;
    }

    //==============================================================================
    /** Eight delays feeding back through a mixing matrix, damped: the older reverbs' tails. */
    struct Tail
    {
        static constexpr int lines = 8;
        std::array<DelayLine, lines> delays;
        std::array<OnePole, lines> damping;
        std::array<float, lines> lengths {}, gains {};
        std::array<DiffusionAllPass, 4> diffusers;
        float diffusion = 0.6f, diffuserScale = 1.0f;
        int diffuserCount = 4;

        void prepare (double rate)
        {
            for (auto& d : delays) d.prepare ((int) (0.5 * rate));
            for (auto& d : damping) d.reset();
            for (auto& d : diffusers) d.line.prepare ((int) (0.05 * rate));
        }
        /** size: the longest line in seconds; seconds: the decay (RT60); cutoff: the damping; density: 0..1. */
        void set (float size, float seconds, double cutoff, float density, float diffusionAmount, double rate)
        {
            static constexpr float spread[lines] { 0.43f, 0.51f, 0.59f, 0.66f, 0.74f, 0.81f, 0.89f, 1.0f };
            for (int k = 0; k < lines; ++k)
            {
                // (less density: the lines further apart, so their echoes stand out)
                const auto ratio = 1.0f - (1.0f - spread[k]) * (0.4f + 0.6f * density);
                lengths[(size_t) k] = std::max (8.0f, size * ratio * (float) rate);
                gains[(size_t) k] = std::pow (10.0f, -3.0f * lengths[(size_t) k] / (float) rate / std::max (0.05f, seconds));
                damping[(size_t) k].setLowPass (cutoff, rate);
            }
            diffusion = juce::jlimit (0.0f, 0.75f, diffusionAmount * 0.75f);
            diffuserCount = diffusionAmount < 0.03f ? 0 : 4;
            diffuserScale = juce::jlimit (0.3f, 1.5f, size / 0.08f);
        }
        void process (float in, float& outL, float& outR, double rate) noexcept
        {
            static constexpr float diffMs[] { 4.1f, 3.3f, 9.7f, 7.1f };
            for (int d = 0; d < diffuserCount; ++d)
                in = diffusers[(size_t) d].process (in, diffMs[d] * 0.001f * (float) rate * diffuserScale, diffusion);
            std::array<float, lines> out;
            float sum = 0.0f;
            for (size_t k = 0; k < (size_t) lines; ++k) { out[k] = delays[k].read (lengths[k]); sum += out[k]; }
            sum *= 2.0f / lines;
            for (size_t k = 0; k < (size_t) lines; ++k)
                delays[k].push (damping[k].lowPass ((out[k] - sum) * gains[k]) + in * (k % 2 == 0 ? 0.5f : -0.5f));
            outL = (out[0] + out[2] + out[4] + out[6]) * 0.5f;
            outR = (out[1] + out[3] + out[5] + out[7]) * 0.5f;
        }
    };

    //==============================================================================
    /** AVerb: one Density/Time control for both the reflections and the tail. */
    class AVerb final : public BuiltInEffect
    {
    public:
        enum P { predelay, reflectivity, roomSize, densityTime, mix };
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            pre.prepare ((int) (0.25 * rate));
            tail.prepare (rate);
            built = -1.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto key = value (reflectivity) + value (roomSize) * 1000.0f + value (densityTime) * 1.0e6f;
            if (key != built)
            {
                built = key;
                const auto size = 0.012f + value (roomSize) * 0.01f * 0.12f;
                const auto dt = value (densityTime) * 0.01f;
                tail.set (size, 0.15f + dt * dt * 6.0f, 1500.0 * std::pow (12.0, value (reflectivity) * 0.01), dt, dt, sampleRate);
            }
            const auto delay = std::max (1.0f, value (predelay) * 0.001f * (float) sampleRate);
            const auto wet = value (mix) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                pre.push (0.5f * (l[i] + r[i]));
                float a, b;
                tail.process (pre.read (delay), a, b, sampleRate);
                l[i] += (a - l[i]) * wet;
                r[i] += (b - r[i]) * wet;
            }
        }
    private:
        DelayLine pre;
        Tail tail;
        float built = -1.0f;
    };

    //==============================================================================
    /** GoldVerb: early reflections from a room of 3 to 7 corners, and a separate tail, balanced. */
    class GoldVerb final : public BuiltInEffect
    {
    public:
        enum P { predelay, roomShape, roomSize, stereoBase, initialDelay, spread, highCut, density, reverbTime, diffusion, balance, mix, count };
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            early.prepare ((int) (0.6 * rate));
            pre.prepare ((int) (0.5 * rate));
            tail.prepare (rate);
            highCutFilter.reset();
            built.fill (-1.0f);
        }
        void process (float* l, float* r, int n) noexcept override
        {
            std::array<float, count> v;
            for (size_t i = 0; i < (size_t) count; ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            if (v != built)
            {
                built = v;
                configure (v);
            }
            const auto wet = v[mix] * 0.01f;
            const auto bal = v[balance] * 0.01f;   // -1: reflections only .. 1: reverb only
            const auto erGain = std::min (1.0f, 1.0f - bal), revGain = std::min (1.0f, 1.0f + bal);
            const auto width = v[spread] * 0.01f;
            const auto initial = std::max (1.0f, v[initialDelay] * 0.001f * (float) sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto mono = 0.5f * (l[i] + r[i]);
                early.push (mono);
                float el = 0.0f, er = 0.0f;
                for (size_t t = 0; t < tapCount; ++t)
                {
                    const auto s = early.read (taps[t]) * tapGains[t];
                    el += s * tapPanL[t];
                    er += s * tapPanR[t];
                }
                pre.push (mono);
                float tl, tr;
                tail.process (pre.read (initial), tl, tr, sampleRate);
                tl = highCutFilter.process (tl, 0);
                tr = highCutFilter.process (tr, 1);
                const auto mid = 0.5f * (tl + tr), side = 0.5f * (tl - tr) * width;
                const auto a = el * erGain + (mid + side) * revGain, b = er * erGain + (mid - side) * revGain;
                l[i] += (a - l[i]) * wet;
                r[i] += (b - r[i]) * wet;
            }
        }
    private:
        void configure (const std::array<float, count>& v)
        {
            // Reflections: one from each wall of a regular polygon room, heard by two microphones the stereo base apart
            const auto corners = juce::jlimit (3, 7, juce::roundToInt (v[roomShape]) + 3);
            const auto radius = std::max (0.5f, v[roomSize]) / (2.0f * std::sin (juce::MathConstants<float>::pi / (float) corners));
            const auto base = v[stereoBase] * 0.01f * 0.5f;   // (metres either side)
            tapCount = 0;
            for (int order = 1; order <= 2; ++order)
                for (int wall = 0; wall < corners && tapCount < taps.size(); ++wall)
                {
                    const auto angle = juce::MathConstants<float>::twoPi * ((float) wall + 0.5f) / (float) corners;
                    const auto apothem = radius * std::cos (juce::MathConstants<float>::pi / (float) corners);
                    const auto dist = 2.0f * apothem * (float) order + 0.3f;
                    const auto side = std::cos (angle);
                    const auto pathL = dist + base * side, pathR = dist - base * side;
                    const auto seconds = 0.5f * (pathL + pathR) / 343.0f + v[predelay] * 0.001f;
                    taps[tapCount] = std::max (1.0f, seconds * (float) sampleRate);
                    tapGains[tapCount] = (order == 1 ? 0.7f : 0.4f) / (1.0f + dist * 0.15f);
                    tapPanL[tapCount] = 0.5f + 0.5f * side;
                    tapPanR[tapCount] = 0.5f - 0.5f * side;
                    ++tapCount;
                }
            const auto size = juce::jlimit (0.02f, 0.25f, v[roomSize] * 0.004f + 0.02f);
            tail.set (size, std::max (0.1f, v[reverbTime]), 9000.0, v[density] * 0.01f, v[diffusion] * 0.01f, sampleRate);
            highCutFilter.setLowPass (sampleRate, std::min ((double) v[highCut], sampleRate * 0.45), 0.707);
        }
        DelayLine early, pre;
        Tail tail;
        Biquad highCutFilter;
        std::array<float, 14> taps {}, tapGains {}, tapPanL {}, tapPanR {};
        size_t tapCount = 0;
        std::array<float, count> built {};
    };

    //==============================================================================
    /** Silver Compressor: threshold, ratio, attack, release, a soft knee and makeup gain. */
    class SilverCompressor final : public BuiltInEffect
    {
    public:
        enum P { threshold, ratio, attack, release, gain, autoGain, knee };
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; env = 0.0f; }
        float displayValue() const noexcept override { return reduction.load (std::memory_order_relaxed); }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto thr = value (threshold), rat = value (ratio);
            const auto att = std::exp (-1.0f / (std::max (0.05f, value (attack)) * 0.001f * (float) sampleRate));
            const auto rel = std::exp (-1.0f / (std::max (1.0f, value (release)) * 0.001f * (float) sampleRate));
            const auto makeup = dbToGain (value (gain) + (value (autoGain) > 0.5f ? -thr * (1.0f - 1.0f / rat) * 0.5f : 0.0f));
            const auto kneeDb = value (knee) > 0.5f ? 10.0f : 0.0f;
            float most = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto level = gainToDb (std::max (std::abs (l[i]), std::abs (r[i])) + 1.0e-9f);
                const auto target = -compressorGainDb (level, thr, rat, kneeDb);
                env = target > env ? target + att * (env - target) : target + rel * (env - target);
                const auto g = dbToGain (-env) * makeup;
                l[i] *= g;
                r[i] *= g;
                most = std::max (most, env);
            }
            reduction.store (most, std::memory_order_relaxed);
        }
    private:
        float env = 0.0f;
        std::atomic<float> reduction { 0.0f };
    };

    //==============================================================================
    /** Silver Gate: below the threshold the sound drops by the reduction, with attack, hold and release, and lookahead. */
    class SilverGate final : public BuiltInEffect
    {
    public:
        enum P { threshold, reduction, attack, hold, release, lookahead };
        using BuiltInEffect::BuiltInEffect;
        int getLatencySamples() const override { return juce::roundToInt (value (lookahead) * 0.001 * sampleRate); }
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& d : delay) d.prepare ((int) (0.012 * rate));
            gainNow = 0.0f;
            holdLeft = 0;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto open = dbToGain (value (threshold));
            const auto floorGain = value (reduction) <= -99.0f ? 0.0f : dbToGain (value (reduction));
            const auto att = 1.0f - std::exp (-1.0f / (std::max (0.01f, value (attack)) * 0.001f * (float) sampleRate));
            const auto rel = 1.0f - std::exp (-1.0f / (std::max (1.0f, value (release)) * 0.001f * (float) sampleRate));
            const auto holdSamples = (int) (value (hold) * 0.001f * (float) sampleRate);
            const auto look = std::max (1.0f, value (lookahead) * 0.001f * (float) sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto level = std::max (std::abs (l[i]), std::abs (r[i]));
                delay[0].push (l[i]);
                delay[1].push (r[i]);
                float target;
                if (level >= open) { target = 1.0f; holdLeft = holdSamples; }
                else if (holdLeft > 0) { target = 1.0f; --holdLeft; }
                else target = floorGain;
                gainNow += (target - gainNow) * (target > gainNow ? att : rel);
                l[i] = delay[0].read (look) * gainNow;
                r[i] = delay[1].read (look) * gainNow;
            }
        }
    private:
        std::array<DelayLine, 2> delay;
        float gainNow = 0.0f;
        int holdLeft = 0;
    };

    //==============================================================================
    /** The original DeEsser: a detector band and a separate suppressor band; strength, smoothing, monitors. */
    class DeEsserLegacy final : public BuiltInEffect
    {
    public:
        enum P { detectorFreq, sensitivity, suppressorFreq, strength, smoothing, monitor };
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            detector.reset();
            suppressor.reset();
            env = 0.0f;
            built = -1.0f;
        }
        float displayValue() const noexcept override { return shown.load (std::memory_order_relaxed); }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto key = value (detectorFreq) + value (suppressorFreq) * 1.0e5f;
            if (key != built)
            {
                built = key;
                detector.setHighPass (sampleRate, std::min ((double) value (detectorFreq), sampleRate * 0.45), 0.707);
                suppressor.set (std::min ((double) value (suppressorFreq), sampleRate * 0.45), sampleRate);
            }
            // Sensitivity: how far below full scale the detector starts to react
            const auto start = -10.0f - value (sensitivity) * 0.5f;
            const auto maximum = value (strength);
            const auto rel = std::exp (-1.0f / (std::max (1.0f, value (smoothing)) * 0.001f * (float) sampleRate));
            const auto monitorMode = choice (monitor);
            float most = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto dl = detector.process (l[i], 0), dr = detector.process (r[i], 1);
                const auto d = std::max (std::abs (dl), std::abs (dr));
                const auto over = juce::jlimit (0.0f, maximum, (gainToDb (d + 1.0e-9f) - start) * 1.5f);
                env = over > env ? over : over + rel * (env - over);
                const auto cut = dbToGain (-env);
                // (split where the suppressor works, phase-matched: the top turned down, the rest as it was)
                float lowL, hl, lowR, hr;
                suppressor.split (l[i], 0, lowL, hl);
                suppressor.split (r[i], 1, lowR, hr);
                if (monitorMode == 1) { l[i] = dl; r[i] = dr; }
                else if (monitorMode == 2) { l[i] = hl * (1.0f - cut); r[i] = hr * (1.0f - cut); }
                else { l[i] = lowL + hl * cut; r[i] = lowR + hr * cut; }
                most = std::max (most, env);
            }
            shown.store (most, std::memory_order_relaxed);
        }
    private:
        Biquad detector;
        Crossover suppressor;
        float env = 0.0f, built = -1.0f;
        std::atomic<float> shown { 0.0f };
    };

    //==============================================================================
    /** Base for an effect played by another of the stock effects, its knobs mapped onto that one's. */
    class Wrapped : public BuiltInEffect
    {
    public:
        Wrapped (const EffectInfo& i, const char* innerType) : BuiltInEffect (i), inner (BuiltInEffect::create (innerType)) { jassert (inner != nullptr); }
        void prepare (double rate, int block) override { sampleRate = preparedRate = rate; inner->prepare (rate, block); }
        int getLatencySamples() const override { return inner->getLatencySamples(); }
        void setPosition (juce::int64 at, bool isPlaying, double tempo) noexcept override
        {
            BuiltInEffect::setPosition (at, isPlaying, tempo);
            inner->setPosition (at, isPlaying, tempo);
        }
        float displayValue() const noexcept override { return inner->displayValue(); }
    protected:
        int innerIndex (const char* id) const { return (int) indexIn (inner->getInfo(), id); }
        std::unique_ptr<BuiltInEffect> inner;
    };

    /** Guitar Amp Pro: its amps, speakers and EQs are among Amp Designer's. */
    class GuitarAmpPro final : public Wrapped
    {
    public:
        enum P { amp, speaker, eq, gain, bass, mid, treble, presence, master, micPosition, micType, reverbOn, reverbType, reverbLevel,
                 fxOn, fxMode, fxDepth, fxSpeed, fxSync, output, count };
        explicit GuitarAmpPro (const EffectInfo& i) : Wrapped (i, "guitarAmp")
        {
            const char* ids[] { "model", "cab", "eqType", "gain", "bass", "mid", "treble", "presence", "master", "mic", "micType",
                                "revOn", "revType", "revLevel", "fxOn", "fxMode", "fxDepth", "fxSpeed", "fxSync", "output" };
            for (size_t k = 0; k < (size_t) count; ++k)
                target[k] = innerIndex (ids[k]);
        }
        void process (float* l, float* r, int n) noexcept override
        {
            static constexpr float amps[] { 3, 12, 8, 4, 19, 2, 10, 21 };       // US Combo, UK Combo, UK Top, US Top Clean, US Top Lead, Small Combo, Brown Top, Modern Top
            static constexpr float speakers[] { 15, 8, 0, 13, 4, 3, 24, 25 };   // UK 1x12, UK 4x12, US 1x12 open, US 1x12 closed, US 2x12, US 4x10, Small 1x8, DI
            static constexpr float eqs[] { 1, 0, 2 };                           // British, American, Modern
            for (size_t k = 0; k < (size_t) count; ++k)
            {
                auto x = value ((int) k);
                switch ((int) k)
                {
                    case amp:         x = amps[juce::jlimit (0, 7, juce::roundToInt (x))]; break;
                    case speaker:     x = speakers[juce::jlimit (0, 7, juce::roundToInt (x))]; break;
                    case eq:          x = eqs[juce::jlimit (0, 2, juce::roundToInt (x))]; break;
                    case gain:        x *= 10.0f; break;   // (0..10 here, 0..100 there)
                    case micPosition: x = x < 0.5f ? 10.0f : 75.0f; break;
                    case micType:     x = x < 0.5f ? 3.0f : 0.0f; break;
                    default: break;
                }
                inner->setParameter (target[k], x);
            }
            inner->process (l, r, n);
        }
    private:
        std::array<int, count> target {};
    };

    /** The original Bass Amp: its amps and controls, played by Bass Amp Designer. */
    class BassAmpLegacy final : public Wrapped
    {
    public:
        enum P { amp, preGain, bass, mid, midFreq, treble, output, count };
        explicit BassAmpLegacy (const EffectInfo& i) : Wrapped (i, "bassAmp")
        {
            for (const auto* id : { "model", "cab", "bright", "gain", "bass", "lowMid", "treble", "output", "addEqOn", "addEqType", "loFreq", "loGain", "eqOn" })
                targets.push_back (innerIndex (id));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto model = juce::jlimit (0, 3, choice (amp));
            static constexpr float innerModel[] { 0, 0, 1, 2 }, cabinet[] { 3, 3, 4, 5 }, bright[] { 0, 1, 0, 1 };
            const float values[] { innerModel[model], cabinet[model], bright[model], value (preGain) * 10.0f, value (bass), 5.0f, value (treble), value (output),
                                   1.0f, 1.0f, value (midFreq) * 0.001f, (value (mid) - 5.0f) * 3.0f, 1.0f };
            for (size_t k = 0; k < targets.size(); ++k)
                inner->setParameter (targets[k], values[k]);
            inner->process (l, r, n);
        }
    private:
        std::vector<int> targets;
    };

    //==============================================================================
    /** Speech Enhancer: noise taken down, the microphone's colour corrected, the voice brought forward. */
    class SpeechEnhancer final : public BuiltInEffect
    {
    public:
        enum P { denoise, micCorrection, micModel, voiceEnhance, enhanceMode };
        explicit SpeechEnhancer (const EffectInfo& i) : BuiltInEffect (i), cleaner (BuiltInEffect::create ("denoiser"))
        {
            threshold = (int) indexIn (cleaner->getInfo(), "threshold");
            reductionAt = (int) indexIn (cleaner->getInfo(), "reduction");
        }
        void prepare (double rate, int block) override
        {
            sampleRate = preparedRate = rate;
            cleaner->prepare (rate, block);
            for (auto& f : mic) f.reset();
            for (auto& c : split) c.reset();
            bandEnv.fill (0.0f);
            builtMic = -1;
            builtMode = -1;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            // Denoise: the noise floor you expect (towards 100 dB lets more through)
            cleaner->setParameter (threshold, -value (denoise));
            cleaner->setParameter (reductionAt, 20.0f);
            cleaner->process (l, r, n);
            if (value (micCorrection) > 0.5f)
            {
                const auto model = choice (micModel);
                if (model != builtMic)
                {
                    builtMic = model;
                    // (small built-in microphones: thin and boxy at the low mids, rolled off on top)
                    static constexpr float lowShelf[] { 3.0f, 5.0f, 4.0f, 6.0f, 2.0f }, boxDip[] { -2.0f, -3.0f, -4.0f, -2.5f, -1.5f }, air[] { 2.0f, 3.0f, 2.5f, 4.0f, 1.5f };
                    const auto m = (size_t) juce::jlimit (0, 4, model);
                    mic[0].setLowShelf (sampleRate, 180.0, lowShelf[m], 0.7);
                    mic[1].setPeak (sampleRate, 550.0, boxDip[m], 1.2);
                    mic[2].setHighShelf (sampleRate, 6000.0, air[m], 0.7);
                }
                for (int i = 0; i < n; ++i)
                    for (auto& f : mic) { l[i] = f.process (l[i], 0); r[i] = f.process (r[i], 1); }
            }
            if (value (voiceEnhance) > 0.5f)
            {
                const auto mode = choice (enhanceMode);
                if (mode != builtMode)
                {
                    builtMode = mode;
                    const auto female = mode % 2 == 0;
                    split[0].set (female ? 300.0 : 200.0, sampleRate);
                    split[1].set (female ? 3500.0 : 2800.0, sampleRate);
                    split[2].set (female ? 300.0 : 200.0, sampleRate);
                    split[3].set (female ? 3500.0 : 2800.0, sampleRate);
                    // (over music: harder, to stand out)
                    const auto over = mode >= 2;
                    bandThreshold = { -30.0f, over ? -28.0f : -24.0f, -30.0f };
                    bandRatio = { 2.0f, over ? 4.0f : 3.0f, 2.5f };
                    bandMakeup = { 1.0f, over ? 9.0f : 6.0f, 3.0f };
                }
                const auto att = std::exp (-1.0f / (0.005f * (float) sampleRate)), rel = std::exp (-1.0f / (0.12f * (float) sampleRate));
                for (int i = 0; i < n; ++i)
                {
                    std::array<std::array<float, 3>, 2> bands;
                    for (int c = 0; c < 2; ++c)
                    {
                        float low, rest, mid, high;
                        split[(size_t) (c * 2)].split (c == 0 ? l[i] : r[i], 0, low, rest);
                        split[(size_t) (c * 2 + 1)].split (rest, 0, mid, high);
                        bands[(size_t) c] = { low, mid, high };
                    }
                    for (size_t b = 0; b < 3; ++b)
                    {
                        const auto level = gainToDb (std::max (std::abs (bands[0][b]), std::abs (bands[1][b])) + 1.0e-9f);
                        const auto target = -compressorGainDb (level, bandThreshold[b], bandRatio[b], 6.0f);
                        bandEnv[b] = target > bandEnv[b] ? target + att * (bandEnv[b] - target) : target + rel * (bandEnv[b] - target);
                        const auto g = dbToGain (bandMakeup[b] - bandEnv[b]);
                        bands[0][b] *= g;
                        bands[1][b] *= g;
                    }
                    l[i] = saturate (bands[0][0] + bands[0][1] + bands[0][2]);
                    r[i] = saturate (bands[1][0] + bands[1][1] + bands[1][2]);
                }
            }
        }
    private:
        std::unique_ptr<BuiltInEffect> cleaner;
        int threshold = 0, reductionAt = 0, builtMic = -1, builtMode = -1;
        std::array<Biquad, 3> mic;
        std::array<Crossover, 4> split;
        std::array<float, 3> bandEnv {}, bandThreshold {}, bandRatio {}, bandMakeup {};
    };

    //==============================================================================
    /** Grooveshifter: swing put into straight playing - every other eighth (or sixteenth) moved later, without
        changing the pitch (grains of the sound played at their speed while time is stretched and squeezed). */
    class Grooveshifter final : public BuiltInEffect
    {
    public:
        enum P { source, grainSize, grid, swing, accent };
        using BuiltInEffect::BuiltInEffect;
        int getLatencySamples() const override { return maxGrain; }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            maxGrain = (int) (0.1 * rate);
            for (auto& h : history) h.assign ((size_t) (8.0 * rate), 0.0f);
            written = 0;
            voices = {};
            nextGrain = 0;
            freeBeats = 0.0;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto tempo = std::max (20.0, bpm.load (std::memory_order_relaxed));
            const auto beatSamples = 60.0 / tempo * sampleRate;
            const auto cell = (choice (grid) == 0 ? 0.5 : 0.25) * beatSamples;   // (1/8 or 1/16)
            const auto s = juce::jlimit (0.5, 0.85, (double) value (swing) * 0.01);
            const auto tonal = choice (source) == 1;
            const auto grainSetting = value (grainSize);
            const auto grain = tonal ? (grainSetting < 1.0f ? std::min (maxGrain, (int) (0.04 * sampleRate)) : std::min (maxGrain, (int) (grainSetting * 0.001f * (float) sampleRate)))
                                     : std::min (maxGrain, (int) (0.012 * sampleRate));
            const auto hop = std::max (1, grain / 2);
            const auto accentGain = dbToGain (value (accent));
            const auto size = (juce::int64) history[0].size();
            const auto isPlaying = playing.load (std::memory_order_relaxed);
            auto beat = isPlaying ? (double) position.load (std::memory_order_relaxed) / beatSamples : freeBeats;
            for (int i = 0; i < n; ++i)
            {
                const auto at = written % size;
                history[0][(size_t) at] = l[i];
                history[1][(size_t) at] = r[i];
                // Where in the pair of cells the output is, and where in the input that comes from
                // (on the stream as delayed by a grain: the delay compensation puts it back in time with the song)
                const auto delayedAt = std::max (0.0, beat * beatSamples - maxGrain);
                const auto inPair = std::fmod (delayedAt, 2.0 * cell);
                const auto split = 2.0 * cell * s;
                const auto source = inPair < split ? inPair / (2.0 * s) : cell + (inPair - split) / (2.0 * (1.0 - s));
                const auto sourceAt = (double) written - maxGrain - inPair + source;
                if (--nextGrain <= 0)
                {
                    auto& v = voices[voiceTurn];
                    voiceTurn = 1 - voiceTurn;
                    v.start = sourceAt;
                    v.t = 0;
                    v.length = grain;
                    nextGrain = hop;
                }
                float a = 0.0f, b = 0.0f;
                for (auto& v : voices)
                {
                    if (v.t >= v.length)
                        continue;
                    const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) v.t / (float) v.length);
                    a += read (0, v.start + v.t, size) * w;
                    b += read (1, v.start + v.t, size) * w;
                    ++v.t;
                }
                // Accent: the moved notes louder or softer
                if (inPair >= split) { a *= accentGain; b *= accentGain; }
                l[i] = a;
                r[i] = b;
                ++written;
                beat += 1.0 / beatSamples;
            }
            freeBeats = beat;
        }

    private:
        struct Voice { double start = 0.0; int t = 1 << 30, length = 1; };
        float read (int ch, double at, juce::int64 size) const noexcept
        {
            const auto base = (juce::int64) std::floor (at);
            const auto frac = (float) (at - (double) base);
            const auto& h = history[(size_t) ch];
            const auto i0 = (size_t) (((base % size) + size) % size), i1 = (size_t) ((((base + 1) % size) + size) % size);
            return h[i0] + frac * (h[i1] - h[i0]);
        }
        std::array<std::vector<float>, 2> history;
        juce::int64 written = 0;
        std::array<Voice, 2> voices {};
        int voiceTurn = 0, nextGrain = 0, maxGrain = 4800;
        double freeBeats = 0.0;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockLegacyEffects()
{
    const juce::String ms (" ms"), dB (" dB"), hz (" Hz"), pc (" %");
    static const std::vector<StockEffect> effects {
        { { "aVerb", "AVerb", {
              { "predelay", "Predelay", ms, 0.0f, 200.0f, 10.0f, 30.0f },
              { "reflectivity", "Reflectivity", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "roomSize", "Room Size", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "densityTime", "Density/Time", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Legacy" },
          make<AVerb> },
        { { "goldVerb", "GoldVerb", {
              { "predelay", "Predelay", ms, 0.0f, 200.0f, 8.0f, 30.0f },
              { "roomShape", "Room Shape", "", 0.0f, 4.0f, 2.0f, 2.0f, { "3 Corners", "4 Corners", "5 Corners", "6 Corners", "7 Corners" } },
              { "roomSize", "Room Size", " m", 1.0f, 50.0f, 12.0f, 12.0f },
              { "stereoBase", "Stereo Base", " cm", 0.0f, 200.0f, 30.0f, 50.0f },
              { "initialDelay", "Initial Delay", ms, 0.0f, 200.0f, 25.0f, 40.0f },
              { "spread", "Spread", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "highCut", "High Cut", hz, 200.0f, 20000.0f, 8000.0f, 4000.0f },
              { "density", "Density", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "reverbTime", "Reverb Time", " s", 0.3f, 20.0f, 1.8f, 2.5f },
              { "diffusion", "Diffusion", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "balance", "Balance ER/Reverb", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Legacy" },
          make<GoldVerb> },
        { { "silverComp", "Silver Compressor", {
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "ratio", "Ratio", ":1", 1.0f, 30.0f, 3.0f, 4.0f },
              { "attack", "Attack", ms, 0.1f, 200.0f, 10.0f, 20.0f },
              { "release", "Release", ms, 5.0f, 2000.0f, 150.0f, 200.0f },
              { "gain", "Gain", dB, 0.0f, 30.0f, 0.0f, 10.0f },
              { "autoGain", "Auto Gain", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "knee", "Soft Knee", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff } }, "Legacy" },
          make<SilverCompressor> },
        { { "silverGate", "Silver Gate", {
              { "threshold", "Threshold", dB, -100.0f, 0.0f, -40.0f, -40.0f },
              { "reduction", "Reduction", dB, -100.0f, 0.0f, -60.0f, -30.0f },
              { "attack", "Attack", ms, 0.0f, 100.0f, 1.0f, 10.0f },
              { "hold", "Hold", ms, 0.0f, 2000.0f, 50.0f, 200.0f },
              { "release", "Release", ms, 2.0f, 2000.0f, 100.0f, 200.0f },
              { "lookahead", "Lookahead", ms, 0.0f, 10.0f, 0.0f, 3.0f } }, "Legacy" },
          make<SilverGate> },
        { { "deesserLegacy", "DeEsser", {
              { "detectorFreq", "Detector Frequency", hz, 500.0f, 20000.0f, 6000.0f, 5000.0f },
              { "sensitivity", "Sensitivity", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "suppressorFreq", "Suppressor Frequency", hz, 500.0f, 20000.0f, 6000.0f, 5000.0f },
              { "strength", "Strength", dB, 0.0f, 50.0f, 12.0f, 15.0f },
              { "smoothing", "Smoothing", ms, 1.0f, 200.0f, 20.0f, 30.0f },
              { "monitor", "Monitor", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Off", "Detector", "Suppressor" } } }, "Legacy" },
          make<DeEsserLegacy> },
        { { "speechEnhancer", "Speech Enhancer", {
              { "denoise", "Denoise", dB, 0.0f, 100.0f, 70.0f, 70.0f },
              { "micCorrection", "Mic Correction", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
              { "micModel", "Mic Model", "", 0.0f, 4.0f, 0.0f, 2.0f, { "Generic", "Laptop", "Desktop", "Webcam", "Headset" } },
              { "voiceEnhance", "Voice Enhance", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
              { "enhanceMode", "Enhance Mode", "", 0.0f, 3.0f, 1.0f, 1.5f, { "Female Solo", "Male Solo", "Female Voice Over", "Male Voice Over" } } }, "Legacy" },
          make<SpeechEnhancer> },
        { { "grooveshifter", "Grooveshifter", {
              { "source", "Source Material", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Beat", "Tonal" } },
              { "grainSize", "Grain Size", ms, 0.0f, 100.0f, 0.0f, 40.0f },
              { "grid", "Grid", "", 0.0f, 1.0f, 0.0f, 0.5f, { "1/8", "1/16" } },
              { "swing", "Swing", pc, 50.0f, 85.0f, 58.0f, 62.0f },
              { "accent", "Accent", dB, -12.0f, 12.0f, 0.0f, 0.0f } }, "Legacy" },
          make<Grooveshifter> },
        { { "guitarAmpPro", "Guitar Amp Pro", {
              { "amp", "Amp", "", 0.0f, 7.0f, 0.0f, 3.5f, { "US Combo", "UK Combo", "UK Top", "US Top Clean", "US Top Lead", "Small Combo", "Brown Top", "Modern Top" } },
              { "speaker", "Speaker", "", 0.0f, 7.0f, 1.0f, 3.5f, { "UK 1 x 12", "UK 4 x 12", "US 1 x 12 Open", "US 1 x 12 Closed", "US 2 x 12", "US 4 x 10", "Small 1 x 8", "DI Box" } },
              { "eq", "EQ", "", 0.0f, 2.0f, 0.0f, 1.0f, { "British", "American", "Modern" } },
              { "gain", "Gain", "", 0.0f, 10.0f, 4.0f, 5.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "mid", "Mids", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "presence", "Presence", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "master", "Master", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "micPosition", "Mic Position", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Centre", "Off-Axis" } },
              { "micType", "Mic", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Dynamic", "Condenser" } },
              { "reverbOn", "Reverb", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "reverbType", "Reverb Type", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Spring 1", "Spring 2", "Spring 3" } },
              { "reverbLevel", "Reverb Level", pc, 0.0f, 100.0f, 25.0f, 50.0f },
              { "fxOn", "Effects", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "fxMode", "Trem/Vib", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Tremolo", "Vibrato" } },
              { "fxDepth", "Depth", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "fxSpeed", "Speed", hz, 0.1f, 12.0f, 4.0f, 4.0f },
              { "fxSync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Free", "Sync" } },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Legacy" },
          make<GuitarAmpPro> },
        { { "bassAmpLegacy", "Bass Amp", {
              { "amp", "Amp Model", "", 0.0f, 3.0f, 0.0f, 1.5f, { "American Basic", "American Deluxe", "British Top", "Modern" } },
              { "preGain", "Pre Gain", "", 0.0f, 10.0f, 3.0f, 5.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "mid", "Mid", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "midFreq", "Mid Frequency", hz, 100.0f, 1000.0f, 400.0f, 400.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Legacy" },
          make<BassAmpLegacy> },
    };
    return effects;
}

} // namespace riff
