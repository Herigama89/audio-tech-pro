#include "StockDsp.h"

/*
    Imaging, Utility and Specialized (Logic's folders of the same names): stereo spread, direction
    mixer, gain, test oscillator, vocoder, exciter and a spectral denoiser.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    //==============================================================================
    /** Widens by sending alternate frequency bands left and right (mono-compatible: it only adds side). */
    class StereoSpread final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& c : splits) c.reset();
            last.fill (-1.0f);
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto bands = juce::jlimit (2, 8, choice (0) + 2);
            const auto lowAmount = value (1) * 0.01f, highAmount = value (2) * 0.01f;
            const auto lowF = value (3), highF = std::max (value (3) * 2.0f, value (4));
            const auto mix = value (5) * 0.01f;
            const std::array<float, 3> now { (float) bands, lowF, highF };
            if (now != last)
            {
                for (int b = 0; b < bands - 1; ++b)
                    splits[(size_t) b].set (lowF * std::pow (highF / lowF, (b + 1.0f) / (float) bands), sampleRate);
                last = now;
            }
            for (int i = 0; i < n; ++i)
            {
                const auto mid = 0.5f * (l[i] + r[i]);
                auto rest = mid, side = 0.0f;
                for (int b = 0; b < bands; ++b)
                {
                    float band, higher = 0.0f;
                    if (b < bands - 1)
                        splits[(size_t) b].split (rest, 0, band, higher);
                    else
                        band = rest;
                    const auto amount = lowAmount + (highAmount - lowAmount) * (float) b / (float) (bands - 1);
                    side += (b % 2 == 0 ? 1.0f : -1.0f) * band * amount;
                    rest = higher;
                }
                l[i] += side * mix;
                r[i] -= side * mix;
            }
        }
    private:
        std::array<Crossover, 7> splits;
        std::array<float, 3> last {};
    };

    //==============================================================================
    /** Width and direction of a stereo (or mid/side) signal. */
    /**
        Direction Mixer (as Logic's): an LR or MS input, the direction its centre points to (degrees: 90 hard
        left, -90 hard right, beyond that turning back) and its spread (0 mono, 1 as is, 2 only the sides);
        Split gives the lows and highs (either side of the crossover) their own direction and spread.
    */
    class DirectionMixer final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iInput, iSpread, iDirection, iOutput, iSplit, iCrossover, iDirLow, iSpreadLow };
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; split.reset(); lastX = -1.0f; }
        static void place (float m, float s, float spread, float degrees, float& outL, float& outR) noexcept
        {
            // (the stereo base turned: 0 centre, +90 hard left, -90 hard right, 180 back to centre with the sides swapped)
            const auto rad = degrees / 180.0f * pi;
            const auto pan = std::sin (rad);                  // -1 right .. +1 left
            const auto flip = std::cos (rad) < 0.0f ? -1.0f : 1.0f;
            s *= spread * flip;
            const auto angle = (1.0f - pan) * pi * 0.25f;     // 0 hard left .. pi/2 hard right
            const auto gl = 1.41421356f * std::cos (angle), gr = 1.41421356f * std::sin (angle);
            outL = (m + s) * gl;
            outR = (m - s) * gr;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const bool msInput = choice (iInput) == 1;
            const auto out = dbToGain (value (iOutput));
            const auto splitOn = choice (iSplit) == 1;
            if (splitOn && value (iCrossover) != lastX)
            {
                split.set (value (iCrossover), sampleRate);
                lastX = value (iCrossover);
            }
            for (int i = 0; i < n; ++i)
            {
                const auto m = msInput ? l[i] : 0.5f * (l[i] + r[i]);
                const auto s = msInput ? r[i] : 0.5f * (l[i] - r[i]);
                float a, b;
                if (splitOn)
                {
                    float mLow, mHigh, sLow, sHigh;
                    split.split (m, 0, mLow, mHigh);
                    split.split (s, 1, sLow, sHigh);
                    float la, lb, ha, hb;
                    place (mLow, sLow, value (iSpreadLow), value (iDirLow), la, lb);
                    place (mHigh, sHigh, value (iSpread), value (iDirection), ha, hb);
                    a = la + ha; b = lb + hb;
                }
                else
                    place (m, s, value (iSpread), value (iDirection), a, b);
                l[i] = a * out;
                r[i] = b * out;
            }
        }
    private:
        Crossover split;
        float lastX = -1.0f;
    };

    //==============================================================================
    class Gain final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; smoothed.reset (rate, 0.02); smoothed.setCurrentAndTargetValue (dbToGain (value (0))); }
        void process (float* l, float* r, int n) noexcept override
        {
            smoothed.setTargetValue (value (0) <= -95.0f ? 0.0f : dbToGain (value (0)));
            const auto invL = choice (1) == 1 ? -1.0f : 1.0f, invR = choice (2) == 1 ? -1.0f : 1.0f;
            const bool swap = choice (3) == 1;
            const auto mono = choice (4);
            const auto balance = value (5) * 0.01f;
            const auto bl = std::min (1.0f, 1.0f - balance), br = std::min (1.0f, 1.0f + balance);
            for (int i = 0; i < n; ++i)
            {
                const auto g = smoothed.getNextValue();
                auto a = l[i] * invL, b = r[i] * invR;
                if (swap)
                    std::swap (a, b);
                if (mono == 1) a = b = 0.5f * (a + b);
                else if (mono == 2) b = a;
                else if (mono == 3) a = b;
                l[i] = a * g * bl;
                r[i] = b * g * br;
            }
        }
    private:
        juce::SmoothedValue<float> smoothed { 1.0f };
    };

    //==============================================================================
    /** A test tone, noise or a sweep, in place of (or added to) the track. */
    class TestOscillator final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; phase = sweepTime = 0.0; pink.fill (0.0f); }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto wave = choice (0);
            const auto freq = (double) value (1);
            const auto level = dbToGain (value (2));
            const auto sweepSeconds = std::max (0.5, (double) value (3));
            const bool add = choice (4) == 1;
            for (int i = 0; i < n; ++i)
            {
                auto f = freq;
                if (wave == 6)
                {
                    // Sweep: 20 Hz to 20 kHz, up the octaves evenly, then again
                    sweepTime += 1.0 / sampleRate;
                    if (sweepTime > sweepSeconds)
                        sweepTime = 0.0;
                    f = 20.0 * std::pow (1000.0, sweepTime / sweepSeconds);
                }
                phase += f / sampleRate;
                phase -= std::floor (phase);
                float x;
                switch (wave)
                {
                    case 1: x = phase < 0.5 ? 1.0f : -1.0f; break;
                    case 2: x = (float) (2.0 * phase - 1.0); break;
                    case 3: x = (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); break;
                    case 4: x = noise.next(); break;
                    case 5:
                    {
                        // Pink: Paul Kellet's filter on white noise
                        const auto w = noise.next();
                        pink[0] = 0.99886f * pink[0] + w * 0.0555179f; pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
                        pink[2] = 0.96900f * pink[2] + w * 0.1538520f; pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
                        pink[4] = 0.55000f * pink[4] + w * 0.5329522f; pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
                        x = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + w * 0.5362f) * 0.11f;
                        break;
                    }
                    default: x = (float) std::sin (juce::MathConstants<double>::twoPi * phase); break;
                }
                l[i] = (add ? l[i] : 0.0f) + x * level;
                r[i] = (add ? r[i] : 0.0f) + x * level;
            }
        }
    private:
        double phase = 0.0, sweepTime = 0.0;
        Noise noise;
        std::array<float, 6> pink {};
    };

    //==============================================================================
    /** The classic vocoder: the track's spectrum (bands followed) shapes a carrier - the side chain or a built-in synth. */
    class Vocoder final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& b : bands) { b.mod.reset(); b.mod2.reset(); b.car.reset(); b.car2.reset(); b.env = 0.0f; }
            last.fill (-1.0f);
            phase = 0.0;
            hiss.reset();
        }
        void setSidechain (const float* left, const float* right) noexcept override { keyL = left; keyR = right != nullptr ? right : left; }
        bool acceptsSidechain() const override { return true; }

        void process (float* l, float* r, int n) noexcept override
        {
            static const int counts[] { 8, 12, 16, 24, 32 };
            const auto count = counts[juce::jlimit (0, 4, choice (0))];
            const auto low = value (1), high = std::max (value (1) * 4.0f, value (2));
            const auto attack = coeffMs (value (3), sampleRate), release = coeffMs (value (4), sampleRate);
            const auto shift = std::pow (2.0f, value (5) / 12.0f);
            const auto carrier = choice (6);
            const auto noteHz = 440.0 * std::pow (2.0, ((double) (24 + choice (7)) - 69.0) / 12.0);
            const auto unvoiced = value (8) * 0.01f;
            const auto mix = value (9) * 0.01f;
            const std::array<float, 4> now { (float) count, low, high, shift };
            if (now != last)
            {
                const auto q = std::max (2.0, count / 2.5);
                for (int b = 0; b < count; ++b)
                {
                    const auto f = low * std::pow (high / low, (b + 0.5f) / (float) count);
                    bands[(size_t) b].mod.setBandPass (sampleRate, f, q);
                    bands[(size_t) b].mod2.setBandPass (sampleRate, f, q);
                    bands[(size_t) b].car.setBandPass (sampleRate, std::min ((double) f * shift, sampleRate * 0.45), q);
                    bands[(size_t) b].car2.setBandPass (sampleRate, std::min ((double) f * shift, sampleRate * 0.45), q);
                }
                last = now;
            }
            hiss.setLowPass (6000.0, sampleRate);
            const auto norm = 3.0f * std::sqrt ((float) count);

            for (int i = 0; i < n; ++i)
            {
                const auto modulator = 0.5f * (l[i] + r[i]);
                // The carrier: the side chain, or a bright saw (with noise for consonants)
                float c;
                if (carrier == 0 && keyL != nullptr)
                    c = 0.5f * (keyL[i] + keyR[i]);
                else
                {
                    phase += noteHz / sampleRate;
                    phase -= std::floor (phase);
                    const auto saw = (float) (2.0 * phase - 1.0);
                    const auto n2 = noise.next();
                    c = carrier == 3 ? n2 : saw + (carrier == 2 ? 0.3f * n2 : 0.0f);
                }
                // Consonants: the modulator's hiss passes as noise
                const auto hf = modulator - hiss.lowPass (modulator);
                float out = 0.0f;
                for (int b = 0; b < count; ++b)
                {
                    auto& band = bands[(size_t) b];
                    const auto m = std::abs (band.mod2.process (band.mod.process (modulator, 0), 0));
                    band.env = m + (m > band.env ? attack : release) * (band.env - m);
                    out += band.car2.process (band.car.process (c, 0), 0) * band.env;
                }
                out = out * norm + hf * unvoiced;
                l[i] = l[i] * (1.0f - mix) + out * mix;
                r[i] = r[i] * (1.0f - mix) + out * mix;
            }
        }
    private:
        struct Band { Biquad mod, mod2, car, car2; float env = 0.0f; };
        std::array<Band, 32> bands;
        std::array<float, 4> last {};
        const float* keyL = nullptr;
        const float* keyR = nullptr;
        double phase = 0.0;
        Noise noise;
        OnePole hiss;
    };

    //==============================================================================
    /** Adds brightness: new harmonics made from the highs only. */
    class Exciter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; for (auto* f : { &hpL, &hpR, &postL, &postR }) f->reset(); lastF = -1.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto freq = value (0);
            if (freq != lastF)
            {
                for (auto* f : { &hpL, &hpR }) f->setHighPass (sampleRate, freq, 0.707);
                for (auto* f : { &postL, &postR }) f->setHighPass (sampleRate, freq * 1.2, 0.707);
                lastF = freq;
            }
            const auto drive = 1.0f + value (1) * 0.01f * 9.0f;
            const auto amount = value (2) * 0.01f;
            const bool even = choice (3) == 1;
            const auto dry = choice (4) == 1 ? 1.0f : 0.0f;
            const auto highRatio = value (5) * 0.01f;   // (the generated harmonics against the source's own top)
            for (int i = 0; i < n; ++i)
            {
                // Odd: a soft clip; even: a rectifier-like curve (its offset is filtered away after)
                const auto shape = [&] (float x)
                {
                    const auto y = x * drive;
                    return even ? y * y / (1.0f + y * y) : saturate (y);
                };
                const auto hl = hpL.process (l[i], 0), hr = hpR.process (r[i], 0);
                const auto a = postL.process (shape (hl), 0), b = postR.process (shape (hr), 0);
                l[i] = l[i] * dry + (a * highRatio + hl * (1.0f - highRatio)) * amount * (dry > 0.0f ? 1.0f : 2.0f);
                r[i] = r[i] * dry + (b * highRatio + hr * (1.0f - highRatio)) * amount * (dry > 0.0f ? 1.0f : 2.0f);
            }
        }
    private:
        Biquad hpL, hpR, postL, postR;
        float lastF = -1.0f;
    };

    //==============================================================================
    /** Spectral noise reduction: each frequency below the threshold is turned down, smoothly over time. */
    class Denoiser final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        static constexpr int order = 10, fftSize = 1 << order, hop = fftSize / 4;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& c : channels)
            {
                c.input.assign ((size_t) fftSize, 0.0f);
                c.output.assign ((size_t) fftSize * 2, 0.0f);
                c.gains.assign ((size_t) fftSize / 2 + 1, 1.0f);
                c.fill = c.outPos = c.sinceFrame = 0;
            }
            work.assign ((size_t) fftSize * 2, 0.0f);
            window.resize ((size_t) fftSize);
            for (int i = 0; i < fftSize; ++i)   // (root Hann, in and out: they overlap-add flat at 75%)
                window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (twoPi * (float) i / (float) fftSize));
        }
        int getLatencySamples() const override { return fftSize; }

        void process (float* l, float* r, int n) noexcept override
        {
            for (int i = 0; i < n; ++i)
            {
                l[i] = step (channels[0], l[i]);
                r[i] = step (channels[1], r[i]);
            }
        }

    private:
        struct Channel
        {
            std::vector<float> input, output, gains;
            int fill = 0, outPos = 0, sinceFrame = 0;
        };

        float step (Channel& c, float x) noexcept
        {
            // The last fftSize inputs in a ring; every hop a frame is cleaned and added a frame's length ahead
            c.input[(size_t) c.fill] = x;
            c.fill = (c.fill + 1) % fftSize;
            const auto y = c.output[(size_t) c.outPos];
            c.output[(size_t) c.outPos] = 0.0f;
            c.outPos = (c.outPos + 1) % (fftSize * 2);
            if (++c.sinceFrame >= hop)
            {
                c.sinceFrame = 0;
                frame (c);
            }
            return y;
        }

        void frame (Channel& c) noexcept
        {
            const auto threshold = dbToGain (value (0));
            const auto floor = dbToGain (-value (1));
            const auto smoothing = value (2) * 0.01f;
            const auto attack = 0.3f + 0.65f * smoothing, release = 0.6f + 0.38f * smoothing;
            std::fill (work.begin(), work.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                work[(size_t) i] = c.input[(size_t) ((c.fill + i) % fftSize)] * window[(size_t) i];
            fft.performRealOnlyForwardTransform (work.data(), true);
            const auto bins = fftSize / 2 + 1;
            const auto scale = 2.0f / (float) fftSize;
            for (int b = 0; b < bins; ++b)
            {
                const auto re = work[(size_t) (2 * b)], im = work[(size_t) (2 * b + 1)];
                const auto target = std::sqrt (re * re + im * im) * scale < threshold ? floor : 1.0f;
                auto& g = c.gains[(size_t) b];
                g = target + (target < g ? attack : release) * (g - target);
            }
            for (int b = 0; b < bins; ++b)
            {
                // (smoothed across neighbouring frequencies too: fewer "musical" chirps)
                const auto g = (c.gains[(size_t) std::max (0, b - 1)] + 2.0f * c.gains[(size_t) b] + c.gains[(size_t) std::min (bins - 1, b + 1)]) * 0.25f;
                work[(size_t) (2 * b)] *= g;
                work[(size_t) (2 * b + 1)] *= g;
            }
            fft.performRealOnlyInverseTransform (work.data());
            for (int i = 0; i < fftSize; ++i)
                c.output[(size_t) ((c.outPos + i) % (fftSize * 2))] += work[(size_t) i] * window[(size_t) i] * 0.5f;
        }

        std::array<Channel, 2> channels;
        juce::dsp::FFT fft { order };
        std::vector<float> work, window;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }

    std::vector<juce::String> noteNamesFrom (int first, int last)
    {
        std::vector<juce::String> names;
        for (int m = first; m <= last; ++m)
            names.push_back (juce::MidiMessage::getMidiNoteName (m, true, true, 3));
        return names;
    }
}

