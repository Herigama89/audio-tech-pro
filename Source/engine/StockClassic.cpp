#include "StockDsp.h"

/**
    Classic Compressors: hardware-style compressors with faces of their own (a menu section of their own).

    Vintage Opto: an optical levelling amplifier. Its gain reduction is a light cell lit by the signal: the cell
    needs ~10 ms to glow (the soft attack), and lets go in two stages - about half its reduction in ~60 ms, the
    rest over 0.5-5 s, slower the longer and harder it has been working (the cell "remembers"). The curve is gentle
    (about 3:1, program-dependent), firmer in Limit; HF Emphasis lets the highs push the cell more. Then the valve
    output stage, in one of three voices (Clean, Opto, Vari-Mu), a low bump and a texture (warm or bright).
*/
namespace riff
{
namespace
{
    using namespace dsp;

    class VintageOptoClassic final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iGain, iPeak, iLimit, iEmphasis, iMeterMode, iPower, iVoice, iLowBump, iTexture };
        enum Voice { voiceOff, voiceClean, voiceOpto, voiceVariMu };

        void prepare (double rate, int) override
        {
            sampleRate = rate;
            cell = 0.0f; fastPart = slowPart = 0.0f; memory = 0.0f; detector = 0.0f;
            dcL = dcR = 0.0f;
            emphasisFilter.reset(); bumpLow.reset(); bumpDip.reset(); warmLow.reset(); warmHigh.reset(); brightHigh.reset();
            lastKey = -1.0f;
        }

        float takeGainReductionDb() noexcept override { return reductionMeter.exchange (0.0f); }
        float displayValue() const noexcept override { return display.load (std::memory_order_relaxed); }

        int readMeters (float* dest, int capacity) const noexcept override
        {
            // 0: gain reduction (dB), 1-2: input peak (dB), 3-4: output peak (dB)
            if (capacity < 5)
                return 0;
            dest[0] = -grShown.load (std::memory_order_relaxed);
            dest[1] = dest[2] = inPeak.load (std::memory_order_relaxed);
            dest[3] = dest[4] = outPeak.load (std::memory_order_relaxed);
            return 5;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            if (choice (iPower) == 0)
            {
                grShown.store (0.0f, std::memory_order_relaxed);
                return;   // (switched off: the signal passes untouched, the light goes out)
            }
            updateFilters();
            const auto peak = value (iPeak) / 100.0f;      // 0..1: how hard the cell is driven
            const auto gainDb = value (iGain) * 0.4f;     // 0..40 dB of make up, as the hardware's gain
            const auto limit = choice (iLimit) == 1;
            const auto voice = choice (iVoice);
            const auto texture = choice (iTexture);
            const auto bump = value (iLowBump);
            const auto emphasis = value (iEmphasis) > 0.01f;

            // the cell's law: the drive into it (Peak Reduction), its threshold, and how firmly it holds the level
            const auto driveDb = -10.0f + peak * 50.0f;   // the side chain's gain
            const auto ratio = limit ? 12.0f : 3.0f;
            const auto knee = limit ? 4.0f : 10.0f;
            const auto attack = coeffMs (10.0, sampleRate);
            const auto fastRelease = coeffMs (60.0, sampleRate);
            const auto detectorCoeff = coeffMs (5.0, sampleRate);
            const auto makeup = dbToGain (gainDb - 16.0f);

            float inMax = 0.0f, outMax = 0.0f, grMax = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto inL = l[i], inR = r[i];
                inMax = std::max (inMax, std::max (std::abs (inL), std::abs (inR)));
                // The side chain: both channels, the highs pushed by HF Emphasis
                auto sc = 0.5f * (inL + inR);
                if (emphasis)
                    sc = emphasisFilter.process (sc, 0);
                const auto level = sc * sc;
                detector = level + detectorCoeff * (detector - level);
                const auto levelDb = 10.0f * std::log10 (std::max (detector, 1.0e-12f)) + 3.0f + driveDb;

                // the target reduction: a soft knee into the ratio
                float over = levelDb;   // (threshold at 0 dB of drive)
                float target;
                if (over < -knee * 0.5f)      target = 0.0f;
                else if (over > knee * 0.5f)  target = over * (1.0f - 1.0f / ratio);
                else
                {
                    const auto x = over + knee * 0.5f;
                    target = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee);
                }

                // The light cell: lit in ~10 ms; let go in two stages, the slow one slower the more it has worked
                memory = std::min (1.0f, memory + (cell > 1.0f ? cell * 1.5e-6f * (float) (48000.0 / sampleRate) : -4.0e-7f * (float) (48000.0 / sampleRate)));
                memory = std::max (0.0f, memory);
                const auto slowRelease = coeffMs (500.0 + 4500.0 * memory, sampleRate);
                if (target > cell)
                {
                    cell = target + attack * (cell - target);
                    fastPart = slowPart = cell * 0.5f;
                }
                else
                {
                    fastPart = target * 0.5f + fastRelease * (fastPart - target * 0.5f);
                    slowPart = target * 0.5f + slowRelease * (slowPart - target * 0.5f);
                    cell = fastPart + slowPart;
                }
                grMax = std::max (grMax, cell);
                const auto g = dbToGain (-cell) * makeup;
                auto a = inL * g, b = inR * g;

