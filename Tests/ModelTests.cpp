#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "model/Project.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    double num (const juce::ValueTree& t, const juce::Identifier& id) { return (double) t[id]; }
}

TEST_CASE ("New project has defaults and no tracks", "[model]")
{
    auto p = model::createProject ("Song");
    CHECK (p.hasType (ids::PROJECT));
    CHECK (p[ids::name].toString() == "Song");
    CHECK (num (p, ids::tempo) == 120.0);
    CHECK (p.getNumChildren() == 0);
}

TEST_CASE ("Adding tracks and regions, with undo and redo", "[model][undo]")
{
    juce::UndoManager um;
    auto p = model::createProject();

    um.beginNewTransaction();
    auto track = model::addTrack (p, "Drums", &um);

    um.beginNewTransaction();
    auto region = model::addRegion (track, juce::File ("/tmp/kick.wav"), 2.0, 4.0, &um);

    REQUIRE (p.getNumChildren() == 1);
    REQUIRE (track.getNumChildren() == 1);
    CHECK (region[ids::name].toString() == "kick");
    CHECK (num (region, ids::start) == 2.0);
    CHECK (model::regionEnd (region) == 6.0);
    CHECK (model::projectEnd (p) == 6.0);
    CHECK (model::findById (p, region[ids::id]) == region);

    um.undo();
    CHECK (track.getNumChildren() == 0);
    um.undo();
    CHECK (p.getNumChildren() == 0);

    um.redo();
    um.redo();
    CHECK (p.getNumChildren() == 1);
    CHECK (p.getChild (0).getNumChildren() == 1);
}

TEST_CASE ("Moving a region to another track is one undo step", "[model][undo]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto a = model::addTrack (p, "A", nullptr);
    auto b = model::addTrack (p, "B", nullptr);
    auto region = model::addRegion (a, juce::File ("/tmp/x.wav"), 1.0, 2.0, nullptr);

    um.beginNewTransaction ("Move");
    model::moveRegion (region, b, 3.0, &um);
    model::moveRegion (region, b, 3.5, &um);   // a drag sends many moves

    CHECK (region.getParent() == b);
    CHECK (num (region, ids::start) == 3.5);

    um.undo();
    CHECK (region.getParent() == a);
    CHECK (num (region, ids::start) == 1.0);

    model::moveRegion (region, {}, -5.0, nullptr);
    CHECK (num (region, ids::start) == 0.0); // never before zero
}

TEST_CASE ("Splitting keeps the audio in place", "[model]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "A", nullptr);
    auto region = model::addRegion (t, juce::File ("/tmp/x.wav"), 10.0, 8.0, nullptr);
    region.setProperty (ids::offset, 1.0, nullptr);

    um.beginNewTransaction();
    auto right = model::splitRegion (region, 13.0, &um);

    REQUIRE (right.isValid());
    CHECK (t.getNumChildren() == 2);
    CHECK (num (region, ids::length) == 3.0);
    CHECK (num (right, ids::start) == 13.0);
    CHECK (num (right, ids::offset) == 4.0);   // 1 + (13 - 10)
    CHECK (num (right, ids::length) == 5.0);
    CHECK (right[ids::id] != region[ids::id]);

    CHECK_FALSE (model::splitRegion (region, 50.0, &um).isValid()); // outside
    CHECK_FALSE (model::splitRegion (region, 10.0, &um).isValid()); // at the edge

    um.undo();
    CHECK (t.getNumChildren() == 1);
    CHECK (num (region, ids::length) == 8.0);
}

TEST_CASE ("Trimming is clamped to the source audio", "[model]")
{
    auto p = model::createProject();
    auto t = model::addTrack (p, "A", nullptr);
    auto region = model::addRegion (t, juce::File ("/tmp/x.wav"), 10.0, 4.0, nullptr);
    const double sourceLength = 6.0;

    // Left edge: can move right, but not before the start of the file.
    model::trimRegionStart (region, 11.0, sourceLength, nullptr);
    CHECK (num (region, ids::start) == 11.0);
    CHECK (num (region, ids::offset) == 1.0);
    CHECK (num (region, ids::length) == 3.0);

    model::trimRegionStart (region, 5.0, sourceLength, nullptr);
    CHECK (num (region, ids::start) == 10.0);
    CHECK (num (region, ids::offset) == 0.0);
    CHECK (model::regionEnd (region) == 14.0);

    // Right edge: no further than the end of the file.
    model::trimRegionEnd (region, 30.0, sourceLength, nullptr);
    CHECK (model::regionEnd (region) == 16.0);

    model::trimRegionEnd (region, 9.0, sourceLength, nullptr);
    CHECK_THAT (num (region, ids::length), WithinAbs (model::minRegionLength, 1e-9));
}

