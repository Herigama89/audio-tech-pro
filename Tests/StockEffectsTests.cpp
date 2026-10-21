#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/engine/BuiltInEffects.h"
#include <set>

using namespace riff;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<BuiltInEffect> make (const juce::String& type)
    {
        auto e = BuiltInEffect::create (type);
        REQUIRE (e != nullptr);
        e->prepare (rate, block);
        return e;
    }

    void set (BuiltInEffect& e, const juce::String& id, float value)
    {
        const auto& params = e.getInfo().params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id)
            {
                e.setParameter ((int) i, value);
                return;
            }
        FAIL ("no parameter " << id);
    }

    /** Runs a signal through in blocks; returns the output (left, right interleaved by channel). */
    std::pair<std::vector<float>, std::vector<float>> run (BuiltInEffect& e, std::vector<float> l, std::vector<float> r)
    {
        for (size_t at = 0; at < l.size(); at += block)
        {
            const auto n = (int) std::min ((size_t) block, l.size() - at);
            e.process (l.data() + at, r.data() + at, n);
        }
        return { l, r };
    }

    std::vector<float> sine (double freq, double seconds, float amp)
    {
        std::vector<float> s ((size_t) (seconds * rate));
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) i / rate);
        return s;
    }

    float peakOf (const std::vector<float>& s, size_t from = 0)
    {
        float p = 0.0f;
        for (size_t i = from; i < s.size(); ++i)
            p = std::max (p, std::abs (s[i]));
        return p;
    }
}

TEST_CASE ("Every stock effect plays noise safely, at every setting's extremes", "[stockfx]")
{
    juce::Random random (7);
    for (const auto& info : builtInEffects())
    {
        INFO (info.name.toStdString());
        CHECK (info.category.isNotEmpty());
        CHECK (effectCategories().contains (info.category));
        CHECK ((int) info.params.size() <= BuiltInEffect::maxParams);
        for (const auto& p : info.params)
        {
            CHECK (p.min < p.max);
            CHECK (p.defaultValue >= p.min);
            CHECK (p.defaultValue <= p.max);
            if (p.isChoice())
                CHECK ((int) p.choices.size() == juce::roundToInt (p.max - p.min) + 1);
        }

        for (int setting = 0; setting < 3; ++setting)
        {
            auto e = make (info.type);
            for (size_t i = 0; i < info.params.size(); ++i)
            {
                const auto& p = info.params[i];
                e->setParameter ((int) i, setting == 0 ? p.defaultValue : setting == 1 ? p.min : p.max);
            }
            e->setPosition (0, true, 128.0);
            std::vector<float> l (24000), r (24000);
            for (size_t i = 0; i < l.size(); ++i)
            {
                l[i] = random.nextFloat() * 1.6f - 0.8f;
                r[i] = random.nextFloat() * 1.6f - 0.8f;
            }
            const auto [ol, orr] = run (*e, l, r);
            bool finite = true;
            float most = 0.0f;
            for (size_t i = 0; i < ol.size(); ++i)
            {
                finite = finite && std::isfinite (ol[i]) && std::isfinite (orr[i]);
                most = std::max ({ most, std::abs (ol[i]), std::abs (orr[i]) });
            }
            CHECK (finite);
            CHECK (most < 1.0e8f);   // (every EQ band at +24 dB and a resonant cut on one frequency is very loud, but nothing blows up)
        }
    }
}

