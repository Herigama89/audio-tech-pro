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
    /**
        Stereo Delay (as Logic's): each side its own input (Off, Left, Right, L+R, L-R), time or note with
        deviation, feedback with phase, crossfeed to the other side with phase; the routing presets
        (Straight, Crossfeed, 90/10, 10/90, Ping Pong, Pan, Rotate) or Customized; low and high cut on the
        echoes; output level per side, width, dry/wet mix.
    */
    class StereoDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iSync, iTimeL, iTimeR, iNoteL, iNoteR, iFbL, iFbR, iCrossLR, iLowCut, iHighCut, iMix, iWidth,
                     iCrossRL, iFbPhaseL, iFbPhaseR, iCrossPhase, iInputL, iInputR, iDevL, iDevR, iOutL, iOutR, iRouting };

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

        static float inputOf (int mode, float l, float r) noexcept
        {
            switch (mode)
            {
                case 0:  return 0.0f;
                case 1:  return l;
                case 2:  return r;
                case 3:  return 0.5f * (l + r);
                default: return 0.5f * (l - r);
            }
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto [a, b] = times();
            timeL.setTargetValue (a);
            timeR.setTargetValue (b);
            auto fbL = value (iFbL) * 0.01f * (choice (iFbPhaseL) == 1 ? -1.0f : 1.0f);
            auto fbR = value (iFbR) * 0.01f * (choice (iFbPhaseR) == 1 ? -1.0f : 1.0f);
            const auto crossSign = choice (iCrossPhase) == 1 ? -1.0f : 1.0f;
            auto crossLR = value (iCrossLR) * 0.01f * crossSign, crossRL = value (iCrossRL) * 0.01f * crossSign;
            auto inL = choice (iInputL), inR = choice (iInputR);
            float mixInLL = 1.0f, mixInLR = 0.0f;   // (90/10 and 10/90 blend the inputs)
            const auto routing = choice (iRouting);
            switch (routing)
            {
                case 1: inL = 1; inR = 2; crossLR = crossRL = 0.0f; break;                                  // Straight
                case 2: inL = 1; inR = 2; crossLR = std::abs (fbL); crossRL = std::abs (fbR); break;       // Crossfeed
                case 3: inL = 1; inR = 2; mixInLL = 0.9f; mixInLR = 0.1f; break;                           // 90/10
                case 4: inL = 1; inR = 2; mixInLL = 0.1f; mixInLR = 0.9f; break;                           // 10/90
                case 5: inL = 3; inR = 0; crossLR = std::abs (fbL); crossRL = std::abs (fbR); fbL = fbR = 0.0f; break;   // Ping Pong
                case 6: inL = 3; inR = 0; crossLR = 1.0f; crossRL = 0.0f; fbR = 0.0f; break;               // Pan: left, then right
                case 7: inL = 3; inR = 0; crossLR = std::abs (fbL); crossRL = -std::abs (fbR); fbL = fbR = 0.0f; break;  // Rotate
                default: break;                                                                              // Customized
            }
            for (auto* f : { &lowCutL, &lowCutR })
                f->setLowPass (value (iLowCut), sampleRate);
            for (auto* f : { &highCutL, &highCutR })
                f->setLowPass (value (iHighCut), sampleRate);
            const auto mix = value (iMix) * 0.01f;
            const auto width = value (iWidth) * 0.01f;
            const auto outL = value (iOutL) * 0.01f, outR = value (iOutR) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                auto el = dl.read (timeL.getNextValue()), er = dr.read (timeR.getNextValue());
                // Tone of the echoes: low cut (high pass) and high cut
                el = highCutL.lowPass (lowCutL.highPass (el));
                er = highCutR.lowPass (lowCutR.highPass (er));
                const auto srcL = inputOf (inL, l[i], r[i]), srcR = inputOf (inR, l[i], r[i]);
                const auto feedL = mixInLL * srcL + mixInLR * srcR, feedR = mixInLL * srcR + mixInLR * srcL;
                dl.push (feedL + el * fbL + er * crossRL);
                dr.push (feedR + er * fbR + el * crossLR);
                el *= outL;
                er *= outR;
                // Width of the echoes: 0 mono, 100 as set
                const auto mid = 0.5f * (el + er), side = 0.5f * (el - er) * width;
                l[i] = l[i] * (1.0f - mix) + (mid + side) * mix;
                r[i] = r[i] * (1.0f - mix) + (mid - side) * mix;
            }
        }

    private:
        std::pair<float, float> times() const noexcept
        {
            const bool sync = choice (iSync) == 1;
            const auto tempo = bpm.load (std::memory_order_relaxed);
            // (deviation: off the grid by a percentage, for a looser feel)
            const auto devL = sync ? 1.0f + value (iDevL) * 0.01f : 1.0f, devR = sync ? 1.0f + value (iDevR) * 0.01f : 1.0f;
            return { timeSamples (sync, value (iTimeL), choice (iNoteL), tempo, sampleRate) * devL,
                     timeSamples (sync, value (iTimeR), choice (iNoteR), tempo, sampleRate) * devR };
        }
        DelayLine dl, dr;
        juce::SmoothedValue<float> timeL, timeR;
        OnePole lowCutL, lowCutR, highCutL, highCutR;
    };

    //==============================================================================
    /**
        Tape Delay (as Logic's): time or note with deviation, feedback, Freeze, low and high cut in the loop,
        the tape's LFO and flutter (each a rate and an intensity) evened out by Smoothing, a clip
        threshold for the tape's saturation, Clean or Diffuse head, spread, dry and wet.
    */
    class TapeDelay final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iSync, iTime, iNote, iDeviation, iFeedback, iFreeze, iLowCut, iHighCut, iSmoothing, iClip, iSpread, iHead,
                     iLfoRate, iLfoDepth, iFlutterRate, iFlutter, iDry, iWet };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* d : { &dl, &dr })
                d->prepare ((int) (maxDelaySeconds * rate) + 8);
            for (auto* a : { &diffL1, &diffL2, &diffR1, &diffR2 })
                a->line.prepare ((int) (0.02 * rate));
            time.reset (rate, 0.25);   // (a tape machine's speed glides: the famous pitch swoop)
            time.setCurrentAndTargetValue (target());
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR, &flutterSmooth, &modSmooth })
                f->reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            time.setTargetValue (target());
            const auto frozen = choice (iFreeze) == 1;
            const auto diffuse = choice (iHead) == 1;
            // (Diffuse: the head further along - a softer, smeared repeat that holds up a little longer)
            const auto feedback = frozen ? 1.0f : value (iFeedback) * 0.01f * (diffuse ? 1.04f : 1.0f);
            const auto lfoDepth = value (iLfoDepth) * 0.01f, flutter = value (iFlutter) * 0.01f * (diffuse ? 1.3f : 1.0f);
            const auto lfoRate = value (iLfoRate), flutterRate = value (iFlutterRate);
            for (auto* f : { &lowCutL, &lowCutR })
                f->setLowPass (value (iLowCut), sampleRate);
            for (auto* f : { &highCutL, &highCutR })
                f->setLowPass (value (iHighCut), sampleRate);
            flutterSmooth.setLowPass (30.0, sampleRate);
            // Smoothing: the speed changes rounded off (0: as they come, 100: very gentle)
            modSmooth.setLowPass (juce::jmap (value (iSmoothing) * 0.01f, 200.0f, 2.0f), sampleRate);
            // Clip threshold: where the tape starts to break up (100 % clean, low values aggressive)
            const auto ceiling = juce::jmap (value (iClip) * 0.01f, 0.08f, 1.6f);
            const auto spread = value (iSpread) * 0.01f;
            const auto dry = value (iDry) * 0.01f, wet = value (iWet) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                // The LFO (slow wow) and flutter (fast, a little random): the tape's speed wandering
                const auto w = wowLfo.next (lfoRate, sampleRate) * lfoDepth * 0.003f;
                const auto f = (flutterLfo.next (flutterRate, sampleRate) * 0.6f + flutterSmooth.lowPass (noise.next()) * 4.0f) * flutter * 0.0006f;
                const auto t = time.getNextValue() * (1.0f + modSmooth.lowPass (w + f));
                auto el = dl.read (t), er = dr.read (t * (1.0f + spread * 0.02f));
                if (! frozen)
                {
                    el = highCutL.lowPass (lowCutL.highPass (el));
                    er = highCutR.lowPass (lowCutR.highPass (er));
                }
                if (diffuse)
                {
                    el = diffL2.process (diffL1.process (el, (float) (0.0041 * sampleRate), 0.5f), (float) (0.0067 * sampleRate), 0.4f);
                    er = diffR2.process (diffR1.process (er, (float) (0.0047 * sampleRate), 0.5f), (float) (0.0071 * sampleRate), 0.4f);
                }
                // The tape saturates at its clip threshold; the repeats push into it as they build up
                const auto inL = frozen ? 0.0f : l[i], inR = frozen ? 0.0f : r[i];
                dl.push (frozen ? el : ceiling * saturate ((inL + el * feedback) / ceiling));
                dr.push (frozen ? er : ceiling * saturate ((inR + er * feedback) / ceiling));
                l[i] = l[i] * dry + el * wet;
                r[i] = r[i] * dry + er * wet;
            }
        }

    private:
        float target() const noexcept
        {
            const auto sync = choice (iSync) == 1;
            const auto t = timeSamples (sync, value (iTime), choice (iNote), bpm.load (std::memory_order_relaxed), sampleRate);
            return sync ? juce::jlimit (1.0f, (float) (maxDelaySeconds * sampleRate - 8.0), t * (1.0f + value (iDeviation) * 0.01f)) : t;
        }
        DelayLine dl, dr;
        DiffusionAllPass diffL1, diffL2, diffR1, diffR2;
        juce::SmoothedValue<float> time;
        OnePole lowCutL, lowCutR, highCutL, highCutR, flutterSmooth, modSmooth;
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
            const auto wet = value (5) * 0.01f, dry = value (6) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                const auto t = time.getNextValue();
                auto el = dl.read (t), er = dr.read (t);
                const auto ll = toneL.lowPass (el), lr = toneR.lowPass (er);
                el = ll + bright * (el - ll) * 1.5f;
                er = lr + bright * (er - lr) * 1.5f;
                dl.push (l[i] + el * repeats);
                dr.push (r[i] + er * repeats);
                l[i] = l[i] * dry + el * wet;
                r[i] = r[i] * dry + er * wet;
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
            const auto a = (int) value (0), b = choice (2) == 1 ? a : (int) value (1);   // (Link L & R: the right follows the left)
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
              { "cross", "Crossfeed L to R", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 2000.0f, 80.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 12000.0f, 6000.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "width", "Width", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "crossRL", "Crossfeed R to L", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "fbPhaseL", "Left Feedback Phase", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Inverted" } },
              { "fbPhaseR", "Right Feedback Phase", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Inverted" } },
              { "crossPhase", "Crossfeed Phase", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Inverted" } },
              { "inputL", "Left Input", "", 0.0f, 4.0f, 1.0f, 2.0f, { "Off", "Left", "Right", "L + R", "L - R" } },
              { "inputR", "Right Input", "", 0.0f, 4.0f, 2.0f, 2.0f, { "Off", "Left", "Right", "L + R", "L - R" } },
              { "devL", "Left Deviation", pc, -50.0f, 50.0f, 0.0f, 0.0f },
              { "devR", "Right Deviation", pc, -50.0f, 50.0f, 0.0f, 0.0f },
              { "outL", "Left Output", pc, 0.0f, 150.0f, 100.0f, 75.0f },
              { "outR", "Right Output", pc, 0.0f, 150.0f, 100.0f, 75.0f },
              { "routing", "Routing", "", 0.0f, 7.0f, 0.0f, 3.5f,
                { "Customized", "Straight", "Crossfeed", "90/10", "10/90", "Ping Pong L/R", "Pan L/R", "Rotate L/R" } } }, "Delay" },
          make<StereoDelay> },

        { { "tapeDelay", "Tape Delay", {
              { "sync", "Tempo Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "time", "Delay Time", ms, 1.0f, 4000.0f, 375.0f, 400.0f },
              { "note", "Note", "", 0.0f, notesMax, 7.0f, 7.5f, notes },
              { "deviation", "Deviation", pc, -50.0f, 50.0f, 0.0f, 0.0f },
              { "feedback", "Feedback", pc, 0.0f, 110.0f, 35.0f, 50.0f },
              { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "lowCut", "Low Cut", hz, 10.0f, 2000.0f, 120.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 5000.0f, 6000.0f },
              { "smoothing", "Smoothing", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "clip", "Clip Threshold", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "spread", "Spread", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "head", "Tape Head Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Clean", "Diffuse" } },
              { "lfoRate", "LFO Rate", hz, 0.05f, 10.0f, 0.55f, 1.0f },
              { "wow", "LFO Intensity", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "flutterRate", "Flutter Rate", hz, 0.5f, 20.0f, 6.3f, 6.0f },
              { "flutter", "Flutter Intensity", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "mix", "Wet", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Delay" },
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
              { "mix", "Wet", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Delay" },
          make<Echo> },

        { { "sampleDelay", "Sample Delay", {
              { "left", "Left", " smp", 0.0f, 10000.0f, 0.0f, 500.0f },
              { "right", "Right", " smp", 0.0f, 10000.0f, 0.0f, 500.0f },
              { "link", "Link L & R", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Delay" },
          make<SampleDelay> },
    };
    return effects;
}

} // namespace riff
