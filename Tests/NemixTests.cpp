#include <catch2/catch_test_macros.hpp>
#include "../Source/engine/nemix/Nemix.h"
#include "../Source/engine/nemix/Wavetables.h"
#include "../Source/model/NemixState.h"
#include "../Source/engine/nemix/SampleLibrary.h"

using namespace riff;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    void set (Nemix& n, const juce::String& id, float value)
    {
        const auto i = nemix::indexOf (id);
        REQUIRE (i >= 0);
        n.setParameter (i, value);
    }

    struct Result { float peak = 0.0f, rms = 0.0f; bool finite = true; };

    /** Plays a chord (or one note) for `seconds`, released at `release`. */
    Result play (Nemix& n, std::vector<int> notes, double seconds = 1.0, double release = 0.7, bool transport = false)
    {
        n.prepare (rate, block);
        Result r;
        double sum = 0.0;
        juce::int64 count = 0;
        std::vector<float> l (block), rr (block);
        const auto blocks = (int) (seconds * rate / block);
        const auto releaseBlock = (int) (release * rate / block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) for (auto note : notes) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
            if (b == releaseBlock) for (auto note : notes) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            n.setPosition ((juce::int64) b * block, transport, 120.0);
            n.render (l.data(), rr.data(), block, midi);
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (rr[(size_t) i])) r.finite = false;
                r.peak = std::max ({ r.peak, std::abs (l[(size_t) i]), std::abs (rr[(size_t) i]) });
                sum += (double) l[(size_t) i] * l[(size_t) i];
                ++count;
            }
        }
        r.rms = (float) std::sqrt (sum / (double) std::max<juce::int64> (1, count));
        return r;
    }
}

TEST_CASE ("Nemix parameters are unique and in range", "[nemix]")
{
    std::set<juce::String> seen;
    for (const auto& p : nemix::params())
    {
        CHECK (seen.insert (p.id).second);
        CHECK (p.defaultValue >= p.min);
        CHECK (p.defaultValue <= p.max);
    }
    CHECK (nemix::count() <= Nemix::maxParams);
    for (int i = 0; i < nemix::count(); ++i)
    {
        const auto& p = nemix::params()[(size_t) i];
        if (p.isChoice()) continue;
        const auto v = nemix::fromNormal (i, nemix::toNormal (i, p.defaultValue));
        CHECK (std::abs (v - p.defaultValue) <= 1.0e-3f * std::max (1.0f, p.max - p.min));
    }
}

TEST_CASE ("Every Nemix wavetable is made, finite and at a good level", "[nemix]")
{
    for (int t = 0; t < (int) nemix::tableNames().size(); ++t)
    {
        const auto& table = nemix::wt::get (t);
        for (int l = 0; l < nemix::wt::levels; ++l)
        {
            float peak = 0.0f;
            for (const auto v : table.data[(size_t) l])
            {
                REQUIRE (std::isfinite (v));
                peak = std::max (peak, std::abs (v));
            }
            INFO ("table " << t << " level " << l);
            if (l == 0) CHECK (peak > 0.5f);
            CHECK (peak < 1.6f);
        }
    }
}

TEST_CASE ("Nemix plays its default sound", "[nemix]")
{
    Nemix n;
    const auto r = play (n, { 60 });
    CHECK (r.finite);
    CHECK (r.rms > 0.01f);
    CHECK (r.peak < 1.5f);
    // and stops after the release
    juce::MidiBuffer none;
    std::vector<float> l (block), rr (block);
    for (int b = 0; b < 50; ++b) n.render (l.data(), rr.data(), block, none);
    float tail = 0.0f;
    for (auto v : l) tail = std::max (tail, std::abs (v));
    CHECK (tail < 1.0e-4f);
    CHECK (n.activeVoices() == 0);
}

