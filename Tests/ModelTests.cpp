#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "model/Project.h"
#include "model/ProjectFiles.h"

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
    CHECK (model::numTracks (p) == 0);
}

TEST_CASE ("Adding tracks and regions, with undo and redo", "[model][undo]")
{
    juce::UndoManager um;
    auto p = model::createProject();

    um.beginNewTransaction();
    auto track = model::addTrack (p, "Drums", &um);

    um.beginNewTransaction();
    auto region = model::addRegion (track, juce::File ("/tmp/kick.wav"), 2.0, 4.0, &um);

    REQUIRE (model::numTracks (p) == 1);
    REQUIRE (track.getNumChildren() == 1);
    CHECK (region[ids::name].toString() == "kick");
    CHECK (num (region, ids::start) == 2.0);
    CHECK (model::regionEnd (region) == 6.0);
    CHECK (model::projectEnd (p) == 6.0);
    CHECK (model::findById (p, region[ids::id]) == region);

    um.undo();
    CHECK (track.getNumChildren() == 0);
    um.undo();
    CHECK (model::numTracks (p) == 0);

    um.redo();
    um.redo();
    CHECK (model::numTracks (p) == 1);
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
    REQUIRE (model::numTracks (loaded) == 1);

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
    CHECK (error.contains ("isn't an Audio Tech Pro project"));

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

TEST_CASE ("MIDI regions and notes", "[model][midi]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto audio = model::addTrack (p, "Gtr", nullptr);
    auto inst = model::addTrack (p, "Keys", nullptr, -1, model::instrumentKind);

    CHECK_FALSE (model::isInstrumentTrack (audio));
    CHECK (model::isInstrumentTrack (inst));
    CHECK (inst[ids::instrument].toString() == "synth");

    um.beginNewTransaction();
    auto r = model::addMidiRegion (inst, 2.0, 2.0, &um);
    CHECK (model::isMidiRegion (r));

    um.beginNewTransaction();
    auto n1 = model::addNote (r, 60, 0.0, 0.5, 100, &um);
    auto n2 = model::addNote (r, 64, 0.53, 0.5, 200, &um);   // velocity clamps to 127
    CHECK ((int) n2[ids::velocity] == 127);
    CHECK (r.getNumChildren() == 2);

    um.beginNewTransaction();
    model::moveNote (n1, 1.0, 62, &um);
    model::resizeNote (n1, 0.25, &um);
    CHECK ((int) n1[ids::pitch] == 62);
    CHECK (num (n1, ids::length) == 0.25);
    um.undo();
    CHECK ((int) n1[ids::pitch] == 60);
    CHECK (num (n1, ids::start) == 0.0);

    um.beginNewTransaction();
    model::quantizeNotes (r, 0.125, &um);
    CHECK_THAT (num (n2, ids::start), WithinAbs (0.5, 1e-9));

    // Splitting keeps the notes in place: the right half reads from further in.
    auto right = model::splitRegion (r, 2.5, nullptr);
    REQUIRE (right.isValid());
    CHECK (model::isMidiRegion (right));
    CHECK (num (right, ids::offset) == 0.5);
    CHECK (right.getNumChildren() == 2);

    // Notes survive a save.
    juce::String error;
    const juce::File f ("/tmp/riff-midi/Song.riff");
    auto loaded = model::fromJson (model::toJson (p, f), f, error);
    REQUIRE (loaded.isValid());
    const auto lr = loaded.getChild (1).getChild (0);
    CHECK (model::isMidiRegion (lr));
    CHECK ((int) lr.getChild (0)[ids::pitch] == 60);
}

TEST_CASE ("Recorded MIDI events become notes", "[model][midi]")
{
    using E = model::RecordedMidiEvent;

    SECTION ("pairs ons with offs, relative to the region")
    {
        const std::vector<E> events { { 10.0, 0x90, 60, 100 }, { 10.5, 0x90, 64, 90 },
                                      { 11.0, 0x80, 60, 0 },   { 11.25, 0x90, 64, 0 } };
        const auto notes = model::notesFromEvents (events, 10.0, 12.0);
        REQUIRE (notes.size() == 2);
        CHECK (notes[0].pitch == 60);
        CHECK (notes[0].start == 0.0);
        CHECK (notes[0].length == 1.0);
        CHECK (notes[0].velocity == 100);
        CHECK (notes[1].pitch == 64);
        CHECK (notes[1].start == 0.5);
        CHECK (notes[1].length == 0.75);
    }

    SECTION ("held notes end at the region end; wrapped releases too")
    {
        const std::vector<E> events { { 3.5, 0x90, 48, 80 },                       // never released
                                      { 3.8, 0x90, 50, 80 }, { 2.2, 0x80, 50, 0 } }; // released after a wrap
        const auto notes = model::notesFromEvents (events, 2.0, 4.0);
        REQUIRE (notes.size() == 2);
        CHECK_THAT (notes[0].length, WithinAbs (0.5, 1e-9));
        CHECK_THAT (notes[1].length, WithinAbs (0.2, 1e-9));
    }
}

TEST_CASE ("Mixer chains and inserts", "[model][mixer]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "Vox", nullptr);

    // The mixer stays after the tracks, whatever gets added.
    CHECK (p.getChild (model::numTracks (p)).hasType (ids::MIXER));
    model::addTrack (p, "Gtr", nullptr);
    CHECK (model::numTracks (p) == 2);
    CHECK (p.getChild (2).hasType (ids::MIXER));

    CHECK_FALSE (model::findChain (p, t[ids::id]).isValid());
    auto chain = model::chainFor (p, t[ids::id]);
    CHECK (model::findChain (p, t[ids::id]) == chain);

    um.beginNewTransaction();
    auto eq = model::addBuiltinInsert (chain, "eq", 0, &um);
    auto comp = model::addBuiltinInsert (chain, "compressor", 0, &um); // goes first
    CHECK (chain.getChild (0) == comp);
    CHECK (chain.getChild (1) == eq);

    um.undo();
    CHECK (chain.getNumChildren() == 0);
    um.redo();
    CHECK (chain.getNumChildren() == 2);

    um.beginNewTransaction();
    model::removeInsert (comp, &um);
    CHECK (chain.getNumChildren() == 1);

    // Saved and loaded with the project.
    eq.setProperty ("lowGain", 4.5, nullptr);
    const juce::File f ("/tmp/riff-mix/Song.riff");
    juce::String error;
    auto loaded = model::fromJson (model::toJson (p, f), f, error);
    REQUIRE (loaded.isValid());
    const auto lc = model::findChain (loaded, t[ids::id]);
    REQUIRE (lc.getNumChildren() == 1);
    CHECK (lc.getChild (0)[ids::effect].toString() == "eq");
    CHECK ((double) lc.getChild (0)["lowGain"] == 4.5);
}

TEST_CASE ("Older project files get a mixer after their tracks", "[model][mixer]")
{
    const juce::String json = R"({"riffVersion": 1, "project": {"type": "PROJECT", "tempo": 100,
        "children": [ {"type": "TRACK", "id": "a", "name": "A"}, {"type": "TRACK", "id": "b", "name": "B"} ]}})";
    juce::String error;
    auto p = model::fromJson (json, juce::File ("/tmp/x.riff"), error);
    REQUIRE (p.isValid());
    CHECK (model::numTracks (p) == 2);
    CHECK (p.getChild (2).hasType (ids::MIXER));
}

