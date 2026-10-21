#include "StockDsp.h"

/*
    The stock filters (Logic's Filter folder): an auto filter (cutoff moved by an LFO and by the
    sound's own level), an auto wah, and a step filter whose cutoff steps through eight values in
    time with the song.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** The filter types these share: 12 and 24 dB low pass, high pass, band pass, notch. */
    struct ModeFilter
    {
        std::array<Svf, 2> first, second;   // (two in series for 24 dB)
        void reset() noexcept { for (auto& f : first) f.reset(); for (auto& f : second) f.reset(); }
        void set (double freq, double q, double rate) noexcept
        {
            for (auto& f : first) f.set (freq, q, rate);
            for (auto& f : second) f.set (freq, 0.707, rate);
        }
        float process (float x, int ch, int mode) noexcept
        {
            float lp, bp, hp;
            first[(size_t) ch].tick (x, lp, bp, hp);
            switch (mode)
            {
                case 1: { float lp2, bp2, hp2; second[(size_t) ch].tick (lp, lp2, bp2, hp2); return lp2; }
                case 2: return hp;
                case 3: return bp;
                case 4: return lp + hp;
                default: return lp;
            }
        }
    };

    /** An LFO wave at a phase: saw down, saw up, triangle, pulse (with width), sample & hold, smooth random. */
    struct WaveLfo
    {
        double phase = 0.0;
        float held = 0.0f, from = 0.0f, to = 0.0f;
        Noise noise;
        float at (int wave, double offset, float width) noexcept
        {
            auto p = phase + offset;
            p -= std::floor (p);
            switch (wave)
            {
                case 0:  return (float) (1.0 - 2.0 * p);
                case 1:  return (float) (2.0 * p - 1.0);
                case 2:  return (float) (p < 0.5 ? p * 4.0 - 1.0 : 3.0 - p * 4.0);
                case 3:  return p < juce::jlimit (0.02, 0.98, (double) width) ? 1.0f : -1.0f;
                case 4:  return held;
                default: { const auto t = (float) p; const auto s = t * t * (3.0f - 2.0f * t); return from + (to - from) * s; }
            }
        }
        void advance (double hz, double rate, int samples) noexcept
        {
            const auto before = phase;
            phase += hz * samples / rate;
            if (std::floor (phase) != std::floor (before))
            {
                held = noise.next();
                from = to;
                to = held;
            }
            phase -= std::floor (phase);
        }
    };

    //==============================================================================
    /**
        AutoFilter (as Logic's): a resonant filter (HP, BP, LP, or a 4-pole low pass of 6 to 24 dB/oct) with
        Fatness and stereo Spread; its cutoff moved by an ADSR envelope (started above the threshold, scaled
        by Dynamic) and by an LFO (six waves, pulse width, rate or synced with phase, stereo phase,
        retrigger); distortion before and after the filter; dry signal and main out.
    */
    class AutoFilter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iType, iCutoff, iResonance, iSync, iNote, iRate, iLfoToCutoff, iEnvToCutoff, iAttack, iRelease, iPreDist, iMainOut, iStereoPhase,
                     iFilterOn, iFatness, iSpread, iEnvOn, iThreshold, iDynamic, iDecay, iSustain, iLfoOn, iSyncPhase, iRetrigger, iWave, iPulseWidth,
                     iDistOn, iPostDist, iPreMode, iPostMode, iDry };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& f : filters) f.reset();
            ladder = {};
            env = 0.0f; level = 0.0f;
            stage = 0;
            triggered = false;
            lfo = WaveLfo();
            fatL.reset(); fatR.reset();
        }

        static float distort (int mode, float x, float amount) noexcept
        {
            if (amount <= 0.0f) return x;
            const auto d = 1.0f + amount * 0.01f * 20.0f;
            switch (mode)
            {
                case 1:  return juce::jlimit (-1.0f, 1.0f, x * d) / std::sqrt (d);                       // Hard
                case 2:  return (std::tanh (x * d + 0.2f) - std::tanh (0.2f)) / std::sqrt (d);          // Tube (biased)
                default: return std::tanh (x * d) / std::sqrt (d);                                       // Soft
            }
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto type = choice (iType);   // HP, BP, LP, LP 6, LP 12, LP 18, LP 24
            const auto cutoff = value (iCutoff);
            const auto q = 0.5f + value (iResonance) * 0.01f * 11.5f;
            const auto tempo = bpm.load (std::memory_order_relaxed);
            const auto synced = choice (iSync) == 1;
            const auto lfoHz = synced ? 1.0 / noteSeconds (choice (iNote), tempo) : (double) value (iRate);
            const auto lfoOn = choice (iLfoOn) == 1, envOn = choice (iEnvOn) == 1, filterOn = choice (iFilterOn) == 1, distOn = choice (iDistOn) == 1;
            const auto lfoAmount = lfoOn ? value (iLfoToCutoff) * 0.01f : 0.0f;   // octaves: +-4 at full
            const auto envAmount = envOn ? value (iEnvToCutoff) * 0.01f : 0.0f;   // octaves: +-5 at full
            const auto threshold = dbToGain (value (iThreshold));
            const auto dynamic = value (iDynamic) * 0.01f;
            const auto attack = 1.0f / std::max (1.0f, value (iAttack) * 0.001f * (float) sampleRate / 16.0f);
            const auto decay = coeffMs (value (iDecay), sampleRate / 16.0);
            const auto sustain = value (iSustain) * 0.01f;
            const auto release = coeffMs (value (iRelease), sampleRate / 16.0);
            const auto stereo = value (iStereoPhase) / 360.0, syncPhase = synced ? value (iSyncPhase) / 360.0 : 0.0;
            const auto wave = choice (iWave);
            const auto width = value (iPulseWidth) * 0.01f;
            const auto spread = value (iSpread) * 0.01f;
            const auto fatness = value (iFatness) * 0.01f;
            const auto pre = distOn ? value (iPreDist) : 0.0f, post = distOn ? value (iPostDist) : 0.0f;
            const auto preMode = choice (iPreMode), postMode = choice (iPostMode);
            const auto dry = value (iDry) * 0.01f, out = dbToGain (value (iMainOut));
            fatL.setLowPass (200.0, sampleRate);
            fatR.setLowPass (200.0, sampleRate);

            // Retrigger: the LFO starts over with each new phrase (when the envelope starts)
            for (int i = 0; i < n; i += 16)
            {
                const auto count = std::min (16, n - i);
                float peak = 0.0f;
                for (int k = 0; k < count; ++k)
                    peak = std::max (peak, std::max (std::abs (l[i + k]), std::abs (r[i + k])));
                level = std::max (peak, level * 0.995f);
                // The envelope: attack to full, decay to sustain while above the threshold, release below it
                if (peak > threshold && ! triggered)
                {
                    triggered = true;
                    stage = 1;
                    velocity = 1.0f - dynamic + dynamic * std::min (1.0f, peak * 2.0f);
                    if (choice (iRetrigger) == 1) lfo.phase = 0.0;
                }
                else if (peak < threshold * 0.5f && triggered)
                {
                    triggered = false;
                    stage = 3;
                }
                switch (stage)
                {
                    case 1: env += attack; if (env >= 1.0f) { env = 1.0f; stage = 2; } break;
                    case 2: env = sustain + decay * (env - sustain); break;
                    case 3: env = release * env; break;
                    default: break;
                }
                const auto envOct = envAmount * 5.0f * env * velocity;
                const auto lfoL = lfo.at (wave, syncPhase, width), lfoR = lfo.at (wave, syncPhase + stereo, width);
                lfo.advance (lfoHz, sampleRate, count);
                const auto fl = cutoff * std::pow (2.0f, lfoL * lfoAmount * 4.0f + envOct - spread);
                const auto fr = cutoff * std::pow (2.0f, lfoR * lfoAmount * 4.0f + envOct + spread);
                filters[0].set (fl, q, sampleRate);
                filters[1].set (fr, q, sampleRate);
                ladderG[0] = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sampleRate * 0.45, (double) fl) / sampleRate);
                ladderG[1] = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sampleRate * 0.45, (double) fr) / sampleRate);
                ladderK = value (iResonance) * 0.01f * 3.6f;
                for (int k = i; k < i + count; ++k)
                {
                    auto a = distort (preMode, l[k], pre), b = distort (preMode, r[k], pre);
                    if (filterOn)
                    {
                        a = filterOne (a, 0, type);
                        b = filterOne (b, 1, type);
                        // Fatness: the lows the resonance thins out, put back
                        a += fatness * fatL.lowPass (l[k]);
                        b += fatness * fatR.lowPass (r[k]);
                    }
                    a = distort (postMode, a, post);
                    b = distort (postMode, b, post);
                    l[k] = (l[k] * dry + a) * out;
                    r[k] = (r[k] * dry + b) * out;
                }
            }
        }

    private:
        std::array<Svf, 2> filters;
        OnePole fatL, fatR;
        WaveLfo lfo;
        float env = 0.0f, level = 0.0f, velocity = 1.0f;
        int stage = 0;
        bool triggered = false;

        // The 4-pole low pass: one-pole stages (6 dB each) with feedback from the last for resonance
        std::array<std::array<float, 4>, 2> ladder {};
        std::array<float, 2> ladderG {};
        float ladderK = 0.0f;

        float filterOne (float x, int ch, int type) noexcept
        {
            float lp, bp, hp;
            filters[(size_t) ch].tick (x, lp, bp, hp);
            if (type == 0) return hp;
            if (type == 1) return bp;
            if (type == 2) return lp;
            const auto stages = type - 2;   // 1..4 (6 to 24 dB per octave)
            auto& z = ladder[(size_t) ch];
            const auto g = ladderG[(size_t) ch];
            auto y = x - ladderK * z[(size_t) (stages - 1)];
            y = std::tanh (y);
            for (int s = 0; s < stages; ++s)
            {
                const auto v = (y - z[(size_t) s]) * g / (1.0f + g);
                const auto out = v + z[(size_t) s];
                z[(size_t) s] = out + v;
                y = out;
            }
            return y * (1.0f + ladderK * 0.5f);
        }
    };

    //==============================================================================
    /** An envelope wah: the louder you play, the higher the peak sweeps. */
    class AutoWah final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; filter.reset(); env = 0.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto sensitivity = dbToGain (value (0));
            const auto low = value (1), high = std::max (value (1) * 1.2f, value (2));
            const auto q = 1.0f + value (3) * 0.01f * 11.0f;
            const auto attack = coeffMs (value (4), sampleRate), release = coeffMs (value (5), sampleRate);
            const auto mode = choice (6) == 0 ? 3 : 0;   // band pass or low pass
            const auto mix = value (7) * 0.01f;
            for (int i = 0; i < n; i += 16)
            {
                const auto count = std::min (16, n - i);
                float level = 0.0f;
                for (int k = 0; k < count; ++k)
                    level = std::max (level, std::max (std::abs (l[i + k]), std::abs (r[i + k])));
                level = std::min (1.0f, level * sensitivity);
                env = level + (level > env ? attack : release) * (env - level);
                const auto f = low * std::pow (high / low, std::sqrt (env));
                filter.set (f, q, sampleRate);
                for (int k = i; k < i + count; ++k)
                {
                    const auto a = filter.process (l[k], 0, mode) * (mode == 3 ? 1.6f : 1.0f);
                    const auto b = filter.process (r[k], 1, mode) * (mode == 3 ? 1.6f : 1.0f);
                    l[k] = l[k] * (1.0f - mix) + a * mix;
                    r[k] = r[k] * (1.0f - mix) + b * mix;
                }
            }
        }
    private:
        ModeFilter filter;
        float env = 0.0f;
    };

    //==============================================================================
    /** Eight steps of cutoff, one per note value, gliding or jumping, in time with the song. */
    class StepFilter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; filter.reset(); position = 0.0; current = -1.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto stepSeconds = noteSeconds (choice (8), bpm.load (std::memory_order_relaxed));
            const auto low = value (9), high = std::max (value (9) * 1.5f, value (10));
            const auto q = 0.7f + value (11) * 0.01f * 9.0f;
            const auto glide = coeffMs (std::max (0.1f, value (12)), sampleRate / 16.0);
            const auto mix = value (13) * 0.01f;
            for (int i = 0; i < n; i += 16)
            {
                const auto count = std::min (16, n - i);
                const auto step = (int) std::floor (position / stepSeconds) % 8;
                position += count / sampleRate;
                if (position > stepSeconds * 8.0)
                    position -= stepSeconds * 8.0;
                const auto target = low * std::pow (high / low, value (step) * 0.01f);
                current = current < 0.0f ? target : target + glide * (current - target);
                filter.set (current, q, sampleRate);
                for (int k = i; k < i + count; ++k)
                {
                    const auto a = filter.process (l[k], 0, 1), b = filter.process (r[k], 1, 1);
                    l[k] = l[k] * (1.0f - mix) + a * mix;
                    r[k] = r[k] * (1.0f - mix) + b * mix;
                }
            }
        }
    private:
        ModeFilter filter;
        double position = 0.0;
        float current = -1.0f;
    };

    //==============================================================================
    /**
        Fuzz-Wah (as Logic's): a wah (Classic, Retro, Modern, Opto 1 and 2, Resonant LP/HP, Peak) moved by the
        playing's level (depth, attack, release, range) with relative Q and auto gain; a compressor (ratio);
        a fuzz (gain, tone); and the order the three come in.
    */
    class FuzzWah final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iWahOn, iType, iAutoGain, iRelQ, iDepth, iAttack, iRelease, iRangeLow, iRangeHigh, iCompOn, iRatio, iFuzzOn, iFuzzGain, iTone, iOrder, iOutput };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& f : wah) f.reset();
            env = 0.0f; compEnv = 0.0f; agcL = agcR = 1.0f;
            toneL.reset(); toneR.reset();
        }
        void process (float* l, float* r, int n) noexcept override
        {
            static const float centreQ[] { 4.0f, 3.0f, 5.5f, 6.0f, 7.0f, 2.0f, 2.0f, 3.0f };
            const auto type = juce::jlimit (0, 7, choice (iType));
            const auto q = centreQ[type] * (0.5f + value (iRelQ) * 0.01f);
            const auto depth = value (iDepth) * 0.01f;
            const auto attack = coeffMs (value (iAttack), sampleRate / 16.0), release = coeffMs (value (iRelease), sampleRate / 16.0);
            const auto low = value (iRangeLow), high = std::max (value (iRangeLow) * 1.2f, value (iRangeHigh));
            const auto ratio = std::max (1.0f, value (iRatio));
            const auto fuzz = dbToGain (value (iFuzzGain));
            toneL.setLowPass (500.0 + value (iTone) * 0.01 * 9500.0, sampleRate);
            toneR.setLowPass (500.0 + value (iTone) * 0.01 * 9500.0, sampleRate);
            const auto wahOn = choice (iWahOn) == 1, compOn = choice (iCompOn) == 1, fuzzOn = choice (iFuzzOn) == 1;
            const auto fuzzFirst = choice (iOrder) == 1;
            const auto autoGain = choice (iAutoGain) == 1;
            const auto out = dbToGain (value (iOutput));
            for (int i = 0; i < n; i += 16)
            {
                const auto count = std::min (16, n - i);
                float peak = 0.0f;
                for (int k = 0; k < count; ++k) peak = std::max (peak, std::max (std::abs (l[i + k]), std::abs (r[i + k])));
                const auto lv = std::min (1.0f, peak * 4.0f);
                env = lv + (lv > env ? attack : release) * (env - lv);
                const auto f = low * std::pow (high / low, std::sqrt (env) * depth);
                for (auto& w : wah) w.set (f, q, sampleRate);
                for (int k = i; k < i + count; ++k)
                {
                    auto a = l[k], b = r[k];
                    const auto doFuzz = [&]
                    {
                        if (! fuzzOn) return;
                        a = toneL.lowPass (std::tanh (a * fuzz * 3.0f) * 0.5f);
                        b = toneR.lowPass (std::tanh (b * fuzz * 3.0f) * 0.5f);
                    };
                    if (fuzzFirst) doFuzz();
                    if (wahOn)
                    {
                        a = wahOne (wah[0], a, type);
                        b = wahOne (wah[1], b, type);
                        if (autoGain)
                        {
                            // (the wah's peaks evened out)
                            const auto la = std::abs (a), lb = std::abs (b);
                            agcL = std::max (la, agcL * 0.9995f); agcR = std::max (lb, agcR * 0.9995f);
                            a /= std::max (1.0f, agcL * 1.5f); b /= std::max (1.0f, agcR * 1.5f);
                        }
                    }
                    if (compOn)
                    {
                        const auto x = std::max (std::abs (a), std::abs (b));
                        compEnv = x + (x > compEnv ? 0.99f : 0.9995f) * (compEnv - x);
                        const auto overDb = std::max (0.0f, gainToDb (compEnv) + 24.0f);
                        const auto g = dbToGain (-overDb * (1.0f - 1.0f / ratio) + (ratio - 1.0f) * 1.5f);
                        a *= g; b *= g;
                    }
                    if (! fuzzFirst) doFuzz();
                    l[k] = a * out;
                    r[k] = b * out;
                }
            }
        }
    private:
        std::array<Svf, 2> wah;
        OnePole toneL, toneR;
        float env = 0.0f, compEnv = 0.0f, agcL = 1.0f, agcR = 1.0f;

        static float wahOne (Svf& f, float x, int type) noexcept
        {
            float lp, bp, hp;
            f.tick (x, lp, bp, hp);
            switch (type)
            {
                case 0:  return bp * 2.2f + lp * 0.25f;            // Classic: a slight low end under the peak
                case 1:  return bp * 2.0f + lp * 0.1f;             // Retro
                case 2:  return bp * 2.4f + x * 0.15f;             // Modern: constant level, some dry
                case 3:  return bp * 2.6f + lp * 0.05f;            // Opto 1
                case 4:  return bp * 2.8f;                          // Opto 2
                case 5:  return lp;                                 // Resonant LP
                case 6:  return hp;                                 // Resonant HP
                default: return x + bp * 1.5f;                      // Peak: a bell on the dry sound
            }
        }
    };

    //==============================================================================
    /**
        Spectral Gate (as Logic's): a band (centre frequency and bandwidth, both wobbled at Speed by CF and
        BW Modulation) split from the rest; the band's parts above the threshold (Super Energy) and below it
        (Sub Energy) at their own levels; the original lows and highs either side mixed back; gain.
    */
    class SpectralGate final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iThreshold, iSpeed, iCfMod, iBwMod, iCentre, iBandwidth, iLowLevel, iSuper, iSub, iHighLevel, iGain };
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& f : lowSplit) f.reset();
            for (auto& f : highSplit) f.reset();
            envL = envR = 0.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = dbToGain (value (iThreshold));
            const auto speed = value (iSpeed), cfMod = value (iCfMod) * 0.01f, bwMod = value (iBwMod) * 0.01f;
            const auto centre = value (iCentre), bandwidth = value (iBandwidth);
            const auto lowLevel = value (iLowLevel) * 0.01f, highLevel = value (iHighLevel) * 0.01f;
            const auto super = value (iSuper) * 0.01f, sub = value (iSub) * 0.01f;
            const auto gain = dbToGain (value (iGain));
            const auto follow = coeffMs (10.0, sampleRate);
            for (int i = 0; i < n; i += 16)
            {
                const auto count = std::min (16, n - i);
                const auto m = lfo.next (speed, sampleRate);
                for (int k = 1; k < count; ++k) lfo.next (speed, sampleRate);
                const auto c = centre * std::pow (2.0f, m * cfMod * 2.0f);
                const auto bw = std::max (20.0f, bandwidth * (1.0f + m * bwMod * 0.8f));
                const auto lo = std::max (20.0f, c - bw * 0.5f), hi = std::min ((float) sampleRate * 0.45f, c + bw * 0.5f);
                for (int ch = 0; ch < 2; ++ch)
                {
                    lowSplit[(size_t) ch].set (lo, 0.707, sampleRate);
                    highSplit[(size_t) ch].set (hi, 0.707, sampleRate);
                }
                for (int k = i; k < i + count; ++k)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto& x = ch == 0 ? l[k] : r[k];
                        auto& env = ch == 0 ? envL : envR;
                        float lowLp, lowBp, lowHp, hiLp, hiBp, hiHp;
                        lowSplit[(size_t) ch].tick (x, lowLp, lowBp, lowHp);   // below the band: lowLp
                        highSplit[(size_t) ch].tick (lowHp, hiLp, hiBp, hiHp); // the band: hiLp, above it: hiHp
                        const auto band = hiLp;
                        env = std::abs (band) + follow * (env - std::abs (band));
                        const auto bandOut = env > threshold ? band * super : band * sub;
                        x = (lowLp * lowLevel + bandOut + hiHp * highLevel) * gain;
                    }
                }
            }
        }
    private:
        std::array<Svf, 2> lowSplit, highSplit;
        Lfo lfo;
        float envL = 0.0f, envR = 0.0f;
    };

    //==============================================================================
    /**
        Formant Filterbank (as Logic's EVOC 20 Filterbank): two banks of up to 20 band-pass filters between the
        lowest and highest frequency, each band's level set in bank A and bank B, crossfaded (Fade AB);
        formant shift, resonance, slope, the lowest band band-pass or high-pass and the highest band-pass or
        low-pass; a Shift LFO and a Fade LFO (rate, wave, intensity); overdrive, level, stereo width.
    */
    class FormantFilterbank final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        static constexpr int maxBands = 20;
        enum Index { iLow, iHigh, iShift, iBands, iLowest, iHighest, iResonance, iSlope, iFade,
                     iShiftLfo, iShiftRate, iShiftWave, iFadeLfo, iFadeRate, iFadeWave, iOverdrive, iLevel, iWidth, iBankA, iBankB = iBankA + maxBands };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& f : bandsL) f.reset();
            for (auto& f : bandsR) f.reset();
            for (auto& f : second) f.reset();
            lastKey = -1.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto count = juce::jlimit (5, maxBands, choice (iBands) + 5);
            const auto steep = choice (iSlope) == 1;
            const auto res = 2.0f + value (iResonance) * 0.01f * 18.0f;
            const auto lowestHp = choice (iLowest) == 1, highestLp = choice (iHighest) == 1;
            const auto level = dbToGain (value (iLevel));
            const auto overdrive = choice (iOverdrive) == 1;
            const auto width = value (iWidth) * 0.01f;
            for (int i = 0; i < n; i += 32)
            {
                const auto chunk = std::min (32, n - i);
                const auto shiftWave = shiftLfo.at (choice (iShiftWave), 0.0, 0.5f), fadeWave = fadeLfo.at (choice (iFadeWave), 0.0, 0.5f);
                shiftLfo.advance (value (iShiftRate), sampleRate, chunk);
                fadeLfo.advance (value (iFadeRate), sampleRate, chunk);
                const auto shift = value (iShift) * 0.01f + shiftWave * value (iShiftLfo) * 0.01f;   // (in octaves, -1..+1 plus the LFO)
                const auto fade = juce::jlimit (0.0f, 1.0f, value (iFade) * 0.01f + fadeWave * value (iFadeLfo) * 0.005f);
                const auto low = value (iLow), high = std::max (value (iLow) * 2.0f, value (iHigh));
                const auto key = low + high * 3.0f + shift * 1000.0f + res * 17.0f + (float) count * 101.0f + (steep ? 7.0f : 0.0f) + (lowestHp ? 11.0f : 0.0f) + (highestLp ? 13.0f : 0.0f);
                if (key != lastKey)
                {
                    lastKey = key;
                    for (int b = 0; b < count; ++b)
                    {
                        const auto f = juce::jlimit (20.0f, (float) sampleRate * 0.45f,
                                                     low * std::pow (high / low, (float) b / (float) std::max (1, count - 1)) * std::pow (2.0f, shift));
                        const auto q = (double) res * 0.5;
                        if (b == 0 && lowestHp) { bandsL[(size_t) b].setHighPass (sampleRate, f, 0.707); }
                        else if (b == count - 1 && highestLp) { bandsL[(size_t) b].setLowPass (sampleRate, f, 0.707); }
                        else bandsL[(size_t) b].setBandPass (sampleRate, f, q);
                        copyCoefficients (bandsL[(size_t) b], bandsR[(size_t) b]);
                        copyCoefficients (bandsL[(size_t) b], second[(size_t) b]);
                    }
                }
                std::array<float, maxBands> gains;
                for (int b = 0; b < count; ++b)
                    gains[(size_t) b] = (value (iBankA + b) * (1.0f - fade) + value (iBankB + b) * fade) * 0.01f;
                for (int k = i; k < i + chunk; ++k)
                {
                    auto inL = l[k], inR = r[k];
                    if (overdrive) { inL = std::tanh (inL * 3.0f) * 0.5f; inR = std::tanh (inR * 3.0f) * 0.5f; }
                    float outL = 0.0f, outR = 0.0f;
                    for (int b = 0; b < count; ++b)
                    {
                        auto yl = bandsL[(size_t) b].process (inL, 0), yr = bandsR[(size_t) b].process (inR, 1);
                        if (steep) { yl = second[(size_t) b].process (yl, 0); yr = second[(size_t) b].process (yr, 1); }
                        // Width: 0 all centred, 50 rising left to right, 100 alternating left and right
                        const auto pos = width <= 0.5f ? ((float) b / std::max (1, count - 1) * 2.0f - 1.0f) * width * 2.0f
                                                       : ((b % 2 == 0) ? -1.0f : 1.0f) * (width - 0.5f) * 2.0f + ((float) b / std::max (1, count - 1) * 2.0f - 1.0f) * (1.0f - (width - 0.5f) * 2.0f);
                        const auto gl = gains[(size_t) b] * (1.0f - std::max (0.0f, pos)), gr = gains[(size_t) b] * (1.0f + std::min (0.0f, pos));
                        outL += (yl * 0.5f + yr * 0.5f * std::abs (std::min (0.0f, pos))) * gl;
                        outR += (yr * 0.5f + yl * 0.5f * std::max (0.0f, pos)) * gr;
                    }
                    l[k] = outL * level * 1.6f;
                    r[k] = outR * level * 1.6f;
                }
            }
        }
    private:
        std::array<Biquad, maxBands> bandsL, bandsR, second;
        WaveLfo shiftLfo, fadeLfo;
        float lastKey = -1.0f;
        static void copyCoefficients (const Biquad& from, Biquad& to) noexcept
        {
            to.b0 = from.b0; to.b1 = from.b1; to.b2 = from.b2; to.a1 = from.a1; to.a2 = from.a2;
        }
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

