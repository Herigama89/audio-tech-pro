#include "StockDsp.h"

/*
    Logic's Multi Effects: Phat FX (filters, three distortions, a modulation effect, a bass enhancer and a
    compressor in an order you choose, moved by two LFOs, an envelope follower and an XY pad) and Step FX
    (filter, distortion, modulation, delay and reverb, moved by three step sequencers and an XY pad).
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    const std::vector<juce::String> onOff { "Off", "On" };
    const std::vector<juce::String> divisions { "1/1", "1/2", "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1/64" };
    constexpr double divisionBeats[] { 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    constexpr int chunk = 32;   // (modulation is worked out this often)

    //==============================================================================
    // Shared parts
    //==============================================================================
    /** A parameter's knob travel, 0..1 (log for frequencies and times), so modulation moves it evenly. */
    struct Travel
    {
        float min = 0.0f, max = 1.0f;
        bool log = false;
        static Travel of (const ParamInfo& p) noexcept
        {
            return { p.min, p.max, p.min > 0.0f && (p.suffix == " Hz" || p.suffix == " ms" || p.suffix == " s") };
        }
        float toUnit (float v) const noexcept
        {
            return log ? std::log (std::max (v, min) / min) / std::log (max / min) : (v - min) / std::max (1.0e-6f, max - min);
        }
        float fromUnit (float u) const noexcept
        {
            u = juce::jlimit (0.0f, 1.0f, u);
            return log ? min * std::pow (max / min, u) : min + u * (max - min);
        }
    };

    /** The modulation targets: the names in the menus and the parameters they move. */
    struct Targets
    {
        std::vector<juce::String> names;   // (first: "Off")
        std::vector<juce::String> ids;     // (first: empty)
    };

    float indexOf (const EffectInfo& info, const juce::String& id)
    {
        for (size_t i = 0; i < info.params.size(); ++i)
            if (info.params[i].id == id)
                return (float) i;
        jassertfalse;
        return 0.0f;
    }

    /** The distortion types Phat FX offers, as shapers. */
    const std::vector<juce::String> distortionTypes { "Bitcrusher", "Diode", "Dirt", "Downsampler", "Exciter", "Grit", "Scream",
                                                      "Soft Saturation", "Squared", "Tube", "Vari Drive" };
    struct Shaper
    {
        float held = 0.0f, count = 0.0f;
        OnePole exciterHp;
        Biquad scream;
        Noise noise;
        int screamFor = -1;

        void reset() noexcept { held = count = 0.0f; exciterHp.reset(); scream.reset(); }

        float process (int type, float x, float amount, double rate, int ch) noexcept
        {
            const auto a = juce::jlimit (0.0f, 1.0f, amount * 0.01f);
            if (a <= 1.0e-4f)
                return x;
            switch (type)
            {
                case 0:  { const auto q = std::pow (2.0f, 15.0f - a * 13.0f); return std::round (x * q) / q; }
                case 1:  { const auto u = x * (1.0f + a * 20.0f); return (u >= 0.0f ? 1.0f - std::exp (-u) : -0.6f * (1.0f - std::exp (u))) * (1.0f - 0.35f * a); }
                case 2:  { const auto u = x * (1.0f + a * 30.0f); return (0.75f * juce::jlimit (-1.0f, 1.0f, u) + 0.25f * saturate (u) + 0.004f * a * noise.next()) * (1.0f - 0.4f * a); }
                case 3:
                {
                    const auto factor = 1.0f + a * a * 48.0f;
                    if ((count += 1.0f) >= factor) { count -= factor; held = x; }
                    return held;
                }
                case 4:
                {
                    if (exciterHp.a == 0.0f) exciterHp.setLowPass (3000.0, rate);
                    const auto high = exciterHp.highPass (x);
                    return x + a * saturate (high * 8.0f) * 0.4f;
                }
                case 5:  { const auto u = x * (1.0f + a * 10.0f); return std::sin (u) * (1.0f - 0.3f * a); }
                case 6:
                {
                    if (screamFor != juce::roundToInt (a * 100.0f)) { scream.setPeak (rate, 1200.0, 14.0 * a, 0.8); screamFor = juce::roundToInt (a * 100.0f); }
                    return std::tanh (scream.process (x, ch) * (1.0f + a * 60.0f)) * (1.0f - 0.45f * a);
                }
                case 7:  return saturate (x * (1.0f + a * 8.0f)) * (1.0f - 0.3f * a);
                case 8:  { const auto u = juce::jlimit (-1.5f, 1.5f, x * (1.0f + a * 6.0f)); return saturate (u * std::abs (u) * 1.5f + u * (1.0f - a)) * (1.0f - 0.3f * a); }
                case 9:  { const auto bias = 0.25f * a; const auto u = x * (1.0f + a * 15.0f); return (std::tanh (u + bias) - std::tanh (bias)) * (1.0f - 0.35f * a); }
                default: { const auto drive = 1.0f + a * a * 50.0f; return std::tanh (drive * x) / std::sqrt (std::tanh (drive)) * (1.0f - 0.3f * a); }
            }
        }
    };

    /** A four-pole ladder low pass with resonance, saturating in its feedback. */
    struct Ladder
    {
        std::array<float, 4> s {};
        void reset() noexcept { s = {}; }
        float process (float x, float g, float resonance) noexcept
        {
            const auto G = g / (1.0f + g);
            auto u = std::tanh (x - resonance * 4.0f * s[3]);
            for (auto& stage : s)
            {
                const auto v = G * (u - stage);
                const auto y = v + stage;
                stage = y + v;
                u = y;
            }
            return u;
        }
    };

    /** The multimode filter (Phat FX's and Step FX's): ladder, classic 12 dB, phat 24 dB, notch and peak. */
    const std::vector<juce::String> filterTypes { "Ladder LP 24", "Classic LP 12", "Classic BP 12", "Classic HP 12", "Phat LP 24", "Phat HP 24", "Notch", "Peak" };
    struct MultiFilter
    {
        std::array<Svf, 2> a, b;
        std::array<Ladder, 2> ladder;
        float g = 0.1f, res = 0.0f;
        int type = 0;

        void reset() noexcept { for (auto& f : a) f.reset(); for (auto& f : b) f.reset(); for (auto& l : ladder) l.reset(); }
        void set (int newType, double cutoff, float resonance01, double rate) noexcept
        {
            type = newType;
            res = juce::jlimit (0.0f, 1.0f, resonance01);
            const auto freq = juce::jlimit (20.0, rate * 0.45, cutoff);
            g = (float) std::tan (juce::MathConstants<double>::pi * freq / rate);
            const auto q = 0.6 + res * res * 18.0;
            for (auto* bank : { &a, &b })
                for (auto& f : *bank)
                    f.set (freq, type >= 4 && type <= 5 ? 0.55 + res * 6.0 : q, rate);
        }
        float process (float x, int ch) noexcept
        {
            float low, band, high;
            switch (type)
            {
                case 0:  return ladder[(size_t) ch].process (x, g, res * 1.05f);
                case 1:  a[(size_t) ch].tick (x, low, band, high); return low;
                case 2:  a[(size_t) ch].tick (x, low, band, high); return band;
                case 3:  a[(size_t) ch].tick (x, low, band, high); return high;
                case 4:
                {
                    a[(size_t) ch].tick (x, low, band, high);
                    const auto first = saturate (low);
                    b[(size_t) ch].tick (first, low, band, high);
                    return low;
                }
                case 5:
                {
                    a[(size_t) ch].tick (x, low, band, high);
                    const auto first = saturate (high);
                    b[(size_t) ch].tick (first, low, band, high);
                    return high;
                }
                case 6:  a[(size_t) ch].tick (x, low, band, high); return low + high;
                default: a[(size_t) ch].tick (x, low, band, high); return x + band * (1.0f + res * 3.0f);
            }
        }
    };

    /** A chorus/flanger: one to three modulated taps. */
    struct ModUnit
    {
        std::array<DelayLine, 2> lines;
        double phase = 0.0;
        float feedbackL = 0.0f, feedbackR = 0.0f;
        void prepare (double rate) { for (auto& l : lines) l.prepare ((int) (0.06 * rate)); phase = 0.0; feedbackL = feedbackR = 0.0f; }
        /** baseMs/depthMs: the taps' middle and swing; voices 1-3; spread: the right side's LFO offset. */
        void process (float& l, float& r, double rate, double hz, float baseMs, float depthMs, int voices, float feedback, float mix, float spread) noexcept
        {
            lines[0].push (l + feedbackL * feedback);
            lines[1].push (r + feedbackR * feedback);
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < voices; ++v)
            {
                const auto offset = (double) v / voices;
                const auto ml = std::sin (juce::MathConstants<double>::twoPi * (phase + offset));
                const auto mr = std::sin (juce::MathConstants<double>::twoPi * (phase + offset + spread));
                wl += lines[0].read ((float) ((baseMs + depthMs * ml) * 0.001 * rate));
                wr += lines[1].read ((float) ((baseMs + depthMs * mr) * 0.001 * rate));
            }
            wl /= (float) voices;
            wr /= (float) voices;
            feedbackL = wl;
            feedbackR = wr;
            phase += hz / rate;
            phase -= std::floor (phase);
            l += (wl - l) * mix;
            r += (wr - r) * mix;
        }
    };

    /** A small reverb: four delays feeding back through a mixing matrix, darkened. */
    struct SmallReverb
    {
        std::array<DelayLine, 4> lines;
        std::array<OnePole, 4> damping;
        std::array<float, 4> lengths {};
        void prepare (double rate)
        {
            for (auto& l : lines) l.prepare ((int) (0.2 * rate));
            for (auto& d : damping) d.reset();
        }
        void set (int type, double rate) noexcept
        {
            const double base[3][4] { { 0.0213, 0.0287, 0.0331, 0.0397 }, { 0.0451, 0.0563, 0.0677, 0.0791 }, { 0.0119, 0.0173, 0.0229, 0.0283 } };
            const double cut[3] { 6000.0, 4500.0, 9000.0 };
            const auto t = juce::jlimit (0, 2, type);
            for (size_t i = 0; i < 4; ++i)
            {
                lengths[i] = (float) (base[t][i] * rate);
                damping[i].setLowPass (cut[t], rate);
            }
        }
        void process (float& l, float& r, float seconds, float mix, double rate) noexcept
        {
            std::array<float, 4> out;
            for (size_t i = 0; i < 4; ++i)
                out[i] = lines[i].read (lengths[i]);
            const auto avg = (float) (lengths[0] + lengths[1] + lengths[2] + lengths[3]) * 0.25f;
            const auto g = std::pow (10.0f, -3.0f * avg / (std::max (0.1f, seconds) * (float) rate));
            // (a Householder matrix: lossless mixing)
            const auto sum = (out[0] + out[1] + out[2] + out[3]) * 0.5f;
            const float in[4] { l, r, l, r };
            for (size_t i = 0; i < 4; ++i)
                lines[i].push (damping[i].lowPass ((out[i] - sum) * g) + in[i] * 0.35f);
            l += ((out[0] + out[2]) * 0.6f - l) * mix;
            r += ((out[1] + out[3]) * 0.6f - r) * mix;
        }
    };

    /** The XY pad's parameters (both effects): position, the two axes on/off, two targets with depths each. */
    void addXyParams (std::vector<ParamInfo>& p, const Targets& t)
    {
        p.push_back ({ "xyX", "XY Pad X", "", 0.0f, 1.0f, 0.5f, 0.5f });
        p.push_back ({ "xyY", "XY Pad Y", "", 0.0f, 1.0f, 0.5f, 0.5f });
        p.push_back ({ "xOn", "X", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
        p.push_back ({ "yOn", "Y", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
        p.push_back ({ "xT1", "X Target 1", "", 0.0f, (float) t.names.size() - 1.0f, 0.0f, 1.0f, t.names });
        p.push_back ({ "xT2", "X Target 2", "", 0.0f, (float) t.names.size() - 1.0f, 0.0f, 1.0f, t.names });
        p.push_back ({ "xD1", "X Depth 1", " %", -100.0f, 100.0f, 50.0f, 0.0f });
        p.push_back ({ "xD2", "X Depth 2", " %", -100.0f, 100.0f, 50.0f, 0.0f });
        p.push_back ({ "yT1", "Y Target 1", "", 0.0f, (float) t.names.size() - 1.0f, 0.0f, 1.0f, t.names });
        p.push_back ({ "yT2", "Y Target 2", "", 0.0f, (float) t.names.size() - 1.0f, 0.0f, 1.0f, t.names });
        p.push_back ({ "yD1", "Y Depth 1", " %", -100.0f, 100.0f, 50.0f, 0.0f });
        p.push_back ({ "yD2", "Y Depth 2", " %", -100.0f, 100.0f, 50.0f, 0.0f });
    }

    /** Base: values per chunk, the modulation applied to the targets' parameters. */
    class ModulatedEffect : public BuiltInEffect
    {
    public:
        ModulatedEffect (const EffectInfo& i, const Targets& t) : BuiltInEffect (i)
        {
            for (const auto& id : t.ids)
                targetParam.push_back (id.isEmpty() ? -1 : (int) indexOf (i, id));
            for (const auto& p : i.params)
                travel.push_back (Travel::of (p));
        }

    protected:
        int idx (const char* id) const { return (int) indexOf (info, id); }

        /** Starts a chunk: takes the knobs, clears the modulation. */
        void beginChunk() noexcept
        {
            for (size_t i = 0; i < info.params.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            mods.fill (0.0f);
        }
        void modulate (int target, float amount) noexcept
        {
            if (target > 0 && target < (int) targetParam.size())
                mods[(size_t) targetParam[(size_t) target]] += amount;
        }
        /** After the modulators: each modulated parameter moved along its travel. */
        void applyModulation() noexcept
        {
            for (size_t i = 0; i < info.params.size(); ++i)
                if (mods[i] != 0.0f)
                    v[i] = travel[i].fromUnit (travel[i].toUnit (v[i]) + mods[i]);
        }
        void addXy() noexcept
        {
            const auto x = v[(size_t) xy], y = v[(size_t) xy + 1];
            if (v[(size_t) xy + 2] > 0.5f)
            {
                modulate (juce::roundToInt (v[(size_t) xy + 4]), x * v[(size_t) xy + 6] * 0.01f);
                modulate (juce::roundToInt (v[(size_t) xy + 5]), x * v[(size_t) xy + 7] * 0.01f);
            }
            if (v[(size_t) xy + 3] > 0.5f)
            {
                modulate (juce::roundToInt (v[(size_t) xy + 8]), y * v[(size_t) xy + 10] * 0.01f);
                modulate (juce::roundToInt (v[(size_t) xy + 9]), y * v[(size_t) xy + 11] * 0.01f);
            }
        }

        std::array<float, maxParams> v {}, mods {};
        std::vector<int> targetParam;
        std::vector<Travel> travel;
        int xy = 0;
    };

    //==============================================================================
    // Phat FX
    //==============================================================================
    const std::vector<juce::String> phatUnits { "Bandpass", "Filter", "Distortion", "Mod FX", "Bass Enhancer", "Compressor" };
    const std::vector<juce::String> lfoWaves { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" };

    const Targets& phatTargets()
    {
        static const Targets t {
            { "Off", "Input", "Output", "Mix", "Bandpass Low", "Bandpass High", "Reject Mix", "Filter Cutoff", "Filter Res", "Filter Drive", "Filter Mix",
              "Distortion 1", "Distortion 2", "Distortion 3", "Mod FX Mix", "Mod FX Rate", "Bass Amount", "Bass Tune", "Comp Amount", "Comp Release",
              "LFO 1 Rate", "LFO 1 Depth", "LFO 2 Rate", "LFO 2 Depth" },
            { "", "input", "output", "mix", "bpLow", "bpHigh", "bpReject", "fCutoff", "fRes", "fDrive", "fMix",
              "dAmt1", "dAmt2", "dAmt3", "mMix", "mRate", "bAmount", "bTune", "cAmount", "cRelease",
              "l1Rate", "l1Depth", "l2Rate", "l2Depth" } };
        return t;
    }

    class PhatFx final : public ModulatedEffect
    {
    public:
        enum Key { k_l1On, k_eOn, k_input, k_output, k_mix, k_limiter, k_slot1, k_bpOn, k_bpType, k_bpLowRes, k_bpHighRes, k_bpLow, k_bpHigh, k_bpReject, k_fOn, k_fType, k_fCutoff, k_fRes, k_fDrive, k_fMix, k_dOn, k_dType1, k_mOn, k_mType, k_mMix, k_mRate, k_bOn, k_bType, k_bAmount, k_bTune, k_cOn, k_cType, k_cAmount, k_cRelease, k_dAmt1, keyCount };
        static constexpr const char* keyNames[] { "l1On", "eOn", "input", "output", "mix", "limiter", "slot1", "bpOn", "bpType", "bpLowRes", "bpHighRes", "bpLow", "bpHigh", "bpReject", "fOn", "fType", "fCutoff", "fRes", "fDrive", "fMix", "dOn", "dType1", "mOn", "mType", "mMix", "mRate", "bOn", "bType", "bAmount", "bTune", "cOn", "cType", "cAmount", "cRelease", "dAmt1" };
        explicit PhatFx (const EffectInfo& i) : ModulatedEffect (i, phatTargets())
        {
            xy = idx ("xyX");
            for (int k = 0; k < keyCount; ++k)
                at[(size_t) k] = (int) indexOf (info, keyNames[k]);
        }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& s : shapers) for (auto& c : s) c.reset();
            for (auto& b : bandpass) b.reset();
            filter.reset();
            mod.prepare (rate);
            bassBand.reset(); bassShelf.reset(); bassLow.reset();
            compEnv = 0.0f; envelope = 0.0f; flip = 1.0f; lastSign = 0.0f;
            lfoPhase = { 0.0, 0.0 }; lfoHeld = { 0.0f, 0.0f }; lfoFrom = { 0.0f, 0.0f }; lfoTo = { 0.0f, 0.0f };
        }

        void process (float* l, float* r, int n) noexcept override
        {
            for (int start = 0; start < n; start += chunk)
            {
                const auto count = std::min (chunk, n - start);
                beginChunk();
                modulators (count);
                applyModulation();
                configure();
                for (int i = start; i < start + count; ++i)
                    processSample (l[i], r[i]);
            }
        }

    private:
        float p (Key key) const noexcept { return v[(size_t) at[(size_t) key]]; }
        float p (int index) const noexcept { return v[(size_t) index]; }

        float lfoValue (int k, int wave, double phase) noexcept
        {
            switch (wave)
            {
                case 0:  return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                case 1:  return (float) (1.0 - 4.0 * std::abs (phase - 0.5));
                case 2:  return (float) (2.0 * phase - 1.0);
                case 3:  return (float) (1.0 - 2.0 * phase);
                case 4:  return phase < 0.5 ? 1.0f : -1.0f;
                case 5:  return lfoHeld[(size_t) k];
                default: return lfoFrom[(size_t) k] + (lfoTo[(size_t) k] - lfoFrom[(size_t) k]) * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * phase));
            }
        }

        void modulators (int count) noexcept
        {
            const auto l1 = at[k_l1On];
            for (int k = 0; k < 2; ++k)
            {
                const auto base = l1 + k * 7;   // on, wave, target, rate, sync, division, depth
                if (v[(size_t) base] < 0.5f)
                    continue;
                const auto hz = v[(size_t) base + 4] > 0.5f
                                  ? bpm.load (std::memory_order_relaxed) / 60.0 / divisionBeats[juce::jlimit (0, 8, juce::roundToInt (v[(size_t) base + 5]))]
                                  : (double) v[(size_t) base + 3];
                auto& ph = lfoPhase[(size_t) k];
                ph += hz * count / sampleRate;
                if (ph >= 1.0)
                {
                    ph -= std::floor (ph);
                    lfoHeld[(size_t) k] = noise.next();
                    lfoFrom[(size_t) k] = lfoTo[(size_t) k];
                    lfoTo[(size_t) k] = noise.next();
                }
                const auto value = lfoValue (k, juce::roundToInt (v[(size_t) base + 1]), ph);
                lfoShown[(size_t) k] = value;
                modulate (juce::roundToInt (v[(size_t) base + 2]), value * v[(size_t) base + 6] * 0.005f);
            }
            const auto e = at[k_eOn];   // on, target, attack, release, depth
            if (v[(size_t) e] > 0.5f)
                modulate (juce::roundToInt (v[(size_t) e + 1]), std::min (1.0f, envelope * v[(size_t) e + 4] * 0.01f));
            envAttack = std::exp (-1.0f / (std::max (0.1f, v[(size_t) e + 2]) * 0.001f * (float) sampleRate));
            envRelease = std::exp (-1.0f / (std::max (1.0f, v[(size_t) e + 3]) * 0.001f * (float) sampleRate));
            addXy();
        }

        void configure() noexcept
        {
            inputGain = dbToGain (p (k_input));
            outputGain = dbToGain (p (k_output));
            wet = p (k_mix) * 0.01f;
            limiterMode = juce::roundToInt (p (k_limiter));
            const auto s1 = at[k_slot1];
            std::array<bool, 6> used {};
            orderCount = 0;
            for (int s = 0; s < 6; ++s)
            {
                const auto unit = juce::jlimit (0, 5, juce::roundToInt (v[(size_t) (s1 + s)]));
                if (! used[(size_t) unit]) { used[(size_t) unit] = true; order[(size_t) orderCount++] = unit; }
            }
            // Bandpass
            bpOn = p (k_bpOn) > 0.5f;
            const auto steep = juce::roundToInt (p (k_bpType)) == 1;
            const auto lowQ = 0.707 + p (k_bpLowRes) * 0.06, highQ = 0.707 + p (k_bpHighRes) * 0.06;
            const auto low = std::min (p (k_bpLow), p (k_bpHigh));
            for (int s = 0; s < 2; ++s)
            {
                if (s == 0 || steep)
                {
                    bandpass[(size_t) s * 2].setHighPass (sampleRate, low, s == 0 ? lowQ : 0.707);
                    bandpass[(size_t) s * 2 + 1].setLowPass (sampleRate, std::min ((double) p (k_bpHigh), sampleRate * 0.45), s == 0 ? highQ : 0.707);
                }
                else
                {
                    bandpass[2].setIdentity();
                    bandpass[3].setIdentity();
                }
            }
            reject = p (k_bpReject) * 0.01f;
            // Filter
            fOn = p (k_fOn) > 0.5f;
            filter.set (juce::roundToInt (p (k_fType)), p (k_fCutoff), p (k_fRes) * 0.01f, sampleRate);
            fDrive = 1.0f + p (k_fDrive) * 0.12f;
            fMix = p (k_fMix) * 0.01f;
            // Distortion
            dOn = p (k_dOn) > 0.5f;
            const auto d1 = at[k_dType1];
            for (int k = 0; k < 3; ++k)
            {
                dType[(size_t) k] = juce::roundToInt (v[(size_t) (d1 + k * 2)]);
                dAmount[(size_t) k] = v[(size_t) (d1 + k * 2 + 1)];
            }
            // Mod FX
            mOn = p (k_mOn) > 0.5f;
            mType = juce::roundToInt (p (k_mType));
            mMix = p (k_mMix) * 0.01f;
            mRate = p (k_mRate);
            // Bass Enhancer
            bOn = p (k_bOn) > 0.5f;
            bType = juce::roundToInt (p (k_bType));
            bAmount = p (k_bAmount) * 0.01f;
            const auto tune = (double) p (k_bTune);
            if (std::abs (tune - builtTune) > 0.01 || bType != builtType || std::abs (bAmount - builtAmount) > 0.001f)
            {
                bassBand.setBandPass (sampleRate, tune, 1.2);
                bassShelf.setLowShelf (sampleRate, tune * 1.5, 12.0 * bAmount, 0.8);
                bassLow.setLowPass (sampleRate, tune * 0.75, 0.707);
                builtTune = tune; builtType = bType; builtAmount = bAmount;
            }
            // Compressor
            cOn = p (k_cOn) > 0.5f;
            cType = juce::roundToInt (p (k_cType));
            cAmount = p (k_cAmount) * 0.01f;
            static constexpr float ratios[] { 4.0f, 1.0f, 3.0f, 4.0f, 8.0f, 4.0f, 3.0f, 12.0f, 3.0f };
            static constexpr float attacks[] { 5.0f, 0.1f, 3.0f, 2.0f, 0.3f, 1.0f, 8.0f, 0.2f, 10.0f };
            ratio = ratios[juce::jlimit (0, 8, cType)];
            threshold = -cAmount * 40.0f;
            cAttack = std::exp (-1.0f / (attacks[juce::jlimit (0, 8, cType)] * 0.001f * (float) sampleRate));
            cRelease = std::exp (-1.0f / (std::max (5.0f, p (k_cRelease)) * 0.001f * (float) sampleRate));
            makeup = dbToGain (-threshold * (1.0f - 1.0f / ratio) * 0.5f);
        }

        void processSample (float& l, float& r) noexcept
        {
            auto a = l * inputGain, b = r * inputGain;
            const auto dryA = a, dryB = b;
            // the envelope follower listens to the input
            const auto level = std::max (std::abs (a), std::abs (b));
            envelope = level > envelope ? level + envAttack * (envelope - level) : level + envRelease * (envelope - level);
            for (int u = 0; u < orderCount; ++u)
            {
                switch (order[(size_t) u])
                {
                    case 0: if (bpOn) { bandpassUnit (a, 0); bandpassUnit (b, 1); } break;
                    case 1: if (fOn) { a = filterUnit (a, 0); b = filterUnit (b, 1); } break;
                    case 2: if (dOn) for (int k = 0; k < 3; ++k) { a = shapers[(size_t) k][0].process (dType[(size_t) k], a, dAmount[(size_t) k], sampleRate, 0);
                                                                  b = shapers[(size_t) k][1].process (dType[(size_t) k], b, dAmount[(size_t) k], sampleRate, 1); } break;
                    case 3: if (mOn) modUnit (a, b); break;
                    case 4: if (bOn) bassUnit (a, b); break;
                    case 5: if (cOn) compressorUnit (a, b); break;
                    default: break;
                }
            }
            a = dryA + (a - dryA) * wet;
            b = dryB + (b - dryB) * wet;
            if (limiterMode == 1) { a = saturate (a); b = saturate (b); }
            else if (limiterMode == 2) { a = juce::jlimit (-0.99f, 0.99f, a); b = juce::jlimit (-0.99f, 0.99f, b); }
            l = a * outputGain;
            r = b * outputGain;
        }

        void bandpassUnit (float& x, int ch) noexcept
        {
            auto y = x;
            for (auto& f : bandpass)
                y = f.process (y, ch);
            x = y + (x - y) * reject;
        }
        float filterUnit (float x, int ch) noexcept
        {
            const auto driven = fDrive > 1.001f ? saturate (x * fDrive) / std::sqrt (fDrive) : x;
            return x + (filter.process (driven, ch) - x) * fMix;
        }
        void modUnit (float& a, float& b) noexcept
        {
            switch (mType)
            {
                case 0:  mod.process (a, b, sampleRate, mRate, 7.0f, 2.5f, 1, 0.0f, mMix * 0.5f, 0.25f); break;   // Chorus
                case 1:  mod.process (a, b, sampleRate, mRate, 9.0f, 3.5f, 2, 0.0f, mMix * 0.5f, 0.5f); break;    // Wide Chorus
                case 2:  mod.process (a, b, sampleRate, mRate, 12.0f, 4.0f, 3, 0.0f, mMix * 0.5f, 0.33f); break;  // Ensemble
                default: mod.process (a, b, sampleRate, mRate, 2.0f, 1.6f, 1, 0.65f, mMix * 0.5f, 0.25f); break;  // Flanger
            }
        }
        void bassUnit (float& a, float& b) noexcept
        {
            const auto mono = 0.5f * (a + b);
            float add = 0.0f;
            switch (bType)
            {
                case 0:   // Warm: a low shelf, then saturated
                    a = saturate (bassShelf.process (a, 0) * (1.0f + bAmount)) / (1.0f + 0.5f * bAmount);
                    b = saturate (bassShelf.process (b, 1) * (1.0f + bAmount)) / (1.0f + 0.5f * bAmount);
                    return;
                case 1:   // Punch: the band around Tune, driven and added
                    add = saturate (bassBand.process (mono, 0) * 4.0f) * bAmount * 0.6f;
                    break;
                default:  // Sub: an octave below, from the band's zero crossings
                {
                    const auto band = bassBand.process (mono, 0);
                    const auto sign = band >= 0.0f ? 1.0f : -1.0f;
                    if (sign > 0.0f && lastSign < 0.0f) flip = -flip;
                    lastSign = sign;
                    subLevel += (std::abs (band) - subLevel) * 0.002f;
                    add = bassLow.process (flip * subLevel * 1.6f, 0) * bAmount;
                    break;
                }
            }
            a += add;
            b += add;
        }
        void compressorUnit (float& a, float& b) noexcept
        {
            if (cType == 1)
            {
                // Clip: driven into a hard clip
                const auto drive = 1.0f + cAmount * 6.0f;
                a = juce::jlimit (-0.8f, 0.8f, a * drive) / std::sqrt (drive) * 1.1f;
                b = juce::jlimit (-0.8f, 0.8f, b * drive) / std::sqrt (drive) * 1.1f;
                return;
            }
            const auto level = gainToDb (std::max (std::abs (a), std::abs (b)) + 1.0e-9f);
            const auto target = -compressorGainDb (level, threshold, ratio, cType == 6 || cType == 8 ? 8.0f : 3.0f);
            compEnv = target > compEnv ? target + cAttack * (compEnv - target) : target + cRelease * (compEnv - target);
            const auto g = dbToGain (-compEnv) * makeup;
            a *= g;
            b *= g;
            if (cType >= 6) { a = saturate (a * 1.1f) / 1.1f; b = saturate (b * 1.1f) / 1.1f; }   // (the vintage ones colour it)
        }

        std::array<int, keyCount> at {};
        std::array<std::array<Shaper, 2>, 3> shapers;
        std::array<Biquad, 4> bandpass;
        MultiFilter filter;
        ModUnit mod;
        Biquad bassBand, bassShelf, bassLow;
        Noise noise;
        std::array<double, 2> lfoPhase {};
        std::array<float, 2> lfoHeld {}, lfoFrom {}, lfoTo {}, lfoShown {};
        float envelope = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
        float inputGain = 1.0f, outputGain = 1.0f, wet = 1.0f;
        int limiterMode = 1;
        std::array<int, 6> order {};
        int orderCount = 0;
        bool bpOn = false, fOn = false, dOn = false, mOn = false, bOn = false, cOn = false;
        float reject = 0.0f, fDrive = 1.0f, fMix = 1.0f;
        std::array<int, 3> dType {};
        std::array<float, 3> dAmount {};
        int mType = 0;
        float mMix = 0.5f, mRate = 0.5f;
        int bType = 0, builtType = -1;
        float bAmount = 0.0f, builtAmount = -1.0f, flip = 1.0f, lastSign = 0.0f, subLevel = 0.0f;
        double builtTune = -1.0;
        int cType = 0;
        float cAmount = 0.0f, ratio = 4.0f, threshold = 0.0f, cAttack = 0.0f, cRelease = 0.0f, makeup = 1.0f, compEnv = 0.0f;
    };

    std::vector<ParamInfo> phatParams()
    {
        const auto& t = phatTargets();
        const auto targetCount = (float) t.names.size() - 1.0f;
        std::vector<ParamInfo> p {
            { "input", "Input", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
            { "output", "Output", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
            { "mix", "Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f },
            { "limiter", "Limiter", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Off", "Soft", "Hard" } },
        };
        for (int s = 0; s < 6; ++s)
            p.push_back ({ "slot" + juce::String (s + 1), "Order " + juce::String (s + 1), "", 0.0f, 5.0f, (float) s, 2.5f, phatUnits });
        const std::vector<ParamInfo> units {
            { "bpOn", "Bandpass", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "bpType", "Bandpass Type", "", 0.0f, 1.0f, 0.0f, 0.5f, { "12 dB", "24 dB" } },
            { "bpLow", "Low", " Hz", 20.0f, 2000.0f, 40.0f, 200.0f },
            { "bpHigh", "High", " Hz", 200.0f, 20000.0f, 8000.0f, 2000.0f },
            { "bpLowRes", "Low Res", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "bpHighRes", "High Res", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "bpReject", "Reject Mix", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "fOn", "Filter", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
            { "fType", "Filter Type", "", 0.0f, 7.0f, 0.0f, 3.5f, filterTypes },
            { "fCutoff", "Cutoff", " Hz", 20.0f, 20000.0f, 2500.0f, 1000.0f },
            { "fRes", "Res", " %", 0.0f, 100.0f, 20.0f, 50.0f },
            { "fDrive", "Drive", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "fMix", "Filter Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f },
            { "dOn", "Distortion", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
            { "dType1", "Type 1", "", 0.0f, 10.0f, 9.0f, 5.0f, distortionTypes },
            { "dAmt1", "Amount 1", " %", 0.0f, 100.0f, 25.0f, 50.0f },
            { "dType2", "Type 2", "", 0.0f, 10.0f, 4.0f, 5.0f, distortionTypes },
            { "dAmt2", "Amount 2", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "dType3", "Type 3", "", 0.0f, 10.0f, 2.0f, 5.0f, distortionTypes },
            { "dAmt3", "Amount 3", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "mOn", "Mod FX", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "mType", "Mod FX Type", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Chorus", "Wide Chorus", "Ensemble", "Flanger" } },
            { "mMix", "Mod Mix", " %", 0.0f, 100.0f, 50.0f, 50.0f },
            { "mRate", "Mod Rate", " Hz", 0.05f, 10.0f, 0.5f, 1.0f },
            { "bOn", "Bass Enhancer", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "bType", "Bass Type", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Warm", "Punch", "Sub" } },
            { "bAmount", "Bass Amount", " %", 0.0f, 100.0f, 40.0f, 50.0f },
            { "bTune", "Tune", " Hz", 30.0f, 200.0f, 80.0f, 80.0f },
            { "cOn", "Compressor", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
            { "cType", "Compressor Type", "", 0.0f, 8.0f, 0.0f, 4.0f, { "Classic", "Clip", "Platinum", "Studio VCA", "Studio FET", "Classic VCA", "Vintage VCA", "Vintage FET", "Vintage Opto" } },
            { "cAmount", "Comp Amount", " %", 0.0f, 100.0f, 30.0f, 50.0f },
            { "cRelease", "Comp Release", " ms", 10.0f, 1000.0f, 120.0f, 120.0f },
        };
        p.insert (p.end(), units.begin(), units.end());
        for (int k = 1; k <= 2; ++k)
        {
            const auto n = juce::String (k), name = "LFO " + n + " ";
            p.push_back ({ "l" + n + "On", "LFO " + n, "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
            p.push_back ({ "l" + n + "Wave", name + "Waveform", "", 0.0f, 6.0f, 0.0f, 3.0f, lfoWaves });
            p.push_back ({ "l" + n + "Target", name + "Target", "", 0.0f, targetCount, k == 1 ? 7.0f : 0.0f, 1.0f, t.names });
            p.push_back ({ "l" + n + "Rate", name + "Rate", " Hz", 0.05f, 20.0f, k == 1 ? 1.0f : 0.3f, 1.0f });
            p.push_back ({ "l" + n + "Sync", name + "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff });
            p.push_back ({ "l" + n + "Div", name + "Sync Rate", "", 0.0f, 8.0f, 2.0f, 4.0f, divisions });
            p.push_back ({ "l" + n + "Depth", name + "Depth", " %", -100.0f, 100.0f, 30.0f, 0.0f });
        }
        const std::vector<ParamInfo> env {
            { "eOn", "Envelope Follower", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "eTarget", "Envelope Target", "", 0.0f, targetCount, 7.0f, 1.0f, t.names },
            { "eAttack", "Attack", " ms", 0.1f, 200.0f, 5.0f, 20.0f },
            { "eRelease", "Release", " ms", 5.0f, 2000.0f, 150.0f, 200.0f },
            { "eDepth", "Envelope Depth", " %", 0.0f, 1000.0f, 100.0f, 200.0f },
        };
        p.insert (p.end(), env.begin(), env.end());
        addXyParams (p, t);
        return p;
    }

    //==============================================================================
    // Step FX
    //==============================================================================
    const Targets& stepTargets()
    {
        static const Targets t {
            { "Off", "Gate (Mix)", "Gate (Add)", "Filter Cutoff", "Filter Res", "Filter Mix", "Exciter", "Dirt", "Mod FX Rate", "Mod FX Depth",
              "Mod FX Feedback", "Mod FX Mix", "Delay Feedback", "Delay Cutoff", "Delay Mix", "Reverb Time", "Reverb Mix", "Mix", "Output" },
            { "", "", "", "fltCutoff", "fltRes", "fltMix", "distExciter", "distDirt", "modRate", "modDepth",
              "modFeedback", "modMix", "dlyFeedback", "dlyCutoff", "dlyMix", "revTime", "revMix", "mix", "output" } };
        return t;
    }

    const std::vector<juce::String> stepStates { "Off", "25", "50", "75", "100", "Tie" };
    constexpr int stepsAt = 9;   // within a step modulator: on, target, depth, rate, swing, attack, hold, release, length, then levels and states

    class StepFx final : public ModulatedEffect
    {
    public:
        enum Key { k_s1On, k_input, k_output, k_mix, k_fltOn, k_fltType, k_fltCutoff, k_fltRes, k_fltMix, k_distOn, k_distExciter, k_distDirt, k_modOn, k_modDelay, k_modRate, k_modDepth, k_modFeedback, k_modMix, k_dlyOn, k_dlyFilter, k_dlyFeedback, k_dlyMix, k_dlySync, k_dlyDivL, k_dlyTimeL, k_dlyDivR, k_dlyTimeR, k_dlyCutoff, k_revOn, k_revTime, k_revMix, k_revType, keyCount };
        static constexpr const char* keyNames[] { "s1On", "input", "output", "mix", "fltOn", "fltType", "fltCutoff", "fltRes", "fltMix", "distOn", "distExciter", "distDirt", "modOn", "modDelay", "modRate", "modDepth", "modFeedback", "modMix", "dlyOn", "dlyFilter", "dlyFeedback", "dlyMix", "dlySync", "dlyDivL", "dlyTimeL", "dlyDivR", "dlyTimeR", "dlyCutoff", "revOn", "revTime", "revMix", "revType" };
        explicit StepFx (const EffectInfo& i) : ModulatedEffect (i, stepTargets())
        {
            xy = idx ("xyX");
            for (int k = 0; k < keyCount; ++k)
                at[(size_t) k] = (int) indexOf (info, keyNames[k]);
        }

        static int modulatorBase (int s1, int k) noexcept { return s1 + k * (stepsAt + 2 * stepFxMaxSteps); }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            filter.reset();
            for (auto* set : { &exciter, &dirt }) for (auto& sh : *set) sh.reset();
            mod.prepare (rate);
            for (auto& l : delay) l.prepare ((int) (4.2 * rate));
            for (auto& pair : delayFilter) for (auto& f : pair) f.reset();
            for (auto& l : delay) l.clear();
            reverb.prepare (rate);
            freeBeats = 0.0;
            gateNow = 1.0f;
            for (auto& s : stepShown) s.store (-1);
        }

        /** The step each modulator is on (for its window), and its value: 0-2 steps, 3-5 values. */
        int readMeters (float* dest, int capacity) const noexcept override
        {
            if (capacity < 6)
                return 0;
            for (size_t k = 0; k < 3; ++k)
            {
                dest[k] = (float) stepShown[k].load (std::memory_order_relaxed);
                dest[3 + k] = stepValue[k].load (std::memory_order_relaxed);
            }
            return 6;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto tempo = bpm.load (std::memory_order_relaxed);
            const auto isPlaying = playing.load (std::memory_order_relaxed);
            const auto beatsPerSample = tempo / 60.0 / sampleRate;
            auto beat = isPlaying ? (double) position.load (std::memory_order_relaxed) * beatsPerSample : freeBeats;
            for (int start = 0; start < n; start += chunk)
            {
                const auto count = std::min (chunk, n - start);
                beginChunk();
                float gateTarget = 1.0f;
                stepModulators (beat, gateTarget);
                addXy();
                applyModulation();
                configure (tempo);
                const auto gateFrom = gateNow;
                for (int i = 0; i < count; ++i)
                {
                    gateNow = gateFrom + (gateTarget - gateFrom) * (float) (i + 1) / (float) count;
                    processSample (l[start + i], r[start + i]);
                }
                beat += count * beatsPerSample;
            }
            freeBeats = beat;
        }

    private:
        float p (Key key) const noexcept { return v[(size_t) at[(size_t) key]]; }

        /** A step modulator's value now (0..1), from its steps, swing and envelope. */
        float stepValue01 (int base, double beat, int k) noexcept
        {
            const auto length = juce::jlimit (1, stepFxMaxSteps, juce::roundToInt (v[(size_t) base + 8]) + 1);
            const auto stepBeats = divisionBeats[juce::jlimit (0, 8, juce::roundToInt (v[(size_t) base + 3]))];
            const auto swing = v[(size_t) base + 4] * 0.01;
            // pairs of steps: the first lengthened by the swing, the second shortened
            const auto pair = 2.0 * stepBeats;
            const auto inPattern = std::fmod (std::max (0.0, beat), pair * std::ceil (length / 2.0));
            auto step = 2 * (int) (inPattern / pair);
            auto within = std::fmod (inPattern, pair);
            const auto first = stepBeats * (1.0 + swing * 0.5);
            double stepLength = first;
            if (within >= first) { ++step; within -= first; stepLength = pair - first; }
            step %= length;
            stepShown[(size_t) k].store (step, std::memory_order_relaxed);
            const auto state = [&] (int s) { return juce::roundToInt (v[(size_t) (base + stepsAt + stepFxMaxSteps + s)]); };
            const auto level = [&] (int s) { return v[(size_t) (base + stepsAt + s)] * 0.01f; };
            // a tie carries the step before on
            auto chainStart = step;
            while (chainStart > 0 && state (chainStart - 1) == 5)
                --chainStart;
            if (chainStart == step && state (step) == 0)
                return 0.0f;
            auto chainEnd = chainStart;
            while (chainEnd < length - 1 && state (chainEnd) == 5)
                ++chainEnd;
            const auto endState = state (chainEnd);
            const auto lastPart = endState == 0 || endState >= 4 ? 1.0 : endState * 0.25;
            const auto gate = (chainEnd - chainStart) + lastPart;                          // in steps
            const auto t = (step - chainStart) + within / std::max (1.0e-9, stepLength);   // in steps
            const auto attack = v[(size_t) base + 5] * 0.01, hold = v[(size_t) base + 6] * 0.01, release = v[(size_t) base + 7] * 0.01;
            const auto held = std::max (attack, gate * hold);
            double env;
            if (t < attack) env = t / attack;
            else if (t < held) env = 1.0;
            else if (release > 1.0e-6 && t < held + release) env = 1.0 - (t - held) / release;
            else env = 0.0;
            return level (chainStart) * (float) env;
        }

        void stepModulators (double beat, float& gate) noexcept
        {
            const auto s1 = at[k_s1On];
            for (int k = 0; k < 3; ++k)
            {
                const auto base = modulatorBase (s1, k);
                if (v[(size_t) base] < 0.5f)
                {
                    stepShown[(size_t) k].store (-1, std::memory_order_relaxed);
                    continue;
                }
                const auto value = stepValue01 (base, beat, k);
                stepValue[(size_t) k].store (value, std::memory_order_relaxed);
                const auto target = juce::roundToInt (v[(size_t) base + 1]);
                const auto depth = v[(size_t) base + 2] * 0.01f;
                if (target == 1)        // Gate (Mix): 0 % no change, 100 % the steps alone
                    gate *= 1.0f - std::abs (depth) * (1.0f - value);
                else if (target == 2)   // Gate (Add): 0 % silent, 100 % the steps alone
                    gate *= juce::jlimit (0.0f, 1.0f, value + depth - 1.0f);
                else
                    modulate (target, value * depth);
            }
        }

        void configure (double tempo) noexcept
        {
            inputGain = dbToGain (p (k_input));
            outputGain = dbToGain (p (k_output));
            wet = p (k_mix) * 0.01f;
            fltOn = p (k_fltOn) > 0.5f;
            filter.set (juce::roundToInt (p (k_fltType)), p (k_fltCutoff), p (k_fltRes) * 0.01f, sampleRate);
            fltMix = p (k_fltMix) * 0.01f;
            distOn = p (k_distOn) > 0.5f;
            exciterAmount = p (k_distExciter);
            dirtAmount = p (k_distDirt);
            modOn = p (k_modOn) > 0.5f;
            modDelay = p (k_modDelay);
            modRate = p (k_modRate);
            modDepth = p (k_modDepth) * 0.01f;
            modFeedback = p (k_modFeedback) * 0.01f;
            modMix = p (k_modMix) * 0.01f;
            dlyOn = p (k_dlyOn) > 0.5f;
            dlyType = juce::roundToInt (p (k_dlyFilter));
            dlyFeedback = p (k_dlyFeedback) * 0.01f;
            dlyMix = p (k_dlyMix) * 0.01f;
            const auto synced = p (k_dlySync) > 0.5f;
            const auto beatMs = 60000.0 / std::max (20.0, tempo);
            dlyTime[0] = (float) ((synced ? divisionBeats[juce::jlimit (0, 8, juce::roundToInt (p (k_dlyDivL)))] * beatMs : p (k_dlyTimeL)) * 0.001 * sampleRate);
            dlyTime[1] = (float) ((synced ? divisionBeats[juce::jlimit (0, 8, juce::roundToInt (p (k_dlyDivR)))] * beatMs : p (k_dlyTimeR)) * 0.001 * sampleRate);
            const auto cutoff = (double) p (k_dlyCutoff);
            for (int c = 0; c < 2; ++c)
            {
                auto& f = delayFilter[(size_t) c];
                switch (dlyType)
                {
                    case 0:  f[0].setLowPass (sampleRate, std::min (cutoff, sampleRate * 0.45), 0.707); f[1].setIdentity(); break;
                    case 1:  f[0].setBandPass (sampleRate, std::min (cutoff, sampleRate * 0.45), 1.0); f[1].setIdentity(); break;
                    case 2:  f[0].setHighPass (sampleRate, std::min (cutoff, sampleRate * 0.45), 0.707); f[1].setIdentity(); break;
                    default: f[0].setLowPass (sampleRate, std::min (cutoff, sampleRate * 0.45), 0.707); f[1].setHighPass (sampleRate, 250.0, 0.707); break;
                }
            }
            revOn = p (k_revOn) > 0.5f;
            revTime = p (k_revTime);
            revMix = p (k_revMix) * 0.01f;
            const auto type = juce::roundToInt (p (k_revType));
            if (type != revType) { revType = type; reverb.set (type, sampleRate); }
        }

        void processSample (float& l, float& r) noexcept
        {
            auto a = l * inputGain, b = r * inputGain;
            const auto dryA = a, dryB = b;
            if (fltOn)
            {
                a += (filter.process (a, 0) - a) * fltMix;
                b += (filter.process (b, 1) - b) * fltMix;
            }
            if (distOn)
            {
                a = dirt[0].process (2, exciter[0].process (4, a, exciterAmount, sampleRate, 0), dirtAmount, sampleRate, 0);
                b = dirt[1].process (2, exciter[1].process (4, b, exciterAmount, sampleRate, 1), dirtAmount, sampleRate, 1);
            }
            if (modOn)
                mod.process (a, b, sampleRate, modRate, modDelay, modDelay * 0.8f * modDepth, 1, modFeedback * 0.9f, modMix * 0.5f, 0.25f);
            if (dlyOn)
            {
                const auto dl = delay[0].read (dlyTime[0]), dr = delay[1].read (dlyTime[1]);
                const auto fl = delayFilter[0][1].process (delayFilter[0][0].process (dl, 0), 0);
                const auto fr = delayFilter[1][1].process (delayFilter[1][0].process (dr, 1), 1);
                delay[0].push (a + saturate (fl * dlyFeedback));
                delay[1].push (b + saturate (fr * dlyFeedback));
                a += (fl - a) * dlyMix;
                b += (fr - b) * dlyMix;
            }
            if (revOn)
                reverb.process (a, b, revTime, revMix, sampleRate);
            a *= gateNow;
            b *= gateNow;
            l = (dryA + (a - dryA) * wet) * outputGain;
            r = (dryB + (b - dryB) * wet) * outputGain;
        }

        std::array<int, keyCount> at {};
        MultiFilter filter;
        std::array<Shaper, 2> exciter, dirt;
        ModUnit mod;
        std::array<DelayLine, 2> delay;
        std::array<std::array<Biquad, 2>, 2> delayFilter;
        SmallReverb reverb;
        double freeBeats = 0.0;
        float gateNow = 1.0f;
        std::array<std::atomic<int>, 3> stepShown {};
        std::array<std::atomic<float>, 3> stepValue {};
        float inputGain = 1.0f, outputGain = 1.0f, wet = 1.0f;
        bool fltOn = false, distOn = false, modOn = false, dlyOn = false, revOn = false;
        float fltMix = 1.0f, exciterAmount = 0.0f, dirtAmount = 0.0f;
        float modDelay = 5.0f, modRate = 0.5f, modDepth = 0.5f, modFeedback = 0.0f, modMix = 0.5f;
        int dlyType = 0, revType = -1;
        float dlyFeedback = 0.3f, dlyMix = 0.3f, revTime = 1.5f, revMix = 0.2f;
        std::array<float, 2> dlyTime { 1000.0f, 1000.0f };
    };

    std::vector<ParamInfo> stepParams()
    {
        const auto& t = stepTargets();
        const auto targetCount = (float) t.names.size() - 1.0f;
        std::vector<ParamInfo> p {
            { "input", "Input", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
            { "mix", "Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f },
            { "output", "Output", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
            { "view", "Step Modulator", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Step 1", "Step 2", "Step 3" } },
            { "fltOn", "Filter", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
            { "fltType", "Filter Type", "", 0.0f, 7.0f, 0.0f, 3.5f, filterTypes },
            { "fltCutoff", "Cutoff", " Hz", 20.0f, 20000.0f, 1200.0f, 1000.0f },
            { "fltRes", "Res", " %", 0.0f, 100.0f, 30.0f, 50.0f },
            { "fltMix", "Filter Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f },
            { "distOn", "Distortion", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "distExciter", "Exciter", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "distDirt", "Dirt", " %", 0.0f, 100.0f, 30.0f, 50.0f },
            { "modOn", "Mod FX", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "modDelay", "Mod Delay", " ms", 0.5f, 30.0f, 6.0f, 6.0f },
            { "modRate", "Mod Rate", " Hz", 0.05f, 10.0f, 0.4f, 1.0f },
            { "modDepth", "Mod Depth", " %", 0.0f, 100.0f, 50.0f, 50.0f },
            { "modFeedback", "Mod Feedback", " %", -100.0f, 100.0f, 0.0f, 0.0f },
            { "modMix", "Mod Mix", " %", 0.0f, 100.0f, 50.0f, 50.0f },
            { "dlyOn", "Delay", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "dlyFilter", "Delay Filter", "", 0.0f, 3.0f, 0.0f, 1.5f, { "LP", "BP", "HP", "LP + LoCut" } },
            { "dlyFeedback", "Delay Feedback", " %", 0.0f, 100.0f, 35.0f, 50.0f },
            { "dlyCutoff", "Delay Cutoff", " Hz", 100.0f, 20000.0f, 4000.0f, 2000.0f },
            { "dlyMix", "Delay Mix", " %", 0.0f, 100.0f, 30.0f, 50.0f },
            { "dlySync", "Delay Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
            { "dlyTimeL", "Time L", " ms", 1.0f, 2000.0f, 375.0f, 300.0f },
            { "dlyTimeR", "Time R", " ms", 1.0f, 2000.0f, 500.0f, 300.0f },
            { "dlyDivL", "Time L (Sync)", "", 0.0f, 8.0f, 3.0f, 4.0f, divisions },
            { "dlyDivR", "Time R (Sync)", "", 0.0f, 8.0f, 2.0f, 4.0f, divisions },
            { "revOn", "Reverb", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "revType", "Reverb Type", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Room", "Hall", "Plate" } },
            { "revTime", "Reverb Time", " s", 0.2f, 10.0f, 1.5f, 1.5f },
            { "revMix", "Reverb Mix", " %", 0.0f, 100.0f, 20.0f, 50.0f },
        };
        addXyParams (p, t);
        std::vector<juce::String> lengths;
        for (int c = 1; c <= stepFxMaxSteps; ++c)
            lengths.push_back (juce::String (c));
        for (int k = 1; k <= 3; ++k)
        {
            const auto n = juce::String (k), name = "Step " + n + " ";
            p.push_back ({ "s" + n + "On", "Step Modulator " + n, "", 0.0f, 1.0f, k == 1 ? 1.0f : 0.0f, 0.5f, onOff });
            p.push_back ({ "s" + n + "Target", name + "Target", "", 0.0f, targetCount, k == 1 ? 1.0f : k == 2 ? 3.0f : 0.0f, 1.0f, t.names });
            p.push_back ({ "s" + n + "Depth", name + "Depth", " %", -100.0f, 100.0f, k == 1 ? 100.0f : 50.0f, 0.0f });
            p.push_back ({ "s" + n + "Rate", name + "Rate", "", 0.0f, 8.0f, 5.0f, 4.0f, divisions });
            p.push_back ({ "s" + n + "Swing", name + "Swing", " %", 0.0f, 100.0f, 0.0f, 50.0f });
            p.push_back ({ "s" + n + "Attack", name + "Attack", " %", 0.0f, 100.0f, 0.0f, 50.0f });
            p.push_back ({ "s" + n + "Hold", name + "Hold", " %", 0.0f, 100.0f, 100.0f, 50.0f });
            p.push_back ({ "s" + n + "Release", name + "Release", " %", 0.0f, 100.0f, 10.0f, 50.0f });
            p.push_back ({ "s" + n + "Length", name + "Length", "", 0.0f, (float) stepFxMaxSteps - 1.0f, 15.0f, 15.5f, lengths });
            // A pattern to start from: on the beats loud, offbeats softer, every fourth a rest (the second modulator a rising ramp)
            for (int s = 0; s < stepFxMaxSteps; ++s)
                p.push_back ({ "s" + n + "L" + juce::String (s + 1), name + juce::String (s + 1) + " Level", " %", 0.0f, 100.0f,
                               k == 2 ? (float) ((s % 8) + 1) * 12.5f : s % 4 == 0 ? 100.0f : s % 2 == 0 ? 70.0f : 45.0f, 50.0f });
            for (int s = 0; s < stepFxMaxSteps; ++s)
                p.push_back ({ "s" + n + "S" + juce::String (s + 1), name + juce::String (s + 1) + " Length", "", 0.0f, 5.0f,
                               k == 1 && s % 4 == 3 ? 0.0f : 3.0f, 2.5f, stepStates });
        }
        return p;
    }

    //==============================================================================
    // Remix FX
    //==============================================================================
    const std::vector<juce::String> padEffects { "Filter", "Repeater", "Wobble", "Reverb", "Orbit", "Delay" };
    const std::vector<juce::String> buttonDivisions { "1/1", "1/2", "1/4", "1/8", "1/16" };
    constexpr double buttonBeats[] { 4.0, 2.0, 1.0, 0.5, 0.25 };

    /** Logic's DJ-style multi-effect: two XY pads, a gater and a downsampler, reverse, scratch and tape stop. */
    class RemixFx final : public BuiltInEffect
    {
    public:
        enum Key { k_reverse, k_scratch, k_tapeStop, k_reverseDiv, k_scratchDiv, k_tapeStopDiv, k_gater, k_gaterRate, k_downsampler, k_downType, k_padAEffect, k_padBEffect, k_padAOn, k_padBOn, k_padALock, k_padBLock, k_padAX, k_padBX, k_padAY, k_padBY, k_filterType, k_reverbColor, k_orbitType, keyCount };
        static constexpr const char* keyNames[] { "reverse", "scratch", "tapeStop", "reverseDiv", "scratchDiv", "tapeStopDiv", "gater", "gaterRate", "downsampler", "downType", "padAEffect", "padBEffect", "padAOn", "padBOn", "padALock", "padBLock", "padAX", "padBX", "padAY", "padBY", "filterType", "reverbColor", "orbitType" };
        explicit RemixFx (const EffectInfo& i) : BuiltInEffect (i)
        {
            for (int k = 0; k < keyCount; ++k)
                at[(size_t) k] = (int) indexOf (info, keyNames[k]);
        }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& b : history) b.assign ((size_t) (8.5 * rate), 0.0f);
            write = 0;
            for (auto& pad : pads) pad = {};
            for (auto& pad : pads) { pad.reverb.prepare (rate); pad.reverb.set (1, rate); pad.orbit.prepare (rate); for (auto& d : pad.echo) d.prepare ((int) (2.1 * rate)); }
            gatePhase = 0.0;
            held = { 0.0f, 0.0f };
            holdCount = 0.0f;
            buttons = {};
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto tempo = std::max (20.0, bpm.load (std::memory_order_relaxed));
            const auto beatSamples = 60.0 / tempo * sampleRate;
            const auto size = (int) history[0].size();
            // The buttons: each takes over the sound from when it was pressed
            const int downKeys[] { at[k_reverse], at[k_scratch], at[k_tapeStop] }, lengthKeys[] { at[k_reverseDiv], at[k_scratchDiv], at[k_tapeStopDiv] };
            for (size_t b = 0; b < 3; ++b)
            {
                const auto down = value (downKeys[b]) > 0.5f;
                auto& state = buttons[b];
                if (down && ! state.active) { state.active = true; state.start = write; state.t = 0.0; state.played = 0.0; }
                if (! down) state.active = false;
                state.length = buttonBeats[juce::jlimit (0, 4, choice (lengthKeys[b]))] * beatSamples;
            }
            const auto gaterAmount = value (at[k_gater]) * 0.01f;
            const auto gaterSamples = divisionBeats[juce::jlimit (0, 8, choice (at[k_gaterRate]))] * beatSamples;
            const auto downAmount = value (at[k_downsampler]) * 0.01f;
            const auto downType = choice (at[k_downType]);
            for (size_t k = 0; k < 2; ++k)
                configurePad (k, beatSamples);

            for (int i = 0; i < n; ++i)
            {
                history[0][(size_t) write] = l[i];
                history[1][(size_t) write] = r[i];
                auto a = l[i], b = r[i];
                // Reverse: the beat (or so) from the press, backwards, again and again
                if (auto& s = buttons[0]; s.active)
                {
                    const auto back = std::fmod (s.t, s.length);
                    a = readAt (0, (double) s.start - back, size);
                    b = readAt (1, (double) s.start - back, size);
                    s.t += 1.0;
                }
                // Scratch: the record pushed back and forth around where it was pressed
                if (auto& s = buttons[1]; s.active)
                {
                    const auto cycle = s.t / s.length;
                    const auto offset = (1.0 - std::cos (juce::MathConstants<double>::twoPi * cycle)) * 0.5 * s.length * 0.5;
                    a = readAt (0, (double) s.start - offset, size);
                    b = readAt (1, (double) s.start - offset, size);
                    s.t += 1.0;
                }
                // Tape Stop: slowing to a halt over the length, then silence
                if (auto& s = buttons[2]; s.active)
                {
                    const auto progress = std::min (1.0, s.t / s.length);
                    const auto speed = 1.0 - progress;
                    s.played += speed;
                    const auto fade = (float) std::min (1.0, speed * 8.0);
                    a = readAt (0, (double) s.start + s.played, size) * fade;
                    b = readAt (1, (double) s.start + s.played, size) * fade;
                    s.t += 1.0;
                }
                // Gater: on for the first half of each division
                if (gaterAmount > 0.0f)
                {
                    gatePhase += 1.0 / gaterSamples;
                    gatePhase -= std::floor (gatePhase);
                    const auto open = gatePhase < 0.5 ? 1.0f : 1.0f - gaterAmount;
                    gateGain += (open - gateGain) * 0.02f;
                    a *= gateGain;
                    b *= gateGain;
                }
                // Downsampler: fewer samples (and bits), more as it's pushed up
                if (downAmount > 0.0f)
                {
                    const auto factor = 1.0f + downAmount * downAmount * (downType == 2 ? 60.0f : 30.0f);
                    if ((holdCount += 1.0f) >= factor) { holdCount -= factor; held = { a, b }; }
                    a = held[0];
                    b = held[1];
                    if (downType >= 1)
                    {
                        const auto q = std::pow (2.0f, 14.0f - downAmount * (downType == 2 ? 11.0f : 9.0f));
                        a = std::round (a * q) / q;
                        b = std::round (b * q) / q;
                    }
                }
                for (size_t k = 0; k < 2; ++k)
                    processPad (k, a, b, size);
                l[i] = a;
                r[i] = b;
                write = (write + 1) % size;
            }
        }

    private:
        struct Button { bool active = false; int start = 0; double t = 0.0, length = 1.0, played = 0.0; };
        struct Pad
        {
            int effect = 0;
            bool active = false, wasActive = false;
            float x = 0.5f, y = 0.5f;
            int captureStart = 0;
            double repeat = 1.0, repeatT = 0.0;
            MultiFilter filter;
            double wobblePhase = 0.0, wobbleHz = 2.0;
            SmallReverb reverb;
            ModUnit orbit;
            std::array<DelayLine, 2> echo;
            double echoSamples = 1.0;
        };

        float readAt (int ch, double at, int size) const noexcept
        {
            const auto wrapped = std::fmod (at, (double) size) + (at < 0.0 ? size : 0.0);
            const auto i0 = (int) wrapped % size;
            const auto frac = (float) (wrapped - std::floor (wrapped));
            const auto& h = history[(size_t) ch];
            return h[(size_t) i0] + frac * (h[(size_t) ((i0 + 1) % size)] - h[(size_t) i0]);
        }

        void configurePad (size_t k, double beatSamples) noexcept
        {
            const auto a = k == 0;
            auto& pad = pads[k];
            pad.effect = choice (a ? at[k_padAEffect] : at[k_padBEffect]);
            pad.active = value (a ? at[k_padAOn] : at[k_padBOn]) > 0.5f || value (a ? at[k_padALock] : at[k_padBLock]) > 0.5f;
            pad.x = value (a ? at[k_padAX] : at[k_padBX]);
            pad.y = value (a ? at[k_padAY] : at[k_padBY]);
            if (pad.active && ! pad.wasActive) { pad.captureStart = write; pad.repeatT = 0.0; }
            pad.wasActive = pad.active;
            if (! pad.active)
                return;
            switch (pad.effect)
            {
                case 0:   // Filter: left of the middle a low pass coming down, right a high pass going up; up: resonance
                {
                    const auto phat = choice (at[k_filterType]) == 0;
                    const auto side = pad.x - 0.5f;
                    const auto cutoff = side < 0.0f ? 20000.0 * std::pow (0.0015, (double) -side * 2.0) : 20.0 * std::pow (600.0, (double) side * 2.0);
                    pad.filter.set (side < 0.0f ? (phat ? 4 : 1) : (phat ? 5 : 3), cutoff, pad.y * 0.85f, sampleRate);
                    break;
                }
                case 1:   // Repeater: across, the length repeated (1/2 to 1/32)
                {
                    static constexpr double lengths[] { 2.0, 1.0, 0.5, 0.25, 0.125 };
                    pad.repeat = lengths[juce::jlimit (0, 4, (int) (pad.x * 5.0f))] * beatSamples;
                    break;
                }
                case 2:   // Wobble: across, the speed (in time); up, the depth
                {
                    static constexpr double beats[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
                    pad.wobbleHz = sampleRate / (beats[juce::jlimit (0, 5, (int) (pad.x * 6.0f))] * beatSamples);
                    break;
                }
                case 3: pad.reverb.set (juce::jlimit (0, 2, 2 - choice (at[k_reverbColor])), sampleRate); break;
                case 5:
                {
                    static constexpr double beats[] { 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 0.125 };
                    pad.echoSamples = beats[juce::jlimit (0, 5, (int) (pad.x * 6.0f))] * beatSamples;
                    break;
                }
                default: break;
            }
        }

        void processPad (size_t k, float& a, float& b, int size) noexcept
        {
            auto& pad = pads[k];
            if (! pad.active)
                return;
            switch (pad.effect)
            {
                case 0:
                    if (std::abs (pad.x - 0.5f) > 0.02f) { a = pad.filter.process (a, 0); b = pad.filter.process (b, 1); }
                    break;
                case 1:
                {
                    const auto t = std::fmod (pad.repeatT, pad.repeat);
                    const auto at = (double) pad.captureStart - pad.repeat + t;
                    const auto fade = (float) std::min (1.0, std::min (t, pad.repeat - t) / 64.0);
                    const auto mix = 0.4f + 0.6f * pad.y;
                    a += (readAt (0, at, size) * fade - a) * mix;
                    b += (readAt (1, at, size) * fade - b) * mix;
                    pad.repeatT += 1.0;
                    break;
                }
                case 2:
                {
                    pad.wobblePhase += pad.wobbleHz / sampleRate;
                    pad.wobblePhase -= std::floor (pad.wobblePhase);
                    const auto lfo = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * pad.wobblePhase);
                    pad.filter.set (0, 150.0 * std::pow (60.0, lfo * (0.3 + 0.7 * pad.y)), 0.55f, sampleRate);
                    a = pad.filter.process (a, 0);
                    b = pad.filter.process (b, 1);
                    break;
                }
                case 3: pad.reverb.process (a, b, 0.5f + pad.x * 7.5f, pad.y * 0.85f, sampleRate); break;
                case 4:
                {
                    const auto flanger = choice (at[k_orbitType]) != 1;
                    pad.orbit.process (a, b, sampleRate, 0.1 + pad.x * 4.0, flanger ? 2.5f : 6.0f, flanger ? 2.0f : 4.0f, flanger ? 1 : 2,
                                       flanger ? 0.7f * pad.y : 0.0f, 0.5f, 0.25f);
                    break;
                }
                default:
                {
                    const auto el = pad.echo[0].read ((float) pad.echoSamples), er = pad.echo[1].read ((float) pad.echoSamples);
                    const auto fb = 0.25f + 0.65f * pad.y;
                    pad.echo[0].push (a + er * fb);   // (ping pong)
                    pad.echo[1].push (b + el * fb);
                    a += el * 0.6f;
                    b += er * 0.6f;
                    break;
                }
            }
        }

        std::array<int, keyCount> at {};
        std::array<std::vector<float>, 2> history;
        int write = 0;
        std::array<Pad, 2> pads;
        std::array<Button, 3> buttons {};
        double gatePhase = 0.0;
        float gateGain = 1.0f, holdCount = 0.0f;
        std::array<float, 2> held {};
    };

    std::vector<ParamInfo> remixParams()
    {
        std::vector<ParamInfo> p {
            { "gater", "Gater", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "gaterRate", "Gater Rate", "", 0.0f, 8.0f, 5.0f, 4.0f, divisions },
            { "downsampler", "Downsampler", " %", 0.0f, 100.0f, 0.0f, 50.0f },
            { "downType", "Downsampler Type", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Downsample", "Crush", "Lo-Fi" } },
            { "reverse", "Reverse", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "reverseDiv", "Reverse Length", "", 0.0f, 4.0f, 2.0f, 2.0f, buttonDivisions },
            { "scratch", "Scratch", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "scratchDiv", "Scratch Speed", "", 0.0f, 4.0f, 3.0f, 2.0f, buttonDivisions },
            { "tapeStop", "Tape Stop", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "tapeStopDiv", "Tape Stop Length", "", 0.0f, 4.0f, 1.0f, 2.0f, buttonDivisions },
        };
        for (const char* pad : { "A", "B" })
        {
            const auto n = juce::String ("pad") + pad, name = juce::String ("Pad ") + pad + " ";
            p.push_back ({ n + "Effect", name + "Effect", "", 0.0f, 5.0f, juce::String (pad) == "A" ? 0.0f : 1.0f, 2.5f, padEffects });
            p.push_back ({ n + "X", name + "X", "", 0.0f, 1.0f, 0.5f, 0.5f });
            p.push_back ({ n + "Y", name + "Y", "", 0.0f, 1.0f, 0.5f, 0.5f });
            p.push_back ({ n + "On", name + "Active", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
            p.push_back ({ n + "Lock", name + "Lock", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
        }
        p.push_back ({ "filterType", "Filter Type", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Phat 24 dB", "Classic 12 dB" } });
        p.push_back ({ "reverbColor", "Reverb Color", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Dark", "Medium", "Bright" } });
        p.push_back ({ "orbitType", "Orbit Type", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Flanger", "Phaser" } });
        return p;
    }

    //==============================================================================
    // Beat Breaker
    //==============================================================================
    /** Beat Breaker: the incoming audio, a pattern long, played back in slices - each from its own place in the
        input, at its own speed (backwards too), repeated, filtered, faded and panned. */
    class BeatBreaker final : public BuiltInEffect
    {
    public:
        enum Global { length, declick, bypassOn, bypassFreq, mix, mode, selected, sliceCount, pattern, patternLoaded, patternOff, randomize, randomAll, globals };
        enum Slice { outBeat, inBeat, speed, curve, repeat, cutoff, reso, volume, slope, pan, perSlice };
        static constexpr int maxSlices = 32;
        static int index (int slice, Slice p) noexcept { return globals + slice * perSlice + (int) p; }
        static constexpr double lengthBeats[] { 1.0, 2.0, 4.0, 8.0, 16.0 };

        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& h : history) h.assign ((size_t) (34.0 * rate), 0.0f);
            written = 0;
            for (auto& f : filters) f.reset();
            for (auto& c : bypass) c.reset();
            freeBeats = 0.0;
        }

        /** 0: the playhead (0..1 of the pattern), 1: the slice playing, 2-129: the input's level across the pattern. */
        int readMeters (float* dest, int capacity) const noexcept override
        {
            if (capacity < 130)
                return 0;
            dest[0] = playhead.load (std::memory_order_relaxed);
            dest[1] = (float) playingSlice.load (std::memory_order_relaxed);
            for (size_t k = 0; k < 128; ++k)
                dest[2 + k] = overview[k].load (std::memory_order_relaxed);
            return 130;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < info.params.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            const auto tempo = std::max (20.0, bpm.load (std::memory_order_relaxed));
            const auto beatSamples = 60.0 / tempo * sampleRate;
            const auto patternBeats = lengthBeats[juce::jlimit (0, 4, juce::roundToInt (v[length]))];
            const auto patternSamples = patternBeats * beatSamples;
            const auto size = (juce::int64) history[0].size();
            const auto isPlaying = playing.load (std::memory_order_relaxed);
            auto beat = isPlaying ? (double) position.load (std::memory_order_relaxed) / beatSamples : freeBeats;
            const auto wet = v[mix] * 0.01f;
            const auto off = v[patternOff] > 0.5f;
            const auto fadeSamples = std::max (1.0, v[declick] * 0.01 * 0.012 * sampleRate);
            const auto splitting = v[bypassOn] > 0.5f;
            if (splitting)
                for (auto& c : bypass) c.set (v[bypassFreq], sampleRate);

            // The slices in order of where they start in the output
            const auto count = juce::jlimit (1, maxSlices, juce::roundToInt (v[sliceCount]) + 1);
            std::array<int, maxSlices> order {};
            for (int k = 0; k < count; ++k) order[(size_t) k] = k;
            std::sort (order.begin(), order.begin() + count, [&v] (int a, int b) { return v[(size_t) index (a, outBeat)] < v[(size_t) index (b, outBeat)]; });
            const auto sixteenth = 0.25;   // (slice starts are in sixteenths)

            for (int i = 0; i < n; ++i)
            {
                // Record
                const auto at = written % size;
                history[0][(size_t) at] = l[i];
                history[1][(size_t) at] = r[i];
                const auto inPattern = std::fmod (std::max (0.0, beat), patternBeats);
                const auto cycleStart = (double) written - inPattern * beatSamples;
                float a = l[i], b = r[i];
                if (! off)
                {
                    // Which slice: the last to start at or before here
                    int slot = 0;
                    for (int k = 0; k < count; ++k)
                        if (v[(size_t) index (order[(size_t) k], outBeat)] * sixteenth <= inPattern + 1.0e-9)
                            slot = k;
                    const auto s = order[(size_t) slot];
                    const auto start = slot == 0 ? 0.0 : (double) v[(size_t) index (s, outBeat)] * sixteenth;   // (the first plays from the top)
                    const auto end = slot + 1 < count ? (double) v[(size_t) index (order[(size_t) slot + 1], outBeat)] * sixteenth : patternBeats;
                    const auto sliceBeats = std::max (1.0e-3, std::min (end, patternBeats) - start);
                    // Repeats: the slice's first part again and again
                    const auto repeats = juce::jlimit (1, 8, juce::roundToInt (v[(size_t) index (s, repeat)]) + 1);
                    const auto part = sliceBeats / repeats;
                    const auto within = std::fmod (inPattern - start, part);
                    const auto x = within / part;   // 0..1 through this repeat
                    // Time: from the input beat, at the slice's speed (curved)
                    const auto spd = v[(size_t) index (s, speed)] * 0.01;
                    const auto bend = std::pow (2.0, v[(size_t) index (s, curve)] / 50.0);
                    const auto source = (double) v[(size_t) index (s, inBeat)] * sixteenth + spd * part * std::pow (x, bend);
                    auto readPos = cycleStart + source * beatSamples;
                    while (readPos > (double) written + 1.0e-3)
                        readPos -= patternSamples;   // (not heard yet: the pattern before's)
                    readPos = std::max (readPos, (double) (written - size + 2));
                    a = read (0, readPos, size);
                    b = read (1, readPos, size);
                    // De-click: a short fade at each edge
                    const auto fromEdge = std::min (within * beatSamples, (part - within) * beatSamples);
                    auto gain = v[declick] > 0.0f ? (float) std::min (1.0, (fromEdge + 1.0) / fadeSamples) : 1.0f;
                    // Volume: level and slope across the slice
                    const auto sl = v[(size_t) index (s, slope)] * 0.01f;
                    gain *= dbToGain (v[(size_t) index (s, volume)]) * (sl >= 0.0f ? 1.0f - sl + sl * (float) x : 1.0f + sl * (float) x);
                    if (v[(size_t) index (s, volume)] <= -59.9f) gain = 0.0f;
                    // Cutoff and resonance
                    const auto cut = v[(size_t) index (s, cutoff)];
                    if (cut < 19900.0f)
                    {
                        if (s != filterSlice || std::abs (cut - filterCut) > 0.5f)
                        {
                            for (auto& f : filters) f.set (cut, 0.6 + v[(size_t) index (s, reso)] * 0.01 * 9.0, sampleRate);
                            filterSlice = s;
                            filterCut = cut;
                        }
                        float lo, band, hi;
                        filters[0].tick (a, lo, band, hi); a = lo;
                        filters[1].tick (b, lo, band, hi); b = lo;
                    }
                    // Pan
                    const auto pn = v[(size_t) index (s, pan)] * 0.01f;
                    a *= gain * (pn > 0.0f ? 1.0f - pn : 1.0f);
                    b *= gain * (pn < 0.0f ? 1.0f + pn : 1.0f);
                    if (i == 0)
                        playingSlice.store (s, std::memory_order_relaxed);
                }
                if (splitting)
                {
                    // (below the frequency: the dry signal)
                    float dl, dh, pl, ph;
                    bypass[0].split (l[i], 0, dl, dh);
                    bypass[1].split (a, 0, pl, ph);
                    float el, eh, ql, qh;
                    bypass[2].split (r[i], 0, el, eh);
                    bypass[3].split (b, 0, ql, qh);
                    a = dl + ph;
                    b = el + qh;
                }
                l[i] += (a - l[i]) * wet;
                r[i] += (b - r[i]) * wet;
                // The overview: the input's level in 128 bins across the pattern
                const auto bin = (size_t) juce::jlimit (0, 127, (int) (inPattern / patternBeats * 128.0));
                const auto level = std::max (std::abs (history[0][(size_t) at]), std::abs (history[1][(size_t) at]));
                if (bin != lastBin) { overview[bin].store (level, std::memory_order_relaxed); lastBin = bin; }
                else if (level > overview[bin].load (std::memory_order_relaxed)) overview[bin].store (level, std::memory_order_relaxed);
                beat += 1.0 / beatSamples;
                ++written;
            }
            playhead.store ((float) (std::fmod (std::max (0.0, beat), patternBeats) / patternBeats), std::memory_order_relaxed);
            freeBeats = beat;
        }

    private:
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
        std::array<Svf, 2> filters;
        int filterSlice = -1;
        float filterCut = -1.0f;
        std::array<Crossover, 4> bypass;
        double freeBeats = 0.0;
        size_t lastBin = 0;
        std::atomic<float> playhead { 0.0f };
        std::atomic<int> playingSlice { 0 };
        std::array<std::atomic<float>, 128> overview {};
    };

    std::vector<ParamInfo> beatBreakerParams()
    {
        std::vector<juce::String> slices, counts, repeats, patterns { "Straight", "Stutter", "Reverse", "Halftime", "Double", "Tape Stop", "Gated", "Shuffle", "Filter Sweep", "Ping Pong" };
        for (int k = 1; k <= BeatBreaker::maxSlices; ++k) { slices.push_back (juce::String (k)); counts.push_back (juce::String (k)); }
        for (int k = 1; k <= 8; ++k) repeats.push_back (juce::String (k));
        std::vector<ParamInfo> p {
            { "length", "Length", "", 0.0f, 4.0f, 2.0f, 2.0f, { "1 Beat", "2 Beats", "4 Beats", "8 Beats", "16 Beats" } },
            { "declick", "De-click", " %", 0.0f, 100.0f, 30.0f, 50.0f },
            { "bypassOn", "Bypass Below", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "bypassFreq", "Bypass Below Freq", " Hz", 20.0f, 500.0f, 120.0f, 120.0f },
            { "mix", "Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f },
            { "mode", "Edit Mode", "", 0.0f, 5.0f, 0.0f, 2.5f, { "Time", "Repeat", "Cutoff", "Resonance", "Volume", "Pan" } },
            { "selected", "Slice", "", 0.0f, (float) BeatBreaker::maxSlices - 1.0f, 0.0f, 15.5f, slices },
            { "sliceCount", "Slices", "", 0.0f, (float) BeatBreaker::maxSlices - 1.0f, 7.0f, 15.5f, counts },
            { "pattern", "Pattern", "", 0.0f, (float) patterns.size() - 1.0f, 0.0f, 4.5f, patterns },
            { "patternLoaded", "Pattern Loaded", "", 0.0f, 100.0f, 0.0f, 50.0f },
            { "patternOff", "Pattern Off", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "randomize", "Randomize", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
            { "randomAll", "Randomize All", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        };
        for (int k = 0; k < BeatBreaker::maxSlices; ++k)
        {
            const auto n = juce::String (k + 1), name = "Slice " + n + " ";
            const auto start = (float) (k * 2 % 64);
            p.push_back ({ "out" + n, name + "Output Beat", " /16", 0.0f, 63.0f, start, 32.0f });
            p.push_back ({ "in" + n, name + "Input Beat", " /16", 0.0f, 63.0f, start, 32.0f });
            p.push_back ({ "speed" + n, name + "Speed", " %", -400.0f, 400.0f, 100.0f, 0.0f });
            p.push_back ({ "curve" + n, name + "Speed Curve", "", -100.0f, 100.0f, 0.0f, 0.0f });
            p.push_back ({ "repeat" + n, name + "Repeat", "", 0.0f, 7.0f, 0.0f, 3.5f, repeats });
            p.push_back ({ "cutoff" + n, name + "Cutoff", " Hz", 20.0f, 20000.0f, 20000.0f, 1000.0f });
            p.push_back ({ "reso" + n, name + "Resonance", " %", 0.0f, 100.0f, 0.0f, 50.0f });
            p.push_back ({ "volume" + n, name + "Volume", " dB", -60.0f, 6.0f, 0.0f, -12.0f });
            p.push_back ({ "slope" + n, name + "Volume Slope", " %", -100.0f, 100.0f, 0.0f, 0.0f });
            p.push_back ({ "pan" + n, name + "Pan", " %", -100.0f, 100.0f, 0.0f, 0.0f });
        }
        return p;
    }

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockMultiEffects()
{
    static const std::vector<StockEffect> effects {
        { { "phatFx", "Phat FX", phatParams(), "Multi Effects" }, make<PhatFx> },
        { { "stepFx", "Step FX", stepParams(), "Multi Effects" }, make<StepFx> },
        { { "remixFx", "Remix FX", remixParams(), "Multi Effects" }, make<RemixFx> },
        { { "beatBreaker", "Beat Breaker", beatBreakerParams(), "Multi Effects" }, make<BeatBreaker> },
    };
    return effects;
}

} // namespace riff
