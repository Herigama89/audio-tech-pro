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

    // Bypass Notes: A left alone (452 Hz stays 452); Tolerance wide enough does the same
    for (const auto* setting : { "bp10", "tolerance" })
    {
        INFO (setting);
        auto by = make ("pitchCorrection");
        set (*by, "scale", 0.0f);
        set (*by, "response", 5.0f);
        set (*by, setting, juce::String (setting) == "tolerance" ? 60.0f : 1.0f);
        auto [bl, br] = run (*by, sine (452.0, 1.5, 0.4f), sine (452.0, 1.5, 0.4f));
        CHECK_THAT (frequencyOf (bl, 36000, 72000), Catch::Matchers::WithinAbs (452.0, 3.0));
    }
    // Edit Scale: A left out, so 452 Hz goes to A# (466 Hz)
    auto edited = make ("pitchCorrection");
    set (*edited, "scale", 0.0f);
    set (*edited, "response", 5.0f);
    set (*edited, "n10", 0.0f);
    auto [el, er] = run (*edited, sine (452.0, 1.5, 0.4f), sine (452.0, 1.5, 0.4f));
    CHECK_THAT (frequencyOf (el, 36000, 72000), Catch::Matchers::WithinAbs (466.2, 4.0));

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
    // One tap at 200 ms, the rest silent: the impulse comes back 200 ms later
    auto dd = make ("delayDesigner");
    set (*dd, "dry", 0.0f);
    set (*dd, "mix", 100.0f);
    set (*dd, "fbOn", 0.0f);
    set (*dd, "tapCount", 0.0f);
    set (*dd, "time1", 200.0f);
    set (*dd, "level1", 0.0f);
    set (*dd, "pan1", 0.0f);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    l[0] = r[0] = 1.0f;
    const auto [ol, orr] = run (*dd, l, r);
    size_t at = 0;
    for (size_t i = 0; i < ol.size(); ++i)
        if (std::abs (ol[i]) > std::abs (ol[at]))
            at = i;
    CHECK (std::abs ((int) at - 9600) <= 2);
    CHECK (std::abs (ol[at]) > 0.9f);

    // Synced to a 1/8 grid at 120 BPM: 230 ms snaps to 250 ms; swing pushes the odd step later
    std::array<float, BuiltInEffect::meterCapacity> m {};
    dd->setPosition (0, true, 120.0);
    set (*dd, "sync", 1.0f);
    set (*dd, "grid", 1.0f);
    set (*dd, "time1", 230.0f);
    dd->readMeters (m.data(), (int) m.size());
    CHECK_THAT (m[0], Catch::Matchers::WithinAbs (250.0, 0.01));
    set (*dd, "swing", 75.0f);
    dd->readMeters (m.data(), (int) m.size());
    CHECK_THAT (m[0], Catch::Matchers::WithinAbs (375.0, 0.01));
    set (*dd, "sync", 0.0f);

    // A tap transposed up a fifth plays the input a fifth higher
    set (*dd, "time1", 50.0f);
    set (*dd, "transp1", 7.0f);
    const auto [pl, pr] = run (*dd, sine (300.0, 1.0, 0.5f), sine (300.0, 1.0, 0.5f));
    CHECK_THAT (frequencyOf (pl, 12000, 48000), Catch::Matchers::WithinAbs (300.0 * std::pow (2.0, 7.0 / 12.0), 8.0));
    set (*dd, "transp1", 0.0f);

    // Its filter: a high pass at 2 kHz takes a 100 Hz tone down
    set (*dd, "filter1", 1.0f);
    set (*dd, "hp1", 2000.0f);
    const auto [fl, fr] = run (*dd, sine (100.0, 0.5, 0.5f), sine (100.0, 0.5, 0.5f));
    CHECK (peakOf (fl, 12000) < 0.05f);
    set (*dd, "filter1", 0.0f);

    // Pan hard left: nothing on the right; Flip: the other way round
    set (*dd, "pan1", -100.0f);
    const auto [hl, hr] = run (*dd, sine (300.0, 0.3, 0.5f), sine (300.0, 0.3, 0.5f));
    CHECK (peakOf (hr, 4800) < 0.001f);
    CHECK (peakOf (hl, 4800) > 0.4f);

    // Feedback from the tap: the echo repeats, and decays
    set (*dd, "pan1", 0.0f);
    set (*dd, "time1", 100.0f);
    set (*dd, "fbOn", 1.0f);
    set (*dd, "fbTap", 0.0f);
    set (*dd, "feedback", 50.0f);
    auto fresh = make ("delayDesigner");
    for (const auto& [id, val] : std::vector<std::pair<const char*, float>> { { "dry", 0.0f }, { "mix", 100.0f }, { "tapCount", 0.0f }, { "time1", 100.0f },
                                                                              { "level1", 0.0f }, { "pan1", 0.0f }, { "fbOn", 1.0f }, { "fbTap", 0.0f }, { "feedback", 50.0f } })
        set (*fresh, id, val);
    const auto [el, er] = run (*fresh, l, r);
    CHECK_THAT (el[9600], Catch::Matchers::WithinAbs (0.5, 0.05));
    CHECK_THAT (el[14400], Catch::Matchers::WithinAbs (0.25, 0.05));

    // All 26 taps at full: finite and bounded
    auto all = make ("delayDesigner");
    set (*all, "tapCount", 25.0f);
    for (int t = 1; t <= 26; ++t)
    {
        set (*all, "transp" + juce::String (t), (float) (t % 5) * 3.0f - 6.0f);
        set (*all, "filter" + juce::String (t), 1.0f);
        set (*all, "reso" + juce::String (t), 100.0f);
    }
    set (*all, "feedback", 100.0f);
    const auto [al, ar] = run (*all, sine (220.0, 3.0, 0.5f), sine (220.0, 3.0, 0.5f));
    for (auto x : al) REQUIRE (std::isfinite (x));
    CHECK (peakOf (al) < 50.0f);

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