namespace
{
    std::vector<ParamInfo> formantParams()
    {
        const juce::String hz (" Hz"), pc (" %"), dB (" dB");
        const std::vector<juce::String> waves { "Saw Down", "Saw Up", "Triangle", "Square", "S&H", "Smooth" };
        std::vector<juce::String> counts;
        for (int b = 5; b <= 20; ++b) counts.push_back (juce::String (b));
        std::vector<ParamInfo> p {
            { "low", "Low Frequency", hz, 20.0f, 2000.0f, 150.0f, 200.0f },
            { "high", "High Frequency", hz, 1000.0f, 18000.0f, 8000.0f, 6000.0f },
            { "shift", "Formant Shift", pc, -100.0f, 100.0f, 0.0f, 0.0f },
            { "bands", "Bands", "", 0.0f, 15.0f, 15.0f, 7.5f, counts },
            { "lowest", "Lowest", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Bandpass", "Highpass" } },
            { "highest", "Highest", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Bandpass", "Lowpass" } },
            { "resonance", "Resonance", pc, 0.0f, 100.0f, 40.0f, 50.0f },
            { "slope", "Slope", "", 0.0f, 1.0f, 0.0f, 0.5f, { "12 dB", "24 dB" } },
            { "fade", "Fade AB", pc, 0.0f, 100.0f, 0.0f, 50.0f },
            { "shiftLfo", "Shift LFO Intensity", pc, 0.0f, 100.0f, 0.0f, 50.0f },
            { "shiftRate", "Shift LFO Rate", hz, 0.02f, 20.0f, 1.0f, 2.0f },
            { "shiftWave", "Shift LFO Wave", "", 0.0f, 5.0f, 2.0f, 2.5f, waves },
            { "fadeLfo", "Fade LFO Intensity", pc, 0.0f, 100.0f, 0.0f, 50.0f },
            { "fadeRate", "Fade LFO Rate", hz, 0.02f, 20.0f, 0.5f, 2.0f },
            { "fadeWave", "Fade LFO Wave", "", 0.0f, 5.0f, 2.0f, 2.5f, waves },
            { "overdrive", "Overdrive", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
            { "level", "Level", dB, -24.0f, 12.0f, 0.0f, 0.0f },
            { "width", "Stereo Width", pc, 0.0f, 100.0f, 50.0f, 50.0f } };
        for (int b = 0; b < 20; ++b)
            p.push_back ({ "a" + juce::String (b + 1), "A " + juce::String (b + 1), pc, 0.0f, 100.0f, b % 4 == 1 ? 90.0f : 50.0f, 50.0f });
        for (int b = 0; b < 20; ++b)
            p.push_back ({ "b" + juce::String (b + 1), "B " + juce::String (b + 1), pc, 0.0f, 100.0f, b % 5 == 3 ? 90.0f : 40.0f, 50.0f });
        return p;
    }
}

const std::vector<StockEffect>& stockFilterEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), dB (" dB");
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    const std::vector<juce::String> modes { "LP 12", "LP 24", "High Pass", "Band Pass", "Notch" };
    static const std::vector<StockEffect> effects {
        { { "autoFilter", "AutoFilter", {
              { "type", "Filter Type", "", 0.0f, 6.0f, 6.0f, 3.0f, { "HP", "BP", "LP", "LP 6", "LP 12", "LP 18", "LP 24" } },
              { "cutoff", "Cutoff", hz, 30.0f, 18000.0f, 800.0f, 1000.0f },
              { "resonance", "Resonance", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "sync", "LFO Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "note", "LFO Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "rate", "LFO Rate", hz, 0.02f, 20.0f, 0.5f, 1.0f },
              { "lfoDepth", "LFO to Cutoff", pc, -100.0f, 100.0f, 40.0f, 0.0f },
              { "envAmount", "Envelope to Cutoff", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "attack", "Attack", ms, 0.5f, 2000.0f, 10.0f, 50.0f },
              { "release", "Release", ms, 10.0f, 5000.0f, 200.0f, 300.0f },
              { "preDist", "Pre Filter Distortion", pc, 0.0f, 100.0f, 0.0f, 30.0f },
              { "mainOut", "Main Out", dB, -24.0f, 12.0f, 0.0f, 0.0f },
              { "phase", "Stereo Phase", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0.0f, 360.0f, 0.0f, 180.0f },
              { "filterOn", "Filter", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "fatness", "Fatness", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "spread", "Spread", " oct", 0.0f, 2.0f, 0.0f, 0.5f },
              { "envOn", "Envelope", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -30.0f, -30.0f },
              { "dynamic", "Dynamic", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "decay", "Decay", ms, 1.0f, 5000.0f, 300.0f, 300.0f },
              { "sustain", "Sustain", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "lfoOn", "LFO", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "syncPhase", "Sync Phase", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0.0f, 360.0f, 0.0f, 180.0f },
              { "retrigger", "Retrigger", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "wave", "Waveform", "", 0.0f, 5.0f, 2.0f, 2.5f, { "Saw Down", "Saw Up", "Triangle", "Pulse", "S&H", "Smooth" } },
              { "pulseWidth", "Pulse Width", pc, 2.0f, 98.0f, 50.0f, 50.0f },
              { "distOn", "Distortion", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "postDist", "Post Filter Distortion", pc, 0.0f, 100.0f, 0.0f, 30.0f },
              { "preMode", "Pre Mode", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Soft", "Hard", "Tube" } },
              { "postMode", "Post Mode", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Soft", "Hard", "Tube" } },
              { "dry", "Dry Signal", pc, 0.0f, 100.0f, 0.0f, 50.0f } }, "Filter" },
          make<AutoFilter> },

        { { "autoWah", "Auto Wah", {
              { "sensitivity", "Sensitivity", dB, 0.0f, 36.0f, 12.0f, 12.0f },
              { "low", "Range Low", hz, 100.0f, 1000.0f, 350.0f, 400.0f },
              { "high", "Range High", hz, 500.0f, 6000.0f, 2500.0f, 2000.0f },
              { "resonance", "Resonance", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "attack", "Attack", ms, 0.5f, 100.0f, 5.0f, 10.0f },
              { "release", "Release", ms, 10.0f, 1000.0f, 120.0f, 150.0f },
              { "mode", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Band Pass", "Low Pass" } },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Filter" },
          make<AutoWah> },

        { { "fuzzWah", "Fuzz-Wah", {
              { "wahOn", "Wah", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "type", "Wah Type", "", 0.0f, 7.0f, 0.0f, 3.5f,
                { "Classic Wah", "Retro Wah", "Modern Wah", "Opto Wah 1", "Opto Wah 2", "Resonant LP", "Resonant HP", "Peak" } },
              { "autoGain", "Auto Gain", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "relQ", "Relative Q", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "depth", "Depth", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "attack", "Attack", ms, 0.5f, 200.0f, 10.0f, 20.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 150.0f, 200.0f },
              { "rangeLow", "Range Low", hz, 100.0f, 1000.0f, 400.0f, 400.0f },
              { "rangeHigh", "Range High", hz, 600.0f, 5000.0f, 2200.0f, 2000.0f },
              { "compOn", "Compressor", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "ratio", "Ratio", ":1", 1.0f, 10.0f, 3.0f, 3.0f },
              { "fuzzOn", "Fuzz", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "fuzzGain", "Fuzz Gain", dB, 0.0f, 48.0f, 20.0f, 20.0f },
              { "tone", "Tone", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "order", "Order", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Wah > Fuzz", "Fuzz > Wah" } },
              { "output", "Output", dB, -24.0f, 12.0f, 0.0f, 0.0f } }, "Filter" },
          make<FuzzWah> },

        { { "spectralGate", "Spectral Gate", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -30.0f, -30.0f },
              { "speed", "Speed", hz, 0.0f, 10.0f, 0.5f, 1.0f },
              { "cfMod", "CF Modulation", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "bwMod", "BW Modulation", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "centre", "Center Freq", hz, 50.0f, 15000.0f, 1500.0f, 1500.0f },
              { "bandwidth", "Bandwidth", hz, 50.0f, 10000.0f, 1500.0f, 1500.0f },
              { "lowLevel", "Low Level", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "super", "Super Energy", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "sub", "Sub Energy", pc, 0.0f, 200.0f, 0.0f, 100.0f },
              { "highLevel", "High Level", pc, 0.0f, 100.0f, 0.0f, 50.0f },
              { "gain", "Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Filter" },
          make<SpectralGate> },

        { { "formantBank", "Formant Filterbank", formantParams(), "Filter" },
          make<FormantFilterbank> },
        { { "stepFilter", "Step Filter", {
              { "s1", "Step 1", pc, 0.0f, 100.0f, 80.0f, 50.0f }, { "s2", "Step 2", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "s3", "Step 3", pc, 0.0f, 100.0f, 50.0f, 50.0f }, { "s4", "Step 4", pc, 0.0f, 100.0f, 10.0f, 50.0f },
              { "s5", "Step 5", pc, 0.0f, 100.0f, 90.0f, 50.0f }, { "s6", "Step 6", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "s7", "Step 7", pc, 0.0f, 100.0f, 60.0f, 50.0f }, { "s8", "Step 8", pc, 0.0f, 100.0f, 15.0f, 50.0f },
              { "note", "Step Length", "", 0.0f, notesMax, 3.0f, 7.5f, notes },
              { "low", "Range Low", hz, 30.0f, 2000.0f, 200.0f, 300.0f },
              { "high", "Range High", hz, 500.0f, 18000.0f, 6000.0f, 4000.0f },
              { "resonance", "Resonance", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "glide", "Glide", ms, 0.0f, 200.0f, 10.0f, 30.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Filter" },
          make<StepFilter> },
    };
    return effects;
}

} // namespace riff
