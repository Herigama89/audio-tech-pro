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
        explicit PhatFx (const EffectInfo& i) : ModulatedEffect (i, phatTargets())
        {
            xy = idx ("xyX");
            for (const auto* id : { "input", "output", "mix", "limiter", "slot1", "bpOn", "bpType", "bpLow", "bpHigh", "bpLowRes", "bpHighRes", "bpReject",
                                    "fOn", "fType", "fCutoff", "fRes", "fDrive", "fMix", "dOn", "dType1", "dAmt1", "mOn", "mType", "mMix", "mRate",
                                    "bOn", "bType", "bAmount", "bTune", "cOn", "cType", "cAmount", "cRelease", "l1On", "eOn" })
                at[id] = idx (id);
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
        float p (const char* id) const noexcept { return v[(size_t) at.at (id)]; }
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
            const auto l1 = at.at ("l1On");
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
            const auto e = at.at ("eOn");   // on, target, attack, release, depth
            if (v[(size_t) e] > 0.5f)
                modulate (juce::roundToInt (v[(size_t) e + 1]), std::min (1.0f, envelope * v[(size_t) e + 4] * 0.01f));
            envAttack = std::exp (-1.0f / (std::max (0.1f, v[(size_t) e + 2]) * 0.001f * (float) sampleRate));
            envRelease = std::exp (-1.0f / (std::max (1.0f, v[(size_t) e + 3]) * 0.001f * (float) sampleRate));
            addXy();
        }

        void configure() noexcept
        {
            inputGain = dbToGain (p ("input"));
            outputGain = dbToGain (p ("output"));
            wet = p ("mix") * 0.01f;
            limiterMode = juce::roundToInt (p ("limiter"));
            const auto s1 = at.at ("slot1");
            std::array<bool, 6> used {};
            orderCount = 0;
            for (int s = 0; s < 6; ++s)
            {
                const auto unit = juce::jlimit (0, 5, juce::roundToInt (v[(size_t) (s1 + s)]));
                if (! used[(size_t) unit]) { used[(size_t) unit] = true; order[(size_t) orderCount++] = unit; }
            }
            // Bandpass
            bpOn = p ("bpOn") > 0.5f;
            const auto steep = juce::roundToInt (p ("bpType")) == 1;
            const auto lowQ = 0.707 + p ("bpLowRes") * 0.06, highQ = 0.707 + p ("bpHighRes") * 0.06;
            const auto low = std::min (p ("bpLow"), p ("bpHigh"));
            for (int s = 0; s < 2; ++s)
            {
                if (s == 0 || steep)
                {
                    bandpass[(size_t) s * 2].setHighPass (sampleRate, low, s == 0 ? lowQ : 0.707);
                    bandpass[(size_t) s * 2 + 1].setLowPass (sampleRate, std::min ((double) p ("bpHigh"), sampleRate * 0.45), s == 0 ? highQ : 0.707);
                }
                else
                {
                    bandpass[2].setIdentity();
                    bandpass[3].setIdentity();
                }
            }
            reject = p ("bpReject") * 0.01f;
            // Filter
            fOn = p ("fOn") > 0.5f;
            filter.set (juce::roundToInt (p ("fType")), p ("fCutoff"), p ("fRes") * 0.01f, sampleRate);
            fDrive = 1.0f + p ("fDrive") * 0.12f;
            fMix = p ("fMix") * 0.01f;
            // Distortion
            dOn = p ("dOn") > 0.5f;
            const auto d1 = at.at ("dType1");
            for (int k = 0; k < 3; ++k)
            {
                dType[(size_t) k] = juce::roundToInt (v[(size_t) (d1 + k * 2)]);
                dAmount[(size_t) k] = v[(size_t) (d1 + k * 2 + 1)];
            }
            // Mod FX
            mOn = p ("mOn") > 0.5f;
            mType = juce::roundToInt (p ("mType"));
            mMix = p ("mMix") * 0.01f;
            mRate = p ("mRate");
            // Bass Enhancer
            bOn = p ("bOn") > 0.5f;
            bType = juce::roundToInt (p ("bType"));
            bAmount = p ("bAmount") * 0.01f;
            const auto tune = (double) p ("bTune");
            if (std::abs (tune - builtTune) > 0.01 || bType != builtType || std::abs (bAmount - builtAmount) > 0.001f)
            {
                bassBand.setBandPass (sampleRate, tune, 1.2);
                bassShelf.setLowShelf (sampleRate, tune * 1.5, 12.0 * bAmount, 0.8);
                bassLow.setLowPass (sampleRate, tune * 0.75, 0.707);
                builtTune = tune; builtType = bType; builtAmount = bAmount;
            }
            // Compressor
            cOn = p ("cOn") > 0.5f;
            cType = juce::roundToInt (p ("cType"));
            cAmount = p ("cAmount") * 0.01f;
            static constexpr float ratios[] { 4.0f, 1.0f, 3.0f, 4.0f, 8.0f, 4.0f, 3.0f, 12.0f, 3.0f };
            static constexpr float attacks[] { 5.0f, 0.1f, 3.0f, 2.0f, 0.3f, 1.0f, 8.0f, 0.2f, 10.0f };
            ratio = ratios[juce::jlimit (0, 8, cType)];
            threshold = -cAmount * 40.0f;
            cAttack = std::exp (-1.0f / (attacks[juce::jlimit (0, 8, cType)] * 0.001f * (float) sampleRate));
            cRelease = std::exp (-1.0f / (std::max (5.0f, p ("cRelease")) * 0.001f * (float) sampleRate));
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

        std::map<juce::String, int> at;
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
        explicit StepFx (const EffectInfo& i) : ModulatedEffect (i, stepTargets())
        {
            xy = idx ("xyX");
            for (const auto* id : { "input", "mix", "output", "fltOn", "fltType", "fltCutoff", "fltRes", "fltMix", "distOn", "distExciter", "distDirt",
                                    "modOn", "modDelay", "modRate", "modDepth", "modFeedback", "modMix", "dlyOn", "dlyFilter", "dlyFeedback", "dlyCutoff",
                                    "dlyMix", "dlySync", "dlyTimeL", "dlyTimeR", "dlyDivL", "dlyDivR", "revOn", "revType", "revTime", "revMix", "s1On" })
                at[id] = idx (id);
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
        float p (const char* id) const noexcept { return v[(size_t) at.at (id)]; }

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
            const auto s1 = at.at ("s1On");
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
            inputGain = dbToGain (p ("input"));
            outputGain = dbToGain (p ("output"));
            wet = p ("mix") * 0.01f;
            fltOn = p ("fltOn") > 0.5f;
            filter.set (juce::roundToInt (p ("fltType")), p ("fltCutoff"), p ("fltRes") * 0.01f, sampleRate);
            fltMix = p ("fltMix") * 0.01f;
            distOn = p ("distOn") > 0.5f;
            exciterAmount = p ("distExciter");
            dirtAmount = p ("distDirt");
            modOn = p ("modOn") > 0.5f;
            modDelay = p ("modDelay");
            modRate = p ("modRate");
            modDepth = p ("modDepth") * 0.01f;
            modFeedback = p ("modFeedback") * 0.01f;
            modMix = p ("modMix") * 0.01f;
            dlyOn = p ("dlyOn") > 0.5f;
            dlyType = juce::roundToInt (p ("dlyFilter"));
            dlyFeedback = p ("dlyFeedback") * 0.01f;
            dlyMix = p ("dlyMix") * 0.01f;
            const auto synced = p ("dlySync") > 0.5f;
            const auto beatMs = 60000.0 / std::max (20.0, tempo);
            dlyTime[0] = (float) ((synced ? divisionBeats[juce::jlimit (0, 8, juce::roundToInt (p ("dlyDivL")))] * beatMs : p ("dlyTimeL")) * 0.001 * sampleRate);
            dlyTime[1] = (float) ((synced ? divisionBeats[juce::jlimit (0, 8, juce::roundToInt (p ("dlyDivR")))] * beatMs : p ("dlyTimeR")) * 0.001 * sampleRate);
            const auto cutoff = (double) p ("dlyCutoff");
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
            revOn = p ("revOn") > 0.5f;
            revTime = p ("revTime");
            revMix = p ("revMix") * 0.01f;
            const auto type = juce::roundToInt (p ("revType"));
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

        std::map<juce::String, int> at;
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

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockMultiEffects()
{
    static const std::vector<StockEffect> effects {
        { { "phatFx", "Phat FX", phatParams(), "Multi Effects" }, make<PhatFx> },
        { { "stepFx", "Step FX", stepParams(), "Multi Effects" }, make<StepFx> },
    };
    return effects;
}

} // namespace riff