TEST_CASE ("Every Nemix filter stays stable at full resonance", "[nemix]")
{
    for (int type = 0; type < (int) nemix::filterTypes().size(); ++type)
    {
        Nemix n;
        set (n, "aUnison", 7.0f);
        set (n, "aDetune", 0.4f);
        set (n, "f1On", 1.0f);
        set (n, "f1Type", (float) type);
        set (n, "f1Res", 1.0f);
        set (n, "f1Drive", 0.8f);
        set (n, "f1Cutoff", 800.0f);
        const auto r = play (n, { 36, 48, 60, 64, 67 });
        INFO ("filter " << nemix::filterTypes()[(size_t) type]);
        CHECK (r.finite);
        CHECK (r.peak < 4.0f);
        CHECK (r.rms > 1.0e-4f);
    }
}

TEST_CASE ("Every Nemix warp and effect plays finite", "[nemix]")
{
    for (int warp = 0; warp < (int) nemix::warpModes().size(); ++warp)
    {
        Nemix n;
        set (n, "bOn", 1.0f);
        set (n, "aWarp", (float) warp);
        set (n, "aWarpAmt", 0.8f);
        const auto r = play (n, { 48 });
        INFO ("warp " << nemix::warpModes()[(size_t) warp]);
        CHECK (r.finite);
        CHECK (r.rms > 1.0e-3f);
    }
    Nemix n;
    for (const auto* id : { "dsOn", "ffOn", "chOn", "phOn", "dlOn", "otOn", "rvOn", "eqOn" })
        set (n, id, 1.0f);
    set (n, "dsType", 4.0f);
    const auto r = play (n, { 48, 55, 60 }, 2.0, 1.0);
    CHECK (r.finite);
    CHECK (r.peak < 3.0f);
}

TEST_CASE ("Nemix modulation moves the sound", "[nemix]")
{
    // An LFO on the cutoff: the level changes from block to block more than without it
    const auto spread = [] (bool withLfo)
    {
        Nemix n;
        set (n, "f1On", 1.0f);
        set (n, "f1Cutoff", 300.0f);
        set (n, "l1Sync", 0.0f);
        set (n, "l1Rate", 4.0f);
        if (withLfo)
        {
            set (n, "m1Src", (float) nemix::srcLfo1);
            const auto& dests = nemix::destinations();
            const auto cutoff = nemix::indexOf ("f1Cutoff");
            const auto at = (int) (std::find (dests.begin(), dests.end(), cutoff) - dests.begin());
            set (n, "m1Dst", (float) at);
            set (n, "m1Amt", 0.6f);
        }
        n.prepare (rate, block);
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
        std::vector<float> l (block), r (block);
        float lo = 1.0e9f, hi = 0.0f;
        for (int b = 0; b < 180; ++b)
        {
            n.render (l.data(), r.data(), block, b == 0 ? on : juce::MidiBuffer());
            if (b < 20) continue;
            float s = 0.0f;
            for (auto v : l) s += v * v;
            lo = std::min (lo, s); hi = std::max (hi, s);
        }
        return hi / std::max (1.0e-9f, lo);
    };
    CHECK (spread (true) > 2.0f * spread (false));
}

TEST_CASE ("Nemix's arpeggiator plays steps from a held chord", "[nemix]")
{
    Nemix n;
    set (n, "arOn", 1.0f);
    set (n, "arRate", 14.0f);    // 1/16
    set (n, "e1Release", 0.01f);
    n.prepare (rate, block);
    juce::MidiBuffer chord;
    for (auto note : { 60, 64, 67 }) chord.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
    std::vector<float> l (block), r (block);
    int onsets = 0;
    float last = 0.0f;
    for (int b = 0; b < (int) (2.0 * rate / block); ++b)
    {
        n.setPosition ((juce::int64) b * block, false, 120.0);
        n.render (l.data(), r.data(), block, b == 0 ? chord : juce::MidiBuffer());
        float s = 0.0f;
        for (auto v : l) s += v * v;
        if (s > 4.0f * last + 1.0e-3f) ++onsets;
        last = s;
    }
    // 2 seconds of 1/16 at 120: 16 steps
    CHECK (onsets >= 10);
}