TEST_CASE ("JSON round trip with relative audio paths", "[model][file]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-test-" + juce::Uuid().toString());
    const auto projectFile = dir.getChildFile ("Song.riff");
    const auto audioFile = dir.getChildFile ("Audio Files").getChildFile ("vocal.wav");
    dir.createDirectory();

    auto p = model::createProject ("Song");
    p.setProperty (ids::tempo, 97.5, nullptr);
    auto t = model::addTrack (p, "Vocal", nullptr);
    t.setProperty (ids::mute, true, nullptr);
    auto r = model::addRegion (t, audioFile, 1.25, 3.5, nullptr);

    const auto json = model::toJson (p, projectFile);
    CHECK (json.contains ("riffVersion"));
    CHECK (json.contains ("Audio Files"));
    CHECK_FALSE (json.contains (dir.getFullPathName())); // stored relative

    juce::String error;
    REQUIRE (model::save (p, projectFile, error));
    auto loaded = model::load (projectFile, error);

    REQUIRE (loaded.isValid());
    CHECK (error.isEmpty());
    CHECK (num (loaded, ids::tempo) == 97.5);
    REQUIRE (loaded.getNumChildren() == 1);

    const auto lt = loaded.getChild (0);
    CHECK (lt[ids::name].toString() == "Vocal");
    CHECK ((bool) lt[ids::mute]);
    CHECK (lt[ids::id] == t[ids::id]);

    const auto lr = lt.getChild (0);
    CHECK (juce::File (lr[ids::file].toString()) == audioFile);
    CHECK_THAT (num (lr, ids::start), WithinAbs (1.25, 1e-9));
    CHECK_THAT (num (lr, ids::length), WithinAbs (3.5, 1e-9));

    dir.deleteRecursively();
}

TEST_CASE ("Bad project files are rejected with a message", "[model][file]")
{
    juce::String error;
    const juce::File f ("/tmp/whatever.riff");

    CHECK_FALSE (model::fromJson ("not json {", f, error).isValid());
    CHECK (error.isNotEmpty());

    error = {};
    CHECK_FALSE (model::fromJson (R"({"hello": 1})", f, error).isValid());
    CHECK (error.contains ("isn't a Riff project"));

    error = {};
    CHECK_FALSE (model::fromJson (R"({"riffVersion": 999, "project": {"type": "PROJECT"}})", f, error).isValid());
    CHECK (error.contains ("newer version"));
}

TEST_CASE ("Recorded regions are shifted earlier by the latency", "[model][record]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "Vox", nullptr);
    CHECK_FALSE ((bool) t[ids::armed]);
    CHECK ((int) t[ids::input] == 0);

    um.beginNewTransaction ("Record");
    auto r = model::addRecordedRegion (t, juce::File ("/tmp/Vox#01.wav"), 4.0, 10.0, 0.012, &um);

    REQUIRE (r.isValid());
    CHECK (num (r, ids::start) == 4.0);
    CHECK_THAT (num (r, ids::offset), WithinAbs (0.012, 1e-12));
    CHECK_THAT (num (r, ids::length), WithinAbs (9.988, 1e-12));

    // Too short to keep after removing the latency
    CHECK_FALSE (model::addRecordedRegion (t, juce::File ("/tmp/x.wav"), 0.0, 0.01, 0.012, &um).isValid());

    um.undo();
    CHECK (t.getNumChildren() == 0);
}

TEST_CASE ("Input names for the track's input setting", "[model][record]")
{
    auto p = model::createProject();
    auto t = model::addTrack (p, "A", nullptr);
    const juce::StringArray names { "Mic 1", "Mic 2", "Line 3" };

    CHECK (model::inputName (t, names) == "Mic 1");
    t.setProperty (ids::input, 1, nullptr);
    t.setProperty (ids::inputStereo, true, nullptr);
    CHECK (model::inputName (t, names) == "Mic 2 + Line 3");
    t.setProperty (ids::input, 7, nullptr);
    CHECK (model::inputName (t, names) == "In 8 + In 9");
}

