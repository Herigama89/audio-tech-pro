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

    /** Logic's tremolo-style wave: a pulse with its symmetry (duty) moved, smoothed from square (0) to round (100). */
    inline float pulseAt (double phase, float symmetry, float smoothing) noexcept
    {
        phase -= std::floor (phase);
        const auto duty = juce::jlimit (0.02, 0.98, 0.5 + symmetry * 0.0048);
        // A sine bent so its half above zero takes the duty's share of the cycle
        const auto bent = phase < duty ? 0.5 * phase / duty : 0.5 + 0.5 * (phase - duty) / (1.0 - duty);
        const auto sine = (float) std::sin (juce::MathConstants<double>::twoPi * bent);
        const auto hardness = 1.0f + (1.0f - smoothing * 0.01f) * 30.0f;
        return std::tanh (sine * hardness) / std::tanh (hardness);
    }

    /** A Hilbert pair: two all-pass chains whose outputs stay 90 degrees apart (for frequency shifting). */
    struct Hilbert
    {
        struct Section
        {
            float a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
            float process (float x) noexcept
            {
                const auto y = a2 * (x + y2) - x2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
        };
        std::array<Section, 4> a, b;
        float delayed = 0.0f;
        Hilbert()
        {
            const float ca[] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            const float cb[] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            for (size_t k = 0; k < 4; ++k) { a[k].a2 = ca[k] * ca[k]; b[k].a2 = cb[k] * cb[k]; }
        }
        void process (float x, float& i, float& q) noexcept
        {
            auto ya = x, yb = x;
            for (auto& s : a) ya = s.process (ya);
            for (auto& s : b) yb = s.process (yb);
            i = ya;
            q = delayed;
            delayed = yb;
        }
    };

    //==============================================================================
    /** Chorus (as Logic's): rate, intensity, delay, voices, spread, feedback, D-Mode, mix. */
    class Chorus final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.06 * rate));
            dr.prepare ((int) (0.06 * rate));
            dHpL.reset(); dHpR.reset();
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = value (0), depth = value (1) * 0.01f * 0.006f * (float) sampleRate;
            const auto base = value (2) * 0.001f * (float) sampleRate;
            const auto voices = juce::jlimit (1, 4, choice (3) + 1);
            const auto spread = value (4) * 0.01f;
            const auto feedback = value (5) * 0.01f;
            const auto mix = value (6) * 0.01f;
            const auto dMode = choice (7) == 1;
            dHpL.setLowPass (400.0, sampleRate);
            dHpR.setLowPass (400.0, sampleRate);
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
                if (dMode)
                {
                    // D-Mode: each side's chorus, its highs inverted, leaks into the other (the classic dimension box)
                    const auto hl = dHpL.highPass (wl), hr = dHpR.highPass (wr);
                    wl -= 0.35f * hr;
                    wr -= 0.35f * hl;
                }
                l[i] = l[i] * (1.0f - mix * 0.5f) + wl * mix;
                r[i] = r[i] * (1.0f - mix * 0.5f) + wr * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
        OnePole dHpL, dHpR;
    };

    //==============================================================================
    /**
        Ensemble (as Logic's): up to eight voices modulated by LFO 1, LFO 2 and a random source (each on/off,
        rate, intensity), spread across the stereo field, their phases offset, volume compensation, mix.
    */
    class Ensemble final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iVoices, iSpread, iMix, iLfo1On, iLfo1Rate, iLfo1Int, iLfo2On, iLfo2Rate, iLfo2Int, iRndOn, iRndRate, iRndInt, iPhase, iVolComp };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            line.prepare ((int) (0.06 * rate));
            for (auto& r : randomState) r = 0.0f;
            for (auto& r : randomTarget) r = 0.0f;
            rndSmooth.reset();
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto voices = juce::jlimit (2, 8, choice (iVoices) + 2);
            const auto spread = value (iSpread) * 0.01f;
            const auto mix = value (iMix) * 0.01f;
            const auto ms = 0.001f * (float) sampleRate;
            const auto d1 = choice (iLfo1On) == 1 ? value (iLfo1Int) * 0.01f * 4.0f * ms : 0.0f;
            const auto d2 = choice (iLfo2On) == 1 ? value (iLfo2Int) * 0.01f * 1.0f * ms : 0.0f;
            const auto dr = choice (iRndOn) == 1 ? value (iRndInt) * 0.01f * 2.0f * ms : 0.0f;
            const auto phase = value (iPhase) / 360.0;
            const auto comp = value (iVolComp) * 0.01f;
            const auto base = 14.0f * ms;
            const auto rndCoeff = coeffMs (1000.0 / std::max (0.05f, value (iRndRate)), sampleRate);
            for (int i = 0; i < n; ++i)
            {
                line.push (0.5f * (l[i] + r[i]));
                // The random source: a new target per voice each cycle, glided to
                rndPhase += value (iRndRate) / sampleRate;
                if (rndPhase >= 1.0)
                {
                    rndPhase -= 1.0;
                    for (auto& t : randomTarget) t = noise.next();
                }
                float wl = 0.0f, wr = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    const auto off = phase * (double) v / voices;
                    auto& rs = randomState[(size_t) v];
                    rs = randomTarget[(size_t) v] + rndCoeff * (rs - randomTarget[(size_t) v]);
                    const auto m = d1 * lfo1.peek (off) + d2 * lfo2.peek (off * 1.3) + dr * rs;
                    const auto s = line.read (base + m + (float) v * 0.6f * ms);
                    const auto pan = ((float) v / (float) (voices - 1) * 2.0f - 1.0f) * std::min (1.0f, spread);
                    wl += s * (1.0f - pan) * 0.5f;
                    wr += s * (1.0f + pan) * 0.5f;
                }
                lfo1.next (value (iLfo1Rate), sampleRate);
                lfo2.next (value (iLfo2Rate), sampleRate);
                // (volume compensation: more voices, less level each)
                const auto norm = (2.0f / (float) voices) * (1.0f - comp) + comp * (2.0f / std::sqrt ((float) voices)) * 0.6f;
                auto el = wl * norm, er = wr * norm;
                if (spread > 1.0f)
                {
                    const auto mid = 0.5f * (el + er), side = 0.5f * (el - er) * spread;
                    el = mid + side; er = mid - side;
                }
                l[i] = l[i] * (1.0f - mix) + el * mix;
                r[i] = r[i] * (1.0f - mix) + er * mix;
            }
        }
    private:
        DelayLine line;
        Lfo lfo1, lfo2;
        Noise noise;
        OnePole rndSmooth;
        std::array<float, 8> randomState {}, randomTarget {};
        double rndPhase = 0.0;
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
    /** Microphaser (as Logic's): a small, simple phaser - rate (or synced), intensity, feedback. */
    class Microphaser final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; fbL = fbR = 0.0f; for (auto& a : apL) a.z = 0.0f; for (auto& a : apR) a.z = 0.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed));
            const auto intensity = value (3) * 0.01f;
            const auto feedback = value (4) * 0.01f * 0.85f;
            for (int i = 0; i < n; ++i)
            {
                const auto coeffFor = [&] (float m)
                {
                    const auto f = 900.0f * std::pow (2.0f, m * intensity * 1.6f);
                    const auto t = std::tan (pi * juce::jlimit (40.0f, (float) sampleRate * 0.45f, f) / (float) sampleRate);
                    return (t - 1.0f) / (t + 1.0f);
                };
                const auto al = coeffFor (lfo.peek (0.0)), ar = coeffFor (lfo.peek (0.25));
                lfo.next (rate, sampleRate);
                auto a = l[i] + fbL * feedback, b = r[i] + fbR * feedback;
                for (size_t s = 0; s < 4; ++s) { a = apL[s].process (a, al); b = apR[s].process (b, ar); }
                fbL = a; fbR = b;
                l[i] = 0.5f * (l[i] + a);
                r[i] = 0.5f * (r[i] + b);
            }
        }
    private:
        std::array<AllPass1, 4> apL, apR;
        float fbL = 0.0f, fbR = 0.0f;
        Lfo lfo;
    };

    //==============================================================================
    /**
        Phaser (as Logic's): 4 to 12 stages (odd numbers: comb filtering), the sweep between Floor and
        Ceiling by two LFOs (each its rate or a note, mixed), or by the input's envelope; the stereo
        phase and its distribution; feedback level, Warmth, a low/high cut filter, output mix.
    */
    class Phaser final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iStages, iSync, iRate, iNote, iCeiling, iFloor, iLevel, iPhase, iMix, iSweep, iRate2, iSync2, iNote2, iLfoMix,
                     iDistribution, iWarmth, iFilter, iLowCut, iHighCut };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& a : stagesL) a.z = 0.0f;
            for (auto& a : stagesR) a.z = 0.0f;
            fbL = fbR = envelope = 0.0f;
            for (auto* f : { &lowL, &lowR, &highL, &highR }) f->reset();
            randomOffset = 0.37;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto stages = juce::jlimit (4, 12, choice (iStages) + 4);
            const auto tempo = bpm.load (std::memory_order_relaxed);
            const auto rate1 = rateHz (choice (iSync) == 1, value (iRate), choice (iNote), tempo);
            const auto rate2 = rateHz (choice (iSync2) == 1, value (iRate2), choice (iNote2), tempo);
            const auto lfoMix = value (iLfoMix) * 0.01f;   // (0: LFO 1 only, 100: LFO 2 only)
            const auto ceiling = std::max (value (iCeiling), value (iFloor) + 10.0f), floor = value (iFloor);
            const auto feedback = value (iLevel) * 0.01f * 0.9f;
            const auto distribution = choice (iDistribution);
            const auto phase = distribution == 2 ? randomOffset : value (iPhase) / 360.0 * (distribution == 1 ? 0.5 : 1.0);
            const auto mix = value (iMix) * 0.01f;
            const auto envelopeSweep = choice (iSweep) == 1;
            const auto warmth = choice (iWarmth) == 1;
            const auto filter = choice (iFilter) == 1;
            for (auto* f : { &lowL, &lowR }) f->setLowPass (value (iLowCut), sampleRate);
            for (auto* f : { &highL, &highR }) f->setLowPass (value (iHighCut), sampleRate);
            const auto follow = coeffMs (20.0, sampleRate);
            const auto octaves = std::log2 (ceiling / std::max (10.0f, floor));
            for (int i = 0; i < n; ++i)
            {
                const auto in = std::abs (0.5f * (l[i] + r[i]));
                envelope = in + follow * (envelope - in);
                const auto sweepAt = [&] (double offset)
                {
                    if (envelopeSweep)
                        return juce::jlimit (0.0f, 1.0f, envelope * 4.0f);
                    const auto m = (1.0f - lfoMix) * lfo1.peek (offset) + lfoMix * lfo2.peek (offset);
                    return 0.5f + 0.5f * m;
                };
                const auto coeffFor = [&] (float position)
                {
                    const auto f = floor * std::pow (2.0f, position * octaves);
                    const auto t = std::tan (pi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, f) / (float) sampleRate);
                    return (t - 1.0f) / (t + 1.0f);
                };
                const auto al = coeffFor (sweepAt (0.0)), ar = coeffFor (sweepAt (phase));
                lfo1.next (rate1, sampleRate);
                lfo2.next (rate2, sampleRate);
                auto a = l[i] + fbL * feedback, b = r[i] + fbR * feedback;
                if (warmth)
                {
                    a = saturate (a * 1.6f) / 1.3f;
                    b = saturate (b * 1.6f) / 1.3f;
                }
                for (int st = 0; st < stages; ++st)
                {
                    a = stagesL[(size_t) st].process (a, al);
                    b = stagesR[(size_t) st].process (b, ar);
                }
                fbL = a;
                fbR = b;
                if (filter)
                {
                    a = highL.lowPass (lowL.highPass (a));
                    b = highR.lowPass (lowR.highPass (b));
                }
                // (odd stage counts: the sweep is blended in a little less - gentler comb filtering)
                const auto wet = mix * (stages % 2 == 1 ? 0.6f : 1.0f);
                l[i] = (l[i] + a * wet) / (1.0f + wet * 0.5f);
                r[i] = (r[i] + b * wet) / (1.0f + wet * 0.5f);
            }
        }
    private:
        std::array<AllPass1, 12> stagesL, stagesR;
        float fbL = 0.0f, fbR = 0.0f, envelope = 0.0f;
        double randomOffset = 0.37;
        Lfo lfo1, lfo2;
        OnePole lowL, lowR, highL, highR;
    };

    //==============================================================================
    /**
        Tremolo (as Logic's): rate (or a note), depth, the wave's smoothing and symmetry, its offset, and the
        left-right phase with its distribution.
    */
    class Tremolo final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iSync, iRate, iNote, iDepth, iSmoothing, iPhase, iSymmetry, iOffset, iDistribution };
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; phase = 0.0; smoothL = smoothR = 1.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (iSync) == 1, value (iRate), choice (iNote), bpm.load (std::memory_order_relaxed));
            const auto depth = value (iDepth) * 0.01f;
            const auto smoothing = value (iSmoothing), symmetry = value (iSymmetry);
            const auto offset = value (iOffset) / 360.0;
            const auto distribution = choice (iDistribution);
            const auto stereo = (value (iPhase) / 360.0) * (distribution == 1 ? 0.5 : 1.0) + (distribution == 2 ? 0.31 : 0.0);
            const auto ease = coeffMs (1.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                phase += rate / sampleRate;
                const auto ml = 0.5f + 0.5f * pulseAt (phase + offset, symmetry, smoothing);
                const auto mr = 0.5f + 0.5f * pulseAt (phase + offset + stereo, symmetry, smoothing);
                const auto tl = 1.0f - depth * (1.0f - ml), tr = 1.0f - depth * (1.0f - mr);
                smoothL = tl + ease * (smoothL - tl);
                smoothR = tr + ease * (smoothR - tr);
                l[i] *= smoothL;
                r[i] *= smoothR;
            }
            phase -= std::floor (phase);
        }
    private:
        double phase = 0.0;
        float smoothL = 1.0f, smoothR = 1.0f;
    };

    //==============================================================================
    /** Auto Pan: equal-power balance swung left and right in a chosen shape. */
    class AutoPan final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; phase = 0.0; smooth = 0.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = rateHz (choice (0) == 1, value (1), choice (2), bpm.load (std::memory_order_relaxed));
            const auto depth = value (3) * 0.01f;
            const auto shape = choice (4);
            const auto ease = coeffMs (2.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto before = phase;
                phase += rate / sampleRate;
                if (std::floor (phase) != std::floor (before))
                    step = noise.next();
                const auto target = depth * shapeAt (shape, phase, step);
                smooth = target + ease * (smooth - target);
                const auto angle = (smooth + 1.0f) * pi * 0.25f;
                l[i] *= 1.41421356f * std::cos (angle);
                r[i] *= 1.41421356f * std::sin (angle);
            }
            phase -= std::floor (phase);
        }
    private:
        double phase = 0.0;
        float smooth = 0.0f, step = 0.0f;
        Noise noise;
    };

    //==============================================================================
    /**
        Rotor Cabinet (as Logic's): Slow, Brake or Fast; the cabinet (Wood, Proline, Single, Split, and their
        "& Horn IR" versions); acceleration and maximum rate; motor control (Normal, Inverse, 910, Sync);
        the horn's and drum's microphones (Dynamic, Condenser or Mid-Side), front or rear, distance, angle,
        horn/drum balance, and the horn's and drum's width.
    */
    class RotarySpeaker final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iSpeed, iMaxRate, iAccel, iBalance, iDoppler, iDrive, iWidth, iCabinet, iMotor, iUpperMic, iLowerMic,
                     iMicPos, iDistance, iAngle, iDrumWidth };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            split.reset();
            hornL.prepare ((int) (0.012 * rate));
            hornR.prepare ((int) (0.012 * rate));
            for (auto& b : cabinetTone) b.reset();
            for (auto* f : { &dynHp, &dynLp, &dynHp2, &dynLp2 }) f->reset();
            hornSpeed = 0.8;
            drumSpeed = 0.7;
            hornPhase = drumPhase = 0.0;
            lastCabinet = -1;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto mode = choice (iSpeed);   // Slow, Brake, Fast
            const auto motor = choice (iMotor);  // Normal, Inv, 910, Sync
            const auto maxRate = value (iMaxRate);
            double targetHorn = mode == 2 ? maxRate : mode == 0 ? 0.83 : 0.0;
            double targetDrum = mode == 2 ? maxRate * 0.86 : mode == 0 ? 0.7 : 0.0;
            if (motor == 1) std::swap (targetHorn, targetDrum);           // Inverse: drum fast when the horn is slow
            if (motor == 2 && mode == 0) targetDrum = 0.0;                // 910: the drum stops at slow
            const auto accel = value (iAccel);
            const auto hornRate = 1.0 / std::max (0.05, accel * 0.6 * sampleRate);
            const auto drumRate = motor == 3 ? hornRate : 1.0 / std::max (0.05, accel * 2.5 * sampleRate);
            const auto balance = value (iBalance) * 0.01f;
            const auto doppler = value (iDoppler) * 0.01f;
            const auto drive = value (iDrive) * 0.01f;
            const auto hornWidth = value (iWidth) * 0.01f, drumWidth = value (iDrumWidth) * 0.01f;
            const auto cabinet = choice (iCabinet);
            const auto single = cabinet == 2, splitCab = cabinet == 3 || cabinet == 6;
            const auto rear = choice (iMicPos) == 1;
            const auto distance = value (iDistance) * 0.01f;
            const auto angle = value (iAngle) / 180.0f;
            if (cabinet != lastCabinet)
            {
                // Each cabinet's tone: Wood warmer, Proline more open; the IR versions add the box's resonances
                lastCabinet = cabinet;
                const auto ir = cabinet >= 4;
                const auto proline = cabinet == 1 || cabinet == 5;
                cabinetTone[0].setPeak (sampleRate, proline ? 1200.0 : 700.0, proline ? 1.5 : 2.5, 1.0);
                cabinetTone[1].setPeak (sampleRate, ir ? 2600.0 : 4000.0, ir ? 4.0 : 0.0, ir ? 3.0 : 1.0);
                cabinetTone[2].setLowPass (sampleRate, proline ? 9000.0 : 6500.0, 0.707);
                split.set (single ? 20000.0 : 800.0, sampleRate);
            }
            const auto excursion = (float) (0.00045 * sampleRate) * doppler * (1.0f + distance * 0.5f) * (rear ? 1.2f : 1.0f);
            dynHp.setLowPass (120.0, sampleRate); dynHp2.setLowPass (120.0, sampleRate);
            dynLp.setLowPass (7000.0, sampleRate); dynLp2.setLowPass (7000.0, sampleRate);
            const auto upperDynamic = choice (iUpperMic) == 0, lowerDynamic = choice (iLowerMic) == 0;
            const auto upperMs = choice (iUpperMic) == 2, lowerMs = choice (iLowerMic) == 2;

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
                if (single) { low = 0.0f; high = x; }
                else split.split (x, 0, low, high);
                const auto twoPi = juce::MathConstants<double>::twoPi;
                const auto micAngle = angle * 0.5f;
                const auto hs = (float) std::sin (twoPi * (hornPhase + (rear ? 0.5 : 0.0)));
                const auto hc = (float) std::cos (twoPi * (hornPhase + micAngle));
                const auto ds = (float) std::sin (twoPi * drumPhase);
                // Horn: two mics either side hear it coming and going (pitch and level)
                hornL.push (high);
                hornR.push (high);
                auto hl = hornL.read (2.0f + excursion * (1.0f + hs)) * (0.65f + 0.35f * hc);
                auto hr = hornR.read (2.0f + excursion * (1.0f - hs)) * (0.65f - 0.35f * hc);
                auto dlv = low * (0.75f + 0.25f * ds), drv = low * (0.75f - 0.25f * ds);
                if (upperDynamic) { hl = dynLp.lowPass (dynHp.highPass (hl)); hr = dynLp2.lowPass (dynHp2.highPass (hr)); }
                if (upperMs) { const auto m = 0.5f * (hl + hr), sd = 0.5f * (hl - hr) * 1.6f; hl = m + sd; hr = m - sd; }
                if (lowerDynamic) { dlv *= 0.9f; drv *= 0.9f; }
                if (lowerMs) { const auto m = 0.5f * (dlv + drv), sd = 0.5f * (dlv - drv) * 1.6f; dlv = m + sd; drv = m - sd; }
                // Widths of the horn and drum pairs
                { const auto m = 0.5f * (hl + hr), sd = 0.5f * (hl - hr) * hornWidth; hl = m + sd; hr = m - sd; }
                { const auto m = 0.5f * (dlv + drv), sd = 0.5f * (dlv - drv) * drumWidth; dlv = m + sd; drv = m - sd; }
                float wl, wr;
                if (splitCab)
                {
                    // Split: the drum to the left, the horn to the right
                    wl = (dlv + drv) * (1.0f - balance) * 2.0f + (hl + hr) * balance * 0.4f;
                    wr = (hl + hr) * balance * 2.0f + (dlv + drv) * (1.0f - balance) * 0.4f;
                }
                else
                {
                    wl = hl * balance * 2.0f + dlv * (1.0f - balance) * 2.0f;
                    wr = hr * balance * 2.0f + drv * (1.0f - balance) * 2.0f;
                }
                for (auto& b : cabinetTone)
                {
                    wl = b.process (wl, 0);
                    wr = b.process (wr, 1);
                }
                // (further mics: a little quieter)
                const auto level = 0.7f / (1.0f + distance * 0.4f);
                l[i] = wl * level;
                r[i] = wr * level;
            }
        }
    private:
        Crossover split;
        DelayLine hornL, hornR;
        std::array<Biquad, 3> cabinetTone;
        OnePole dynHp, dynLp, dynHp2, dynLp2;
        double hornSpeed = 0.8, drumSpeed = 0.7, hornPhase = 0.0, drumPhase = 0.0;
        int lastCabinet = -1;
    };

    //==============================================================================
    /**
        Ringshifter (as Logic's): a frequency shifter (one shift, or opposite shifts left and right) or a ring
        modulator (by its sine oscillator, or by the side chain). The oscillator's frequency, moved by the envelope follower and the LFO; a delay (or synced) with its level;
        the envelope follower (sensitivity, attack, decay) and the LFO (symmetry, smoothing, rate or synced);
        then dry/wet (also moved by the envelope and LFO), feedback and stereo width.
    */
    class Ringshifter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iMode, iFreq, iEnvToFreq, iLfoToFreq, iDelayTime, iDelaySync, iDelayNote, iDelayLevel,
                     iEnvOn, iEnvSens, iEnvAttack, iEnvDecay, iLfoOn, iLfoSymmetry, iLfoSmooth, iLfoRate, iLfoSync, iLfoNote,
                     iDryWet, iFeedback, iWidth, iEnvToMix, iLfoToMix };
        void setSidechain (const float* left, const float* right) noexcept override { keyL = left; keyR = right != nullptr ? right : left; }
        bool acceptsSidechain() const override { return true; }
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (2.1 * rate));
            dr.prepare ((int) (2.1 * rate));
            hl = Hilbert(); hr = Hilbert();
            phase = 0.0; lfoPhase = 0.0;
            env = 0.0f; fbL = fbR = 0.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto mode = choice (iMode);   // Single shift, Dual shift, Osc ring mod, Side chain ring mod
            const auto freq = value (iFreq);
            const auto tempo = bpm.load (std::memory_order_relaxed);
            const auto delaySeconds = choice (iDelaySync) == 1 ? (float) noteSeconds (choice (iDelayNote), tempo) : value (iDelayTime) * 0.001f;
            const auto delaySamples = juce::jlimit (1.0f, (float) (2.0 * sampleRate), delaySeconds * (float) sampleRate);
            const auto delayLevel = value (iDelayLevel) * 0.01f;
            const auto envOn = choice (iEnvOn) == 1, lfoOn = choice (iLfoOn) == 1;
            const auto sens = value (iEnvSens) * 0.01f;
            const auto att = coeffMs (value (iEnvAttack), sampleRate), dec = coeffMs (value (iEnvDecay), sampleRate);
            const auto lfoRate = rateHz (choice (iLfoSync) == 1, value (iLfoRate), choice (iLfoNote), tempo);
            const auto symmetry = value (iLfoSymmetry), smoothing = value (iLfoSmooth);
            const auto dryWet = value (iDryWet) * 0.01f;
            const auto feedback = value (iFeedback) * 0.01f * 0.95f;
            const auto width = value (iWidth) * 0.01f;
            const auto envToFreq = value (iEnvToFreq) * 0.01f, lfoToFreq = value (iLfoToFreq) * 0.01f;
            const auto envToMix = value (iEnvToMix) * 0.01f, lfoToMix = value (iLfoToMix) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto in = std::abs (0.5f * (l[i] + r[i])) * (1.0f + sens * 20.0f);
                if (envOn) env = in + (in > env ? att : dec) * (env - in);
                else env = 0.0f;
                lfoPhase += lfoRate / sampleRate;
                lfoPhase -= std::floor (lfoPhase);
                const auto lfo = lfoOn ? pulseAt (lfoPhase, symmetry, smoothing) : 0.0f;
                const auto f = freq + (envToFreq * std::min (1.0f, env) + lfoToFreq * lfo) * 1000.0f;
                phase += f / sampleRate;
                phase -= std::floor (phase);
                const auto c = (float) std::cos (juce::MathConstants<double>::twoPi * phase);
                const auto sn = (float) std::sin (juce::MathConstants<double>::twoPi * phase);

                const auto xl = l[i] + fbL * feedback, xr = r[i] + fbR * feedback;
                float wl, wr;
                if (mode <= 1)
                {
                    float il, ql, ir, qr;
                    hl.process (xl, il, ql);
                    hr.process (xr, ir, qr);
                    wl = il * c + ql * sn;                                     // shifted up
                    wr = mode == 1 ? ir * c - qr * sn : ir * c + qr * sn;      // (dual: the right shifted down)
                }
                else if (mode == 2) { wl = xl * c; wr = xr * c; }
                else
                {
                    const auto kl = keyL != nullptr ? keyL[i] : 0.0f, kr = keyR != nullptr ? keyR[i] : 0.0f;
                    wl = xl * kl; wr = xr * kr;
                }
                // The delay on the effect
                dl.push (wl);
                dr.push (wr);
                wl += dl.read (delaySamples) * delayLevel;
                wr += dr.read (delaySamples) * delayLevel;
                fbL = wl; fbR = wr;
                const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                wl = mid + side; wr = mid - side;
                const auto m = juce::jlimit (0.0f, 1.0f, dryWet + envToMix * std::min (1.0f, env) + lfoToMix * lfo * 0.5f);
                l[i] = l[i] * (1.0f - m) + wl * m;
                r[i] = r[i] * (1.0f - m) + wr * m;
            }
        }
    private:
        DelayLine dl, dr;
        Hilbert hl, hr;
        double phase = 0.0, lfoPhase = 0.0;
        float env = 0.0f, fbL = 0.0f, fbR = 0.0f;
        const float* keyL = nullptr;
        const float* keyR = nullptr;
    };

    //==============================================================================
    /**
        Scanner Vibrato (as an organ's scanner): V1-V3 vibrato (only the scanned line heard) or C0-C3 chorus
        (scanned and dry together); depth, stereo phase, left and right rates.
    */
    class ScannerVibrato final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.01 * rate));
            dr.prepare ((int) (0.01 * rate));
            phL = phR = 0.0;
            toneL.reset(); toneR.reset();
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto type = choice (0);   // V1 V2 V3 C0 C1 C2 C3
            static const float depths[] { 0.35f, 0.65f, 1.0f, 0.0f, 0.35f, 0.65f, 1.0f };
            const auto depth = depths[juce::jlimit (0, 6, type)] * value (1) * 0.01f * 0.0011f * (float) sampleRate;
            const auto chorus = type >= 3;
            const auto stereoPhase = value (2) / 360.0;
            const auto rateL = value (3), rateR = value (4);
            toneL.setLowPass (5500.0, sampleRate);
            toneR.setLowPass (5500.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                // The scanner: a triangle sweep over the line's taps (a little rounded, as the capacitor's)
                const auto tri = [] (double p) { p -= std::floor (p); return (float) (p < 0.5 ? p * 4.0 - 1.0 : 3.0 - p * 4.0); };
                const auto ml = tri (phL), mr = tri (phR + stereoPhase);
                phL += rateL / sampleRate; phR += rateR / sampleRate;
                phL -= std::floor (phL); phR -= std::floor (phR);
                dl.push (l[i]);
                dr.push (r[i]);
                const auto a = toneL.lowPass (dl.read (2.0f + depth * (1.0f + ml)));
                const auto b = toneR.lowPass (dr.read (2.0f + depth * (1.0f + mr)));
                if (type == 3) continue;    // C0: neither
                if (chorus) { l[i] = 0.5f * (l[i] + a) * 1.2f; r[i] = 0.5f * (r[i] + b) * 1.2f; }
                else { l[i] = a; r[i] = b; }
            }
        }
    private:
        DelayLine dl, dr;
        OnePole toneL, toneR;
        double phL = 0.0, phR = 0.0;
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
    const std::vector<juce::String> onOff { "Off", "On" };
    const std::vector<juce::String> distributions { "Circular", "Left/Right", "Random" };
    static const std::vector<StockEffect> effects {
        { { "chorus", "Chorus", {
              { "rate", "Rate", hz, 0.02f, 8.0f, 0.6f, 1.0f },
              { "depth", "Intensity", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "delay", "Delay", ms, 3.0f, 40.0f, 12.0f, 12.0f },
              { "voices", "Voices", "", 0.0f, 3.0f, 1.0f, 1.5f, { "1", "2", "3", "4" } },
              { "spread", "Spread", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "feedback", "Feedback", pc, 0.0f, 80.0f, 0.0f, 30.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "dMode", "D-Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff } }, "Modulation" },
          make<Chorus> },

        { { "ensemble", "Ensemble", {
              { "voices", "Voices", "", 0.0f, 6.0f, 3.0f, 3.0f, { "2", "3", "4", "5", "6", "7", "8" } },
              { "spread", "Stereo Spread", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "mix", "Output Mix", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "lfo1On", "LFO 1", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
              { "lfo1Rate", "LFO 1 Rate", hz, 0.05f, 10.0f, 0.5f, 1.0f },
              { "lfo1Int", "LFO 1 Intensity", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "lfo2On", "LFO 2", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
              { "lfo2Rate", "LFO 2 Rate", hz, 0.05f, 20.0f, 5.0f, 4.0f },
              { "lfo2Int", "LFO 2 Intensity", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "rndOn", "Random", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "rndRate", "Random Rate", hz, 0.05f, 10.0f, 1.0f, 1.0f },
              { "rndInt", "Random Intensity", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "phase", "Phase", deg, 0.0f, 360.0f, 180.0f, 180.0f },
              { "volComp", "Volume Compensation", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Modulation" },
          make<Ensemble> },

        { { "flanger", "Flanger", {
              { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.02f, 10.0f, 0.3f, 1.0f },
              { "note", "Note", "", 0.0f, notesMax, 14.0f, 7.5f, notes },
              { "delay", "Delay", ms, 0.1f, 10.0f, 1.0f, 2.0f },
              { "depth", "Intensity", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "feedback", "Feedback", pc, -100.0f, 100.0f, 50.0f, 0.0f },
              { "phase", "Stereo Phase", deg, 0.0f, 180.0f, 90.0f, 90.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f } }, "Modulation" },
          make<Flanger> },

        { { "microphaser", "Microphaser", {
              { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.02f, 10.0f, 0.5f, 1.0f },
              { "note", "Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "intensity", "Intensity", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "feedback", "Feedback", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Modulation" },
          make<Microphaser> },

        { { "phaser", "Phaser", {
              { "stages", "Stages", "", 0.0f, 8.0f, 2.0f, 4.0f, { "4", "5", "6", "7", "8", "9", "10", "11", "12" } },
              { "sync", "LFO 1 Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "rate", "LFO 1 Rate", hz, 0.02f, 10.0f, 0.4f, 1.0f },
              { "note", "LFO 1 Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "ceiling", "Ceiling", hz, 200.0f, 18000.0f, 3000.0f, 3000.0f },
              { "floor", "Floor", hz, 20.0f, 5000.0f, 200.0f, 300.0f },
              { "feedback", "Level", pc, -100.0f, 100.0f, 30.0f, 0.0f },
              { "phase", "Phase", deg, 0.0f, 360.0f, 90.0f, 180.0f },
              { "mix", "Output Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "sweep", "Sweep Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "LFO", "Envelope" } },
              { "rate2", "LFO 2 Rate", hz, 0.02f, 10.0f, 2.0f, 1.0f },
              { "sync2", "LFO 2 Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "note2", "LFO 2 Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "lfoMix", "LFO Mix", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "distribution", "Distribution", "", 0.0f, 2.0f, 0.0f, 1.0f, distributions },
              { "warmth", "Warmth", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "filter", "Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "lowCut", "Low Cut", hz, 20.0f, 2000.0f, 80.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 12000.0f, 6000.0f } }, "Modulation" },
          make<Phaser> },

        { { "tremolo", "Tremolo", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.05f, 20.0f, 4.0f, 4.0f },
              { "note", "Note", "", 0.0f, notesMax, 6.0f, 7.5f, notes },
              { "depth", "Depth", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "smoothing", "Smoothing", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "phase", "Phase", deg, 0.0f, 360.0f, 0.0f, 180.0f },
              { "symmetry", "Symmetry", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "offset", "Offset", deg, -180.0f, 180.0f, 0.0f, 0.0f },
              { "distribution", "Distribution", "", 0.0f, 2.0f, 0.0f, 1.0f, distributions } }, "Modulation" },
          make<Tremolo> },

        { { "autoPan", "Auto Pan", {
              { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, sync },
              { "rate", "Rate", hz, 0.05f, 20.0f, 1.0f, 2.0f },
              { "note", "Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "depth", "Depth", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "shape", "Shape", "", 0.0f, 4.0f, 0.0f, 2.0f, shapes } }, "Modulation" },
          make<AutoPan> },

        { { "rotary", "Rotor Cabinet", {
              { "speed", "Rotation", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Slow", "Brake", "Fast" } },
              { "fast", "Max Rate", hz, 4.0f, 9.0f, 6.7f, 6.5f },
              { "accel", "Acceleration", " s", 0.2f, 6.0f, 1.5f, 1.5f },
              { "balance", "Balance", pc, 0.0f, 100.0f, 55.0f, 50.0f },
              { "doppler", "Doppler", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "drive", "Drive", pc, 0.0f, 100.0f, 15.0f, 50.0f },
              { "width", "Horn Width", pc, 0.0f, 100.0f, 80.0f, 50.0f },
              { "cabinet", "Cabinet", "", 0.0f, 6.0f, 0.0f, 3.0f,
                { "Wood", "Proline", "Single", "Split", "Wood & Horn IR", "Proline & Horn IR", "Split & Horn IR" } },
              { "motor", "Motor Control", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Normal", "Inv", "910", "Sync" } },
              { "upperMic", "Upper Mic", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Dynamic", "Condenser", "Mid-Side" } },
              { "lowerMic", "Lower Mic", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Dynamic", "Condenser", "Mid-Side" } },
              { "micPos", "Mic Position", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Front", "Rear" } },
              { "distance", "Distance", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "angle", "Angle", deg, 0.0f, 180.0f, 90.0f, 90.0f },
              { "drumWidth", "Drum Width", pc, 0.0f, 100.0f, 60.0f, 50.0f } }, "Modulation" },
          make<RotarySpeaker> },

        { { "ringMod", "Ringshifter", {
              { "mode", "Mode", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Single Freq Shift", "Dual Freq Shift", "OSC Ring Mod", "Side Chain Ring Mod" } },
              { "freq", "Frequency", hz, -5000.0f, 5000.0f, 100.0f, 0.0f },
              { "envToFreq", "Freq Env Follow", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "lfoToFreq", "Freq LFO", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "delayTime", "Delay Time", ms, 0.0f, 2000.0f, 0.0f, 200.0f },
              { "delaySync", "Delay Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "delayNote", "Delay Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "delayLevel", "Delay Level", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "envOn", "Env Follower", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "envSens", "Sensitivity", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "envAttack", "Attack", ms, 0.1f, 500.0f, 10.0f, 30.0f },
              { "envDecay", "Decay", ms, 1.0f, 2000.0f, 200.0f, 200.0f },
              { "lfoOn", "LFO", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "lfoSymmetry", "Symmetry", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "lfoSmooth", "Smooth", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "lfoRate", "LFO Rate", hz, 0.02f, 20.0f, 1.0f, 2.0f },
              { "lfoSync", "LFO Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, sync },
              { "lfoNote", "LFO Note", "", 0.0f, notesMax, 9.0f, 7.5f, notes },
              { "mix", "Dry/Wet", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "feedback", "Feedback", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "width", "Stereo Width", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "envToMix", "Dry/Wet Env Follow", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "lfoToMix", "Dry/Wet LFO", pc, -100.0f, 100.0f, 0.0f, 0.0f } }, "Modulation" },
          make<Ringshifter> },

        { { "scannerVibrato", "Scanner Vibrato", {
              { "type", "Type", "", 0.0f, 6.0f, 6.0f, 3.0f, { "V1", "V2", "V3", "C0", "C1", "C2", "C3" } },
              { "depth", "Depth", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "phase", "Stereo Phase", deg, 0.0f, 360.0f, 0.0f, 180.0f },
              { "rateL", "Rate Left", hz, 2.0f, 10.0f, 6.9f, 6.9f },
              { "rateR", "Rate Right", hz, 2.0f, 10.0f, 6.9f, 6.9f } }, "Modulation" },
          make<ScannerVibrato> },

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
