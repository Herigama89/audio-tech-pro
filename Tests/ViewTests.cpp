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
