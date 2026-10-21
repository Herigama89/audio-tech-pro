#include "StockDsp.h"

/*
    More of Logic's set: a linear-phase EQ (the Channel EQ's curve with no phase shift), a multi-tap
    delay designer, a sub-bass octave generator and a spreader that widens with tiny pitch and time
    differences between the sides.
*/
namespace riff
{
namespace
{
    using namespace riff::dsp;

    //==============================================================================
    /** The Channel EQ's curve as a linear-phase filter (an FIR made from it, run by convolution). */
    class LinearPhaseEq final : public BuiltInEffect,
                                private juce::Timer
    {
    public:
        static constexpr int taps = 4096;
        explicit LinearPhaseEq (const EffectInfo& info) : BuiltInEffect (info) {}
        ~LinearPhaseEq() override { stopTimer(); }

        int getLatencySamples() const override { return taps / 2; }

        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            blockSize = std::max (16, maxBlock);
            convolution.prepare ({ rate, (juce::uint32) blockSize, 2 });
            built.fill (std::numeric_limits<float>::quiet_NaN());
            rebuild();
            startTimerHz (8);
        }

        void process (float* l, float* r, int n) noexcept override
        {
            for (int at = 0; at < n; at += blockSize)
            {
                const auto count = std::min (blockSize, n - at);
                float* channels[] { l + at, r + at };
                juce::dsp::AudioBlock<float> block (channels, 2, (size_t) count);
                convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            }
        }

    private:
        void timerCallback() override { rebuild(); }

        void rebuild()
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load();
            if (v == built || sampleRate <= 0.0 || info.response == nullptr)
                return;
            built = v;
            // The curve sampled at each FFT bin, zero phase; back to time, centred and windowed
            juce::dsp::FFT fft (12);
            std::vector<float> data ((size_t) taps * 2, 0.0f);
            for (int b = 0; b <= taps / 2; ++b)
            {
                const auto f = std::max (5.0, b * sampleRate / taps);
                const auto gain = (float) std::pow (10.0, info.response (v.data(), f, sampleRate) / 20.0);
                data[(size_t) (2 * b)] = gain;
                data[(size_t) (2 * b + 1)] = 0.0f;
            }
            for (int b = taps / 2 + 1; b < taps; ++b)
            {
                data[(size_t) (2 * b)] = data[(size_t) (2 * (taps - b))];
                data[(size_t) (2 * b + 1)] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (data.data());
            juce::AudioBuffer<float> ir (1, taps);
            for (int i = 0; i < taps; ++i)
            {
                const auto source = (i + taps / 2) % taps;   // (centred: the delay of half the length)
                const auto w = 0.42f - 0.5f * std::cos (twoPi * i / (taps - 1)) + 0.08f * std::cos (2.0f * twoPi * i / (taps - 1));
                ir.setSample (0, i, data[(size_t) source] * w);
            }
            convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        }

        juce::dsp::Convolution convolution;
        int blockSize = 512;
        std::array<float, maxParams> built {};
    };

    //==============================================================================
    /** Match EQ: learns the average spectrum of a reference (the template, played or from a file) and of the
        material, and makes a filter that turns the one into the other, by an Apply amount, smoothed. */
    namespace match
    {
        constexpr int bands = 31;                    // third octaves, 20 Hz to 20 kHz
        constexpr int globals = 10, perUser = 4, users = 4;
        constexpr int templateAt = globals + users * perUser, currentAt = templateAt + bands;
        enum Global { apply, smoothing, phase, analyzer, learnT, learnC, clearT, clearC, output, templateGen };
        constexpr float empty = -120.0f;

        inline double bandFrequency (int k) noexcept { return 1000.0 * std::pow (2.0, (k - 17) / 3.0); }

