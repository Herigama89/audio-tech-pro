#include "BandAnalysis.h"
#include "StockDsp.h"

/*
    Metering (Logic's Metering folder) and the Mastering Assistant. The meters pass the sound through
    untouched and publish what they measure for their windows: Level Meter (peak, RMS, hold), Loudness
    Meter (EBU R128: momentary, short-term, integrated, loudness range, true peak), Correlation Meter,
    MultiMeter (third-octave analyzer, goniometer, correlation), Tuner and BPM Counter.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    /** Published readings: written on the audio thread, read by the window. */
    template <size_t N>
    struct Readings
    {
        std::array<std::atomic<float>, N> values {};
        void set (size_t i, float v) noexcept { values[i].store (v, std::memory_order_relaxed); }
        int copy (float* dest, int capacity) const noexcept
        {
            const auto n = std::min ((int) N, capacity);
            for (int i = 0; i < n; ++i)
                dest[i] = values[(size_t) i].load (std::memory_order_relaxed);
            return n;
        }
    };

    /** The peak between two samples (a cubic through four): for true peak readings. */
    inline float interSamplePeak (float y0, float y1, float y2, float y3) noexcept
    {
        float most = std::max (std::abs (y1), std::abs (y2));
        for (const auto t : { 0.25f, 0.5f, 0.75f })
        {
            const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const auto c = -0.5f * y0 + 0.5f * y2;
            most = std::max (most, std::abs (((a * t + b) * t + c) * t + y1));
        }
        return most;
    }

    //==============================================================================
    /**
        Loudness as EBU R128 measures it: K-weighting, 400 ms momentary and 3 s short-term windows (in 100 ms
        blocks), integrated loudness gated at -70 LUFS and 10 LU under, loudness range (10th to 95th percentile
        of short-term, gated 20 LU under), and the true peak (between samples too).
    */
    struct LoudnessEngine
    {
        void prepare (double rate)
        {
            sampleRate = rate;
            for (int c = 0; c < 2; ++c)
            {
                shelf[(size_t) c].setHighShelf (rate, 1681.97, 3.99984, 0.7071752);
                highPass[(size_t) c].setHighPass (rate, 38.13547, 0.5003270);
                shelf[(size_t) c].reset();
                highPass[(size_t) c].reset();
            }
            blockSize = (int) (rate * 0.1);
            reset();
        }
        void reset()
        {
            blocks.fill (0.0);
            blockCount = 0;
            filled = 0;
            accum = 0.0;
            inBlock = 0;
            histogram.fill (0);
            histogramEnergy.fill (0.0);
            shortHistogram.fill (0);
            momentary = shortTerm = integrated = -70.0f;
            range = 0.0f;
            truePeak = 0.0f;
            history.fill (0.0f);
        }

        static int binOf (double lufs) noexcept { return juce::jlimit (0, bins - 1, (int) ((lufs + 70.0) * 10.0)); }

        void push (float l, float r) noexcept
        {
            // True peak (the samples and between them)
            history[0] = history[1]; history[1] = history[2]; history[2] = history[3]; history[3] = l;
            history[4] = history[5]; history[5] = history[6]; history[6] = history[7]; history[7] = r;
            truePeak = std::max ({ truePeak, interSamplePeak (history[0], history[1], history[2], history[3]),
                                   interSamplePeak (history[4], history[5], history[6], history[7]) });

            const auto kl = highPass[0].process (shelf[0].process (l, 0), 0);
            const auto kr = highPass[1].process (shelf[1].process (r, 0), 0);
            accum += (double) kl * kl + (double) kr * kr;
            if (++inBlock < blockSize)
                return;
            // A 100 ms block done
            blocks[(size_t) (blockCount % 30)] = accum / blockSize;
            ++blockCount;
            filled = std::min (30, filled + 1);
            accum = 0.0;
            inBlock = 0;
            const auto mean = [this] (int count)
            {
                double sum = 0.0;
                const auto n = std::min (count, filled);
                for (int k = 0; k < n; ++k)
                    sum += blocks[(size_t) ((blockCount - 1 - k + 3000) % 30)];
                return n > 0 ? sum / n : 0.0;
            };
            const auto toLufs = [] (double energy) { return energy > 1.0e-12 ? -0.691 + 10.0 * std::log10 (energy) : -100.0; };
            const auto m = mean (4), st = mean (30);
            momentary = (float) toLufs (m);
            shortTerm = (float) toLufs (st);
            // Integrated: every 400 ms window (each 100 ms) above -70 LUFS, then again above 10 LU under their mean
            if (filled >= 4 && momentary > -70.0f)
            {
                histogram[(size_t) binOf (momentary)]++;
                histogramEnergy[(size_t) binOf (momentary)] += m;
            }
            double total = 0.0;
            juce::int64 count = 0;
            for (int b = 0; b < bins; ++b) { total += histogramEnergy[(size_t) b]; count += histogram[(size_t) b]; }
            if (count > 0)
            {
                const auto relative = toLufs (total / (double) count) - 10.0;
                double gatedTotal = 0.0;
                juce::int64 gatedCount = 0;
                for (int b = binOf (relative); b < bins; ++b) { gatedTotal += histogramEnergy[(size_t) b]; gatedCount += histogram[(size_t) b]; }
                if (gatedCount > 0)
                    integrated = (float) toLufs (gatedTotal / (double) gatedCount);
            }
            // Loudness range: short-term values (each second), gated 20 LU under the integrated
            if (filled >= 30 && blockCount % 10 == 0 && shortTerm > -70.0f)
                shortHistogram[(size_t) binOf (shortTerm)]++;
            const auto gate = binOf (integrated - 20.0f);
            juce::int64 n = 0;
            for (int b = gate; b < bins; ++b) n += shortHistogram[(size_t) b];
            if (n > 2)
            {
                juce::int64 seen = 0;
                float low = 0.0f, high = 0.0f;
                for (int b = gate; b < bins; ++b)
                {
                    const auto before = seen;
                    seen += shortHistogram[(size_t) b];
                    if (before < n / 10 && seen >= n / 10) low = (float) b / 10.0f - 70.0f;
                    if (before < n * 95 / 100 && seen >= n * 95 / 100) high = (float) b / 10.0f - 70.0f;
                }
                range = std::max (0.0f, high - low);
            }
        }

        static constexpr int bins = 800;
        double sampleRate = 48000.0, accum = 0.0;
        int blockSize = 4800, inBlock = 0, filled = 0;
        juce::int64 blockCount = 0;
        std::array<double, 30> blocks {};
        std::array<juce::int64, bins> histogram {}, shortHistogram {};
        std::array<double, bins> histogramEnergy {};
        std::array<Biquad, 2> shelf, highPass;
        std::array<float, 8> history {};
        float momentary = -70.0f, shortTerm = -70.0f, integrated = -70.0f, range = 0.0f, truePeak = 0.0f;
    };

    //==============================================================================
    /** Level Meter: peak and RMS of each side, with the highest peak held (for the window). */
    class LevelMeter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; rmsL = rmsR = 0.0f; holdL = holdR = 0.0f; }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }
        void process (float* l, float* r, int n) noexcept override
        {
            if (choice (0) == 1 && ! wasReset) { holdL = holdR = 0.0f; }
            wasReset = choice (0) == 1;
            const auto coeff = coeffMs (300.0, sampleRate);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                pl = std::max (pl, std::abs (l[i]));
                pr = std::max (pr, std::abs (r[i]));
                rmsL = l[i] * l[i] + coeff * (rmsL - l[i] * l[i]);
                rmsR = r[i] * r[i] + coeff * (rmsR - r[i] * r[i]);
            }
            holdL = std::max (holdL, pl);
            holdR = std::max (holdR, pr);
            out.set (0, gainToDb (pl)); out.set (1, gainToDb (pr));
            out.set (2, gainToDb (std::sqrt (rmsL))); out.set (3, gainToDb (std::sqrt (rmsR)));
            out.set (4, gainToDb (holdL)); out.set (5, gainToDb (holdR));
        }
    private:
        Readings<6> out;
        float rmsL = 0.0f, rmsR = 0.0f, holdL = 0.0f, holdR = 0.0f;
        bool wasReset = false;
    };

    //==============================================================================
    /** Loudness Meter (as Logic's): M, S and I in LUFS, the loudness range in LU, the true peak; Start/Pause and Reset. */
    class LoudnessMeter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; loudness.prepare (rate); }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }
        void process (float* l, float* r, int n) noexcept override
        {
            // (Reset: a press flips the switch; Pause holds the integrated reading)
            if (choice (1) != lastReset) { lastReset = choice (1); loudness.reset(); }
            const auto running = choice (0) == 1;
            if (running)
                for (int i = 0; i < n; ++i)
                    loudness.push (l[i], r[i]);
            out.set (0, loudness.momentary); out.set (1, loudness.shortTerm); out.set (2, loudness.integrated);
            out.set (3, loudness.range); out.set (4, gainToDb (loudness.truePeak)); out.set (5, running ? 1.0f : 0.0f);
        }
    private:
        LoudnessEngine loudness;
        Readings<6> out;
        int lastReset = 0;
    };

    //==============================================================================
    /** Correlation Meter: +1 in phase (mono), 0 wide, -1 out of phase. */
    class CorrelationMeter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; lr = ll = rr = 0.0; }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto c = (double) coeffMs (300.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                lr = l[i] * r[i] + c * (lr - l[i] * r[i]);
                ll = l[i] * l[i] + c * (ll - l[i] * l[i]);
                rr = r[i] * r[i] + c * (rr - r[i] * r[i]);
            }
            out.set (0, ll * rr > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, lr / std::sqrt (ll * rr)) : 0.0f);
        }
    private:
        double lr = 0.0, ll = 0.0, rr = 0.0;
        Readings<1> out;
    };

    //==============================================================================
    /** MultiMeter (as Logic's): a 31-band third-octave analyzer, a goniometer, correlation and peak levels. */
    class MultiMeter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        static constexpr int bands = 31, points = 256, fftOrder = 13, fftSize = 1 << fftOrder;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            lr = ll = rr = 0.0;
            write = 0;
            ringAt.store (0);
            shown.fill (-120.0f);
            lastRead = 0.0;
        }

        /** The bands are worked out here, when the window reads them: the latest fftSize samples' spectrum,
            integrated over each third octave (equal octaves, as the MM window's). */
        int readMeters (float* dest, int capacity) const noexcept override
        {
            const auto n = out.copy (dest, capacity);
            if (capacity < bands)
                return n;
            std::vector<float> data ((size_t) fftSize * 2, 0.0f);
            const auto at = ringAt.load (std::memory_order_acquire);
            for (int i = 0; i < fftSize; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
                data[(size_t) i] = ring[(size_t) ((at + i) & (fftSize - 1))].load (std::memory_order_relaxed) * w;
            }
            juce::dsp::FFT fft (fftOrder);
            fft.performFrequencyOnlyForwardTransform (data.data(), true);
            const auto norm = 4.0f / ((float) fftSize * (float) fftSize * 0.375f);   // (Hann's power)
            for (int k = 0; k < fftSize / 2; ++k)
                data[(size_t) k] = data[(size_t) k] * data[(size_t) k] * norm;
            std::vector<float> power;
            bands::integrate (data.data(), fftSize / 2, sampleRate / fftSize, bands, power);
            // Ballistics: up at once, down at the return rate (Peak Hold: slower)
            const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const auto dt = lastRead > 0.0 ? juce::jlimit (0.0, 0.5, now - lastRead) : 0.0;
            lastRead = now;
            const auto fall = (float) dt * (value (0) > 0.5f ? 6.0f : 20.0f);
            for (int b = 0; b < bands; ++b)
            {
                const auto db = juce::jmax (-120.0f, 10.0f * std::log10 (power[(size_t) b] + 1.0e-13f));
                auto& s = shown[(size_t) b];
                s = db > s ? db : juce::jmax (db, s - fall);
                dest[b] = s;
            }
            return n;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            const auto c = (double) coeffMs (300.0, sampleRate);
            float pl = 0.0f, pr = 0.0f;
            auto at = ringAt.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) at].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
                at = (at + 1) & (fftSize - 1);
                lr = l[i] * r[i] + c * (lr - l[i] * r[i]);
                ll = l[i] * l[i] + c * (ll - l[i] * l[i]);
                rr = r[i] * r[i] + c * (rr - r[i] * r[i]);
                pl = std::max (pl, std::abs (l[i]));
                pr = std::max (pr, std::abs (r[i]));
                // The goniometer's points: every few samples, side across, mid up
                if ((++counter & 3) == 0)
                {
                    out.set ((size_t) (bands + 3 + write * 2), (l[i] - r[i]) * 0.7071f);
                    out.set ((size_t) (bands + 3 + write * 2 + 1), (l[i] + r[i]) * 0.7071f);
                    write = (write + 1) % points;
                }
            }
            ringAt.store (at, std::memory_order_release);
            out.set (bands, ll * rr > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, lr / std::sqrt (ll * rr)) : 0.0f);
            out.set (bands + 1, gainToDb (pl));
            out.set (bands + 2, gainToDb (pr));
        }
    private:
        std::array<std::atomic<float>, fftSize> ring {};
        std::atomic<int> ringAt { 0 };
        mutable std::array<float, bands> shown {};
        mutable double lastRead = 0.0;
        double lr = 0.0, ll = 0.0, rr = 0.0;
        int write = 0;
        juce::uint32 counter = 0;
        Readings<bands + 3 + points * 2> out;
    };

    //==============================================================================
    /** Tuner (as Logic's): the note played and how far off it is, against the reference pitch. */
    class Tuner final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        static constexpr int window = 1024, decimate = 4;
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; filled = 0; lp.reset(); phase = 0; }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }
        void process (float* l, float* r, int n) noexcept override
        {
            lp.setLowPass (sampleRate / decimate * 0.4, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto x = lp.lowPass (0.5f * (l[i] + r[i]));
                if (++phase < decimate) continue;
                phase = 0;
                buffer[(size_t) filled++] = x;
                if (filled < window) continue;
                analyse();
                // (half the window kept: a new reading every half window)
                std::copy (buffer.begin() + window / 2, buffer.end(), buffer.begin());
                filled = window / 2;
            }
        }
    private:
        void analyse() noexcept
        {
            // YIN: the difference function, cumulative-mean normalised, first dip under 0.15
            const auto rate = sampleRate / decimate;
            const auto maxLag = std::min (window / 2, (int) (rate / 30.0)), minLag = (int) (rate / 1500.0);
            float energy = 0.0f;
            for (int k = 0; k < window; ++k) energy += buffer[(size_t) k] * buffer[(size_t) k];
            if (energy < 1.0e-5f) { out.set (3, 0.0f); return; }
            std::array<float, window / 2 + 1> d {};
            float running = 0.0f;
            int best = -1;
            for (int lag = 1; lag <= maxLag; ++lag)
            {
                float sum = 0.0f;
                for (int k = 0; k < window / 2; ++k)
                {
                    const auto diff = buffer[(size_t) k] - buffer[(size_t) (k + lag)];
                    sum += diff * diff;
                }
                running += sum;
                d[(size_t) lag] = sum * (float) lag / std::max (1.0e-9f, running);
                if (lag > minLag + 1 && best < 0 && d[(size_t) (lag - 1)] < 0.15f && d[(size_t) (lag - 1)] <= d[(size_t) lag])
                    best = lag - 1;
            }
            if (best <= 0) { out.set (3, 0.0f); return; }
            // (a parabola through the dip for a finer period)
            const auto a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const auto shift = (a - c) / (2.0f * (a - 2.0f * b + c) + 1.0e-9f);
            const auto freq = (float) (rate / ((float) best + shift));
            const auto reference = value (0);
            const auto note = 69.0f + 12.0f * std::log2 (freq / reference);
            const auto nearest = std::round (note);
            out.set (0, freq);
            out.set (1, nearest);
            out.set (2, (note - nearest) * 100.0f);
            out.set (3, 1.0f);
        }
        std::array<float, window> buffer {};
        int filled = 0, phase = 0;
        OnePole lp;
        Readings<4> out;
    };

    //==============================================================================
    /** BPM Counter (as Logic's): the tempo found from the onsets of what's playing. */
    class BpmCounter final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        static constexpr int frames = 600;   // 6 s at 100 frames a second
        void prepare (double rate, int) override { sampleRate = preparedRate = rate; frameSize = (int) (rate / 100.0); count = 0; energy = last = 0.0f; written = 0; }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }
        void process (float* l, float* r, int n) noexcept override
        {
            for (int i = 0; i < n; ++i)
            {
                energy += l[i] * l[i] + r[i] * r[i];
                if (++count < frameSize) continue;
                // The onset strength: how much louder this frame is than the last
                const auto level = std::log (1.0f + energy * 1000.0f);
                onsets[(size_t) (written % frames)] = std::max (0.0f, level - last);
                last = level;
                energy = 0.0f;
                count = 0;
                if (++written % 50 == 0 && written >= frames)
                    estimate();
            }
        }
    private:
        void estimate() noexcept
        {
            // Autocorrelation of the onsets over lags for 60..180 BPM; the strongest wins (folded into that range)
            float bestScore = 0.0f;
            int bestLag = 0;
            for (int lag = 33; lag <= 100; ++lag)
            {
                float score = 0.0f;
                for (int k = 0; k + lag < frames; ++k)
                    score += onsets[(size_t) ((written + k) % frames)] * onsets[(size_t) ((written + k + lag) % frames)];
                if (score > bestScore) { bestScore = score; bestLag = lag; }
            }
            if (bestLag > 0)
                out.set (0, 6000.0f / (float) bestLag);
        }
        std::array<float, frames> onsets {};
        int frameSize = 480, count = 0;
        juce::int64 written = 0;
        float energy = 0.0f, last = 0.0f;
        Readings<1> out;
    };

    //==============================================================================
    /**
        Mastering Assistant (as Logic's): listens to the mix (the first 30 seconds played after Reanalyze), then
        sets a corrective EQ (Auto EQ, how much of it), the gain to reach the loudness you choose, and runs the
        mix through a character - Clean, Valve, Punch or Transparent - into a true peak limiter at -1 dBTP;
        Excite adds upper-mid saturation, Width moves the sides (above 150 Hz), a custom EQ (low, mid, high);
        Loudness Compensation plays it back at the original loudness for a fair comparison.
    */
    class MasteringAssistant final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        enum Index { iCharacter, iAutoEq, iLoudness, iLoudComp, iExcite, iWidth, iCustomOn, iCustomLow, iCustomMid, iCustomHigh, iReanalyze, iBypass };
        static constexpr int bands = 8;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            input.prepare (rate);
            output.prepare (rate);
            for (int b = 0; b < bands; ++b)
            {
                analysisBands[(size_t) b].setBandPass (rate, bandFreq (b), 1.4);
                analysisBands[(size_t) b].reset();
            }
            for (auto& f : eq) f.reset();
            for (auto* f : { &exciteL, &exciteR, &sideLow }) f->reset();
            delayL.prepare ((int) (0.003 * rate) + 4);
            delayR.prepare ((int) (0.003 * rate) + 4);
            look = std::max (1, (int) (0.003 * rate) - 2);
            restart();
            lastKey = -1.0f;
            compEnv = 0.0f;
            limiterGain = 1.0f;
        }
        int getLatencySamples() const override { return look; }
        int readMeters (float* dest, int capacity) const noexcept override { return out.copy (dest, capacity); }

        static double bandFreq (int b) noexcept { return 62.5 * std::pow (2.0, b); }   // 62.5 Hz .. 8 kHz

        void process (float* l, float* r, int n) noexcept override
        {
            if (choice (iReanalyze) != lastReanalyze) { lastReanalyze = choice (iReanalyze); restart(); }
            // Listening: band energies and loudness of what comes in (while it plays)
            const auto analysing = analysed < (juce::int64) (30.0 * sampleRate);
            for (int i = 0; i < n; ++i)
            {
                input.push (l[i], r[i]);
                if (analysing && playing.load (std::memory_order_relaxed))
                {
                    const auto m = 0.5f * (l[i] + r[i]);
                    if (std::abs (m) > 1.0e-4f)
                    {
                        for (int b = 0; b < bands; ++b)
                        {
                            const auto y = analysisBands[(size_t) b].process (m, 0);
                            bandEnergy[(size_t) b] += (double) y * y;
                        }
                        ++analysed;
                    }
                }
            }
            if (analysing && analysed > 0)
                measuredLufs = input.integrated;
            updateEq();

            const auto bypass = choice (iBypass) == 1;
            const auto character = choice (iCharacter);
            const auto gainDb = juce::jlimit (-12.0f, 24.0f, value (iLoudness) - measuredLufs);
            const auto gain = dbToGain (bypass ? 0.0f : gainDb);
            const auto compensate = choice (iLoudComp) == 1 && ! bypass ? dbToGain (-gainDb) : 1.0f;
            const auto width = value (iWidth) * 0.01f;
            const auto excite = choice (iExcite) == 1;
            exciteL.setHighPass (sampleRate, 2500.0, 0.7);
            exciteR.setHighPass (sampleRate, 2500.0, 0.7);
            sideLow.setLowPass (sampleRate, 150.0, 0.7);
            const auto ceiling = dbToGain (-1.0f);
            const auto release = coeffMs (80.0, sampleRate);
            const auto attack = coeffMs (0.5, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                auto a = l[i], b = r[i];
                if (! bypass)
                {
                    for (auto& f : eq) { a = f.process (a, 0); b = f.process (b, 1); }
                    a *= gain; b *= gain;
                    // The character
                    switch (character)
                    {
                        case 1:   // Valve: even harmonics, a soft squeeze
                            a = saturate (a * 0.9f + 0.06f * a * a) / 0.9f; b = saturate (b * 0.9f + 0.06f * b * b) / 0.9f; break;
                        case 2:   // Punch: a slow-attack compressor (transients through), a firm low-mid
                        {
                            const auto x = std::max (std::abs (a), std::abs (b));
                            compEnv = x + (x > compEnv ? 0.9995f : 0.99995f) * (compEnv - x);
                            const auto over = std::max (0.0f, gainToDb (compEnv) + 12.0f);
                            const auto g = dbToGain (-over * 0.5f + 1.5f);
                            a *= g; b *= g;
                            break;
                        }
                        case 3:   // Transparent: a tight, fast compressor
                        {
                            const auto x = std::max (std::abs (a), std::abs (b));
                            compEnv = x + (x > compEnv ? 0.99f : 0.9998f) * (compEnv - x);
                            const auto over = std::max (0.0f, gainToDb (compEnv) + 8.0f);
                            const auto g = dbToGain (-over * 0.4f + 1.0f);
                            a *= g; b *= g;
                            break;
                        }
                        default: break;   // Clean
                    }
                    if (excite)
                    {
                        a += 0.15f * saturate (exciteL.process (a, 0) * 3.0f);
                        b += 0.15f * saturate (exciteR.process (b, 1) * 3.0f);
                    }
                    // Width: the sides above 150 Hz
                    const auto mid = 0.5f * (a + b), side = 0.5f * (a - b);
                    const auto sideLows = sideLow.process (side, 0);
                    const auto newSide = sideLows + (side - sideLows) * width;
                    a = mid + newSide; b = mid - newSide;
                    // The true peak limiter, looking ahead
                    delayL.push (a);
                    delayR.push (b);
                    // (true peak: between the samples too, from the last four)
                    histL[0] = histL[1]; histL[1] = histL[2]; histL[2] = histL[3]; histL[3] = a;
                    histR[0] = histR[1]; histR[1] = histR[2]; histR[2] = histR[3]; histR[3] = b;
                    const auto peak = std::max ({ std::abs (a), std::abs (b), interSamplePeak (histL[0], histL[1], histL[2], histL[3]),
                                                  interSamplePeak (histR[0], histR[1], histR[2], histR[3]) }) * 1.12f;
                    const auto need = peak > ceiling ? ceiling / peak : 1.0f;
                    // (the lowest gain needed over the lookahead is held, so the delayed peak meets it fully)
                    if (need <= heldNeed || --holdLeft <= 0) { heldNeed = need; holdLeft = look; }
                    limiterGain = heldNeed < limiterGain ? heldNeed + attack * (limiterGain - heldNeed) : heldNeed + release * (limiterGain - heldNeed);
                    a = juce::jlimit (-ceiling, ceiling, delayL.readInt (look) * limiterGain);
                    b = juce::jlimit (-ceiling, ceiling, delayR.readInt (look) * limiterGain);
                }
                else
                {
                    delayL.push (a);
                    delayR.push (b);
                    a = delayL.readInt (look);
                    b = delayR.readInt (look);
                }
                a *= compensate; b *= compensate;
                output.push (a, b);
                l[i] = a;
                r[i] = b;
            }
            out.set (0, output.shortTerm);
            out.set (1, output.integrated);
            out.set (2, gainToDb (output.truePeak));
            out.set (3, measuredLufs);
            out.set (4, (float) std::min (1.0, (double) analysed / (30.0 * sampleRate)));
            for (int b = 0; b < bands; ++b)
                out.set ((size_t) (5 + b), eqGain[(size_t) b]);
            out.set (13, gainDb);
        }

    private:
        void restart() noexcept
        {
            analysed = 0;
            bandEnergy.fill (0.0);
            measuredLufs = -14.0f;
            input.reset();
            output.reset();
            eqGain.fill (0.0f);
        }

        /** The Auto EQ: each band towards a balanced master's shape (by how much of it), plus the custom EQ. */
        void updateEq() noexcept
        {
            static const float target[bands] { 1.5f, 1.0f, 0.5f, 0.0f, 0.0f, -0.5f, -1.0f, -2.0f };
            const auto amount = value (iAutoEq) * 0.01f;
            if (analysed > (juce::int64) sampleRate)
            {
                std::array<float, bands> db {};
                float mean = 0.0f;
                for (int b = 0; b < bands; ++b)
                {
                    // (per octave: a band twice as wide holds twice the energy of pink noise's - level them)
                    db[(size_t) b] = 10.0f * std::log10 ((float) (bandEnergy[(size_t) b] / (double) analysed) + 1.0e-12f);
                    mean += db[(size_t) b] / bands;
                }
                for (int b = 0; b < bands; ++b)
                    eqGain[(size_t) b] = juce::jlimit (-6.0f, 6.0f, (target[b] - (db[(size_t) b] - mean)) * 0.5f);
            }
            const auto custom = choice (iCustomOn) == 1;
            const auto key = amount * 7.0f + eqGain[0] + eqGain[3] * 3.0f + eqGain[7] * 5.0f + (custom ? value (iCustomLow) * 11.0f + value (iCustomMid) * 13.0f + value (iCustomHigh) * 17.0f : 0.0f);
            if (key == lastKey) return;
            lastKey = key;
            for (int b = 0; b < bands; ++b)
            {
                auto g = eqGain[(size_t) b] * amount;
                if (custom)
                    g += b < 2 ? value (iCustomLow) * (b == 0 ? 1.0f : 0.5f) : b < 6 ? value (iCustomMid) * ((b == 3 || b == 4) ? 1.0f : 0.5f)
                                                                                  : value (iCustomHigh) * (b == 7 ? 1.0f : 0.5f);
                Biquad fresh;
                fresh.setPeak (sampleRate, bandFreq (b), g, 1.0);
                eq[(size_t) b].b0 = fresh.b0; eq[(size_t) b].b1 = fresh.b1; eq[(size_t) b].b2 = fresh.b2;
                eq[(size_t) b].a1 = fresh.a1; eq[(size_t) b].a2 = fresh.a2;
            }
        }

        LoudnessEngine input, output;
        std::array<Biquad, bands> analysisBands, eq;
        std::array<double, bands> bandEnergy {};
        std::array<float, bands> eqGain {};
        Biquad exciteL, exciteR, sideLow;
        DelayLine delayL, delayR;
        juce::int64 analysed = 0;
        float measuredLufs = -14.0f, lastKey = -1.0f, compEnv = 0.0f, limiterGain = 1.0f, heldNeed = 1.0f;
        int holdLeft = 0;
        int lastReanalyze = 0, look = 72;
        std::array<float, 4> histL {}, histR {};
        Readings<14> out;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

