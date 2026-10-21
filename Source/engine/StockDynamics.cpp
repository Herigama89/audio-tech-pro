#include "StockDsp.h"

/*
    The stock dynamics (Logic's Dynamics folder): a compressor with seven circuit models, a three-band
    multiband compressor, de-esser, noise gate, expander, transient shaper and a look-ahead brickwall
    limiter. Every one reports its gain reduction to the strip's meter.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** Base: the strip's gain reduction meter, and the side chain when one is routed in. */
    class DynamicsBase : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        float takeGainReductionDb() noexcept override { return meter.exchange (0.0f); }
        float displayValue() const noexcept override { return display.load (std::memory_order_relaxed); }
        void setSidechain (const float* left, const float* right) noexcept override { keyL = left; keyR = right != nullptr ? right : left; }
        bool acceptsSidechain() const override { return true; }

    protected:
        void report (float reductionDb) noexcept
        {
            peak = std::max (peak, reductionDb);
        }
        void publish() noexcept
        {
            meter.store (std::max (meter.load (std::memory_order_relaxed), peak), std::memory_order_relaxed);
            display.store (peak, std::memory_order_relaxed);
            peak = 0.0f;
        }
        const float* keyL = nullptr;
        const float* keyR = nullptr;

    private:
        std::atomic<float> meter { 0.0f }, display { 0.0f };
        float peak = 0.0f;
    };

    //==============================================================================
    /** The studio compressor: one gain computer, seven circuits' characters (detector, timing, knee, colour). */
    class StudioCompressor final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;
        enum Model { digital, studioVca, studioFet, vintageVca, vintageFet, vintageOpto, variMu };

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            envDb = rms = 0.0f;
            heldDb = 0.0f;
            scFilter.reset();
            lastHpf = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto model = (Model) juce::jlimit (0, 6, choice (0));
            const auto threshold = value (1);
            auto ratio = std::max (1.0f, value (2));
            auto attackMs = value (3), releaseMs = value (4);
            auto knee = value (5) * 18.0f;
            const auto autoGain = choice (7) == 1;
            const auto hpf = value (8);
            const auto mix = value (9) * 0.01f;
            const auto distortion = choice (10);
            const auto output = dbToGain (value (11));
            const auto limit = choice (12) == 1;

            // Each circuit's character
            bool peakDetect = true;
            float colour = 0.0f;
            switch (model)
            {
                case digital:     break;
                case studioVca:   peakDetect = false; colour = 0.02f; break;
                case studioFet:   attackMs *= 0.25f; colour = 0.12f; break;
                case vintageVca:  peakDetect = false; knee = std::max (knee, 10.0f); colour = 0.06f; break;
                case vintageFet:  attackMs *= 0.1f; colour = 0.22f; break;
                case vintageOpto: peakDetect = false; attackMs = std::max (attackMs, 10.0f); knee = std::max (knee, 12.0f); colour = 0.05f; break;
                case variMu:      peakDetect = false; attackMs = std::max (attackMs, 5.0f); knee = std::max (knee, 14.0f); colour = 0.1f; break;
            }
            const auto attack = coeffMs (attackMs, sampleRate);
            const auto rmsCoeff = coeffMs (model == variMu ? 25.0 : 10.0, sampleRate);
            auto makeup = value (6);
            if (autoGain)
                makeup += -0.5f * compressorGainDb (0.0f, threshold, ratio, knee);
            if (hpf != lastHpf)
            {
                if (hpf > 20.0f)
                    scFilter.setHighPass (sampleRate, hpf, 0.707);
                else
                    scFilter.setIdentity();
                lastHpf = hpf;
            }

            for (int i = 0; i < n; ++i)
            {
                const auto inL = l[i], inR = r[i];
                const auto kl = keyL != nullptr ? keyL[i] : inL, kr = keyR != nullptr ? keyR[i] : inR;
                // The key: both channels (linked), through the key filter when it's on
                const auto key = hpf > 20.0f ? scFilter.process (0.5f * (kl + kr), 0) : std::max (std::abs (kl), std::abs (kr));
                float levelDb;
                if (peakDetect)
                    levelDb = gainToDb (std::abs (key));
                else
                {
                    rms = key * key + rmsCoeff * (rms - key * key);
                    levelDb = 10.0f * std::log10 (std::max (rms * 2.0f, 1.0e-12f));
                }

                // Vari-mu: the ratio grows the harder it works
                auto r2 = ratio;
                if (model == variMu)
                    r2 = 1.0f + (ratio - 1.0f) * juce::jlimit (0.2f, 1.0f, (levelDb - threshold) / 15.0f);
                const auto target = -compressorGainDb (levelDb, threshold, r2, knee);

                // Opto: the longer and harder it's been working, the slower it lets go
                auto rel = releaseMs;
                if (model == vintageOpto)
                {
                    heldDb = std::max (heldDb * 0.99995f, envDb);
                    rel = releaseMs * (0.4f + juce::jlimit (0.0f, 4.0f, heldDb * 0.15f));
                }
                const auto coeff = target > envDb ? attack : coeffMs (rel, sampleRate);
                envDb = target + coeff * (envDb - target);
                report (envDb);

                const auto g = dbToGain (makeup - envDb);
                auto a = inL * g, b = inR * g;
                if (colour > 0.0f)
                {
                    // (more colour the harder it's working, as the circuits do)
                    const auto c = colour * (1.0f + envDb * 0.05f);
                    a = a + c * (saturate (a * 1.5f) / 1.5f - a);
                    b = b + c * (saturate (b * 1.5f) / 1.5f - b);
                }
                switch (distortion)
                {
                    case 1: a = saturate (a * 1.4f) / 1.4f * 1.1f; b = saturate (b * 1.4f) / 1.4f * 1.1f; break;
                    case 2: a = std::tanh (a * 2.5f) / 1.5f; b = std::tanh (b * 2.5f) / 1.5f; break;
                    case 3: a = juce::jlimit (-1.0f, 1.0f, a); b = juce::jlimit (-1.0f, 1.0f, b); break;
                    default: break;
                }
                a = inL + mix * (a - inL);
                b = inR + mix * (b - inR);
                a *= output;
                b *= output;
                if (limit)
                {
                    constexpr float ceiling = 0.966f;   // -0.3 dB
                    a = std::abs (a) > ceiling * 0.8f ? std::copysign (ceiling * 0.8f + ceiling * 0.2f * saturate ((std::abs (a) - ceiling * 0.8f) / (ceiling * 0.2f)), a) : a;
                    b = std::abs (b) > ceiling * 0.8f ? std::copysign (ceiling * 0.8f + ceiling * 0.2f * saturate ((std::abs (b) - ceiling * 0.8f) / (ceiling * 0.2f)), b) : b;
                }
                l[i] = a;
                r[i] = b;
            }
            publish();
        }

    private:
        float envDb = 0.0f, rms = 0.0f, heldDb = 0.0f, lastHpf = -1.0f;
        Biquad scFilter;
    };

    //==============================================================================
    /** Three bands, each compressed on its own, split by phase-matched crossovers. */
    class MultibandCompressor final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* c : { &low, &high, &lowAlign })
                c->reset();
            env.fill (0.0f);
            lastX1 = lastX2 = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto x1 = value (0), x2 = std::max (value (1), value (0) * 1.5f);
            if (x1 != lastX1 || x2 != lastX2)
            {
                low.set (x1, sampleRate);
                high.set (x2, sampleRate);
                lowAlign.set (x2, sampleRate);
                lastX1 = x1;
                lastX2 = x2;
            }
            const auto attack = coeffMs (value (8), sampleRate), release = coeffMs (value (9), sampleRate);
            const float thr[3] { value (2), value (4), value (6) }, ratio[3] { value (3), value (5), value (7) };
            const float gain[3] { value (10), value (11), value (12) };
            const auto output = dbToGain (value (13));

            for (int i = 0; i < n; ++i)
            {
                float bandsL[3], bandsR[3], restL, restR;
                low.split (l[i], 0, bandsL[0], restL);
                low.split (r[i], 1, bandsR[0], restR);
                high.split (restL, 0, bandsL[1], bandsL[2]);
                high.split (restR, 1, bandsR[1], bandsR[2]);
                bandsL[0] = lowAlign.allPass (bandsL[0], 0);
                bandsR[0] = lowAlign.allPass (bandsR[0], 1);

                float outL = 0.0f, outR = 0.0f, most = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const auto levelDb = gainToDb (std::max (std::abs (bandsL[b]), std::abs (bandsR[b])));
                    const auto target = -compressorGainDb (levelDb, thr[b], ratio[b], 6.0f);
                    auto& e = env[(size_t) b];
                    e = target + (target > e ? attack : release) * (e - target);
                    most = std::max (most, e);
                    const auto g = dbToGain (gain[b] - e);
                    outL += bandsL[b] * g;
                    outR += bandsR[b] * g;
                }
                report (most);
                l[i] = outL * output;
                r[i] = outR * output;
            }
            publish();
        }

    private:
        Crossover low, high, lowAlign;
        std::array<float, 3> env {};
        float lastX1 = -1.0f, lastX2 = -1.0f;
    };

    //==============================================================================
    /** Turns sibilance down: just the highs (split band) or the whole voice (wideband). */
    class DeEsser final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            split.reset();
            detector.reset();
            env = 0.0f;
            lastFreq = -1.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto freq = value (0);
            if (freq != lastFreq)
            {
                split.set (freq, sampleRate);
                detector.setHighPass (sampleRate, freq * 0.9, 0.8);
                lastFreq = freq;
            }
            const auto threshold = value (1), range = value (2);
            const bool wideband = choice (3) == 1, listen = choice (4) == 1;
            const auto attack = coeffMs (0.5, sampleRate), release = coeffMs (60.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto key = detector.process (0.5f * (l[i] + r[i]), 0);
                const auto levelDb = gainToDb (std::abs (key));
                const auto target = std::min (range, std::max (0.0f, (levelDb - threshold) * 0.8f));
                env = target + (target > env ? attack : release) * (env - target);
                report (env);
                const auto g = dbToGain (-env);

                float lowL, highL, lowR, highR;
                split.split (l[i], 0, lowL, highL);
                split.split (r[i], 1, lowR, highR);
                if (listen)
                {
                    l[i] = highL;
                    r[i] = highR;
                }
                else if (wideband)
                {
                    l[i] = (lowL + highL) * g;
                    r[i] = (lowR + highR) * g;
                }
                else
                {
                    l[i] = lowL + highL * g;
                    r[i] = lowR + highR * g;
                }
            }
            publish();
        }

    private:
        Crossover split;
        Biquad detector;
        float env = 0.0f, lastFreq = -1.0f;
    };

    //==============================================================================
    /** Opens above the threshold, holds, then closes down to the range (with hysteresis, and key filters). */
    class NoiseGate final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            gain = 0.0f;
            holdLeft = 0;
            open = false;
            lastHp = lastLp = -1.0f;
            hp.reset();
            lp.reset();
            env = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = value (0), floor = dbToGain (value (1));
            const auto attack = coeffMs (value (2), sampleRate), release = coeffMs (value (4), sampleRate);
            const auto hold = (int) (value (3) * 0.001 * sampleRate);
            const auto hysteresis = value (5);
            if (value (6) != lastHp) { hp.setHighPass (sampleRate, std::max (20.0f, value (6)), 0.707); lastHp = value (6); }
            if (value (7) != lastLp) { lp.setLowPass (sampleRate, std::min ((float) (sampleRate * 0.45), value (7)), 0.707); lastLp = value (7); }
            const auto envRelease = coeffMs (5.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                const auto key = std::abs (lp.process (hp.process (0.5f * (kl + kr), 0), 0));
                env = key > env ? key : key + envRelease * (env - key);
                const auto levelDb = gainToDb (env);
                if (levelDb > threshold)
                {
                    open = true;
                    holdLeft = hold;
                }
                else if (open && levelDb < threshold - hysteresis)
                {
                    if (holdLeft > 0)
                        --holdLeft;
                    else
                        open = false;
                }
                const auto target = open ? 1.0f : floor;
                gain = target + (target > gain ? attack : release) * (gain - target);
                report (-gainToDb (std::max (gain, 1.0e-5f)));
                l[i] *= gain;
                r[i] *= gain;
            }
            publish();
        }

    private:
        Biquad hp, lp;
        float gain = 0.0f, env = 0.0f, lastHp = -1.0f, lastLp = -1.0f;
        int holdLeft = 0;
        bool open = false;
    };

    //==============================================================================
    /** Downward expansion: quiet parts made quieter, by the ratio, down to the range. */
    class Expander final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            rms = 0.0f;
            envDb = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto threshold = value (0), ratio = std::max (1.0f, value (1));
            const auto attack = coeffMs (value (2), sampleRate), release = coeffMs (value (3), sampleRate);
            const auto knee = value (4), range = -value (5);
            const auto output = dbToGain (value (6));
            const auto rmsCoeff = coeffMs (5.0, sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const auto kl = keyL != nullptr ? keyL[i] : l[i], kr = keyR != nullptr ? keyR[i] : r[i];
                const auto x = 0.5f * (kl + kr);
                rms = x * x + rmsCoeff * (rms - x * x);
                const auto levelDb = 10.0f * std::log10 (std::max (rms * 2.0f, 1.0e-12f));
                auto under = threshold - levelDb;
                if (knee > 0.01f && std::abs (under) < knee * 0.5f)
                    under = (under + knee * 0.5f) * (under + knee * 0.5f) / (2.0f * knee);
                else if (under < 0.0f)
                    under = 0.0f;
                const auto target = std::min (range, under * (ratio - 1.0f));
                // (opening is the attack here: the reduction falls quickly when the sound comes back)
                envDb = target + (target < envDb ? attack : release) * (envDb - target);
                report (envDb);
                const auto g = dbToGain (-envDb) * output;
                l[i] *= g;
                r[i] *= g;
            }
            publish();
        }

    private:
        float rms = 0.0f, envDb = 0.0f;
    };

    //==============================================================================
    /** More or less attack and sustain, whatever the level (two envelope followers, compared). */
    class TransientShaper final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            fast = slow = fastTail = slowTail = 0.0f;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto attackAmount = value (0) * 0.01f, sustainAmount = value (1) * 0.01f;
            const auto quick = coeffMs (0.3, sampleRate), slowAttack = coeffMs (value (2), sampleRate);
            const auto releaseFast = coeffMs (20.0, sampleRate), releaseSlow = coeffMs (value (3), sampleRate);
            const auto output = dbToGain (value (4));

            for (int i = 0; i < n; ++i)
            {
                const auto x = std::max (std::abs (l[i]), std::abs (r[i]));
                // Attack: a fast follower ahead of a slow one is a hit starting
                fast = x + (x > fast ? quick : releaseSlow) * (fast - x);
                slow = x + (x > slow ? slowAttack : releaseSlow) * (slow - x);
                // Sustain: a slow-releasing follower above a fast-releasing one is a tail ringing on
                fastTail = x + (x > fastTail ? quick : releaseFast) * (fastTail - x);
                slowTail = x + (x > slowTail ? quick : releaseSlow) * (slowTail - x);
                const auto hit = std::max (0.0f, gainToDb (fast) - gainToDb (slow));
                const auto tail = std::max (0.0f, gainToDb (slowTail) - gainToDb (fastTail));
                const auto db = juce::jlimit (-24.0f, 18.0f, attackAmount * hit * 1.5f + sustainAmount * tail);
                const auto g = dbToGain (db) * output;
                l[i] *= g;
                r[i] *= g;
            }
        }

    private:
        float fast = 0.0f, slow = 0.0f, fastTail = 0.0f, slowTail = 0.0f;
    };

    //==============================================================================
    /** Looks ahead 2 ms so peaks are caught before they arrive: nothing goes over the ceiling. */
    class BrickwallLimiter final : public DynamicsBase
    {
    public:
        using DynamicsBase::DynamicsBase;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            look = std::max (1, (int) std::round (0.002 * rate));
            delayL.prepare (look + 2);
            delayR.prepare (look + 2);
            window.assign ((size_t) (look + 2), {});
            head = tail = 0;
            counter = 0;
            gain = 1.0f;
        }

        int getLatencySamples() const override { return look; }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto input = dbToGain (value (0)), ceiling = dbToGain (value (1));
            const auto release = coeffMs (value (2), sampleRate);
            const bool warm = choice (3) == 1;
            const auto attack = coeffMs (1000.0 * look / sampleRate / 4.0, sampleRate);
            const auto size = (int) window.size();

            for (int i = 0; i < n; ++i)
            {
                auto a = l[i] * input, b = r[i] * input;
                if (warm)
                {
                    a = saturate (a / ceiling) * ceiling * 1.02f;
                    b = saturate (b / ceiling) * ceiling * 1.02f;
                }
                // The gain this sample needs, and the least needed over the next `look` samples (a sliding minimum)
                const auto peak = std::max (std::abs (a), std::abs (b));
                const auto need = peak > ceiling ? ceiling / peak : 1.0f;
                while (head != tail && window[(size_t) ((tail - 1 + size) % size)].need >= need)
                    tail = (tail - 1 + size) % size;
                window[(size_t) tail] = { counter, need };
                tail = (tail + 1) % size;
                while (window[(size_t) head].index <= counter - look - 1)
                    head = (head + 1) % size;
                const auto target = window[(size_t) head].need;
                ++counter;

                gain = target < gain ? target + attack * (gain - target) : target + release * (gain - target);
                gain = std::min (gain, 1.0f);

                const auto dl = delayL.readInt (look), dr = delayR.readInt (look);
                delayL.push (a);
                delayR.push (b);
                l[i] = juce::jlimit (-ceiling, ceiling, dl * gain);
                r[i] = juce::jlimit (-ceiling, ceiling, dr * gain);
                report (-gainToDb (gain));
            }
            publish();
        }

    private:
        struct Slot { juce::int64 index = 0; float need = 1.0f; };
        DelayLine delayL, delayR;
        std::vector<Slot> window;
        int head = 0, tail = 0, look = 96;
        juce::int64 counter = 0;
        float gain = 1.0f;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockDynamicsEffects()
{
    const juce::String dB (" dB"), ms (" ms"), hz (" Hz"), pc (" %");
    static const std::vector<StockEffect> effects {
        { { "studioComp", "Studio Compressor", {
              { "model", "Circuit", "", 0.0f, 6.0f, 1.0f, 3.0f,
                { "Clean Digital", "Studio VCA", "Studio FET", "Vintage VCA", "Vintage FET", "Vintage Opto", "Vari-Mu" } },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "ratio", "Ratio", ":1", 1.0f, 30.0f, 4.0f, 4.0f },
              { "attack", "Attack", ms, 0.05f, 200.0f, 10.0f, 10.0f },
              { "release", "Release", ms, 5.0f, 3000.0f, 150.0f, 150.0f },
              { "knee", "Knee", "", 0.0f, 1.0f, 0.3f, 0.5f },
              { "makeup", "Make Up", dB, 0.0f, 30.0f, 0.0f, 8.0f },
              { "autoGain", "Auto Gain", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } },
              { "scHpf", "Key Filter", hz, 20.0f, 500.0f, 20.0f, 100.0f },
              { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f },
              { "distortion", "Distortion", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Off", "Soft", "Hard", "Clip" } },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "limiter", "Limiter", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "On" } } }, "Dynamics" },
          make<StudioCompressor> },

        { { "multiband", "Multiband Compressor", {
              { "lowX", "Low/Mid", hz, 40.0f, 1000.0f, 200.0f, 200.0f },
              { "highX", "Mid/High", hz, 1000.0f, 12000.0f, 3000.0f, 3000.0f },
              { "lowThr", "Low Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "lowRatio", "Low Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "midThr", "Mid Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "midRatio", "Mid Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "highThr", "High Thresh", dB, -60.0f, 0.0f, -20.0f, -20.0f },
              { "highRatio", "High Ratio", ":1", 1.0f, 20.0f, 3.0f, 4.0f },
              { "attack", "Attack", ms, 0.1f, 200.0f, 20.0f, 20.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 200.0f, 200.0f },
              { "lowGain", "Low Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "midGain", "Mid Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "highGain", "High Gain", dB, -24.0f, 24.0f, 0.0f, 0.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<MultibandCompressor> },

        { { "deesser", "DeEsser", {
              { "freq", "Frequency", hz, 2000.0f, 12000.0f, 6000.0f, 6000.0f },
              { "threshold", "Threshold", dB, -60.0f, 0.0f, -30.0f, -30.0f },
              { "range", "Reduction", dB, 0.0f, 24.0f, 8.0f, 8.0f },
              { "mode", "Mode", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Split Band", "Wideband" } },
              { "listen", "Listen", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Off", "Sibilance" } } }, "Dynamics" },
          make<DeEsser> },

        { { "noiseGate", "Noise Gate", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "range", "Reduction", dB, -100.0f, 0.0f, -80.0f, -40.0f },
              { "attack", "Attack", ms, 0.01f, 100.0f, 0.5f, 5.0f },
              { "hold", "Hold", ms, 0.0f, 500.0f, 20.0f, 50.0f },
              { "release", "Release", ms, 5.0f, 2000.0f, 100.0f, 150.0f },
              { "hysteresis", "Hysteresis", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "hp", "Key High Cut", hz, 20.0f, 2000.0f, 20.0f, 200.0f },
              { "lp", "Key Low Cut", hz, 500.0f, 20000.0f, 20000.0f, 5000.0f } }, "Dynamics" },
          make<NoiseGate> },

        { { "expander", "Expander", {
              { "threshold", "Threshold", dB, -80.0f, 0.0f, -40.0f, -40.0f },
              { "ratio", "Ratio", ":1", 1.0f, 10.0f, 2.0f, 3.0f },
              { "attack", "Attack", ms, 0.1f, 100.0f, 1.0f, 5.0f },
              { "release", "Release", ms, 10.0f, 2000.0f, 100.0f, 150.0f },
              { "knee", "Knee", dB, 0.0f, 12.0f, 3.0f, 4.0f },
              { "range", "Range", dB, -80.0f, 0.0f, -40.0f, -30.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<Expander> },

        { { "transient", "Transient Shaper", {
              { "attack", "Attack", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "sustain", "Sustain", pc, -100.0f, 100.0f, 0.0f, 0.0f },
              { "speed", "Attack Time", ms, 1.0f, 50.0f, 10.0f, 10.0f },
              { "release", "Release", ms, 20.0f, 1000.0f, 200.0f, 200.0f },
              { "output", "Output", dB, -24.0f, 24.0f, 0.0f, 0.0f } }, "Dynamics" },
          make<TransientShaper> },

        { { "brickwall", "Brickwall Limiter", {
              { "gain", "Gain", dB, 0.0f, 24.0f, 0.0f, 6.0f },
              { "ceiling", "Ceiling", dB, -12.0f, 0.0f, -0.3f, -3.0f },
              { "release", "Release", ms, 1.0f, 1000.0f, 50.0f, 100.0f },
              { "mode", "Character", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Clean", "Warm" } } }, "Dynamics" },
          make<BrickwallLimiter> },
    };
    return effects;
}

} // namespace riff
