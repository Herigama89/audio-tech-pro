#include "Wavetables.h"
#include "NemixParams.h"
#include <complex>

namespace riff::nemix::wt
{
namespace
{
    using Spectrum = std::vector<std::complex<float>>;   // [harmonic 0..maxHarmonic]
    constexpr float pi = juce::MathConstants<float>::pi;

    Spectrum empty() { return Spectrum ((size_t) maxHarmonic + 1); }

    /** A sum of sines: b(n) for each harmonic. */
    template <typename F> Spectrum sines (F&& b, int most = maxHarmonic)
    {
        auto s = empty();
        for (int n = 1; n <= std::min (most, maxHarmonic); ++n)
            s[(size_t) n] = { 0.0f, -b (n) };
        return s;
    }

    /** The spectrum of a shape drawn in time (x through the cycle, 0..1), from 8192 points so little folds back. */
    template <typename F> Spectrum shape (F&& fn)
    {
        constexpr int order = 13, n = 1 << order;
        static thread_local juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = fn ((float) i / (float) n);
        fft.performRealOnlyForwardTransform (buf.data(), true);
        auto s = empty();
        for (int h = 1; h <= maxHarmonic; ++h)
            s[(size_t) h] = { buf[(size_t) h * 2], buf[(size_t) h * 2 + 1] };
        return s;
    }

    Spectrum mix (const Spectrum& a, const Spectrum& b, float t)
    {
        auto s = empty();
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = a[i] * (1.0f - t) + b[i] * t;
        return s;
    }

    float level (const Spectrum& s)
    {
        float sum = 0.0f;
        for (const auto& c : s) sum += std::norm (c);
        return std::sqrt (sum) + 1.0e-12f;
    }

    /** a and b at the same loudness before mixing (so a morph doesn't dip or swell). */
    Spectrum morph (const Spectrum& a, const Spectrum& b, float t)
    {
        auto bb = b;
        const auto k = level (a) / level (b);
        for (auto& c : bb) c *= k;
        return mix (a, bb, t);
    }

    float saw (int n)      { return ((n & 1) ? 1.0f : -1.0f) * 2.0f / (pi * (float) n); }
    float square (int n)   { return (n & 1) ? 4.0f / (pi * (float) n) : 0.0f; }
    float triangle (int n) { return (n & 1) ? (((n - 1) / 2) % 2 == 0 ? 1.0f : -1.0f) * 8.0f / (pi * pi * (float) n * (float) n) : 0.0f; }

    /** A resonance at harmonic `centre` (width in harmonics), for formants and filter sweeps. */
    float peak (float n, float centre, float width) { const auto d = (n - centre) / std::max (0.3f, width); return std::exp (-0.5f * d * d); }

