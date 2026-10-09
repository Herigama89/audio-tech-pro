#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/engine/AutomationWriter.h"
#include "../Source/model/Project.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    struct WriterFixture
    {
        AudioEngine engine;
        AudioFileCache cache { engine.getFormatManager() };
        ProjectPlayer player { engine, cache };
        juce::UndoManager um;
        AutomationWriter writer { engine, player, um };
        juce::ValueTree project = model::createProject();
        juce::ValueTree track = model::addTrack (project, "A", nullptr);
        juce::String id = track[ids::id].toString();
        double time = 0.0;

        WriterFixture()
        {
            engine.prepare (48000.0, 512);
            player.setProject (project);
            writer.setProject (project);
            writer.clock = [this] { return time; };
        }

        juce::ValueTree lane() { return model::laneFor (project, id, model::volumeParam, false); }
    };
}

TEST_CASE ("Read mode: moving a fader while playing writes nothing", "[automation]")
{
    WriterFixture f;
    f.writer.transportChanged (true);
    CHECK_FALSE (f.writer.begin (f.id, model::volumeParam, 0.5));
    CHECK_FALSE (f.writer.move (f.id, model::volumeParam, 0.5));
    f.writer.transportChanged (false);
    CHECK_FALSE (f.lane().isValid());
}

TEST_CASE ("Touch writes while you hold the fader, then goes back", "[automation]")
{
    WriterFixture f;
    f.track.setProperty (ids::automationMode, "touch", nullptr);
    f.writer.transportChanged (true);

    f.time = 1.0;
    REQUIRE (f.writer.begin (f.id, model::volumeParam, 0.5));
    for (int i = 1; i <= 10; ++i)
    {
        f.time = 1.0 + i * 0.1;
        CHECK (f.writer.move (f.id, model::volumeParam, 0.5 - i * 0.02));
    }
    CHECK_THAT (*f.writer.liveValue (f.id, model::volumeParam), WithinAbs (0.3, 1e-9));
    f.writer.end (f.id, model::volumeParam);
    CHECK_FALSE (f.writer.liveValue (f.id, model::volumeParam).has_value());

    const auto lane = f.lane();
    REQUIRE (lane.isValid());
    CHECK_THAT (model::valueAt (lane, 0.5, 1.0), WithinAbs (1.0, 1e-9));    // before: the fader's own value
    CHECK_THAT (model::valueAt (lane, 1.0, 1.0), WithinAbs (0.5, 1e-9));
    CHECK_THAT (model::valueAt (lane, 2.0, 1.0), WithinAbs (0.3, 1e-9));
    CHECK_THAT (model::valueAt (lane, 3.0, 1.0), WithinAbs (1.0, 1e-9));    // after: back to what was there
    CHECK ((double) f.track[ids::volume] == 1.0);                           // the static value didn't move

    // One undo step takes the whole pass away.
    f.um.undo();
    CHECK (f.lane().getNumChildren() == 0);
}

TEST_CASE ("Latch holds the last value until playback stops; Write writes from the start and becomes Latch", "[automation]")
{
    WriterFixture f;
    f.track.setProperty (ids::automationMode, "latch", nullptr);
    f.writer.transportChanged (true);
    f.time = 1.0;
    f.writer.begin (f.id, model::volumeParam, 0.25);
    f.writer.end (f.id, model::volumeParam);
    for (int i = 1; i <= 40; ++i)
    {
        f.time = 1.0 + i * 0.05;
        f.writer.tick();
    }
    f.writer.transportChanged (false);
    CHECK_THAT (model::valueAt (f.lane(), 2.9, 1.0), WithinAbs (0.25, 1e-9));
    CHECK_THAT (model::valueAt (f.lane(), 4.0, 1.0), WithinAbs (1.0, 1e-9));

    // Write: the pan is written from the start, with its value then.
    f.track.setProperty (ids::automationMode, "write", nullptr);
    f.track.setProperty (ids::pan, -0.5, nullptr);
    f.time = 0.0;
    f.writer.transportChanged (true);
    for (int i = 1; i <= 20; ++i)
    {
        f.time = i * 0.05;
        f.writer.tick();
    }
    f.writer.transportChanged (false);
    const auto panLane = model::laneFor (f.project, f.id, model::panParam, false);
    REQUIRE (panLane.isValid());
    CHECK_THAT (model::valueAt (panLane, 0.5, 0.0), WithinAbs (-0.5, 1e-9));
    CHECK (AutomationWriter::modeOf (f.track) == "latch");
}

TEST_CASE ("Off mode: the curves are left out of playback", "[automation]")
{
    WriterFixture f;
    auto lane = model::laneFor (f.project, f.id, model::volumeParam, true);
    model::addPoint (lane, 0.0, 0.25, nullptr);
    f.player.rebuildNow();
    CHECK_THAT (f.writer.displayedValue (f.track, model::volumeParam, 1.0), WithinAbs (0.25, 1e-9));
    f.track.setProperty (ids::automationMode, "off", nullptr);
    CHECK_THAT (f.writer.displayedValue (f.track, model::volumeParam, 1.0), WithinAbs (1.0, 1e-9));
    auto graph = f.player.buildGraph (48000.0);
    REQUIRE (graph->tracks.size() == 1);
    CHECK (graph->tracks[0].volumeAutomation.empty());
}