TEST_CASE ("Space Designer plays a sampled impulse response from a file", "[stockfx]")
{
    // An IR with a single tap 100 ms in
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-test-ir.wav");
    {
        juce::AudioBuffer<float> ir (2, 9600);
        ir.clear();
        ir.setSample (0, 4800, 1.0f);
        ir.setSample (1, 4800, 1.0f);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    }
    auto e = make ("convSpace");
    set (*e, "mode", 0.0f);
    set (*e, "predelay", 0.0f);
    set (*e, "direct", -60.0f);
    set (*e, "mix", 100.0f);
    set (*e, "lowCut", 10.0f);
    e->setAttachedFile (file.getFullPathName());
    // (the convolution takes its new impulse response in the background)
    std::vector<float> silence (4800, 0.0f);
    for (int k = 0; k < 40; ++k)
    {
        run (*e, silence, silence);
        juce::Thread::sleep (5);
    }
    std::vector<float> click (24000, 0.0f);
    click[0] = 1.0f;
    const auto [l, r] = run (*e, click, click);
    size_t at = 0;
    for (size_t i = 0; i < l.size(); ++i)
        if (std::abs (l[i]) > std::abs (l[at])) at = i;
    CHECK (std::abs ((int) at - 4800) < 64);
    file.deleteFile();
}