    /** The formants of a vowel (Hz) at t across A E I O U, as harmonic numbers of a 130 Hz voice. */
    std::array<float, 3> vowel (float t)
    {
        static const float f[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        const auto p = juce::jlimit (0.0f, 1.0f, t) * 4.0f;
        const auto i = std::min (3, (int) p);
        const auto k = p - (float) i;
        std::array<float, 3> r {};
        for (int j = 0; j < 3; ++j)
            r[(size_t) j] = (f[i][j] + k * (f[i + 1][j] - f[i][j])) / 130.0f;
        return r;
    }

    Spectrum formantSaw (const std::array<float, 3>& formants, float brightness, float q)
    {
        return sines ([&] (int n)
        {
            const auto fn = (float) n;
            const auto env = peak (fn, formants[0], q * 2.5f) + 0.7f * peak (fn, formants[1], q * 3.0f) + 0.35f * peak (fn, formants[2], q * 4.0f);
            return saw (n) * (0.08f * brightness + env);
        });
    }

    /** One frame's spectrum: table `index` at `t` (0..1 across its frames). */
    Spectrum frameSpectrum (int index, float t)
    {
        switch (index)
        {
            case 0:   // Basic Shapes: sine, triangle, saw, square
            {
                const Spectrum shapes[] { sines ([] (int n) { return n == 1 ? 1.0f : 0.0f; }), sines (triangle), sines (saw), sines (square) };
                const auto p = t * 3.0f;
                const auto i = std::min (2, (int) p);
                return morph (shapes[i], shapes[i + 1], p - (float) i);
            }
            case 1:   // Pulse Width: 50% down to a thin pulse
            {
                const auto w = 0.5f - 0.47f * t;
                return shape ([w] (float x) { return x < w ? 1.0f - w : -w; });
            }
            case 2:   // Saw Harmonics: a resonant low pass opening over a saw
            case 3:   // ... and over a square
            {
                const auto cut = 1.0f + std::pow (t, 2.2f) * 200.0f;
                return sines ([&] (int n)
                {
                    const auto fn = (float) n;
                    const auto lp = 1.0f / std::sqrt (1.0f + std::pow (fn / cut, 4.0f));
                    return (index == 2 ? saw (n) : square (n)) * lp * (1.0f + 1.5f * peak (fn, cut, 0.6f + cut * 0.08f));
                });
            }
            case 4:   // Harmonic Sweep: the fundamental and one bright partial walking up
            {
                const auto centre = 1.0f + t * 40.0f;
                return sines ([&] (int n) { return (n == 1 ? 0.8f : 0.0f) + peak ((float) n, centre, 0.7f) * 0.7f; }, 64);
            }
            case 5:   // Vowels: A E I O U
                return formantSaw (vowel (t), 1.0f, 1.0f);
            case 6:   // FM Bells: two operators, the index rising
            {
                const auto index6 = t * 7.0f;
                return shape ([index6] (float x) { return std::sin (2.0f * pi * x + index6 * std::sin (2.0f * pi * 3.0f * x)); });
            }
            case 7:   // Sync Sweep: a saw hard-synced, its ratio rising
            {
                const auto ratio = 1.0f + t * 7.0f;
                return shape ([ratio] (float x)
                {
                    const auto y = std::fmod (x * ratio, 1.0f);
                    return (2.0f * y - 1.0f) * (1.0f - x * 0.3f);
                });
            }
            case 8:   // Fold: a sine wave folded harder and harder
            {
                const auto drive = 1.0f + t * 9.0f;
                return shape ([drive] (float x) { return std::sin (std::sin (2.0f * pi * x) * drive * pi * 0.5f); });
            }
            case 9:   // Digital: a sine through fewer and fewer bits
            {
                const auto steps = std::pow (2.0f, 7.0f - t * 5.5f);
                return shape ([steps] (float x) { return std::round (std::sin (2.0f * pi * x) * steps) / steps; });
            }
            case 10:  // Growl: a saw with a moving vocal resonance and a little fold
            {
                const auto formants = vowel (0.15f + 0.7f * t);
                auto a = formantSaw (formants, 0.6f, 0.7f);
                const auto folded = shape ([t] (float x) { const auto s = 2.0f * x - 1.0f; return std::sin (s * (2.0f + t * 4.0f)); });
                return morph (a, folded, 0.35f);
            }
            case 11:  // Organ: drawbar settings, mellow to full
            {
                static const float bars[4][6] { { 1, 0.6f, 0, 0, 0, 0 }, { 1, 0.8f, 0.6f, 0.3f, 0, 0 }, { 1, 1, 0.8f, 0.6f, 0.4f, 0.3f }, { 1, 0.9f, 1, 0.8f, 0.8f, 0.7f } };
                const int harmonic[6] { 1, 2, 3, 4, 6, 8 };
                const auto p = t * 3.0f;
                const auto i = std::min (2, (int) p);
                const auto k = p - (float) i;
                return sines ([&] (int n)
                {
                    for (int j = 0; j < 6; ++j)
                        if (harmonic[j] == n) return bars[i][j] + k * (bars[i + 1][j] - bars[i][j]);
                    return 0.0f;
                }, 8);
            }
            case 12:  // Hollow: odd harmonics only, softer to brighter (clarinet-like)
                return sines ([t] (int n) { return (n & 1) ? 1.0f / std::pow ((float) n, 2.0f - 1.3f * t) : 0.0f; });
            case 13:  // Bright Stack: a saw with its octave and fifth growing in
                return sines ([t] (int n) { return saw (n) + t * (n % 2 == 0 ? saw (n / 2) * 0.6f : 0.0f) + t * (n % 3 == 0 ? saw (n / 3) * 0.45f : 0.0f); });
            case 14:  // Brass: brighter as it's blown harder (a resonance near 1.2 kHz)
            {
                const auto bright = 4.0f + t * 40.0f;
                return sines ([&] (int n) { const auto fn = (float) n; return saw (n) * (1.0f / (1.0f + std::pow (fn / bright, 2.0f))) * (1.0f + 0.8f * peak (fn, 9.0f, 3.0f)); });
            }
            case 15:  // Strings: a gentle saw, its sheen growing
            {
                const auto bright = 6.0f + t * 60.0f;
                return sines ([&] (int n) { const auto fn = (float) n; return saw (n) / std::sqrt (1.0f + std::pow (fn / bright, 3.0f)) * (1.0f + 0.4f * peak (fn, 22.0f, 6.0f)); });
            }
            case 16:  // Electric Piano: a sine body and the tine's bell partials fading
                return sines ([t] (int n) { return n == 1 ? 1.0f : n == 2 ? 0.25f : n == 3 ? 0.08f : (n == 7 || n == 14) ? 0.35f * (1.0f - t) : n == 21 ? 0.12f * (1.0f - t) : 0.0f; }, 24);
            case 17:  // Choir: soft "oo" opening to "ah"
            {
                const auto formants = vowel (1.0f - 0.75f * t);
                return formantSaw (formants, 0.25f, 1.4f);
            }
            case 18:  // Spectral Noise: random partials, a new set each frame
            {
                juce::Random r (1234 + (int) (t * 31.0f + 0.5f));
                return sines ([&r] (int n) { return (r.nextFloat() * 2.0f - 1.0f) / std::pow ((float) n, 0.75f); });
            }
            case 19:  // Phase Distortion: a cosine bent into a resonant peak
            {
                const auto d = 0.02f + 0.46f * (1.0f - t);
                return shape ([d, t] (float x)
                {
                    const auto p = x < d ? x * 0.5f / d : 0.5f + (x - d) * 0.5f / (1.0f - d);
                    return -std::cos (2.0f * pi * p * (1.0f + t * 3.0f)) * (1.0f - x);
                });
            }
            case 20:  // Reese: two saws drifting apart in phase
            {
                const auto offset = t * 0.5f;
                return shape ([offset] (float x)
                {
                    const auto a = 2.0f * std::fmod (x, 1.0f) - 1.0f, b = 2.0f * std::fmod (x + offset, 1.0f) - 1.0f;
                    return 0.5f * (a + b);
                });
            }
            case 21:  // Bell Partials: the strong partials of a bell coming in one by one
            {
                const int partial[6] { 1, 2, 3, 5, 8, 12 };
                return sines ([t, &partial] (int n)
                {
                    for (int j = 0; j < 6; ++j)
                        if (partial[j] == n) return juce::jlimit (0.0f, 1.0f, t * 6.0f - (float) j + 1.0f) / (1.0f + (float) j * 0.3f);
                    return 0.0f;
                }, 16);
            }
            case 22:  // Wobble: a sharp resonance sweeping up a saw (the "yoy")
            {
                const auto centre = 2.0f + std::pow (t, 1.5f) * 28.0f;
                return sines ([&] (int n) { const auto fn = (float) n; return saw (n) * (0.15f + 2.2f * peak (fn, centre, 1.0f + centre * 0.05f)); });
            }
            case 23:  // Sub Thump: a sine filling out with its second and third harmonics
                return sines ([t] (int n) { return n == 1 ? 1.0f : n == 2 ? 0.35f * t : n == 3 ? 0.2f * t * t : n == 4 ? 0.08f * t * t : 0.0f; }, 6);
            case 24:  // Glass: sparse high partials, cold and bright
                return sines ([t] (int n)
                {
                    const auto r = std::sqrt ((float) n);
                    return std::abs (r - std::round (r)) < 1.0e-4f ? 1.0f / (0.6f + r * (1.4f - t)) : 0.0f;
                }, 256);
            case 25:  // Chip: 8-bit pulses (12.5 %, 25 %, 50 %) and the stepped triangle
            {
                if (t < 0.75f)
                {
                    const auto w = t < 0.25f ? 0.125f : t < 0.5f ? 0.25f : 0.5f;
                    return shape ([w] (float x) { return x < w ? 1.0f : -1.0f; });
                }
                return shape ([] (float x) { const auto tri = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; return std::round (tri * 7.5f) / 7.5f; });
            }
            default:
                return sines ([] (int n) { return n == 1 ? 1.0f : 0.0f; });
        }
    }

    /** Spectra to the stored levels, each frame at the same peak. */
    std::unique_ptr<Table> build (const std::vector<Spectrum>& spectra, const juce::String& name)
    {
        constexpr int order = 10;
        static_assert ((1 << order) == frameSize);
        juce::dsp::FFT fft (order);
        auto table = std::make_unique<Table>();
        table->name = name;
        for (auto& d : table->data)
            d.assign ((size_t) frames * (frameSize + 1), 0.0f);
        std::vector<float> buf ((size_t) frameSize * 2);
        for (int f = 0; f < frames; ++f)
        {
            const auto& s = spectra[(size_t) std::min ((int) spectra.size() - 1, f)];
            float gain = 1.0f;
            for (int l = 0; l < levels; ++l)
            {
                const auto most = maxHarmonic >> l;
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int h = 1; h <= most && h < frameSize / 2; ++h)
                {
                    buf[(size_t) h * 2] = s[(size_t) h].real();
                    buf[(size_t) h * 2 + 1] = s[(size_t) h].imag();
                }
                fft.performRealOnlyInverseTransform (buf.data());
                if (l == 0)
                {
                    float peakValue = 0.0f;
                    for (int i = 0; i < frameSize; ++i)
                        peakValue = std::max (peakValue, std::abs (buf[(size_t) i]));
                    gain = peakValue > 1.0e-9f ? 0.92f / peakValue : 0.0f;
                }
                auto* out = table->data[(size_t) l].data() + (size_t) f * (frameSize + 1);
                for (int i = 0; i < frameSize; ++i)
                    out[i] = buf[(size_t) i] * gain;
                out[frameSize] = out[0];
            }
        }
        return table;
    }

