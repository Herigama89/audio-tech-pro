#include "ArrangeCanvas.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    constexpr float edgeGrab = 6.0f;   // pixels at a region's edge that trim instead of move
    constexpr int regionHeader = 16;
}

ArrangeCanvas::ArrangeCanvas (juce::UndoManager& um, TimelineState& tl, AudioEngine& e,
                              AudioFileCache& fc, WaveformCache& wf)
    : undoManager (um), timeline (tl), engine (e), fileCache (fc), waveforms (wf)
{
    setWantsKeyboardFocus (true); // key presses bubble up to MainComponent
    timeline.addChangeListener (this);
}

ArrangeCanvas::~ArrangeCanvas()
{
    timeline.removeChangeListener (this);
    project.removeListener (this);
}

void ArrangeCanvas::setProject (juce::ValueTree newProject)
{
    project.removeListener (this);
    project = newProject;
    project.addListener (this);
    dragMode = DragMode::none;
    dragRegion = {};
    repaint();
}

//==============================================================================
// Grid
//==============================================================================
double ArrangeCanvas::gridSeconds() const
{
    const auto beat = model::secondsPerBeat (project);
    const auto barLength = beat * (int) project.getProperty (ids::timeSigNumerator, 4);

    if (beat * timeline.pixelsPerSecond >= 14.0)
        return beat;

    double bars = 1.0;
    while (barLength * bars * timeline.pixelsPerSecond < 40.0)
        bars *= 2.0;
    return barLength * bars;
}

double ArrangeCanvas::snap (double t) const
{
    const auto grid = gridSeconds();
    return juce::jmax (0.0, std::round (t / grid) * grid);
}

//==============================================================================
// Painting
//==============================================================================
void ArrangeCanvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const int numTracks = model::numTracks (project);

    // Lanes
    for (int i = 0; i < numTracks; ++i)
    {
        const auto top = timeline.trackTop (i);
        if (top + TimelineState::trackHeight < TimelineState::rulerHeight || top > getHeight())
            continue;

        const auto track = project.getChild (i);
        const bool selected = timeline.selectedTrackId == track[ids::id].toString();

        g.setColour (selected ? theme::selection.withAlpha (0.6f) : (i % 2 == 0 ? theme::laneA : theme::laneB));
        g.fillRect (0, top, getWidth(), TimelineState::trackHeight);
    }

    paintGrid (g);

    // Tint the lanes inside an active cycle.
    if ((bool) project[ids::cycleEnabled])
    {
        const auto c = cycleBounds();
        g.setColour (theme::solo.withAlpha (0.05f));
        g.fillRect (c.getX(), (float) TimelineState::rulerHeight, c.getWidth(), (float) getHeight());
    }

    for (int i = 0; i < numTracks; ++i)
    {
        const auto track = project.getChild (i);
        for (const auto& region : track)
            paintRegion (g, region, i);
    }

    paintLiveTakes (g);

    // File-drop target highlight
    if (dropHighlightTrack != -2)
    {
        const bool newTrack = dropHighlightTrack < 0 || dropHighlightTrack >= numTracks;
        const auto top = newTrack ? timeline.trackTop (numTracks) : timeline.trackTop (dropHighlightTrack);

        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (0, top, getWidth(), TimelineState::trackHeight);
        g.setColour (theme::accent);
        g.drawRect (0, top, getWidth(), TimelineState::trackHeight, 2);

        if (newTrack)
        {
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("New track", 12, top, 200, TimelineState::trackHeight, juce::Justification::centredLeft);
        }
    }

    if (numTracks == 0 && dropHighlightTrack == -2)
    {
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Drag audio files here, or click Import\n"
                          "Space: play / stop    Cmd+Z: undo    Cmd+T: split    Delete: remove region",
                          getLocalBounds().withTrimmedTop (TimelineState::rulerHeight).reduced (20),
                          juce::Justification::centred, 3);
    }

    paintRuler (g);

    // Playhead
    const auto px = timeline.timeToX (engine.getPlayheadSeconds());
    if (px >= 0.0f && px <= (float) getWidth())
    {
        g.setColour (juce::Colours::white);
        g.fillRect (px - 0.5f, 0.0f, 1.5f, (float) getHeight());

        juce::Path head;
        head.addTriangle (px - 6.0f, 0.0f, px + 6.0f, 0.0f, px, 9.0f);
        g.fillPath (head);
    }
}