TEST_CASE ("Pedalboard plays its pedals in a row; Amp Designer and Bass Amp Designer models all play", "[stockfx]")
{
    auto board = make ("pedalboard");
    const auto [l, r] = run (*board, sine (220.0, 0.5, 0.4f), sine (220.0, 0.5, 0.4f));
    CHECK (peakOf (l, 12000) > 0.01f);
    CHECK (peakOf (l, 12000) < 4.0f);
    // Every pedal type, on its own
    for (int type = 0; type < 23; ++type)
    {
        INFO ("pedal " << type);
        auto one = make ("pedalboard");
        set (*one, "p1Type", (float) type);
        set (*one, "p1On", 1.0f);
        for (int s = 2; s <= 6; ++s) set (*one, "p" + juce::String (s) + "On", 0.0f);
        one->messageThreadUpdate();
        const auto [a, b] = run (*one, sine (330.0, 0.3, 0.4f), sine (330.0, 0.3, 0.4f));
        CHECK (std::isfinite (peakOf (a)));
        CHECK (peakOf (a, 4800) < 8.0f);
    }
    for (int model = 0; model < 24; ++model)
    {
        INFO ("amp " << model);
        auto amp = make ("guitarAmp");
        set (*amp, "model", (float) model);
        set (*amp, "revOn", 1.0f);
        set (*amp, "fxOn", 1.0f);
        const auto [a, b] = run (*amp, sine (110.0, 0.3, 0.3f), sine (110.0, 0.3, 0.3f));
        CHECK (peakOf (a, 4800) > 0.005f);
        CHECK (peakOf (a, 4800) < 4.0f);
    }
    for (int model = 0; model < 3; ++model)
    {
        auto bass = make ("bassAmp");
        set (*bass, "model", (float) model);
        set (*bass, "compOn", 1.0f);
        set (*bass, "addEqOn", 1.0f);
        const auto [a, b] = run (*bass, sine (55.0, 0.3, 0.3f), sine (55.0, 0.3, 0.3f));
        CHECK (peakOf (a, 4800) > 0.005f);
        CHECK (peakOf (a, 4800) < 4.0f);
    }
}

TEST_CASE ("Meters: loudness to EBU R128, tuner, mastering assistant stays under -1 dBTP", "[stockfx]")
{
    // A 1 kHz sine at -23 dBFS on both channels reads -23 LUFS
    auto loud = make ("loudnessMeter");
    const auto amp = juce::Decibels::decibelsToGain (-23.0f);
    run (*loud, sine (1000.0, 4.0, amp), sine (1000.0, 4.0, amp));
    std::array<float, BuiltInEffect::meterCapacity> m {};
    REQUIRE (loud->readMeters (m.data(), (int) m.size()) >= 5);
    CHECK_THAT (m[2], Catch::Matchers::WithinAbs (-23.0, 0.5));   // integrated
    CHECK_THAT (m[0], Catch::Matchers::WithinAbs (-23.0, 0.5));   // momentary

    auto tuner = make ("tuner");
    run (*tuner, sine (440.0, 1.0, 0.5f), sine (440.0, 1.0, 0.5f));
    tuner->readMeters (m.data(), (int) m.size());
    CHECK (m[3] > 0.5f);
    CHECK (juce::roundToInt (m[1]) == 69);
    CHECK (std::abs (m[2]) < 5.0f);

    auto master = make ("masteringAssistant");
    master->setPosition (0, true, 120.0);
    set (*master, "loudness", -8.0f);
    juce::Random random (5);
    std::vector<float> l (48000 * 3), r (48000 * 3);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = (random.nextFloat() * 2.0f - 1.0f) * ((i / 4800) % 2 == 0 ? 0.9f : 0.2f);
        r[i] = l[i] * 0.9f;
    }
    run (*master, l, r);
    master->readMeters (m.data(), (int) m.size());
    CHECK (m[2] <= -0.8f);   // true peak, dBTP
}