TEST_CASE ("Stock EQs: flat at their defaults, and the curve matches what they play", "[stockfx]")
{
    for (const auto* type : { "channelEq", "graphicEq", "singleEq" })
    {
        INFO (type);
        const auto* info = findBuiltInEffect (type);
        REQUIRE (info != nullptr);
        REQUIRE (info->response != nullptr);
        std::array<float, BuiltInEffect::maxParams> v {};
        for (size_t i = 0; i < info->params.size(); ++i)
            v[i] = info->params[i].defaultValue;
        for (const auto f : { 50.0, 1000.0, 10000.0 })
            CHECK_THAT (info->response (v.data(), f, rate), Catch::Matchers::WithinAbs (0.0, 0.3));
    }

    // A +12 dB bell at 1 kHz: the curve says +12 there, and a 1 kHz tone comes out four times as loud
    auto eq = make ("channelEq");
    set (*eq, "p2Freq", 1000.0f);
    set (*eq, "p2Gain", 12.0f);
    const auto* info = findBuiltInEffect ("channelEq");
    std::array<float, BuiltInEffect::maxParams> v {};
    for (size_t i = 0; i < info->params.size(); ++i)
        v[i] = eq->getParameter ((int) i);
    CHECK_THAT (info->response (v.data(), 1000.0, rate), Catch::Matchers::WithinAbs (12.0, 0.2));
    const auto [l, r] = run (*eq, sine (1000.0, 0.5, 0.1f), sine (1000.0, 0.5, 0.1f));
    CHECK_THAT (peakOf (l, 12000), Catch::Matchers::WithinAbs (0.398, 0.01));

    // Its low cut takes 30 Hz away
    auto cut = make ("channelEq");
    set (*cut, "hpFreq", 200.0f);
    const auto [cl, cr] = run (*cut, sine (30.0, 1.0, 0.5f), sine (30.0, 1.0, 0.5f));
    CHECK (peakOf (cl, 24000) < 0.01f);
}

TEST_CASE ("Channel EQ: cut slopes, bands on and off, one-side processing, HQ", "[stockfx]")
{
    // An octave below the low cut: 6 dB/oct takes off about 6 dB, 48 dB/oct far more
    for (const auto [slope, lo, hi] : { std::tuple { 0.0f, 0.35f, 0.65f }, std::tuple { 5.0f, 0.0f, 0.02f } })
    {
        auto e = make ("channelEq");
        set (*e, "hpFreq", 400.0f);
        set (*e, "hpSlope", slope);
        const auto [l, r] = run (*e, sine (200.0, 1.0, 1.0f), sine (200.0, 1.0, 1.0f));
        CHECK (peakOf (l, 24000) >= lo);
        CHECK (peakOf (l, 24000) <= hi);
    }
    // A band switched off does nothing
    auto off = make ("channelEq");
    set (*off, "p2Freq", 1000.0f);
    set (*off, "p2Gain", 12.0f);
    set (*off, "on4", 0.0f);
    const auto [ol, orr] = run (*off, sine (1000.0, 0.5, 0.25f), sine (1000.0, 0.5, 0.25f));
    CHECK_THAT (peakOf (ol, 12000), Catch::Matchers::WithinAbs (0.25, 0.01));
    // Left only: the right channel passes untouched
    auto left = make ("channelEq");
    set (*left, "p2Freq", 1000.0f);
    set (*left, "p2Gain", 12.0f);
    set (*left, "processing", 1.0f);
    const auto [ll, lr] = run (*left, sine (1000.0, 0.5, 0.1f), sine (1000.0, 0.5, 0.1f));
    CHECK (peakOf (ll, 12000) > 0.3f);
    CHECK_THAT (peakOf (lr, 12000), Catch::Matchers::WithinAbs (0.1, 0.005));
    // HQ: the same boost, oversampled
    auto hq = make ("channelEq");
    set (*hq, "p2Freq", 1000.0f);
    set (*hq, "p2Gain", 12.0f);
    set (*hq, "hq", 1.0f);
    const auto [hl, hr] = run (*hq, sine (1000.0, 0.5, 0.1f), sine (1000.0, 0.5, 0.1f));
    CHECK_THAT (peakOf (hl, 12000), Catch::Matchers::WithinAbs (0.398, 0.03));
}

