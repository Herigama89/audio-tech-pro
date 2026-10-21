#include "StockDsp.h"

/*
    The stock reverbs (Logic's Reverb folder): an algorithmic studio reverb with six spaces (a
    feedback delay network), a classic plate (Dattorro's figure-eight tank), a spring tank (chirping
    dispersion in a feedback loop) and a convolution space whose impulse response is made from its
    knobs - length, early reflections, brightness - and can be played backwards.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    //==============================================================================
    /** Eight delay lines mixed through a Hadamard matrix: dense, smooth, colourless tails. */
    class StudioReverb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            const auto scale = rate / 44100.0;
            predelay.prepare ((int) (0.26 * rate));
            for (auto& d : diffL) d.line.prepare ((int) (400 * scale * 1.6) + 4);
            for (auto& d : diffR) d.line.prepare ((int) (400 * scale * 1.6) + 4);
            for (auto& l : lines)
                l.prepare ((int) (2111 * 2.3 * 1.6 * scale) + 64);
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR }) f->reset();
            state.fill (0.0f);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            static const float spaces[] { 0.45f, 0.6f, 0.8f, 1.2f, 1.6f, 2.2f };
            static const float base[] { 1009, 1123, 1289, 1447, 1597, 1741, 1913, 2111 };
            static const float diffBase[] { 142, 107, 379, 277 };
            const auto scale = (float) (sampleRate / 44100.0);
            const auto spaceScale = spaces[juce::jlimit (0, 5, choice (0))] * (0.5f + value (1) * 0.01f);
            const auto decay = std::max (0.1f, value (2));
            const auto pre = std::max (1.0f, value (3) * 0.001f * (float) sampleRate);
            const auto damping = value (4) * 0.01f;
            for (auto* f : { &lowCutL, &lowCutR }) f->setLowPass (value (5), sampleRate);
            for (auto* f : { &highCutL, &highCutR }) f->setLowPass (value (6), sampleRate);
            const auto diffusion = value (7) * 0.01f * 0.72f;
            const auto modDepth = value (8) * 0.01f * 12.0f * scale;
            const auto width = value (9) * 0.01f;
            const auto mix = value (10) * 0.01f;
            const bool freeze = choice (11) == 1;

            std::array<float, 8> lengths, gains;
            for (size_t i = 0; i < 8; ++i)
            {
                lengths[i] = base[i] * scale * spaceScale;
                gains[i] = freeze ? 1.0f : (float) std::pow (10.0, -3.0 * lengths[i] / (decay * sampleRate));
            }

            for (int i = 0; i < n; ++i)
            {
                // Pre-delay, then four diffusers to smear the attack
                const auto in = freeze ? 0.0f : 0.5f * (l[i] + r[i]);
                predelay.push (in);
                auto x = predelay.read (pre);
                auto xl = x, xr = x * 0.97f + (l[i] - r[i]) * 0.15f;
                for (size_t d = 0; d < 4; ++d)
                {
                    xl = diffL[d].process (xl, diffBase[d] * scale * (0.6f + spaceScale * 0.4f), diffusion);
                    xr = diffR[d].process (xr, diffBase[d] * scale * (0.6f + spaceScale * 0.4f) * 1.07f, diffusion);
                }

                // The network: read each line (two of them wobbling a little), mix, damp, feed back
                std::array<float, 8> out;
                const auto m = lfo.next (0.37, sampleRate) * modDepth;
                for (size_t k = 0; k < 8; ++k)
                    out[k] = lines[k].read (lengths[k] + (k == 0 ? m : k == 5 ? -m : 0.0f));
                auto mixed = out;
                hadamard (mixed);
                for (size_t k = 0; k < 8; ++k)
                {
                    auto v = mixed[k] * gains[k];
                    state[k] = v + damping * 0.8f * (state[k] - v);   // (highs die away sooner)
                    lines[k].push (state[k] + (k < 4 ? xl : xr) * 0.35f);
                }
                auto wl = out[0] - out[2] + out[4] - out[6];
                auto wr = out[1] - out[3] + out[5] - out[7];
                wl = highCutL.lowPass (lowCutL.highPass (wl)) * 0.5f;
                wr = highCutR.lowPass (lowCutR.highPass (wr)) * 0.5f;
                const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                l[i] = l[i] * (1.0f - mix) + (mid + side) * mix;
                r[i] = r[i] * (1.0f - mix) + (mid - side) * mix;
            }
        }

    private:
        static void hadamard (std::array<float, 8>& v) noexcept
        {
            for (size_t h = 1; h < 8; h <<= 1)
                for (size_t i = 0; i < 8; i += h << 1)
                    for (size_t j = i; j < i + h; ++j)
                    {
                        const auto a = v[j], b = v[j + h];
                        v[j] = a + b;
                        v[j + h] = a - b;
                    }
            for (auto& x : v)
                x *= 0.35355339f;   // 1/sqrt(8): energy kept
        }

        DelayLine predelay;
        std::array<DiffusionAllPass, 4> diffL, diffR;
        std::array<DelayLine, 8> lines;
        std::array<float, 8> state {};
        OnePole lowCutL, lowCutR, highCutL, highCutR;
        Lfo lfo;
    };

    //==============================================================================
    /** The classic plate (Jon Dattorro's 1997 design): a figure-of-eight tank with modulated diffusers. */
    class PlateReverb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            k = (float) (rate / 29761.0) * 1.5f;   // (room for the Size knob)
            predelay.prepare ((int) (0.21 * rate));
            for (auto& d : inputDiff) d.line.prepare ((int) (400 * k) + 8);
            for (auto* a : { &apL1, &apR1, &apL2, &apR2 }) a->line.prepare ((int) (2700 * k) + 40);
            for (auto* d : { &dL1, &dL2, &dR1, &dR2 }) d->prepare ((int) (4500 * k) + 8);
            bandwidth.reset();
            dampLState = dampRState = 0.0f;
            lowCutL.reset();
            lowCutR.reset();
            fromL = fromR = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto size = (0.5f + value (2) * 0.01f) * (float) (sampleRate / 29761.0);
            const auto pre = std::max (1.0f, value (0) * 0.001f * (float) sampleRate);
            const auto decay = 0.2f + value (1) * 0.01f * 0.78f;
            bandwidth.setLowPass (500.0 + value (3) * 0.01 * 17000.0, sampleRate);
            const auto damping = value (4) * 0.01f * 0.7f;
            const auto excursion = value (5) * 0.01f * 16.0f * size;
            for (auto* f : { &lowCutL, &lowCutR }) f->setLowPass (value (6), sampleRate);
            const auto width = value (7) * 0.01f;
            const auto mix = value (8) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                predelay.push (0.5f * (l[i] + r[i]));
                auto x = bandwidth.lowPass (predelay.read (pre));
                x = inputDiff[0].process (x, 142 * size, 0.75f);
                x = inputDiff[1].process (x, 107 * size, 0.75f);
                x = inputDiff[2].process (x, 379 * size, 0.625f);
                x = inputDiff[3].process (x, 277 * size, 0.625f);

                const auto m1 = lfo.next (0.9, sampleRate) * excursion, m2 = lfo.peek (0.25) * excursion;
                // Left half of the tank
                auto a = apL1.process (x + decay * fromR, 672 * size + m1, -0.7f);
                const auto aDelayed = dL1.read (4453 * size);
                dL1.push (a);
                auto b = aDelayed + damping * (dampLState - aDelayed);   // (damping: the highs fade each time round)
                dampLState = b;
                b = apL2.process (b * decay, 1800 * size, 0.5f);
                const auto leftOut = dL2.read (3720 * size);
                dL2.push (b);
                // Right half
                auto c = apR1.process (x + decay * fromL, 908 * size + m2, -0.7f);
                const auto cDelayed = dR1.read (4217 * size);
                dR1.push (c);
                auto d = cDelayed + damping * (dampRState - cDelayed);
                dampRState = d;
                d = apR2.process (d * decay, 2656 * size, 0.5f);
                const auto rightOut = dR2.read (3163 * size);
                dR2.push (d);
                fromL = leftOut;
                fromR = rightOut;

                // The output taps (Dattorro's table)
                auto wl = 0.6f * (dR1.read (266 * size) + dR1.read (2974 * size) - apR2.line.read (1913 * size) + dR2.read (1996 * size)
                                  - dL1.read (1990 * size) - apL2.line.read (187 * size) - dL2.read (1066 * size));
                auto wr = 0.6f * (dL1.read (353 * size) + dL1.read (3627 * size) - apL2.line.read (1228 * size) + dL2.read (2673 * size)
                                  - dR1.read (2111 * size) - apR2.line.read (335 * size) - dR2.read (121 * size));
                wl = lowCutL.highPass (wl);
                wr = lowCutR.highPass (wr);
                const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                l[i] = l[i] * (1.0f - mix) + (mid + side) * mix;
                r[i] = r[i] * (1.0f - mix) + (mid - side) * mix;
            }
        }

    private:
        float k = 1.0f, fromL = 0.0f, fromR = 0.0f, dampLState = 0.0f, dampRState = 0.0f;
        DelayLine predelay, dL1, dL2, dR1, dR2;
        std::array<DiffusionAllPass, 4> inputDiff;
        DiffusionAllPass apL1, apR1, apL2, apR2;
        OnePole bandwidth, lowCutL, lowCutR;
        Lfo lfo;
    };

    //==============================================================================
    /** A spring tank: a chain of all-passes spreads each sound into the spring's chirp, round a feedback loop. */
    class SpringReverb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& s : springs)
            {
                s.line.prepare ((int) (0.09 * rate) + 8);
                for (auto& a : s.chain) a.z = 0.0f;
                s.tone.reset();
                s.fb = 0.0f;
            }
            inputHp.reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto count = juce::jlimit (1, 3, choice (0) + 1);
            const auto tension = value (1) * 0.01f;             // shorter, tighter springs
            const auto decay = 0.5f + value (2) * 0.01f * 0.47f;
            const auto dispersion = -(0.55f + value (3) * 0.01f * 0.35f);
            const auto tone = value (4);
            const auto drip = value (5) * 0.01f;
            const auto mix = value (6) * 0.01f;
            inputHp.setLowPass (180.0, sampleRate);
            static const float lengths[] { 0.061f, 0.047f, 0.071f };

            for (size_t s = 0; s < 3; ++s)
                springs[s].tone.setLowPass (tone, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto dry = 0.5f * (l[i] + r[i]);
                auto in = inputHp.highPass (dry);
                in = in + drip * saturate (in * 4.0f) * 0.5f;   // (the drip: hits kick the springs harder)
                float outs[3] {};
                for (int s = 0; s < count; ++s)
                {
                    auto& sp = springs[(size_t) s];
                    const auto delay = lengths[s] * (1.0f - tension * 0.55f) * (float) sampleRate;
                    auto v = sp.line.read (delay);
                    for (auto& a : sp.chain)
                        v = a.process (v, dispersion);
                    v = sp.tone.lowPass (v);
                    sp.fb = v;
                    sp.line.push (in + v * decay);
                    outs[s] = v;
                }
                const auto wl = outs[0] + outs[2] * 0.7f, wr = (count > 1 ? outs[1] : outs[0]) + outs[2] * 0.7f;
                l[i] = l[i] * (1.0f - mix) + wl * mix;
                r[i] = r[i] * (1.0f - mix) + wr * mix;
            }
        }

    private:
        struct Spring
        {
            DelayLine line;
            std::array<AllPass1, 16> chain;
            OnePole tone;
            float fb = 0.0f;
        };
        std::array<Spring, 3> springs;
        OnePole inputHp;
    };

    //==============================================================================
    /** Convolution with an impulse response made from the knobs (built again, off the audio thread, when they move). */
    class ConvolutionSpace final : public BuiltInEffect,
                                   private juce::Timer
    {
    public:
        explicit ConvolutionSpace (const EffectInfo& info) : BuiltInEffect (info) {}
        ~ConvolutionSpace() override { stopTimer(); }

        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            blockSize = std::max (16, maxBlock);
            convolution.prepare ({ rate, (juce::uint32) blockSize, 2 });
            dry.setSize (2, blockSize);
            built.fill (std::numeric_limits<float>::quiet_NaN());
            rebuild();
            startTimerHz (5);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto mix = value (8) * 0.01f;
            for (int at = 0; at < n; at += blockSize)
            {
                const auto count = std::min (blockSize, n - at);
                float* channels[] { l + at, r + at };
                dry.copyFrom (0, 0, l + at, count);
                dry.copyFrom (1, 0, r + at, count);
                juce::dsp::AudioBlock<float> block (channels, 2, (size_t) count);
                convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
                for (int i = 0; i < count; ++i)
                {
                    channels[0][i] = dry.getSample (0, i) * (1.0f - mix) + channels[0][i] * mix * 0.5f;
                    channels[1][i] = dry.getSample (1, i) * (1.0f - mix) + channels[1][i] * mix * 0.5f;
                }
            }
        }

    private:
        void timerCallback() override { rebuild(); }

        /** The impulse response: early reflections, then a noise tail fading (and darkening) over the length. */
        void rebuild()
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load();
            if (v == built || sampleRate <= 0.0)
                return;
            built = v;
            const auto length = std::max (0.1f, v[0]);
            const auto pre = v[1] * 0.001;
            const auto early = v[2] * 0.01f;
            const auto brightStart = v[3], brightEnd = v[4];
            const auto density = v[5] * 0.01f;
            const auto width = v[6] * 0.01f;
            const bool reverse = juce::roundToInt (v[7]) == 1;
            const auto lowCut = v[9];

            const auto total = (int) ((pre + length * 1.1) * sampleRate);
            juce::AudioBuffer<float> ir (2, std::max (64, total));
            ir.clear();
            juce::Random random (1234);
            const auto preSamples = (int) (pre * sampleRate);
            // Early reflections: a few distinct echoes in the first 80 ms
            for (int e = 0; e < 14; ++e)
                for (int c = 0; c < 2; ++c)
                {
                    const auto at = preSamples + (int) ((0.004 + random.nextDouble() * 0.075) * sampleRate);
                    if (at < ir.getNumSamples())
                        ir.addSample (c, at, early * (random.nextFloat() * 1.4f - 0.7f) * (1.0f - (float) e / 14.0f));
                }
            // The tail: noise, sparser when density is low, darkening from start to end brightness
            std::array<OnePole, 2> lp;
            for (int c = 0; c < 2; ++c)
            {
                lp[(size_t) c].reset();
                OnePole cut;
                cut.setLowPass (lowCut, sampleRate);
                auto* d = ir.getWritePointer (c);
                for (int i = preSamples; i < ir.getNumSamples(); ++i)
                {
                    const auto t = (float) (i - preSamples) / (float) sampleRate;
                    const auto env = std::exp (-6.9f * t / length);
                    if ((i & 63) == 0)
                        lp[(size_t) c].setLowPass (brightStart * std::pow (brightEnd / brightStart, std::min (1.0f, t / length)), sampleRate);
                    auto noise = random.nextFloat() * 2.0f - 1.0f;
                    if (density < 1.0f && random.nextFloat() > density * density + 0.02f)
                        noise = 0.0f;
                    const auto fade = std::min (1.0f, t / 0.02f);   // (a soft onset after the early part)
                    d[i] += cut.highPass (lp[(size_t) c].lowPass (noise)) * env * fade;
                }
            }
            // Width: blend the two channels towards mono
            for (int i = 0; i < ir.getNumSamples(); ++i)
            {
                const auto a = ir.getSample (0, i), b = ir.getSample (1, i);
                const auto mid = 0.5f * (a + b), side = 0.5f * (a - b) * width;
                ir.setSample (0, i, mid + side);
                ir.setSample (1, i, mid - side);
            }
            if (reverse)
                for (int c = 0; c < 2; ++c)
                    ir.reverse (c, preSamples, ir.getNumSamples() - preSamples);
            convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
        }

        juce::dsp::Convolution convolution;
        juce::AudioBuffer<float> dry;
        int blockSize = 512;
        std::array<float, maxParams> built {};
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockReverbEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), s (" s");
    static const std::vector<StockEffect> effects {
        { { "studioReverb", "Studio Reverb", {
              { "space", "Space", "", 0.0f, 5.0f, 3.0f, 2.5f, { "Room", "Studio", "Chamber", "Hall", "Large Hall", "Cathedral" } },
              { "size", "Size", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "decay", "Decay", s, 0.1f, 20.0f, 2.2f, 2.5f },
              { "predelay", "Pre-Delay", ms, 0.0f, 250.0f, 15.0f, 30.0f },
              { "damping", "Damping", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 1000.0f, 100.0f, 150.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 9000.0f, 6000.0f },
              { "diffusion", "Diffusion", pc, 0.0f, 100.0f, 75.0f, 50.0f },
              { "mod", "Modulation", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "width", "Width", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 25.0f, 50.0f },
              { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Reverb" },
          make<StudioReverb> },

        { { "plateReverb", "Plate Reverb", {
              { "predelay", "Pre-Delay", ms, 0.0f, 200.0f, 10.0f, 30.0f },
              { "decay", "Decay", pc, 0.0f, 100.0f, 55.0f, 50.0f },
              { "size", "Size", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "bandwidth", "Bandwidth", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "damping", "Damping", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "mod", "Modulation", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 1000.0f, 120.0f, 150.0f },
              { "width", "Width", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 25.0f, 50.0f } }, "Reverb" },
          make<PlateReverb> },

        { { "springReverb", "Spring Reverb", {
              { "springs", "Springs", "", 0.0f, 2.0f, 2.0f, 1.0f, { "1", "2", "3" } },
              { "tension", "Tension", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "decay", "Decay", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "dispersion", "Dispersion", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "tone", "Tone", hz, 1000.0f, 10000.0f, 4500.0f, 4000.0f },
              { "drip", "Drip", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Reverb" },
          make<SpringReverb> },

        { { "convSpace", "Convolution Space", {
              { "length", "Length", s, 0.1f, 12.0f, 2.5f, 3.0f },
              { "predelay", "Pre-Delay", ms, 0.0f, 250.0f, 10.0f, 30.0f },
              { "early", "Early", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "brightStart", "Bright Start", hz, 1000.0f, 20000.0f, 14000.0f, 8000.0f },
              { "brightEnd", "Bright End", hz, 200.0f, 20000.0f, 2500.0f, 4000.0f },
              { "density", "Density", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "width", "Width", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "reverse", "Reverse", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "mix", "Mix", pc, 0.0f, 100.0f, 25.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 1000.0f, 80.0f, 150.0f } }, "Reverb" },
          make<ConvolutionSpace> },
    };
    return effects;
}

} // namespace riff