TEST_CASE ("Match EQ learns two spectra and makes the filter between them", "[stockfx]")
{
    const auto gainAt = [] (BuiltInEffect& e, double freq)
    {
        const auto [l, r] = run (e, sine (freq, 1.0, 0.1f), sine (freq, 1.0, 0.1f));
        return juce::Decibels::gainToDecibels (peakOf (l, 24000) / 0.1f);
    };

    // A template brighter by 1 dB per band above 1 kHz than the (flat) material: the filter lifts the top
    for (const auto phase : { 0.0f, 1.0f })
    {
        auto eq = make ("matchEq");
        set (*eq, "phase", phase);
        set (*eq, "smoothing", 0.0f);
        for (int k = 1; k <= 31; ++k)
        {
            set (*eq, "c" + juce::String (k), -40.0f);
            set (*eq, "t" + juce::String (k), -40.0f + (float) std::max (0, k - 18));
        }
        // (the convolution takes up a new impulse response on a background thread: give it time, playing silence)
        const auto settle = [] (BuiltInEffect& e)
        {
            for (int k = 0; k < 60; ++k)
            {
                juce::Thread::sleep (20);
                std::vector<float> zl (block, 0.0f), zr (block, 0.0f);
                e.process (zl.data(), zr.data(), block);
            }
        };
        eq->prepare (rate, block);   // (rebuilds now)
        settle (*eq);
        CHECK (eq->getLatencySamples() == (phase < 0.5f ? 2048 : 0));
        const auto low = gainAt (*eq, 200.0), high = gainAt (*eq, 10000.0);
        CHECK (high - low > 8.0f);
        CHECK (high - low < 13.0f);

        // Apply at 0 %: flat
        set (*eq, "apply", 0.0f);
        eq->prepare (rate, block);
        settle (*eq);
        CHECK_THAT (gainAt (*eq, 200.0) - gainAt (*eq, 10000.0), Catch::Matchers::WithinAbs (0.0, 0.5));
    }

    // Learning: white noise gives a spectrum rising 1 dB per third octave (equal energy per Hz)
    auto learner = make ("matchEq");
    set (*learner, "learnT", 1.0f);
    juce::Random random (3);
    std::vector<float> l (48000 * 2), r (48000 * 2);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = random.nextFloat() * 0.5f - 0.25f;
    run (*learner, l, r);
    std::array<float, BuiltInEffect::meterCapacity> m {};
    REQUIRE (learner->readMeters (m.data(), (int) m.size()) == 96);
    CHECK (m[62] > 1.9f);
    CHECK_THAT (m[27] - m[17], Catch::Matchers::WithinAbs (9.5, 2.0));
    CHECK (m[31] <= -119.0f);   // (the current wasn't learned)
}

TEST_CASE ("Step FX gates in time with the song; Phat FX's modulators and order", "[stockfx]")
{
    // Step FX's first modulator gates the sound: 1/16 steps at 120 BPM, every fourth step a rest
    auto step = make ("stepFx");
    set (*step, "fltOn", 0.0f);
    step->setPosition (0, false, 120.0);   // (stopped: the steps run on their own from the start)
    const auto [sl, sr] = run (*step, sine (440.0, 1.0, 0.5f), sine (440.0, 1.0, 0.5f));
    const auto windowPeak = [] (const std::vector<float>& s, double from, double to)
    {
        float p = 0.0f;
        for (auto i = (size_t) (from * rate); i < (size_t) (to * rate); ++i)
            p = std::max (p, std::abs (s[i]));
        return p;
    };
    CHECK (windowPeak (sl, 0.01, 0.1) > 0.4f);     // step 1: full
    CHECK (windowPeak (sl, 0.39, 0.49) < 0.02f);   // step 4: a rest
    CHECK (windowPeak (sl, 0.51, 0.6) > 0.4f);     // step 5: on the beat again
    for (auto x : sl) REQUIRE (std::isfinite (x));

    // ...and its depth at 0 % leaves it alone
    set (*step, "s1Depth", 0.0f);
    const auto [ul, ur] = run (*step, sine (440.0, 1.0, 0.5f), sine (440.0, 1.0, 0.5f));
    CHECK (windowPeak (ul, 0.39, 0.49) > 0.4f);

    // Phat FX: an LFO on the filter cutoff makes the level of a bright tone move
    auto phat = make ("phatFx");
    set (*phat, "cOn", 0.0f);
    set (*phat, "dOn", 0.0f);
    set (*phat, "fType", 1.0f);
    set (*phat, "fCutoff", 1000.0f);
    set (*phat, "fRes", 0.0f);
    set (*phat, "l1On", 1.0f);
    set (*phat, "l1Sync", 0.0f);
    set (*phat, "l1Rate", 2.0f);
    set (*phat, "l1Depth", 100.0f);
    const auto [pl, pr] = run (*phat, sine (3000.0, 1.0, 0.3f), sine (3000.0, 1.0, 0.3f));
    float lowest = 1.0f, highest = 0.0f;
    for (int w = 1; w < 20; ++w)
    {
        const auto peak = windowPeak (pl, w * 0.05, w * 0.05 + 0.02);
        lowest = std::min (lowest, peak);
        highest = std::max (highest, peak);
    }
    CHECK (highest > lowest * 3.0f);

    // The order: distortion then a low pass is darker than a low pass then distortion
    const auto brightness = [] (const std::vector<float>& s)
    {
        double diff = 0.0, level = 0.0;
        for (size_t i = 1; i < s.size(); ++i) { diff += juce::square (s[i] - s[i - 1]); level += juce::square (s[i]); }
        return diff / std::max (1.0e-9, level);
    };
    const auto ordered = [&] (bool distortionFirst)
    {
        auto fx = make ("phatFx");
        set (*fx, "cOn", 0.0f);
        set (*fx, "fType", 1.0f);
        set (*fx, "fCutoff", 400.0f);
        set (*fx, "fRes", 0.0f);
        set (*fx, "dType1", 7.0f);
        set (*fx, "dAmt1", 100.0f);
        set (*fx, "slot1", distortionFirst ? 2.0f : 1.0f);
        set (*fx, "slot2", distortionFirst ? 1.0f : 2.0f);
        set (*fx, "slot3", 0.0f);
        return brightness (run (*fx, sine (200.0, 0.5, 0.8f), sine (200.0, 0.5, 0.8f)).first);
    };
    CHECK (ordered (true) < ordered (false));

    // Every unit and modulator on, extreme settings: stays finite
    auto all = make ("phatFx");
    for (const auto* id : { "bpOn", "mOn", "bOn", "l2On", "eOn", "xOn", "yOn" })
        set (*all, id, 1.0f);
    set (*all, "bType", 2.0f);
    set (*all, "fRes", 100.0f);
    set (*all, "dAmt2", 100.0f);
    set (*all, "dAmt3", 100.0f);
    set (*all, "xT1", 7.0f);
    set (*all, "input", 24.0f);
    const auto [al, ar] = run (*all, sine (110.0, 1.0, 0.9f), sine (110.0, 1.0, 0.9f));
    for (auto x : al) REQUIRE (std::isfinite (x));
    CHECK (peakOf (al) < 4.0f);
}