TEST_CASE ("Splitting a recording into cycle passes", "[model][cycle]")
{
    SECTION ("no cycle: one piece, shifted by the latency")
    {
        const auto pieces = model::splitRecording (2.0, 5.0, 0.01, false, 0.0, 4.0);
        REQUIRE (pieces.size() == 1);
        CHECK (pieces[0].timelineStart == 2.0);
        CHECK (pieces[0].fileOffset == 0.01);
        CHECK_THAT (pieces[0].length, WithinAbs (4.99, 1e-9));
    }

    SECTION ("cycle never reached: one piece")
    {
        const auto pieces = model::splitRecording (0.0, 3.0, 0.0, true, 0.0, 4.0);
        REQUIRE (pieces.size() == 1);
        CHECK (pieces[0].length == 3.0);
    }

    SECTION ("three full passes and a short fourth")
    {
        // Cycle 2..6 s (4 s long), recording from 2 s for 13 s, no latency.
        const auto pieces = model::splitRecording (2.0, 13.0, 0.0, true, 2.0, 6.0);
        REQUIRE (pieces.size() == 4);

        for (int i = 0; i < 4; ++i)
        {
            CHECK (pieces[(size_t) i].timelineStart == 2.0);
            CHECK (pieces[(size_t) i].fileOffset == 4.0 * i);
        }

        CHECK (pieces[0].length == 4.0);
        CHECK (pieces[2].length == 4.0);
        CHECK_THAT (pieces[3].length, WithinAbs (1.0, 1e-9));
    }

    SECTION ("punch in mid-cycle: a partial first pass, then whole passes")
    {
        const auto pieces = model::splitRecording (3.0, 7.0, 0.005, true, 2.0, 6.0);
        REQUIRE (pieces.size() == 2);
        CHECK (pieces[0].timelineStart == 3.0);
        CHECK (pieces[0].length == 3.0);
        CHECK (pieces[0].fileOffset == 0.005);
        CHECK (pieces[1].timelineStart == 2.0);
        CHECK_THAT (pieces[1].fileOffset, WithinAbs (3.005, 1e-9));
        CHECK_THAT (pieces[1].length, WithinAbs (3.995, 1e-9));
    }
}

TEST_CASE ("Take folders switch, flatten, split and save", "[model][cycle]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "Vox", nullptr);
    const juce::File file ("/tmp/riff-takes/Vox#01.wav");

    auto r = model::addTakeFolder (t, file, 2.0, 4.0, { 0.01, 4.01, 8.01 }, 2, &um);
    REQUIRE (model::numTakes (r) == 3);
    CHECK ((int) r[ids::activeTake] == 2);
    CHECK (num (r, ids::offset) == 8.01);
    CHECK (r[ids::name].toString() == "Vox: Take 3");

    um.beginNewTransaction();
    model::setActiveTake (r, 0, &um);
    CHECK (num (r, ids::offset) == 0.01);
    CHECK (r[ids::name].toString() == "Vox: Take 1");
    um.undo();
    CHECK (num (r, ids::offset) == 8.01);

    // Splitting shifts every take, so switching takes later still lines up.
    auto right = model::splitRegion (r, 3.0, nullptr);
    REQUIRE (right.isValid());
    CHECK (model::numTakes (right) == 3);
    CHECK_THAT (num (right.getChild (0), ids::offset), WithinAbs (1.01, 1e-9));
    CHECK_THAT (num (right.getChild (2), ids::offset), WithinAbs (9.01, 1e-9));
    CHECK (right.getChild (0)[ids::id] != r.getChild (0)[ids::id]);

    // Trimming the left edge can't go past the earliest take's start in the file.
    model::trimRegionStart (r, 0.0, 0.0, nullptr);
    CHECK_THAT (num (r, ids::start), WithinAbs (1.99, 1e-9));

    // JSON round trip keeps the takes.
    const auto projectFile = juce::File ("/tmp/riff-takes/Song.riff");
    juce::String error;
    auto loaded = model::fromJson (model::toJson (p, projectFile), projectFile, error);
    REQUIRE (loaded.isValid());
    const auto lr = loaded.getChild (0).getChild (0);
    CHECK (model::numTakes (lr) == 3);
    CHECK (juce::File (lr.getChild (1)[ids::file].toString()) == file);

    um.beginNewTransaction();
    model::flattenTakes (r, &um);
    CHECK (model::numTakes (r) == 0);
    CHECK_FALSE (r.hasProperty (ids::activeTake));
    um.undo();
    CHECK (model::numTakes (r) == 3);
}

TEST_CASE ("Cycle range is ordered and not undoable", "[model][cycle]")
{
    auto p = model::createProject();
    CHECK_FALSE ((bool) p[ids::cycleEnabled]);
    CHECK_FALSE ((bool) p[ids::countIn]);

    model::setCycle (p, 8.0, 4.0, true);
    CHECK (num (p, ids::cycleStart) == 4.0);
    CHECK (num (p, ids::cycleEnd) == 8.0);
    CHECK ((bool) p[ids::cycleEnabled]);
}
