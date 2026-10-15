#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/model/Automix.h"
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
    // Each track lands on headroom + its role's level: kick -18, vocal -18, hats -27 (while playing)
    CHECK_THAT (results[0].toDb, WithinAbs (-8.0, 1e-6));             // -18 - (-10)
    CHECK_THAT (results[1].toDb, WithinAbs (-18.0 + 24.0 - 0.0 - 0.0 + 0.0, 1e-6) );  // -18 - (-24) = +6
    CHECK_THAT (results[2].toDb, WithinAbs (-18.0 - 9.0 + 6.0, 1e-6)); // -27 - (-6) = -21
    CHECK (results[1].role == automix::Role::leadVocal);

    // A track that would need more than +6 brings everything down together.
    tracks[1].activeRmsDb = -40.0;   // own level -34: would need +16
    const auto down = automix::balance (tracks, "Hip-Hop", -18.0);
    CHECK_THAT (down[1].toDb, WithinAbs (6.0, 1e-6));
    CHECK_THAT (down[0].toDb, WithinAbs (-8.0 - 10.0, 1e-6));
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