TEST_CASE ("Remix FX: gater, tape stop, repeater, reverse", "[stockfx]")
{
    const auto windowPeak = [] (const std::vector<float>& s, double from, double to)
    {
        float p = 0.0f;
        for (auto i = (size_t) (from * rate); i < (size_t) (to * rate); ++i)
            p = std::max (p, std::abs (s[i]));
        return p;
    };
    // Gater fully up at 1/8 (120 BPM: 0.25 s): on for the first half, off for the second
    auto gater = make ("remixFx");
    gater->setPosition (0, false, 120.0);
    set (*gater, "gater", 100.0f);
    set (*gater, "gaterRate", 3.0f);
    const auto [gl, gr] = run (*gater, sine (300.0, 1.0, 0.5f), sine (300.0, 1.0, 0.5f));
    CHECK (windowPeak (gl, 0.27, 0.36) > 0.45f);
    CHECK (windowPeak (gl, 0.40, 0.49) < 0.02f);

    // Tape Stop over a half note: slowing, then silent
    auto stop = make ("remixFx");
    stop->setPosition (0, false, 120.0);
    set (*stop, "tapeStop", 1.0f);
    set (*stop, "tapeStopDiv", 1.0f);
    const auto [tl, tr] = run (*stop, sine (300.0, 2.0, 0.5f), sine (300.0, 2.0, 0.5f));
    CHECK (windowPeak (tl, 0.0, 0.2) > 0.3f);
    CHECK (windowPeak (tl, 1.1, 1.9) < 0.001f);
    for (auto x : tl) REQUIRE (std::isfinite (x));

    // Repeater on a pad: what came just before it was touched, over and over (a tone that then stops keeps sounding)
    auto rep = make ("remixFx");
    rep->setPosition (0, false, 120.0);
    set (*rep, "padAEffect", 1.0f);
    set (*rep, "padAX", 0.3f);
    set (*rep, "padAY", 1.0f);
    auto input = sine (300.0, 1.0, 0.5f);
    std::fill (input.begin() + 24000, input.end(), 0.0f);
    std::vector<float> l1 (input.begin(), input.begin() + 24000), r1 = l1;
    run (*rep, l1, r1);
    set (*rep, "padAOn", 1.0f);
    std::vector<float> l2 (input.begin() + 24000, input.end()), r2 = l2;
    const auto [ol, orr] = run (*rep, l2, r2);
    CHECK (windowPeak (ol, 0.1, 0.4) > 0.3f);

    // Reverse plays and stays finite
    auto rev = make ("remixFx");
    set (*rev, "reverse", 1.0f);
    const auto [vl, vr] = run (*rev, sine (300.0, 1.0, 0.5f), sine (300.0, 1.0, 0.5f));
    for (auto x : vl) REQUIRE (std::isfinite (x));
}

