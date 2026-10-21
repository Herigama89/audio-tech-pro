#include "StockDsp.h"

/*
    Pedalboard (as Logic's): six slots, each holding a stomp box - drives and fuzzes, modulation, wahs,
    delays, a spring, a compressor, a pitch pedal - with its on/off switch and three knobs. Each pedal is
    one of Riff's own effects with its knobs mapped onto the pedal's three; they are built (and swapped)
    on the message thread, so the audio thread only ever runs what's ready.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** A pedal: the effect behind it, which of its parameters the three knobs turn, and settings fixed for it. */
    struct PedalType
    {
        const char* name;
        const char* effect;
        std::array<const char*, 3> knobs;
        std::array<const char*, 3> knobNames;
        std::vector<std::pair<const char*, float>> fixed;
    };

    const std::vector<PedalType>& pedalTypes()
    {
        static const std::vector<PedalType> types {
            { "Fuzz", "fuzz", { "fuzz", "tone", "output" }, { "Fuzz", "Tone", "Level" }, {} },
            { "Octave Fuzz", "fuzz", { "fuzz", "bias", "output" }, { "Fuzz", "Octave", "Level" }, { { "gate", -80.0f } } },
            { "Overdrive", "overdrive", { "drive", "tone", "output" }, { "Drive", "Tone", "Level" }, {} },
            { "Tube Drive", "saturator", { "drive", "mix", "output" }, { "Drive", "Blend", "Level" }, { { "model", 0.0f }, { "style", 1.0f } } },
            { "Distortion", "distortion", { "drive", "tone", "output" }, { "Drive", "Tone", "Level" }, {} },
            { "Grinder", "distortion2", { "drive", "tone", "output" }, { "Grind", "Tone", "Level" }, { { "type", 2.0f } } },
            { "Bit Crusher", "bitcrusher", { "bits", "downsample", "mix" }, { "Bits", "Rate", "Mix" }, {} },
            { "Compressor", "studioComp", { "threshold", "ratio", "makeup" }, { "Sustain", "Ratio", "Level" }, { { "model", 5.0f } } },
            { "Chorus", "chorus", { "rate", "depth", "mix" }, { "Rate", "Depth", "Mix" }, {} },
            { "Flanger", "flanger", { "rate", "depth", "feedback" }, { "Rate", "Depth", "Feedback" }, { { "sync", 0.0f } } },
            { "Phaser", "phaser", { "rate", "feedback", "mix" }, { "Rate", "Feedback", "Mix" }, { { "sync", 0.0f } } },
            { "Microphaser", "microphaser", { "rate", "intensity", "feedback" }, { "Rate", "Depth", "Feedback" }, { { "sync", 0.0f } } },
            { "Tremolo", "tremolo", { "rate", "depth", "smoothing" }, { "Rate", "Depth", "Shape" }, { { "sync", 0.0f } } },
            { "Vibrato", "vibrato", { "rate", "depth", "mix" }, { "Rate", "Depth", "Mix" }, { { "sync", 0.0f } } },
            { "Scanner Vibrato", "scannerVibrato", { "type", "depth", "rateL" }, { "Type", "Depth", "Rate" }, {} },
            { "Rotary", "rotary", { "speed", "accel", "balance" }, { "Speed", "Ramp", "Horn" }, {} },
            { "Auto Wah", "autoWah", { "sensitivity", "resonance", "mix" }, { "Sense", "Q", "Mix" }, {} },
            { "Fuzz Wah", "fuzzWah", { "depth", "relQ", "fuzzGain" }, { "Wah", "Q", "Fuzz" }, { { "fuzzOn", 1.0f } } },
            { "Tape Delay", "tapeDelay", { "time", "feedback", "mix" }, { "Time", "Repeats", "Mix" }, { { "sync", 0.0f } } },
            { "Echo", "echo", { "time", "repeats", "mix" }, { "Time", "Repeats", "Mix" }, { { "sync", 0.0f } } },
            { "Spring Reverb", "springReverb", { "decay", "tone", "mix" }, { "Decay", "Tone", "Mix" }, {} },
            { "Ring Shifter", "ringMod", { "freq", "feedback", "mix" }, { "Freq", "Feedback", "Mix" }, {} },
            { "Pitch", "pitchShifter", { "semitones", "cents", "mix" }, { "Pitch", "Fine", "Mix" }, {} },
        };
        return types;
    }

    constexpr int slots = 6, perSlot = 5;   // (type, on, three knobs)

    class Pedalboard final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        ~Pedalboard() override = default;

        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            block = std::max (16, maxBlock);
            for (auto& s : owned)
                if (s != nullptr) s->prepare (rate, block);
            messageThreadUpdate();
        }

        void messageThreadUpdate() override
        {
            // Build (on this thread) a pedal for any slot whose type changed; the old one is kept until next time
            const auto& types = pedalTypes();
            for (int s = 0; s < slots; ++s)
            {
                const auto type = juce::jlimit (0, (int) types.size() - 1, juce::roundToInt (value (s * perSlot)));
                if (type == builtType[(size_t) s] && owned[(size_t) s] != nullptr)
                    continue;
                auto fresh = BuiltInEffect::create (types[(size_t) type].effect);
                if (fresh == nullptr) continue;
                fresh->prepare (sampleRate, block);
                retired.push_back (std::move (owned[(size_t) s]));
                owned[(size_t) s] = std::move (fresh);
                liveType[(size_t) s].store (type, std::memory_order_relaxed);
                live[(size_t) s].store (owned[(size_t) s].get(), std::memory_order_release);
                builtType[(size_t) s] = type;
            }
            // (anything retired two updates ago is no longer being played)
            while (retired.size() > slots * 2)
                retired.erase (retired.begin());
        }

        void setPosition (juce::int64 at, bool isPlaying, double beatsPerMinute) noexcept override
        {
            BuiltInEffect::setPosition (at, isPlaying, beatsPerMinute);
            for (auto& p : live)
                if (auto* e = p.load (std::memory_order_acquire)) e->setPosition (at, isPlaying, beatsPerMinute);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto& types = pedalTypes();
            const auto mix = value (slots * perSlot) * 0.01f;
            const auto out = dbToGain (value (slots * perSlot + 1));
            for (int i = 0; i < n; ++i)
            {
                dryL[(size_t) (i & 4095)] = l[i];
                dryR[(size_t) (i & 4095)] = r[i];
            }
            for (int s = 0; s < slots; ++s)
            {
                auto* e = live[(size_t) s].load (std::memory_order_acquire);
                if (e == nullptr || choice (s * perSlot + 1) != 1)
                    continue;
                const auto type = liveType[(size_t) s].load (std::memory_order_relaxed);
                if (type < 0 || type >= (int) types.size()) continue;
                const auto& t = types[(size_t) type];
                const auto& params = e->getInfo().params;
                // The pedal's fixed settings, then its three knobs (0..100 % across each parameter's range)
                for (const auto& [id, v] : t.fixed)
                    for (size_t p = 0; p < params.size(); ++p)
                        if (params[p].id == id) e->setParameter ((int) p, v);
                for (int k = 0; k < 3; ++k)
                    for (size_t p = 0; p < params.size(); ++p)
                        if (params[p].id == t.knobs[(size_t) k])
                        {
                            const auto& info = params[p];
                            const auto amount = value (s * perSlot + 2 + k) * 0.01f;
                            // (a frequency or time sweeps logarithmically, as its knob would)
                            const auto logScale = info.min > 0.0f && info.max / info.min > 50.0f;
                            const auto v = logScale ? info.min * std::pow (info.max / info.min, amount) : info.min + (info.max - info.min) * amount;
                            e->setParameter ((int) p, info.isChoice() ? std::round (v) : v);
                        }
                e->process (l, r, n);
            }
            for (int i = 0; i < n; ++i)
            {
                l[i] = (dryL[(size_t) (i & 4095)] * (1.0f - mix) + l[i] * mix) * out;
                r[i] = (dryR[(size_t) (i & 4095)] * (1.0f - mix) + r[i] * mix) * out;
            }
        }

    private:
        std::array<std::unique_ptr<BuiltInEffect>, slots> owned;
        std::array<std::atomic<BuiltInEffect*>, slots> live {};
        std::array<int, slots> builtType { -1, -1, -1, -1, -1, -1 };
        std::array<std::atomic<int>, slots> liveType {};
        std::vector<std::unique_ptr<BuiltInEffect>> retired;
        std::array<float, 4096> dryL {}, dryR {};
        int block = 512;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