TEST_CASE ("Buses and sends", "[model][routing]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "Vox", nullptr);
    auto bus = model::addTrack (p, "Verb", nullptr, -1, model::busKind);
    CHECK (model::isBusTrack (bus));
    CHECK_FALSE (model::isBusTrack (t));

    auto chain = model::chainFor (p, t[ids::id]);
    model::addBuiltinInsert (chain, "eq", 0, nullptr);
    um.beginNewTransaction();
    auto send = model::addSend (chain, bus[ids::id], 0.5, &um);

    CHECK (model::sendsOf (chain).size() == 1);
    CHECK (model::insertsOf (chain).size() == 1);   // sends and inserts kept apart
    CHECK (send[ids::target].toString() == bus[ids::id].toString());
    CHECK_FALSE ((bool) send[ids::pre]);

    um.undo();
    CHECK (model::sendsOf (chain).isEmpty());
}

TEST_CASE ("Automation lanes", "[model][automation]")
{
    juce::UndoManager um;
    auto p = model::createProject();
    auto t = model::addTrack (p, "A", nullptr);
    const auto id = t[ids::id].toString();

    CHECK_FALSE (model::laneFor (p, id, model::volumeParam, false).isValid());
    auto lane = model::laneFor (p, id, model::volumeParam, true);
    CHECK (model::laneFor (p, id, model::volumeParam, false) == lane);

    CHECK (model::valueAt (lane, 1.0, 0.7) == 0.7);  // no points: the fader's value

    um.beginNewTransaction();
    auto b = model::addPoint (lane, 2.0, 0.0, &um);
    auto a = model::addPoint (lane, 0.0, 1.0, &um);  // added out of order...
    CHECK (lane.getChild (0) == a);                   // ...but kept in time order
    CHECK (lane.getChild (1) == b);

    CHECK (model::valueAt (lane, -1.0, 0.7) == 1.0); // held before the first point
    CHECK (model::valueAt (lane, 0.5, 0.7) == 0.75); // linear between
    CHECK (model::valueAt (lane, 9.0, 0.7) == 0.0);  // held after the last

    um.beginNewTransaction();
    model::movePoint (a, 3.0, 0.5, &um);              // moves past b
    CHECK (lane.getChild (0) == b);
    CHECK (lane.getChild (1) == a);
    um.undo();
    CHECK (lane.getChild (0) == a);

    um.beginNewTransaction();
    model::removePoint (b, &um);
    CHECK (lane.getNumChildren() == 1);

    // Ranges
    const auto vol = model::rangeOf (p, model::volumeParam);
    CHECK (vol.decibels);
    CHECK (model::rangeOf (p, model::panParam).min == -1.0);

    auto insert = model::addBuiltinInsert (model::chainFor (p, id), "delay", 0, nullptr);
    const auto mix = model::rangeOf (p, model::insertParam (insert[ids::id], "mix"));
    CHECK (mix.max == 1.0f);
    CHECK (mix.name == "Delay Mix");

    // Saved with the project.
    juce::String error;
    const juce::File f ("/tmp/riff-auto/Song.riff");
    auto loaded = model::fromJson (model::toJson (p, f), f, error);
    REQUIRE (loaded.isValid());
    CHECK (model::laneFor (loaded, id, model::volumeParam, false).getNumChildren() == 1);
    CHECK (model::insertsOf (model::findChain (loaded, id)).size() == 1); // the insert went in its chain, not the lane
}