                // The valve output stage
                a = valve (a, voice, dcL);
                b = valve (b, voice, dcR);
                // The low bump: a broad lift in the lows with the small dip above it, as a console's low end
                if (bump > 0.01f)
                {
                    a = bumpDip.process (bumpLow.process (a, 0), 0);
                    b = bumpDip.process (bumpLow.process (b, 1), 1);
                }
                // Texture: warm (weight in the low mids, the very top softened, a little more 2nd harmonic) or
                // bright (air on top, a touch of 3rd harmonic)
                if (texture == 1)
                {
                    a = warmHigh.process (warmLow.process (a, 0), 0);
                    b = warmHigh.process (warmLow.process (b, 1), 1);
                    a += 0.04f * a * a; b += 0.04f * b * b;
                }
                else if (texture == 2)
                {
                    a = brightHigh.process (a, 0);
                    b = brightHigh.process (b, 1);
                    a += 0.03f * (std::tanh (a * 1.5f) / 1.5f - a);
                    b += 0.03f * (std::tanh (b * 1.5f) / 1.5f - b);
                }
                l[i] = a;
                r[i] = b;
                outMax = std::max (outMax, std::max (std::abs (a), std::abs (b)));
            }
            reductionMeter.store (std::max (reductionMeter.load (std::memory_order_relaxed), grMax), std::memory_order_relaxed);
            display.store (grMax, std::memory_order_relaxed);
            grShown.store (grMax, std::memory_order_relaxed);
            inPeak.store (gainToDb (inMax), std::memory_order_relaxed);
            outPeak.store (gainToDb (outMax), std::memory_order_relaxed);
        }

    private:
        /** The valve stage's voice: Clean (very little, open), Opto (warm 2nd harmonic), Vari-Mu (thicker: 2nd and
            3rd, rounder on the peaks). Always on a little, as valves are; more as the level rises. */
        static float valve (float x, int voice, float& dc) noexcept
        {
            float y;
            switch (voice)
            {
                case voiceClean:  y = x + 0.015f * x * x + 0.01f * (std::tanh (x * 1.2f) / 1.2f - x); break;
                case voiceOpto:
                {
                    const auto t = std::tanh (x * 1.2f);
                    y = x + 0.12f * t * t + 0.04f * (std::tanh (x * 1.6f) / 1.6f - x);
                    break;
                }
                case voiceVariMu:
                {
                    const auto t = std::tanh (x * 1.4f);
                    y = x + 0.16f * t * t + 0.18f * (std::tanh (x * 2.0f) / 2.0f - x);
                    break;
                }
                default: return x;
            }
            dc += 0.0005f * (y - dc);   // (even harmonics push a little DC: out, as the transformer would)
            return y - dc;
        }

        void updateFilters() noexcept
        {
            const auto emphasis = value (iEmphasis), bump = value (iLowBump);
            const auto key = emphasis * 7.0f + bump * 131.0f + (float) choice (iTexture) * 977.0f;
            if (std::abs (key - lastKey) < 1.0e-5f)
                return;
            lastKey = key;
            emphasisFilter.setHighShelf (sampleRate, 3000.0, emphasis * 12.0, 0.7);
            bumpLow.setPeak (sampleRate, 85.0, bump, 0.6);
            bumpDip.setPeak (sampleRate, 280.0, -bump * 0.25, 1.0);
            warmLow.setPeak (sampleRate, 220.0, 1.5, 0.7);
            warmHigh.setHighShelf (sampleRate, 9000.0, -1.0, 0.7);
            brightHigh.setHighShelf (sampleRate, 8000.0, 2.0, 0.7);
        }

        double sampleRate = 48000.0;
        float cell = 0.0f, fastPart = 0.0f, slowPart = 0.0f, memory = 0.0f, detector = 0.0f, dcL = 0.0f, dcR = 0.0f, lastKey = -1.0f;
        Biquad emphasisFilter, bumpLow, bumpDip, warmLow, warmHigh, brightHigh;
        std::atomic<float> reductionMeter { 0.0f }, display { 0.0f }, grShown { 0.0f }, inPeak { -100.0f }, outPeak { -100.0f };
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockClassicEffects()
{
    static const std::vector<StockEffect> effects {
        { { "classicOpto", "Vintage Opto", {
              { "gain", "Gain", "", 0.0f, 100.0f, 40.0f, 50.0f },
              { "peak", "Peak Reduction", "", 0.0f, 100.0f, 40.0f, 50.0f },
              { "limit", "Compress/Limit", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Compress", "Limit" } },
              { "emphasis", "HF Emphasis", "", 0.0f, 1.0f, 0.0f, 0.5f },
              { "meterMode", "Meter", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Gain Reduction", "Output +10", "Output +4" } },
              { "power", "Power", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "voice", "Tube Voice", "", 0.0f, 3.0f, 2.0f, 1.5f, { "Off", "Clean", "Opto", "Vari-Mu" } },
              { "lowBump", "Low Bump", " dB", 0.0f, 6.0f, 0.0f, 3.0f },
              { "texture", "Texture", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Off", "Warm", "Bright" } } }, "Classic Compressors" },
          make<VintageOptoClassic> },
    };
    return effects;
}
}
