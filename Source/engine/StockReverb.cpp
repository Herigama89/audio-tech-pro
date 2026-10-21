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
    /**
        Studio Reverb (as Logic's ChromaVerb): fourteen room types, each its own size, density, colour and
        attack behaviour, over an eight-line feedback delay network with early reflections.
          Main:     Attack, Size, Density, Predelay (or synced), Decay (or synced), Freeze, Distance, Dry, Wet
          Damping:  four bands (low shelf, two bells, high shelf) setting each band's decay as a ratio
          Details:  Quality, modulation speed/depth/source/smoothing, early/late mix, width, Mono Maker
          Output:   a six-band EQ on the reverb (cut, shelf, two bells, shelf, cut)
    */
    namespace chroma
    {
        enum Index { roomType, size, decay, predelay, lowCut, highCut, density, mod, width, mix, freeze,
                     attack, distance, dry, predelaySync, predelayNote, decaySync, decayNote,
                     dLsFreq, dLsRatio, dP1Freq, dP1Ratio, dP1Q, dP2Freq, dP2Ratio, dP2Q, dHsFreq, dHsRatio,
                     quality, modSpeed, modSource, smoothing, earlyLate, monoMaker, monoFreq,
                     eqOn, eqHpOn, eqLsOn, eqLsFreq, eqLsGain, eqP1On, eqP1Freq, eqP1Gain, eqP1Q, eqP2On, eqP2Freq, eqP2Gain, eqP2Q,
                     eqHsOn, eqHsFreq, eqHsGain, eqLpOn, view, count };

        /** Each room's character: line lengths, diffusion, early pattern, damping, and whether Attack swells the level or the density. */
        struct Room { float scale, diffusion, early, damp, modAmount; bool volumeAttack; };
        const Room rooms[] {
            { 0.55f, 0.70f, 0.9f, 0.30f, 0.20f, false },   // Room
            { 0.65f, 0.78f, 1.0f, 0.20f, 0.15f, false },   // Chamber
            { 1.60f, 0.62f, 0.6f, 0.45f, 0.30f, false },   // Concert Hall
            { 1.10f, 0.55f, 0.7f, 0.35f, 0.20f, true  },   // Theater
            { 1.30f, 0.40f, 0.4f, 0.20f, 0.50f, false },   // Synth Hall
            { 0.95f, 0.66f, 0.5f, 0.10f, 0.70f, false },   // Digital
            { 0.60f, 0.50f, 0.6f, 0.65f, 0.15f, false },   // Dark Room
            { 0.45f, 0.80f, 1.0f, 0.30f, 0.15f, true  },   // Dense Room
            { 0.95f, 0.75f, 0.5f, 0.25f, 0.35f, true  },   // Smooth Space
            { 1.20f, 0.72f, 0.6f, 0.30f, 0.30f, false },   // Vocal Hall
            { 1.25f, 0.45f, 0.9f, 0.10f, 0.20f, true  },   // Reflective Hall
            { 1.70f, 0.40f, 0.3f, 0.15f, 0.40f, true  },   // Airy
            { 0.90f, 0.60f, 0.8f, 0.20f, 0.90f, true  },   // FX - Strange Room
            { 1.50f, 0.68f, 0.4f, 0.20f, 0.60f, false },   // FX - Bloomy
        };
    }

    class StudioReverb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            const auto scale = rate / 44100.0;
            predelayLine.prepare ((int) (2.3 * rate));
            attackLine.prepare ((int) (0.45 * rate));
            for (auto& d : diffL) d.line.prepare ((int) (400 * scale * 2.2) + 4);
            for (auto& d : diffR) d.line.prepare ((int) (400 * scale * 2.2) + 4);
            for (auto& l : lines)
                l.prepare ((int) (2111 * 2.3 * 1.8 * scale) + 64);
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR, &monoSplit, &modSmooth, &modNoise }) f->reset();
            for (auto& bank : damping) for (auto& f : bank) f.reset();
            for (auto& f : outEq) f.reset();
            state.fill (0.0f);
            lastKey = -1.0f;
            randomTarget = randomValue = 0.0f;
            randomCounter = 0;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            using namespace chroma;
            static const float base[] { 1009, 1123, 1289, 1447, 1597, 1741, 1913, 2111 };
            static const float diffBase[] { 142, 107, 379, 277, 211, 163 };
            static const float earlyTimes[] { 0.0043f, 0.0071f, 0.0109f, 0.0137f, 0.0181f, 0.0223f, 0.0269f, 0.0311f };
            const auto& room = rooms[juce::jlimit (0, 13, choice (roomType))];
            const auto scale = (float) (sampleRate / 44100.0);
            const auto spaceScale = room.scale * (0.45f + value (size) * 0.011f);
            const auto tempo = bpm.load (std::memory_order_relaxed);
            auto decaySeconds = std::max (0.1f, value (decay));
            if (choice (decaySync) == 1)
                decaySeconds = (float) std::max (0.1, noteSeconds (choice (decayNote), tempo));
            const auto preSeconds = choice (predelaySync) == 1 ? (float) noteSeconds (choice (predelayNote), tempo) : value (predelay) * 0.001f;
            const auto pre = juce::jlimit (1.0f, (float) (2.0 * sampleRate), preSeconds * (float) sampleRate);
            const auto quality = juce::jlimit (0, 2, choice (chroma::quality));
            const auto densityAmount = value (density) * 0.01f;
            const auto diffusion = juce::jlimit (0.1f, 0.78f, room.diffusion * (0.55f + densityAmount * 0.6f));
            const auto stages = quality == 2 ? 6 : 4;
            const auto modDepth = value (mod) * 0.01f * 14.0f * scale * (0.4f + room.modAmount);
            const auto modRate = value (modSpeed);
            const auto source = choice (modSource);
            modSmooth.setLowPass (juce::jmap (value (smoothing) * 0.01f, 40.0f, 0.5f), sampleRate);
            modNoise.setLowPass (6.0, sampleRate);
            const auto widthAmount = value (width) * 0.01f;
            const auto dryGain = value (dry) * 0.01f, wetGain = value (mix) * 0.01f;
            const bool frozen = choice (freeze) == 1;
            const auto distanceAmount = value (distance) * 0.01f;
            // (further away: fewer early reflections, more tail, duller)
            const auto earlyMix = value (earlyLate) * 0.01f;
            const auto earlyGain = room.early * (1.0f - distanceAmount * 0.8f) * (1.0f - earlyMix) * 2.0f;
            const auto lateGain = (0.5f + distanceAmount * 0.5f) * (0.2f + earlyMix * 1.6f);
            // (a gentle DC block, and the air's dulling with distance; the cuts themselves are the output EQ's)
            for (auto* f : { &lowCutL, &lowCutR }) f->setLowPass (12.0, sampleRate);
            for (auto* f : { &highCutL, &highCutR }) f->setLowPass (20000.0f * (1.0f - distanceAmount * 0.7f), sampleRate);
            const bool mono = choice (monoMaker) == 1;
            monoSplit.setLowPass (value (monoFreq), sampleRate);
            const auto attackSamples = value (attack) * 0.01f * 0.4f * (float) sampleRate;

            std::array<float, 8> lengths, gains;
            for (size_t i = 0; i < 8; ++i)
            {
                lengths[i] = base[i] * scale * spaceScale;
                gains[i] = frozen ? 1.0f : (float) std::pow (10.0, -3.0 * lengths[i] / (decaySeconds * sampleRate));
            }
            updateFilters (lengths, decaySeconds, frozen);

            for (int i = 0; i < n; ++i)
            {
                const auto inMono = frozen ? 0.0f : 0.5f * (l[i] + r[i]);
                const auto inSide = frozen ? 0.0f : 0.5f * (l[i] - r[i]);
                predelayLine.push (inMono);
                auto x = predelayLine.read (pre);

                // Early reflections: taps off the pre-delay, spread left and right
                float el = 0.0f, er = 0.0f;
                for (int k = 0; k < 8; ++k)
                {
                    const auto tap = predelayLine.read (pre + earlyTimes[k] * spaceScale * (float) sampleRate);
                    const auto g = 0.5f / (1.0f + (float) k * 0.35f);
                    (k % 2 == 0 ? el : er) += tap * g;
                }

                // Attack: the level (or the density) swells in over the attack time
                attackLine.push (x);
                if (attackSamples > 1.0f)
                {
                    float swell = 0.0f;
                    for (int k = 0; k < 6; ++k)
                        swell += attackLine.read (1.0f + attackSamples * (float) k / 5.0f) * (room.volumeAttack ? (float) (k + 1) / 21.0f : 1.0f / 6.0f);
                    x = room.volumeAttack ? swell * 1.6f : x * 0.4f + swell * 0.6f;
                }

                auto xl = x, xr = x * 0.97f + inSide * 0.15f;
                const auto spread = scale * (0.6f + spaceScale * 0.4f);
                for (int d = 0; d < stages; ++d)
                {
                    xl = diffL[(size_t) d].process (xl, diffBase[d] * spread, diffusion);
                    xr = diffR[(size_t) d].process (xr, diffBase[d] * spread * 1.07f, diffusion);
                }

                // Modulation: sine, random (smoothed steps) or noise
                float m;
                if (source == 0) m = lfo.next (modRate, sampleRate);
                else if (source == 1)
                {
                    if (--randomCounter <= 0)
                    {
                        randomCounter = std::max (1, (int) (sampleRate / std::max (0.05f, modRate)));
                        randomTarget = noise.next();
                    }
                    m = modSmooth.lowPass (randomTarget);
                }
                else m = juce::jlimit (-1.0f, 1.0f, modNoise.lowPass (noise.next()) * 6.0f);
                if (source != 1)
                    m = std::tanh (m * (1.0f + value (smoothing) * 0.03f)) / std::tanh (1.0f + value (smoothing) * 0.03f);
                if (quality == 0)
                    m += noise.next() * 0.15f;   // (Low: grainy, noisy modulation)
                m *= modDepth;

                std::array<float, 8> out;
                for (size_t k = 0; k < 8; ++k)
                    out[k] = lines[k].read (lengths[k] + (k == 0 ? m : k == 5 ? -m : k == 3 ? m * 0.5f : 0.0f));
                auto mixed = out;
                hadamard (mixed);
                for (size_t k = 0; k < 8; ++k)
                {
                    auto v = mixed[k] * gains[k];
                    // The damping EQ: each band dies away at its own ratio of the decay
                    if (! frozen)
                    {
                        for (auto& f : damping[k])
                            v = f.process (v, 0);
                        state[k] = v + room.damp * 0.5f * (state[k] - v);
                        v = state[k];
                    }
                    lines[k].push (v + (k < 4 ? xl : xr) * 0.35f);
                }
                auto wl = (out[0] - out[2] + out[4] - out[6]) * 0.5f * lateGain + el * earlyGain;
                auto wr = (out[1] - out[3] + out[5] - out[7]) * 0.5f * lateGain + er * earlyGain;
                wl = highCutL.lowPass (lowCutL.highPass (wl));
                wr = highCutR.lowPass (lowCutR.highPass (wr));
                if (choice (eqOn) == 1)
                    for (size_t f = 0; f < outEq.size(); ++f)
                    {
                        wl = outEq[f].process (wl, 0);
                        wr = outEq[f].process (wr, 1);
                    }
                auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * widthAmount;
                if (mono)
                    side -= monoSplit.lowPass (side);   // (Mono Maker: no stereo below its frequency)
                l[i] = l[i] * dryGain + (mid + side) * wetGain;
                r[i] = r[i] * dryGain + (mid - side) * wetGain;
            }
        }

        /** The output EQ's curve (for the details view). */
        static double outputResponse (const float* v, double freq, double rate)
        {
            std::array<Biquad, 6> f;
            designOutputEq (v, rate, f);
            double db = 0.0;
            for (const auto& b : f) db += b.magnitudeDb (freq, rate);
            return db;
        }

        /** The damping EQ's curve: each frequency's decay ratio, drawn as dB (1.0 at the middle). */
        static double dampingResponse (const float* v, double freq, double rate)
        {
            using namespace chroma;
            std::array<Biquad, 4> f;
            designDamping (v, rate, f, -1.0);
            double db = 0.0;
            for (const auto& b : f) db += b.magnitudeDb (freq, rate);
            return db;
        }

    private:
        static void designOutputEq (const float* v, double rate, std::array<Biquad, 6>& f)
        {
            using namespace chroma;
            const auto on = [v] (int i) { return juce::roundToInt (v[i]) == 1; };
            if (on (eqHpOn)) f[0].setHighPass (rate, v[lowCut] > 10.0f ? v[lowCut] : 20.0, 0.707); else f[0].setIdentity();
            if (on (eqLsOn)) f[1].setLowShelf (rate, v[eqLsFreq], v[eqLsGain]); else f[1].setIdentity();
            if (on (eqP1On)) f[2].setPeak (rate, v[eqP1Freq], v[eqP1Gain], v[eqP1Q]); else f[2].setIdentity();
            if (on (eqP2On)) f[3].setPeak (rate, v[eqP2Freq], v[eqP2Gain], v[eqP2Q]); else f[3].setIdentity();
            if (on (eqHsOn)) f[4].setHighShelf (rate, v[eqHsFreq], v[eqHsGain]); else f[4].setIdentity();
            if (on (eqLpOn)) f[5].setLowPass (rate, v[highCut], 0.707); else f[5].setIdentity();
        }

        /** A ratio as the gain change per pass (or, for drawing, per "unit" pass: dB of a 1 s decay heard over 1/60 s). */
        static double ratioDb (double ratio, double perPassDb)
        {
            // (a band at ratio r decays in r times the time: its loss per pass is 1/r of the middle's,
            //  so the filter adds the difference on top of the line's own loss)
            return perPassDb * (1.0 / juce::jlimit (0.1, 4.0, ratio) - 1.0);
        }

        static void designDamping (const float* v, double rate, std::array<Biquad, 4>& f, double perPassDb)
        {
            using namespace chroma;
            f[0].setLowShelf (rate, v[dLsFreq], ratioDb (v[dLsRatio], perPassDb), 0.7);
            f[1].setPeak (rate, v[dP1Freq], ratioDb (v[dP1Ratio], perPassDb), v[dP1Q]);
            f[2].setPeak (rate, v[dP2Freq], ratioDb (v[dP2Ratio], perPassDb), v[dP2Q]);
            f[3].setHighShelf (rate, v[dHsFreq], ratioDb (v[dHsRatio], perPassDb), 0.7);
        }

        void updateFilters (const std::array<float, 8>& lengths, float decaySeconds, bool frozen)
        {
            using namespace chroma;
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            float key = decaySeconds * 13.0f + lengths[0] + (frozen ? 1.0e6f : 0.0f);
            for (int i = dLsFreq; i <= dHsRatio; ++i) key += v[(size_t) i] * (float) (i + 1) * 0.37f;
            for (int i = eqOn; i <= eqLpOn; ++i) key += v[(size_t) i] * (float) (i + 1) * 0.11f;
            key += v[lowCut] * 0.013f + v[highCut] * 0.0017f;
            if (key == lastKey)
                return;
            lastKey = key;
            for (size_t k = 0; k < 8; ++k)
            {
                // The loss each pass through line k (dB) - the bands scale it by their ratios
                const auto perPass = -60.0 * lengths[k] / (decaySeconds * sampleRate);
                std::array<Biquad, 4> fresh;
                designDamping (v.data(), sampleRate, fresh, perPass);
                for (size_t b = 0; b < 4; ++b)
                {
                    const auto z1 = damping[k][b].z1, z2 = damping[k][b].z2;
                    damping[k][b] = fresh[b];
                    damping[k][b].z1 = z1;
                    damping[k][b].z2 = z2;
                }
            }
            std::array<Biquad, 6> eq;
            designOutputEq (v.data(), sampleRate, eq);
            for (size_t b = 0; b < 6; ++b)
            {
                const auto z1 = outEq[b].z1, z2 = outEq[b].z2;
                outEq[b] = eq[b];
                outEq[b].z1 = z1;
                outEq[b].z2 = z2;
            }
        }

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

        DelayLine predelayLine, attackLine;
        std::array<DiffusionAllPass, 6> diffL, diffR;
        std::array<DelayLine, 8> lines;
        std::array<float, 8> state {};
        std::array<std::array<Biquad, 4>, 8> damping;
        std::array<Biquad, 6> outEq;
        OnePole lowCutL, lowCutR, highCutL, highCutR, monoSplit, modSmooth, modNoise;
        Lfo lfo;
        Noise noise;
        float lastKey = -1.0f, randomTarget = 0.0f, randomValue = 0.0f;
        int randomCounter = 0;
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

    //==============================================================================
    /**
        A general feedback delay network for the simpler reverbs: eight lines through a Hadamard matrix,
        two-band decay (below and above a crossover), a high cut in the loop, input diffusion and an
        optional wobble on two lines.
    */
    struct Fdn
    {
        void prepare (double rate)
        {
            sampleRate = rate;
            const auto scale = rate / 44100.0;
            for (auto& l : lines) l.prepare ((int) (2111 * 3.2 * scale) + 64);
            for (auto& d : diffL) d.line.prepare ((int) (400 * scale * 2.5) + 4);
            for (auto& d : diffR) d.line.prepare ((int) (400 * scale * 2.5) + 4);
            for (auto& f : split) f.reset();
            for (auto& f : damp) f.reset();
        }

        /** size: 0.3 (small) to 3 (huge); density 0..1; decays in seconds; damping (high cut in the loop) in Hz. */
        void set (float size, float density, float lowDecay, float highDecay, float crossover, float dampHz, float modSamples)
        {
            static const float base[] { 1009, 1123, 1289, 1447, 1597, 1741, 1913, 2111 };
            const auto scale = (float) (sampleRate / 44100.0);
            for (size_t i = 0; i < 8; ++i)
            {
                lengths[i] = base[i] * scale * size;
                lowGain[i] = (float) std::pow (10.0, -3.0 * lengths[i] / (std::max (0.05f, lowDecay) * sampleRate));
                highGain[i] = (float) std::pow (10.0, -3.0 * lengths[i] / (std::max (0.05f, highDecay) * sampleRate));
                split[i].setLowPass (crossover, sampleRate);
                damp[i].setLowPass (dampHz, sampleRate);
            }
            diffusion = juce::jlimit (0.0f, 0.75f, density * 0.75f);
            diffScale = scale * (0.5f + size * 0.5f);
            wobble = modSamples;
        }

        void process (float inL, float inR, float& outL, float& outR, float mod = 0.0f) noexcept
        {
            static const float diffBase[] { 142, 107, 379, 277 };
            auto xl = inL, xr = inR;
            for (size_t d = 0; d < 4; ++d)
            {
                xl = diffL[d].process (xl, diffBase[d] * diffScale, diffusion);
                xr = diffR[d].process (xr, diffBase[d] * diffScale * 1.07f, diffusion);
            }
            std::array<float, 8> out;
            for (size_t k = 0; k < 8; ++k)
                out[k] = lines[k].read (lengths[k] + (k == 1 ? mod * wobble : k == 6 ? -mod * wobble : 0.0f));
            auto mixed = out;
            for (size_t h = 1; h < 8; h <<= 1)
                for (size_t i = 0; i < 8; i += h << 1)
                    for (size_t j = i; j < i + h; ++j)
                    {
                        const auto a = mixed[j], b = mixed[j + h];
                        mixed[j] = a + b;
                        mixed[j + h] = a - b;
                    }
            for (size_t k = 0; k < 8; ++k)
            {
                const auto v = mixed[k] * 0.35355339f;
                const auto low = split[k].lowPass (v);
                const auto shaped = low * lowGain[k] + (v - low) * highGain[k];
                lines[k].push (damp[k].lowPass (shaped) + (k < 4 ? xl : xr) * 0.35f);
            }
            outL = (out[0] - out[2] + out[4] - out[6]) * 0.5f;
            outR = (out[1] - out[3] + out[5] - out[7]) * 0.5f;
        }

        double sampleRate = 44100.0;
        std::array<DelayLine, 8> lines;
        std::array<DiffusionAllPass, 4> diffL, diffR;
        std::array<OnePole, 8> split, damp;
        std::array<float, 8> lengths {}, lowGain {}, highGain {};
        float diffusion = 0.5f, diffScale = 1.0f, wobble = 0.0f;
    };

    /** Width of a stereo pair: 0 mono, 100 as is, 200 extra wide. */
    inline void spreadPair (float& l, float& r, float amount) noexcept
    {
        const auto mid = 0.5f * (l + r), side = 0.5f * (l - r) * amount;
        l = mid + side;
        r = mid - side;
    }

    //==============================================================================
    /**
        PlatinumVerb (as Logic's): early reflections from a room of 3 to 7 corners, its size, the stereo
        base of the two microphones and an ER scale; then the tail - initial delay, spread, two bands
        (crossover, low ratio, low level), high cut, density, diffusion, reverb time - and the balance.
    */
    class PlatinumVerb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iPredelay, iShape, iRoomSize, iStereoBase, iErScale, iInitial, iSpread, iCrossover, iLowRatio, iLowLevel,
                     iHighCut, iDensity, iDiffusion, iTime, iBalance, iDry, iWet };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            pre.prepare ((int) (0.25 * rate) + (int) (0.6 * rate));
            fdn.prepare (rate);
            lowSplitL.reset(); lowSplitR.reset(); highCutL.reset(); highCutR.reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto preS = value (iPredelay) * 0.001f * (float) sampleRate;
            const auto corners = juce::jlimit (3, 7, choice (iShape) + 3);
            const auto roomM = value (iRoomSize), baseM = value (iStereoBase), erScale = value (iErScale) * 0.01f;
            const auto initialS = value (iInitial) * 0.001f * (float) sampleRate;
            const auto time = value (iTime);
            const auto lowRatio = value (iLowRatio) * 0.01f;
            fdn.set (juce::jlimit (0.3f, 3.0f, roomM / 20.0f + 0.5f), value (iDensity) * 0.01f, time * lowRatio, time,
                     value (iCrossover), 20000.0f, 6.0f);
            fdn.diffusion = juce::jlimit (0.0f, 0.75f, value (iDiffusion) * 0.0075f);
            for (auto* f : { &lowSplitL, &lowSplitR }) f->setLowPass (value (iCrossover), sampleRate);
            for (auto* f : { &highCutL, &highCutR }) f->setLowPass (value (iHighCut), sampleRate);
            const auto lowLevel = dbToGain (value (iLowLevel));
            const auto spread = value (iSpread) * 0.01f;
            const auto balance = value (iBalance) * 0.01f;   // (-100 early only .. +100 reverb only)
            const auto erGain = std::min (1.0f, 1.0f - balance), revGain = std::min (1.0f, 1.0f + balance);
            const auto dry = value (iDry) * 0.01f, wet = value (iWet) * 0.01f;
            // The reflections: one per wall of the polygon, at its distance (sound at 343 m/s), the two mics apart
            std::array<float, 14> tapTime {}, tapGainL {}, tapGainR {};
            for (int k = 0; k < corners * 2; ++k)
            {
                const auto angle = juce::MathConstants<float>::twoPi * (float) k / (float) (corners * 2);
                const auto distance = roomM * (0.5f + 0.5f * (float) ((k * 7) % corners) / (float) corners) * erScale + 0.3f;
                const auto offset = baseM * 0.5f * std::sin (angle);
                tapTime[(size_t) k] = (distance + offset) / 343.0f * (float) sampleRate;
                const auto g = 0.7f / (1.0f + distance * 0.15f);
                tapGainL[(size_t) k] = g * (0.5f + 0.5f * std::cos (angle));
                tapGainR[(size_t) k] = g * (0.5f - 0.5f * std::cos (angle));
            }

            for (int i = 0; i < n; ++i)
            {
                const auto in = 0.5f * (l[i] + r[i]);
                pre.push (in);
                float el = 0.0f, er = 0.0f;
                for (int k = 0; k < corners * 2; ++k)
                {
                    const auto x = pre.read (preS + 1.0f + tapTime[(size_t) k]);
                    el += x * tapGainL[(size_t) k];
                    er += x * tapGainR[(size_t) k];
                }
                const auto tailIn = pre.read (preS + initialS + 1.0f);
                float tl, tr;
                fdn.process (tailIn + el * 0.3f, tailIn + er * 0.3f, tl, tr, lfo.next (0.3, sampleRate));
                // (the low band's level, then the high cut)
                const auto lowL = lowSplitL.lowPass (tl), lowR = lowSplitR.lowPass (tr);
                tl = highCutL.lowPass (tl - lowL + lowL * lowLevel);
                tr = highCutR.lowPass (tr - lowR + lowR * lowLevel);
                spreadPair (tl, tr, spread);
                const auto wl = el * erGain * 0.6f + tl * revGain, wr = er * erGain * 0.6f + tr * revGain;
                l[i] = l[i] * dry + wl * wet;
                r[i] = r[i] * dry + wr * wet;
            }
        }

    private:
        DelayLine pre;
        Fdn fdn;
        OnePole lowSplitL, lowSplitR, highCutL, highCutR;
        Lfo lfo;
    };

    //==============================================================================
    /**
        EnVerb (as Logic's): a reverb shaped by an envelope - attack, decay to the sustain level, hold, then
        release - started by each new phrase; the dry signal can be delayed; density, spread, high cut,
        a crossover with the low band's level, dry and wet.
    */
    class EnVerb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iAttack, iDecay, iSustain, iHold, iRelease, iDryDelay, iDensity, iSpread, iHighCut, iCrossover, iLowLevel, iDry, iWet };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dryL.prepare ((int) (0.5 * rate) + 8);
            dryR.prepare ((int) (0.5 * rate) + 8);
            fdn.prepare (rate);
            stage = Stage::idle;
            env = level = 0.0f;
            for (auto* f : { &splitL, &splitR, &highCutL, &highCutR }) f->reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto rate = (float) sampleRate;
            const auto attack = std::max (1.0f, value (iAttack) * 0.001f * rate);
            const auto decay = std::max (1.0f, value (iDecay) * 0.001f * rate);
            const auto sustain = value (iSustain) * 0.01f;
            const auto hold = value (iHold) * 0.001f * rate;
            const auto release = std::max (1.0f, value (iRelease) * rate);
            const auto dryDelay = value (iDryDelay) * 0.001f * rate;
            // (a long, even tail underneath: the envelope does the shaping)
            fdn.set (1.4f, value (iDensity) * 0.01f, 30.0f, 30.0f, value (iCrossover), 16000.0f, 4.0f);
            for (auto* f : { &splitL, &splitR }) f->setLowPass (value (iCrossover), sampleRate);
            for (auto* f : { &highCutL, &highCutR }) f->setLowPass (value (iHighCut), sampleRate);
            const auto lowLevel = dbToGain (value (iLowLevel));
            const auto spread = value (iSpread) * 0.01f;
            const auto dry = value (iDry) * 0.01f, wet = value (iWet) * 0.01f;
            const auto follow = coeffMs (5.0, sampleRate), slow = coeffMs (300.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto in = 0.5f * (l[i] + r[i]);
                // A new phrase (the level jumps well above its recent average) starts the envelope again
                const auto a = std::abs (in);
                level = a + follow * (level - a);
                average = level + slow * (average - level);
                if (level > 0.02f && level > average * 2.5f && stage != Stage::attack)
                {
                    stage = Stage::attack;
                    counter = 0.0f;
                }
                switch (stage)
                {
                    case Stage::attack:  env += 1.0f / attack; if (env >= 1.0f) { env = 1.0f; stage = Stage::decay; } break;
                    case Stage::decay:   env -= (1.0f - sustain) / decay; if (env <= sustain) { env = sustain; stage = Stage::hold; counter = 0.0f; } break;
                    case Stage::hold:    if ((counter += 1.0f) >= hold) stage = Stage::release; break;
                    case Stage::release: env -= sustain / release + 1.0e-7f; if (env <= 0.0f) { env = 0.0f; stage = Stage::idle; } break;
                    case Stage::idle:    break;
                }
                float tl, tr;
                fdn.process (in, in, tl, tr, lfo.next (0.25, sampleRate));
                const auto lowL = splitL.lowPass (tl), lowR = splitR.lowPass (tr);
                tl = highCutL.lowPass (tl - lowL + lowL * lowLevel) * env;
                tr = highCutR.lowPass (tr - lowR + lowR * lowLevel) * env;
                spreadPair (tl, tr, spread);
                dryL.push (l[i]);
                dryR.push (r[i]);
                const auto dl = dryDelay >= 1.0f ? dryL.read (dryDelay) : l[i], dr = dryDelay >= 1.0f ? dryR.read (dryDelay) : r[i];
                l[i] = dl * dry + tl * wet;
                r[i] = dr * dry + tr * wet;
            }
        }

    private:
        enum class Stage { idle, attack, decay, hold, release };
        Stage stage = Stage::idle;
        float env = 0.0f, level = 0.0f, average = 0.0f, counter = 0.0f;
        DelayLine dryL, dryR;
        Fdn fdn;
        OnePole splitL, splitR, highCutL, highCutR;
        Lfo lfo;
    };

    //==============================================================================
    /**
        SilverVerb (as Logic's): predelay, reflectivity, size, density/time, low and high cut, and an LFO
        (on/off, rate, left-right phase, intensity) that wobbles the tail; dry and wet.
    */
    class SilverVerb final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iPredelay, iReflectivity, iSize, iDensityTime, iLowCut, iHighCut, iModOn, iRate, iPhase, iIntensity, iDry, iWet };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            pre.prepare ((int) (0.21 * rate) + 8);
            fdn.prepare (rate);
            modL.prepare ((int) (0.03 * rate));
            modR.prepare ((int) (0.03 * rate));
            for (auto* f : { &lowCutL, &lowCutR, &highCutL, &highCutR }) f->reset();
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto preS = std::max (1.0f, value (iPredelay) * 0.001f * (float) sampleRate);
            const auto reflect = value (iReflectivity) * 0.01f;
            const auto size = 0.3f + value (iSize) * 0.02f;
            const auto time = 0.3f + value (iDensityTime) * 0.08f;
            // (reflective walls: brighter, longer; soft ones swallow the highs)
            fdn.set (size, 0.4f + value (iDensityTime) * 0.005f, time, time * (0.3f + reflect * 0.7f), 1500.0f, 3000.0f + reflect * 15000.0f, 0.0f);
            for (auto* f : { &lowCutL, &lowCutR }) f->setLowPass (value (iLowCut), sampleRate);
            for (auto* f : { &highCutL, &highCutR }) f->setLowPass (value (iHighCut), sampleRate);
            const auto modOn = choice (iModOn) == 1;
            const auto depth = value (iIntensity) * 0.01f * 0.004f * (float) sampleRate;
            const auto phase = value (iPhase) / 360.0f;
            const auto dry = value (iDry) * 0.01f, wet = value (iWet) * 0.01f;

            for (int i = 0; i < n; ++i)
            {
                pre.push (0.5f * (l[i] + r[i]));
                const auto x = pre.read (preS);
                float tl, tr;
                fdn.process (x, x, tl, tr);
                tl = highCutL.lowPass (lowCutL.highPass (tl));
                tr = highCutR.lowPass (lowCutR.highPass (tr));
                if (modOn && depth > 0.0f)
                {
                    // The LFO wobbles a short delay on each side, the right's phase set against the left's
                    const auto a = lfo.peek (0.0), b = lfo.peek (phase);
                    lfo.next (value (iRate), sampleRate);
                    modL.push (tl);
                    modR.push (tr);
                    tl = modL.read (2.0f + depth * (0.5f + 0.5f * a));
                    tr = modR.read (2.0f + depth * (0.5f + 0.5f * b));
                }
                l[i] = l[i] * dry + tl * wet;
                r[i] = r[i] * dry + tr * wet;
            }
        }

    private:
        DelayLine pre, modL, modR;
        Fdn fdn;
        OnePole lowCutL, lowCutR, highCutL, highCutR;
        Lfo lfo;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockReverbEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), s (" s"), dB (" dB");
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    static const std::vector<StockEffect> effects {
        { { "studioReverb", "Studio Reverb", {
              { "space", "Room Type", "", 0.0f, 13.0f, 9.0f, 6.5f,
                { "Room", "Chamber", "Concert Hall", "Theater", "Synth Hall", "Digital", "Dark Room", "Dense Room",
                  "Smooth Space", "Vocal Hall", "Reflective Hall", "Airy", "FX - Strange Room", "FX - Bloomy" } },
              { "size", "Size", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "decay", "Decay", s, 0.1f, 20.0f, 2.2f, 2.5f },
              { "predelay", "Predelay", ms, 0.0f, 2000.0f, 15.0f, 60.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 1000.0f, 20.0f, 150.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 20000.0f, 6000.0f },
              { "diffusion", "Density", pc, 0.0f, 100.0f, 75.0f, 50.0f },
              { "mod", "Mod Depth", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "width", "Width", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "mix", "Wet", pc, 0.0f, 100.0f, 25.0f, 50.0f },
              { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "attack", "Attack", pc, 0.0f, 100.0f, 10.0f, 50.0f },
              { "distance", "Distance", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "predelaySync", "Predelay Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "predelayNote", "Predelay Note", "", 0.0f, notesMax, 3.0f, 7.5f, notes },
              { "decaySync", "Decay Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "decayNote", "Decay Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "dLsFreq", "Low Freq", hz, 20.0f, 1000.0f, 200.0f, 200.0f },
              { "dLsRatio", "Low Ratio", "x", 0.1f, 1.9f, 1.0f, 1.0f },
              { "dP1Freq", "Low Mid Freq", hz, 100.0f, 5000.0f, 600.0f, 800.0f },
              { "dP1Ratio", "Low Mid Ratio", "x", 0.1f, 1.9f, 1.0f, 1.0f },
              { "dP1Q", "Low Mid Q", "", 0.1f, 4.0f, 0.7f, 1.0f },
              { "dP2Freq", "High Mid Freq", hz, 500.0f, 15000.0f, 3000.0f, 3000.0f },
              { "dP2Ratio", "High Mid Ratio", "x", 0.1f, 1.9f, 1.0f, 1.0f },
              { "dP2Q", "High Mid Q", "", 0.1f, 4.0f, 0.7f, 1.0f },
              { "dHsFreq", "High Freq", hz, 1000.0f, 20000.0f, 6000.0f, 6000.0f },
              { "dHsRatio", "High Ratio", "x", 0.1f, 1.9f, 0.6f, 1.0f },
              { "quality", "Quality", "", 0.0f, 2.0f, 1.0f, 1.0f, { "Low", "High", "Ultra" } },
              { "modSpeed", "Mod Speed", hz, 0.05f, 10.0f, 0.4f, 1.0f },
              { "modSource", "Mod Source", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Sine", "Random", "Noise" } },
              { "smoothing", "Smoothing", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "earlyLate", "Early/Late Mix", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "monoMaker", "Mono Maker", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "monoFreq", "Mono Maker Freq", hz, 20.0f, 1000.0f, 150.0f, 150.0f },
              { "eqOn", "Output EQ", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "eqHpOn", "EQ Low Cut", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "eqLsOn", "EQ Low Shelf", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "eqLsFreq", "Low Shelf Freq", hz, 20.0f, 2000.0f, 150.0f, 200.0f },
              { "eqLsGain", "Low Shelf Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "eqP1On", "EQ Low Mid", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "eqP1Freq", "Low Mid Freq", hz, 50.0f, 10000.0f, 500.0f, 800.0f },
              { "eqP1Gain", "Low Mid Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "eqP1Q", "Low Mid Q", "", 0.1f, 10.0f, 1.0f, 1.0f },
              { "eqP2On", "EQ High Mid", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "eqP2Freq", "High Mid Freq", hz, 200.0f, 18000.0f, 3000.0f, 3000.0f },
              { "eqP2Gain", "High Mid Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "eqP2Q", "High Mid Q", "", 0.1f, 10.0f, 1.0f, 1.0f },
              { "eqHsOn", "EQ High Shelf", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "eqHsFreq", "High Shelf Freq", hz, 1000.0f, 20000.0f, 8000.0f, 6000.0f },
              { "eqHsGain", "High Shelf Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "eqLpOn", "EQ High Cut", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "view", "View", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Main", "Details" } } }, "Reverb" },
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

        { { "platinumVerb", "PlatinumVerb", {
              { "predelay", "Predelay", ms, 0.0f, 200.0f, 10.0f, 30.0f },
              { "shape", "Room Shape", "", 0.0f, 4.0f, 2.0f, 2.0f, { "3", "4", "5", "6", "7" } },
              { "roomSize", "Room Size", " m", 1.0f, 70.0f, 20.0f, 20.0f },
              { "stereoBase", "Stereo Base", " m", 0.0f, 2.0f, 0.3f, 0.5f },
              { "erScale", "ER Scale", pc, 50.0f, 200.0f, 100.0f, 100.0f },
              { "initial", "Initial Delay", ms, 0.0f, 200.0f, 25.0f, 30.0f },
              { "spread", "Spread", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "crossover", "Crossover", hz, 50.0f, 2000.0f, 600.0f, 400.0f },
              { "lowRatio", "Low Ratio", pc, 20.0f, 200.0f, 100.0f, 100.0f },
              { "lowLevel", "Low Freq Level", " dB", -30.0f, 6.0f, 0.0f, -6.0f },
              { "highCut", "High Cut", hz, 200.0f, 20000.0f, 8000.0f, 6000.0f },
              { "density", "Density", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "diffusion", "Diffusion", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "time", "Reverb Time", s, 0.3f, 20.0f, 2.2f, 2.5f },
              { "balance", "Balance ER/Reverb", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "wet", "Wet", pc, 0.0f, 100.0f, 25.0f, 50.0f } }, "Reverb" },
          make<PlatinumVerb> },

        { { "enVerb", "EnVerb", {
              { "attack", "Attack", ms, 0.0f, 2000.0f, 20.0f, 200.0f },
              { "decay", "Decay", ms, 0.0f, 3000.0f, 300.0f, 400.0f },
              { "sustain", "Sustain", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "hold", "Hold", ms, 0.0f, 10000.0f, 500.0f, 1000.0f },
              { "release", "Release", s, 0.05f, 20.0f, 1.5f, 2.0f },
              { "dryDelay", "Dry Signal Delay", ms, 0.0f, 500.0f, 0.0f, 100.0f },
              { "density", "Density", pc, 0.0f, 100.0f, 70.0f, 50.0f },
              { "spread", "Spread", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "highCut", "High Cut", hz, 200.0f, 20000.0f, 9000.0f, 6000.0f },
              { "crossover", "Crossover", hz, 50.0f, 2000.0f, 500.0f, 400.0f },
              { "lowLevel", "Low Freq Level", " dB", -30.0f, 6.0f, -3.0f, -6.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "wet", "Wet", pc, 0.0f, 100.0f, 30.0f, 50.0f } }, "Reverb" },
          make<EnVerb> },

        { { "silverVerb", "SilverVerb", {
              { "predelay", "Predelay", ms, 0.0f, 200.0f, 10.0f, 30.0f },
              { "reflectivity", "Reflectivity", pc, 0.0f, 100.0f, 60.0f, 50.0f },
              { "size", "Size", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "densityTime", "Density/Time", pc, 0.0f, 100.0f, 40.0f, 50.0f },
              { "lowCut", "Low Cut", hz, 10.0f, 2000.0f, 80.0f, 200.0f },
              { "highCut", "High Cut", hz, 1000.0f, 20000.0f, 9000.0f, 6000.0f },
              { "modOn", "Modulation", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "rate", "Rate", hz, 0.05f, 10.0f, 0.5f, 1.0f },
              { "phase", "Phase", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0.0f, 360.0f, 180.0f, 180.0f },
              { "intensity", "Intensity", pc, 0.0f, 100.0f, 20.0f, 50.0f },
              { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "wet", "Wet", pc, 0.0f, 100.0f, 25.0f, 50.0f } }, "Reverb" },
          make<SilverVerb> },
    };
    return effects;
}

} // namespace riff