TEST_CASE ("MIDI: quantize strength and swing, scale quantize, transforms, controllers", "[model][midi]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (track, 0.0, 4.0, nullptr);
    auto a = model::addNote (region, 61, 0.11, 0.2, 100, nullptr);   // C#
    auto b = model::addNote (region, 64, 0.52, 0.2, 80, nullptr);

    model::quantizeNotesTo ({ a, b }, 0.25, 0.5, 0.0, 0.0, nullptr);
    CHECK_THAT ((double) a[ids::start], WithinAbs (0.055, 1e-9));   // half way to the grid
    CHECK_THAT ((double) b[ids::start], WithinAbs (0.51, 1e-9));
    model::quantizeNotesTo ({ a, b }, 0.25, 1.0, 0.0, 0.0, nullptr);
    CHECK_THAT ((double) a[ids::start], Catch::Matchers::WithinAbs (0.0, 1e-9));
    CHECK_THAT ((double) b[ids::start], Catch::Matchers::WithinAbs (0.5, 1e-9));
    model::quantizeNotesTo ({ a }, 0.25, 1.0, 0.6, 0.0, nullptr);
    CHECK_THAT ((double) a[ids::start], Catch::Matchers::WithinAbs (0.0, 1e-9));   // on the beat: no swing
    a.setProperty (ids::start, 0.26, nullptr);
    model::quantizeNotesTo ({ a }, 0.25, 1.0, 0.6, 0.0, nullptr);
    CHECK_THAT ((double) a[ids::start], Catch::Matchers::WithinAbs (0.25 + 0.075, 1e-9));   // off the beat: pushed late

    model::scaleQuantize ({ a, b }, 0, false, nullptr);   // C major: C# -> C, E stays
    CHECK ((int) a[ids::pitch] == 60);
    CHECK ((int) b[ids::pitch] == 64);

    model::transformNotes ({ a, b }, model::Transform::crescendo, nullptr);
    CHECK ((int) a[ids::velocity] < (int) b[ids::velocity]);
    model::transformNotes ({ a, b }, model::Transform::legato, nullptr);
    CHECK_THAT ((double) a[ids::length], Catch::Matchers::WithinAbs ((double) b[ids::start] - (double) a[ids::start], 1e-9));
    model::transformNotes ({ a, b }, model::Transform::transposeUp, nullptr);
    CHECK ((int) a[ids::pitch] == 72);

    model::addControllerEvent (region, 1, 1.0, 0.5, nullptr);
    model::addControllerEvent (region, 1, 0.5, 0.25, nullptr);
    model::addControllerEvent (region, model::pitchBend, 0.5, 1.0, nullptr);
    const auto mod = model::controllerEvents (region, 1);
    REQUIRE (mod.size() == 2);
    CHECK ((double) mod[0][ids::start] == 0.5);
    CHECK (model::controllerName (model::pitchBend) == "Pitch Bend");
}

TEST_CASE ("The tempo track: changes on beats, bars and beats follow them", "[model][tempo]")
{
    auto project = model::createProject();
    project.setProperty (ids::tempo, 120.0, nullptr);   // 0.5 s a beat
    auto map = model::tempoMapOf (project);
    CHECK (map.isConstant());
    CHECK_THAT (map.secondsAt (8.0), WithinAbs (4.0, 1e-9));

    // From bar 3 (beat 8), 60 BPM: a beat a second.
    model::addTempoChange (project, 8.0, 60.0, nullptr);
    map = model::tempoMapOf (project);
    CHECK_THAT (map.secondsAt (8.0), WithinAbs (4.0, 1e-9));
    CHECK_THAT (map.secondsAt (10.0), WithinAbs (6.0, 1e-9));
    CHECK_THAT (map.beatAt (6.0), WithinAbs (10.0, 1e-9));
    CHECK_THAT (map.bpmAt (5.0), WithinAbs (60.0, 1e-9));
    CHECK_THAT (map.bpmAt (1.0), WithinAbs (120.0, 1e-9));

    const auto p = model::barPositionAt (project, 6.0);   // beat 10 = bar 3, beat 3
    CHECK (p.bar == 3);
    CHECK (p.beat == 3);
    CHECK (p.sixteenth == 1);

    CHECK_THAT (model::moveByBars (project, 4.0, 1), WithinAbs (8.0, 1e-9));    // bar 3 -> bar 4 takes 4 s now
    CHECK_THAT (model::moveByBars (project, 5.0, -1), WithinAbs (4.0, 1e-9));   // inside bar 3: back to its start
    CHECK_THAT (model::moveByBars (project, 4.0, -1), WithinAbs (2.0, 1e-9));
}