TEST_CASE ("Beat Breaker: straight is transparent, slices replay and reverse", "[stockfx]")
{
    // The default pattern (each slice playing from where it sits) passes the sound through
    auto bb = make ("beatBreaker");
    bb->setPosition (0, false, 120.0);
    set (*bb, "declick", 0.0f);
    auto input = sine (220.0, 3.0, 0.5f);
    const auto [ol, orr] = run (*bb, input, input);
    float worst = 0.0f;
    for (size_t i = 0; i < ol.size(); ++i)
        worst = std::max (worst, std::abs (ol[i] - input[i]));
    CHECK (worst < 1.0e-3f);

    // Slice 2 (from the 2nd sixteenth) plays the start of the pattern again: a click at the top comes back 1/8 later
    auto rep = make ("beatBreaker");
    rep->setPosition (0, false, 120.0);
    set (*rep, "declick", 0.0f);
    set (*rep, "in2", 0.0f);
    std::vector<float> click (48000 * 2, 0.0f);
    click[0] = 1.0f;
    const auto [cl, cr] = run (*rep, click, click);
    const auto eighth = (size_t) (0.25 * rate * 0.5 * 2.0);   // 1/8 at 120 BPM = 0.25 s
    CHECK (std::abs (cl[eighth]) > 0.9f);

    // Speed -100 %: a rising ramp in the slice comes out falling
    auto rev = make ("beatBreaker");
    rev->setPosition (0, false, 120.0);
    set (*rev, "declick", 0.0f);
    set (*rev, "sliceCount", 0.0f);   // one slice, the whole pattern
    set (*rev, "speed1", -100.0f);
    set (*rev, "in1", 32.0f);         // from the middle of the (4 beat) pattern: the 2nd half of the pattern before, backwards
    std::vector<float> ramp (48000 * 4);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) (i % 96000) / 96000.0f;
    const auto [rl, rr] = run (*rev, ramp, ramp);
    CHECK (rl[96000 + 1000] > rl[96000 + 20000]);
    for (auto x : rl) REQUIRE (std::isfinite (x));
}

