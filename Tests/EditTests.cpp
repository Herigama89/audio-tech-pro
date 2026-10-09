#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "model/Project.h"
#include "ui/TimelineState.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    int countRegions (const juce::ValueTree& track)
    {
        int n = 0;
        for (const auto& r : track)
            n += r.hasType (ids::REGION) ? 1 : 0;
        return n;
    }
}

TEST_CASE ("Duplicating a region gives it new ids and a new place", "[edit]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto keys = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (keys, 1.0, 2.0, nullptr);
    model::addNote (region, 60, 0.0, 0.5, 100, nullptr);

    auto copy = model::duplicateRegion (region, keys, 5.0, &um);
    CHECK (countRegions (keys) == 2);
    CHECK (copy[ids::id].toString() != region[ids::id].toString());
    CHECK ((double) copy[ids::start] == 5.0);
    CHECK (copy.getNumChildren() == 1);
    um.undo();
    CHECK (countRegions (keys) == 1);
}

TEST_CASE ("Joining MIDI regions keeps every note where it sounds", "[edit]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto keys = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto a = model::addMidiRegion (keys, 0.0, 2.0, nullptr);
    model::addNote (a, 60, 0.5, 0.5, 100, nullptr);
    model::addNote (a, 62, 3.0, 0.5, 100, nullptr);     // outside the region's window: not heard, not kept
    auto b = model::addMidiRegion (keys, 4.0, 2.0, nullptr);
    b.setProperty (ids::offset, 1.0, nullptr);          // trimmed: its content starts 1 s in
    model::addNote (b, 64, 1.5, 1.0, 90, nullptr);      // heard at 4.5

    um.beginNewTransaction();
    auto joined = model::joinRegions ({ a, b }, &um);
    REQUIRE (joined.isValid());
    CHECK (countRegions (keys) == 1);
    CHECK_THAT ((double) joined[ids::start], WithinAbs (0.0, 1e-9));
    CHECK_THAT ((double) joined[ids::length], WithinAbs (6.0, 1e-9));
    REQUIRE (joined.getNumChildren() == 2);
    CHECK_THAT ((double) joined.getChild (0)[ids::start], WithinAbs (0.5, 1e-9));
    CHECK ((int) joined.getChild (1)[ids::pitch] == 64);
    CHECK_THAT ((double) joined.getChild (1)[ids::start], WithinAbs (4.5, 1e-9));

    um.undo();
    CHECK (countRegions (keys) == 2);

    // Not across tracks.
    auto other = model::addTrack (project, "Other", nullptr, -1, model::instrumentKind);
    auto c = model::addMidiRegion (other, 0.0, 1.0, nullptr);
    CHECK_FALSE (model::joinRegions ({ a, c }, &um).isValid());
}

TEST_CASE ("No Overlap shortens, splits or removes what a region covers", "[edit]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto keys = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto before = model::addMidiRegion (keys, 0.0, 4.0, nullptr);    // 0-4
    auto inside = model::addMidiRegion (keys, 5.0, 1.0, nullptr);    // 5-6
    auto after = model::addMidiRegion (keys, 7.0, 4.0, nullptr);     // 7-11
    auto moved = model::addMidiRegion (keys, 3.0, 5.0, nullptr);     // 3-8 now on top

    model::clearOverlaps (moved, {}, &um);
    CHECK_THAT (model::regionEnd (before), WithinAbs (3.0, 1e-9));
    CHECK_FALSE (inside.getParent().isValid());                      // covered: removed
    CHECK_THAT ((double) after[ids::start], WithinAbs (8.0, 1e-9));
    CHECK_THAT (model::regionEnd (after), WithinAbs (11.0, 1e-9));

    // Landing in the middle of a long one splits it around.
    auto longOne = model::addMidiRegion (keys, 20.0, 10.0, nullptr);
    auto small = model::addMidiRegion (keys, 24.0, 2.0, nullptr);
    model::clearOverlaps (small, {}, &um);
    CHECK_THAT (model::regionEnd (longOne), WithinAbs (24.0, 1e-9));
    int pieces = 0;
    for (const auto& r : keys)
        if ((double) r[ids::start] >= 26.0 - 1e-9 && (double) r[ids::start] < 30.0)
            ++pieces;
    CHECK (pieces == 1);
}