TEST_CASE ("Studio Compressor: every circuit turns a loud tone down; mix and auto gain", "[stockfx]")
{
    for (int model = 0; model < 7; ++model)
    {
        INFO ("model " << model);
        auto c = make ("studioComp");
        set (*c, "model", (float) model);
        set (*c, "threshold", -30.0f);
        set (*c, "ratio", 8.0f);
        const auto [l, r] = run (*c, sine (200.0, 1.0, 0.5f), sine (200.0, 1.0, 0.5f));
        CHECK (peakOf (l, 36000) < 0.25f);
        CHECK (c->takeGainReductionDb() > 6.0f);
    }

    // Mix at 0: untouched
    auto parallel = make ("studioComp");
    set (*parallel, "model", 0.0f);
    set (*parallel, "threshold", -40.0f);
    set (*parallel, "mix", 0.0f);
    const auto [pl, pr] = run (*parallel, sine (200.0, 0.5, 0.5f), sine (200.0, 0.5, 0.5f));
    CHECK_THAT (peakOf (pl, 12000), Catch::Matchers::WithinAbs (0.5, 0.01));
}

TEST_CASE ("Studio Compressor: circuits differ, the limiter holds its threshold, Listen plays the key", "[stockfx]")
{
    // The same settings sound different on each circuit (timing, detector, colour)
    std::vector<float> peaks;
    for (int model = 0; model < 7; ++model)
    {
        auto c = make ("studioComp");
        set (*c, "model", (float) model);
        set (*c, "threshold", -24.0f);
        set (*c, "ratio", 5.0f);
        const auto [l, r] = run (*c, sine (100.0, 0.5, 0.7f), sine (100.0, 0.5, 0.7f));
        peaks.push_back (peakOf (l, 12000));
    }
    std::sort (peaks.begin(), peaks.end());
    CHECK (peaks.back() - peaks.front() > 0.02f);

    // The limiter: nothing over its threshold
    auto lim = make ("studioComp");
    set (*lim, "threshold", 0.0f);
    set (*lim, "input", 12.0f);
    set (*lim, "limiter", 1.0f);
    set (*lim, "limThr", -6.0f);
    const auto [ll, lr] = run (*lim, sine (200.0, 0.5, 0.9f), sine (200.0, 0.5, 0.9f));
    CHECK (peakOf (ll) <= juce::Decibels::decibelsToGain (-6.0f) + 0.001f);

    // Listen: the side chain filter's output (a high pass takes the low tone away)
    auto listen = make ("studioComp");
    set (*listen, "scFilter", 1.0f);
    set (*listen, "scMode", 2.0f);
    set (*listen, "scHpf", 2000.0f);
    set (*listen, "listen", 1.0f);
    const auto [nl, nr] = run (*listen, sine (60.0, 0.5, 0.5f), sine (60.0, 0.5, 0.5f));
    CHECK (peakOf (nl, 12000) < 0.01f);
}

TEST_CASE ("Studio Reverb: the damping ratios set how long each band rings; freeze holds", "[stockfx]")
{
    const auto tailOf = [] (float highRatio)
    {
        auto e = make ("studioReverb");
        set (*e, "dry", 0.0f);
        set (*e, "mix", 100.0f);
        set (*e, "decay", 2.0f);
        set (*e, "dHsFreq", 3000.0f);
        set (*e, "dHsRatio", highRatio);
        auto burst = sine (9000.0, 0.05, 0.5f);
        burst.resize (72000, 0.0f);
        const auto [l, r] = run (*e, burst, burst);
        double energy = 0.0;
        for (size_t i = 48000; i < 72000; ++i) energy += (double) l[i] * l[i];
        return energy;
    };
    CHECK (tailOf (1.9f) > tailOf (0.2f) * 10.0);

    auto frozen = make ("studioReverb");
    set (*frozen, "dry", 0.0f);
    set (*frozen, "mix", 100.0f);
    run (*frozen, sine (500.0, 0.3, 0.5f), sine (500.0, 0.3, 0.5f));
    set (*frozen, "freeze", 1.0f);
    std::vector<float> silence (96000, 0.0f);
    const auto [fl, fr] = run (*frozen, silence, silence);
    CHECK (peakOf (fl, 90000) > 0.01f);
}