const std::vector<StockEffect>& stockUtilityEffects()
{
    const juce::String dB (" dB"), hz (" Hz"), pc (" %"), ms (" ms");
    static const std::vector<StockEffect> effects {
        { { "stereoSpread", "Stereo Spread", {
              { "order", "Bands", "", 0.0f, 6.0f, 6.0f, 3.0f, { "2", "3", "4", "5", "6", "7", "8" } },
              { "lowInt", "Lower Intensity", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "highInt", "Upper Intensity", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "lowF", "Lower Freq", hz, 80.0f, 2000.0f, 300.0f, 400.0f },
              { "highF", "Upper Freq", hz, 1000.0f, 18000.0f, 8000.0f, 6000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Imaging" },
          make<StereoSpread> },
        { { "directionMixer", "Direction Mixer", {
              { "input", "Input", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Left/Right", "Mid/Side" } },
              { "spread", "Spread (High)", "", 0.0f, 2.0f, 1.0f, 1.0f },
              { "direction", "Direction (High)", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), -180.0f, 180.0f, 0.0f, 0.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "split", "Split", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "crossover", "Crossover", hz, 50.0f, 5000.0f, 300.0f, 300.0f },
              { "dirLow", "Direction Low", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), -180.0f, 180.0f, 0.0f, 0.0f },
              { "spreadLow", "Spread Low", "", 0.0f, 2.0f, 1.0f, 1.0f } }, "Imaging" },
          make<DirectionMixer> },
        { { "gain", "Gain", {
              { "gain", "Gain", dB, -96.0f, 24.0f, 0.0f, 0.0f },
              { "invL", "Phase L", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Inverted" } },
              { "invR", "Phase R", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Inverted" } },
              { "swap", "Swap L/R", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "mono", "Mono", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Off", "Mono", "Left Only", "Right Only" } },
              { "balance", "Balance", "", -100.0f, 100.0f, 0.0f, 0.0f } }, "Utility" },
          make<Gain> },
        { { "testOsc", "Test Oscillator", {
              { "wave", "Waveform", "", 0.0f, 6.0f, 0.0f, 3.0f, { "Sine", "Square", "Saw", "Triangle", "White Noise", "Pink Noise", "Sweep" } },
              { "freq", "Frequency", hz, 20.0f, 20000.0f, 1000.0f, 1000.0f },
              { "level", "Level", dB, -60.0f, 0.0f, -18.0f, -18.0f },
              { "sweep", "Sweep Time", " s", 0.5f, 30.0f, 5.0f, 5.0f },
              { "mode", "Signal", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Replace", "Add" } } }, "Utility" },
          make<TestOscillator> },
        { { "vocoder", "Vocoder", {
              { "bands", "Bands", "", 0.0f, 4.0f, 2.0f, 2.0f, { "8", "12", "16", "24", "32" } },
              { "low", "Lowest", hz, 50.0f, 500.0f, 120.0f, 150.0f },
              { "high", "Highest", hz, 2000.0f, 14000.0f, 8000.0f, 6000.0f },
              { "attack", "Attack", ms, 0.5f, 50.0f, 3.0f, 5.0f },
              { "release", "Release", ms, 5.0f, 500.0f, 40.0f, 60.0f },
              { "formant", "Formant", " st", -12.0f, 12.0f, 0.0f, 0.0f },
              { "carrier", "Carrier", "", 0.0f, 3.0f, 1.0f, 1.5f, { "Side Chain", "Saw", "Saw + Noise", "Noise" } },
              { "note", "Carrier Note", "", 0.0f, 48.0f, 24.0f, 24.0f, noteNamesFrom (24, 72) },
              { "unvoiced", "Unvoiced", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Specialized" },
          make<Vocoder> },
        { { "exciter", "Exciter", {
              { "freq", "Frequency", hz, 1000.0f, 12000.0f, 4000.0f, 4000.0f },
              { "harmonics", "Harmonics", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "amount", "Amount", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "color", "Color", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Color 1", "Color 2" } },
              { "dry", "Dry Signal", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "highRatio", "High Ratio", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Specialized" },
          make<Exciter> },
        { { "denoiser", "Denoiser", {
              { "threshold", "Threshold", dB, -100.0f, -20.0f, -70.0f, -60.0f },
              { "reduction", "Reduction", dB, 0.0f, 40.0f, 15.0f, 15.0f },
              { "smoothing", "Smoothing", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Legacy" },
          make<Denoiser> },
    };
    return effects;
}

} // namespace riff