TEST_CASE ("New track with duplicate settings", "[edit]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto vocal = model::addTrack (project, "Vocal", nullptr);
    model::addTrack (project, "Drums", nullptr);
    vocal.setProperty (ids::volume, 0.5, nullptr);
    vocal.setProperty (ids::consoleHF, 3.0, nullptr);
    vocal.setProperty (ids::armed, true, nullptr);
    model::addRegion (vocal, juce::File ("/tmp/take.wav"), 0.0, 1.0, nullptr);
    auto insert = model::addBuiltinInsert (model::chainFor (project, vocal[ids::id].toString()), "compressor", -1, nullptr);
    insert.setProperty ("threshold", -20.0, nullptr);

    auto copy = model::duplicateTrackSettings (project, vocal, &um);
    CHECK (project.indexOf (copy) == 1);                      // right below
    CHECK (copy[ids::name].toString() == "Vocal");
    CHECK ((double) copy[ids::volume] == 0.5);
    CHECK ((double) copy[ids::consoleHF] == 3.0);
    CHECK_FALSE ((bool) copy[ids::armed]);
    CHECK (countRegions (copy) == 0);
    const auto inserts = model::insertsOf (model::findChain (project, copy[ids::id].toString()));
    REQUIRE (inserts.size() == 1);
    CHECK ((double) inserts[0]["threshold"] == -20.0);
    CHECK (inserts[0][ids::id].toString() != insert[ids::id].toString());
}

TEST_CASE ("Selecting several regions", "[edit][view]")
{
    auto project = model::createProject();
    auto keys = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto a = model::addMidiRegion (keys, 0.0, 1.0, nullptr);
    auto b = model::addMidiRegion (keys, 2.0, 1.0, nullptr);
    auto c = model::addMidiRegion (keys, 4.0, 1.0, nullptr);

    TimelineState t;
    t.project = project;
    t.selectOnlyRegion (a[ids::id].toString());
    t.toggleRegionSelection (c[ids::id].toString());
    CHECK (t.selectedRegionTrees().size() == 2);
    CHECK (t.isRegionSelected (c[ids::id].toString()));
    CHECK_FALSE (t.isRegionSelected (b[ids::id].toString()));
    t.toggleRegionSelection (a[ids::id].toString());
    CHECK (t.selectedRegionTrees() == juce::Array<juce::ValueTree> { c });
    t.selectedRegionId = b[ids::id].toString();          // set directly: just that one
    CHECK (t.selectedRegionTrees() == juce::Array<juce::ValueTree> { b });
}

TEST_CASE ("Markers: added in any order, found in time order, saved with the project", "[edit][markers]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    model::addTrack (project, "A", nullptr);
    model::addMarker (project, 8.0, "Chorus", &um);
    model::addMarker (project, 0.0, "Intro", &um);
    auto verse = model::addMarker (project, 4.0, {}, &um);
    CHECK (verse[ids::name].toString() == "Marker 3");

    const auto list = model::sortedMarkers (project);
    REQUIRE (list.size() == 3);
    CHECK (list[0][ids::name].toString() == "Intro");
    CHECK (list[2][ids::name].toString() == "Chorus");

    CHECK (model::adjacentMarker (project, 4.0, true)[ids::name].toString() == "Chorus");
    CHECK (model::adjacentMarker (project, 4.0, false)[ids::name].toString() == "Intro");
    CHECK_FALSE (model::adjacentMarker (project, 8.0, true).isValid());

    // Markers don't count as tracks.
    CHECK (model::numTracks (project) == 1);

    // Through a save and load
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("markers-" + juce::Uuid().toString() + ".atp");
    juce::String error;
    file.replaceWithText (model::toJson (project, file));
    const auto loaded = model::load (file, error);
    file.deleteFile();
    CHECK (error.isEmpty());
    CHECK (model::sortedMarkers (loaded).size() == 3);

    // Key names
    project.setProperty (ids::keyRoot, 6, nullptr);
    project.setProperty (ids::keyMinor, true, nullptr);
    CHECK (model::keyName (project) == "F# minor");
}
