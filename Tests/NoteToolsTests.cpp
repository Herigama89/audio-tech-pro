#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/model/Project.h"
#include "../Source/model/NoteTools.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::Array<juce::ValueTree> notesOf (const juce::ValueTree& region)
    {
        juce::Array<juce::ValueTree> notes;
        for (const auto& n : region)
            if (n.hasType (ids::NOTE))
                notes.add (n);
        std::sort (notes.begin(), notes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            return (double) a[ids::start] != (double) b[ids::start] ? (double) a[ids::start] < (double) b[ids::start] : (int) a[ids::pitch] < (int) b[ids::pitch];
        });
        return notes;
    }

    /** A project with one instrument track and an empty MIDI region (kept alive with it). */
    struct Song
    {
        juce::ValueTree project = model::createProject();
        juce::ValueTree region = model::addMidiRegion (model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind), 0.0, 8.0, nullptr);
    };
}

TEST_CASE ("Note tools: chop, glue and slice", "[notetools]")
{
    Song song;
    auto region = song.region;
    model::addNote (region, 60, 0.0, 1.0, 100, nullptr);
    notetools::chop (region, notesOf (region), 0.25, nullptr);
    auto notes = notesOf (region);
    REQUIRE (notes.size() == 4);
    CHECK_THAT ((double) notes[3][ids::start], WithinAbs (0.75, 1e-9));
    CHECK_THAT ((double) notes[3][ids::length], WithinAbs (0.25, 1e-9));

    notetools::glue (region, notesOf (region), nullptr);
    notes = notesOf (region);
    REQUIRE (notes.size() == 1);
    CHECK_THAT ((double) notes[0][ids::length], WithinAbs (1.0, 1e-9));

    notetools::slice (region, notesOf (region), 0.4, nullptr);
    notes = notesOf (region);
    REQUIRE (notes.size() == 2);
    CHECK_THAT ((double) notes[0][ids::length], WithinAbs (0.4, 1e-9));
    CHECK_THAT ((double) notes[1][ids::start], WithinAbs (0.4, 1e-9));
}

TEST_CASE ("Note tools: strum, arpeggiate, flam", "[notetools]")
{
    Song song;
    auto region = song.region;
    for (int p : { 60, 64, 67 })
        model::addNote (region, p, 1.0, 1.0, 100, nullptr);
    notetools::strum (notesOf (region), 0.03, false, nullptr);
    auto notes = notesOf (region);
    CHECK_THAT ((double) notes[0][ids::start], WithinAbs (1.0, 1e-9));
    CHECK ((int) notes[1][ids::pitch] == 64);
    CHECK_THAT ((double) notes[2][ids::start], WithinAbs (1.06, 1e-9));
    CHECK_THAT ((double) notes[2][ids::start] + (double) notes[2][ids::length], WithinAbs (2.0, 1e-9));   // still ends with the chord

    Song song2;
    auto region2 = song2.region;
    for (int p : { 60, 64, 67 })
        model::addNote (region2, p, 0.0, 1.0, 90, nullptr);
    notetools::arpeggiate (region2, notesOf (region2), 0.25, 0, nullptr);
    notes = notesOf (region2);
    REQUIRE (notes.size() == 4);
    CHECK ((int) notes[0][ids::pitch] == 60);
    CHECK ((int) notes[1][ids::pitch] == 64);
    CHECK ((int) notes[2][ids::pitch] == 67);
    CHECK ((int) notes[3][ids::pitch] == 60);   // and round again

    Song song3;
    auto region3 = song3.region;
    model::addNote (region3, 38, 1.0, 0.5, 100, nullptr);
    notetools::flam (region3, notesOf (region3), 0.03, nullptr);
    notes = notesOf (region3);
    REQUIRE (notes.size() == 2);
    CHECK_THAT ((double) notes[0][ids::start], WithinAbs (0.97, 1e-9));
    CHECK ((int) notes[0][ids::velocity] < 100);
}

TEST_CASE ("Note tools: velocity, limit, claw, articulate, mute, duplicate", "[notetools]")
{
    Song song;
    auto region = song.region;
    model::addNote (region, 30, 0.0, 0.1, 60, nullptr);
    model::addNote (region, 100, 0.5, 0.1, 100, nullptr);
    notetools::scaleVelocity (notesOf (region), 50.0, 0, nullptr);
    auto notes = notesOf (region);
    CHECK ((int) notes[0][ids::velocity] == 70);
    CHECK ((int) notes[1][ids::velocity] == 90);

    notetools::limit (notesOf (region), 48, 72, nullptr);
    notes = notesOf (region);
    CHECK ((int) notes[0][ids::pitch] == 54);
    CHECK ((int) notes[1][ids::pitch] == 64);

    notetools::articulate (notesOf (region), 100.0, nullptr);
    CHECK_THAT ((double) notesOf (region)[0][ids::length], WithinAbs (0.5, 1e-9));

    notetools::setMuted ({ notesOf (region)[0] }, true, nullptr);
    CHECK (notetools::isMuted (notesOf (region)[0]));

    const auto copies = notetools::duplicate (region, notesOf (region), 1.0, nullptr);
    CHECK (copies.size() == 2);
    CHECK (notesOf (region).size() == 4);
    CHECK (notetools::isMuted (copies[0]));

    notetools::claw (notesOf (region), 2, 1, nullptr);
    CHECK (notesOf (region).size() == 2);
}