TEST_CASE ("Brickwall Limiter never goes over its ceiling, and reports its latency", "[stockfx]")
{
    auto lim = make ("brickwall");
    set (*lim, "gain", 18.0f);
    set (*lim, "ceiling", -1.0f);
    CHECK (lim->getLatencySamples() == 96);
    juce::Random random (3);
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = (random.nextFloat() * 2.0f - 1.0f) * (i % 4800 < 100 ? 1.0f : 0.2f);
        r[i] = l[i];
    }
    const auto [ol, orr] = run (*lim, l, r);
    CHECK (peakOf (ol) <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-5f);
    CHECK (peakOf (ol) > 0.8f);
}

TEST_CASE ("Noise Gate closes on quiet and opens on loud", "[stockfx]")
{
    auto gate = make ("noiseGate");
    set (*gate, "threshold", -30.0f);
    set (*gate, "range", -80.0f);
    auto quiet = sine (500.0, 0.5, 0.005f);   // -46 dB
    auto [ql, qr] = run (*gate, quiet, quiet);
    CHECK (peakOf (ql, 12000) < 0.0005f);
    auto loud = sine (500.0, 0.5, 0.5f);
    auto [ll, lr] = run (*gate, loud, loud);
    CHECK (peakOf (ll, 12000) > 0.45f);
}

TEST_CASE ("Delays: the echo arrives on time, in ms or in time with the song", "[stockfx]")
{
    // Echo, synced: a quarter note at 120 BPM is 0.5 s
    auto echo = make ("echo");
    set (*echo, "sync", 1.0f);
    set (*echo, "note", 9.0f);   // 1/4
    set (*echo, "mix", 100.0f);
    set (*echo, "dry", 0.0f);
    set (*echo, "repeats", 0.0f);
    set (*echo, "colour", 0.0f);
    echo->setPosition (0, true, 120.0);
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[100] = r[100] = 1.0f;
    const auto [el, er] = run (*echo, l, r);
    size_t at = 0;
    for (size_t i = 0; i < el.size(); ++i)
        if (std::abs (el[i]) > std::abs (el[at]))
            at = i;
    CHECK (std::abs ((int) at - (100 + 24000)) <= 3);

    // Sample Delay: exactly so many samples
    auto sd = make ("sampleDelay");
    set (*sd, "left", 37.0f);
    std::vector<float> a (1000, 0.0f), b (1000, 0.0f);
    a[10] = b[10] = 1.0f;
    const auto [sl, sr] = run (*sd, a, b);
    CHECK (sl[47] == 1.0f);
    CHECK (sr[10] == 1.0f);

    // Tape Delay's Freeze holds the repeats with nothing new coming in
    auto tape = make ("tapeDelay");
    set (*tape, "sync", 0.0f);
    set (*tape, "time", 100.0f);
    set (*tape, "dry", 0.0f);
    set (*tape, "mix", 100.0f);
    set (*tape, "wow", 0.0f);
    set (*tape, "flutter", 0.0f);
    const auto [t1, t2] = run (*tape, sine (300.0, 0.3, 0.5f), sine (300.0, 0.3, 0.5f));
    set (*tape, "freeze", 1.0f);
    std::vector<float> silence (48000, 0.0f);
    const auto [f1, f2] = run (*tape, silence, silence);
    CHECK (peakOf (f1, 40000) > 0.1f);

    // Stereo Delay, Ping Pong: the left echo comes back on the right
    auto pp = make ("stereoDelay");
    set (*pp, "sync", 0.0f);
    set (*pp, "timeL", 100.0f);
    set (*pp, "timeR", 100.0f);
    set (*pp, "mix", 100.0f);
    set (*pp, "routing", 5.0f);
    set (*pp, "fbL", 80.0f);
    set (*pp, "fbR", 80.0f);
    std::vector<float> click (24000, 0.0f), none (24000, 0.0f);
    click[0] = 1.0f;
    const auto [pl, pr] = run (*pp, click, none);
    CHECK (std::abs (pl[4800]) > 0.3f);   // (the mono sum, through the low and high cut)
    CHECK (std::abs (pr[9600]) > 0.2f);
}

