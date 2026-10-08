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

    const int numTracks = project.getNumChildren();

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

    for (int i = 0; i < numTracks; ++i)
    {
        const auto track = project.getChild (i);
        for (const auto& region : track)
            paintRegion (g, region, i);
    }

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
    const auto bottom = (float) juce::jmin (getHeight(), timeline.trackTop (project.getNumChildren()));

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
            g.fillRect (x, rh * 0.45f, 1.0f, rh * 0.55f);
            g.setColour (theme::text);
            g.drawText (juce::String (bar + 1), (int) x + 4, 2, 40, 14, juce::Justification::centredLeft);
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
    const bool missing = ! file.existsAsFile();

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
    g.drawText (missing ? region[ids::name].toString() + "  (file missing)" : region[ids::name].toString(),
                textArea, juce::Justification::centredLeft, true);

    // Waveform, drawn only for the visible part
    if (! missing)
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

    if (index < 0 || index >= project.getNumChildren())
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

    if (e.y < TimelineState::rulerHeight)
    {
        dragMode = DragMode::scrub;
        setPlayheadFromX ((float) e.x);
        return;
    }

    int trackIndex = -1;
    auto region = regionAt (e.position, trackIndex);

    if (! region.isValid())
    {
        timeline.selectedRegionId = {};
        if (trackIndex >= 0 && trackIndex < project.getNumChildren())
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
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

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

            const auto newStart = maybeSnap (dragOriginalStart + timeline.xToTime ((float) e.x) - dragMouseDownTime);
            const auto targetIndex = juce::jlimit (0, project.getNumChildren() - 1, timeline.trackIndexAt (e.y));
            auto target = project.getChild (targetIndex);

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

        case DragMode::none:
            break;
    }
}

void ArrangeCanvas::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    dragRegion = {};
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

        const auto contentHeight = (project.getNumChildren() + 1) * TimelineState::trackHeight;
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
    const auto highlight = (index < 0 || index >= project.getNumChildren()) ? project.getNumChildren() : index;

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
        if (! usedExistingTrack && trackIndex >= 0 && trackIndex < project.getNumChildren())
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
