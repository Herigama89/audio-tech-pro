#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "model/Generator.h"
#include "model/Project.h"

using namespace riff;
namespace gen = riff::generator;

namespace
{
    bool sameNotes (const gen::Result& a, const gen::Result& b)
    {
        if (a.parts.size() != b.parts.size())
            return false;
        for (size_t p = 0; p < a.parts.size(); ++p)
        {
            const auto& x = a.parts[p].notes;
            const auto& y = b.parts[p].notes;
            if (x.size() != y.size())
                return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (x[i].pitch != y[i].pitch || x[i].start != y[i].start || x[i].velocity != y[i].velocity)
                    return false;
        }
        return true;
    }

    const gen::Part* find (const gen::Result& r, gen::PartKind kind)
    {
        for (const auto& p : r.parts)
            if (p.kind == kind)
                return &p;
        return nullptr;
    }
}

TEST_CASE ("Generator: the same settings give the same notes; a new seed changes them", "[generator]")
{
    gen::Settings s;
    s.genre = 1;
    s.seed = 42;
    const auto a = gen::generate (s), b = gen::generate (s);
    CHECK (sameNotes (a, b));

    s.seed = 43;
    CHECK_FALSE (sameNotes (a, gen::generate (s)));
}

TEST_CASE ("Generator: every genre writes all four parts inside the bars", "[generator]")
{
    for (int genre = 0; genre < (int) gen::genres().size(); ++genre)
        for (int bars : { 4, 8, 16 })
            for (juce::uint32 seed : { 1u, 7u, 99u })
            {
                gen::Settings s;
                s.genre = genre;
                s.bars = bars;
                s.seed = seed;
                s.root = (int) seed % 12;
                s.minor = gen::genres()[(size_t) genre].minor;
                s.variation = seed == 99u ? 1.0f : 0.3f;
                const auto result = gen::generate (s);

                INFO (gen::genres()[(size_t) genre].name << ", " << bars << " bars, seed " << seed);
                REQUIRE (result.parts.size() == 4);
                CHECK (result.tempo == gen::genres()[(size_t) genre].tempo);

                for (const auto& part : result.parts)
                {
                    CHECK_FALSE (part.notes.empty());
                    for (const auto& n : part.notes)
                    {
                        CHECK (n.start >= 0.0);
                        CHECK (n.start + n.length <= bars * 4.0 + 1e-9);
                        CHECK (n.length > 0.0);
                        CHECK (n.velocity >= 1);
                        CHECK (n.velocity <= 127);
                    }
                }
            }
}

TEST_CASE ("Generator: pitched parts stay in key and in range; drums use GM notes", "[generator]")
{
    const std::set<int> drumNotes { gen::gm::kick, gen::gm::rim, gen::gm::snare, gen::gm::clap, gen::gm::closedHat,
                                    gen::gm::pedalHat, gen::gm::openHat, gen::gm::lowTom, gen::gm::highTom,
                                    gen::gm::crash, gen::gm::ride, gen::gm::cowbell, gen::gm::shaker };

    for (int genre = 0; genre < (int) gen::genres().size(); ++genre)
        for (bool minor : { false, true })
        {
            gen::Settings s;
            s.genre = genre;
            s.root = 9;   // A
            s.minor = minor;
            s.seed = 5;
            const auto result = gen::generate (s);
            const auto scale = gen::scaleOf (s.root, minor);
            auto inKey = [&] (int pitch) { return std::find (scale.begin(), scale.end(), pitch % 12) != scale.end(); };

            INFO (gen::genres()[(size_t) genre].name << (minor ? " minor" : " major"));

            for (const auto& n : find (result, gen::PartKind::drums)->notes)
                CHECK (drumNotes.count (n.pitch) == 1);

            for (const auto& n : find (result, gen::PartKind::bass)->notes)
            {
                CHECK (inKey (n.pitch));
                CHECK (n.pitch >= 28);
                CHECK (n.pitch <= 56);
            }

            for (const auto& n : find (result, gen::PartKind::chords)->notes)
            {
                CHECK (inKey (n.pitch));
                CHECK (n.pitch >= 48);
                CHECK (n.pitch <= 84);
            }

            for (const auto& n : find (result, gen::PartKind::melody)->notes)
            {
                CHECK (inKey (n.pitch));
                CHECK (n.pitch >= 60);
                CHECK (n.pitch <= 96);
            }
        }
}

TEST_CASE ("Generator: four-on-the-floor genres put a kick on every beat", "[generator]")
{
    gen::Settings s;
    s.genre = 6;   // House
    REQUIRE (juce::String (gen::genres()[6].name) == "House");
    const auto result = gen::generate (s);

    std::set<double> kicks;
    for (const auto& n : find (result, gen::PartKind::drums)->notes)
        if (n.pitch == gen::gm::kick)
            kicks.insert (n.start);

    for (int beat = 0; beat < s.bars * 4; ++beat)
        CHECK (kicks.count ((double) beat) == 1);
}

TEST_CASE ("Generator: turning parts off leaves them out", "[generator]")
{
    gen::Settings s;
    s.drums = false;
    s.melody = false;
    const auto result = gen::generate (s);
    REQUIRE (result.parts.size() == 2);
    CHECK (result.parts[0].kind == gen::PartKind::bass);
    CHECK (result.parts[1].kind == gen::PartKind::chords);
}

TEST_CASE ("Generator: key names", "[generator]")
{
    CHECK (gen::keyName (0, false) == "C major");
    CHECK (gen::keyName (9, true) == "A minor");
    CHECK (gen::keyName (10, true) == "Bb minor");
    CHECK (gen::scaleOf (9, true) == std::vector<int> { 9, 11, 0, 2, 4, 5, 7 });
}

TEST_CASE ("Generator: apply adds instrument tracks with MIDI, in one undo step", "[generator][undo]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    model::addTrack (project, "Vocals", &um);

    gen::Settings s;
    s.genre = 1;   // Trap, 140 BPM
    s.bars = 4;
    const auto result = gen::generate (s);

    um.beginNewTransaction ("Generate MIDI");
    const auto tracks = gen::apply (project, result, s, 2.0, true, &um);

    REQUIRE (tracks.size() == 4);
    CHECK (model::numTracks (project) == 5);
    CHECK ((double) project[ids::tempo] == 140.0);

    const auto secondsPerBeat = 60.0 / 140.0;
    for (int i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks.getReference (i);
        CHECK (model::isInstrumentTrack (track));
        REQUIRE (track.getNumChildren() >= 1);
        auto region = track.getChildWithName (ids::REGION);
        REQUIRE (region.isValid());
        CHECK (model::isMidiRegion (region));
        CHECK ((double) region[ids::start] == 2.0);
        CHECK (std::abs ((double) region[ids::length] - 16.0 * secondsPerBeat) < 1e-9);
        CHECK (region.getNumChildren() == (int) result.parts[(size_t) i].notes.size());
        CHECK (track[ids::name].toString().startsWith ("Trap"));
    }

    // One undo takes it all back, tempo included.
    um.undo();
    CHECK (model::numTracks (project) == 1);
    CHECK ((double) project[ids::tempo] == 120.0);

    um.redo();
    CHECK (model::numTracks (project) == 5);
}
