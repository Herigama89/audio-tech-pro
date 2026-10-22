#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "ui/TimelineState.h"
#include "model/Project.h"

using namespace riff;

TEST_CASE ("Tracks have their own heights", "[view]")
{
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", nullptr);
    auto b = model::addTrack (project, "B", nullptr);
    model::addTrack (project, "C", nullptr);

    TimelineState t;
    t.project = project;
    const auto h = TimelineState::trackHeight, ruler = TimelineState::rulerHeight;

    CHECK (t.trackTop (2) == ruler + 2 * h);
    CHECK (t.contentHeight() == 4 * h);

    b.setProperty (ids::height, 150, nullptr);
    a.setProperty (ids::height, 5, nullptr);          // too small: held at the minimum
    CHECK (t.heightOf (0) == TimelineState::minTrackHeight);
    CHECK (t.trackTop (1) == ruler + TimelineState::minTrackHeight);
    CHECK (t.trackTop (2) == ruler + TimelineState::minTrackHeight + 150);

    // Finding the track under the mouse
    CHECK (t.trackIndexAt (ruler - 1) == -1);
    CHECK (t.trackIndexAt (ruler) == 0);
    CHECK (t.trackIndexAt (t.trackTop (1) + 149) == 1);
    CHECK (t.trackIndexAt (t.trackTop (2)) == 2);
    CHECK (t.trackIndexAt (t.trackTop (2) + h) == 3);       // the "New track" row
    CHECK (t.trackIndexAt (t.trackTop (2) + 2 * h + 1) == 4);

    // Scrolled
    t.scrollY = 20;
    CHECK (t.trackTop (1) == ruler + TimelineState::minTrackHeight - 20);
    CHECK (t.trackIndexAt (ruler + TimelineState::minTrackHeight - 21) == 0);

    // The MIXER node after the tracks isn't a track.
    CHECK (t.heightOf (3) == h);
}

namespace
{
    juce::String order (const juce::ValueTree& project)
    {
        juce::String s;
        for (int i = 0; i < model::numTracks (project); ++i)
            s << project.getChild (i)[ids::name].toString();
        return s;
    }
}

TEST_CASE ("Moving tracks, one or several, with undo", "[view][model]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    for (auto name : { "A", "B", "C", "D", "E" })
        model::addTrack (project, name, nullptr);
    auto track = [&project] (int i) { return project.getChild (i); };

    model::moveTracks (project, { track (0) }, 3, &um);        // A before D
    CHECK (order (project) == "BCADE");
    model::moveTracks (project, { track (4) }, 0, &um);        // E to the top
    CHECK (order (project) == "EBCAD");
    model::moveTracks (project, { track (1), track (3) }, 5, &um);   // B and A to the end, in order
    CHECK (order (project) == "ECDBA");
    model::moveTracks (project, { track (3), track (1) }, 1, &um);   // B and C after E (kept in their order)
    CHECK (order (project) == "ECBDA");
    CHECK (project.getChild (project.getNumChildren() - 1).hasType (ids::MIXER));   // the mixer stays last

    um.beginNewTransaction();
    model::moveTracks (project, { track (0) }, 5, &um);
    CHECK (order (project) == "CBDAE");
    um.undo();
    CHECK (order (project) == "ECBDA");
}

TEST_CASE ("Selecting several tracks; hidden tracks", "[view]")
{
    auto project = model::createProject();
    for (auto name : { "A", "B", "C", "D" })
        model::addTrack (project, name, nullptr);
    auto id = [&project] (int i) { return project.getChild (i)[ids::id].toString(); };

    TimelineState t;
    t.project = project;
    t.selectOnly (id (1));
    CHECK (t.selectedTrackIds() == juce::StringArray { id (1) });

    t.extendTrackSelection (id (3));                    // Shift-click: B to D
    CHECK (t.selectedTrackIds() == juce::StringArray { id (1), id (2), id (3) });
    CHECK (t.selectedTrackId == id (3));

    t.toggleTrackSelection (id (2));                    // Cmd-click C off
    CHECK (t.selectedTrackIds() == juce::StringArray { id (1), id (3) });
    t.toggleTrackSelection (id (0));                    // and A on
    CHECK (t.selectedTrackIds() == juce::StringArray { id (0), id (1), id (3) });

    // Setting the selected track directly means just that one.
    t.selectedTrackId = id (2);
    CHECK (t.selectedTrackIds() == juce::StringArray { id (2) });

    // Hidden: no height (left out of layout and of Shift selections) unless shown.
    project.getChild (1).setProperty (ids::hidden, true, nullptr);
    CHECK (t.heightOf (1) == 0);
    CHECK (t.trackTop (2) == t.trackTop (1));
    CHECK (t.trackIndexAt (t.trackTop (2) + 1) == 2);
    t.selectOnly (id (0));
    t.extendTrackSelection (id (2));
    CHECK (t.selectedTrackIds() == juce::StringArray { id (0), id (2) });
    t.showHiddenTracks = true;
    CHECK (t.heightOf (1) == TimelineState::trackHeight);
}


TEST_CASE ("Mixer zones: buses follow the mixer's choice, a channel's own choice wins", "[view]")
{
    auto project = model::createProject();
    auto vox = model::addTrack (project, "Vox", nullptr);
    auto verb = model::addTrack (project, "Verb", nullptr, -1, model::busKind);

    CHECK (model::mixerZoneOf (project, vox).isEmpty());
    CHECK (model::mixerZoneOf (project, verb).isEmpty());

    project.setProperty ("mixerBusZone", "right", nullptr);
    CHECK (model::mixerZoneOf (project, verb) == "right");
    CHECK (model::mixerZoneOf (project, vox).isEmpty());   // (only buses follow it)

    vox.setProperty ("mixerZone", "left", nullptr);
    verb.setProperty ("mixerZone", "none", nullptr);
    CHECK (model::mixerZoneOf (project, vox) == "left");
    CHECK (model::mixerZoneOf (project, verb).isEmpty());   // brought back with the tracks
}