TEST_CASE ("Reverbs ring on after a click; tremolo moves the level; bitcrusher steps", "[stockfx]")
{
    for (const auto* type : { "studioReverb", "plateReverb", "springReverb" })
    {
        INFO (type);
        auto rv = make (type);
        set (*rv, "mix", 100.0f);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[0] = r[0] = 1.0f;
        const auto [ol, orr] = run (*rv, l, r);
        float tail = 0.0f;
        for (size_t i = 9600; i < 24000; ++i)
            tail = std::max (tail, std::abs (ol[i]) + std::abs (orr[i]));
        CHECK (tail > 1.0e-4f);   // still sounding 200-500 ms later
        CHECK (tail < 2.0f);
    }
    // The newer three: a short burst (EnVerb's envelope starts on a new phrase)
    for (const auto* type : { "platinumVerb", "silverVerb", "enVerb" })
    {
        INFO (type);
        auto rv = make (type);
        set (*rv, "dry", 0.0f);
        set (*rv, "wet", 100.0f);
        auto l = sine (440.0, 0.05, 0.5f);
        l.resize (48000, 0.0f);
        const auto [ol, orr] = run (*rv, l, l);
        float tail = 0.0f;
        for (size_t i = 12000; i < 24000; ++i)
            tail = std::max (tail, std::abs (ol[i]) + std::abs (orr[i]));
        CHECK (tail > 1.0e-4f);
        CHECK (tail < 2.0f);
    }

    auto trem = make ("tremolo");
    set (*trem, "sync", 0.0f);
    set (*trem, "rate", 5.0f);
    set (*trem, "depth", 100.0f);
    std::vector<float> ones (48000, 0.5f);
    const auto [tl, tr] = run (*trem, ones, ones);
    float lo = 1.0f, hi = 0.0f;
    for (size_t i = 4800; i < tl.size(); ++i)
    {
        lo = std::min (lo, tl[i]);
        hi = std::max (hi, tl[i]);
    }
    CHECK (lo < 0.05f);
    CHECK (hi > 0.45f);

    auto crush = make ("bitcrusher");
    set (*crush, "bits", 2.0f);
    set (*crush, "downsample", 1.0f);
    const auto [cl, cr] = run (*crush, sine (100.0, 0.1, 0.9f), sine (100.0, 0.1, 0.9f));
    std::set<float> levels (cl.begin(), cl.end());
    CHECK (levels.size() <= 5);
}

namespace
{
    /** A voice-like buzz: a pulse made of 30 harmonics. */
    std::vector<float> buzz (double freq, double seconds, float amp)
    {
        std::vector<float> s ((size_t) (seconds * rate), 0.0f);
        for (int h = 1; h <= 30 && freq * h < rate * 0.45; ++h)
            for (size_t i = 0; i < s.size(); ++i)
                s[i] += amp / (float) h * (float) std::cos (juce::MathConstants<double>::twoPi * freq * h * (double) i / rate);
        return s;
    }