/** The pedal names and each one's knob names (for the Pedalboard's window). */
std::vector<juce::String> pedalboardKnobNames (int type)
{
    const auto& types = pedalTypes();
    const auto& t = types[(size_t) juce::jlimit (0, (int) types.size() - 1, type)];
    return { t.knobNames[0], t.knobNames[1], t.knobNames[2] };
}

const std::vector<StockEffect>& stockPedalboardEffects()
{
    static const std::vector<StockEffect> effects = []
    {
        std::vector<juce::String> names;
        for (const auto& t : pedalTypes()) names.push_back (t.name);
        const int defaults[] { 2, 8, 18, 15, 0, 20 };
        std::vector<ParamInfo> p;
        for (int s = 0; s < slots; ++s)
        {
            const auto n = juce::String (s + 1);
            p.push_back ({ "p" + n + "Type", "Pedal " + n, "", 0.0f, (float) names.size() - 1.0f, (float) defaults[s], (float) names.size() * 0.5f, names });
            p.push_back ({ "p" + n + "On", "Pedal " + n + " On", "", 0.0f, 1.0f, s < 3 ? 1.0f : 0.0f, 0.5f, { "Off", "On" } });
            for (int k = 0; k < 3; ++k)
                p.push_back ({ "p" + n + "K" + juce::String (k + 1), "Pedal " + n + " Knob " + juce::String (k + 1), " %", 0.0f, 100.0f, 50.0f, 50.0f });
        }
        p.push_back ({ "mix", "Mix", " %", 0.0f, 100.0f, 100.0f, 50.0f });
        p.push_back ({ "output", "Output", " dB", -24.0f, 12.0f, 0.0f, 0.0f });
        return std::vector<StockEffect> { { { "pedalboard", "Pedalboard", p, "Amps and Pedals" }, make<Pedalboard> } };
    }();
    return effects;
}

} // namespace riff