void ArrangeCanvas::paintGrid (juce::Graphics& g)
{
    const auto beat = model::secondsPerBeat (project);
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    const auto grid = gridSeconds();
    const auto barLength = beat * beatsPerBar;

    const auto t0 = timeline.scrollSeconds;
    const auto t1 = timeline.xToTime ((float) getWidth());
    const auto bottom = (float) juce::jmin (getHeight(), timeline.trackTop (model::numTracks (project)));

    for (auto i = (juce::int64) std::floor (t0 / grid); (double) i * grid <= t1; ++i)
    {
        const auto t = (double) i * grid;
        const auto x = timeline.timeToX (t);
        const bool isBar = std::abs (std::remainder (t, barLength)) < 1e-6;

        g.setColour (isBar ? theme::gridBar : theme::gridBeat);
        g.fillRect (x, (float) TimelineState::rulerHeight, 1.0f, bottom - (float) TimelineState::rulerHeight);
    }
}

void ArrangeCanvas::paintRuler (juce::Graphics& g)
{
    const auto beat = model::secondsPerBeat (project);
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    const auto barLength = beat * beatsPerBar;
    const auto rh = (float) TimelineState::rulerHeight;

    g.setColour (theme::toolbar);
    g.fillRect (0.0f, 0.0f, (float) getWidth(), rh);

    // Label every Nth bar so numbers don't collide.
    int labelEvery = 1;
    while (barLength * labelEvery * timeline.pixelsPerSecond < 36.0)
        labelEvery *= 2;

    const bool showBeats = beat * timeline.pixelsPerSecond >= 14.0;
    const auto t1 = timeline.xToTime ((float) getWidth());

    g.setFont (juce::FontOptions (11.0f));

    for (auto bar = (juce::int64) std::floor (timeline.scrollSeconds / barLength); (double) bar * barLength <= t1; ++bar)
    {
        const auto x = timeline.timeToX ((double) bar * barLength);

        if (bar % labelEvery == 0)
        {
            g.setColour (theme::dimText);
            g.fillRect (x, rh * 0.5f, 1.0f, rh * 0.5f);
            g.setColour (theme::text);
            g.drawText (juce::String (bar + 1), (int) x + 4, (int) (rh * 0.5f), 40, (int) (rh * 0.5f) - 2, juce::Justification::centredLeft);
        }

        if (showBeats)
        {
            g.setColour (theme::outline);
            for (int b = 1; b < beatsPerBar; ++b)
                g.fillRect (timeline.timeToX ((double) bar * barLength + b * beat), rh * 0.75f, 1.0f, rh * 0.25f);
        }
    }

    g.setColour (theme::outline);
    g.fillRect (0.0f, rh - 1.0f, (float) getWidth(), 1.0f);

    paintCycle (g);
}

juce::Rectangle<float> ArrangeCanvas::cycleBounds() const
{
    const auto x0 = timeline.timeToX (project.getProperty (ids::cycleStart, 0.0));
    const auto x1 = timeline.timeToX (project.getProperty (ids::cycleEnd, 0.0));
    return { x0, 2.0f, juce::jmax (2.0f, x1 - x0), (float) TimelineState::rulerHeight * 0.5f - 3.0f };
}

void ArrangeCanvas::paintCycle (juce::Graphics& g)
{
    // The cycle lives in the top half of the ruler, as in Logic.
    const auto r = cycleBounds();
    const bool on = project[ids::cycleEnabled];

    g.setColour (on ? theme::solo.withAlpha (0.85f) : theme::outline.brighter (0.2f));
    g.fillRoundedRectangle (r, 3.0f);
}

void ArrangeCanvas::paintMidiNotes (juce::Graphics& g, const juce::ValueTree& region,
                                    juce::Rectangle<float> body, juce::Colour colour)
{
    // A miniature piano roll, scaled to the notes' pitch range.
    const double offset = region[ids::offset], length = region[ids::length];
    int low = 127, high = 0;

    for (const auto& note : region)
    {
        if (note.hasType (ids::NOTE))
        {
            low = juce::jmin (low, (int) note[ids::pitch]);
            high = juce::jmax (high, (int) note[ids::pitch]);
        }
    }

    if (low > high)
        return;

    low = juce::jmax (0, low - 2);
    high = juce::jmin (127, high + 2);
    const auto rowH = juce::jmin (4.0f, body.getHeight() / (float) (high - low + 1));

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (body.toNearestInt());
    g.setColour (colour.brighter (0.6f));

    for (const auto& note : region)
    {
        if (! note.hasType (ids::NOTE))
            continue;

        const double s = note[ids::start];
        if (s < offset || s >= offset + length)
            continue;

        const auto x = body.getX() + (float) ((s - offset) * timeline.pixelsPerSecond);
        const auto w = juce::jmax (1.5f, (float) ((double) note[ids::length] * timeline.pixelsPerSecond));
        const auto y = body.getBottom() - (float) ((int) note[ids::pitch] - low + 1) * (body.getHeight() / (float) (high - low + 1));
        g.fillRect (x, y, w, juce::jmax (1.5f, rowH));
    }
}