    /** Fundamental by autocorrelation (the strongest repeat between 50 and 1000 Hz). */
    double fundamentalOf (const std::vector<float>& s, size_t from, size_t length)
    {
        const auto minLag = (int) (rate / 1000.0), maxLag = (int) (rate / 50.0);
        double best = -1.0;
        int bestLag = 0;
        std::vector<double> corr ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag; lag <= maxLag + 1; ++lag)
        {
            double c = 0.0, e1 = 0.0, e2 = 0.0;
            for (size_t i = from; i < from + length; ++i)
            {
                c += s[i] * s[i + (size_t) lag];
                e1 += s[i] * s[i];
                e2 += s[i + (size_t) lag] * s[i + (size_t) lag];
            }
            corr[(size_t) lag] = c / std::sqrt (std::max (1.0e-12, e1 * e2));
        }
        // The first lag that comes within 5% of the best (so an octave below isn't taken for it)
        for (int lag = minLag; lag <= maxLag; ++lag)
            best = std::max (best, corr[(size_t) lag]);
        for (int lag = minLag + 1; lag <= maxLag; ++lag)
            if (corr[(size_t) lag] > best * 0.95 && corr[(size_t) lag] >= corr[(size_t) lag - 1] && corr[(size_t) lag] >= corr[(size_t) lag + 1])
            {
                bestLag = lag;
                break;
            }
        return bestLag > 0 ? rate / bestLag : 0.0;
    }

    /** Frequency from upward zero crossings over a stretch. */
    double frequencyOf (const std::vector<float>& s, size_t from, size_t to)
    {
        int crossings = 0;
        double first = -1.0, last = 0.0;
        for (size_t i = from + 1; i < to; ++i)
            if (s[i - 1] < 0.0f && s[i] >= 0.0f)
            {
                const auto t = (double) (i - 1) + s[i - 1] / (s[i - 1] - s[i]);
                if (first < 0.0) first = t; else ++crossings;
                last = t;
            }
        return crossings > 0 ? crossings / ((last - first) / rate) : 0.0;
    }
}

TEST_CASE ("Pitch: correction pulls a sharp note onto the key, the transformer and shifter move it", "[stockfx]")
{
    // 452 Hz (A4 + 47 cents) comes out at 440 Hz
    auto pc = make ("pitchCorrection");
    set (*pc, "scale", 0.0f);   // chromatic
    set (*pc, "response", 5.0f);
    CHECK (pc->getLatencySamples() > 0);
    auto [cl, cr] = run (*pc, sine (452.0, 1.5, 0.4f), sine (452.0, 1.5, 0.4f));
    CHECK_THAT (frequencyOf (cl, 36000, 72000), Catch::Matchers::WithinAbs (440.0, 3.0));

    // A voice-like buzz an octave up, a fifth up, and an octave down
    for (const auto [semitones, expected] : { std::pair { 12.0f, 440.0 }, std::pair { 7.0f, 329.6 }, std::pair { -12.0f, 110.0 } })
    {
        INFO ("shift " << semitones);
        auto vt = make ("vocalTransformer");
        set (*vt, "pitch", semitones);
        auto [vl, vr] = run (*vt, buzz (220.0, 1.5, 0.05f), buzz (220.0, 1.5, 0.05f));
        CHECK_THAT (fundamentalOf (vl, 36000, 4800), Catch::Matchers::WithinRel (expected, 0.03));
        CHECK (peakOf (vl, 36000) > 0.05f);
    }

    // The granular shifter: 7 semitones up
    auto ps = make ("pitchShifter");
    set (*ps, "semitones", 7.0f);
    auto [sl, sr] = run (*ps, sine (300.0, 1.0, 0.4f), sine (300.0, 1.0, 0.4f));
    CHECK_THAT (frequencyOf (sl, 12000, 48000), Catch::Matchers::WithinAbs (300.0 * std::pow (2.0, 7.0 / 12.0), 8.0));
}