        /** The matching curve, dB per band: template minus current, each about its own average, smoothed. */
        inline std::array<double, bands> curve (const float* v)
        {
            std::array<double, bands> d {};
            double meanT = 0.0, meanC = 0.0;
            int valid = 0;
            for (int k = 0; k < bands; ++k)
                if (v[templateAt + k] > empty + 1.0f && v[currentAt + k] > empty + 1.0f)
                {
                    meanT += v[templateAt + k];
                    meanC += v[currentAt + k];
                    ++valid;
                }
            if (valid < 3)
                return d;
            meanT /= valid;
            meanC /= valid;
            std::array<double, bands> raw {};
            std::array<bool, bands> has {};
            for (int k = 0; k < bands; ++k)
                if ((has[(size_t) k] = v[templateAt + k] > empty + 1.0f && v[currentAt + k] > empty + 1.0f))
                    raw[(size_t) k] = (v[templateAt + k] - meanT) - (v[currentAt + k] - meanC);
            const auto sigma = v[smoothing] * 0.01 * 3.0;
            for (int k = 0; k < bands; ++k)
            {
                double sum = 0.0, weights = 0.0;
                for (int j = 0; j < bands; ++j)
                {
                    if (! has[(size_t) j])
                        continue;
                    const auto w = sigma < 0.05 ? (j == k ? 1.0 : 0.0) : std::exp (-0.5 * juce::square ((j - k) / sigma));
                    sum += raw[(size_t) j] * w;
                    weights += w;
                }
                d[(size_t) k] = juce::jlimit (-30.0, 30.0, weights > 0.0 ? sum / weights : 0.0) * v[apply] * 0.01;
            }
            return d;
        }

        inline double response (const float* v, double freq, double rate)
        {
            const auto d = curve (v);
            const auto at = juce::jlimit (0.0, (double) bands - 1.0, 3.0 * std::log2 (std::max (1.0, freq) / 1000.0) + 17.0);
            const auto k = std::min (bands - 2, (int) at);
            auto db = d[(size_t) k] + (at - k) * (d[(size_t) k + 1] - d[(size_t) k]);
            for (int u = 0; u < users; ++u)
            {
                const auto* b = v + globals + u * perUser;
                if (b[3] < 0.5f || std::abs (b[1]) < 0.01f)
                    continue;
                Biquad peak;
                peak.setPeak (rate, b[0], b[1], b[2]);
                db += peak.magnitudeDb (freq, rate);
            }
            return db + v[output];
        }

        /** Third-octave band levels, accumulated while learning. */
        struct Bank
        {
            std::array<Biquad, bands> filters;
            std::array<double, bands> energy {};
            double samples = 0.0;

            void prepare (double rate)
            {
                for (int k = 0; k < bands; ++k)
                    filters[(size_t) k].setBandPass (rate, std::min (bandFrequency (k), rate * 0.45), 4.32);
                reset();
            }
            void reset() noexcept
            {
                for (auto& f : filters) f.reset();
                energy.fill (0.0);
                samples = 0.0;
            }
            void add (float x) noexcept
            {
                for (size_t k = 0; k < (size_t) bands; ++k)
                {
                    const auto y = filters[k].process (x, 0);
                    energy[k] += (double) y * y;
                }
                samples += 1.0;
            }
            float db (int k) const noexcept
            {
                return samples < 1.0 ? empty : (float) juce::jmax ((double) empty + 2.0, 10.0 * std::log10 (energy[(size_t) k] / samples + 1.0e-14));
            }
        };
    }

