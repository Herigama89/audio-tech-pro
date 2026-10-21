#include "StockDsp.h"

/*
    Amps (Logic's Amps and Pedals folder): a guitar amp and a bass amp, each a preamp of two driven
    stages, a tone stack, a power amp that sags, and a speaker cabinet with a microphone in front of
    it. The driven stages run at twice the sample rate.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** A cabinet and microphone: the speaker's low and high roll-off, its body resonance and presence. */
    struct Cabinet
    {
        Biquad hp, lp1, lp2, body, presence, edge;
        bool off = false;
        void set (int model, float micPosition, double rate, bool bass)
        {
            // (low cut, high cut, body freq/gain, presence freq/gain)
            struct Shape { float lowCut, highCut, bodyF, bodyG, presF, presG; };
            static const Shape guitar[] { { 90, 5200, 120, 3, 2400, 3 }, { 75, 5000, 110, 4, 2200, 4 }, { 65, 4300, 100, 5, 2000, 5 } };
            static const Shape bassShapes[] { { 40, 4500, 80, 3, 1500, 2 }, { 45, 4800, 90, 3, 1800, 3 }, { 35, 3200, 70, 5, 1200, 1 } };
            off = model >= 3;
            const auto& s = bass ? bassShapes[juce::jlimit (0, 2, model)] : guitar[juce::jlimit (0, 2, model)];
            hp.setHighPass (rate, s.lowCut, 0.8);
            lp1.setLowPass (rate, s.highCut * (1.0f - micPosition * 0.35f), butterQ1);
            lp2.setLowPass (rate, s.highCut * (1.0f - micPosition * 0.35f), butterQ2);
            body.setPeak (rate, s.bodyF, s.bodyG, 1.2);
            presence.setPeak (rate, s.presF, s.presG - micPosition * 4.0f, 1.0);
            edge.setPeak (rate, 5500.0, -micPosition * 6.0, 0.8);
        }
        void reset() { for (auto* f : { &hp, &lp1, &lp2, &body, &presence, &edge }) f->reset(); }
        float process (float x, int ch) noexcept
        {
            if (off)
                return x;
            return edge.process (presence.process (body.process (lp2.process (lp1.process (hp.process (x, ch), ch), ch), ch), ch), ch);
        }
    };

    /** The shared amp: input filter, two preamp stages (oversampled), tone stack, power amp with sag, cabinet. */
    class AmpBase : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* f : { &upA, &upB, &downA, &downB })
                f->reset();
            upA.setLowPass (rate * 2.0, rate * 0.45, butterQ1);
            upB.setLowPass (rate * 2.0, rate * 0.45, butterQ2);
            downA.setLowPass (rate * 2.0, rate * 0.45, butterQ1);
            downB.setLowPass (rate * 2.0, rate * 0.45, butterQ2);
            for (auto* f : { &inputHp, &interstage, &low, &mid, &mid2, &high, &presence, &dcBlock })
                f->reset();
            cabinet.reset();
            sag = 0.0f;
            last.fill (std::numeric_limits<float>::quiet_NaN());
        }

    protected:
        /** The amp's voicing, from its knobs (called when one moves). */
        virtual void voice (const std::array<float, maxParams>& v) = 0;
        /** One sample through the preamp shapers (twice-rate). */
        float preamp (float x) noexcept
        {
            auto y = saturate (x * stage1Gain + bias) - saturate (bias);
            y = interstage.process (y, 0);
            return saturate (y * stage2Gain) * 0.8f;
        }

        void run (float* l, float* r, int n, float mix) noexcept
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            if (v != last)
            {
                voice (v);
                last = v;
            }
            const auto sagCoeff = coeffMs (80.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto dry = 0.5f * (l[i] + r[i]);
                auto x = inputHp.process (dry, 0) * inputGain;
                // Twice the rate through the preamp
                const auto a = upB.process (upA.process (x * 2.0f, 0), 0), b = upB.process (upA.process (0.0f, 0), 0);
                x = downB.process (downA.process (preamp (a), 0), 0);
                downB.process (downA.process (preamp (b), 0), 0);
                // Tone stack
                x = high.process (mid2.process (mid.process (low.process (x, 0), 0), 0), 0);
                x = presence.process (x, 0);
                // Power amp: it sags (squashes) when pushed, the master sets how hard
                sag = std::abs (x) + sagCoeff * (sag - std::abs (x));
                x = saturate (x * master / (1.0f + sag * sagAmount)) / std::max (0.5f, master * 0.6f);
                x = dcBlock.process (cabinet.process (x, 0), 0) * output;
                const auto wet = x;
                l[i] = l[i] * (1.0f - mix) + wet * mix;
                r[i] = r[i] * (1.0f - mix) + wet * mix;
            }
        }

        Biquad upA, upB, downA, downB, inputHp, interstage, low, mid, mid2, high, presence, dcBlock;
        Cabinet cabinet;
        float inputGain = 1.0f, stage1Gain = 1.0f, stage2Gain = 1.0f, bias = 0.0f, master = 1.0f, sagAmount = 0.3f, output = 1.0f, sag = 0.0f;
        std::array<float, maxParams> last {};
    };

    //==============================================================================
    class GuitarAmp final : public AmpBase
    {
    public:
        using AmpBase::AmpBase;
        void process (float* l, float* r, int n) noexcept override { run (l, r, n, value (10) * 0.01f); }
    protected:
        void voice (const std::array<float, maxParams>& v) override
        {
            // Model: Clean, Crunch, British, High Gain
            const auto model = juce::jlimit (0, 3, juce::roundToInt (v[0]));
            static const float drive[] { 0.6f, 1.6f, 2.6f, 5.0f }, second[] { 0.8f, 1.6f, 2.4f, 4.0f };
            static const float midF[] { 800.0f, 650.0f, 700.0f, 550.0f }, biasOf[] { 0.0f, 0.08f, 0.12f, 0.05f };
            const auto gain = v[1] * 0.1f;   // 0..10
            stage1Gain = 1.0f + gain * drive[model];
            stage2Gain = 1.0f + gain * second[model] * 0.6f;
            bias = biasOf[model];
            inputHp.setHighPass (sampleRate, model == 3 ? 120.0 : 80.0, 0.7);
            interstage.setLowPass (sampleRate * 2.0, model == 0 ? 9000.0 : 6500.0, 0.6);
            // Tone stack, 0..10 each (5 flat)
            low.setLowShelf (sampleRate, 120.0, (v[2] - 5.0f) * 2.4f, 0.7);
            mid.setPeak (sampleRate, midF[model], (v[3] - 5.0f) * 2.2f - (model == 3 ? 3.0f : 0.0f), 0.8);
            mid2.setPeak (sampleRate, 400.0, model == 2 ? 2.0 : 0.0, 1.0);
            high.setHighShelf (sampleRate, 2800.0, (v[4] - 5.0f) * 2.4f, 0.7);
            presence.setPeak (sampleRate, 4200.0, (v[5] - 5.0f) * 1.8f, 0.9);
            master = 0.6f + v[6] * 0.14f;
            sagAmount = model == 0 ? 0.15f : 0.35f;
            cabinet.set (juce::roundToInt (v[7]), v[8] * 0.01f, sampleRate, false);
            dcBlock.setHighPass (sampleRate, 20.0, 0.7);
            inputGain = 1.0f;
            output = dbToGain (v[9]) * (model == 3 ? 0.6f : 0.8f);
        }
    };

    //==============================================================================
    class BassAmp final : public AmpBase
    {
    public:
        using AmpBase::AmpBase;
        void process (float* l, float* r, int n) noexcept override
        {
            // Blend: the clean DI under the amp
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            const auto blend = value (8) * 0.01f;
            for (int i = 0; i < n; ++i)
                di[(size_t) i] = 0.5f * (l[i] + r[i]);
            run (l, r, n, value (10) * 0.01f);
            for (int i = 0; i < n; ++i)
            {
                const auto d = di[(size_t) i] * dbToGain (value (9));
                l[i] = l[i] * (1.0f - blend) + d * blend;
                r[i] = r[i] * (1.0f - blend) + d * blend;
            }
        }
    protected:
        void voice (const std::array<float, maxParams>& v) override
        {
            const auto gain = v[0] * 0.1f;
            stage1Gain = 1.0f + gain * 1.8f;
            stage2Gain = 1.0f + gain * 1.2f;
            bias = 0.05f;
            inputHp.setHighPass (sampleRate, 30.0, 0.7);
            interstage.setLowPass (sampleRate * 2.0, 5000.0, 0.6);
            low.setLowShelf (sampleRate, 80.0, (v[1] - 5.0f) * 2.6f, 0.8);
            mid.setPeak (sampleRate, 400.0, (v[2] - 5.0f) * 2.4f, 0.9);
            mid2.setPeak (sampleRate, 1200.0, (v[3] - 5.0f) * 2.4f, 0.9);
            high.setHighShelf (sampleRate, 3500.0, (v[4] - 5.0f) * 2.4f, 0.7);
            presence.setIdentity();
            master = 0.6f + v[5] * 0.14f;
            sagAmount = 0.25f;
            cabinet.set (juce::roundToInt (v[6]), v[7] * 0.01f, sampleRate, true);
            dcBlock.setHighPass (sampleRate, 15.0, 0.7);
            output = dbToGain (v[11]) * 0.8f;
        }
    private:
        std::array<float, 1024> di {};
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockAmpEffects()
{
    const juce::String dB (" dB"), pc (" %");
    static const std::vector<StockEffect> effects {
        { { "guitarAmp", "Guitar Amp", {
              { "model", "Amp", "", 0.0f, 3.0f, 1.0f, 1.5f, { "Clean", "Crunch", "British", "High Gain" } },
              { "gain", "Gain", "", 0.0f, 100.0f, 45.0f, 50.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "mid", "Mid", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "presence", "Presence", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "master", "Master", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "cab", "Cabinet", "", 0.0f, 3.0f, 1.0f, 1.5f, { "1x12 Open", "2x12", "4x12 Closed", "Off" } },
              { "mic", "Mic Position", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Amps and Pedals" },
          make<GuitarAmp> },
        { { "bassAmp", "Bass Amp", {
              { "gain", "Gain", "", 0.0f, 100.0f, 30.0f, 50.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "lowMid", "Low Mid", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "highMid", "High Mid", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "master", "Master", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "cab", "Cabinet", "", 0.0f, 3.0f, 0.0f, 1.5f, { "8x10", "4x10", "1x15", "Off (DI)" } },
              { "mic", "Mic Position", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "blend", "DI Blend", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "diLevel", "DI Level", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Amps and Pedals" },
          make<BassAmp> },
    };
    return effects;
}

} // namespace riff
