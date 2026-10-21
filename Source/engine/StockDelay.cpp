#include "StockDsp.h"

/*
    The stock delays (Logic's Delay folder): a stereo delay with separate sides and crossfeed, a tape
    delay with wow, flutter and saturation, a modulation delay (chorus to flanger), a simple echo and a
    sample delay for lining up or widening. Times follow the song's tempo when Sync is on.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    constexpr double maxDelaySeconds = 4.0;

    /** Common: the time of a side, in samples, from ms or a note value at the song's tempo. */
    inline float timeSamples (bool sync, float ms, int note, double bpm, double rate) noexcept
    {
        const auto seconds = sync ? noteSeconds (note, bpm) : ms * 0.001;
        return (float) juce::jlimit (1.0, maxDelaySeconds * rate - 8.0, seconds * rate);
    }

    //==============================================================================
    class StereoDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* d : { &dl, &dr })
                d->prepare ((int) (maxDelaySeconds * rate) + 8);
            timeL.reset (rate, 0.08);
            timeR.reset (rate, 0.08);
            const auto [a, b] = times();
            timeL.setCurrentAndTargetValue (a);
            timeR.setCurrentAndTargetValue (b);
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR })
                f->reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto [a, b] = times();
            timeL.setTargetValue (a);
            timeR.setTargetValue (b);
            const auto fbL = value (5) * 0.01f, fbR = value (6) * 0.01f, cross = value (7) * 0.01f;
            for (auto* f : { &lowCutL, &lowCutR })
                f->setLowPass (value (8), sampleRate);
            for (auto* f : { &highCutL, &highCutR })
                f->setLowPass (value (9), sampleRate);
            const auto mix = value (10) * 0.01f;
            const auto width = value (11) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                auto el = dl.read (timeL.getNextValue()), er = dr.read (timeR.getNextValue());
                // Tone in the feedback: low cut (high pass) and high cut
                el = highCutL.lowPass (lowCutL.highPass (el));
                er = highCutR.lowPass (lowCutR.highPass (er));
                dl.push (l[i] + el * fbL + er * cross);
                dr.push (r[i] + er * fbR + el * cross);
                // Width of the echoes: 0 mono, 100 as set
                const auto mid = 0.5f * (el + er), side = 0.5f * (el - er) * width;
                l[i] = l[i] * (1.0f - mix) + (mid + side) * mix;
                r[i] = r[i] * (1.0f - mix) + (mid - side) * mix;
            }
        }

    private:
        std::pair<float, float> times() const noexcept
        {
            const bool sync = choice (0) == 1;
            const auto tempo = bpm.load (std::memory_order_relaxed);
            return { timeSamples (sync, value (1), choice (3), tempo, sampleRate), timeSamples (sync, value (2), choice (4), tempo, sampleRate) };
        }
        DelayLine dl, dr;
        juce::SmoothedValue<float> timeL, timeR;
        OnePole lowCutL, lowCutR, highCutL, highCutR;
    };

    //==============================================================================
    class TapeDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* d : { &dl, &dr })
                d->prepare ((int) (maxDelaySeconds * rate) + 8);
            time.reset (rate, 0.25);   // (a tape machine's speed glides: the famous pitch swoop)
            time.setCurrentAndTargetValue (target());
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR, &flutterSmooth })
                f->reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            time.setTargetValue (target());
            const auto feedback = value (3) * 0.01f;
            const auto wow = value (4) * 0.01f, flutter = value (5) * 0.01f;
            for (auto* f : { &lowCutL, &lowCutR })
                f->setLowPass (value (6), sampleRate);
            for (auto* f : { &highCutL, &highCutR })
                f->setLowPass (value (7), sampleRate);
            flutterSmooth.setLowPass (30.0, sampleRate);
            const auto drive = 1.0f + value (8) * 0.04f;
            const auto mix = value (9) * 0.01f;
            const auto spread = value (10) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                // Wow (slow) and flutter (fast, a little random): the tape's speed wandering
                const auto w = wowLfo.next (0.55, sampleRate) * wow * 0.003f;
                const auto f = (flutterLfo.next (6.3, sampleRate) * 0.6f + flutterSmooth.lowPass (noise.next()) * 4.0f) * flutter * 0.0006f;
                const auto t = time.getNextValue() * (1.0f + w + f);
                auto el = dl.read (t), er = dr.read (t * (1.0f + spread * 0.02f));
                el = highCutL.lowPass (lowCutL.highPass (el));
                er = highCutR.lowPass (lowCutR.highPass (er));
                // The tape saturates as the repeats build up
                dl.push (saturate ((l[i] + el * feedback) * drive) / drive);
                dr.push (saturate ((r[i] + er * feedback) * drive) / drive);
                l[i] = l[i] * (1.0f - mix) + el * mix;
                r[i] = r[i] * (1.0f - mix) + er * mix;
            }
        }

    private:
        float target() const noexcept
        {
            return timeSamples (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed), sampleRate);
        }
        DelayLine dl, dr;
        juce::SmoothedValue<float> time;
        OnePole lowCutL, lowCutR, highCutL, highCutR, flutterSmooth;
        Lfo wowLfo, flutterLfo;
        Noise noise;
    };

    //==============================================================================
    /** A short modulated delay: chorus at 10-30 ms, flanger at 1-5 ms with feedback. */
    class ModulationDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.12 * rate));
            dr.prepare ((int) (0.12 * rate));
            lastL = lastR = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto base = value (0) * 0.001f * (float) sampleRate;
            const auto feedback = value (1) * 0.01f;
            const auto rate = value (2), depth = value (3) * 0.01f * std::min (base, 0.02f * (float) sampleRate);
            const auto phase = value (4) / 360.0f;
            const auto mix = value (5) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                const auto ml = lfo.peek (0.0), mr = lfo.peek (phase);
                lfo.next (rate, sampleRate);
                const auto el = dl.read (std::max (1.0f, base + ml * depth * 0.5f + depth * 0.5f));
                const auto er = dr.read (std::max (1.0f, base + mr * depth * 0.5f + depth * 0.5f));
                dl.push (l[i] + el * feedback);
                dr.push (r[i] + er * feedback);
                l[i] = l[i] * (1.0f - mix * 0.5f) + el * mix;
                r[i] = r[i] * (1.0f - mix * 0.5f) + er * mix;
            }
        }

    private:
        DelayLine dl, dr;
        Lfo lfo;
        float lastL = 0.0f, lastR = 0.0f;
    };

    //==============================================================================
    /** A simple echo: time (or a note), repeats and a colour knob from dark to bright. */
    class Echo final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (maxDelaySeconds * rate) + 8);
            dr.prepare ((int) (maxDelaySeconds * rate) + 8);
            time.reset (rate, 0.08);
            time.setCurrentAndTargetValue (target());
            toneL.reset();
            toneR.reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            time.setTargetValue (target());
            const auto repeats = value (3) * 0.01f * 0.95f;
            const auto colour = value (4) * 0.01f;   // -1 dark .. +1 bright
            toneL.setLowPass (colour < 0.0f ? 8000.0 * std::pow (0.06, -colour) : 8000.0, sampleRate);
            toneR.setLowPass (colour < 0.0f ? 8000.0 * std::pow (0.06, -colour) : 8000.0, sampleRate);
            const auto bright = std::max (0.0f, colour);
            const auto mix = value (5) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                const auto t = time.getNextValue();
                auto el = dl.read (t), er = dr.read (t);
                const auto ll = toneL.lowPass (el), lr = toneR.lowPass (er);
                el = ll + bright * (el - ll) * 1.5f;
                er = lr + bright * (er - lr) * 1.5f;
                dl.push (l[i] + el * repeats);
                dr.push (r[i] + er * repeats);
                l[i] = l[i] * (1.0f - mix) + el * mix;
                r[i] = r[i] * (1.0f - mix) + er * mix;
            }
        }

    private:
        float target() const noexcept
        {
            return timeSamples (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed), sampleRate);
        }
        DelayLine dl, dr;
        juce::SmoothedValue<float> time;
        OnePole toneL, toneR;
    };

    //==============================================================================
    /** Delays each side by a number of samples (to line up mics, or for the Haas widening trick). */
    class SampleDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare (10010);
            dr.prepare (10010);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto a = (int) value (0), b = (int) value (1);
            for (int i = 0; i < n; ++i)
            {
                dl.push (l[i]);
                dr.push (r[i]);
                // (pushed first: a delay of 0 is the sample itself)
                l[i] = a <= 0 ? l[i] : dl.readInt (a + 1);
                r[i] = b <= 0 ? r[i] : dr.readInt (b + 1);
            }
        }

    private:
        DelayLine dl, dr;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockDelayEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %");
    const std::vector<juce::String> sync { "Off", "On" };
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    static const std::vector<StockEffect> effects {
        { { "stereoDelay", "Stereo Delay", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "timeL", "Left Time", ms, 1.0f, 4000.0f, 375.0f, 400.0f },
              { "timeR", "Right Time", ms, 1.0f, 4000.0f, 500.0f, 400.0f },
              { "noteL", "Left Note", "", 0.0f, notesMax, 7.0f, 7.5f, notes },
              { "noteR", "Right Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "fbL", "Left Feedback", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "fbR", "Right Feedback", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "cross", "Crossfeed", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 2000.0f, 80.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 12000.0f, 6000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "width", "Width", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Delay" },
          make<StereoDelay> },

        { { "tapeDelay", "Tape Delay", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "time", "Time", ms, 1.0f, 4000.0f, 375.0f, 400.0f },
              { "note", "Note", "", 0.0f, notesMax, 7.0f, 7.5f, notes },
              { "feedback", "Feedback", pc, 0.0f, 110.0f, 35.0f, 50.0f },
              { "wow", "Wow", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "flutter", "Flutter", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 2000.0f, 120.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 5000.0f, 6000.0f },
              { "drive", "Saturation", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "spread", "Spread", pc, 0.0f, 100.0f, 20.0f, 50.0f } }, "Delay" },
          make<TapeDelay> },

        { { "modDelay", "Modulation Delay", {
              { "time", "Delay", ms, 0.5f, 50.0f, 12.0f, 10.0f },
              { "feedback", "Feedback", pc, -95.0f, 95.0f, 0.0f, 0.0f },
              { "rate", "Rate", hz, 0.02f, 10.0f, 0.5f, 1.0f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "phase", "Stereo Phase", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0.0f, 180.0f, 90.0f, 90.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Delay" },
          make<ModulationDelay> },

        { { "echo", "Echo", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "time", "Time", ms, 1.0f, 4000.0f, 375.0f, 400.0f },
              { "note", "Note", "", 0.0f, notesMax, 7.0f, 7.5f, notes },
              { "repeats", "Repeats", pc, 0.0f, 100.0f, 35.0f, 50.0f },
              { "colour", "Color", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Delay" },
          make<Echo> },

        { { "sampleDelay", "Sample Delay", {
              { "left", "Left", " smp", 0.0f, 10000.0f, 0.0f, 500.0f },
              { "right", "Right", " smp", 0.0f, 10000.0f, 0.0f, 500.0f } }, "Delay" },
          make<SampleDelay> },
    };
    return effects;
}

} // namespace riff