    class MatchEq final : public BuiltInEffect,
                          private juce::Timer
    {
    public:
        static constexpr int taps = 4096;
        explicit MatchEq (const EffectInfo& i) : BuiltInEffect (i)
        {
            for (auto* set : { &learnedTemplate, &learnedCurrent, &fileTemplate })
                for (auto& x : *set)
                    x.store (match::empty);
        }
        ~MatchEq() override { stopTimer(); }

        int getLatencySamples() const override { return choice (match::phase) == 0 ? taps / 2 : 0; }

        void prepare (double rate, int maxBlock) override
        {
            sampleRate = preparedRate = rate;
            blockSize = std::max (16, maxBlock);
            convolution.prepare ({ rate, (juce::uint32) blockSize, 2 });
            templateBank.prepare (rate);
            currentBank.prepare (rate);
            built.fill (std::numeric_limits<float>::quiet_NaN());
            rebuild();
            startTimerHz (6);
        }

        void setAttachedFile (const juce::String& path) override
        {
            if (path == filePath)
                return;
            filePath = path;
            const juce::File file (path);
            if (path.isEmpty() || ! file.existsAsFile())
                return;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
                return;
            match::Bank bank;
            bank.prepare (reader->sampleRate);
            const auto length = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 180.0));
            juce::AudioBuffer<float> chunk ((int) std::min (2u, reader->numChannels), 65536);
            for (juce::int64 at = 0; at < length; at += chunk.getNumSamples())
            {
                const auto n = (int) std::min<juce::int64> (chunk.getNumSamples(), length - at);
                reader->read (&chunk, 0, n, at, true, chunk.getNumChannels() > 1);
                for (int i = 0; i < n; ++i)
                    bank.add (chunk.getNumChannels() > 1 ? 0.5f * (chunk.getSample (0, i) + chunk.getSample (1, i)) : chunk.getSample (0, i));
            }
            for (int k = 0; k < match::bands; ++k)
                fileTemplate[(size_t) k].store (bank.db (k));
            fileGeneration.store (fileGeneration.load() + 1);
        }

        /** 0-30 the template being learned, 31-61 the material being learned, 62/63 seconds learned of each,
            64 the file template's generation (changes when a file was read), 65-95 the file's template. */
        int readMeters (float* dest, int capacity) const noexcept override
        {
            if (capacity < 96)
                return 0;
            for (int k = 0; k < match::bands; ++k)
            {
                dest[k] = learnedTemplate[(size_t) k].load (std::memory_order_relaxed);
                dest[31 + k] = learnedCurrent[(size_t) k].load (std::memory_order_relaxed);
                dest[65 + k] = fileTemplate[(size_t) k].load (std::memory_order_relaxed);
            }
            dest[62] = secondsT.load (std::memory_order_relaxed);
            dest[63] = secondsC.load (std::memory_order_relaxed);
            dest[64] = (float) fileGeneration.load (std::memory_order_relaxed);
            return 96;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            // Learning: the input's spectrum, before the filter
            const auto learningT = value (match::learnT) > 0.5f, learningC = value (match::learnC) > 0.5f;
            if (learningT && ! wasLearningT) templateBank.reset();
            if (learningC && ! wasLearningC) currentBank.reset();
            wasLearningT = learningT;
            wasLearningC = learningC;
            if (learningT || learningC)
            {
                for (int i = 0; i < n; ++i)
                {
                    const auto x = 0.5f * (l[i] + r[i]);
                    if (learningT) templateBank.add (x);
                    if (learningC) currentBank.add (x);
                }
                for (int k = 0; k < match::bands; ++k)
                {
                    if (learningT) learnedTemplate[(size_t) k].store (templateBank.db (k), std::memory_order_relaxed);
                    if (learningC) learnedCurrent[(size_t) k].store (currentBank.db (k), std::memory_order_relaxed);
                }
                secondsT.store ((float) (templateBank.samples / sampleRate), std::memory_order_relaxed);
                secondsC.store ((float) (currentBank.samples / sampleRate), std::memory_order_relaxed);
            }

            for (int at = 0; at < n; at += blockSize)
            {
                const auto count = std::min (blockSize, n - at);
                float* channels[] { l + at, r + at };
                juce::dsp::AudioBlock<float> block (channels, 2, (size_t) count);
                convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            }
        }

    private:
        void timerCallback() override { rebuild(); }

        void rebuild()
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load();
            // (learning or clearing doesn't change the filter)
            for (auto i : { match::analyzer, match::learnT, match::learnC, match::clearT, match::clearC, match::templateGen })
                v[(size_t) i] = 0.0f;
            if (v == built || sampleRate <= 0.0)
                return;
            built = v;
            const auto minimum = juce::roundToInt (v[match::phase]) == 1;
            // The curve's magnitude at each bin of a long FFT
            constexpr int order = 13, size = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<std::complex<float>> spectrum ((size_t) size), work ((size_t) size);
            for (int b = 0; b <= size / 2; ++b)
            {
                const auto f = std::max (5.0, b * sampleRate / size);
                const auto mag = (float) std::pow (10.0, match::response (v.data(), f, sampleRate) / 20.0);
                spectrum[(size_t) b] = mag;
                if (b > 0 && b < size / 2)
                    spectrum[(size_t) (size - b)] = mag;
            }
            juce::AudioBuffer<float> ir (1, taps);
            if (! minimum)
            {
                // zero phase, back to time, centred and windowed
                fft.perform (spectrum.data(), work.data(), true);
                for (int i = 0; i < taps; ++i)
                {
                    const auto source = (i - taps / 2 + size) % size;
                    const auto w = 0.42f - 0.5f * std::cos (twoPi * i / (taps - 1)) + 0.08f * std::cos (2.0f * twoPi * i / (taps - 1));
                    ir.setSample (0, i, work[(size_t) source].real() * w);
                }
            }
            else
            {
                // minimum phase from the real cepstrum: log magnitude, to quefrency, folded, and back
                for (auto& c : spectrum)
                    c = std::log (std::max (1.0e-6f, std::abs (c)));
                fft.perform (spectrum.data(), work.data(), true);
                for (int i = 1; i < size / 2; ++i)
                {
                    work[(size_t) i] *= 2.0f;
                    work[(size_t) (size - i)] = 0.0f;
                }
                fft.perform (work.data(), spectrum.data(), false);
                for (auto& c : spectrum)
                    c = std::exp (c);
                fft.perform (spectrum.data(), work.data(), true);
                for (int i = 0; i < taps; ++i)
                {
                    const auto fade = i < taps * 3 / 4 ? 1.0f : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - taps * 3 / 4) / (float) (taps / 4));
                    ir.setSample (0, i, work[(size_t) i].real() * fade);
                }
            }
            convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        }

        juce::dsp::Convolution convolution;
        int blockSize = 512;
        std::array<float, maxParams> built {};
        match::Bank templateBank, currentBank;
        bool wasLearningT = false, wasLearningC = false;
        std::array<std::atomic<float>, match::bands> learnedTemplate {}, learnedCurrent {}, fileTemplate {};
        std::atomic<float> secondsT { 0.0f }, secondsC { 0.0f };
        std::atomic<int> fileGeneration { 0 };
        juce::String filePath;
    };

    //==============================================================================
    /** Logic's Delay Designer: up to 26 taps (A to Z), each with its own time, level, pan, transposition,
        filter, flip and mute; taps can snap to a tempo grid with swing, and one tap feeds back. */
    class DelayDesigner final : public BuiltInEffect
    {
    public:
        static constexpr int maxTaps = 26, globals = 11, perTap = 12;
        enum Global { tapSel, tapCount, sync, grid, swing, fbOn, fbTap, fbLevel, dry, wet, view };
        enum Tap { time, level, pan, transpose, cents, filterOn, hpFreq, lpFreq, slope, reso, flip, mute };
        static int index (int tap, Tap p) noexcept { return globals + tap * perTap + (int) p; }

        using BuiltInEffect::BuiltInEffect;

        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            window = (float) (0.08 * rate);
            for (auto& line : lines)
                line.prepare ((int) (10.2 * rate) + (int) window * 2 + 8);
            for (auto& t : taps)
                t = {};
            feedbackL = feedbackR = 0.0f;
        }

        /** A tap's time in ms, snapped to the grid (with swing) when Sync is on. */
        static double tapTimeMs (const float* v, int t, double beatsPerMinute)
        {
            const auto ms = (double) v[index (t, time)];
            if (v[sync] < 0.5f)
                return ms;
            static constexpr double beats[] { 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
            const auto stepMs = beats[juce::jlimit (0, 7, juce::roundToInt (v[grid]))] * 60000.0 / std::max (20.0, beatsPerMinute);
            const auto n = std::max (1.0, std::round (ms / stepMs));
            const auto odd = ((long long) n) % 2 == 1;
            return n * stepMs + (odd ? ((double) v[swing] - 50.0) / 50.0 * stepMs : 0.0);
        }

        int readMeters (float* dest, int capacity) const noexcept override
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            const auto count = std::min (capacity, maxTaps);
            for (int t = 0; t < count; ++t)
                dest[t] = (float) tapTimeMs (v.data(), t, bpm.load (std::memory_order_relaxed));
            return count;
        }

        void process (float* l, float* r, int n) noexcept override
        {
            std::array<float, maxParams> v;
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = values[i].load (std::memory_order_relaxed);
            const auto count = juce::jlimit (1, maxTaps, juce::roundToInt (v[tapCount]) + 1);
            const auto tempo = bpm.load (std::memory_order_relaxed);
            const auto fbIndex = juce::roundToInt (v[fbTap]);
            const auto fbGain = v[fbOn] > 0.5f && fbIndex < count ? v[fbLevel] * 0.01f : 0.0f;
            const auto dryGain = v[dry] * 0.01f, wetGain = v[wet] * 0.01f;

            struct Setting { float delay, gain, panL, panR, step; bool pitched, flipped, filtered, firstOrder; };
            std::array<Setting, maxTaps> set {};
            for (int t = 0; t < count; ++t)
            {
                auto& s = set[(size_t) t];
                auto& state = taps[(size_t) t];
                const auto lvl = v[(size_t) index (t, level)];
                s.gain = v[(size_t) index (t, mute)] > 0.5f || lvl <= -59.9f ? 0.0f : dbToGain (lvl);
                s.delay = (float) std::max (1.0, tapTimeMs (v.data(), t, tempo) * 0.001 * sampleRate);
                const auto p = juce::jlimit (-1.0f, 1.0f, v[(size_t) index (t, pan)] * 0.01f);
                s.panL = p > 0.0f ? 1.0f - p : 1.0f;
                s.panR = p < 0.0f ? 1.0f + p : 1.0f;
                const auto semis = v[(size_t) index (t, transpose)] + v[(size_t) index (t, cents)] * 0.01f;
                s.pitched = std::abs (semis) > 0.001f;
                s.step = s.pitched ? (1.0f - std::pow (2.0f, semis / 12.0f)) / window : 0.0f;
                s.flipped = v[(size_t) index (t, flip)] > 0.5f;
                s.filtered = v[(size_t) index (t, filterOn)] > 0.5f;
                s.firstOrder = v[(size_t) index (t, slope)] < 0.5f;
                if (s.filtered)
                {
                    const auto q = 0.707 + v[(size_t) index (t, reso)] * 0.01 * 7.0;
                    const auto hp = juce::jlimit (10.0, sampleRate * 0.45, (double) v[(size_t) index (t, hpFreq)]);
                    const auto lp = juce::jlimit (10.0, sampleRate * 0.45, (double) v[(size_t) index (t, lpFreq)]);
                    if (s.firstOrder) { state.hp.setFirstOrderHighPass (sampleRate, hp); state.lp.setFirstOrderLowPass (sampleRate, lp); }
                    else              { state.hp.setHighPass (sampleRate, hp, q); state.lp.setLowPass (sampleRate, lp, q); }
                }
            }

            for (int i = 0; i < n; ++i)
            {
                float wl = 0.0f, wr = 0.0f, fl = 0.0f, fr = 0.0f;
                for (int t = 0; t < count; ++t)
                {
                    const auto& s = set[(size_t) t];
                    auto& state = taps[(size_t) t];
                    if (s.gain == 0.0f && t != fbIndex)
                        continue;
                    float a, b;
                    if (s.pitched)
                    {
                        // two read heads sweeping through an 80 ms window, crossfaded: the tap's pitch moves
                        state.phase += s.step;
                        state.phase -= std::floor (state.phase);
                        const auto p2 = state.phase + 0.5f - std::floor (state.phase + 0.5f);
                        const auto g1 = std::sin (juce::MathConstants<float>::pi * state.phase);
                        const auto w1 = g1 * g1, w2 = 1.0f - w1;
                        const auto d1 = s.delay + state.phase * window, d2 = s.delay + p2 * window;
                        a = lines[0].read (d1) * w1 + lines[0].read (d2) * w2;
                        b = lines[1].read (d1) * w1 + lines[1].read (d2) * w2;
                    }
                    else
                    {
                        a = lines[0].read (s.delay);
                        b = lines[1].read (s.delay);
                    }
                    if (s.filtered)
                    {
                        a = state.lp.process (state.hp.process (a, 0), 0);
                        b = state.lp.process (state.hp.process (b, 1), 1);
                    }
                    if (s.flipped)
                        std::swap (a, b);
                    a *= s.gain * s.panL;
                    b *= s.gain * s.panR;
                    if (t == fbIndex) { fl = a; fr = b; }
                    wl += a;
                    wr += b;
                }
                feedbackL = fl * fbGain;
                feedbackR = fr * fbGain;
                lines[0].push (l[i] + softClip (feedbackL));
                lines[1].push (r[i] + softClip (feedbackR));
                l[i] = l[i] * dryGain + wl * wetGain;
                r[i] = r[i] * dryGain + wr * wetGain;
            }
        }

    private:
        static float softClip (float x) noexcept { return std::abs (x) < 1.0f ? x : std::tanh (x); }
        struct TapState { Biquad hp, lp; float phase = 0.0f; };
        std::array<DelayLine, 2> lines;
        std::array<TapState, maxTaps> taps {};
        float window = 2400.0f, feedbackL = 0.0f, feedbackR = 0.0f;
    };

    //==============================================================================
    /** An octave (or two) below the bass, made from its zero crossings and following its level. */
    class SubBass final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            for (auto* f : { &detect1, &detect2, &smooth1, &smooth2 }) f->reset();
            env = 0.0f;
            state = 1.0f;
            count = 0;
            previous = 0.0f;
            lastCut = -1.0f;
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto cut = value (0);
            if (cut != lastCut)
            {
                detect1.setLowPass (sampleRate, cut, butterQ1);
                detect2.setLowPass (sampleRate, cut, butterQ2);
                smooth1.setLowPass (sampleRate, cut * 0.6, butterQ1);
                smooth2.setLowPass (sampleRate, cut * 0.6, butterQ2);
                lastCut = cut;
            }
            const auto octaves = choice (1) + 1;
            const auto level = dbToGain (value (2));
            const auto dry = value (3) <= -59.0f ? 0.0f : dbToGain (value (3));
            const bool square = choice (4) == 1;
            const auto envCoeff = coeffMs (20.0, sampleRate);
            for (int i = 0; i < n; ++i)
            {
                const auto low = detect2.process (detect1.process (0.5f * (l[i] + r[i]), 0), 0);
                env = std::abs (low) + envCoeff * (env - std::abs (low));
                // A flip on every rising zero crossing (every second one for two octaves down)
                if (previous < 0.0f && low >= 0.0f && ++count >= octaves)
                {
                    count = 0;
                    state = -state;
                }
                previous = low;
                auto sub = state * env * 1.5f;
                if (! square)
                    sub = smooth2.process (smooth1.process (sub, 0), 0) * 1.3f;
                l[i] = l[i] * dry + sub * level;
                r[i] = r[i] * dry + sub * level;
            }
        }
    private:
        Biquad detect1, detect2, smooth1, smooth2;
        float env = 0.0f, state = 1.0f, previous = 0.0f, lastCut = -1.0f;
        int count = 0;
    };

    //==============================================================================
    /** Wider: each side a few milliseconds apart and a few cents apart, gently moving. */
    class Spreader final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
        void prepare (double rate, int) override
        {
            sampleRate = preparedRate = rate;
            dl.prepare ((int) (0.06 * rate));
            dr.prepare ((int) (0.06 * rate));
        }
        void process (float* l, float* r, int n) noexcept override
        {
            const auto delay = value (0) * 0.001f * (float) sampleRate;
            const auto cents = value (1);
            const auto rate = value (2);
            const auto swing = (float) ((std::pow (2.0, cents / 1200.0) - 1.0) / (juce::MathConstants<double>::twoPi * std::max (0.05f, rate)) * sampleRate);
            const auto width = value (3) * 0.01f;
            const auto mix = value (4) * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                const auto m = 0.5f * (l[i] + r[i]);
                dl.push (m);
                dr.push (m);
                const auto a = dl.read (2.0f + delay * 0.3f + std::min (swing, delay * 0.3f + 1.0f) * (1.0f + lfo.peek (0.0)));
                const auto b = dr.read (2.0f + delay + std::min (swing, delay * 0.3f + 1.0f) * (1.0f + lfo.peek (0.5)));
                lfo.next (rate, sampleRate);
                const auto side = 0.5f * (a - b) * width;
                l[i] = l[i] + side * mix;
                r[i] = r[i] - side * mix;
            }
        }
    private:
        DelayLine dl, dr;
        Lfo lfo;
    };

    template <typename T>
    std::unique_ptr<BuiltInEffect> make (const EffectInfo& info) { return std::make_unique<T> (info); }
}

