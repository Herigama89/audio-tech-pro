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

    //==============================================================================
    // Cabinets, microphones, amp voicings (tables, as Logic's Amp Designer and Bass Amp Designer offer)
    //==============================================================================
    struct CabShape { const char* name; float lowCut, highCut, bodyF, bodyG, presF, presG; bool open; };
    const CabShape guitarCabs[] {
        { "Tweed 1 x 12", 85, 5000, 130, 3, 2200, 3, true },          { "Tweed 4 x 10", 75, 5200, 110, 4, 2500, 3, true },
        { "Tweed 1 x 10", 95, 5400, 150, 2, 2600, 3, true },          { "Black Panel 4 x 10", 70, 5600, 100, 4, 2800, 2, true },
        { "Silver Panel 2 x 12", 70, 5200, 100, 5, 2400, 2, true },   { "Black Panel 1 x 10", 95, 6000, 160, 2, 3200, 4, true },
        { "Brown Panel 1 x 12", 85, 5300, 130, 3, 2400, 2, true },    { "Brown Panel 1 x 15", 60, 4500, 90, 5, 1800, 2, true },
        { "Vintage British 4 x 12", 70, 4800, 110, 5, 2200, 5, false }, { "Modern British 4 x 12", 65, 5400, 100, 5, 2600, 5, false },
        { "Brown 4 x 12", 65, 4900, 105, 5, 2000, 4, false },         { "British Blues 2 x 12", 75, 5800, 120, 3, 2800, 5, true },
        { "Modern American 4 x 12", 60, 5000, 95, 6, 2300, 3, false }, { "Studio 1 x 12", 85, 5800, 140, 3, 3000, 3, true },
        { "British 2 x 12", 80, 5400, 120, 3, 2600, 3, true },        { "British 1 x 12", 90, 5600, 140, 2, 2900, 4, true },
        { "Boutique British 2 x 12", 75, 5500, 115, 4, 2400, 4, true }, { "Sunshine 4 x 12", 65, 4700, 100, 6, 1900, 3, false },
        { "Sunshine 1 x 12", 85, 5300, 130, 3, 2400, 3, true },       { "Stadium 4 x 12", 70, 5600, 105, 4, 3000, 6, false },
        { "Stadium 2 x 12", 75, 5500, 115, 3, 2700, 4, true },        { "Boutique Retro 2 x 12", 75, 5400, 115, 4, 2300, 3, true },
        { "High Octane 4 x 12", 60, 5800, 95, 6, 3200, 5, false },     { "Turbo 4 x 12", 55, 6000, 90, 7, 3400, 6, false },
        { "Pawnshop 1 x 8", 130, 4200, 220, 4, 2000, 4, true },        { "Direct", 0, 0, 0, 0, 0, 0, true } };
    constexpr int guitarCabCount = (int) (sizeof (guitarCabs) / sizeof (guitarCabs[0]));

    const CabShape bassCabs[] {
        { "Modern Cabinet 15\"", 35, 3500, 70, 5, 1200, 1, false },   { "Modern Cabinet 10\"", 55, 4800, 110, 3, 1800, 3, false },
        { "Modern Cabinet 6\"", 90, 6500, 180, 2, 2600, 3, false },   { "Classic Cabinet 8 x 10\"", 45, 4500, 85, 4, 1600, 3, false },
        { "Flip Top Cabinet 1 x 15\"", 35, 3200, 70, 6, 1100, 1, false }, { "Modern 3 Way", 35, 7000, 75, 3, 2000, 2, false },
        { "Direct (PowerAmp Out)", 0, 0, 0, 0, 0, 0, false },          { "Direct (PreAmp Out)", 0, 0, 0, 0, 0, 0, false } };
    constexpr int bassCabCount = (int) (sizeof (bassCabs) / sizeof (bassCabs[0]));

    /** A cabinet and microphone: the speaker's roll-offs, body and presence; the mic's own colour, where it points
        (centre fuller, rim brighter) and how close it is (closer: more bass). */
    struct Cabinet
    {
        Biquad hp, lp1, lp2, body, presence, micA, micB, proximity;
        bool off = false;
        void set (const CabShape& s, int micType, float axis, float distance, double rate, bool bassMics)
        {
            off = s.highCut <= 0.0f;
            if (off) return;
            hp.setHighPass (rate, s.lowCut * (s.open ? 1.15f : 1.0f), 0.8);
            const auto top = s.highCut * (1.0f + axis * 0.25f);
            lp1.setLowPass (rate, top, butterQ1);
            lp2.setLowPass (rate, top, butterQ2);
            body.setPeak (rate, s.bodyF, s.bodyG * (s.open ? 0.8f : 1.0f) - axis * 2.0f, 1.2);
            presence.setPeak (rate, s.presF, s.presG + axis * 3.0f, 1.0);
            proximity.setLowShelf (rate, 180.0, (1.0f - distance) * 4.0f - 1.0f, 0.7);
            // The microphones (guitar: 87, 414, 20, 57, 421, 609, Ribbon 121 - bass: 87, 20, 421)
            static const int bassMap[] { 0, 2, 4 };
            const auto m = bassMics ? bassMap[juce::jlimit (0, 2, micType)] : juce::jlimit (0, 6, micType);
            switch (m)
            {
                case 0: micA.setPeak (rate, 8000.0, 1.5, 0.7); micB.setIdentity(); break;                          // Condenser 87: open, even
                case 1: micA.setPeak (rate, 11000.0, 2.5, 0.7); micB.setPeak (rate, 200.0, -1.0, 0.8); break;      // Condenser 414: airy
                case 2: micA.setPeak (rate, 2500.0, 3.0, 0.8); micB.setLowShelf (rate, 150.0, 2.0, 0.7); break;    // Dynamic 20: punchy lows, mids
                case 3: micA.setPeak (rate, 5000.0, 5.0, 0.9); micB.setPeak (rate, 300.0, -3.0, 0.8); break;       // Dynamic 57: the classic bite
                case 4: micA.setPeak (rate, 4000.0, 3.0, 0.8); micB.setLowShelf (rate, 120.0, 2.0, 0.7); break;    // Dynamic 421: full, present
                case 5: micA.setPeak (rate, 3000.0, 4.0, 0.9); micB.setPeak (rate, 500.0, -2.0, 0.8); break;       // Dynamic 609: forward mids
                default: micA.setLowPass (rate, 7000.0, 0.7); micB.setLowShelf (rate, 160.0, 2.5, 0.7); break;     // Ribbon 121: warm, soft top
            }
        }
        void reset() { for (auto* f : { &hp, &lp1, &lp2, &body, &presence, &micA, &micB, &proximity }) f->reset(); }
        float process (float x, int ch) noexcept
        {
            if (off) return x;
            x = body.process (lp2.process (lp1.process (hp.process (x, ch), ch), ch), ch);
            return proximity.process (micB.process (micA.process (presence.process (x, ch), ch), ch), ch);
        }
    };

    /** The amp heads: preamp drive, second stage, bias (even harmonics), mid frequency, brightness, sag, input cut, cabinet. */
    struct AmpVoice { const char* name; float drive, second, bias, midF, bright, sag, inputCut, darkness; int cabinet; };
    const AmpVoice guitarAmps[] {
        { "Small Tweed Combo", 1.4f, 1.2f, 0.10f, 700, 1, 0.45f, 70, 6500, 0 },     { "Large Tweed Combo", 1.3f, 1.1f, 0.08f, 650, 1, 0.40f, 65, 7000, 1 },
        { "Mini Tweed Combo", 1.6f, 1.3f, 0.12f, 750, 0, 0.50f, 90, 6000, 2 },      { "Large Black Panel Combo", 0.6f, 0.8f, 0.02f, 500, 3, 0.20f, 70, 9000, 3 },
        { "Silver Panel Combo", 0.7f, 0.8f, 0.03f, 520, 3, 0.20f, 65, 8500, 4 },    { "Mini Black Panel Combo", 0.8f, 0.9f, 0.03f, 550, 3, 0.25f, 90, 8500, 5 },
        { "Small Brown Panel Combo", 1.0f, 1.0f, 0.05f, 600, 2, 0.30f, 80, 8000, 6 }, { "Blues Blaster Combo", 1.2f, 1.1f, 0.06f, 650, 2, 0.30f, 60, 8000, 7 },
        { "Vintage British Stack", 2.2f, 2.0f, 0.10f, 750, 2, 0.35f, 90, 6500, 8 },  { "Modern British Stack", 3.0f, 2.8f, 0.08f, 700, 2, 0.30f, 100, 7000, 9 },
        { "Brown Stack", 2.6f, 2.4f, 0.12f, 680, 1, 0.40f, 90, 6500, 10 },          { "British Blues Combo", 1.8f, 1.6f, 0.08f, 800, 3, 0.30f, 85, 7500, 11 },
        { "British Combo", 1.5f, 1.4f, 0.06f, 900, 4, 0.30f, 90, 8500, 14 },        { "Small British Combo", 1.7f, 1.5f, 0.08f, 850, 3, 0.35f, 100, 8000, 15 },
        { "Boutique British Combo", 1.6f, 1.5f, 0.07f, 850, 3, 0.30f, 90, 8000, 16 }, { "Sunshine Stack", 2.4f, 2.2f, 0.10f, 600, 1, 0.35f, 80, 6500, 17 },
        { "Small Sunshine Combo", 2.0f, 1.8f, 0.10f, 620, 1, 0.35f, 90, 7000, 18 },  { "Stadium Stack", 2.6f, 2.4f, 0.08f, 800, 3, 0.30f, 90, 7500, 19 },
        { "Stadium Combo", 2.2f, 2.0f, 0.07f, 780, 2, 0.30f, 90, 7500, 20 },        { "High Octane Stack", 4.2f, 3.6f, 0.05f, 650, 3, 0.25f, 120, 7500, 22 },
        { "Turbo Stack", 5.0f, 4.2f, 0.04f, 600, 4, 0.20f, 130, 8000, 23 },         { "Studio Combo", 2.8f, 2.6f, 0.06f, 700, 2, 0.25f, 100, 7500, 13 },
        { "Boutique Retro Combo", 1.6f, 1.5f, 0.06f, 750, 3, 0.30f, 85, 8000, 21 },  { "Pawnshop Combo", 1.8f, 1.6f, 0.15f, 900, 1, 0.55f, 140, 5000, 24 } };
    constexpr int guitarAmpCount = (int) (sizeof (guitarAmps) / sizeof (guitarAmps[0]));

    /** A spring tank: a short delay in a feedback loop through dispersive all-passes (the chirp), toned by its type. */
    struct SpringTank
    {
        DelayLine line;
        std::array<AllPass1, 6> disperse;
        OnePole tone, low;
        void prepare (double rate) { line.prepare ((int) (0.1 * rate)); for (auto& a : disperse) a.z = 0.0f; tone.reset(); low.reset(); }
        float process (float x, double rate, float decay, float brightHz, float delayMs, float chirp) noexcept
        {
            auto y = line.read (delayMs * 0.001f * (float) rate);
            for (auto& a : disperse) y = a.process (y, chirp);
            y = tone.lowPass (y);
            y = y - low.lowPass (y) * 0.8f;
            line.push (x + y * decay);
            juce::ignoreUnused (brightHz);
            return y;
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
            spring.prepare (rate);
            vibrato.prepare ((int) (0.02 * rate));
            sag = 0.0f;
            last.fill (std::numeric_limits<float>::quiet_NaN());
            onPrepare();
        }

    protected:
        virtual void onPrepare() {}
        /** The amp's voicing, from its knobs (called when one moves). */
        virtual void voice (const std::array<float, maxParams>& v) = 0;
        /** Anything after the preamp, before the power amp (Amp Designer's reverb and tremolo). */
        virtual float effects (float x) noexcept { return x; }
        /** Bass: after the preamp, before the power amp (compressor and extra EQ). */
        virtual float preampOnly (float x) noexcept { return x; }
        bool skipPowerAmp = false;

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
                if (toneOn)
                    x = high.process (mid2.process (mid.process (low.process (x, 0), 0), 0), 0);
                x = preampOnly (x);
                if (! skipPowerAmp)
                {
                    x = effects (presence.process (x, 0));
                    // Power amp: it sags (squashes) when pushed, the master sets how hard
                    sag = std::abs (x) + sagCoeff * (sag - std::abs (x));
                    x = saturate (x * master / (1.0f + sag * sagAmount)) / std::max (0.5f, master * 0.6f);
                    x = cabinet.process (x, 0);
                }
                x = dcBlock.process (x, 0) * output;
                l[i] = l[i] * (1.0f - mix) + x * mix;
                r[i] = r[i] * (1.0f - mix) + x * mix;
            }
        }

        Biquad upA, upB, downA, downB, inputHp, interstage, low, mid, mid2, high, presence, dcBlock;
        Cabinet cabinet;
        SpringTank spring;
        DelayLine vibrato;
        Lfo fxLfo;
        bool toneOn = true;
        float inputGain = 1.0f, stage1Gain = 1.0f, stage2Gain = 1.0f, bias = 0.0f, master = 1.0f, sagAmount = 0.3f, output = 1.0f, sag = 0.0f;
        std::array<float, maxParams> last {};
    };

    //==============================================================================
    /**
        Amp Designer (as Logic's): 24 amps, 25 cabinets (and Direct), an EQ type for the tone stack, gain, bass,
        mids, treble, presence, master; spring and studio reverbs (ten types, level); tremolo or vibrato
        (depth, speed or synced); seven microphones, where they point and how close; output.
    */
    class GuitarAmp final : public AmpBase
    {
    public:
        using AmpBase::AmpBase;
        enum Index { iModel, iGain, iBass, iMid, iTreble, iPresence, iMaster, iCab, iMicAxis, iOutput, iMix, iEqType, iMicDistance, iMicType,
                     iRevOn, iRevType, iRevLevel, iFxOn, iFxMode, iFxDepth, iFxSpeed, iFxSync, iFxNote };
        void process (float* l, float* r, int n) noexcept override { run (l, r, n, value (iMix) * 0.01f); }
    protected:
        float effects (float x) noexcept override
        {
            // Tremolo (level) or vibrato (pitch), then the reverb
            if (choice (iFxOn) == 1)
            {
                const auto hz = choice (iFxSync) == 1 ? 1.0 / noteSeconds (choice (iFxNote), bpm.load (std::memory_order_relaxed)) : (double) value (iFxSpeed);
                const auto m = fxLfo.next (hz, sampleRate);
                const auto depth = value (iFxDepth) * 0.01f;
                if (choice (iFxMode) == 0) x *= 1.0f - depth * (0.5f + 0.5f * m);
                else
                {
                    vibrato.push (x);
                    x = vibrato.read (2.0f + depth * 0.004f * (float) sampleRate * (0.5f + 0.5f * m));
                }
            }
            if (choice (iRevOn) == 1)
            {
                // The ten types: (decay, tone, tank delay, chirp)
                struct T { float decay, bright, delay, chirp; };
                static const T types[] { { 0.80f, 4500, 33, 0.65f }, { 0.70f, 4000, 30, 0.50f }, { 0.75f, 2800, 36, 0.55f }, { 0.75f, 6500, 30, 0.70f },
                                         { 0.78f, 2200, 38, 0.60f }, { 0.88f, 3800, 28, 0.80f }, { 0.82f, 5000, 34, 0.60f }, { 0.80f, 5500, 45, 0.30f },
                                         { 0.86f, 4800, 52, 0.25f }, { 0.84f, 3000, 48, 0.30f } };
                const auto& t = types[juce::jlimit (0, 9, choice (iRevType))];
                if (lastRevType != choice (iRevType))
                {
                    spring.tone.setLowPass (t.bright, sampleRate);
                    spring.low.setLowPass (200.0, sampleRate);
                    lastRevType = choice (iRevType);
                }
                x += spring.process (x, sampleRate, t.decay, t.bright, t.delay, t.chirp) * value (iRevLevel) * 0.01f;
            }
            return x;
        }
        void voice (const std::array<float, maxParams>& v) override
        {
            const auto& amp = guitarAmps[juce::jlimit (0, guitarAmpCount - 1, juce::roundToInt (v[iModel]))];
            const auto gain = v[iGain] * 0.1f;   // 0..10
            stage1Gain = 1.0f + gain * amp.drive;
            stage2Gain = 1.0f + gain * amp.second * 0.6f;
            bias = amp.bias;
            inputHp.setHighPass (sampleRate, amp.inputCut, 0.7);
            interstage.setLowPass (sampleRate * 2.0, amp.darkness, 0.6);
            // The EQ type: Vintage (scooped, interactive), British (mid forward), Modern (tight), Boutique (smooth)
            const auto eq = juce::roundToInt (v[iEqType]);
            static const float lowF[] { 100, 120, 90, 110 }, midShift[] { -2.0f, 2.0f, 0.0f, 0.5f }, highF[] { 2500, 3200, 3500, 2800 };
            const auto e = juce::jlimit (0, 3, eq);
            low.setLowShelf (sampleRate, lowF[e], (v[iBass] - 5.0f) * 2.4f, 0.7);
            mid.setPeak (sampleRate, amp.midF, (v[iMid] - 5.0f) * 2.2f + midShift[e], 0.8);
            mid2.setPeak (sampleRate, 400.0, e == 1 ? 1.5 : 0.0, 1.0);
            high.setHighShelf (sampleRate, highF[e] + amp.bright * 150.0f, (v[iTreble] - 5.0f) * 2.4f + amp.bright * 0.5f, 0.7);
            presence.setPeak (sampleRate, 4200.0, (v[iPresence] - 5.0f) * 1.8f, 0.9);
            master = 0.6f + v[iMaster] * 0.14f;
            sagAmount = amp.sag;
            cabinet.set (guitarCabs[juce::jlimit (0, guitarCabCount - 1, juce::roundToInt (v[iCab]))], juce::roundToInt (v[iMicType]),
                         v[iMicAxis] * 0.01f, v[iMicDistance] * 0.01f, sampleRate, false);
            dcBlock.setHighPass (sampleRate, 20.0, 0.7);
            inputGain = 1.0f;
            output = dbToGain (v[iOutput]) * (amp.drive > 3.0f ? 0.6f : 0.8f);
        }
    private:
        int lastRevType = -1;
    };

    //==============================================================================
    /**
        Bass Amp Designer (as Logic's): three amps (Classic, Flip Top, Modern), eight cabinets (and the two Direct
        outs), three microphones with their position; channel I/II, Bright, gain, bass/mids/treble with the
        Low, 1-2-3 and High switches, master; a compressor (hard/soft, amount, gain); an extra EQ (graphic, or
        two parametric bands), before or after the compressor; a D.I. box (boost, HF cut, six tone curves)
        blended with the amp; output.
    */
    class BassAmp final : public AmpBase
    {
    public:
        using AmpBase::AmpBase;
        enum Index { iGain, iBass, iMid, iTreble, iMaster, iCab, iMicAxis, iBlend, iDiLevel, iMix, iOutput,
                     iModel, iMicType, iMicDistance, iChannel, iBright, iEqOn, iLowSw, iMidSw, iHighSw,
                     iCompOn, iCompHard, iComp, iCompGain, iAddEqOn, iAddEqType, iAddEqPost,
                     iG1, iG2, iG3, iG4, iG5, iG6, iG7, iLoGain, iLoFreq, iLoQ, iHiGain, iHiFreq, iHiQ,
                     iDiBoost, iDiHfCut, iDiToneOn, iDiTone };
        void process (float* l, float* r, int n) noexcept override
        {
            if (n > 1024)
            {
                for (int at = 0; at < n; at += 1024)
                    process (l + at, r + at, std::min (1024, n - at));
                return;
            }
            // Blend: the D.I. box under (or instead of) the amp
            const auto blend = value (iBlend) * 0.01f;
            for (int i = 0; i < n; ++i)
                di[(size_t) i] = diBox (0.5f * (l[i] + r[i]));
            run (l, r, n, value (iMix) * 0.01f);
            for (int i = 0; i < n; ++i)
            {
                const auto d = di[(size_t) i] * dbToGain (value (iDiLevel));
                l[i] = l[i] * (1.0f - blend) + d * blend;
                r[i] = r[i] * (1.0f - blend) + d * blend;
            }
        }
    protected:
        void onPrepare() override
        {
            for (auto& b : graphic) b.reset();
            for (auto* b : { &loMid, &hiMid, &diTone1, &diTone2, &diHf }) b->reset();
            compEnv = 0.0f;
        }
        float diBox (float x) noexcept
        {
            x *= dbToGain (value (iDiBoost));
            if (choice (iDiHfCut) == 1) x = diHf.process (x, 1);
            if (choice (iDiToneOn) == 1) x = diTone2.process (diTone1.process (x, 1), 1);
            return x;
        }
        float compress (float x) noexcept
        {
            if (choice (iCompOn) != 1) return x;
            const auto hard = choice (iCompHard) == 0;
            const auto ax = std::abs (x);
            compEnv = ax + (ax > compEnv ? (hard ? 0.995f : 0.9995f) : (hard ? 0.9999f : 0.99997f)) * (compEnv - ax);
            const auto amount = value (iComp) * 0.01f;
            const auto threshold = dbToGain (-6.0f - amount * 30.0f);
            const auto ratio = hard ? 6.0f : 2.5f;
            const auto over = compEnv > threshold ? gainToDb (compEnv / threshold) : 0.0f;
            // (AutoGain makes up what the compression took, the knob adds or takes off)
            return x * dbToGain (-over * (1.0f - 1.0f / ratio) + amount * 12.0f * (1.0f - 1.0f / ratio) + value (iCompGain));
        }
        float addEq (float x) noexcept
        {
            if (choice (iAddEqOn) != 1) return x;
            if (choice (iAddEqType) == 0)
            {
                for (auto& b : graphic) x = b.process (x, 0);
                return x;
            }
            return hiMid.process (loMid.process (x, 0), 0);
        }
        float preampOnly (float x) noexcept override
        {
            // The extra EQ before or after the compressor (Pre/Post)
            if (choice (iAddEqPost) == 0) return compress (addEq (x));
            return addEq (compress (x));
        }
        void voice (const std::array<float, maxParams>& v) override
        {
            const auto model = juce::jlimit (0, 2, juce::roundToInt (v[iModel]));
            static const float drives[] { 1.8f, 1.4f, 1.2f }, seconds[] { 1.2f, 1.0f, 1.0f }, biases[] { 0.06f, 0.08f, 0.03f }, sags[] { 0.25f, 0.35f, 0.15f };
            static const float midFs[] { 500.0f, 400.0f, 650.0f };
            const auto channel2 = juce::roundToInt (v[iChannel]) == 1;
            const auto gain = v[iGain] * 0.1f * (channel2 ? 0.18f : 1.0f);   // (channel II is passive: -15 dB)
            stage1Gain = 1.0f + gain * drives[model];
            stage2Gain = 1.0f + gain * seconds[model];
            bias = biases[model];
            inputHp.setHighPass (sampleRate, 30.0, 0.7);
            interstage.setLowPass (sampleRate * 2.0, juce::roundToInt (v[iBright]) == 1 ? 8000.0 : 5000.0, 0.6);
            toneOn = juce::roundToInt (v[iEqOn]) == 1;
            const auto lowSw = juce::roundToInt (v[iLowSw]) == 1, highSw = juce::roundToInt (v[iHighSw]) == 1;
            static const float midSwitch[] { 250.0f, 500.0f, 1000.0f };
            low.setLowShelf (sampleRate, lowSw ? 50.0 : 90.0, (v[iBass] - 5.0f) * 2.6f + (lowSw ? 2.0f : 0.0f), 0.8);
            mid.setPeak (sampleRate, midSwitch[juce::jlimit (0, 2, juce::roundToInt (v[iMidSw]))] * midFs[model] / 500.0f, (v[iMid] - 5.0f) * 2.4f, 0.9);
            mid2.setIdentity();
            high.setHighShelf (sampleRate, highSw ? 5000.0 : 3000.0, (v[iTreble] - 5.0f) * 2.4f + (juce::roundToInt (v[iBright]) == 1 ? 3.0f : 0.0f), 0.7);
            presence.setIdentity();
            master = 0.6f + v[iMaster] * 0.14f;
            sagAmount = sags[model];
            const auto cab = juce::jlimit (0, bassCabCount - 1, juce::roundToInt (v[iCab]));
            cabinet.set (bassCabs[cab], juce::roundToInt (v[iMicType]), v[iMicAxis] * 0.01f, v[iMicDistance] * 0.01f, sampleRate, true);
            skipPowerAmp = cab == bassCabCount - 1;   // (Direct, PreAmp Out: no power amp, no cabinet)
            dcBlock.setHighPass (sampleRate, 15.0, 0.7);
            output = dbToGain (v[iOutput]) * 0.8f;
            // The extra EQ
            static const float bands[] { 40, 80, 160, 320, 640, 1250, 2500 };
            for (int b = 0; b < 7; ++b)
                graphic[(size_t) b].setPeak (sampleRate, bands[b], v[iG1 + b], 1.2);
            loMid.setPeak (sampleRate, v[iLoFreq] * 1000.0, v[iLoGain], v[iLoQ]);
            hiMid.setPeak (sampleRate, v[iHiFreq] * 1000.0, v[iHiGain], v[iHiQ]);
            // The D.I. box: HF cut, and its six tone curves (scoops and slopes)
            diHf.setLowPass (sampleRate, 6000.0, 0.7);
            switch (juce::jlimit (0, 5, juce::roundToInt (v[iDiTone])))
            {
                case 0: diTone1.setPeak (sampleRate, 1000.0, -6.0, 0.4); diTone2.setIdentity(); break;
                case 1: diTone1.setPeak (sampleRate, 1000.0, -18.0, 0.5); diTone2.setPeak (sampleRate, 1000.0, -6.0, 1.5); break;
                case 2: diTone1.setPeak (sampleRate, 1000.0, -3.0, 0.4); diTone2.setIdentity(); break;
                case 3: diTone1.setPeak (sampleRate, 3000.0, -3.0, 0.6); diTone2.setIdentity(); break;
                case 4: diTone1.setHighPass (sampleRate, 60.0, 0.7); diTone2.setPeak (sampleRate, 900.0, 3.0, 0.7); break;
                default: diTone1.setHighPass (sampleRate, 120.0, 0.7); diTone2.setPeak (sampleRate, 900.0, 3.0, 0.7); break;
            }
        }
    private:
        std::array<float, 1024> di {};
        std::array<Biquad, 7> graphic;
        Biquad loMid, hiMid, diTone1, diTone2, diHf;
        float compEnv = 0.0f;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockAmpEffects()
{
    const juce::String dB (" dB"), pc (" %");
    std::vector<juce::String> amps, cabs, bassCabNames;
    for (const auto& a : guitarAmps) amps.push_back (a.name);
    for (const auto& c : guitarCabs) cabs.push_back (c.name);
    for (const auto& c : bassCabs) bassCabNames.push_back (c.name);
    const std::vector<juce::String> onOff { "Off", "On" };
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    static const std::vector<StockEffect> effects {
        { { "guitarAmp", "Amp Designer", {
              { "model", "Amp", "", 0.0f, (float) guitarAmpCount - 1.0f, 9.0f, 11.5f, amps },
              { "gain", "Gain", "", 0.0f, 100.0f, 45.0f, 50.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "mid", "Mids", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "presence", "Presence", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "master", "Master", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "cab", "Cabinet", "", 0.0f, (float) guitarCabCount - 1.0f, 9.0f, 12.5f, cabs },
              { "mic", "Mic Position", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "eqType", "EQ", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Vintage", "British", "Modern", "Boutique" } },
              { "micDistance", "Mic Distance", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "micType", "Mic", "", 0.0f, 6.0f, 3.0f, 3.0f,
                { "Condenser 87", "Condenser 414", "Dynamic 20", "Dynamic 57", "Dynamic 421", "Dynamic 609", "Ribbon 121" } },
              { "revOn", "Reverb", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "revType", "Reverb Type", "", 0.0f, 9.0f, 0.0f, 4.5f,
                { "Vintage Spring", "Simple Spring", "Mellow Spring", "Bright Spring", "Dark Spring", "Resonant Spring", "Boutique Spring",
                  "Sweet Reverb", "Rich Reverb", "Warm Reverb" } },
              { "revLevel", "Reverb Level", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "fxOn", "Effects", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "fxMode", "Trem/Vib", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Tremolo", "Vibrato" } },
              { "fxDepth", "Depth", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "fxSpeed", "Speed", " Hz", 0.1f, 12.0f, 4.0f, 4.0f },
              { "fxSync", "Sync/Free", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Free", "Sync" } },
              { "fxNote", "Speed Note", "", 0.0f, notesMax, 6.0f, 7.5f, notes } }, "Amps and Pedals" },
          make<GuitarAmp> },
        { { "bassAmp", "Bass Amp Designer", {
              { "gain", "Gain", "", 0.0f, 100.0f, 30.0f, 50.0f },
              { "bass", "Bass", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "lowMid", "Mids", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "treble", "Treble", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "master", "Master", "", 0.0f, 10.0f, 5.0f, 5.0f },
              { "cab", "Cabinet", "", 0.0f, (float) bassCabCount - 1.0f, 3.0f, 3.5f, bassCabNames },
              { "mic", "Mic Position", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "blend", "Blend", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "diLevel", "DI Level", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "model", "Amp", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Classic Amp", "Flip Top Amp", "Modern Amp" } },
              { "micType", "Mic", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Condenser 87", "Dynamic 20", "Dynamic 421" } },
              { "micDistance", "Mic Distance", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "channel", "Channel", "", 0.0f, 1.0f, 0.0f, 0.5f, { "I", "II" } },
              { "bright", "Bright", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "eqOn", "EQ", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
              { "lowSw", "Low", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Deep" } },
              { "midSw", "Mid Freq", "", 0.0f, 2.0f, 1.0f, 1.0f, { "1", "2", "3" } },
              { "highSw", "High", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Normal", "Bright" } },
              { "compOn", "Compressor", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "compHard", "Hard/Soft", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Hard", "Soft" } },
              { "comp", "Comp", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "compGain", "Comp Gain", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "addEqOn", "Additional EQ", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "addEqType", "EQ Type", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Graphic", "Parametric" } },
              { "addEqPost", "Pre/Post", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Pre", "Post" } },
              { "g1", "40", dB, -15.0f, 15.0f, 0.0f, 0.0f }, { "g2", "80", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "g3", "160", dB, -15.0f, 15.0f, 0.0f, 0.0f }, { "g4", "320", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "g5", "640", dB, -15.0f, 15.0f, 0.0f, 0.0f }, { "g6", "1.25k", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "g7", "2.5k", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "loGain", "LoMid Gain", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "loFreq", "LoMid kHz", " kHz", 0.05f, 1.0f, 0.25f, 0.25f },
              { "loQ", "LoMid Q", "", 0.3f, 4.0f, 1.0f, 1.0f },
              { "hiGain", "HiMid Gain", dB, -15.0f, 15.0f, 0.0f, 0.0f },
              { "hiFreq", "HiMid kHz", " kHz", 0.5f, 8.0f, 2.0f, 2.0f },
              { "hiQ", "HiMid Q", "", 0.3f, 4.0f, 1.0f, 1.0f },
              { "diBoost", "DI Boost", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "diHfCut", "HF Cut", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "diToneOn", "DI Tone", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "diTone", "DI Tone Curve", "", 0.0f, 5.0f, 0.0f, 2.5f, { "1", "2", "3", "4", "5", "6" } },
              { "page", "View", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Amp", "Pedals", "D.I. Box" } } }, "Amps and Pedals" },
          make<BassAmp> },
    };
    return effects;
}

} // namespace riff