TEST_CASE ("Patches keep a track's sound and put it on another track", "[model][library]")
{
    auto project = model::createProject();
    auto source = model::addTrack (project, "Lead", nullptr, -1, model::instrumentKind);
    source.setProperty (ids::synthCutoff, 900.0, nullptr);
    source.setProperty (ids::consoleHF, 3.0, nullptr);
    model::addBuiltinInsert (model::chainFor (project, source[ids::id]), "delay", 0, nullptr);
    model::addNote (model::addMidiRegion (source, 0.0, 1.0, nullptr), 60, 0.0, 0.5, 100, nullptr);

    const auto patch = model::makePatch (project, source);
    CHECK (patch.getChild (0).getNumChildren() == 0);   // no regions in a patch

    auto target = model::addTrack (project, "Other", nullptr, -1, model::instrumentKind);
    model::addRegion (target, juce::File ("/tmp/x.wav"), 0.0, 1.0, nullptr);
    model::addBuiltinInsert (model::chainFor (project, target[ids::id]), "limiter", 0, nullptr);
    juce::UndoManager um;
    model::applyPatch (project, target, patch, &um);

    CHECK ((double) target[ids::synthCutoff] == 900.0);
    CHECK ((double) target[ids::consoleHF] == 3.0);
    CHECK (target[ids::name].toString() == "Other");        // its own name stays
    CHECK (target.getNumChildren() == 1);                    // and its regions
    const auto inserts = model::insertsOf (model::findChain (project, target[ids::id]));
    REQUIRE (inserts.size() == 1);
    CHECK (inserts[0][ids::effect].toString() == "delay");
    um.undo();
    CHECK (model::insertsOf (model::findChain (project, target[ids::id]))[0][ids::effect].toString() == "limiter");
}

TEST_CASE ("MIDI, markers and the cycle follow tempo changes; audio stays", "[model][tempo]")
{
    auto project = model::createProject();
    project.setProperty (ids::tempo, 120.0, nullptr);
    auto keys = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto midi = model::addMidiRegion (keys, 2.0, 2.0, nullptr);          // bar 2 (beat 4), 4 beats long
    auto note = model::addNote (midi, 60, 0.5, 0.5, 100, nullptr);       // beat 5, a beat long
    auto audio = model::addRegion (model::addTrack (project, "Vox", nullptr), juce::File ("/tmp/v.wav"), 2.0, 1.0, nullptr);
    auto marker = model::addMarker (project, 4.0, "Chorus", nullptr);     // beat 8
    model::setCycle (project, 2.0, 4.0, true);

    model::setTempo (project, 60.0, nullptr);   // half as fast: everything on the grid twice as far
    CHECK_THAT ((double) midi[ids::start], WithinAbs (4.0, 1e-9));
    CHECK_THAT ((double) midi[ids::length], WithinAbs (4.0, 1e-9));
    CHECK_THAT ((double) note[ids::start], WithinAbs (1.0, 1e-9));
    CHECK_THAT ((double) note[ids::length], WithinAbs (1.0, 1e-9));
    CHECK_THAT ((double) marker[ids::time], WithinAbs (8.0, 1e-9));
    CHECK_THAT ((double) project[ids::cycleEnd], WithinAbs (8.0, 1e-9));
    CHECK_THAT ((double) audio[ids::start], WithinAbs (2.0, 1e-9));   // audio isn't stretched
}

TEST_CASE ("Time signature changes along the song", "[model][tempo]")
{
    auto project = model::createProject();
    project.setProperty (ids::tempo, 120.0, nullptr);
    model::addSignatureChange (project, 3, 3, nullptr);   // from bar 3: 3/4
    const auto map = model::tempoMapOf (project);
    CHECK_THAT (map.beatOfBar (2), WithinAbs (8.0, 1e-9));    // bar 3 starts after two 4/4 bars
    CHECK_THAT (map.beatOfBar (3), WithinAbs (11.0, 1e-9));   // bar 4: three beats later
    const auto [bar, inBar] = map.barAndBeat (12.0);
    CHECK (bar == 3);
    CHECK_THAT (inBar, WithinAbs (1.0, 1e-9));

    const auto p = model::barPositionAt (project, 6.0);   // beat 12 = bar 4, beat 2
    CHECK (p.bar == 4);
    CHECK (p.beat == 2);
    CHECK_THAT (model::moveByBars (project, 4.0, 1), WithinAbs (5.5, 1e-9));   // bar 3 (4 s) -> bar 4 (beat 11 = 5.5 s)
}

TEST_CASE ("Section edits: cut, insert silence, duplicate", "[model][arrange]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    auto r = model::addRegion (track, juce::File ("/tmp/a.wav"), 0.0, 10.0, nullptr);
    auto lane = model::laneFor (project, track[ids::id], model::volumeParam, true);
    model::addPoint (lane, 8.0, 0.5, nullptr);
    model::addMarker (project, 6.0, "Chorus", nullptr);

    model::deleteSection (project, 2.0, 4.0, nullptr);   // 2 s cut out
    CHECK (track.getNumChildren() == 2);
    CHECK_THAT (model::projectEnd (project), WithinAbs (8.0, 1e-9));
    CHECK_THAT ((double) lane.getChild (0)[ids::time], WithinAbs (6.0, 1e-9));
    CHECK_THAT ((double) model::sortedMarkers (project)[0][ids::time], WithinAbs (4.0, 1e-9));

    model::insertSilence (project, 1.0, 2.0, nullptr);
    CHECK_THAT (model::projectEnd (project), WithinAbs (9.0, 1e-9));

    model::duplicateSection (project, 5.0, 7.0, nullptr);   // the marker's 2 s, twice
    CHECK_THAT (model::projectEnd (project), WithinAbs (11.0, 1e-9));
    CHECK (model::sortedMarkers (project).size() == 2);
    juce::ignoreUnused (r);
}