TEST_CASE ("Utility and specialized: gain, oscillator, denoiser, vocoder", "[stockfx]")
{
    auto gain = make ("gain");
    set (*gain, "invL", 1.0f);
    set (*gain, "gain", -6.0f);
    std::vector<float> ones (4800, 0.5f);
    auto [gl, gr] = run (*gain, ones, ones);
    CHECK_THAT (gl.back(), Catch::Matchers::WithinAbs (-0.25, 0.01));
    CHECK_THAT (gr.back(), Catch::Matchers::WithinAbs (0.25, 0.01));

    auto osc = make ("testOsc");
    set (*osc, "level", -6.0f);
    auto [ol, orr] = run (*osc, std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f));
    CHECK_THAT (peakOf (ol), Catch::Matchers::WithinAbs (0.5, 0.01));
    CHECK_THAT (frequencyOf (ol, 0, 48000), Catch::Matchers::WithinAbs (1000.0, 1.0));

    // Denoiser: a loud tone passes; quiet noise is turned down
    auto dn = make ("denoiser");
    set (*dn, "threshold", -50.0f);
    set (*dn, "reduction", 30.0f);
    auto [tl, tr] = run (*dn, sine (1000.0, 1.0, 0.5f), sine (1000.0, 1.0, 0.5f));
    CHECK (peakOf (tl, 24000) > 0.4f);
    juce::Random random (9);
    std::vector<float> hiss (48000);
    for (auto& x : hiss) x = (random.nextFloat() * 2.0f - 1.0f) * 0.001f;
    auto dn2 = make ("denoiser");
    set (*dn2, "threshold", -50.0f);
    set (*dn2, "reduction", 30.0f);
    auto [nl, nr] = run (*dn2, hiss, hiss);
    CHECK (peakOf (nl, 24000) < 0.0005f);

    // Vocoder with its own saw: a voice-like input comes out
    auto voc = make ("vocoder");
    auto [vl, vr] = run (*voc, sine (300.0, 0.5, 0.3f), sine (300.0, 0.5, 0.3f));
    CHECK (peakOf (vl, 12000) > 0.01f);
}


TEST_CASE ("Delay Designer's taps and Sub Bass's octave", "[stockfx]")
{
    auto dd = make ("delayDesigner");
    set (*dd, "mix", 100.0f);
    set (*dd, "feedback", 0.0f);
    set (*dd, "spread", 0.0f);
    for (int t = 2; t <= 6; ++t)
        set (*dd, "level" + juce::String (t), -60.0f);
    set (*dd, "time1", 200.0f);
    set (*dd, "level1", 0.0f);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    l[0] = r[0] = 1.0f;
    const auto [ol, orr] = run (*dd, l, r);
    size_t at = 0;
    for (size_t i = 0; i < ol.size(); ++i)
        if (std::abs (ol[i]) > std::abs (ol[at]))
            at = i;
    CHECK (std::abs ((int) at - 9600) <= 2);

    auto sub = make ("subBass");
    set (*sub, "dry", -60.0f);
    set (*sub, "shape", 1.0f);
    const auto [sl, sr] = run (*sub, sine (100.0, 1.0, 0.5f), sine (100.0, 1.0, 0.5f));
    CHECK_THAT (frequencyOf (sl, 12000, 48000), Catch::Matchers::WithinAbs (50.0, 1.0));
}

TEST_CASE ("Ringshifter shifts every frequency by the same amount; Scanner Vibrato and Rotor Cabinet play", "[stockfx]")
{
    // 1 kHz shifted up 200 Hz (not a pitch shift: every partial moves by 200 Hz)
    auto shift = make ("ringMod");
    set (*shift, "mode", 0.0f);
    set (*shift, "freq", 200.0f);
    set (*shift, "mix", 100.0f);
    const auto [sl, sr] = run (*shift, sine (1000.0, 1.0, 0.5f), sine (1000.0, 1.0, 0.5f));
    CHECK_THAT (frequencyOf (sl, 4800, 48000), Catch::Matchers::WithinAbs (1200.0, 5.0));
    // Dual: the right goes down
    set (*shift, "mode", 1.0f);
    const auto [dl, dr] = run (*shift, sine (1000.0, 1.0, 0.5f), sine (1000.0, 1.0, 0.5f));
    CHECK_THAT (frequencyOf (dr, 4800, 48000), Catch::Matchers::WithinAbs (800.0, 5.0));

    for (const auto* type : { "scannerVibrato", "rotary", "ensemble", "microphaser" })
    {
        INFO (type);
        auto e = make (type);
        const auto [l, r] = run (*e, sine (440.0, 0.5, 0.5f), sine (440.0, 0.5, 0.5f));
        CHECK (peakOf (l, 12000) > 0.05f);
        CHECK (peakOf (l, 12000) < 2.0f);
    }
}