TEST_CASE ("A Nemix sound goes to text and back", "[nemix]")
{
    nemixstate::Sound s { { nemix::indexOf ("aTable"), 5.0f }, { nemix::indexOf ("f1Cutoff"), 1234.5f } };
    const auto back = nemixstate::fromText (nemixstate::toText (s));
    REQUIRE (back.size() == 2);
    CHECK (back[0].first == s[0].first);
    CHECK (std::abs (back[1].second - 1234.5f) < 0.01f);
}

#include "../Source/engine/nemix/NemixPresets.h"

TEST_CASE ("Every Nemix factory sound plays, finite and at a sensible level", "[nemix][presets]")
{
    const auto& presets = nemix::factoryPresets();
    REQUIRE (presets.size() >= 200);
    std::set<juce::String> names;
    const auto report = juce::SystemStats::getEnvironmentVariable ("NEMIX_LEVELS", {}).isNotEmpty();
    for (const auto& p : presets)
    {
        CHECK (names.insert (p.name).second);
        CHECK (nemix::presetCategories().contains (p.category));
        if (p.needsLibrary && nemix::samples::catalogue().empty())
            continue;   // (a recorded instrument, and the library isn't here)
        Nemix n;
        for (const auto& [index, value] : p.sound)
            n.setParameter (index, value);
        if (p.needsLibrary)
            REQUIRE (nemix::samples::waitUntilLoaded (juce::roundToInt (n.getParameter (nemix::indexOf ("smpInst")))));
        const auto drums = p.category == "Drums" || p.category == "Percussion";
        const auto kit = drums && p.needsLibrary;   // (a recorded kit: General MIDI keys)
        const auto r = play (n, kit ? std::vector<int> { 36, 38, 42 } : drums ? std::vector<int> { 60 } : std::vector<int> { 48, 55, 60, 64 },
                             p.category == "FX" ? 3.0 : 1.5, 1.0, true);
        INFO (p.name);
        CHECK (r.finite);
        CHECK (r.peak < 2.0f);
        CHECK (r.rms > 1.0e-4f);
        if (report)
            std::cout << "LEVEL|" << p.name << "|" << juce::Decibels::gainToDecibels (r.rms) << "|" << juce::Decibels::gainToDecibels (r.peak) << std::endl;
    }
}


TEST_CASE ("Nemix plays recorded instruments from its library", "[nemix][library]")
{
    // (needs the library: NEMIX_LIBRARY, or installed next to the app)
    if (nemix::samples::catalogue().empty())
    {
        WARN ("Nemix Library not found: skipped");
        return;
    }
    for (size_t i = 0; i < nemix::samples::catalogue().size(); ++i)
    {
        const auto& entry = nemix::samples::catalogue()[i];
        INFO (entry.name);
        const auto report = juce::SystemStats::getEnvironmentVariable ("NEMIX_LEVELS", {}).isNotEmpty();
        REQUIRE (nemix::samples::waitUntilLoaded ((int) i));
        const auto* ins = nemix::samples::find ((int) i);
        REQUIRE (ins != nullptr);
        CHECK (! ins->regions.empty());
        for (const auto& rec : ins->recordings)
            CHECK (rec.length > 100);
        Nemix n;
        set (n, "aOn", 0.0f);
        set (n, "smpOn", 1.0f);
        set (n, "smpInst", (float) i);
        set (n, "voices", 32.0f);
        set (n, "e1Release", 0.3f);
        const auto drums = entry.category == "Drums";
        // (notes in the instrument's own range)
        int lo = 127, hi = 0;
        for (const auto& reg : ins->regions) { lo = std::min (lo, reg.root); hi = std::max (hi, reg.root); }
        const auto mid = (lo + hi) / 2;
        const auto r = play (n, drums ? std::vector<int> { 36, 38, 42 } : std::vector<int> { mid, mid + 4, mid + 7 }, 1.5, 1.0);
        if (report)
            std::cout << "SAMPLELEVEL|" << entry.id << "|" << juce::Decibels::gainToDecibels (r.rms) << "|" << juce::Decibels::gainToDecibels (r.peak) << std::endl;
        CHECK (r.finite);
        CHECK (r.rms > 1.0e-4f);
        CHECK (r.peak < 2.0f);
    }
}