TEST_CASE ("Key changes along the song", "[model][key]")
{
    auto project = model::createProject();
    project.setProperty (ids::tempo, 120.0, nullptr);
    project.setProperty (ids::keyRoot, 0, nullptr);
    model::addKeyChange (project, 5, 9, true, nullptr);   // A minor from bar 5 (8 s)
    CHECK (model::keyAt (project, 1.0).first == 0);
    CHECK (model::keyAt (project, 8.5).first == 9);
    CHECK (model::keyAt (project, 8.5).second);
    CHECK (model::keyName (9, true) == "A minor");
}

TEST_CASE ("More MIDI transforms", "[model][midi]")
{
    auto project = model::createProject();
    auto region = model::addMidiRegion (model::addTrack (project, "K", nullptr, -1, model::instrumentKind), 0.0, 4.0, nullptr);
    auto a = model::addNote (region, 60, 0.0, 0.5, 100, nullptr);
    auto b = model::addNote (region, 67, 1.0, 0.5, 100, nullptr);
    model::transformNotes ({ a, b }, model::Transform::invertPitch, nullptr);
    CHECK ((int) a[ids::pitch] == 67);
    CHECK ((int) b[ids::pitch] == 60);
    model::transformNotes ({ a }, model::Transform::noteRepeat, nullptr, 125);
    CHECK (region.getNumChildren() == 5);   // a became four 125 ms repeats
}

#include "../Source/model/Generator.h"

TEST_CASE ("Drummer and Session Players write their regions from their settings", "[model][player]")
{
    auto project = model::createProject();
    project.setProperty (ids::tempo, 100.0, nullptr);
    auto track = generator::player::addPlayerTrack (project, "drums", 0.0, 4, nullptr);
    CHECK (track[ids::instrument].toString() == "drums");
    auto region = track.getChild (0);
    const auto busy = region.getNumChildren();
    CHECK (busy > 10);

    region.setProperty (generator::player::complexity, 0.0, nullptr);
    generator::player::regenerate (region, nullptr);
    const auto simple = region.getNumChildren();
    CHECK (simple < busy);

    // Only kick and snare.
    region.setProperty (generator::player::pieces, 1, nullptr);
    generator::player::regenerate (region, nullptr);
    for (const auto& n : region)
        CHECK (((int) n[ids::pitch] == 36 || (int) n[ids::pitch] == 38 || (int) n[ids::pitch] == 37 || (int) n[ids::pitch] == 39));

    // Everything stays inside the region.
    for (const auto& n : region)
        CHECK ((double) n[ids::start] < (double) region[ids::length]);

    auto bass = generator::player::addPlayerTrack (project, "bass", 0.0, 2, nullptr);
    CHECK (bass.getChild (0).getNumChildren() > 0);
}

TEST_CASE ("Inserts stay in the slot they were put in", "[model][inserts]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    auto chain = model::chainFor (project, track[riff::ids::id].toString());
    auto make = [] (const juce::String& type) { return [type] (juce::ValueTree c, int index) { return model::addBuiltinInsert (c, type, index, nullptr); }; };

    // The fourth slot first: it stays fourth, with three empty slots above.
    auto delay = model::placeInsert (chain, 3, make ("delay"), nullptr);
    CHECK (model::insertSlot (delay) == 3);
    CHECK (! model::insertAtSlot (chain, 0).isValid());
    CHECK (model::insertAtSlot (chain, 3) == delay);

    // Then the first: it processes before the fourth.
    auto eq = model::placeInsert (chain, 0, make ("eq"), nullptr);
    auto inserts = model::insertsOf (chain);
    REQUIRE (inserts.size() == 2);
    CHECK (inserts[0] == eq);
    CHECK (inserts[1] == delay);

    // Choosing again in a used slot replaces what's there.
    auto comp = model::placeInsert (chain, 3, make ("compressor"), nullptr);
    CHECK (model::insertsOf (chain).size() == 2);
    CHECK (model::insertAtSlot (chain, 3) == comp);
}