    constexpr int slots = 64;
    struct Store
    {
        std::array<std::unique_ptr<Table>, slots> tables;
        std::array<std::atomic<const Table*>, slots> published {};
        std::vector<std::unique_ptr<Table>> retired;   // replaced ones, kept: the audio thread may still be reading them
        juce::CriticalSection lock;
    };
    Store& store() { static Store s; return s; }
}

const Table& get (int index)
{
    index = juce::jlimit (0, slots - 1, index);
    auto& s = store();
    if (const auto* t = s.published[(size_t) index].load (std::memory_order_acquire))
        return *t;
    const juce::ScopedLock sl (s.lock);
    if (s.tables[(size_t) index] == nullptr)
    {
        std::vector<Spectrum> spectra;
        for (int f = 0; f < frames; ++f)
            spectra.push_back (frameSpectrum (index, (float) f / (float) (frames - 1)));
        const auto& names = tableNames();
        s.tables[(size_t) index] = build (spectra, index < (int) names.size() ? names[(size_t) index] : juce::String ("Table"));
        s.published[(size_t) index].store (s.tables[(size_t) index].get(), std::memory_order_release);
    }
    return *s.tables[(size_t) index];
}

const Table* ready (int index) noexcept
{
    if (index < 0 || index >= slots)
        return nullptr;
    return store().published[(size_t) index].load (std::memory_order_acquire);
}

void makeFromAudio (int index, const float* samples, int numSamples, int cycleLength)
{
    index = juce::jlimit (0, slots - 1, index);
    cycleLength = juce::jmax (16, cycleLength);
    const auto cycles = juce::jlimit (1, frames, numSamples / cycleLength);
    std::vector<Spectrum> spectra;
    for (int f = 0; f < frames; ++f)
    {
        const auto c = cycles == 1 ? 0 : juce::roundToInt ((float) f / (frames - 1) * (float) (cycles - 1));
        const auto* cycle = samples + (size_t) c * (size_t) cycleLength;
        spectra.push_back (shape ([cycle, cycleLength] (float x)
        {
            const auto p = x * (float) cycleLength;
            const auto i = std::min ((int) p, cycleLength - 1);
            const auto j = (i + 1) % cycleLength;
            return cycle[i] + (p - (float) i) * (cycle[j] - cycle[i]);
        }));
    }
    auto table = build (spectra, "Your Audio");
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.published[(size_t) index].store (table.get(), std::memory_order_release);
    if (s.tables[(size_t) index] != nullptr)
        s.retired.push_back (std::move (s.tables[(size_t) index]));
    s.tables[(size_t) index] = std::move (table);
}

void frameForDisplay (int index, float position, float* dest)
{
    const auto& t = get (index);
    for (int i = 0; i < frameSize; ++i)
        dest[i] = read (t, 0, position, (double) i / frameSize);
}

} // namespace riff::nemix::wt
