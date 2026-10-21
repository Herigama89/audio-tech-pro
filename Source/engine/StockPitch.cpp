#include "StockDsp.h"

/*
    Pitch (Logic's Pitch folder): real-time pitch correction, a vocal transformer (pitch and formants
    moved separately, or a robot voice on one note), and a pitch shifter for chords and instruments.

    The first two share a real-time PSOLA engine: the voice's period is tracked (YIN, on a quarter-rate
    copy, refined at full rate), and two-period grains cut one period apart are laid down at the new
    period. The grains keep the voice's character (formants) unless asked to move it. They look 50 ms
    ahead, which the mixer's delay compensation accounts for.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    class RealtimePsola
    {
    public:
        void prepare (double rate)
        {
            sampleRate = rate;
            latency = (int) std::ceil (0.06 * rate);
            maxPeriod = (int) (rate / 70.0);
            size = 1;
            while (size < latency * 4 + 8192)
                size <<= 1;
            mask = size - 1;
            for (auto* b : { &inL, &inR, &accL, &accR, &weights })
                b->assign ((size_t) size, 0.0f);
            decimated.assign (1024, 0.0f);
            periods.fill (0.0f);
            time = 0;
            synth = (double) latency;
            mark = 0.0;
            lowPass = 0.0f;
            hopCount = 0;
        }

        int getLatency() const noexcept { return latency; }

        /** The period found nearest an input time (0: unvoiced), and the MIDI note it is. */
        float periodAt (juce::int64 inputTime) const noexcept
        {
            if (hopCount == 0)
                return 0.0f;
            // (detection j covered input up to window + j hops: its centre is half a window before that)
            const auto hop = juce::jlimit ((juce::int64) 0, hopCount - 1, (inputTime - window / 2) / hopSize);
            return hopCount > 0 ? periods[(size_t) (hop % (juce::int64) periods.size())] : 0.0f;
        }

        /** Runs one block. `ratioFor` gives the pitch ratio for a grain from its period (0 if unvoiced);
            formant > 1 moves the voice's character up; `fixedPeriod` > 0 lays grains at that spacing (robot). */
        template <typename RatioFn>
        void process (float* l, float* r, int n, RatioFn&& ratioFor, float formant, float fixedPeriod) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const auto t = time;
                inL[(size_t) (t & mask)] = l[i];
                inR[(size_t) (t & mask)] = r[i];
                // A quarter-rate copy for the period search
                lowPass += 0.35f * (0.5f * (l[i] + r[i]) - lowPass);
                if ((t & 3) == 0)
                    decimated[(size_t) ((t >> 2) & 1023)] = lowPass;
                ++time;
                if (time % hopSize == 0 && time >= window)
                    detect();

                // Lay down every grain that reaches this sample (grains are centred up to a period ahead)
                while (synth <= (double) time + 1.5 * maxPeriod)
                    addGrain (ratioFor, formant, fixedPeriod);

                const auto slot = (size_t) (t & mask);
                const auto w = weights[slot];
                const auto norm = 1.0f / std::max (0.35f, w);
                l[i] = accL[slot] * norm;
                r[i] = accR[slot] * norm;
                accL[slot] = accR[slot] = weights[slot] = 0.0f;
            }
        }

    private:
        static constexpr int hopSize = 256, window = 2048;

        float input (const std::vector<float>& b, double at) const noexcept
        {
            const auto base = (juce::int64) std::floor (at);
            const auto frac = (float) (at - (double) base);
            const auto a = b[(size_t) (base & mask)], c = b[(size_t) ((base + 1) & mask)];
            return a + frac * (c - a);
        }

        /** YIN on the last 2048 samples (at quarter rate), then the period refined at full rate. */
        void detect() noexcept
        {
            const auto rate4 = sampleRate / 4.0;
            const auto tauMin = std::max (2, (int) (rate4 / 1100.0)), tauMax = std::min (250, (int) (rate4 / 70.0));
            const auto count = window / 4 - tauMax;
            const auto end = time >> 2;
            const auto start = end - window / 4;
            float energy = 0.0f;
            for (int j = 0; j < window / 4; ++j)
            {
                const auto x = decimated[(size_t) ((start + j) & 1023)];
                energy += x * x;
            }
            float period = 0.0f;
            if (energy / (window / 4) > 1.0e-6f)   // (about -60 dB: quieter is taken as unvoiced)
            {
                std::array<float, 256> diff {};
                float running = 0.0f;
                int best = -1;
                float bestValue = 1.0f;
                for (int tau = 1; tau <= tauMax; ++tau)
                {
                    float d = 0.0f;
                    for (int j = 0; j < count; ++j)
                    {
                        const auto a = decimated[(size_t) ((start + j) & 1023)] - decimated[(size_t) ((start + j + tau) & 1023)];
                        d += a * a;
                    }
                    running += d;
                    diff[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
                }
                for (int tau = tauMin; tau < tauMax; ++tau)
                    if (diff[(size_t) tau] < 0.18f)
                    {
                        while (tau + 1 < tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau])
                            ++tau;
                        best = tau;
                        bestValue = diff[(size_t) tau];
                        break;
                    }
                if (best > 0 && bestValue < 0.3f)
                {
                    // Refine at full rate around four times the coarse lag
                    const auto centre = best * 4;
                    float values[11];
                    for (int k = 0; k < 11; ++k)
                    {
                        const auto lag = centre - 5 + k;
                        float d = 0.0f;
                        for (int j = 0; j < 1024; ++j)
                        {
                            const auto a = inL[(size_t) ((time - 1024 - lag + j) & mask)] + inR[(size_t) ((time - 1024 - lag + j) & mask)]
                                         - inL[(size_t) ((time - 1024 + j) & mask)] - inR[(size_t) ((time - 1024 + j) & mask)];
                            d += a * a;
                        }
                        values[k] = d;
                    }
                    const auto bestK = (int) (std::min_element (values, values + 11) - values);
                    auto refined = (float) (centre - 5 + bestK);
                    if (bestK > 0 && bestK < 10)
                    {
                        const auto a = values[bestK - 1], b = values[bestK], c = values[bestK + 1];
                        const auto den = a - 2.0f * b + c;
                        if (std::abs (den) > 1.0e-12f)
                            refined += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
                    }
                    period = refined;
                }
            }
            periods[(size_t) (hopCount % (juce::int64) periods.size())] = period;
            ++hopCount;
        }

        template <typename RatioFn>
        void addGrain (RatioFn& ratioFor, float formant, float fixedPeriod) noexcept
        {
            const auto centre = (juce::int64) std::llround (synth);
            const auto analysisTime = centre - latency;
            const auto pin = periodAt (analysisTime);
            float halfWidth, step;
            double source;
            if (pin > 0.0f)
            {
                // The analysis mark: kept a whole number of periods from the last, nearest this grain
                if (mark <= 0.0 || std::abs ((double) analysisTime - mark) > pin * 3.0)
                    mark = (double) analysisTime;
                else
                    mark += std::round (((double) analysisTime - mark) / pin) * pin;
                const auto ratio = fixedPeriod > 0.0f ? pin / fixedPeriod : std::max (0.25f, ratioFor (pin));
                step = fixedPeriod > 0.0f ? fixedPeriod : pin / ratio;
                // Two-period grains, narrower when going up (as TD-PSOLA: twice the smaller period)
                halfWidth = std::min (pin, step);
                source = mark;
            }
            else
            {
                // Unvoiced: plain overlap-add of the input (with the formant shift, if any)
                mark = 0.0;
                step = 256.0f;
                halfWidth = 256.0f;
                source = (double) analysisTime;
                formant = 1.0f;
            }
            const auto reach = (int) std::ceil (halfWidth);
            for (int k = -reach; k <= reach; ++k)
            {
                const auto x = (float) k / halfWidth;
                if (x <= -1.0f || x >= 1.0f)
                    continue;
                const auto w = 0.5f + 0.5f * std::cos (pi * x);
                const auto at = source + k * formant;
                if (at > (double) (time - 2) || at < (double) (time - size + 4))
                    continue;   // (beyond what's been heard yet: only with extreme formant settings)
                const auto slot = (size_t) ((centre + k) & mask);
                accL[slot] += w * input (inL, at);
                accR[slot] += w * input (inR, at);
                weights[slot] += w;
            }
            synth += step;
        }

        double sampleRate = 48000.0, synth = 0.0, mark = 0.0;
        int latency = 2400, maxPeriod = 686, size = 0, mask = 0;
        juce::int64 time = 0, hopCount = 0;
        std::vector<float> inL, inR, accL, accR, weights, decimated;
        std::array<float, 64> periods {};
        float lowPass = 0.0f;
    };

    inline float midiOfPeriod (float period, double rate) noexcept
    {
        return 69.0f + 12.0f * std::log2 ((float) rate / (period * 440.0f));
    }

    //==============================================================================
    /** Pitch correction: each note pulled to the nearest note of the key, as fast as the Response says. */
    class PitchCorrection final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; psola.prepare (rate); shift = 0.0f; }
        int getLatencySamples() const override { return psola.getLatency(); }
        float displayValue() const noexcept override { return shownCents.load (std::memory_order_relaxed); }

        void process (float* l, float* r, int n) noexcept override
        {
            static const int scales[][12] {
                { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 },
                { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 } };
            const auto key = choice (0), scale = juce::jlimit (0, 5, choice (1));
            const auto response = std::max (1.0f, value (2));
            const auto detune = value (3) * 0.01f;
            const auto amount = value (4) * 0.01f;
            const auto rate = sampleRate;
            auto ratioFor = [&] (float period)
            {
                const auto note = midiOfPeriod (period, rate);
                // The nearest note that's in the key
                const auto inKey = [&] (int m) { return scales[scale][((m - key) % 12 + 12) % 12] == 1; };
                const auto nearest = (int) std::round (note);
                auto target = (float) nearest;
                for (int d = 0; d <= 6; ++d)
                {
                    const int candidates[] { nearest - d, nearest + d };
                    auto bestDistance = 99.0f;
                    for (const auto m : candidates)
                        if (inKey (m) && std::abs ((float) m - note) < bestDistance)
                        {
                            bestDistance = std::abs ((float) m - note);
                            target = (float) m;
                        }
                    if (bestDistance < 99.0f)
                        break;
                }
                const auto wanted = (target - note) * amount + detune;
                // Response: how quickly the voice is pulled there (per grain, one period at a time)
                const auto c = std::exp (-period / (response * 0.001f * (float) rate));
                shift = wanted + c * (shift - wanted);
                return std::pow (2.0f, shift / 12.0f);
            };
            psola.process (l, r, n, ratioFor, 1.0f, 0.0f);
            shownCents.store ((shift - value (3) * 0.01f) * 100.0f, std::memory_order_relaxed);
        }
    private:
        RealtimePsola psola;
        float shift = 0.0f;
        std::atomic<float> shownCents { 0.0f };
    };

    //==============================================================================
    /** Pitch and formants moved on their own (a deeper voice that isn't slowed, a higher one that isn't a chipmunk), or a robot. */
    class VocalTransformer final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            psola.prepare (rate);
            dryDelayL.prepare (psola.getLatency() + std::max (16, maxBlock) + 8);
            dryDelayR.prepare (psola.getLatency() + std::max (16, maxBlock) + 8);
        }
        int getLatencySamples() const override { return psola.getLatency(); }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto ratio = std::pow (2.0f, value (0) / 12.0f);
            const auto formant = std::pow (2.0f, value (1) / 12.0f);
            const bool robot = choice (2) == 1;
            const auto robotNote = 36.0f + (float) choice (3);
            const auto fixedPeriod = robot ? (float) (sampleRate / (440.0 * std::pow (2.0, (robotNote - 69.0f) / 12.0))) : 0.0f;
            const auto mix = value (4) * 0.01f;
            const auto latency = psola.getLatency();
            for (int at = 0; at < n; at += 1024)
            {
                const auto count = std::min (1024, n - at);
                for (int i = 0; i < count; ++i)
                {
                    dryDelayL.push (l[at + i]);
                    dryDelayR.push (r[at + i]);
                    dryL[(size_t) i] = dryDelayL.readInt (latency + 1);
                    dryR[(size_t) i] = dryDelayR.readInt (latency + 1);
                }
                psola.process (l + at, r + at, count, [ratio] (float) { return ratio; }, formant, fixedPeriod);
                for (int i = 0; i < count; ++i)
                {
                    l[at + i] = dryL[(size_t) i] * (1.0f - mix) + l[at + i] * mix;
                    r[at + i] = dryR[(size_t) i] * (1.0f - mix) + r[at + i] * mix;
                }
            }
        }
    private:
        RealtimePsola psola;
        DelayLine dryDelayL, dryDelayR;
        std::array<float, 1024> dryL {}, dryR {};
    };

    //==============================================================================
    /** Shifts anything (chords, drums) by two crossfading delay taps sweeping through a window. */
    class PitchShifter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.25 * rate));
            dr.prepare ((int) (0.25 * rate));
            phase = 0.0;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto ratio = std::pow (2.0, (value (0) + value (1) * 0.01) / 12.0);
            const auto windowSamples = std::max (64.0, value (2) * 0.001 * sampleRate);
            const auto mix = value (3) * 0.01f;
            const auto delayBase = value (4) * 0.001 * sampleRate;
            const auto step = (1.0 - ratio) / windowSamples;   // the taps' sweep, as a fraction of the window per sample
            for (int i = 0; i < n; ++i)
            {
                dl.push (l[i]);
                dr.push (r[i]);
                phase += step;
                phase -= std::floor (phase);
                const auto p2 = phase + 0.5 - std::floor (phase + 0.5);
                const auto d1 = (float) (delayBase + 2.0 + phase * windowSamples), d2 = (float) (delayBase + 2.0 + p2 * windowSamples);
                const auto w1 = (float) std::sin (juce::MathConstants<double>::pi * phase), w2 = (float) std::sin (juce::MathConstants<double>::pi * p2);
                // (sin windows half a cycle apart: their squares add to one)
                const auto a = dl.read (d1) * w1 * w1 + dl.read (d2) * w2 * w2;
                const auto b = dr.read (d1) * w1 * w1 + dr.read (d2) * w2 * w2;
                l[i] = l[i] * (1.0f - mix) + a * mix;
                r[i] = r[i] * (1.0f - mix) + b * mix;
            }
        }
    private:
        DelayLine dl, dr;
        double phase = 0.0;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockPitchEffects()
{
    const juce::String pc (" %"), ms (" ms"), st (" st");
    std::vector<juce::String> keys;
    for (int k = 0; k < 12; ++k)
        keys.push_back (juce::MidiMessage::getMidiNoteName (k, true, false, 3));
    std::vector<juce::String> robotNotes;
    for (int m = 36; m <= 72; ++m)
        robotNotes.push_back (juce::MidiMessage::getMidiNoteName (m, true, true, 3));
    static const std::vector<StockEffect> effects {
        { { "pitchCorrection", "Pitch Correction", {
              { "key", "Key", "", 0.0f, 11.0f, 0.0f, 5.5f, keys },
              { "scale", "Scale", "", 0.0f, 5.0f, 1.0f, 2.5f, { "Chromatic", "Major", "Minor", "Major Pentatonic", "Minor Pentatonic", "Harmonic Minor" } },
              { "response", "Response", ms, 1.0f, 400.0f, 40.0f, 50.0f },
              { "detune", "Detune", " cents", -50.0f, 50.0f, 0.0f, 0.0f },
              { "amount", "Amount", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Pitch" },
          make<PitchCorrection> },
        { { "vocalTransformer", "Vocal Transformer", {
              { "pitch", "Pitch", st, -24.0f, 24.0f, 0.0f, 0.0f },
              { "formant", "Formant", st, -12.0f, 12.0f, 0.0f, 0.0f },
              { "mode", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Natural", "Robot" } },
              { "robotNote", "Robot Note", "", 0.0f, 36.0f, 12.0f, 18.0f, robotNotes },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Pitch" },
          make<VocalTransformer> },
        { { "pitchShifter", "Pitch Shifter", {
              { "semitones", "Semitones", st, -24.0f, 24.0f, 0.0f, 0.0f },
              { "cents", "Cents", "", -100.0f, 100.0f, 0.0f, 0.0f },
              { "window", "Window", ms, 20.0f, 150.0f, 60.0f, 60.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "delay", "Delay", ms, 0.0f, 100.0f, 0.0f, 20.0f } }, "Pitch" },
          make<PitchShifter> },
    };
    return effects;
}

} // namespace riff