TEST_CASE ("Region and Track parameters shape the notes as they play", "[model][inspector]")
{
    auto project = model::createProject();   // 120 BPM: a beat is 0.5 s
    auto track = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (track, 0.0, 4.0, nullptr);
    const auto map = model::tempoMapOf (project);

    auto shape = [&] (double start, double length, int pitch, int vel, bool& played)
    {
        double s = start, l = length; int p = pitch, v = vel;
        played = model::MidiPlayback::of (region, track).shape (map, s, l, p, v);
        return std::tuple { s, l, p, v };
    };
    bool played = false;

    // Nothing set: as recorded.
    auto [s0, l0, p0, v0] = shape (0.27, 0.2, 60, 90, played);
    CHECK (played);
    CHECK_THAT (s0, WithinAbs (0.27, 1e-9));
    CHECK (p0 == 60);
    CHECK (v0 == 90);

    // Quantize to 1/8 (half a beat = 0.25 s), with swing on the off-beats.
    region.setProperty (riff::ids::playQuantize, 0.5, nullptr);
    auto [s1, l1, p1, v1] = shape (0.27, 0.2, 60, 90, played);
    CHECK_THAT (s1, WithinAbs (0.25, 1e-9));
    region.setProperty (riff::ids::playSwing, 75.0, nullptr);
    auto [s2, l2, p2, v2] = shape (0.27, 0.2, 60, 90, played);
    CHECK_THAT (s2, WithinAbs (0.25 + 0.125, 1e-9));   // the off-beat moved half a step later

    // Dynamics 50%: velocities move halfway to 64. Gate 50%: half as long. Delay +10 ms.
    region.setProperty (riff::ids::playQuantize, 0.0, nullptr);
    region.setProperty (riff::ids::playDynamics, 50.0, nullptr);
    region.setProperty (riff::ids::playGate, 50.0, nullptr);
    region.setProperty (riff::ids::playDelay, 10.0, nullptr);
    auto [s3, l3, p3, v3] = shape (1.0, 0.4, 60, 120, played);
    CHECK (v3 == 92);
    CHECK_THAT (l3, WithinAbs (0.2, 1e-9));
    CHECK_THAT (s3, WithinAbs (1.01, 1e-9));

    // Key and velocity limits leave notes out; No Transpose ignores transposing.
    track.setProperty (riff::ids::keyLow, 48, nullptr);
    track.setProperty (riff::ids::keyHigh, 72, nullptr);
    shape (1.0, 0.4, 40, 100, played);
    CHECK (! played);
    track.setProperty (riff::ids::velLow, 50, nullptr);
    shape (1.0, 0.4, 60, 20, played);
    CHECK (! played);
    region.setProperty (riff::ids::transpose, 12, nullptr);
    auto [s4, l4, p4, v4] = shape (1.0, 0.4, 60, 100, played);
    CHECK (p4 == 72);
    track.setProperty (riff::ids::noTranspose, true, nullptr);
    auto [s5, l5, p5, v5] = shape (1.0, 0.4, 60, 100, played);
    CHECK (p5 == 60);
}

#include "ui/RoutingMenu.h"

TEST_CASE ("A bus fader never moves track faders; edits go to every selected channel", "[model][mixer]")
{
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", nullptr);
    auto b = model::addTrack (project, "B", nullptr);
    auto bus = model::addTrack (project, "Bus 1", nullptr, -1, model::busKind);
    auto vca = model::addTrack (project, "VCA", nullptr, -1, model::vcaKind);
    const juce::StringArray everything { a[ids::id].toString(), b[ids::id].toString(), bus[ids::id].toString(), vca[ids::id].toString() };

    // Volume: the bus moves alone, a track moves the other selected tracks.
    const auto fromBus = routing::linkedTracks (project, bus, everything, "Volume");
    CHECK (fromBus.size() == 1);
    CHECK (fromBus.contains (bus));
    const auto fromTrack = routing::linkedTracks (project, a, everything, "Volume");
    CHECK (fromTrack.size() == 2);
    CHECK_FALSE (fromTrack.contains (bus));

    // Inserts, sends, pan: every selected channel (but not the VCA); nothing else when it isn't selected.
    const auto acting = routing::actingTracks (project, a, everything);
    CHECK (acting.size() == 3);
    CHECK_FALSE (acting.contains (vca));
    CHECK (routing::actingTracks (project, a, { b[ids::id].toString() }).size() == 1);
}

TEST_CASE ("Dragging an insert: to another slot, onto another channel, or copied with Option", "[model][inserts]")
{
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", nullptr);
    auto b = model::addTrack (project, "B", nullptr);
    const auto aId = a[ids::id].toString(), bId = b[ids::id].toString();
    juce::UndoManager um;

    auto chainA = model::chainFor (project, aId);
    auto eq = model::placeInsert (chainA, 0, [] (juce::ValueTree c, int i) { return model::addBuiltinInsert (c, "eq", i, nullptr); }, nullptr);
    auto comp = model::placeInsert (chainA, 1, [] (juce::ValueTree c, int i) { return model::addBuiltinInsert (c, "compressor", i, nullptr); }, nullptr);
    const auto compId = comp[ids::id].toString();
    auto lane = model::laneFor (project, aId, model::insertParam (compId, "threshold"), true);

    // Up: the compressor into slot 0, the EQ moves down one.
    um.beginNewTransaction();
    model::moveInsert (project, comp, aId, 0, false, &um);
    CHECK (model::insertAtSlot (chainA, 0)[ids::id].toString() == compId);
    CHECK (model::insertAtSlot (chainA, 1) == eq);
    CHECK (model::insertsOf (chainA).getFirst()[ids::id].toString() == compId);   // the chain plays in slot order

    // Onto B: it leaves A, keeps its id (and its settings), and its automation goes with it.
    um.beginNewTransaction();
    auto moved = model::moveInsert (project, comp, bId, 2, false, &um);
    CHECK (moved[ids::id].toString() == compId);
    CHECK (model::insertAtSlot (model::findChain (project, bId), 2) == moved);
    CHECK_FALSE (model::insertAtSlot (chainA, 0).isValid());
    CHECK (lane[ids::owner].toString() == bId);

    // Option: a copy (a new id), the original stays.
    um.beginNewTransaction();
    auto copy = model::moveInsert (project, eq, bId, 2, true, &um);
    CHECK (copy[ids::id].toString() != eq[ids::id].toString());
    CHECK (model::insertAtSlot (chainA, 1) == eq);
    CHECK (model::insertAtSlot (model::findChain (project, bId), 2) == copy);
    CHECK (model::insertAtSlot (model::findChain (project, bId), 3) == moved);   // made room

    // One undo puts each step back.
    um.undo();
    CHECK (model::insertAtSlot (model::findChain (project, bId), 2) == moved);
}