TEST_CASE ("Room Simulator: its reverb time, freeze, both models", "[stockfx]")
{
    // The decay time, measured from an impulse (Schroeder's backward integral, -5 to -25 dB)
    const auto measuredRt = [] (const std::vector<float>& s)
    {
        std::vector<double> energy (s.size());
        double sum = 0.0;
        for (size_t i = s.size(); i-- > 0;) { sum += (double) s[i] * s[i]; energy[i] = sum; }
        const auto db = [&] (size_t i) { return 10.0 * std::log10 (energy[i] / energy[0] + 1.0e-30); };
        size_t a = 0, b = 0;
        while (a < s.size() && db (a) > -5.0) ++a;
        b = a;
        while (b < s.size() && db (b) > -25.0) ++b;
        return (double) (b - a) / rate * 3.0;
    };
    auto room = make ("roomSim");
    set (*room, "dryLevel", -60.0f);
    set (*room, "refLevel", -60.0f);
    set (*room, "reverbLevel", 0.0f);
    set (*room, "lowMult", 1.0f);
    set (*room, "highMult", 1.0f);
    set (*room, "reverbTime", 2.0f);
    std::vector<float> impulse ((size_t) (rate * 5.0), 0.0f);
    impulse[0] = 1.0f;
    const auto [il, ir] = run (*room, impulse, impulse);
    CHECK_THAT (measuredRt (il), Catch::Matchers::WithinRel (2.0, 0.3));
    for (auto x : il) REQUIRE (std::isfinite (x));

    // Longer: 4 s
    auto longer = make ("roomSim");
    for (const auto& [id, val] : std::vector<std::pair<const char*, float>> { { "dryLevel", -60.0f }, { "refLevel", -60.0f }, { "reverbLevel", 0.0f },
                                                                              { "lowMult", 1.0f }, { "highMult", 1.0f }, { "reverbTime", 4.0f } })
        set (*longer, id, val);
    std::vector<float> impulse2 ((size_t) (rate * 8.0), 0.0f);
    impulse2[0] = 1.0f;
    const auto [ll, lr] = run (*longer, impulse2, impulse2);
    CHECK_THAT (measuredRt (ll), Catch::Matchers::WithinRel (4.0, 0.3));

    // Freeze: the tail goes on, new sound doesn't get in
    auto frozen = make ("roomSim");
    set (*frozen, "dryLevel", -60.0f);
    set (*frozen, "refLevel", -60.0f);
    set (*frozen, "reverbTime", 0.5f);
    auto tone = sine (440.0, 0.5, 0.5f);
    run (*frozen, tone, tone);
    set (*frozen, "freeze", 1.0f);
    std::vector<float> silence ((size_t) (rate * 3.0), 0.0f);
    const auto [fl, fr] = run (*frozen, silence, silence);
    CHECK (peakOf (fl, (size_t) (rate * 2.5)) > 0.01f);

    // The classic model, with Enhance; the modern one in each mode: all play and stay finite
    for (const auto& settings : std::vector<std::vector<std::pair<const char*, float>>> {
             { { "model", 0.0f }, { "enhance", 1.0f } }, { { "model", 0.0f } }, { { "mode", 1.0f } }, { { "mode", 2.0f }, { "density", 200.0f } },
             { { "roomSize", 1000000.0f }, { "reverbTime", 100.0f } }, { { "roomSize", 1.0f } } })
    {
        auto fx = make ("roomSim");
        for (const auto& [id, val] : settings)
            set (*fx, id, val);
        const auto [ol, orr] = run (*fx, sine (220.0, 1.0, 0.5f), sine (220.0, 1.0, 0.5f));
        for (auto x : ol) REQUIRE (std::isfinite (x));
        CHECK (peakOf (ol) < 10.0f);
        CHECK (peakOf (ol, 24000) > 0.01f);
    }
}

