#include "StockDsp.h"

/*
    The stock modulation effects (Logic's Modulation folder): chorus, ensemble, flanger, phaser,
    tremolo, auto pan, a rotary speaker cabinet, ring modulator and vibrato. Rates can follow the
    song's tempo where Logic's do.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** An LFO shape at a phase (0..1): sine, triangle, square (softened), saw down, random steps. */
    inline float shapeAt (int shape, double phase, float randomStep) noexcept
    {
        phase -= std::floor (phase);
        switch (shape)
        {
            case 1:  return (float) (phase < 0.5 ? phase * 4.0 - 1.0 : 3.0 - phase * 4.0);
            case 2:  return std::tanh ((float) std::sin (juce::MathConstants<double>::twoPi * phase) * 6.0f) / 0.99999f;
            case 3:  return (float) (1.0 - phase * 2.0);
            case 4:  return randomStep;
            default: return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
        }
    }

    /** Rate in Hz from either Hz or a note value at the song's tempo. */
    inline double rateHz (bool sync, float hz, int note, double bpm) noexcept
    {
        return sync ? 1.0 / noteSeconds (note, bpm) : (double) hz;
    }

    //==============================================================================
    class Chorus final : public BuiltInEffect
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
            const auto rate = value (0), depth = value (1) * 0.01f * 0.006f * (float) sampleRate;
            const auto base = value (2) * 0.001f * (float) sampleRate;
            const auto voices = juce::jlimit (1, 4, choice (3) + 1);
            const auto spread = value (4) * 0.01f;
            const auto feedback = value (5) * 0.01f;
            const auto mix = value (6) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                float wl = 0.0f, wr = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    const auto off = (double) v / voices;
                    wl += dl.read (base + depth * (0.5f + 0.5f * lfo.peek (off)));
                    wr += dr.read (base + depth * (0.5f + 0.5f * lfo.peek (off + 0.25 * spread)));
                }
                lfo.next (rate, sampleRate);
                wl /= (float) voices;
                wr /= (float) voices;
                dl.push (l[i] + wl * feedback);
                dr.push (r[i] + wr * feedback);
                l[i] = l[i] * (1.0f - mix * 0.5f) + wl * mix;
                r[i] = r[i] * (1.0f - mix * 0.5f) + wr * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
    };

    //==============================================================================
    /** Many slightly detuned voices (as a string ensemble): two LFOs per voice, spread across the stereo field. */
    class Ensemble final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            line.prepare ((int) (0.05 * rate));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto voices = juce::jlimit (2, 8, choice (0) + 2);
            const auto rate = value (1), depth = value (2) * 0.01f * 0.004f * (float) sampleRate;
            const auto spread = value (3) * 0.01f;
            const auto mix = value (4) * 0.01f;
            const auto base = 0.012f * (float) sampleRate;
            for (int i = 0; i < n; ++i)
            {
                line.push (0.5f * (l[i] + r[i]));
                float wl = 0.0f, wr = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    const auto off = (double) v / voices;
                    const auto m = 0.6f * slow.peek (off) + 0.4f * fast.peek (off * 1.7);
                    const auto s = line.read (base + depth * (1.0f + m));
                    const auto pan = voices > 1 ? ((float) v / (voices - 1) * 2.0f - 1.0f) * spread : 0.0f;
                    wl += s * (1.0f - pan) * 0.5f;
                    wr += s * (1.0f + pan) * 0.5f;
                }
                slow.next (rate, sampleRate);
                fast.next (rate * 6.3, sampleRate);
                const auto norm = 2.0f / (float) voices;
                l[i] = l[i] * (1.0f - mix * 0.5f) + wl * norm * mix;
                r[i] = r[i] * (1.0f - mix * 0.5f) + wr * norm * mix;
            }
        }
    private:
        DelayLine line;
        Lfo slow, fast;
    };

    //==============================================================================
    class Flanger final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.03 * rate));
            dr.prepare ((int) (0.03 * rate));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed));
            const auto base = value (3) * 0.001f * (float) sampleRate;
            const auto depth = value (4) * 0.01f * 0.008f * (float) sampleRate;
            const auto feedback = value (5) * 0.01f * 0.95f;
            const auto phase = value (6) / 360.0;
            const auto mix = value (7) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto el = dl.read (std::max (1.0f, base + depth * (0.5f + 0.5f * lfo.peek (0.0))));
                const auto er = dr.read (std::max (1.0f, base + depth * (0.5f + 0.5f * lfo.peek (phase))));
                lfo.next (rate, sampleRate);
                dl.push (l[i] + el * feedback);
                dr.push (r[i] + er * feedback);
                l[i] = (l[i] + el * mix) / (1.0f + mix * 0.5f);
                r[i] = (r[i] + er * mix) / (1.0f + mix * 0.5f);
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
    };

    //==============================================================================
    /** All-pass stages swept by an LFO: notches move through the sound. */
    class Phaser final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& a : stagesL) a.z = 0.0f;
            for (auto& a : stagesR) a.z = 0.0f;
            fbL = fbR = 0.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            static const int counts[] { 4, 6, 8, 12 };
            const auto stages = counts[juce::jlimit (0, 3, choice (0))];
            const auto rate = rateHz (choice (1) == 1, value (2), choice (3), bpm.load (std::memory_order_relaxed));
            const auto centre = value (4), depth = value (5) * 0.01f;
            const auto feedback = value (6) * 0.01f * 0.9f;
            const auto phase = value (7) / 360.0;
            const auto mix = value (8) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto coeffFor = [&] (float m)
                {
                    const auto f = centre * std::pow (2.0f, m * depth * 2.5f);   // +-2.5 octaves
                    const auto t = std::tan (pi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, f) / (float) sampleRate);
                    return (t - 1.0f) / (t + 1.0f);
                };
                const auto al = coeffFor (lfo.peek (0.0)), ar = coeffFor (lfo.peek (phase));
                lfo.next (rate, sampleRate);
                auto a = l[i] + fbL * feedback, b = r[i] + fbR * feedback;
                for (int s = 0; s < stages; ++s)
                {
                    a = stagesL[(size_t) s].process (a, al);
                    b = stagesR[(size_t) s].process (b, ar);
                }
                fbL = a;
                fbR = b;
                l[i] = (l[i] + a * mix) / (1.0f + mix * 0.5f);
                r[i] = (r[i] + b * mix) / (1.0f + mix * 0.5f);
            }
        }
    private:
        std::array<AllPass1, 12> stagesL, stagesR;
        float fbL = 0.0f, fbR = 0.0f;
        Lfo lfo;
    };

    //==============================================================================
    /** Level up and down in time (and Auto Pan: the same, opposite on each side). */
    template <bool Pan>
    class Tremolo final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; phase = 0.0; smoothL = smoothR = Pan ? 0.0f : 1.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed));
            const auto depth = value (3) * 0.01f;
            const auto shape = choice (4);
            const auto offset = Pan ? 0.0 : value (5) / 360.0;
            const auto smooth = coeffMs (2.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto before = phase;
                phase += rate / sampleRate;
                if (std::floor (phase) != std::floor (before))
                    step = noise.next();
                if constexpr (Pan)
                {
                    // Equal-power balance swung left and right
                    const auto target = depth * shapeAt (shape, phase, step);
                    smoothL = target + smooth * (smoothL - target);
                    const auto angle = (smoothL + 1.0f) * pi * 0.25f;
                    l[i] *= 1.41421356f * std::cos (angle);
                    r[i] *= 1.41421356f * std::sin (angle);
                }
                else
                {
                    const auto ml = 0.5f + 0.5f * shapeAt (shape, phase, step);
                    const auto mr = 0.5f + 0.5f * shapeAt (shape, phase + offset, step);
                    // (eased a little so a square shape doesn't click)
                    smoothL = (1.0f - depth * ml) + smooth * (smoothL - (1.0f - depth * ml));
                    smoothR = (1.0f - depth * mr) + smooth * (smoothR - (1.0f - depth * mr));
                    l[i] *= smoothL;
                    r[i] *= smoothR;
                }
            }
            phase -= std::floor (phase);
        }
    private:
        double phase = 0.0;
        float smoothL = 1.0f, smoothR = 1.0f, step = 0.0f;
        Noise noise;
    };

    //==============================================================================
    /** A rotating horn and drum: Doppler pitch, level and stereo movement, speeding up and slowing down. */
    class RotarySpeaker final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            split.reset();
            hornL.prepare ((int) (0.01 * rate));
            hornR.prepare ((int) (0.01 * rate));
            hornSpeed = 0.8;
            drumSpeed = 0.7;
            hornPhase = drumPhase = 0.0;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto mode = choice (0);   // Slow, Fast, Brake
            const auto targetHorn = mode == 1 ? value (1) : mode == 0 ? 0.83 : 0.0;
            const auto targetDrum = mode == 1 ? value (1) * 0.86 : mode == 0 ? 0.7 : 0.0;
            const auto accel = value (2);   // seconds to change speed
            const auto balance = value (3) * 0.01f;   // horn vs drum
            const auto doppler = value (4) * 0.01f;
            const auto drive = value (5) * 0.01f;
            const auto width = value (6) * 0.01f;
            split.set (800.0, sampleRate);
            const auto hornRate = 1.0 / std::max (0.05, accel * 0.6 * sampleRate);
            const auto drumRate = 1.0 / std::max (0.05, accel * 2.5 * sampleRate);
            const auto excursion = (float) (0.00045 * sampleRate) * doppler;

            for (int i = 0; i < n; ++i)
            {
                hornSpeed += (targetHorn - hornSpeed) * hornRate * 4.0;
                drumSpeed += (targetDrum - drumSpeed) * drumRate * 4.0;
                hornPhase += hornSpeed / sampleRate;
                drumPhase += drumSpeed / sampleRate;
                hornPhase -= std::floor (hornPhase);
                drumPhase -= std::floor (drumPhase);

                auto x = 0.5f * (l[i] + r[i]);
                if (drive > 0.0f)
                    x = saturate (x * (1.0f + drive * 4.0f)) / (1.0f + drive * 1.5f);
                float low, high;
                split.split (x, 0, low, high);
                const auto hs = (float) std::sin (juce::MathConstants<double>::twoPi * hornPhase);
                const auto hc = (float) std::cos (juce::MathConstants<double>::twoPi * hornPhase);
                const auto ds = (float) std::sin (juce::MathConstants<double>::twoPi * drumPhase);
                // Horn: two mics on either side hear it coming and going (pitch and level)
                hornL.push (high);
                hornR.push (high);
                const auto hl = hornL.read (2.0f + excursion * (1.0f + hs)) * (0.65f + 0.35f * hc);
                const auto hr = hornR.read (2.0f + excursion * (1.0f - hs)) * (0.65f - 0.35f * hc);
                const auto dlv = low * (0.75f + 0.25f * ds), drv = low * (0.75f - 0.25f * ds);
                const auto wl = hl * balance * 2.0f + dlv * (1.0f - balance) * 2.0f;
                const auto wr = hr * balance * 2.0f + drv * (1.0f - balance) * 2.0f;
                const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                l[i] = (mid + side) * 0.7f;
                r[i] = (mid - side) * 0.7f;
            }
        }
    private:
        Crossover split;
        DelayLine hornL, hornR;
        double hornSpeed = 0.8, drumSpeed = 0.7, hornPhase = 0.0, drumPhase = 0.0;
    };

    //==============================================================================
    class RingModulator final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; phase = 0.0; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto freq = value (0), lfoRate = value (2), lfoDepth = value (3) * 0.01f;
            const auto square = choice (1) == 1;
            const auto mix = value (4) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto f = freq * std::pow (2.0f, lfo.next (lfoRate, sampleRate) * lfoDepth * 2.0f);
                phase += f / sampleRate;
                phase -= std::floor (phase);
                auto c = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                if (square)
                    c = std::tanh (c * 8.0f);
                l[i] = l[i] * (1.0f - mix) + l[i] * c * mix;
                r[i] = r[i] * (1.0f - mix) + r[i] * c * mix;
            }
        }
    private:
        double phase = 0.0;
        Lfo lfo;
    };

    //==============================================================================
    /** Pitch wobble: a short delay swept, heard alone (no dry). */
    class Vibrato final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.03 * rate));
            dr.prepare ((int) (0.03 * rate));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed));
            // Depth in cents: the delay swing that bends the pitch so far at this rate
            const auto cents = value (3);
            const auto swing = std::min (0.013f * (float) sampleRate,
                                         (float) ((std::pow (2.0, cents / 1200.0) - 1.0) / (juce::MathConstants<double>::twoPi * std::max (0.05, rate)) * sampleRate));
            const auto phaseOffset = value (4) / 360.0;
            const auto mix = value (5) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                dl.push (l[i]);
                dr.push (r[i]);
                const auto a = dl.read (2.0f + swing * (1.0f + lfo.peek (0.0)));
                const auto b = dr.read (2.0f + swing * (1.0f + lfo.peek (phaseOffset)));
                lfo.next (rate, sampleRate);
                l[i] = l[i] * (1.0f - mix) + a * mix;
                r[i] = r[i] * (1.0f - mix) + b * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockModulationEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), deg (juce::CharPointer_UTF8 ("\xc2\xb0"));
    const std::vector<juce::String> sync { "Off", "On" };
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    const std::vector<juce::String> shapes { "Sine", "Triangle", "Square", "Saw", "Random" };
    static const std::vector<StockEffect> effects {
        { { "chorus", "Chorus", {
              { "rate", "Rate", hz, 0.02f, 8.0f, 0.6f, 1.0f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "delay", "Delay", ms, 3.0f, 40.0f, 12.0f, 12.0f },
              { "voices", "Voices", "", 0.0f, 3.0f, 1.0f, 1.5f, { "1", "2", "3", "4" } },
              { "spread", "Spread", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "feedback", "Feedback", pc, 0.0f, 80.0f, 0.0f, 30.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Modulation" },
          make<Chorus> },

        { { "ensemble", "Ensemble", {
              { "voices", "Voices", "", 0.0f, 6.0f, 3.0f, 3.0f, { "2", "3", "4", "5", "6", "7", "8" } },
              { "rate", "Rate", hz, 0.05f, 5.0f, 0.5f, 0.8f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 45.0f, 50.0f },
              { "spread", "Spread", pc, 0.0f, 100.0f, 90.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 60.0f, 50.0f } }, "Modulation" },
          make<Ensemble> },

        { { "flanger", "Flanger", {
              { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.02f, 10.0f, 0.3f, 1.0f },
              { "note", "Note", "", 0.0f, notesMax, 14.0f, 7.5f, notes },
              { "delay", "Delay", ms, 0.1f, 10.0f, 1.0f, 2.0f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "feedback", "Feedback", pc, -100.0f, 100.0f, 50.0f, 0.0f },
              { "phase", "Stereo Phase", deg, 0.0f, 180.0f, 90.0f, 90.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Modulation" },
          make<Flanger> },

        { { "phaser", "Phaser", {
              { "stages", "Stages", "", 0.0f, 3.0f, 1.0f, 1.5f, { "4", "6", "8", "12" } },
              { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.02f, 10.0f, 0.4f, 1.0f },
              { "note", "Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "centre", "Center", hz, 100.0f, 8000.0f, 800.0f, 800.0f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "feedback", "Feedback", pc, -100.0f, 100.0f, 30.0f, 0.0f },
              { "phase", "Stereo Phase", deg, 0.0f, 180.0f, 90.0f, 90.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Modulation" },
          make<Phaser> },

        { { "tremolo", "Tremolo", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.05f, 20.0f, 4.0f, 4.0f },
              { "note", "Note", "", 0.0f, notesMax, 6.0f, 7.5f, notes },
              { "depth", "Depth", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "shape", "Shape", "", 0.0f, 4.0f, 0.0f, 2.0f, shapes },
              { "phase", "Stereo Phase", deg, 0.0f, 180.0f, 0.0f, 90.0f } }, "Modulation" },
          make<Tremolo<false>> },

        { { "autoPan", "Auto Pan", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.05f, 20.0f, 1.0f, 2.0f },
              { "note", "Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "depth", "Depth", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "shape", "Shape", "", 0.0f, 4.0f, 0.0f, 2.0f, shapes } }, "Modulation" },
          make<Tremolo<true>> },

        { { "rotary", "Rotary Speaker", {
              { "speed", "Speed", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Slow", "Fast", "Brake" } },
              { "fast", "Fast Rate", hz, 4.0f, 9.0f, 6.7f, 6.5f },
              { "accel", "Acceleration", " s", 0.2f, 6.0f, 1.5f, 1.5f },
              { "balance", "Horn/Drum", pc, 0.0f, 100.0f, 55.0f, 50.0f },
              { "doppler", "Doppler", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "drive", "Drive", pc, 0.0f, 100.0f, 15.0f, 50.0f },
              { "width", "Mic Width", pc, 0.0f, 100.0f, 80.0f, 50.0f } }, "Modulation" },
          make<RotarySpeaker> },

        { { "ringMod", "Ring Modulator", {
              { "freq", "Frequency", hz, 1.0f, 5000.0f, 220.0f, 300.0f },
              { "shape", "Carrier", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Sine", "Square" } },
              { "lfoRate", "LFO Rate", hz, 0.02f, 10.0f, 0.5f, 1.0f },
              { "lfoDepth", "LFO Depth", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Modulation" },
          make<RingModulator> },

        { { "vibrato", "Vibrato", {
              { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.1f, 14.0f, 5.5f, 5.0f },
              { "note", "Note", "", 0.0f, notesMax, 3.0f, 7.5f, notes },
              { "depth", "Depth", " cents", 0.0f, 100.0f, 20.0f, 30.0f },
              { "phase", "Stereo Phase", deg, 0.0f, 180.0f, 0.0f, 90.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Modulation" },
          make<Vibrato> },
    };
    return effects;
}

} // namespace riff