#include "model/Presets.h"

TEST_CASE ("Presets: save and load an insert's settings; a Default every new one starts from", "[model]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "Vox", nullptr);
    auto chain = model::chainFor (project, track[ids::id].toString());
    auto comp = model::addBuiltinInsert (chain, "studioComp", -1, nullptr);
    comp.setProperty ("threshold", -24.0, nullptr);
    comp.setProperty ("ratio", 6.0, nullptr);

    REQUIRE (presets::save (comp, "Vocal Squeeze"));
    CHECK (presets::list (comp).contains ("Vocal Squeeze"));

    comp.setProperty ("threshold", -3.0, nullptr);
    juce::UndoManager um;
    REQUIRE (presets::load (comp, "Vocal Squeeze", &um));
    CHECK ((double) comp["threshold"] == -24.0);
    CHECK (comp["presetName"].toString() == "Vocal Squeeze");
    CHECK (comp[ids::effect].toString() == "studioComp");   // (what it is stays)
    um.undo();
    CHECK ((double) comp["threshold"] == -3.0);

    // Set as Default: the next new one of it starts there; others don't
    comp.setProperty ("threshold", -18.0, nullptr);
    REQUIRE (presets::saveDefault (comp));
    auto next = model::addBuiltinInsert (chain, "studioComp", -1, nullptr);
    CHECK ((double) next["threshold"] == -18.0);
    CHECK ((double) next["ratio"] == 6.0);
    auto other = model::addBuiltinInsert (chain, "channelEq", -1, nullptr);
    CHECK (! other.hasProperty ("threshold"));
    REQUIRE (presets::clearDefault (comp));
    CHECK (! model::addBuiltinInsert (chain, "studioComp", -1, nullptr).hasProperty ("threshold"));

    // A plug-in's preset is its state
    auto plug = model::addPluginInsert (chain, "<PLUGIN name=\"Pro-Q\" format=\"VST3\" manufacturer=\"Maker\"/>", -1, nullptr);
    REQUIRE (presets::save (plug, "Bright", "c3RhdGU="));
    juce::String state;
    REQUIRE (presets::load (plug, "Bright", nullptr, &state));
    CHECK (state == "c3RhdGU=");
    CHECK (plug[ids::pluginState].toString() == "c3RhdGU=");

    CHECK (presets::remove (comp, "Vocal Squeeze"));
    CHECK (! presets::list (comp).contains ("Vocal Squeeze"));
}

TEST_CASE ("Stretch: a region points at its render, remembers its original, and goes back to it", "[model][stretch]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    const juce::File original ("/media/loop.wav"), render1 ("/media/Stretched/loop x1.5.wav"), render2 ("/media/Stretched/loop x0.75.wav");
    auto region = model::addRegion (track, original, 4.0, 2.0, nullptr);
    region.setProperty (ids::offset, 1.0, nullptr);
    region.setProperty (ids::fadeOut, 0.1, nullptr);

    // 1.5 times as long from its end edge: same start, the content's place in the render 1.5 times further in
    auto plan = model::stretchPlanFor (region, 1.5, model::stretchKeepPitch);
    CHECK (plan.source == original);
    CHECK (std::abs (plan.ratio - 1.5) < 1e-9);
    model::applyStretch (region, render1, plan, false, nullptr);
    CHECK (juce::File (region[ids::file].toString()) == render1);
    CHECK (std::abs ((double) region[ids::start] - 4.0) < 1e-9);
    CHECK (std::abs ((double) region[ids::length] - 3.0) < 1e-9);
    CHECK (std::abs ((double) region[ids::offset] - 1.5) < 1e-9);
    CHECK (std::abs ((double) region[ids::fadeOut] - 0.15) < 1e-9);
    CHECK (std::abs (model::stretchOf (region) - 1.5) < 1e-9);

    // Half of that from its start edge: rendered from the original again (x0.75), the end stays at 7 s
    plan = model::stretchPlanFor (region, 0.5, model::stretchKeepPitch);
    CHECK (plan.source == original);
    CHECK (std::abs (plan.ratio - 0.75) < 1e-9);
    model::applyStretch (region, render2, plan, true, nullptr);
    CHECK (std::abs ((double) region[ids::length] - 1.5) < 1e-9);
    CHECK (std::abs ((double) region[ids::start] - 5.5) < 1e-9);
    CHECK (std::abs ((double) region[ids::offset] - 0.75) < 1e-9);

    // Back to the original: its own file, length and offset, and no stretch left on it
    plan = model::stretchPlanFor (region, 1.0 / model::stretchOf (region), model::stretchKeepPitch);
    CHECK (plan.ratio == 1.0);
    model::applyStretch (region, plan.source, plan, false, nullptr);
    CHECK (juce::File (region[ids::file].toString()) == original);
    CHECK (std::abs ((double) region[ids::length] - 2.0) < 1e-9);
    CHECK (std::abs ((double) region[ids::offset] - 1.0) < 1e-9);
    CHECK (! region.hasProperty (ids::stretch));
    CHECK (! region.hasProperty (ids::stretchSource));

    // Limits: never more than 8 times or less than an eighth of the original
    CHECK (model::stretchPlanFor (region, 100.0, model::stretchTape).ratio == model::maxStretch);
    CHECK (model::stretchPlanFor (region, 0.001, model::stretchTape).ratio == model::minStretch);
}

