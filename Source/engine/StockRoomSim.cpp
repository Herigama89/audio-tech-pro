#include "StockDsp.h"

/*
    Room Simulator: Logic's room-simulating reverb with its two models. "Classic" is the early-1980s design
    (Enhance swaps the tail for a dense reflection cluster); "Modern" is its successor (crossovers you set,
    density, mode, correlation patterns). Both: a room size in cubic metres setting the reverb time's range,
    low and high time multipliers, a first reflection, Freeze (with Add and Clear).
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    const std::vector<juce::String> onOff { "Off", "On" };

    class RoomSimulator final : public BuiltInEffect
    {
    public:
        enum P { model, reverbTime, freeze, add, clear, mode, enhance, lowMult, lowXover, highMult, highXover, panel,
                 roomSize, density, reverbDelay, dryLevel, refLevel, reverbLevel,
                 refDelay, refSpread, highCut, bassGain, bassXover, correlation,
                 drySource, refSource, corrL, corrR, subsonic, count };
        static constexpr int lines = 16;

        using BuiltInEffect::BuiltInEffect;

        /** The reverb times a room allows: from its size (the cube root, a length in metres). */
        static std::pair<float, float> timeRange (float cubicMetres) noexcept
        {
            const auto length = std::cbrt (std::max (1.0f, cubicMetres));
            return { 0.02f * length + 0.05f, 4.0f * length };
        }

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto& l : delays) l.prepare ((int) (0.9 * rate));
            for (auto& s : lowSplit) s.reset();
            for (auto& s : highSplit) s.reset();
            for (auto& d : diffusers) d.line.prepare ((int) (0.05 * rate));
            predelay[0].prepare ((int) (0.25 * rate));
            predelay[1].prepare ((int) (0.25 * rate));
            reflection[0].prepare ((int) (0.3 * rate));
            reflection[1].prepare ((int) (0.3 * rate));
            cluster.prepare ((int) (1.2 * rate));
            for (auto& f : outputEq) f.reset();
            subsonicFilter.reset();
            built.fill (-1.0f);
            inLevel = outLevel = 0.0f;
        }

        /** 0/1: in and out levels (dB), 2/3: the reverb time's range for the room size. */
        int readMeters (float* dest, int capacity) const noexcept override
        {
            if (capacity < 4)
                return 0;
            dest[0] = gainToDb (inLevel.load (std::memory_order_relaxed) + 1.0e-9f);
            dest[1] = gainToDb (outLevel.load (std::memory_order_relaxed) + 1.0e-9f);
            const auto range = timeRange (value (roomSize));
            dest[2] = range.first;
            dest[3] = range.second;
            return 4;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < (size_t) count; ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            configure (v);
            const auto frozen = v[freeze] > 0.5f;
            const auto adding = frozen && v[add] > 0.5f;
            if (v[clear] > 0.5f && ! cleared) { for (auto& d : delays) d.clear(); cleared = true; }
            if (v[clear] < 0.5f) cleared = false;
            const auto feedTail = ! frozen || adding;
            const auto dry = dbToGain (v[dryLevel]) * (v[dryLevel] <= -59.9f ? 0.0f : 1.0f);
            const auto refGain = v[refLevel] <= -59.9f ? 0.0f : dbToGain (v[refLevel]);
            const auto tailGain = v[reverbLevel] <= -59.9f ? 0.0f : dbToGain (v[reverbLevel]);
            const auto swapDry = v[drySource] > 0.5f, swapRef = v[refSource] > 0.5f;
            const auto width = v[model] > 0.5f ? v[correlation] * 0.01f : 1.0f;
            const auto useCluster = v[model] < 0.5f && v[enhance] > 0.5f;
            const auto active = activeLines;
            float peakIn = 0.0f, peakOut = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const auto inL = l[i], inR = r[i];
                peakIn = std::max (peakIn, std::max (std::abs (inL), std::abs (inR)));
                auto mono = 0.5f * (inL + inR);
                if (v[subsonic] > 0.5f)
                    mono = subsonicFilter.process (mono, 0);
                // First reflection: the source delayed, the sides apart by the spread
                reflection[0].push (swapRef ? inR : inL);
                reflection[1].push (swapRef ? inL : inR);
                const auto refL = reflection[0].read (refSamples) * refGain;
                const auto refR = reflection[1].read (refSamples + spreadSamples) * refGain;
                // Into the tail, after the reverb delay
                predelay[0].push (mono);
                auto into = predelay[0].read (preSamples);
                for (int d = 0; d < diffuserCount; ++d)
                    into = diffusers[(size_t) d].process (into, diffuserLengths[(size_t) d], diffusion);
                float tailL = 0.0f, tailR = 0.0f;
                if (useCluster)
                {
                    // Enhance: a dense cluster of reflections, as long as the room
                    cluster.push (into);
                    for (size_t t = 0; t < clusterTaps.size(); ++t)
                    {
                        const auto s = cluster.read (clusterTaps[t]) * clusterGains[t];
                        (t % 2 == 0 ? tailL : tailR) += s;
                    }
                }
                else
                {
                    std::array<float, lines> out;
                    float sum = 0.0f;
                    for (int k = 0; k < active; ++k)
                    {
                        out[(size_t) k] = delays[(size_t) k].read (lengths[(size_t) k]);
                        sum += out[(size_t) k];
                    }
                    const auto householder = 2.0f / (float) active;
                    for (int k = 0; k < active; ++k)
                    {
                        // each line's decay set by band: below the low crossover, between, above the high
                        auto y = out[(size_t) k] - householder * sum;
                        const auto low = lowSplit[(size_t) k].lowPass (y);
                        const auto rest = y - low;
                        const auto high = rest - highSplit[(size_t) k].lowPass (rest);
                        const auto mid = rest - high;
                        y = frozen ? out[(size_t) k] - householder * sum
                                   : low * gains[(size_t) k][0] + mid * gains[(size_t) k][1] + high * gains[(size_t) k][2];
                        const auto feed = feedTail ? into * inputSigns[(size_t) k] * 0.5f : 0.0f;
                        delays[(size_t) k].push (y + feed);
                        tailL += out[(size_t) k] * patternL[(size_t) k];
                        tailR += out[(size_t) k] * patternR[(size_t) k];
                    }
                    const auto norm = 1.0f / std::sqrt ((float) active);
                    tailL *= norm;
                    tailR *= norm;
                }
                // The tail's tone and width
                tailL = outputEq[1].process (outputEq[0].process (tailL, 0), 0);
                tailR = outputEq[1].process (outputEq[0].process (tailR, 1), 1);
                const auto mid = 0.5f * (tailL + tailR), side = 0.5f * (tailL - tailR) * width;
                tailL = (mid + side) * tailGain;
                tailR = (mid - side) * tailGain;
                const auto dl = swapDry ? inR : inL, dr = swapDry ? inL : inR;
                l[i] = dl * dry + refL + tailL;
                r[i] = dr * dry + refR + tailR;
                peakOut = std::max (peakOut, std::max (std::abs (l[i]), std::abs (r[i])));
            }
            inLevel.store (std::max (peakIn, inLevel.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
            outLevel.store (std::max (peakOut, outLevel.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
        }

    private:
        void configure (const std::array<float, maxParams>& v) noexcept
        {
            bool same = true;
            for (size_t i = 0; i < (size_t) count; ++i)
                if (i != (size_t) add && i != (size_t) clear && i != (size_t) freeze && v[i] != built[i]) { same = false; break; }
            if (same)
                return;
            built = v;
            const auto modern = v[model] > 0.5f;
            activeLines = ! modern ? 8 : juce::roundToInt (v[mode]) == 0 ? 16 : juce::roundToInt (v[mode]) == 1 ? 8 : 4;
            // The room: line lengths spread between a third and the whole of its mean free path
            const auto size = std::max (1.0f, v[roomSize]);
            const auto length = std::cbrt (size);
            const auto meanPath = 4.0f * size / (6.0f * length * length) / 343.0f;   // (4V/S, in seconds)
            const auto base = juce::jlimit (0.004f, 0.8f, meanPath * 1.6f);
            static constexpr float spread[lines] { 1.0f, 1.13f, 1.27f, 1.39f, 1.51f, 1.67f, 1.79f, 1.93f, 1.07f, 1.21f, 1.33f, 1.47f, 1.59f, 1.73f, 1.87f, 1.97f };
            const auto range = timeRange (size);
            const auto rt = juce::jlimit (range.first, range.second, v[reverbTime]);
            const auto lowRt = rt * v[lowMult], highRt = rt * v[highMult];
            for (int k = 0; k < lines; ++k)
            {
                lengths[(size_t) k] = std::min ((float) (0.85 * sampleRate), (float) (base * spread[k] * 0.55f * sampleRate) + 3.0f);
                const auto seconds = lengths[(size_t) k] / (float) sampleRate;
                const auto decay = [seconds] (float t) { return std::pow (10.0f, -3.0f * seconds / std::max (0.02f, t)); };
                gains[(size_t) k] = { decay (lowRt), decay (rt), decay (highRt) };
                lowSplit[(size_t) k].setLowPass (modern ? v[lowXover] : 500.0, sampleRate);
                highSplit[(size_t) k].setLowPass (modern ? v[highXover] : 1000.0, sampleRate);
                inputSigns[(size_t) k] = (k % 3 == 0) ? -1.0f : 1.0f;
            }
            // Density: diffusion before the tail (above 100 %: denser, and metallic)
            const auto dens = modern ? v[density] * 0.01f : 0.8f;
            diffuserCount = dens < 0.05f ? 0 : 4;
            diffusion = juce::jlimit (0.0f, 0.82f, 0.55f * dens);
            static constexpr float diffuserMs[] { 4.7f, 3.6f, 12.7f, 9.3f };
            for (size_t d = 0; d < 4; ++d)
                diffuserLengths[d] = diffuserMs[d] * 0.001f * (float) sampleRate * juce::jlimit (0.5f, 1.5f, std::sqrt (length / 10.0f));
            // Enhance's cluster: taps across the room's first second, thinning
            juce::Random random (7);
            for (size_t t = 0; t < clusterTaps.size(); ++t)
            {
                const auto at = std::pow (random.nextFloat(), 0.7f) * std::min (1.1f, base * 4.0f) * (float) sampleRate + 8.0f;
                clusterTaps[t] = at;
                clusterGains[t] = (random.nextBool() ? 0.18f : -0.18f) * std::pow (10.0f, -1.5f * at / (float) sampleRate / std::max (0.1f, rt * 0.3f));
            }
            // Correlation patterns: each a set of signs over the lines (the same on both: mono; Off: silent)
            const auto pattern = [&] (int which, std::array<float, lines>& dest)
            {
                if (which == 0) { dest.fill (0.0f); return; }
                juce::Random signs ((juce::int64) which * 31 + 5);
                for (auto& x : dest) x = signs.nextBool() ? 1.0f : -1.0f;
            };
            pattern (modern ? juce::roundToInt (v[corrL]) : 1, patternL);
            pattern (modern ? juce::roundToInt (v[corrR]) : 2, patternR);
            preSamples = std::max (1.0f, v[reverbDelay] * 0.001f * (float) sampleRate);
            refSamples = std::max (1.0f, v[refDelay] * 0.001f * (float) sampleRate);
            spreadSamples = modern ? v[refSpread] * 0.001f * (float) sampleRate : 0.0f;
            if (modern)
            {
                outputEq[0].setLowShelf (sampleRate, v[bassXover], v[bassGain], 0.707);
                outputEq[1].setLowPass (sampleRate, std::min ((double) v[highCut], sampleRate * 0.45), 0.707);
            }
            else
            {
                outputEq[0].setIdentity();
                outputEq[1].setIdentity();
            }
            subsonicFilter.setHighPass (sampleRate, 25.0, 0.707);
        }

        std::array<DelayLine, lines> delays;
        std::array<OnePole, lines> lowSplit, highSplit;
        std::array<float, lines> lengths {}, inputSigns {}, patternL {}, patternR {};
        std::array<std::array<float, 3>, lines> gains {};
        std::array<DiffusionAllPass, 4> diffusers;
        std::array<float, 4> diffuserLengths {};
        int diffuserCount = 4, activeLines = 8;
        float diffusion = 0.5f;
        std::array<DelayLine, 2> predelay, reflection;
        DelayLine cluster;
        std::array<float, 48> clusterTaps {}, clusterGains {};
        std::array<Biquad, 2> outputEq;
        Biquad subsonicFilter;
        float preSamples = 1.0f, refSamples = 1.0f, spreadSamples = 0.0f;
        bool cleared = false;
        std::array<float, maxParams> built {};
        std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockRoomSimEffects()
{
    const juce::String dB (" dB"), ms (" ms"), hz (" Hz");
    std::vector<juce::String> patterns { "Off" };
    for (int k = 1; k <= 8; ++k)
        patterns.push_back ("Pattern " + juce::String (k));
    static const std::vector<StockEffect> effects {
        { { "roomSim", "Room Simulator", {
              { "model", "Model", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Classic", "Modern" } },
              { "reverbTime", "Reverb Time", " s", 0.07f, 400.0f, 2.0f, 4.0f },
              { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "add", "Add", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "clear", "Clear", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "mode", "Mode", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Complex", "Medium", "Simple" } },
              { "enhance", "Enhance", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "lowMult", "Low", "x", 0.1f, 10.0f, 1.2f, 1.0f },
              { "lowXover", "Low Xover", hz, 50.0f, 1000.0f, 300.0f, 300.0f },
              { "highMult", "High", "x", 0.1f, 10.0f, 0.6f, 1.0f },
              { "highXover", "High Xover", hz, 1000.0f, 16000.0f, 3000.0f, 4000.0f },
              { "panel", "Parameters", "", 0.0f, 2.0f, 0.0f, 1.0f, { "Primary", "Secondary", "Extended" } },
              { "roomSize", "Room Size", juce::String::fromUTF8 (" m\xc2\xb3"), 1.0f, 1000000.0f, 2000.0f, 1000.0f },
              { "density", "Reverb Density", " %", 0.0f, 200.0f, 100.0f, 100.0f },
              { "reverbDelay", "Reverb Delay", ms, 0.0f, 200.0f, 20.0f, 30.0f },
              { "dryLevel", "Dry Level", dB, -60.0f, 0.0f, 0.0f, -12.0f },
              { "refLevel", "1st Reflection Level", dB, -60.0f, 0.0f, -12.0f, -12.0f },
              { "reverbLevel", "Reverb Level", dB, -60.0f, 0.0f, -10.0f, -12.0f },
              { "refDelay", "1st Reflection Delay", ms, 0.0f, 200.0f, 12.0f, 20.0f },
              { "refSpread", "1st Reflection Spread", ms, 0.0f, 50.0f, 4.0f, 10.0f },
              { "highCut", "Reverb High Cut", hz, 1000.0f, 20000.0f, 12000.0f, 6000.0f },
              { "bassGain", "Bass Gain", dB, -12.0f, 12.0f, 0.0f, 0.0f },
              { "bassXover", "Bass Crossover", hz, 50.0f, 1000.0f, 200.0f, 200.0f },
              { "correlation", "Reverb Correlation", " %", 0.0f, 150.0f, 100.0f, 100.0f },
              { "drySource", "Dry Source", "", 0.0f, 1.0f, 0.0f, 0.5f, { "L R", "R L" } },
              { "refSource", "1st Reflection Source", "", 0.0f, 1.0f, 0.0f, 0.5f, { "L R", "R L" } },
              { "corrL", "L Correlation Pattern", "", 0.0f, 8.0f, 1.0f, 4.0f, patterns },
              { "corrR", "R Correlation Pattern", "", 0.0f, 8.0f, 2.0f, 4.0f, patterns },
              { "subsonic", "Subsonic", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff } }, "Reverb" },
          make<RoomSimulator> },
    };
    return effects;
}

} // namespace riff
