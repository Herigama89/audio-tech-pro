#include "StockDsp.h"

/*
    More of Logic's set: a linear-phase EQ (the Channel EQ's curve with no phase shift), a multi-tap
    delay designer, a sub-bass octave generator and a spreader that widens with tiny pitch and time
    differences between the sides.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    //==============================================================================
    /** The Channel EQ's curve as a linear-phase filter (an FIR made from it, run by convolution). */
    class LinearPhaseEq final : public BuiltInEffect,
                                private juce::Timer
    {
    public:
        static constexpr int taps = 4096;
        explicit LinearPhaseEq (const EffectInfo& info) : BuiltInEffect (info) {}
        ~LinearPhaseEq() override { stopTimer(); }

        int getLatencySamples() const override { return taps / 2; }

        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            blockSize = std::max (16, maxBlock);
            convolution.prepare ({ rate, (juce::uint32) blockSize, 2 });
            built.fill (std::numeric_limits<float>::quiet_NaN());
            rebuild();
            startTimerHz (8);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            for (int at = 0; at < n; at += blockSize)
            {
                const auto count = std::min (blockSize, n - at);
                float* channels[] { l + at, r + at };
                juce::dsp::AudioBlock<float> block (channels, 2, (size_t) count);
                convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            }
        }

    private:
        void timerCallback() override { rebuild(); }

        void rebuild()
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load();
            if (v == built || sampleRate <= 0.0 || info.response == nullptr)
                return;
            built = v;
            // The curve sampled at each FFT bin, zero phase; back to time, centred and windowed
            juce::dsp::FFT fft (12);
            std::vector<float> data ((size_t) taps * 2, 0.0f);
            for (int b = 0; b <= taps / 2; ++b)
            {
                const auto f = std::max (5.0, b * sampleRate / taps);
                const auto gain = (float) std::pow (10.0, info.response (v.data(), f, sampleRate) / 20.0);
                data[(size_t) (2 * b)] = gain;
                data[(size_t) (2 * b + 1)] = 0.0f;
            }
            for (int b = taps / 2 + 1; b < taps; ++b)
            {
                data[(size_t) (2 * b)] = data[(size_t) (2 * (taps - b))];
                data[(size_t) (2 * b + 1)] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (data.data());
            juce::AudioBuffer<float> ir (1, taps);
            for (int i = 0; i < taps; ++i)
            {
                const auto source = (i + taps / 2) % taps;   // (centred: the delay of half the length)
                const auto w = 0.42f - 0.5f * std::cos (twoPi * i / (taps - 1)) + 0.08f * std::cos (2.0f * twoPi * i / (taps - 1));
                ir.setSample (0, i, data[(size_t) source] * w);
            }
            convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        }

        juce::dsp::Convolution convolution;
        int blockSize = 512;
        std::array<float, maxParams> built {};
    };

    //==============================================================================
    /** Six taps, each with its own time and level, alternating sides by the spread, and feedback from the last. */
    class DelayDesigner final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            line.prepare ((int) (4.1 * rate));
            lowCut.reset();
            highCut.reset();
            feedbackSample = 0.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto feedback = value (12) * 0.01f;
            const auto spread = value (13) * 0.01f;
            lowCut.setLowPass (value (14), sampleRate);
            highCut.setLowPass (12000.0, sampleRate);
            const auto mix = value (15) * 0.01f;
            std::array<float, 6> delays, levels;
            float longest = 1.0f;
            for (size_t t = 0; t < 6; ++t)
            {
                delays[t] = std::max (1.0f, value ((int) t * 2) * 0.001f * (float) sampleRate);
                levels[t] = dbToGain (value ((int) t * 2 + 1));
                if (value ((int) t * 2 + 1) <= -59.0f)
                    levels[t] = 0.0f;
                longest = std::max (longest, delays[t]);
            }
            for (int i = 0; i < n; ++i)
            {
                line.push (0.5f * (l[i] + r[i]) + feedbackSample * feedback);
                float wl = 0.0f, wr = 0.0f;
                for (size_t t = 0; t < 6; ++t)
                {
                    const auto tap = line.read (delays[t]) * levels[t];
                    const auto pan = (t % 2 == 0 ? -1.0f : 1.0f) * spread;
                    wl += tap * (1.0f - pan) * 0.5f;
                    wr += tap * (1.0f + pan) * 0.5f;
                }
                feedbackSample = highCut.lowPass (lowCut.highPass (line.read (longest)));
                l[i] = l[i] * (1.0f - mix) + wl * mix * 2.0f;
                r[i] = r[i] * (1.0f - mix) + wr * mix * 2.0f;
            }
        }
    private:
        DelayLine line;
        OnePole lowCut, highCut;
        float feedbackSample = 0.0f;
    };

    //==============================================================================
    /** An octave (or two) below the bass, made from its zero crossings and following its level. */
    class SubBass final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* f : { &detect1, &detect2, &smooth1, &smooth2 }) f->reset();
            env = 0.0f;
            state = 1.0f;
            count = 0;
            previous = 0.0f;
            lastCut = -1.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto cut = value (0);
            if (cut != lastCut)
            {
                detect1.setLowPass (sampleRate, cut, butterQ1);
                detect2.setLowPass (sampleRate, cut, butterQ2);
                smooth1.setLowPass (sampleRate, cut * 0.6, butterQ1);
                smooth2.setLowPass (sampleRate, cut * 0.6, butterQ2);
                lastCut = cut;
            }
            const auto octaves = choice (1) + 1;
            const auto level = dbToGain (value (2));
            const auto dry = value (3) <= -59.0f ? 0.0f : dbToGain (value (3));
            const bool square = choice (4) == 1;
            const auto envCoeff = coeffMs (20.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto low = detect2.process (detect1.process (0.5f * (l[i] + r[i]), 0), 0);
                env = std::abs (low) + envCoeff * (env - std::abs (low));
                // A flip on every rising zero crossing (every second one for two octaves down)
                if (previous < 0.0f && low >= 0.0f && ++count >= octaves)
                {
                    count = 0;
                    state = -state;
                }
                previous = low;
                auto sub = state * env * 1.5f;
                if (! square)
                    sub = smooth2.process (smooth1.process (sub, 0), 0) * 1.3f;
                l[i] = l[i] * dry + sub * level;
                r[i] = r[i] * dry + sub * level;
            }
        }
    private:
        Biquad detect1, detect2, smooth1, smooth2;
        float env = 0.0f, state = 1.0f, previous = 0.0f, lastCut = -1.0f;
        int count = 0;
    };

    //==============================================================================
    /** Wider: each side a few milliseconds apart and a few cents apart, gently moving. */
    class Spreader final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.06 * rate));
            dr.prepare ((int) (0.06 * rate));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto delay = value (0) * 0.001f * (float) sampleRate;
            const auto cents = value (1);
            const auto rate = value (2);
            const auto swing = (float) ((std::pow (2.0, cents / 1200.0) - 1.0) / (juce::MathConstants<double>::twoPi * std::max (0.05f, rate)) * sampleRate);
            const auto width = value (3) * 0.01f;
            const auto mix = value (4) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto m = 0.5f * (l[i] + r[i]);
                dl.push (m);
                dr.push (m);
                const auto a = dl.read (2.0f + delay * 0.3f + std::min (swing, delay * 0.3f + 1.0f) * (1.0f + lfo.peek (0.0)));
                const auto b = dr.read (2.0f + delay + std::min (swing, delay * 0.3f + 1.0f) * (1.0f + lfo.peek (0.5)));
                lfo.next (rate, sampleRate);
                const auto side = 0.5f * (a - b) * width;
                l[i] = l[i] + side * mix;
                r[i] = r[i] - side * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockExtraEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), dB (" dB");
    static const std::vector<StockEffect> effects = [&]
    {
        // The Linear Phase EQ: the Channel EQ's knobs and curve
        const auto& channel = stockEqEffects().front().info;
        EffectInfo linear { "linearEq", "Linear Phase EQ", channel.params, "EQ", channel.response, channel.columns };

        std::vector<ParamInfo> taps;
        const float times[] { 125, 250, 375, 500, 750, 1000 }, levels[] { -6, -9, -12, -15, -20, -24 };
        for (int t = 0; t < 6; ++t)
        {
            const auto n = juce::String (t + 1);
            taps.push_back ({ "time" + n, "Tap " + n + " Time", ms, 1.0f, 4000.0f, times[t], 400.0f });
            taps.push_back ({ "level" + n, "Tap " + n + " Level", dB, -60.0f, 0.0f, levels[t], -12.0f });
        }
        taps.push_back ({ "feedback", "Feedback", pc, 0.0f, 95.0f, 20.0f, 40.0f });
        taps.push_back ({ "spread", "Spread", pc, 0.0f, 100.0f, 70.0f, 50.0f });
        taps.push_back ({ "lowCut", "Low Cut", hz, 10.0f, 1000.0f, 100.0f, 150.0f });
        taps.push_back ({ "mix", "Mix", pc, 0.0f, 100.0f, 35.0f, 50.0f });

        return std::vector<StockEffect> {
            { linear, make<LinearPhaseEq> },
            { { "delayDesigner", "Delay Designer", taps, "Delay" }, make<DelayDesigner> },
            { { "subBass", "Sub Bass", {
                  { "cut", "Bass Range", hz, 60.0f, 300.0f, 150.0f, 150.0f },
                  { "octave", "Octave", "", 0.0f, 1.0f, 0.0f, 0.5f, { "-1", "-2" } },
                  { "level", "Sub Level", dB, -60.0f, 6.0f, -6.0f, -12.0f },
                  { "dry", "Dry Level", dB, -60.0f, 6.0f, 0.0f, -6.0f },
                  { "shape", "Shape", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Sine", "Square" } } }, "Specialized" },
              make<SubBass> },
            { { "spreader", "Spreader", {
                  { "delay", "Delay", ms, 0.0f, 30.0f, 8.0f, 10.0f },
                  { "cents", "Pitch Spread", " cents", 0.0f, 30.0f, 8.0f, 10.0f },
                  { "rate", "Rate", hz, 0.05f, 3.0f, 0.3f, 0.5f },
                  { "width", "Width", pc, 0.0f, 200.0f, 100.0f, 100.0f },
                  { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Imaging" },
              make<Spreader> },
        };
    }();
    return effects;
}

} // namespace riff
