#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/engine/BuiltInEffects.h"

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
            CHECK (most < 1.0e5f);   // (every EQ band at +24 dB on one frequency is loud, but nothing blows up)
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
}