TEST_CASE ("Stretch: a MIDI region's notes spread out with it", "[model][stretch]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (track, 2.0, 2.0, nullptr);
    auto note = model::addNote (region, 60, 1.0, 0.5, 100, nullptr);
    model::stretchMidiRegion (region, 2.0, false, nullptr);
    CHECK (std::abs ((double) region[ids::length] - 4.0) < 1e-9);
    CHECK (std::abs ((double) note[ids::start] - 2.0) < 1e-9);
    CHECK (std::abs ((double) note[ids::length] - 1.0) < 1e-9);
    CHECK (std::abs ((double) region[ids::start] - 2.0) < 1e-9);
}

TEST_CASE ("Stretch: a stretched region's original is saved, copied and counted as its media", "[model][stretch]")
{
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-stretch-" + juce::Uuid().toString());
    temp.createDirectory();
    const auto original = temp.getChildFile ("loop.wav"), rendered = temp.getChildFile ("Stretched/loop x2.wav");
    original.replaceWithText ("x");
    rendered.create();
    rendered.replaceWithText ("y");
    auto project = model::createProject();
    auto region = model::addRegion (model::addTrack (project, "A", nullptr), original, 0.0, 1.0, nullptr);
    model::applyStretch (region, rendered, model::stretchPlanFor (region, 2.0, model::stretchKeepPitch), false, nullptr);

    const auto media = model::files::audioFilesOf (project);
    CHECK (media.contains (original));
    CHECK (media.contains (rendered));

    // saved relative to the project, and read back
    const auto file = temp.getChildFile ("Song" + model::fileExtension);
    juce::String error;
    REQUIRE (model::save (project, file, error));
    CHECK (! file.loadFileAsString().contains (temp.getFullPathName()));
    auto loaded = model::load (file, error);
    REQUIRE (loaded.isValid());
    const auto r = loaded.getChild (0).getChild (0);
    CHECK (juce::File (r[ids::stretchSource].toString()) == original);
    temp.deleteRecursively();
}

TEST_CASE ("Reset Channels: every channel and the master back to a fresh start", "[model][reset]")
{
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", nullptr), b = model::addTrack (project, "B", nullptr);
    for (auto t : { a, b })
    {
        t.setProperty (ids::volume, 0.3, nullptr);
        t.setProperty (ids::pan, -0.7, nullptr);
        t.setProperty (ids::mute, true, nullptr);
        t.setProperty (ids::consoleHF, 6.0, nullptr);
        t.setProperty ("tapeOn", true, nullptr);
        t.setProperty ("preOn", true, nullptr);
        model::addBuiltinInsert (model::chainFor (project, t[ids::id].toString()), "gain", -1, nullptr);
    }
    project.setProperty (ids::masterGain, 0.2, nullptr);
    project.setProperty ("masterTapeOn", true, nullptr);
    model::addBuiltinInsert (model::chainFor (project, model::masterOwner), "gain", -1, nullptr);

    // Only the selected one
    model::ResetOptions onlyA;
    onlyA.onlyTracks = { a[ids::id].toString() };
    model::resetChannels (project, onlyA, nullptr);
    CHECK ((double) a[ids::volume] == 1.0);
    CHECK ((double) a[ids::pan] == 0.0);
    CHECK (! (bool) a[ids::mute]);
    CHECK (! a.hasProperty (ids::consoleHF));
    CHECK (! (bool) a["tapeOn"]);
    CHECK ((bool) model::insertsOf (model::findChain (project, a[ids::id].toString()))[0][ids::bypass]);
    CHECK ((double) b[ids::volume] == 0.3);
    CHECK ((double) project[ids::masterGain] == 0.2);

    // Everything, inserts taken out
    model::ResetOptions all;
    all.removeInserts = true;
    juce::UndoManager um;
    model::resetChannels (project, all, &um);
    CHECK ((double) b[ids::volume] == 1.0);
    CHECK (model::insertsOf (model::findChain (project, b[ids::id].toString())).isEmpty());
    CHECK (model::insertsOf (model::findChain (project, model::masterOwner)).isEmpty());
    CHECK (std::abs ((double) project[ids::masterGain] - 0.8) < 1e-9);
    CHECK (! (bool) project["masterTapeOn"]);

    // One undo brings it all back
    um.undo();
    CHECK ((double) b[ids::volume] == 0.3);
    CHECK (model::insertsOf (model::findChain (project, b[ids::id].toString())).size() == 1);
}
