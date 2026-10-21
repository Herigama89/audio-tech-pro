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

    //==============================================================================
    class AutoFilter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; filter.reset(); env = 0.0f; }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto mode = choice (0);
            const auto cutoff = value (1), resonance = 0.5f + value (2) * 0.01f * 11.5f;
            const auto lfoHz = choice (3) == 1 ? 1.0 / noteSeconds (choice (4), bpm.load (std::memory_order_relaxed)) : (double) value (5);
            const auto lfoDepth = value (6) * 0.01f;          // octaves: +-4 at full
            const auto envAmount = value (7) * 0.01f;         // octaves: +-5 at full
            const auto attack = coeffMs (value (8), sampleRate), release = coeffMs (value (9), sampleRate);
            const auto drive = dbToGain (value (10));
            const auto mix = value (11) * 0.01f;
            const auto phase = value (12) / 360.0;

            for (int i = 0; i < n; i += 16)
            {
                // (the cutoff moves every 16 samples: smooth enough, and cheap)
                const auto count = std::min (16, n - i);
                float level = 0.0f;
                for (int k = 0; k < count; ++k)
                    level = std::max (level, std::max (std::abs (l[i + k]), std::abs (r[i + k])));
                env = level + (level > env ? attack : release) * (env - level);
                const auto lfoL = lfo.peek (0.0), lfoR = lfo.peek (phase);
                for (int k = 0; k < count; ++k)
                    lfo.next (lfoHz, sampleRate);
                const auto envOct = envAmount * 5.0f * std::min (1.0f, env * 2.0f);
                const auto fl = cutoff * std::pow (2.0f, lfoL * lfoDepth * 4.0f + envOct);
                const auto fr = cutoff * std::pow (2.0f, lfoR * lfoDepth * 4.0f + envOct);
                filter.first[0].set (fl, resonance, sampleRate);
                filter.first[1].set (fr, resonance, sampleRate);
                filter.second[0].set (fl, 0.707, sampleRate);
                filter.second[1].set (fr, 0.707, sampleRate);
                for (int k = i; k < i + count; ++k)
                {
                    const auto a = filter.process (saturate (l[k] * drive) / std::max (1.0f, drive * 0.5f), 0, mode);
                    const auto b = filter.process (saturate (r[k] * drive) / std::max (1.0f, drive * 0.5f), 1, mode);
                    l[k] = l[k] * (1.0f - mix) + a * mix;
                    r[k] = r[k] * (1.0f - mix) + b * mix;
                }
            }
        }
    private:
        ModeFilter filter;
        Lfo lfo;
        float env = 0.0f;
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

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockFilterEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), dB (" dB");
    const auto& notes = noteNames();
    const auto notesMax = (float) notes.size() - 1.0f;
    const std::vector<juce::String> modes { "LP 12", "LP 24", "High Pass", "Band Pass", "Notch" };
    static const std::vector<StockEffect> effects {
        { { "autoFilter", "Auto Filter", {
              { "mode", "Type", "", 0.0f, 4.0f, 1.0f, 2.0f, modes },
              { "cutoff", "Cutoff", hz, 30.0f, 18000.0f, 800.0f, 1000.0f },
              { "resonance", "Resonance", pc, 0.0f, 100.0f, 30.0f, 50.0f },
              { "sync", "LFO Sync", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Off", "On" } },
              { "note", "LFO Note", "", 0.0f, notesMax, 12.0f, 7.5f, notes },
              { "rate", "LFO Rate", hz, 0.02f, 20.0f, 0.5f, 1.0f },
              { "lfoDepth", "LFO Depth", pc, -100.0f, 100.0f, 40.0f, 0.0f },
              { "envAmount", "Envelope", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "attack", "Env Attack", ms, 0.5f, 200.0f, 10.0f, 20.0f },
              { "release", "Env Release", ms, 10.0f, 2000.0f, 200.0f, 200.0f },
              { "drive", "Drive", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "phase", "Stereo Phase", juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), 0.0f, 180.0f, 0.0f, 90.0f } }, "Filter" },
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
