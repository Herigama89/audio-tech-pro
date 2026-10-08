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