const std::vector<StockEffect>& stockMeteringEffects()
{
    const juce::String dB (" dB"), pc (" %");
    const std::vector<juce::String> onOff { "Off", "On" };
    static const std::vector<StockEffect> effects {
        { { "levelMeter", "Level Meter", { { "reset", "Reset Hold", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff } }, "Metering" }, make<LevelMeter> },
        { { "loudnessMeter", "Loudness Meter", {
              { "running", "Start/Pause", "", 0.0f, 1.0f, 1.0f, 0.5f, { "Paused", "Running" } },
              { "reset", "Reset", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "scale", "Scale", "", 0.0f, 1.0f, 0.0f, 0.5f, { "EBU +9", "EBU +18" } } }, "Metering" }, make<LoudnessMeter> },
        { { "correlationMeter", "Correlation Meter", {}, "Metering" }, make<CorrelationMeter> },
        { { "multiMeter", "MultiMeter", {
              { "peakHold", "Peak Hold", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "view", "View", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Analyzer", "Goniometer" } },
              { "tilt", "Tilt", "", 0.0f, 4.0f, 2.0f, 2.0f, { "Off", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" } } }, "Metering" }, make<MultiMeter> },
        { { "tuner", "Tuner", { { "reference", "Reference", " Hz", 415.0f, 466.0f, 440.0f, 440.0f } }, "Metering" }, make<Tuner> },
        { { "bpmCounter", "BPM Counter", {}, "Metering" }, make<BpmCounter> },
        { { "masteringAssistant", "Mastering Assistant", {
              { "character", "Character", "", 0.0f, 3.0f, 0.0f, 1.5f, { "Clean", "Valve", "Punch", "Transparent" } },
              { "autoEq", "Auto EQ", pc, 0.0f, 100.0f, 50.0f, 50.0f },
              { "loudness", "Loudness", " LUFS", -24.0f, -6.0f, -14.0f, -14.0f },
              { "loudComp", "Loudness Compensation", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "excite", "Excite", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "width", "Width", pc, 0.0f, 200.0f, 100.0f, 100.0f },
              { "customOn", "Custom EQ", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "customLow", "Custom Low", dB, -6.0f, 6.0f, 0.0f, 0.0f },
              { "customMid", "Custom Mid", dB, -6.0f, 6.0f, 0.0f, 0.0f },
              { "customHigh", "Custom High", dB, -6.0f, 6.0f, 0.0f, 0.0f },
              { "reanalyze", "Reanalyze", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
              { "bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff } }, "Metering" }, make<MasteringAssistant> },
    };
    return effects;
}

} // namespace riff