void ArrangeCanvas::paintLiveTakes (juce::Graphics& g)
{
    for (const auto& take : engine.getLiveTakes())
    {
        const auto track = model::findById (project, take.trackId);
        if (! track.isValid())
            continue;

        const auto index = project.indexOf (track);
        const auto start = take.startSeconds >= 0.0 ? take.startSeconds : engine.getPlayheadSeconds();
        const auto x = timeline.timeToX (start);
        const auto w = juce::jmax (2.0f, (float) (take.lengthSeconds * timeline.pixelsPerSecond));
        const juce::Rectangle<float> r (x, (float) timeline.trackTop (index) + 3.0f, w, (float) TimelineState::trackHeight - 6.0f);

        g.setColour (theme::danger.withAlpha (0.3f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::danger);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.5f);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("Recording", r.withHeight (16.0f).reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

juce::Rectangle<float> ArrangeCanvas::regionBounds (const juce::ValueTree& region, int trackIndex) const
{
    const auto x = timeline.timeToX (region[ids::start]);
    const auto w = (float) ((double) region[ids::length] * timeline.pixelsPerSecond);
    return { x, (float) timeline.trackTop (trackIndex) + 3.0f, juce::jmax (2.0f, w), (float) TimelineState::trackHeight - 6.0f };
}

void ArrangeCanvas::paintRegion (juce::Graphics& g, const juce::ValueTree& region, int trackIndex)
{
    const auto bounds = regionBounds (region, trackIndex);
    const auto visible = bounds.getIntersection (getLocalBounds().toFloat().withTrimmedTop ((float) TimelineState::rulerHeight));

    if (visible.isEmpty())
        return;

    const auto colour = model::trackColour (region.getParent());
    const bool selected = timeline.selectedRegionId == region[ids::id].toString();
    const juce::File file (region[ids::file].toString());
    const bool isMidi = model::isMidiRegion (region);
    const bool missing = ! isMidi && ! file.existsAsFile();

    g.setColour ((missing ? theme::danger : colour).withAlpha (0.25f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Header strip with the region name
    auto header = bounds.withHeight ((float) regionHeader);
    g.setColour ((missing ? theme::danger : colour).withAlpha (0.85f));
    g.fillRoundedRectangle (header, 4.0f);
    g.fillRect (header.withTrimmedTop (4.0f));

    g.setColour (juce::Colour (0xff15161a));
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    const auto textArea = header.withLeft (juce::jmax (header.getX(), 0.0f)).reduced (5.0f, 0.0f);
    auto label = region[ids::name].toString();

    if (const auto takes = model::numTakes (region); takes > 0)
        label << "  (" << ((int) region.getProperty (ids::activeTake, 0) + 1) << " of " << takes << ")  "
              << juce::String::fromUTF8 ("\xe2\x96\xbe"); // down-pointing triangle

    if (missing)
        label << "  (file missing)";

    g.drawText (label, textArea, juce::Justification::centredLeft, true);

    if (isMidi)
        paintMidiNotes (g, region, bounds.withTrimmedTop ((float) regionHeader + 1.0f).reduced (0.0f, 3.0f), colour);

    // Waveform, drawn only for the visible part
    if (! missing && ! isMidi)
    {
        auto& thumb = waveforms.get (file);
        const auto body = bounds.withTrimmedTop ((float) regionHeader + 1.0f).reduced (0.0f, 2.0f);
        const auto x0 = juce::jmax (body.getX(), visible.getX());
        const auto x1 = juce::jmin (body.getRight(), visible.getRight());

        if (thumb.getTotalLength() > 0.0 && x1 > x0)
        {
            const double offset = region[ids::offset];
            const auto tStart = offset + (x0 - body.getX()) / timeline.pixelsPerSecond;
            const auto tEnd   = offset + (x1 - body.getX()) / timeline.pixelsPerSecond;

            g.setColour (colour.brighter (0.5f).withAlpha (0.9f));
            thumb.drawChannels (g, juce::Rectangle<float> (x0, body.getY(), x1 - x0, body.getHeight()).toNearestInt(),
                                tStart, tEnd, (float) (double) region.getProperty (ids::gain, 1.0));
        }
    }

    g.setColour (selected ? juce::Colours::white : colour.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);
}

//==============================================================================
// Mouse
//==============================================================================
juce::ValueTree ArrangeCanvas::regionAt (juce::Point<float> pos, int& trackIndexOut) const
{
    const auto index = timeline.trackIndexAt ((int) pos.y);
    trackIndexOut = index;

    if (index < 0 || index >= model::numTracks (project))
        return {};

    const auto track = project.getChild (index);

    // Last drawn is on top, so search backwards.
    for (int r = track.getNumChildren(); --r >= 0;)
        if (regionBounds (track.getChild (r), index).contains (pos))
            return track.getChild (r);

    return {};
}

void ArrangeCanvas::setPlayheadFromX (float x)
{
    engine.setPlayheadSeconds (juce::jmax (0.0, timeline.xToTime (x)));
    repaint();
}

void ArrangeCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragMode = DragMode::none;

    clickedTakeHeader = false;

    if (e.y < TimelineState::rulerHeight)
    {
        // The playhead and cycle stay put while recording, so takes line up.
        if (engine.isRecording())
            return;

        if (e.y < TimelineState::rulerHeight / 2)
        {
            const auto c = cycleBounds();
            dragMouseDownTime = timeline.xToTime ((float) e.x);
            dragCycleStart = project.getProperty (ids::cycleStart, 0.0);
            dragCycleEnd = project.getProperty (ids::cycleEnd, 0.0);
            dragMode = (e.position.x >= c.getX() && e.position.x <= c.getRight()) ? DragMode::cycleMove : DragMode::cycleNew;
            return;
        }

        dragMode = DragMode::scrub;
        setPlayheadFromX ((float) e.x);
        return;
    }

    int trackIndex = -1;
    auto region = regionAt (e.position, trackIndex);

    if (! region.isValid())
    {
        timeline.selectedRegionId = {};
        if (trackIndex >= 0 && trackIndex < model::numTracks (project))
            timeline.selectedTrackId = project.getChild (trackIndex)[ids::id].toString();
        timeline.changed();
        return;
    }

    timeline.selectedRegionId = region[ids::id].toString();
    timeline.selectedTrackId = region.getParent()[ids::id].toString();
    timeline.changed();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem ("Split at playhead", [this, region]
        {
            undoManager.beginNewTransaction ("Split region");
            model::splitRegion (region, engine.getPlayheadSeconds(), &undoManager);
        });
        menu.addItem ("Delete", [this, region]
        {
            undoManager.beginNewTransaction ("Delete region");
            model::deleteRegion (region, &undoManager);
        });

        if (model::numTakes (region) > 0)
        {
            juce::PopupMenu takesMenu;
            addTakeItems (takesMenu, region);
            menu.addSeparator();
            menu.addSubMenu ("Takes", takesMenu);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

    // A click (not a drag) on a take folder's name bar opens its take menu.
    if (model::numTakes (region) > 0 && e.position.y < regionBounds (region, trackIndex).getY() + 16.0f)
        clickedTakeHeader = true;

    const auto bounds = regionBounds (region, trackIndex);
    const bool wideEnough = bounds.getWidth() > edgeGrab * 3.0f;

    if (wideEnough && e.position.x - bounds.getX() < edgeGrab)
        dragMode = DragMode::trimStart;
    else if (wideEnough && bounds.getRight() - e.position.x < edgeGrab)
        dragMode = DragMode::trimEnd;
    else
        dragMode = DragMode::move;

    undoManager.beginNewTransaction (dragMode == DragMode::move ? "Move region" : "Trim region");

    dragRegion = region;
    dragOriginalStart = region[ids::start];
    dragMouseDownTime = timeline.xToTime ((float) e.x);
    dragSourceLength = fileCache.getLengthSeconds (juce::File (region[ids::file].toString()));
}

void ArrangeCanvas::mouseDrag (const juce::MouseEvent& e)
{
    // Hold Cmd (Ctrl on Windows/Linux) to move freely without snapping.
    const bool useSnap = ! e.mods.isCommandDown();
    auto maybeSnap = [&] (double t) { return useSnap ? snap (t) : juce::jmax (0.0, t); };

    switch (dragMode)
    {
        case DragMode::scrub:
            setPlayheadFromX ((float) e.x);
            break;

        case DragMode::move:
        {
            if (! dragRegion.isValid() || ! dragRegion.getParent().isValid())
                break;

            // Ignore tiny wobbles so a click never nudges a region onto the grid.
            if (e.getDistanceFromDragStart() < 4)
                break;

            const auto newStart = maybeSnap (dragOriginalStart + timeline.xToTime ((float) e.x) - dragMouseDownTime);
            const auto targetIndex = juce::jlimit (0, model::numTracks (project) - 1, timeline.trackIndexAt (e.y));
            auto target = project.getChild (targetIndex);

            // MIDI belongs on instrument tracks and audio on audio tracks.
            if (model::isMidiRegion (dragRegion) != model::isInstrumentTrack (target))
                target = dragRegion.getParent();

            model::moveRegion (dragRegion, target, newStart, &undoManager);
            timeline.selectedTrackId = target[ids::id].toString();
            break;
        }

        case DragMode::trimStart:
            model::trimRegionStart (dragRegion, maybeSnap (timeline.xToTime ((float) e.x)), dragSourceLength, &undoManager);
            break;

        case DragMode::trimEnd:
            model::trimRegionEnd (dragRegion, maybeSnap (timeline.xToTime ((float) e.x)), dragSourceLength, &undoManager);
            break;

        case DragMode::cycleNew:
        {
            if (e.getDistanceFromDragStart() < 4)
                break;

            const auto a = maybeSnap (dragMouseDownTime);
            const auto b = maybeSnap (timeline.xToTime ((float) e.x));
            if (std::abs (b - a) >= model::minRegionLength)
                model::setCycle (project, juce::jmin (a, b), juce::jmax (a, b), true);
            break;
        }

        case DragMode::cycleMove:
        {
            if (e.getDistanceFromDragStart() < 4)
                break;

            const auto length = dragCycleEnd - dragCycleStart;
            const auto newStart = maybeSnap (dragCycleStart + timeline.xToTime ((float) e.x) - dragMouseDownTime);
            model::setCycle (project, newStart, newStart + length, true);
            break;
        }

        case DragMode::none:
            break;
    }
}

void ArrangeCanvas::mouseUp (const juce::MouseEvent& e)
{
    const bool wasClick = e.getDistanceFromDragStart() < 4;

    // Clicking the cycle bar turns the cycle on and off.
    if (dragMode == DragMode::cycleMove && wasClick)
        project.setProperty (ids::cycleEnabled, ! (bool) project[ids::cycleEnabled], nullptr);

    if (clickedTakeHeader && wasClick && dragRegion.isValid())
        showTakeMenu (dragRegion);

    clickedTakeHeader = false;
    dragMode = DragMode::none;
    dragRegion = {};
}

void ArrangeCanvas::addTakeItems (juce::PopupMenu& menu, juce::ValueTree region)
{
    const int active = region.getProperty (ids::activeTake, 0);

    for (int i = 0; i < region.getNumChildren(); ++i)
    {
        const auto take = region.getChild (i);
        if (! take.hasType (ids::TAKE))
            continue;

        menu.addItem (take[ids::name].toString(), true, i == active, [this, region, i]
        {
            undoManager.beginNewTransaction ("Choose take");
            model::setActiveTake (region, i, &undoManager);
        });
    }

    menu.addSeparator();
    menu.addItem ("Keep only this take", [this, region]
    {
        undoManager.beginNewTransaction ("Keep only this take");
        model::flattenTakes (region, &undoManager);
    });
}

void ArrangeCanvas::showTakeMenu (juce::ValueTree region)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Takes");
    addTakeItems (menu, region);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void ArrangeCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < TimelineState::rulerHeight)
        return;

    int trackIndex = -1;
    auto region = regionAt (e.position, trackIndex);

    if (region.isValid())
    {
        if (model::isMidiRegion (region) && onOpenPianoRoll)
            onOpenPianoRoll (region);
        return;
    }

    // Double-click an empty spot on an instrument track: a new one-bar MIDI region there.
    if (trackIndex < 0 || trackIndex >= model::numTracks (project))
        return;

    auto track = project.getChild (trackIndex);
    if (! model::isInstrumentTrack (track))
        return;

    const auto bar = model::secondsPerBeat (project) * (int) project.getProperty (ids::timeSigNumerator, 4);
    const auto start = std::floor (timeline.xToTime ((float) e.x) / bar) * bar;

    undoManager.beginNewTransaction ("New MIDI region");
    auto newRegion = model::addMidiRegion (track, juce::jmax (0.0, start), bar, &undoManager);

    timeline.selectedRegionId = newRegion[ids::id].toString();
    timeline.selectedTrackId = track[ids::id].toString();
    timeline.changed();

    if (onOpenPianoRoll)
        onOpenPianoRoll (newRegion);
}

void ArrangeCanvas::mouseMove (const juce::MouseEvent& e)
{
    int trackIndex = -1;
    const auto region = regionAt (e.position, trackIndex);
    auto newCursor = juce::MouseCursor::NormalCursor;

    if (region.isValid())
    {
        const auto b = regionBounds (region, trackIndex);
        if (b.getWidth() > edgeGrab * 3.0f
            && (e.position.x - b.getX() < edgeGrab || b.getRight() - e.position.x < edgeGrab))
            newCursor = juce::MouseCursor::LeftRightResizeCursor;
    }

    setMouseCursor (newCursor);
}

void ArrangeCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround ((float) e.x, std::exp (wheel.deltaY * 2.0));
    }
    else
    {
        // Trackpads give both axes; a mouse wheel with Shift scrolls sideways.
        auto dx = wheel.deltaX, dy = wheel.deltaY;
        if (e.mods.isShiftDown() && juce::exactlyEqual (dx, 0.0f))
            std::swap (dx, dy);

        constexpr float pixelsPerUnit = 400.0f;

        timeline.scrollSeconds = juce::jmax (0.0, timeline.scrollSeconds - dx * pixelsPerUnit / timeline.pixelsPerSecond);

        const auto contentHeight = (model::numTracks (project) + 1) * TimelineState::trackHeight;
        const auto maxScrollY = juce::jmax (0, contentHeight - (getHeight() - TimelineState::rulerHeight));
        timeline.scrollY = juce::jlimit (0, maxScrollY, timeline.scrollY - juce::roundToInt (dy * pixelsPerUnit));
    }

    timeline.changed();
    if (onViewChanged)
        onViewChanged();
}

void ArrangeCanvas::zoomAround (float x, double factor)
{
    const auto timeAtX = timeline.xToTime (x);
    timeline.pixelsPerSecond = juce::jlimit (TimelineState::minPixelsPerSecond, TimelineState::maxPixelsPerSecond,
                                             timeline.pixelsPerSecond * factor);
    timeline.scrollSeconds = juce::jmax (0.0, timeAtX - (double) x / timeline.pixelsPerSecond);
}

//==============================================================================
// File drops
//==============================================================================
bool ArrangeCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (engine.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void ArrangeCanvas::fileDragMove (const juce::StringArray&, int, int y)
{
    const auto index = timeline.trackIndexAt (y);
    const auto highlight = (index < 0 || index >= model::numTracks (project)) ? model::numTracks (project) : index;

    if (highlight != dropHighlightTrack)
    {
        dropHighlightTrack = highlight;
        repaint();
    }
}

void ArrangeCanvas::fileDragExit (const juce::StringArray&)
{
    dropHighlightTrack = -2;
    repaint();
}

void ArrangeCanvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHighlightTrack = -2;
    importFiles (files, timeline.trackIndexAt (y), snap (timeline.xToTime ((float) x)));
}

void ArrangeCanvas::importFiles (const juce::StringArray& files, int trackIndex, double time)
{
    undoManager.beginNewTransaction ("Import audio");

    juce::ValueTree lastRegion;
    bool usedExistingTrack = false;

    for (const auto& path : files)
    {
        const juce::File file (path);
        const auto length = fileCache.getLengthSeconds (file);

        if (length <= 0.0)
            continue;

        juce::ValueTree track;

        // The first file goes on the track under the drop; the rest each get a new track.
        if (! usedExistingTrack && trackIndex >= 0 && trackIndex < model::numTracks (project)
            && ! model::isInstrumentTrack (project.getChild (trackIndex)))
        {
            track = project.getChild (trackIndex);
            usedExistingTrack = true;
        }
        else
        {
            track = model::addTrack (project, file.getFileNameWithoutExtension(), &undoManager);
        }

        lastRegion = model::addRegion (track, file, time, length, &undoManager);
    }

    if (lastRegion.isValid())
    {
        timeline.selectedRegionId = lastRegion[ids::id].toString();
        timeline.selectedTrackId = lastRegion.getParent()[ids::id].toString();
        timeline.changed();
    }

    repaint();
}

} // namespace riff