std::vector<ParamInfo> matchEqParams()
{
    const std::vector<juce::String> onOff { "Off", "On" };
    std::vector<ParamInfo> p {
        { "apply", "Apply", " %", -100.0f, 200.0f, 100.0f, 100.0f },
        { "smoothing", "Smoothing", " %", 0.0f, 100.0f, 30.0f, 30.0f },
        { "phase", "Phase", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Linear", "Minimum" } },
        { "analyzer", "Analyzer", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
        { "learnT", "Learn Template", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        { "learnC", "Learn Current", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        { "clearT", "Clear Template", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        { "clearC", "Clear Current", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        { "output", "Gain", " dB", -24.0f, 24.0f, 0.0f, 0.0f },
        { "templateGen", "Template File", "", 0.0f, 100000.0f, 0.0f, 50000.0f },
    };
    const float freqs[] { 100.0f, 500.0f, 2500.0f, 8000.0f };
    for (int u = 0; u < 4; ++u)
    {
        const auto n = juce::String (u + 1);
        p.push_back ({ "u" + n + "Freq", "Band " + n + " Freq", " Hz", 20.0f, 20000.0f, freqs[u], 1000.0f });
        p.push_back ({ "u" + n + "Gain", "Band " + n + " Gain", " dB", -24.0f, 24.0f, 0.0f, 0.0f });
        p.push_back ({ "u" + n + "Q", "Band " + n + " Q", "", 0.1f, 10.0f, 1.0f, 1.0f });
        p.push_back ({ "u" + n + "On", "Band " + n, "", 0.0f, 1.0f, 1.0f, 0.5f, onOff });
    }
    for (const char* set : { "t", "c" })
        for (int k = 0; k < 31; ++k)
        {
            const auto f = 1000.0 * std::pow (2.0, (k - 17) / 3.0);
            const auto name = juce::String (set[0] == 't' ? "Template " : "Current ") + (f >= 1000.0 ? juce::String (f / 1000.0, 1) + "k" : juce::String (juce::roundToInt (f)));
            p.push_back ({ juce::String (set) + juce::String (k + 1), name, " dB", -120.0f, 20.0f, -120.0f, -40.0f });
        }
    return p;
}

std::vector<ParamInfo> delayDesignerParams()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), dB (" dB");
    std::vector<juce::String> letters;
    for (char c = 'A'; c <= 'Z'; ++c)
        letters.push_back (juce::String::charToString (c));
    const std::vector<juce::String> onOff { "Off", "On" };
    std::vector<juce::String> counts;
    for (int c = 1; c <= 26; ++c)
        counts.push_back (juce::String (c));
    std::vector<ParamInfo> p {
        { "tapSel", "Tap", "", 0.0f, 25.0f, 0.0f, 12.5f, letters },
        { "tapCount", "Taps", "", 0.0f, 25.0f, 3.0f, 12.5f, counts },
        { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff },
        { "grid", "Grid", "", 0.0f, 7.0f, 2.0f, 3.5f, { "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/32 T" } },
        { "swing", "Swing", pc, 50.0f, 100.0f, 50.0f, 75.0f },
        { "fbOn", "Feedback", "", 0.0f, 1.0f, 1.0f, 0.5f, onOff },
        { "fbTap", "Feedback Tap", "", 0.0f, 25.0f, 3.0f, 12.5f, letters },
        { "feedback", "Feedback Level", pc, 0.0f, 100.0f, 30.0f, 40.0f },
        { "dry", "Dry", pc, 0.0f, 100.0f, 100.0f, 50.0f },
        { "mix", "Wet", pc, 0.0f, 100.0f, 50.0f, 50.0f },
        { "view", "View", "", 0.0f, 4.0f, 4.0f, 2.0f, { "Cutoff", "Reso", "Transp", "Pan", "Level" } },
    };
    for (int t = 0; t < 26; ++t)
    {
        const auto n = juce::String (t + 1), name = "Tap " + letters[(size_t) t] + " ";
        p.push_back ({ "time" + n, name + "Delay", ms, 0.0f, 10000.0f, (float) (t + 1) * 250.0f, 1000.0f });
        p.push_back ({ "level" + n, name + "Level", dB, -60.0f, 0.0f, juce::jmax (-30.0f, -3.0f - (float) t * 2.0f), -12.0f });
        p.push_back ({ "pan" + n, name + "Pan", pc, -100.0f, 100.0f, (t % 2 == 0 ? -1.0f : 1.0f) * (float) juce::jmin (100, 30 + t * 10), 0.0f });
        p.push_back ({ "transp" + n, name + "Transpose", " st", -24.0f, 24.0f, 0.0f, 0.0f });
        p.push_back ({ "cents" + n, name + "Fine", " ct", -50.0f, 50.0f, 0.0f, 0.0f });
        p.push_back ({ "filter" + n, name + "Filter", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
        p.push_back ({ "hp" + n, name + "HP Cutoff", hz, 20.0f, 20000.0f, 20.0f, 1000.0f });
        p.push_back ({ "lp" + n, name + "LP Cutoff", hz, 20.0f, 20000.0f, 20000.0f, 1000.0f });
        p.push_back ({ "slope" + n, name + "Slope", "", 0.0f, 1.0f, 1.0f, 0.5f, { "6 dB", "12 dB" } });
        p.push_back ({ "reso" + n, name + "Reso", pc, 0.0f, 100.0f, 0.0f, 50.0f });
        p.push_back ({ "flip" + n, name + "Flip", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
        p.push_back ({ "mute" + n, name + "Mute", "", 0.0f, 1.0f, 0.0f, 0.5f, onOff });
    }
    return p;
}

const std::vector<StockEffect>& stockExtraEffects()
{
    const juce::String ms (" ms"), hz (" Hz"), pc (" %"), dB (" dB");
    static const std::vector<StockEffect> effects = [&]
    {
        // The Linear Phase EQ: the Channel EQ's knobs and curve
        const auto& channel = stockEqEffects().front().info;
        EffectInfo linear { "linearEq", "Linear Phase EQ", channel.params, "EQ", channel.response, channel.columns };

        const auto taps = delayDesignerParams();

        return std::vector<StockEffect> {
            { linear, make<LinearPhaseEq> },
            { { "matchEq", "Match EQ", matchEqParams(), "EQ", match::response }, make<MatchEq> },
            { { "delayDesigner", "Delay Designer", taps, "Delay" }, make<DelayDesigner> },
            { { "subBass", "Sub Bass", {
                  { "cut", "Bass Range", hz, 60.0f, 300.0f, 150.0f, 150.0f },
                  { "octave", "Octave", "", 0.0f, 1.0f, 0.0f, 0.5f, { "-1", "-2" } },
                  { "level", "Sub Level", dB, -60.0f, 6.0f, -6.0f, -12.0f },
                  { "dry", "Dry Level", dB, -60.0f, 6.0f, 0.0f, -6.0f },
                  { "shape", "Shape", "", 0.0f, 1.0f, 0.0f, 0.5f, { "Sine", "Square" } } }, "Specialized" },
              make<SubBass> },
            { { "spreader", "Spreader", {
                  { "delay", "Delay", ms, 0.0f, 30.0f, 8.0f, 10.0f },
                  { "cents", "Pitch Spread", " cents", 0.0f, 30.0f, 8.0f, 10.0f },
                  { "rate", "Rate", hz, 0.05f, 3.0f, 0.3f, 0.5f },
                  { "width", "Width", pc, 0.0f, 200.0f, 100.0f, 100.0f },
                  { "mix", "Mix", pc, 0.0f, 100.0f, 100.0f, 50.0f } }, "Imaging" },
              make<Spreader> },
        };
    }();
    return effects;
}

} // namespace riff