TEST_CASE ("Dynamics: ducker, upward expander, adaptive limiter, multipressor solo", "[stockfx]")
{
    // Ducker: a loud key turns it down
    auto duck = make ("noiseGate");
    set (*duck, "gateMode", 1.0f);
    set (*duck, "threshold", -30.0f);
    set (*duck, "range", -20.0f);
    const auto [dl, dr] = run (*duck, sine (500.0, 0.5, 0.5f), sine (500.0, 0.5, 0.5f));
    CHECK (peakOf (dl, 12000) < 0.06f);

    // Upward expander: above the threshold, louder
    auto up = make ("expander");
    set (*up, "threshold", -30.0f);
    set (*up, "ratio", 2.0f);
    set (*up, "autoGain", 0.0f);
    set (*up, "knee", 0.0f);
    const auto [ul, ur] = run (*up, sine (500.0, 0.5, 0.1f), sine (500.0, 0.5, 0.1f));   // -20 dB in: 10 dB over
    CHECK (peakOf (ul, 12000) > 0.25f);

    // Adaptive limiter: never over its ceiling
    auto adaptive = make ("adaptiveLimiter");
    set (*adaptive, "gain", 12.0f);
    set (*adaptive, "ceiling", -1.0f);
    const auto [al, ar] = run (*adaptive, sine (200.0, 0.5, 0.9f), sine (200.0, 0.5, 0.9f));
    CHECK (peakOf (al) <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-5f);

    // Multipressor: soloing the high band takes the low tone away
    auto multi = make ("multiband");
    set (*multi, "b4Solo", 1.0f);
    const auto [ml, mr] = run (*multi, sine (80.0, 0.5, 0.5f), sine (80.0, 0.5, 0.5f));
    CHECK (peakOf (ml, 12000) < 0.02f);
    set (*multi, "b4Solo", 0.0f);
    const auto [fl, fr] = run (*multi, sine (80.0, 0.5, 0.1f), sine (80.0, 0.5, 0.1f));
    CHECK_THAT (peakOf (fl, 12000), Catch::Matchers::WithinAbs (0.1, 0.02));
}

TEST_CASE ("Filters: AutoFilter 4-pole slopes, Fuzz-Wah, Spectral Gate and the Formant Filterbank play", "[stockfx]")
{
    // A steeper low pass takes more off an octave above the cutoff
    float last = 1.0f;
    for (const auto type : { 3.0f, 4.0f, 6.0f })
    {
        auto f = make ("autoFilter");
        set (*f, "type", type);
        set (*f, "cutoff", 500.0f);
        set (*f, "resonance", 0.0f);
        set (*f, "lfoOn", 0.0f);
        const auto [l, r] = run (*f, sine (2000.0, 0.5, 0.5f), sine (2000.0, 0.5, 0.5f));
        const auto peak = peakOf (l, 12000);
        CHECK (peak < last);
        last = peak;
    }
    for (const auto* type : { "fuzzWah", "spectralGate", "formantBank" })
    {
        INFO (type);
        auto e = make (type);
        const auto [l, r] = run (*e, sine (700.0, 0.5, 0.5f), sine (700.0, 0.5, 0.5f));
        CHECK (peakOf (l, 12000) > 0.005f);
        CHECK (peakOf (l, 12000) < 4.0f);
    }
}
