#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/model/Automix.h"
#include <map>
#include "../Source/engine/MixAnalysis.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Automix: roles from names and tone", "[automix]")
{
    automix::Track t;
    t.name = "Kick In";       CHECK (automix::guessRole (t) == automix::Role::kick);
    t.name = "808";           CHECK (automix::guessRole (t) == automix::Role::bass);
    t.name = "Lead Vox";      CHECK (automix::guessRole (t) == automix::Role::leadVocal);
    t.name = "BGV Stack";     CHECK (automix::guessRole (t) == automix::Role::backingVocal);
    t.name = "Rhodes";        CHECK (automix::guessRole (t) == automix::Role::keys);
    t.name = "Hi Hat";        CHECK (automix::guessRole (t) == automix::Role::hats);
    t.name = "Audio 7";
    t.lowShare = 80.0;        CHECK (automix::guessRole (t) == automix::Role::bass);
    t.lowShare = 10.0; t.highShare = 70.0;
    CHECK (automix::guessRole (t) == automix::Role::hats);
}

TEST_CASE ("Automix: faders set to the genre's balance, measured while they play", "[automix]")
{
    std::vector<automix::Track> tracks;
    auto add = [&tracks] (const char* name, double rms, double fader)
    {
        automix::Track t;
        t.id = name;
        t.name = name;
        t.activeRmsDb = rms;
        t.faderDb = fader;
        tracks.push_back (t);
    };
    add ("Kick", -10.0, 0.0);
    add ("Lead Vox", -30.0, -6.0);   // its own level -24
    add ("Hats", -6.0, 0.0);
    add ("Silent", -120.0, 0.0);

    const auto results = automix::balance (tracks, "Hip-Hop", -18.0);
    REQUIRE (results.size() == 3);   // the silent one left alone
    // Each track lands on headroom + its role's level: kick -18, vocal -18, hats -27 (while playing). The vocal would
    // need +6, so everything comes down 6 dB together: no fader above 0 dB.
    CHECK_THAT (results[0].toDb, WithinAbs (-8.0 - 6.0, 1e-6));       // -18 - (-10), less 6
    CHECK_THAT (results[1].toDb, WithinAbs (0.0, 1e-6));              // -18 - (-24) = +6, less 6
    CHECK_THAT (results[2].toDb, WithinAbs (-21.0 - 6.0, 1e-6));      // -27 - (-6) = -21, less 6
    CHECK (results[1].role == automix::Role::leadVocal);

    // A very quiet track: everything comes down by at most 12 dB, and it stops at 0 dB.
    tracks[1].activeRmsDb = -40.0;   // own level -34: would need +16
    const auto down = automix::balance (tracks, "Hip-Hop", -18.0);
    CHECK_THAT (down[1].toDb, WithinAbs (0.0, 1e-6));
    CHECK_THAT (down[0].toDb, WithinAbs (-8.0 - 12.0, 1e-6));
    for (const auto& r : down)
        CHECK (r.toDb <= 0.0);
}

TEST_CASE ("A track's level while it plays leaves its silences out", "[automix]")
{
    LevelStats stats;
    stats.prepare (48000.0);
    std::vector<float> loud (512, 0.5f), quiet (512, 0.0f);
    stats.add (loud.data(), loud.data(), 512);
    for (int i = 0; i < 9; ++i)
        stats.add (quiet.data(), quiet.data(), 512);
    CHECK_THAT (stats.activeRmsDb(), WithinAbs (juce::Decibels::gainToDecibels (0.5), 0.01));
    CHECK (stats.rmsDb() < stats.activeRmsDb() - 9.0);
}

TEST_CASE ("Automix pans by role and genre, and recommends plugins", "[automix]")
{
    using namespace riff::automix;
    // the centre: kick, snare, bass, lead vocal
    for (auto r : { Role::kick, Role::snare, Role::bass, Role::leadVocal })
        CHECK (targetPan ("Pop", r, 0, 1) == 0.0);
    // doubled guitars: left and right, wider in rock than in pop
    CHECK (targetPan ("Rock", Role::guitar, 0, 2) < -0.9);
    CHECK (targetPan ("Rock", Role::guitar, 1, 2) > 0.9);
    CHECK (std::abs (targetPan ("Pop", Role::guitar, 0, 2)) < std::abs (targetPan ("Rock", Role::guitar, 0, 2)));
    // hip-hop keeps things narrower than jazz
    CHECK (std::abs (targetPan ("Hip-Hop", Role::keys, 0, 1)) < std::abs (targetPan ("Jazz", Role::keys, 0, 1)));

    Track vox;
    vox.name = "Lead Vox";
    vox.activeRmsDb = -20.0;
    vox.faderDb = -2.0;
    const auto trap = recommendedInserts ("Trap", Role::leadVocal, vox, 9, true);
    juce::StringArray types;
    for (const auto& i : trap) types.add (i.type);
    CHECK (types == juce::StringArray { "channelEq", "studioComp", "deesser", "pitchCorrection" });
    // the compressor's threshold follows the vocal's own level (-18 in): a few dB above it
    for (const auto& i : trap)
        if (i.type == "studioComp")
            for (const auto& [id, v] : i.params)
                if (id == "threshold")
                    CHECK (std::abs (v - (-15.0f)) < 0.01f);
    // tuned in the song's key (A minor), hard for trap
    for (const auto& i : trap)
        if (i.type == "pitchCorrection")
        {
            std::map<juce::String, float> p (i.params.begin(), i.params.end());
            CHECK (p["key"] == 9.0f);
            CHECK (p["scale"] == 2.0f);
            CHECK (p["response"] == 1.0f);
        }
    // jazz vocals aren't tuned; an 808 in hip-hop gets saturation
    for (const auto& i : recommendedInserts ("Jazz", Role::leadVocal, vox))
        CHECK (i.type != "pitchCorrection");
    bool saturated = false;
    for (const auto& i : recommendedInserts ("Hip-Hop", Role::bass, vox))
        saturated = saturated || i.type == "saturator";
    CHECK (saturated);
}