TEST_CASE ("Legacy effects: reverbs, silver dynamics, de-esser, grooveshifter, amps, speech", "[stockfx]")
{
    // The reverbs ring on after the sound stops
    for (const auto* type : { "aVerb", "goldVerb" })
    {
        auto fx = make (type);
        set (*fx, "mix", 100.0f);
        auto tone = sine (440.0, 1.0, 0.5f);
        std::fill (tone.begin() + 24000, tone.end(), 0.0f);
        const auto [l, r] = run (*fx, tone, tone);
        CHECK (peakOf (l, 26000) > 0.005f);
        for (auto x : l) REQUIRE (std::isfinite (x));
    }

    // Silver Compressor brings a loud tone down; Silver Gate shuts a quiet one
    auto comp = make ("silverComp");
    set (*comp, "threshold", -30.0f);
    set (*comp, "ratio", 10.0f);
    const auto [cl, cr] = run (*comp, sine (300.0, 0.5, 0.9f), sine (300.0, 0.5, 0.9f));
    CHECK (peakOf (cl, 12000) < 0.2f);
    auto gate = make ("silverGate");
    const auto [gl, gr] = run (*gate, sine (300.0, 0.5, 0.001f), sine (300.0, 0.5, 0.001f));
    CHECK (peakOf (gl, 12000) < 0.00002f);

    // The DeEsser takes a sibilant tone down, a low one hardly
    const auto through = [] (float freq)
    {
        auto de = make ("deesserLegacy");
        set (*de, "sensitivity", 100.0f);
        set (*de, "strength", 30.0f);
        const auto [l, r] = run (*de, sine (freq, 0.5, 0.5f), sine (freq, 0.5, 0.5f));
        return peakOf (l, 12000);
    };
    CHECK (through (8000.0f) < 0.25f);
    CHECK (through (300.0f) > 0.45f);

    // Grooveshifter: at 50 % the sound passes (late by its grain); swing moves every other eighth later
    auto straight = make ("grooveshifter");
    straight->setPosition (0, false, 120.0);
    set (*straight, "swing", 50.0f);
    const auto latency = straight->getLatencySamples();
    const auto [sl, sr] = run (*straight, sine (300.0, 1.0, 0.5f), sine (300.0, 1.0, 0.5f));
    CHECK (peakOf (sl, 24000) > 0.4f);
    CHECK (peakOf (sl, 24000) < 0.6f);
    auto swung = make ("grooveshifter");
    swung->setPosition (0, false, 120.0);
    set (*swung, "swing", 75.0f);
    std::vector<float> clicks (48000 * 2, 0.0f);
    for (size_t at = 0; at < clicks.size(); at += 12000)   // eighths at 120 BPM
        for (size_t k = 0; k < 200; ++k) clicks[at + k] = 0.8f * std::sin ((float) k * 0.3f) * (1.0f - (float) k / 200.0f);
    const auto [wl, wr] = run (*swung, clicks, clicks);
    const auto energyNear = [&] (size_t centre)
    {
        double e = 0.0;
        for (size_t i = centre - 1500; i < centre + 1500; ++i) e += (double) wl[i] * wl[i];
        return e;
    };
    // the second eighth (at 12000) is heard at 12000 + 6000, not at 12000
    CHECK (energyNear (48000 + 12000 + 6000 + (size_t) latency) > 4.0 * energyNear (48000 + 12000 + (size_t) latency));

    // The amps and Speech Enhancer play and stay finite
    for (const auto* type : { "guitarAmpPro", "bassAmpLegacy", "speechEnhancer" })
    {
        auto fx = make (type);
        const auto [l, r] = run (*fx, sine (110.0, 0.5, 0.5f), sine (110.0, 0.5, 0.5f));
        for (auto x : l) REQUIRE (std::isfinite (x));
        CHECK (peakOf (l, 12000) > 0.001f);
        CHECK (peakOf (l) < 8.0f);
    }
}

TEST_CASE ("MultiMeter: equal octaves; pink noise reads flat, a tone in its band", "[stockfx]")
{
    // Pink noise (Paul Kellet's filter): the same power in every third octave
    auto mm = make ("multiMeter");
    juce::Random random (9);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    std::vector<float> l (48000 * 2);
    for (auto& x : l)
    {
        const auto white = random.nextFloat() * 2.0f - 1.0f;
        b0 = 0.99886f * b0 + white * 0.0555179f; b1 = 0.99332f * b1 + white * 0.0750759f; b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f; b4 = 0.55000f * b4 + white * 0.5329522f; b5 = -0.7616f * b5 - white * 0.0168980f;
        x = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.05f;
        b6 = white * 0.115926f;
    }
    run (*mm, l, l);
    std::array<float, BuiltInEffect::meterCapacity> m {};
    mm->readMeters (m.data(), (int) m.size());
    // (bands: 20 Hz * 1000^(b/31); 5 = 61 Hz .. 27 = 10 kHz)
    float lowest = 1000.0f, highest = -1000.0f;
    for (int b = 5; b <= 27; ++b)
    {
        lowest = std::min (lowest, m[(size_t) b]);
        highest = std::max (highest, m[(size_t) b]);
    }
    CHECK (highest - lowest < 4.0f);

    // A 1 kHz tone: loudest in the band holding 1 kHz (b = 17: 932 - 1166 Hz)
    auto tone = make ("multiMeter");
    run (*tone, sine (1000.0, 1.0, 0.5f), sine (1000.0, 1.0, 0.5f));
    tone->readMeters (m.data(), (int) m.size());
    int loudest = 0;
    for (int b = 0; b < 31; ++b)
        if (m[(size_t) b] > m[(size_t) loudest]) loudest = b;
    CHECK (loudest == 17);
    CHECK_THAT (m[17], Catch::Matchers::WithinAbs (20.0 * std::log10 (0.5), 1.0));   // (a full-scale sine reads 0 dB, as the MM window's)
}
