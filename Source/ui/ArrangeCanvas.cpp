#include "ArrangeCanvas.h"
#include "../import/ProjectImport.h"
#include "Commands.h"
#include "ToolIcons.h"
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
double ArrangeCanvas::gridBeats() const
{
    // A beat when there's room, otherwise whole bars (doubling until they're 40 px apart). Sized at the start's tempo.
    const auto beat = model::secondsPerBeat (project);
    const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));

    if (beat * timeline.pixelsPerSecond >= 14.0)
        return 1.0;

    double bars = 1.0;
    while (beat * beatsPerBar * bars * timeline.pixelsPerSecond < 40.0)
        bars *= 2.0;
    return beatsPerBar * bars;
}

double ArrangeCanvas::gridSeconds() const
{
    return gridBeats() * model::secondsPerBeat (project);
}

double ArrangeCanvas::snap (double t) const
{
    // On the tempo track's beats: the grid is in beats, so it follows tempo changes.
    double grid = gridBeats();
    switch (timeline.snapMode)
    {
        case TimelineState::Snap::off:      return juce::jmax (0.0, t);
        case TimelineState::Snap::bar:
        {
            // To the nearest bar line (bars can change length with the time signature).
            const auto map = model::tempoMapOf (project);
            const auto beat = map.beatAt (juce::jmax (0.0, t));
            const auto bar = map.barAndBeat (beat).first;
            const auto a = map.beatOfBar (bar), b = map.beatOfBar (bar + 1);
            return map.secondsAt (beat - a < b - beat ? a : b);
        }
        case TimelineState::Snap::beat:     grid = 1.0; break;
        case TimelineState::Snap::division: grid = 0.25; break;
        case TimelineState::Snap::smart:    break;
    }
    const auto map = model::tempoMapOf (project);
    return juce::jmax (0.0, map.secondsAt (std::round (map.beatAt (juce::jmax (0.0, t)) / grid) * grid));
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
        if (timeline.heightOf (i) == 0 || top + timeline.heightOf (i) < TimelineState::rulerHeight || top > getHeight())
            continue;

        const auto track = project.getChild (i);
        const bool selected = timeline.isTrackSelected (track[ids::id].toString());

        g.setColour (selected ? theme::selection.withAlpha (0.6f) : (i % 2 == 0 ? theme::laneA : theme::laneB));
        g.fillRect (0, top, getWidth(), timeline.heightOf (i));
        if (theme::trackColouring() == 2)
        {
            // Filled tracks: the lane in a wash of the track's colour too
            g.setColour (model::trackColour (track).withAlpha (0.08f));
            g.fillRect (0, top, getWidth(), timeline.heightOf (i));
        }
    }

    paintGrid (g);

    // Tint the lanes inside an active cycle.
    if ((bool) project[ids::cycleEnabled])
    {
        const auto c = cycleBounds();
        g.setColour (theme::locators.withAlpha (0.06f));
        g.fillRect (c.getX(), (float) TimelineState::rulerHeight, c.getWidth(), (float) getHeight());
    }

    for (int i = 0; i < numTracks; ++i)
    {
        const auto track = project.getChild (i);
        for (const auto& region : track)
            paintRegion (g, region, i);
    }

    for (int i = 0; i < numTracks; ++i)
    {
        const auto track = project.getChild (i);
        if ((bool) track[ids::showAutomation])
            paintAutomation (g, track, i);
        else if (model::isBusTrack (track))
        {
            g.setColour (theme::dimText.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Aux bus: tracks reach it through sends in the mixer", 12, timeline.trackTop (i),
                        getWidth() - 24, timeline.heightOf (i), juce::Justification::centredLeft);
        }
    }

    paintLiveTakes (g);

    // Clip gain being dragged: its level beside the pointer
    if (gainReadout.isNotEmpty())
    {
        const auto box = juce::Rectangle<float> (gainReadoutAt.x + 14.0f, gainReadoutAt.y - 22.0f, 74.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (gainReadout, box, juce::Justification::centred, false);
    }

    // File-drop target highlight
    if (dropHighlightTrack != -2)
    {
        const bool newTrack = dropHighlightTrack < 0 || dropHighlightTrack >= numTracks;
        const auto top = newTrack ? timeline.trackTop (numTracks) : timeline.trackTop (dropHighlightTrack);

        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (0, top, getWidth(), timeline.heightOf (newTrack ? numTracks : dropHighlightTrack));
        g.setColour (theme::accent);
        g.drawRect (0, top, getWidth(), timeline.heightOf (newTrack ? numTracks : dropHighlightTrack), 2);

        if (newTrack)
        {
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("New track", 12, top, 200, timeline.heightOf (numTracks), juce::Justification::centredLeft);
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

    // The marquee selection: a bright stretch on its tracks.
    if (timeline.hasMarquee())
    {
        const auto x0 = timeline.timeToX (timeline.marqueeStart), x1 = timeline.timeToX (timeline.marqueeEnd);
        for (const auto& id : timeline.marqueeTracks)
        {
            const auto index = project.indexOf (project.getChildWithProperty (ids::id, id));
            if (index < 0)
                continue;
            const juce::Rectangle<float> r (x0, (float) timeline.trackTop (index), x1 - x0, (float) timeline.heightOf (index));
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (r.withWidth (1.0f));
            g.fillRect (juce::Rectangle<float> (x1 - 1.0f, r.getY(), 1.0f, r.getHeight()));
        }
    }

    if (dragMode == DragMode::zoomRect && ! rubberBand.isEmpty())
    {
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (rubberBand);
        g.setColour (theme::accent);
        g.drawRect (rubberBand, 1.0f);
    }

    if (dragMode == DragMode::rubberBand && ! rubberBand.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (rubberBand);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRect (rubberBand, 1.0f);
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
    const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
    const auto grid = gridBeats();
    const auto map = model::tempoMapOf (project);

    const auto b0 = map.beatAt (timeline.scrollSeconds);
    const auto b1 = map.beatAt (timeline.xToTime ((float) getWidth()));
    const auto bottom = (float) juce::jmin (getHeight(), timeline.trackTop (model::numTracks (project)));
    const auto top = (float) TimelineState::rulerHeight;

    // Shaded grids: every other bar (or beat) a shade darker
    if (const auto style = theme::gridStyle(); style == 1 || style == 4)
    {
        for (auto beat = (juce::int64) std::floor (b0); (double) beat <= b1; ++beat)
        {
            const auto [bar, inBar] = map.barAndBeat ((double) beat);
            const bool shade = style == 1 ? (bar % 2 == 1) : ((beat % 2) == 1);
            if (! shade)
                continue;
            const auto x0 = timeline.timeToX (map.secondsAt ((double) beat)), x1 = timeline.timeToX (map.secondsAt ((double) beat + 1.0));
            theme::shadeGridSpan (g, x0, x1, top, bottom - top);
        }
    }

    for (auto i = (juce::int64) std::floor (b0 / grid); (double) i * grid <= b1; ++i)
    {
        const auto b = (double) i * grid;
        const auto x = timeline.timeToX (map.secondsAt (b));
        const auto inBar = map.barAndBeat (b).second;
        const bool isBar = inBar < 1e-6 || std::abs (inBar - map.beatsPerBarAt (b)) < 1e-6;
        const bool isBeat = std::abs (std::remainder (b, 1.0)) < 1e-6;
        juce::ignoreUnused (beatsPerBar);
        theme::drawGridLine (g, x, top, bottom - top, isBar, isBeat, isBar ? theme::gridBar : theme::gridBeat);
    }
}

void ArrangeCanvas::paintRuler (juce::Graphics& g)
{
    const auto beat = model::secondsPerBeat (project);
    const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
    const auto barLength = beat * beatsPerBar;
    const auto rh = (float) TimelineState::barRulerHeight;
    const auto map = model::tempoMapOf (project);

    // Gold ruler
    g.setGradientFill (juce::ColourGradient (theme::ruler.brighter (0.1f), 0.0f, 0.0f, theme::rulerDark, 0.0f, rh, false));
    g.fillRect (0.0f, 0.0f, (float) getWidth(), rh);

    // Label every Nth bar so numbers don't collide.
    int labelEvery = 1;
    while (barLength * labelEvery * timeline.pixelsPerSecond < 36.0)
        labelEvery *= 2;

    const bool showBeats = beat * timeline.pixelsPerSecond >= 14.0;
    const auto t1 = timeline.xToTime ((float) getWidth());

    g.setFont (juce::FontOptions (11.0f));

    juce::ignoreUnused (beatsPerBar);
    for (auto bar = (juce::int64) map.barAndBeat (map.beatAt (timeline.scrollSeconds)).first; map.secondsAt (map.beatOfBar ((double) bar)) <= t1; ++bar)
    {
        const auto barBeat = map.beatOfBar ((double) bar);
        const auto x = timeline.timeToX (map.secondsAt (barBeat));

        if (bar % labelEvery == 0)
        {
            g.setColour (theme::ink.withAlpha (0.7f));
            g.fillRect (x, rh * 0.45f, 1.0f, rh * 0.55f);
            g.setColour (theme::ink);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::String (bar + 1), (int) x + 4, (int) (rh * 0.5f), 40, (int) (rh * 0.5f) - 2, juce::Justification::centredLeft);
        }

        if (showBeats)
        {
            g.setColour (theme::ink.withAlpha (0.35f));
            for (int b = 1; b < map.beatsPerBarAt (barBeat); ++b)
                g.fillRect (timeline.timeToX (map.secondsAt (barBeat + b)), rh * 0.75f, 1.0f, rh * 0.25f);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, rh - 1.0f, (float) getWidth(), 1.0f);

    paintCycle (g);
    paintMarkers (g);
    paintTempo (g);
}

void ArrangeCanvas::paintTempo (juce::Graphics& g)
{
    // The tempo track: the tempo as a stepped line, with each change's BPM written beside it.
    const auto top = (float) TimelineState::tempoTop;
    const auto lane = juce::Rectangle<float> (0.0f, top, (float) getWidth(), (float) TimelineState::tempoHeight);
    g.setColour (theme::toolbarDark.darker (0.2f));
    g.fillRect (lane);

    const auto changes = model::sortedTempoChanges (project);
    const auto map = model::tempoMapOf (project);
    double lo = 60.0 / map.segments.front().secondsPerBeat, hi = lo;
    for (const auto& s : map.segments)
    {
        lo = juce::jmin (lo, 60.0 / s.secondsPerBeat);
        hi = juce::jmax (hi, 60.0 / s.secondsPerBeat);
    }
    const auto yOf = [&] (double bpm) { return hi - lo < 0.5 ? lane.getCentreY() : lane.getBottom() - 4.0f - (float) ((bpm - lo) / (hi - lo)) * (lane.getHeight() - 8.0f); };

    juce::Path line;
    line.startNewSubPath (0.0f, yOf (map.bpmAt (timeline.scrollSeconds)));
    for (const auto& s : map.segments)
    {
        const auto x = timeline.timeToX (s.startSeconds);
        if (x <= 0.0f)
            continue;
        line.lineTo (x, line.getCurrentPosition().y);
        line.lineTo (x, yOf (60.0 / s.secondsPerBeat));
    }
    line.lineTo ((float) getWidth(), line.getCurrentPosition().y);
    g.setColour (juce::Colour (0xff4fd07a).withAlpha (0.8f));
    g.strokePath (line, juce::PathStrokeType (1.5f));

    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText (juce::String ((double) project.getProperty (ids::tempo, 120.0), 1) + " BPM",
                juce::Rectangle<float> (4.0f, top, 80.0f, lane.getHeight()), juce::Justification::centredLeft, false);
    for (const auto& c : changes)
    {
        const auto x = timeline.timeToX (map.secondsAt (c[ids::beat]));
        if (x < -40.0f || x > (float) getWidth())
            continue;
        const bool selected = timeline.selectedTempoId == c[ids::id].toString();
        g.setColour (selected ? theme::neon : juce::Colours::white);
        g.fillRect (x - 1.0f, top + 1.0f, 2.0f, lane.getHeight() - 2.0f);
        g.drawText (juce::String ((double) c[ids::bpm], 1), juce::Rectangle<float> (x + 4.0f, top, 60.0f, lane.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    // Time signature changes, in yellow.
    g.setColour (theme::solo);
    for (const auto& sig : model::sortedSignatureChanges (project))
    {
        const auto x = timeline.timeToX (map.secondsAt (map.beatOfBar ((int) sig[ids::bar] - 1)));
        if (x < -40.0f || x > (float) getWidth())
            continue;
        g.drawText (sig[ids::timeSigNumerator].toString() + "/4", juce::Rectangle<float> (x - 34.0f, top, 30.0f, lane.getHeight()),
                    juce::Justification::centredRight, false);
    }

    // Key changes, in cyan.
    g.setColour (juce::Colour (0xff7fd3ff));
    for (const auto& k : model::sortedKeyChanges (project))
    {
        const auto x = timeline.timeToX (map.secondsAt (map.beatOfBar ((int) k[ids::bar] - 1)));
        if (x < -80.0f || x > (float) getWidth())
            continue;
        g.drawText (model::keyName (k[ids::keyRoot], k[ids::keyMinor]), juce::Rectangle<float> (x + 50.0f, top, 80.0f, lane.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (0.0f, lane.getBottom() - 1.0f, (float) getWidth(), 1.0f);
}

juce::ValueTree ArrangeCanvas::tempoChangeAt (float x) const
{
    const auto map = model::tempoMapOf (project);
    for (const auto& c : model::sortedTempoChanges (project))
    {
        const auto cx = timeline.timeToX (map.secondsAt (c[ids::beat]));
        if (x >= cx - 4.0f && x <= cx + 40.0f)
            return c;
    }
    return {};
}

void ArrangeCanvas::editTempoChange (juce::ValueTree change, double beat)
{
    // Type a BPM: for an existing change, or a new one at `beat`.
    const auto current = change.isValid() ? (double) change[ids::bpm]
                                          : model::tempoMapOf (project).bpmAt (model::tempoMapOf (project).secondsAt (beat));
    auto* window = new juce::AlertWindow (change.isValid() ? "Tempo change" : "New tempo change",
                                          "Tempo (BPM) from here:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("bpm", juce::String (current, 1));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, change, beat] (int result) mutable
    {
        const auto bpm = window->getTextEditorContents ("bpm").getDoubleValue();
        if (result != 1 || bpm < 20.0 || bpm > 400.0)
            return;
        undoManager.beginNewTransaction ("Tempo change");
        const auto before = model::tempoMapOf (project);
        if (change.isValid())
            change.setProperty (ids::bpm, bpm, &undoManager);
        else
            timeline.selectedTempoId = model::addTempoChange (project, beat, bpm, &undoManager)[ids::id].toString();
        model::followTempo (project, before, &undoManager);   // MIDI and markers stay on their beats
        timeline.changed();
    }), true);
}

void ArrangeCanvas::tempoMouseDown (const juce::MouseEvent& e)
{
    auto change = tempoChangeAt ((float) e.x);
    timeline.selectedTempoId = change.isValid() ? change[ids::id].toString() : juce::String();
    timeline.changed();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        const auto map = model::tempoMapOf (project);
        const auto clickedBeat = map.beatAt (timeline.xToTime ((float) e.x));
        const auto [barIndex, inBar] = map.barAndBeat (clickedBeat);
        const auto nearestBar = inBar > map.beatsPerBarAt (clickedBeat) * 0.5 ? barIndex + 1 : barIndex;
        const auto beat = map.beatOfBar (nearestBar);
        menu.addItem ("New tempo change here...", [this, beat] { editTempoChange ({}, beat); });
        menu.addItem ("New time signature at bar " + juce::String (juce::jmax (2, nearestBar + 1)) + "...", [this, nearestBar]
        {
            auto* window = new juce::AlertWindow ("Time signature", "Beats per bar from bar " + juce::String (juce::jmax (2, nearestBar + 1)) + " (e.g. 3 for 3/4):",
                                                  juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("beats", "3");
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, nearestBar] (int result)
            {
                const auto beats = window->getTextEditorContents ("beats").getIntValue();
                if (result != 1 || beats < 1 || beats > 16)
                    return;
                undoManager.beginNewTransaction ("Time signature change");
                model::addSignatureChange (project, juce::jmax (2, nearestBar + 1), beats, &undoManager);
                timeline.changed();
            }), true);
        });
        {
            juce::PopupMenu keys;
            for (int minor = 0; minor < 2; ++minor)
                for (int root = 0; root < 12; ++root)
                    keys.addItem (model::keyName (root, minor == 1), [this, root, minor, nearestBar]
                    {
                        undoManager.beginNewTransaction ("Key change");
                        model::addKeyChange (project, juce::jmax (2, nearestBar + 1), root, minor == 1, &undoManager);
                        timeline.changed();
                    });
            menu.addSubMenu ("New key change at bar " + juce::String (juce::jmax (2, nearestBar + 1)), keys);
        }
        for (const auto& k : model::sortedKeyChanges (project))
            menu.addItem ("Delete key change to " + model::keyName (k[ids::keyRoot], k[ids::keyMinor]) + " at bar " + k[ids::bar].toString(), [this, k]() mutable
            {
                undoManager.beginNewTransaction ("Delete key change");
                k.getParent().removeChild (k, &undoManager);
            });
        for (const auto& sig : model::sortedSignatureChanges (project))
            menu.addItem ("Delete " + sig[ids::timeSigNumerator].toString() + "/4 at bar " + sig[ids::bar].toString(), [this, sig]() mutable
            {
                undoManager.beginNewTransaction ("Delete time signature change");
                sig.getParent().removeChild (sig, &undoManager);
            });
        if (change.isValid())
        {
            menu.addItem ("Change tempo...", [this, change] { editTempoChange (change, 0.0); });
            menu.addItem ("Delete tempo change", [this, change]() mutable
            {
                undoManager.beginNewTransaction ("Delete tempo change");
                const auto before = model::tempoMapOf (project);
                change.getParent().removeChild (change, &undoManager);
                model::followTempo (project, before, &undoManager);
            });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

    if (change.isValid())
    {
        // Drag sideways to move it (on beats), up and down to change its tempo.
        undoManager.beginNewTransaction ("Move tempo change");
        tempoBefore = model::tempoMapOf (project);
        dragTempo = change;
        dragOriginalStart = change[ids::beat];
        dragTempoBpm = change[ids::bpm];
        dragMode = DragMode::tempo;
    }
}

void ArrangeCanvas::paintMarkers (juce::Graphics& g)
{
    // Logic's marker track: each marker runs until the next one.
    const auto top = (float) TimelineState::barRulerHeight;
    const auto lane = juce::Rectangle<float> (0.0f, top, (float) getWidth(), (float) TimelineState::markerHeight);
    g.setColour (theme::toolbarDark);
    g.fillRect (lane);

    const auto markers = model::sortedMarkers (project);
    for (int i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers.getReference (i);
        const auto x0 = timeline.timeToX ((double) m[ids::time]);
        const auto x1 = i + 1 < markers.size() ? timeline.timeToX ((double) markers[i + 1][ids::time]) : x0 + 140.0f;
        if (x1 < 0.0f || x0 > (float) getWidth())
            continue;

        const bool selected = timeline.selectedMarkerId == m[ids::id].toString();
        const auto body = juce::Rectangle<float> (x0, top + 2.0f, juce::jmax (4.0f, x1 - x0 - 1.0f), lane.getHeight() - 4.0f);
        g.setColour (selected ? theme::neon : theme::markers);
        g.fillRoundedRectangle (body, 2.0f);
        const auto fill = selected ? theme::neon : theme::markers;
        g.setColour (fill.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (m[ids::name].toString(), body.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (0.0f, lane.getBottom() - 1.0f, (float) getWidth(), 1.0f);
}

juce::ValueTree ArrangeCanvas::markerAt (float x) const
{
    // The marker whose span (until the next one) holds x.
    juce::ValueTree found;
    for (const auto& m : model::sortedMarkers (project))
        if (timeline.timeToX ((double) m[ids::time]) <= x)
            found = m;
    if (found.isValid())
    {
        const auto next = model::adjacentMarker (project, (double) found[ids::time], true);
        const auto end = next.isValid() ? timeline.timeToX ((double) next[ids::time]) : timeline.timeToX ((double) found[ids::time]) + 140.0f;
        if (x > end)
            return {};
    }
    return found;
}

void ArrangeCanvas::markerMouseDown (const juce::MouseEvent& e)
{
    auto marker = markerAt ((float) e.x);
    timeline.selectedMarkerId = marker.isValid() ? marker[ids::id].toString() : juce::String();
    timeline.changed();

    if (! marker.isValid())
        return;

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem ("Go to", [this, marker] { engine.setPlayheadSeconds ((double) marker[ids::time]); repaint(); });
        menu.addItem ("Rename...", [this, marker] { renameMarker (marker); });
        // Arrangement: the marker's section (to the next marker) as a whole.
        menu.addItem ("Duplicate Section (everything until the next marker)", [this, marker]
        {
            const auto [a, b] = model::markerSection (project, marker);
            undoManager.beginNewTransaction ("Duplicate section");
            model::duplicateSection (project, a, b, &undoManager);
            timeline.changed();
        });
        menu.addItem ("Delete Section (and close the gap)", [this, marker]
        {
            const auto [a, b] = model::markerSection (project, marker);
            undoManager.beginNewTransaction ("Delete section");
            model::deleteSection (project, a, b, &undoManager);
            timeline.changed();
        });
        menu.addItem ("Set Cycle to Marker", [this, marker]
        {
            const auto next = model::adjacentMarker (project, (double) marker[ids::time], true);
            const auto end = next.isValid() ? (double) next[ids::time] : model::projectEnd (project);
            if (end > (double) marker[ids::time])
                model::setCycle (project, marker[ids::time], end, true);
        });
        menu.addSeparator();
        menu.addItem ("Delete", [this, marker]
        {
            undoManager.beginNewTransaction ("Delete marker");
            auto m = marker;
            m.getParent().removeChild (m, &undoManager);
            timeline.selectedMarkerId = {};
            timeline.changed();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

    // Click: go there. Drag: move it.
    engine.setPlayheadSeconds ((double) marker[ids::time]);
    undoManager.beginNewTransaction ("Move marker");
    dragMarker = marker;
    dragOriginalStart = marker[ids::time];
    dragMouseDownTime = timeline.xToTime ((float) e.x);
    dragMode = DragMode::marker;
}

void ArrangeCanvas::renameMarker (juce::ValueTree marker)
{
    auto* window = new juce::AlertWindow ("Rename marker", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", marker[ids::name].toString());
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, marker, window] (int result) mutable
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty())
        {
            undoManager.beginNewTransaction ("Rename marker");
            marker.setProperty (ids::name, name, &undoManager);
        }
    }), true);
}

juce::Rectangle<float> ArrangeCanvas::cycleBounds() const
{
    const auto x0 = timeline.timeToX (project.getProperty (ids::cycleStart, 0.0));
    const auto x1 = timeline.timeToX (project.getProperty (ids::cycleEnd, 0.0));
    return { x0, 2.0f, juce::jmax (2.0f, x1 - x0), (float) TimelineState::barRulerHeight * 0.5f - 3.0f };
}

void ArrangeCanvas::paintCycle (juce::Graphics& g)
{
    // The cycle lives in the top half of the ruler, as in Logic.
    const auto r = cycleBounds();
    const bool on = project[ids::cycleEnabled];

    g.setColour (on ? theme::locators : theme::locators.withMultipliedSaturation (0.4f).withAlpha (0.45f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (theme::ink.withAlpha (on ? 0.6f : 0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    // Autopunch: the punch range as a red band just under the cycle.
    if ((bool) project[ids::autopunch])
    {
        const auto p = punchBounds();
        g.setColour (juce::Colour (0xffd8352d));
        g.fillRoundedRectangle (p, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (p.getX(), p.getY() - 2.0f, 1.5f, p.getHeight() + 4.0f);
        g.fillRect (p.getRight() - 1.5f, p.getY() - 2.0f, 1.5f, p.getHeight() + 4.0f);
    }
}

void ArrangeCanvas::paintTakeLanes (juce::Graphics& g, const juce::ValueTree& region, juce::Rectangle<float> body)
{
    const auto takes = model::numTakes (region);
    const auto laneHeight = body.getHeight() / (float) takes;
    const auto colour = model::trackColour (region.getParent());
    auto pieces = model::compPieces (region);
    if (pieces.empty())
        pieces.push_back ({ 0.0, (double) region[ids::length], (int) region.getProperty (ids::activeTake, 0) });

    const double regionStart = region[ids::start];
    for (int i = 0; i < takes; ++i)
    {
        const auto take = region.getChild (i);
        const auto lane = juce::Rectangle<float> (body.getX(), body.getY() + laneHeight * (float) i, body.getWidth(), laneHeight);
        g.setColour (juce::Colours::black.withAlpha (i % 2 == 0 ? 0.35f : 0.25f));
        g.fillRect (lane);

        // The parts of this take the comp uses.
        for (const auto& p : pieces)
            if (p.take == i)
            {
                const auto x0 = timeline.timeToX (regionStart + p.from), x1 = timeline.timeToX (regionStart + p.to);
                g.setColour (colour.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (x0, lane.getY(), x1 - x0, lane.getHeight()));
            }

        // Its waveform.
        const juce::File file (take[ids::file].toString());
        auto& thumb = waveforms.get (file);
        const auto x0 = juce::jmax (lane.getX(), 0.0f), x1 = juce::jmin (lane.getRight(), (float) getWidth());
        if (thumb.getTotalLength() > 0.0 && x1 > x0)
        {
            const double offset = take[ids::offset];
            const auto tStart = offset + (x0 - lane.getX()) / timeline.pixelsPerSecond;
            const auto tEnd = offset + (x1 - lane.getX()) / timeline.pixelsPerSecond;
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            thumb.drawChannels (g, juce::Rectangle<float> (x0, lane.getY() + 1.0f, x1 - x0, lane.getHeight() - 2.0f).toNearestInt(),
                                tStart, tEnd, (float) (double) region.getProperty (ids::gain, 1.0) * timeline.waveformZoom);
        }

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (take[ids::name].toString(), lane.reduced (4.0f, 0.0f).withWidth (80.0f), juce::Justification::topLeft, false);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (lane.getX(), lane.getBottom() - 1.0f, lane.getWidth(), 1.0f);
    }
}

int ArrangeCanvas::takeLaneAt (const juce::ValueTree& region, int trackIndex, juce::Point<float> pos) const
{
    if (model::isMidiRegion (region) || ! (bool) region.getProperty (ids::takesOpen, false))
        return -1;
    const auto takes = model::numTakes (region);
    const auto body = regionBounds (region, trackIndex).withTrimmedTop ((float) regionHeader + 1.0f);
    if (takes == 0 || ! body.contains (pos))
        return -1;
    return juce::jlimit (0, takes - 1, (int) ((pos.y - body.getY()) / (body.getHeight() / (float) takes)));
}

void ArrangeCanvas::restoreComp (juce::ValueTree region, const std::vector<model::CompPiece>& segments, juce::UndoManager* um)
{
    for (int i = region.getNumChildren(); --i >= 0;)
        if (region.getChild (i).hasType (ids::COMPSEG))
            region.removeChild (i, um);
    for (const auto& s : segments)
        model::compSwipe (region, s.take, s.from, s.to, um);
}

juce::Rectangle<float> ArrangeCanvas::punchBounds() const
{
    const auto x0 = timeline.timeToX (project.getProperty (ids::punchIn, 0.0));
    const auto x1 = timeline.timeToX (project.getProperty (ids::punchOut, 0.0));
    return { x0, (float) TimelineState::barRulerHeight * 0.5f - 1.0f, juce::jmax (2.0f, x1 - x0), 5.0f };
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
    g.setColour (colour.brighter (0.8f));

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

void ArrangeCanvas::setWaveformFill (juce::Graphics& g, juce::Colour trackColour, juce::Rectangle<float> area, bool live)
{
    // Themes > Waveforms: the track's colour, one colour, or a gradient across the region.
    switch (theme::waveformMode())
    {
        case 1:  g.setColour (theme::waveformA().withAlpha (0.95f)); break;
        case 2:  g.setGradientFill (juce::ColourGradient (theme::waveformA(), area.getX(), 0.0f, theme::waveformB(), area.getRight(), 0.0f, false)); break;
        default: g.setColour (live ? theme::waveform.withAlpha (0.95f) : trackColour.brighter (0.75f).withAlpha (0.95f)); break;
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
        const juce::Rectangle<float> r (x, (float) timeline.trackTop (index) + 3.0f, w, (float) timeline.heightOf (index) - 6.0f);

        g.setColour (theme::danger.withAlpha (0.3f));
        g.fillRoundedRectangle (r, 4.0f);

        // The waveform as it comes in (as in Pro Tools), drawn from the engine's live peaks.
        if (! take.peaks.empty() && engine.getSampleRate() > 0.0)
        {
            const auto binSeconds = (double) AudioEngine::peakBlock / engine.getSampleRate();
            const auto body = r.withTrimmedTop (16.0f).reduced (0.0f, 2.0f);
            const auto clip = g.getClipBounds().toFloat();
            const auto x0 = juce::jmax (body.getX(), clip.getX()), x1 = juce::jmin (body.getRight(), clip.getRight());
            setWaveformFill (g, theme::danger, r, true);
            for (auto x = x0; x < x1; x += 1.0f)
            {
                const auto t0 = (double) (x - body.getX()) / timeline.pixelsPerSecond, t1 = t0 + 1.0 / timeline.pixelsPerSecond;
                const auto b0 = (int) (t0 / binSeconds), b1 = juce::jmax (b0 + 1, (int) std::ceil (t1 / binSeconds));
                int peak = 0;
                for (int b = b0; b < b1 && b < (int) take.peaks.size(); ++b)
                    peak = juce::jmax (peak, (int) take.peaks[(size_t) b]);
                if (b0 >= (int) take.peaks.size())
                    break;
                const auto h = juce::jmin (1.0f, (float) peak / 255.0f * timeline.waveformZoom) * body.getHeight() * 0.5f;
                g.fillRect (x, body.getCentreY() - h, 1.0f, juce::jmax (1.0f, h * 2.0f));
            }
        }

        g.setColour (theme::danger);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.5f);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (take.clipped ? juce::String ("Recording  -  CLIPPED") : juce::String ("Recording"),
                    r.withHeight (16.0f).reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
// Automation
//==============================================================================
juce::String ArrangeCanvas::automationParamOf (const juce::ValueTree& track)
{
    return track.getProperty (ids::automationParam, model::volumeParam).toString();
}

double ArrangeCanvas::staticValueOf (const juce::ValueTree& track, const juce::String& param) const
{
    if (param == model::volumeParam) return track.getProperty (ids::volume, 1.0);
    if (param == model::panParam)    return track.getProperty (ids::pan, 0.0);
    return model::rangeOf (project, param).defaultValue; // the insert's current knob value
}

float ArrangeCanvas::valueToY (double value, const model::ParamRange& range, int trackIndex) const
{
    double norm;
    if (range.decibels)
        norm = (juce::jlimit (-60.0, 6.0, value <= 0.0 ? -60.0 : juce::Decibels::gainToDecibels (value)) + 60.0) / 66.0;
    else
        norm = range.max > range.min ? (value - range.min) / (range.max - range.min) : 0.5;

    const auto top = (float) timeline.trackTop (trackIndex) + 6.0f;
    const auto height = (float) timeline.heightOf (trackIndex) - 12.0f;
    return top + (1.0f - (float) juce::jlimit (0.0, 1.0, norm)) * height;
}

double ArrangeCanvas::yToValue (float y, const model::ParamRange& range, int trackIndex) const
{
    const auto top = (float) timeline.trackTop (trackIndex) + 6.0f;
    const auto height = (float) timeline.heightOf (trackIndex) - 12.0f;
    const auto norm = juce::jlimit (0.0, 1.0, 1.0 - (double) ((y - top) / height));

    if (range.decibels)
    {
        const auto db = norm * 66.0 - 60.0;
        return db <= -59.9 ? 0.0 : juce::Decibels::decibelsToGain (db);
    }

    return range.min + norm * (range.max - range.min);
}

void ArrangeCanvas::paintAutomation (juce::Graphics& g, const juce::ValueTree& track, int trackIndex)
{
    const auto top = (float) timeline.trackTop (trackIndex);
    const auto lane = juce::Rectangle<float> (0.0f, top, (float) getWidth(), (float) timeline.heightOf (trackIndex));
    if (lane.getBottom() < (float) TimelineState::rulerHeight)
        return;

    // Dim the regions so the line stands out.
    g.setColour (theme::background.withAlpha (0.6f));
    g.fillRect (lane);

    const auto param = automationParamOf (track);
    const auto range = model::rangeOf (project, param);
    const auto points = model::laneFor (project, track[ids::id].toString(), param, false);
    const auto colour = theme::meterGreen;

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (colour.withAlpha (0.8f));
    g.drawText (range.name + (points.getNumChildren() == 0 ? "  (click to add points)" : "  (double-click a point to remove it)"),
                lane.reduced (8.0f, 2.0f).withHeight (14.0f).toNearestInt(), juce::Justification::topLeft);

    if (points.getNumChildren() == 0)
    {
        // No automation: a dashed line at the fader's value.
        const auto y = valueToY (staticValueOf (track, param), range, trackIndex);
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (colour.withAlpha (0.5f));
        g.drawDashedLine (juce::Line<float> (0.0f, y, (float) getWidth(), y), dashes, 2, 1.25f);
        return;
    }

    juce::Path line;
    const auto firstY = valueToY (points.getChild (0)[ids::value], range, trackIndex);
    line.startNewSubPath (0.0f, firstY);

    for (const auto& p : points)
        line.lineTo (timeline.timeToX (p[ids::time]), valueToY (p[ids::value], range, trackIndex));

    line.lineTo ((float) getWidth(), valueToY (points.getChild (points.getNumChildren() - 1)[ids::value], range, trackIndex));

    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.75f));

    for (const auto& p : points)
    {
        const auto x = timeline.timeToX (p[ids::time]);
        const auto y = valueToY (p[ids::value], range, trackIndex);
        g.setColour (p == dragPoint ? juce::Colours::white : colour);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

juce::ValueTree ArrangeCanvas::automationPointAt (const juce::ValueTree& track, int trackIndex, juce::Point<float> pos) const
{
    const auto param = automationParamOf (track);
    const auto range = model::rangeOf (project, param);

    for (const auto& p : model::laneFor (const_cast<juce::ValueTree&> (project), track[ids::id].toString(), param, false))
        if (juce::Point<float> (timeline.timeToX (p[ids::time]), valueToY (p[ids::value], range, trackIndex)).getDistanceFrom (pos) < 7.0f)
            return p;

    return {};
}

bool ArrangeCanvas::handleAutomationMouseDown (const juce::MouseEvent& e, int trackIndex)
{
    const auto track = project.getChild (trackIndex);
    if (! (bool) track[ids::showAutomation])
        return false;

    timeline.selectedTrackId = track[ids::id].toString();
    timeline.changed();

    auto point = automationPointAt (track, trackIndex, e.position);

    if (! point.isValid())
    {
        // A new point where you clicked.
        const auto param = automationParamOf (track);
        const auto range = model::rangeOf (project, param);
        auto lane = model::laneFor (project, track[ids::id].toString(), param, true);
        const auto time = e.mods.isCommandDown() ? timeline.xToTime ((float) e.x) : snap (timeline.xToTime ((float) e.x));

        undoManager.beginNewTransaction ("Add automation point");
        point = model::addPoint (lane, time, yToValue ((float) e.y, range, trackIndex), &undoManager);
    }
    else
    {
        undoManager.beginNewTransaction ("Move automation point");
    }

    dragPoint = point;
    dragPointTrack = trackIndex;
    dragMode = DragMode::automationPoint;
    repaint();
    return true;
}

juce::Rectangle<float> ArrangeCanvas::regionBounds (const juce::ValueTree& region, int trackIndex) const
{
    const auto x = timeline.timeToX (region[ids::start]);
    const auto w = (float) ((double) region[ids::length] * timeline.pixelsPerSecond);
    return { x, (float) timeline.trackTop (trackIndex) + 3.0f, juce::jmax (2.0f, w), (float) timeline.heightOf (trackIndex) - 6.0f };
}

void ArrangeCanvas::paintRegion (juce::Graphics& g, const juce::ValueTree& region, int trackIndex)
{
    const auto bounds = regionBounds (region, trackIndex);
    // Only what's being repainted (the playhead's strip, say) is drawn.
    const auto visible = bounds.getIntersection (getLocalBounds().toFloat().withTrimmedTop ((float) TimelineState::rulerHeight))
                               .getIntersection (g.getClipBounds().toFloat().expanded (2.0f));

    if (visible.isEmpty())
        return;

    const bool muted = model::isRegionMuted (region);
    const auto colour = muted ? juce::Colour (0xff6b6b70) : model::trackColour (region.getParent());
    const bool selected = timeline.isRegionSelected (region[ids::id].toString());
    const juce::File file (region[ids::file].toString());
    const bool isMidi = model::isMidiRegion (region);
    const bool missing = ! isMidi && ! fileExists (file);

    // Solid body in a deep shade of the track colour, a brighter name bar on top
    // (or, with glass regions, a dark see-through body with a lit outline).
    const auto base = missing ? theme::danger : colour;
    const bool glass = theme::glassRegions() && ! missing;
    auto header = bounds.withHeight ((float) regionHeader);
    if (glass)
    {
        g.setColour (juce::Colour (0xff0c0f1c).interpolatedWith (base, 0.14f).withAlpha (0.82f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (base.withAlpha (0.28f));
        g.fillRoundedRectangle (header, 4.0f);
        g.fillRect (header.withTrimmedTop (4.0f));
        g.setColour (theme::text);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (base.darker (0.05f), 0.0f, bounds.getY(), base.darker (0.45f), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (base.withMultipliedSaturation (0.9f).brighter (0.1f));
        g.fillRoundedRectangle (header, 3.0f);
        g.fillRect (header.withTrimmedTop (3.0f));
        g.setColour (juce::Colour (0xff15161a));
    }
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    const auto textArea = header.withLeft (juce::jmax (header.getX(), 0.0f)).reduced (5.0f, 0.0f);
    auto label = region[ids::name].toString();

    if (const auto takes = model::numTakes (region); takes > 0)
        label << "  (" << ((int) region.getProperty (ids::activeTake, 0) + 1) << " of " << takes << ")  "
              << juce::String::fromUTF8 ("\xe2\x96\xbe"); // down-pointing triangle

    if (muted)
        label = juce::String::fromUTF8 ("\xe2\x80\xa2 ") + label + "  (muted)";
    if (missing)
        label << "  (file missing)";
    else if (! isMidi && fileCache.isPending (file))
        label << "  (loading...)";

    g.drawText (label, textArea, juce::Justification::centredLeft, true);

    if (isMidi)
        paintMidiNotes (g, region, bounds.withTrimmedTop ((float) regionHeader + 1.0f).reduced (0.0f, 3.0f), colour);

    // An open take folder: its takes as lanes, the comp lit.
    if (! isMidi && (bool) region.getProperty (ids::takesOpen, false) && model::numTakes (region) > 0)
    {
        paintTakeLanes (g, region, bounds.withTrimmedTop ((float) regionHeader + 1.0f));
        g.setColour (selected ? juce::Colours::white : base.darker (0.8f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
        return;
    }

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

            setWaveformFill (g, colour, bounds);
            thumb.drawChannels (g, juce::Rectangle<float> (x0, body.getY(), x1 - x0, body.getHeight()).toNearestInt(),
                                tStart, tEnd, (float) (double) region.getProperty (ids::gain, 1.0) * timeline.waveformZoom);
        }
    }

    // Fades: the faded part darkened above its curve, with a handle at the top corner.
    if (! isMidi)
    {
        const auto body = bounds.withTrimmedTop ((float) regionHeader);
        const auto drawFade = [&] (bool fadeIn)
        {
            const double seconds = region.getProperty (fadeIn ? ids::fadeIn : ids::fadeOut, 0.0);
            if (seconds <= 0.0)
                return;
            const double curve = region.getProperty (fadeIn ? ids::fadeInCurve : ids::fadeOutCurve, 0.0);
            const auto w = juce::jmin (body.getWidth(), (float) (seconds * timeline.pixelsPerSecond));
            const auto x0 = fadeIn ? body.getX() : body.getRight() - w;

            juce::Path shade, line;
            shade.startNewSubPath (x0, body.getY());
            const int steps = juce::jlimit (2, 64, (int) w / 3);
            for (int i = 0; i <= steps; ++i)
            {
                const auto f = (float) i / (float) steps;
                const auto gain = (float) model::fadeGain (fadeIn ? f : 1.0f - f, curve);
                const juce::Point<float> p { x0 + f * w, body.getBottom() - gain * body.getHeight() };
                shade.lineTo (p);
                if (i == 0) line.startNewSubPath (p); else line.lineTo (p);
            }
            shade.lineTo (x0 + w, body.getY());
            shade.closeSubPath();
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (shade);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.strokePath (line, juce::PathStrokeType (1.2f));
        };
        drawFade (true);
        drawFade (false);

        if (selected)
        {
            g.setColour (juce::Colours::white);
            const auto inX = body.getX() + (float) ((double) region.getProperty (ids::fadeIn, 0.0) * timeline.pixelsPerSecond);
            const auto outX = body.getRight() - (float) ((double) region.getProperty (ids::fadeOut, 0.0) * timeline.pixelsPerSecond);
            g.fillEllipse (inX - 3.0f, body.getY() - 3.0f, 6.0f, 6.0f);
            g.fillEllipse (outX - 3.0f, body.getY() - 3.0f, 6.0f, 6.0f);
        }

        const double gain = region.getProperty (ids::gain, 1.0);
        if (std::abs (gain - 1.0) > 1.0e-4 && bounds.getWidth() > 60.0f)
        {
            g.setColour (juce::Colour (0xff15161a));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            const auto db = juce::Decibels::gainToDecibels (gain, -96.0);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db, 1) + " dB", header.reduced (5.0f, 0.0f), juce::Justification::centredRight, false);
        }
    }

    // The clip gain handle (as in FL Studio): on the region under the pointer, or selected
    if (! isMidi && ! missing && (selected || region[ids::id].toString() == hoverRegionId || gainReadout.isNotEmpty()))
        if (const auto handle = gainHandleOf (region, trackIndex); ! handle.isEmpty())
        {
            const auto body = bounds.withTrimmedTop ((float) regionHeader);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (handle.getCentreY()), body.getX() + 2.0f, body.getRight() - 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (handle.expanded (1.0f), 2.5f);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (handle, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawHorizontalLine (juce::roundToInt (handle.getCentreY()), handle.getX() + 4.0f, handle.getRight() - 4.0f);
        }

    if (glass)
    {
        g.setColour (selected ? juce::Colours::white : base.brighter (0.4f).withAlpha (0.85f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, selected ? 2.0f : 1.2f);
    }
    else
    {
        g.setColour (selected ? juce::Colours::white : base.darker (0.8f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
    }

    // A looped region's repeats: lighter copies of its outline, up to the loop end.
    if ((bool) region.getProperty (ids::loop, false))
    {
        const double length = region[ids::length];
        const auto loopEnd = model::loopEndOf (region);
        for (double t = model::regionEnd (region); t < loopEnd - 1.0e-6 && length > 0.0; t += length)
        {
            const auto x = timeline.timeToX (t);
            const auto w = (float) (juce::jmin (length, loopEnd - t) * timeline.pixelsPerSecond);
            if (x > (float) getWidth())
                break;
            const juce::Rectangle<float> ghost { x, bounds.getY(), w, bounds.getHeight() };
            g.setColour (colour.withAlpha (0.28f));
            g.fillRoundedRectangle (ghost.reduced (0.5f, 0.0f), 3.0f);
            g.setColour (colour.withAlpha (0.7f));
            g.drawRoundedRectangle (ghost.reduced (1.0f), 3.0f, 1.0f);
        }
    }
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

        if (e.y >= TimelineState::tempoTop)
        {
            tempoMouseDown (e);
            return;
        }
        if (e.y >= TimelineState::barRulerHeight)
        {
            markerMouseDown (e);
            return;
        }

        // Autopunch: drag the punch band's ends, or the band itself, or draw a new one.
        if ((bool) project[ids::autopunch] && std::abs ((float) e.y - punchBounds().getCentreY()) <= 5.0f)
        {
            const auto p = punchBounds();
            dragMouseDownTime = timeline.xToTime ((float) e.x);
            dragCycleStart = project.getProperty (ids::punchIn, 0.0);
            dragCycleEnd = project.getProperty (ids::punchOut, 0.0);
            punchDrag = std::abs ((float) e.x - p.getX()) <= 5.0f ? 1 : std::abs ((float) e.x - p.getRight()) <= 5.0f ? 2
                      : (e.position.x > p.getX() && e.position.x < p.getRight()) ? 3 : 0;
            dragMode = DragMode::punch;
            return;
        }

        if (e.y < TimelineState::barRulerHeight / 2)
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

    if (const auto laneIndex = timeline.trackIndexAt (e.y);
        laneIndex >= 0 && laneIndex < model::numTracks (project) && handleAutomationMouseDown (e, laneIndex))
        return;

    // Marquee and Zoom tools work anywhere in the lanes.
    if ((timeline.tool == TimelineState::Tool::marquee || timeline.tool == TimelineState::Tool::zoom) && ! e.mods.isPopupMenu())
    {
        dragMode = timeline.tool == TimelineState::Tool::marquee ? DragMode::marquee : DragMode::zoomRect;
        dragMouseDownTime = timeline.xToTime ((float) e.x);
        if (dragMode == DragMode::marquee)
        {
            timeline.clearMarquee();
            timeline.changed();
        }
        return;
    }
    if (timeline.hasMarquee())
    {
        timeline.clearMarquee();   // any other click drops the marquee
        timeline.changed();
    }

    int trackIndex = -1;
    auto region = regionAt (e.position, trackIndex);
    const bool onTrack = trackIndex >= 0 && trackIndex < model::numTracks (project);

    // The Select tool: a box round regions, wherever you start (a click on a region selects it; Shift adds)
    if (timeline.tool == TimelineState::Tool::select && ! e.mods.isPopupMenu())
    {
        selectionBeforeRubberBand.clear();
        if (e.mods.isShiftDown())
            for (const auto& r : timeline.selectedRegionTrees())
                selectionBeforeRubberBand.add (r[ids::id].toString());
        if (region.isValid())
        {
            const auto id = region[ids::id].toString();
            if (e.mods.isShiftDown() && selectionBeforeRubberBand.contains (id))
                selectionBeforeRubberBand.removeString (id);
            else
                selectionBeforeRubberBand.addIfNotAlreadyThere (id);
        }
        timeline.selectRegions (selectionBeforeRubberBand);
        rubberBand = {};
        dragMode = DragMode::rubberBand;
        timeline.changed();
        return;
    }

    if (! region.isValid())
    {
        if (onTrack)
            timeline.selectOnly (project.getChild (trackIndex)[ids::id].toString());

        // Right-click on empty space: the tool menu (as in Logic).
        if (e.mods.isPopupMenu())
        {
            timeline.changed();
            if (onEmptyAreaMenu)
                onEmptyAreaMenu();
            return;
        }

        // The pencil draws a new MIDI region on an instrument track (drag for its length).
        if (timeline.tool == TimelineState::Tool::pencil && onTrack && model::isInstrumentTrack (project.getChild (trackIndex))
            && ! e.mods.isPopupMenu())
        {
            const auto start = snap (timeline.xToTime ((float) e.x));
            undoManager.beginNewTransaction ("New MIDI region");
            dragRegion = model::addMidiRegion (project.getChild (trackIndex), start, gridSeconds(), &undoManager);
            dragOriginalStart = start;
            timeline.selectOnlyRegion (dragRegion[ids::id].toString());
            dragMode = DragMode::pencilRegion;
            timeline.changed();
            return;
        }

        // Drag over empty space to select the regions in a rectangle (Shift adds to the selection).
        selectionBeforeRubberBand.clear();
        if (e.mods.isShiftDown())
            for (const auto& r : timeline.selectedRegionTrees())
                selectionBeforeRubberBand.add (r[ids::id].toString());
        else
            timeline.selectOnlyRegion ({});
        rubberBand = {};
        dragMode = DragMode::rubberBand;
        timeline.changed();
        return;
    }

    const auto id = region[ids::id].toString();
    if (e.mods.isShiftDown() && ! e.mods.isPopupMenu())
    {
        timeline.toggleRegionSelection (id);
        timeline.selectOnly (region.getParent()[ids::id].toString());
        timeline.changed();
        return;
    }

    // A click on a region already in the selection keeps the selection (to drag it all).
    if (timeline.isRegionSelected (id) && timeline.selectedRegionTrees().size() > 1)
        timeline.selectedRegionId = id;
    else
        timeline.selectOnlyRegion (id);
    timeline.selectOnly (region.getParent()[ids::id].toString());
    timeline.changed();

    if (e.mods.isPopupMenu())
    {
        showRegionMenu (region);
        return;
    }

    if (timeline.tool != TimelineState::Tool::pointer && timeline.tool != TimelineState::Tool::pencil)
    {
        useTool (e, region, trackIndex);
        return;
    }

    // A click (not a drag) on a take folder's name bar opens its take menu.
    if (model::numTakes (region) > 0 && e.position.y < regionBounds (region, trackIndex).getY() + 16.0f)
        clickedTakeHeader = true;

    // Quick Swipe comping in an open take folder (away from its edges).
    if (const auto lane = takeLaneAt (region, trackIndex, e.position); lane >= 0
        && edgeActionAt (region, trackIndex, e.position) == DragMode::move)
    {
        dragRegion = region;
        swipeLane = lane;
        dragMouseDownTime = timeline.xToTime ((float) e.x);
        compBefore.clear();
        for (const auto& s : region)
            if (s.hasType (ids::COMPSEG))
                compBefore.push_back ({ (double) s[ids::from], (double) s[ids::to], (int) s[ids::take] });
        dragMode = DragMode::compSwipe;
        return;
    }

    dragMode = edgeActionAt (region, trackIndex, e.position);

    if (dragMode == DragMode::gain)
    {
        if (e.mods.isAltDown())   // Option-click: back to 0 dB
        {
            resetGain (region);
            dragMode = DragMode::none;
            return;
        }
        undoManager.beginNewTransaction ("Clip gain");
        startGainDrag (region);
        dragRegion = region;
        return;
    }

    undoManager.beginNewTransaction (dragMode == DragMode::move ? (e.mods.isAltDown() ? "Copy region" : "Move region")
                                     : dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut ? "Fade"
                                     : dragMode == DragMode::loopEnd ? "Loop region" : "Trim region");

    if (dragMode == DragMode::move)
        startMove (e, region);

    dragRegion = region;
    dragOriginalStart = region[ids::start];
    dragMouseDownTime = timeline.xToTime ((float) e.x);
    dragSourceLength = fileCache.getLengthSeconds (juce::File (region[ids::file].toString()));
}

ArrangeCanvas::DragMode ArrangeCanvas::edgeActionAt (const juce::ValueTree& region, int trackIndex, juce::Point<float> pos) const
{
    const auto bounds = regionBounds (region, trackIndex);
    const bool wideEnough = bounds.getWidth() > edgeGrab * 3.0f;
    const bool audio = ! model::isMidiRegion (region);
    const auto body = bounds.withTrimmedTop ((float) regionHeader);

    // Clip gain: the handle on an audio region (as in FL Studio)
    if (audio)
        if (const auto handle = gainHandleOf (region, trackIndex); ! handle.isEmpty() && handle.expanded (3.0f).contains (pos))
            return DragMode::gain;

    // Fade handles: the top corners of the body, where the fade currently ends (audio).
    if (audio && std::abs (pos.y - body.getY()) <= 5.0f)
    {
        const auto inX = body.getX() + (float) ((double) region.getProperty (ids::fadeIn, 0.0) * timeline.pixelsPerSecond);
        const auto outX = body.getRight() - (float) ((double) region.getProperty (ids::fadeOut, 0.0) * timeline.pixelsPerSecond);
        if (std::abs (pos.x - inX) <= 6.0f && pos.x < bounds.getCentreX())
            return DragMode::fadeIn;
        if (std::abs (pos.x - outX) <= 6.0f && pos.x >= bounds.getCentreX())
            return DragMode::fadeOut;
    }

    if (wideEnough && pos.x - bounds.getX() < edgeGrab)
        return DragMode::trimStart;

    if (wideEnough && bounds.getRight() - pos.x < edgeGrab)
        return pos.y < body.getCentreY() && pos.y > body.getY() + 5.0f ? DragMode::loopEnd   // upper half: loop, as in Logic
                                                                        : DragMode::trimEnd;
    return DragMode::move;
}

void ArrangeCanvas::applyDragMode (const juce::Array<juce::ValueTree>& regions, juce::Array<juce::ValueTree> tracks)
{
    using Drag = TimelineState::Drag;
    switch (timeline.dragMode)
    {
        case Drag::noOverlap:
            for (const auto& r : regions)
                if (r.getParent().isValid())
                    model::clearOverlaps (r, [this] (const juce::ValueTree& other)
                                          { return fileCache.getLengthSeconds (juce::File (other[ids::file].toString())); }, &undoManager);
            break;

        case Drag::xfade:
            for (const auto& r : regions)
                if (r.getParent().isValid() && ! model::isMidiRegion (r))
                    model::createCrossfades (r, &undoManager);
            break;

        case Drag::shuffleLeft:
        case Drag::shuffleRight:
            for (const auto& r : regions)
                if (r.getParent().isValid())
                    tracks.addIfNotAlreadyThere (r.getParent());
            for (auto& t : tracks)
                if (t.getParent().isValid())
                    model::shuffleTrack (t, timeline.dragMode == Drag::shuffleLeft, &undoManager);
            break;

        case Drag::overlap:
            break;
    }
}

void ArrangeCanvas::showGainPopup (juce::ValueTree region)
{
    auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    slider->setRange (-30.0, 30.0, 0.1);
    slider->setTextValueSuffix (" dB");
    slider->setValue (juce::Decibels::gainToDecibels ((double) region.getProperty (ids::gain, 1.0), -30.0), juce::dontSendNotification);
    slider->setDoubleClickReturnValue (true, 0.0);
    slider->setSize (260, 32);
    auto regions = timeline.selectedRegionTrees();
    if (! regions.contains (region))
        regions = { region };
    undoManager.beginNewTransaction ("Region gain");
    slider->onValueChange = [this, regions, s = slider.get()]
    {
        for (auto r : regions)
            r.setProperty (ids::gain, juce::Decibels::decibelsToGain (s->getValue(), -96.0), &undoManager);
    };
    const auto where = regionBounds (region, project.indexOf (region.getParent())).toNearestInt();
    juce::CallOutBox::launchAsynchronously (std::move (slider), localAreaToGlobal (where), nullptr);
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

        case DragMode::gain:
        {
            // Up: louder. Shift for fine steps.
            const auto db = -(double) e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.05 : 0.25);
            for (auto& [r, before] : gainBefore)
            {
                const auto from = before > 0.0 ? before : juce::Decibels::decibelsToGain (-60.0);
                r.setProperty (ids::gain, juce::jlimit (0.0, (double) juce::Decibels::decibelsToGain (24.0f), from * std::pow (10.0, db / 20.0)), &undoManager);
            }
            if (! gainBefore.empty())
            {
                const auto now = juce::Decibels::gainToDecibels ((double) gainBefore.front().first.getProperty (ids::gain, 1.0), -96.0);
                gainReadout = (now > 0.05 ? "+" : "") + juce::String (now, 1) + " dB";
            }
            gainReadoutAt = e.position;
            repaint();
            break;
        }

        case DragMode::move:
            dragMove (e, useSnap);
            break;

        case DragMode::rubberBand:
            updateRubberBand (e);
            break;

        case DragMode::marquee:
        {
            const auto a = maybeSnap (dragMouseDownTime), b = maybeSnap (timeline.xToTime ((float) e.x));
            timeline.marqueeStart = juce::jmin (a, b);
            timeline.marqueeEnd = juce::jmax (a, b);
            timeline.marqueeTracks.clear();
            const auto i0 = timeline.trackIndexAt (juce::jmin (e.mouseDownPosition.toInt().y, e.y));
            const auto i1 = timeline.trackIndexAt (juce::jmax (e.mouseDownPosition.toInt().y, e.y));
            for (int i = juce::jmax (0, i0); i <= juce::jmin (model::numTracks (project) - 1, i1); ++i)
                if (timeline.heightOf (i) > 0)
                    timeline.marqueeTracks.add (project.getChild (i)[ids::id].toString());
            repaint();
            break;
        }

        case DragMode::zoomRect:
            rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            repaint();
            break;

        case DragMode::solo:
            break;

        case DragMode::marker:
            if (dragMarker.isValid() && e.getDistanceFromDragStart() >= 4)
            {
                dragMarker.setProperty (ids::time, maybeSnap (dragOriginalStart + timeline.xToTime ((float) e.x) - dragMouseDownTime), &undoManager);
                engine.setPlayheadSeconds ((double) dragMarker[ids::time]);
            }
            break;

        case DragMode::pencilRegion:
        {
            if (! dragRegion.isValid())
                break;
            const auto end = juce::jmax (dragOriginalStart + gridSeconds(), maybeSnap (timeline.xToTime ((float) e.x)));
            dragRegion.setProperty (ids::length, end - dragOriginalStart, &undoManager);
            break;
        }

        case DragMode::trimStart:
            model::trimRegionStart (dragRegion, maybeSnap (timeline.xToTime ((float) e.x)), dragSourceLength, &undoManager);
            break;

        case DragMode::trimEnd:
            model::trimRegionEnd (dragRegion, maybeSnap (timeline.xToTime ((float) e.x)), dragSourceLength, &undoManager);
            break;

        case DragMode::fadeIn:
        case DragMode::fadeOut:
        {
            if (! dragRegion.isValid())
                break;
            const auto t = timeline.xToTime ((float) e.x);
            const bool in = dragMode == DragMode::fadeIn;
            // Shift-drag bends the curve instead.
            if (e.mods.isShiftDown())
            {
                const auto curve = juce::jlimit (-1.0, 1.0, (double) (e.getDistanceFromDragStartY()) / -60.0);
                dragRegion.setProperty (in ? ids::fadeInCurve : ids::fadeOutCurve, curve, &undoManager);
                break;
            }
            model::setFade (dragRegion, in, in ? t - (double) dragRegion[ids::start] : model::regionEnd (dragRegion) - t, &undoManager);
            break;
        }

        case DragMode::loopEnd:
        {
            if (! dragRegion.isValid() || e.getDistanceFromDragStart() < 3)
                break;
            const auto end = maybeSnap (timeline.xToTime ((float) e.x));
            const bool loops = end > model::regionEnd (dragRegion) + 1.0e-3;
            dragRegion.setProperty (ids::loop, loops, &undoManager);
            if (loops)
                dragRegion.setProperty (ids::loopEnd, end, &undoManager);
            break;
        }

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

        case DragMode::compSwipe:
        {
            if (! dragRegion.isValid() || e.getDistanceFromDragStart() < 3)
                break;
            const double regionStart = dragRegion[ids::start];
            restoreComp (dragRegion, compBefore, nullptr);
            model::compSwipe (dragRegion, swipeLane, maybeSnap (dragMouseDownTime) - regionStart,
                              maybeSnap (timeline.xToTime ((float) e.x)) - regionStart, nullptr);
            break;
        }

        case DragMode::tempo:
        {
            if (! dragTempo.isValid() || e.getDistanceFromDragStart() < 3)
                break;
            const auto map = model::tempoMapOf (project);
            // Vertical: tempo (1 BPM per 2 pixels); sideways: position, on beats (Cmd: sixteenths).
            if (std::abs (e.getDistanceFromDragStartY()) > std::abs (e.getDistanceFromDragStartX()))
                dragTempo.setProperty (ids::bpm, juce::jlimit (20.0, 400.0, std::round (dragTempoBpm - e.getDistanceFromDragStartY() * 0.5)), &undoManager);
            else
            {
                const auto grid = e.mods.isCommandDown() ? 0.25 : 1.0;
                const auto beat = std::round (map.beatAt (timeline.xToTime ((float) e.x)) / grid) * grid;
                dragTempo.setProperty (ids::beat, juce::jmax (grid, beat), &undoManager);
            }
            break;
        }

        case DragMode::punch:
        {
            const auto t = maybeSnap (timeline.xToTime ((float) e.x));
            auto in = dragCycleStart, out = dragCycleEnd;
            if (punchDrag == 1)      in = juce::jmin (t, out - model::minRegionLength);
            else if (punchDrag == 2) out = juce::jmax (t, in + model::minRegionLength);
            else if (punchDrag == 3) { const auto shift = maybeSnap (dragCycleStart + timeline.xToTime ((float) e.x) - dragMouseDownTime) - dragCycleStart;
                                       in += shift; out += shift; }
            else if (e.getDistanceFromDragStart() >= 4)
            {
                const auto a = maybeSnap (dragMouseDownTime);
                in = juce::jmin (a, t); out = juce::jmax (a, t);
            }
            if (out - in >= model::minRegionLength && in >= 0.0)
            {
                project.setProperty (ids::punchIn, in, nullptr);
                project.setProperty (ids::punchOut, out, nullptr);
            }
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

        case DragMode::automationPoint:
        {
            if (! dragPoint.isValid() || ! dragPoint.getParent().isValid())
                break;

            const auto track = project.getChild (dragPointTrack);
            const auto range = model::rangeOf (project, automationParamOf (track));
            const auto time = useSnap ? snap (timeline.xToTime ((float) e.x)) : juce::jmax (0.0, timeline.xToTime ((float) e.x));
            model::movePoint (dragPoint, time, yToValue ((float) e.y, range, dragPointTrack), &undoManager);
            break;
        }

        case DragMode::none:
            break;
    }
}

void ArrangeCanvas::mouseUp (const juce::MouseEvent& e)
{
    const bool wasClick = e.getDistanceFromDragStart() < 4;

    if (dragMode == DragMode::gain)
    {
        gainReadout = {};
        gainBefore.clear();
        repaint();
    }

    if (dragMode == DragMode::zoomRect)
    {
        if (wasClick)
            zoomAround ((float) e.x, e.mods.isAltDown() ? 0.5 : 2.0);
        else
        {
            // Zoom so the dragged stretch fills the view.
            const auto t0 = timeline.xToTime (juce::jmin (e.mouseDownPosition.x, e.position.x));
            const auto t1 = timeline.xToTime (juce::jmax (e.mouseDownPosition.x, e.position.x));
            if (t1 - t0 > 0.01)
            {
                timeline.pixelsPerSecond = juce::jlimit (TimelineState::minPixelsPerSecond, 4000.0, (double) getWidth() / (t1 - t0));
                timeline.scrollSeconds = juce::jmax (0.0, t0);
                if (onViewChanged) onViewChanged();
            }
        }
        rubberBand = {};
    }

    if (dragMode == DragMode::tempo && dragTempo.isValid() && ! wasClick)
        model::followTempo (project, tempoBefore, &undoManager);

    if (dragMode == DragMode::compSwipe && dragRegion.isValid())
    {
        // The swipe as one undo step (the preview while dragging wasn't recorded).
        std::vector<model::CompPiece> after;
        for (const auto& s : dragRegion)
            if (s.hasType (ids::COMPSEG))
                after.push_back ({ (double) s[ids::from], (double) s[ids::to], (int) s[ids::take] });
        restoreComp (dragRegion, compBefore, nullptr);

        undoManager.beginNewTransaction ("Comp");
        if (wasClick)
        {
            // A click on a take: all of it (no comp).
            restoreComp (dragRegion, {}, &undoManager);
            model::setActiveTake (dragRegion, swipeLane, &undoManager);
        }
        else
        {
            restoreComp (dragRegion, after, &undoManager);
        }
    }

    if (dragMode == DragMode::solo && soloTrack.isValid())
    {
        engine.stop();
        soloTrack.setProperty (ids::solo, soloWasOn, nullptr);
        soloTrack = {};
    }

    // Clicking the cycle bar turns the cycle on and off.
    if (dragMode == DragMode::cycleMove && wasClick)
        project.setProperty (ids::cycleEnabled, ! (bool) project[ids::cycleEnabled], nullptr);

    if (clickedTakeHeader && wasClick && dragRegion.isValid())
        showTakeMenu (dragRegion);

    // A plain click on one of several selected regions selects just it.
    if (dragMode == DragMode::move && wasClick && dragRegion.isValid() && ! e.mods.isShiftDown())
        timeline.selectOnlyRegion (dragRegion[ids::id].toString());

    // The Drag mode: No Overlap, X-Fade or Shuffle (moved regions also leave a gap on the tracks they came from).
    if (dragMode == DragMode::move && ! wasClick)
    {
        juce::Array<juce::ValueTree> moved, from;
        for (const auto& d : dragged)
        {
            moved.add (d.region);
            from.addIfNotAlreadyThere (project.getChild (d.trackIndex));
        }
        applyDragMode (moved, from);
    }
    else if ((dragMode == DragMode::trimStart || dragMode == DragMode::trimEnd) && ! wasClick && dragRegion.isValid())
    {
        applyDragMode ({ dragRegion }, {});
    }

    if (dragMode == DragMode::pencilRegion && dragRegion.isValid() && onOpenPianoRoll)
        onOpenPianoRoll (dragRegion);

    dragged.clear();
    dragMarker = {};
    rubberBand = {};
    timeline.changed();

    clickedTakeHeader = false;
    dragMode = DragMode::none;
    dragRegion = {};
    dragPoint = {};
    repaint();
}

void ArrangeCanvas::startMove (const juce::MouseEvent& e, juce::ValueTree region)
{
    dragged.clear();
    auto selection = timeline.selectedRegionTrees();
    if (! selection.contains (region))
        selection = { region };

    for (const auto& r : selection)
        dragged.push_back ({ r, (double) r[ids::start], project.indexOf (r.getParent()) });

    // Option-drag copies (the originals stay where they are).
    copyOnDrag = e.mods.isAltDown();
    copiesMade = false;
}

void ArrangeCanvas::dragMove (const juce::MouseEvent& e, bool useSnap)
{
    if (dragged.empty() || ! dragRegion.isValid())
        return;

    // Ignore tiny wobbles so a click never nudges a region onto the grid.
    if (e.getDistanceFromDragStart() < 4)
        return;

    if (copyOnDrag && ! copiesMade)
    {
        juce::StringArray newSelection;
        for (auto& d : dragged)
        {
            const bool isMain = d.region == dragRegion;
            d.region = model::duplicateRegion (d.region, d.region.getParent(), d.start, &undoManager);
            if (isMain)
                dragRegion = d.region;
            newSelection.add (d.region[ids::id].toString());
        }
        timeline.selectRegions (newSelection);
        timeline.selectedRegionId = dragRegion[ids::id].toString();
        copiesMade = true;
    }

    // The one under the mouse sets the move; the rest keep their distance from it.
    const auto& main = *std::find_if (dragged.begin(), dragged.end(), [this] (const Dragged& d) { return d.region == dragRegion; });
    const auto rawStart = main.start + timeline.xToTime ((float) e.x) - dragMouseDownTime;
    const auto newMainStart = useSnap ? snap (rawStart) : juce::jmax (0.0, rawStart);
    auto delta = newMainStart - main.start;
    for (const auto& d : dragged)
        delta = juce::jmax (delta, -d.start);   // nothing goes before the start

    const auto numTracks = model::numTracks (project);
    const auto trackDelta = juce::jlimit (0, numTracks - 1, timeline.trackIndexAt (e.y)) - main.trackIndex;

    for (const auto& d : dragged)
    {
        auto target = project.getChild (juce::jlimit (0, numTracks - 1, d.trackIndex + trackDelta));

        // MIDI belongs on instrument tracks and audio on audio tracks.
        if (model::isMidiRegion (d.region) != model::isInstrumentTrack (target) || model::isBusTrack (target) || timeline.isHidden (project.indexOf (target)))
            target = project.getChild (d.trackIndex);

        model::moveRegion (d.region, target, d.start + delta, &undoManager);
    }

    timeline.selectedTrackId = dragRegion.getParent()[ids::id].toString();
}

void ArrangeCanvas::updateRubberBand (const juce::MouseEvent& e)
{
    rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position).withTop (juce::jmax ((float) TimelineState::rulerHeight,
                                                                                                 juce::jmin (e.mouseDownPosition.y, e.position.y)));
    auto chosen = selectionBeforeRubberBand;
    for (int i = 0; i < model::numTracks (project); ++i)
    {
        if (timeline.heightOf (i) == 0)
            continue;
        for (const auto& r : project.getChild (i))
            if (r.hasType (ids::REGION) && regionBounds (r, i).intersects (rubberBand))
                chosen.addIfNotAlreadyThere (r[ids::id].toString());
    }
    timeline.selectRegions (chosen);
    timeline.changed();
}

void ArrangeCanvas::useTool (const juce::MouseEvent& e, juce::ValueTree region, int trackIndex)
{
    auto selection = timeline.selectedRegionTrees();
    if (! selection.contains (region))
        selection = { region };

    switch (timeline.tool)
    {
        case TimelineState::Tool::select:
            break;   // (handled in mouseDown: a box round regions)
        case TimelineState::Tool::eraser:
            undoManager.beginNewTransaction ("Delete region");
            for (auto& r : selection)
                model::deleteRegion (r, &undoManager);
            timeline.selectOnlyRegion ({});
            break;

        case TimelineState::Tool::scissors:
        {
            // Split where you click (on the grid; Cmd for anywhere).
            const auto time = e.mods.isCommandDown() ? timeline.xToTime ((float) e.x) : snap (timeline.xToTime ((float) e.x));
            undoManager.beginNewTransaction ("Split region");
            for (auto& r : selection)
                model::splitRegion (r, time, &undoManager);
            break;
        }

        case TimelineState::Tool::glue:
            if (onJoin)
                onJoin();
            break;

        case TimelineState::Tool::mute:
            if (onMuteRegions)
                onMuteRegions();
            break;

        case TimelineState::Tool::solo:
        {
            // Hear this track alone from here while the mouse is held.
            soloTrack = region.getParent();
            soloWasOn = soloTrack[ids::solo];
            soloTrack.setProperty (ids::solo, true, nullptr);
            engine.setPlayheadSeconds (juce::jmax (0.0, timeline.xToTime ((float) e.x)));
            engine.play();
            dragMode = DragMode::solo;
            break;
        }

        case TimelineState::Tool::text:
        {
            auto* window = new juce::AlertWindow ("Rename region", {}, juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", region[ids::name].toString());
            window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, selection] (int result)
            {
                const auto name = window->getTextEditorContents ("name").trim();
                if (result != 1 || name.isEmpty())
                    return;
                undoManager.beginNewTransaction ("Rename region");
                for (auto r : selection)
                    r.setProperty (ids::name, name, &undoManager);
            }), true);
            break;
        }

        case TimelineState::Tool::marquee:
        case TimelineState::Tool::zoom:
            break;

        case TimelineState::Tool::fade:
        {
            if (model::isMidiRegion (region))
                break;
            const auto b = regionBounds (region, trackIndex);
            const bool in = e.position.x < b.getCentreX();
            if (e.mods.isAltDown())   // Option-click removes the fade
            {
                undoManager.beginNewTransaction ("Remove fade");
                for (auto& r : selection)
                    r.setProperty (in ? ids::fadeIn : ids::fadeOut, 0.0, &undoManager);
                break;
            }
            undoManager.beginNewTransaction ("Fade");
            dragRegion = region;
            dragMode = in ? DragMode::fadeIn : DragMode::fadeOut;
            break;
        }

        case TimelineState::Tool::gain:
            if (model::isMidiRegion (region))
                break;
            if (e.mods.isAltDown())
            {
                resetGain (region);
                break;
            }
            undoManager.beginNewTransaction ("Gain");
            startGainDrag (region);
            dragRegion = region;
            dragMode = DragMode::gain;
            break;

        case TimelineState::Tool::pointer:
        case TimelineState::Tool::pencil:
            break;
    }
    timeline.changed();
}

juce::Rectangle<float> ArrangeCanvas::gainHandleOf (const juce::ValueTree& region, int trackIndex) const
{
    if (model::isMidiRegion (region))
        return {};
    const auto bounds = regionBounds (region, trackIndex);
    const auto body = bounds.withTrimmedTop ((float) regionHeader + 2.0f).withTrimmedBottom (4.0f);
    if (bounds.getWidth() < 40.0f || body.getHeight() < 14.0f)
        return {};
    // Its height shows the level: +12 dB at the top, -30 dB at the bottom.
    const auto db = juce::Decibels::gainToDecibels ((float) (double) region.getProperty (ids::gain, 1.0), -30.0f);
    const auto y = body.getY() + (1.0f - juce::jmap (juce::jlimit (-30.0f, 12.0f, db), -30.0f, 12.0f, 0.0f, 1.0f)) * body.getHeight();
    return juce::Rectangle<float> (18.0f, 9.0f).withCentre ({ bounds.getCentreX(), y });
}

void ArrangeCanvas::startGainDrag (juce::ValueTree region)
{
    auto selection = timeline.selectedRegionTrees();
    if (! selection.contains (region))
        selection = { region };
    gainBefore.clear();
    for (const auto& r : selection)
        if (! model::isMidiRegion (r))
            gainBefore.emplace_back (r, (double) r.getProperty (ids::gain, 1.0));
}

void ArrangeCanvas::resetGain (juce::ValueTree region)
{
    auto selection = timeline.selectedRegionTrees();
    if (! selection.contains (region))
        selection = { region };
    undoManager.beginNewTransaction ("Clip gain 0 dB");
    for (auto& r : selection)
        if (! model::isMidiRegion (r))
            r.setProperty (ids::gain, 1.0, &undoManager);
    repaint();
}

void ArrangeCanvas::showRegionMenu (juce::ValueTree region)
{
    // Logic's region shortcut menu: the edits, with their keys, then the region's own settings.
    juce::PopupMenu menu;
    const bool midi = model::isMidiRegion (region);
    menu.addSectionHeader (region[ids::name].toString().isNotEmpty() ? region[ids::name].toString() : juce::String (midi ? "MIDI region" : "Audio region"));

    if (commandManager != nullptr)
    {
        for (const auto id : { commands::cutRegions, commands::copyRegions, commands::pasteRegions, commands::deleteSelection })
            menu.addCommandItem (commandManager, id);
        menu.addSeparator();
        juce::PopupMenu split;
        for (const auto id : { commands::splitAtPlayhead, commands::splitByLocators })
            split.addCommandItem (commandManager, id);
        menu.addSubMenu ("Split", split);
        juce::PopupMenu trim;
        for (const auto id : { commands::trimStartToPlayhead, commands::trimEndToPlayhead })
            trim.addCommandItem (commandManager, id);
        menu.addSubMenu ("Trim", trim);
        for (const auto id : { commands::joinRegions, commands::repeatRegions, commands::muteRegions, commands::loopRegions })
            menu.addCommandItem (commandManager, id);
        menu.addSeparator();
        menu.addCommandItem (commandManager, commands::bounceInPlace);
        if (midi)
        {
            menu.addCommandItem (commandManager, commands::quantize);
            menu.addCommandItem (commandManager, commands::showPianoRoll);
            menu.addCommandItem (commandManager, commands::eventList);
        }
        else
        {
            if (onDetectTempo)
                menu.addItem ("Detect Tempo (BPM)...", [this, region] { onDetectTempo (region); });
            menu.addCommandItem (commandManager, commands::regionGain);
            menu.addCommandItem (commandManager, commands::removeFades);
            menu.addCommandItem (commandManager, commands::audioSuite);
            menu.addCommandItem (commandManager, commands::consolidateRegions);
            menu.addCommandItem (commandManager, commands::alignAudio);
        }
        menu.addSeparator();
        menu.addItem ("Rename...", [this, region]
        {
            auto* window = new juce::AlertWindow ("Rename region", {}, juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", region[ids::name].toString());
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, region] (int result) mutable
            {
                if (result != 1)
                    return;
                undoManager.beginNewTransaction ("Rename region");
                region.setProperty (ids::name, window->getTextEditorContents ("name"), &undoManager);
            }), true);
        });
        if (! midi)
        {
            menu.addItem ("Audio File Editor (normalize, reverse, time stretch, transpose)...", [this, region] { if (onOpenAudioEditor) onOpenAudioEditor (region); });
            juce::PopupMenu fades;
            const auto setFades = [this, region] (double in, double out)
            {
                undoManager.beginNewTransaction ("Fade");
                auto regions = timeline.selectedRegionTrees();
                if (! regions.contains (region))
                    regions = { region };
                for (auto r : regions)
                {
                    if (in >= 0.0)  model::setFade (r, true, in, &undoManager);
                    if (out >= 0.0) model::setFade (r, false, out, &undoManager);
                }
            };
            for (auto ms : { 10, 50, 250, 1000 })
                fades.addItem ("Fade in " + juce::String (ms) + " ms", [setFades, ms] { setFades (ms / 1000.0, -1.0); });
            fades.addSeparator();
            for (auto ms : { 10, 50, 250, 1000 })
                fades.addItem ("Fade out " + juce::String (ms) + " ms", [setFades, ms] { setFades (-1.0, ms / 1000.0); });
            menu.addSubMenu ("Fades", fades);

            // Extensions (Cubase): an ARA plugin on just these regions
            if (addExtensionItems)
            {
                auto regions = timeline.selectedRegionTrees();
                if (! regions.contains (region))
                    regions = { region };
                juce::PopupMenu extensions;
                addExtensionItems (extensions, regions);
                if (extensions.getNumItems() > 0)
                    menu.addSubMenu ("Extensions", extensions);
            }
        }
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

    menu.addItem ("Split at playhead", [this] { if (onSplit) onSplit(); });
    menu.addItem ("Repeat", [this] { if (onRepeat) onRepeat(); });
    if (model::isMidiRegion (region))
        menu.addItem ("Join", timeline.selectedRegionTrees().size() > 1, false, [this] { if (onJoin) onJoin(); });
    menu.addItem (model::isRegionMuted (region) ? "Unmute" : "Mute", [this] { if (onMuteRegions) onMuteRegions(); });
    menu.addItem ("Loop", true, (bool) region.getProperty (ids::loop, false), [this, region]
    {
        undoManager.beginNewTransaction ("Loop region");
        const bool on = ! (bool) region.getProperty (ids::loop, false);
        auto regions = timeline.selectedRegionTrees();
        if (! regions.contains (region))
            regions = { region };
        for (auto r : regions)
            model::setLoop (r, on, &undoManager);
    });

    if (! model::isMidiRegion (region))
    {
        menu.addItem ("Gain...", [this, region] { showGainPopup (region); });
        menu.addItem ("Audio File Editor (normalize, reverse, time stretch, transpose)...", [this, region] { if (onOpenAudioEditor) onOpenAudioEditor (region); });

        juce::PopupMenu fades;
        const auto setFades = [this, region] (double in, double out)
        {
            undoManager.beginNewTransaction ("Fade");
            auto regions = timeline.selectedRegionTrees();
            if (! regions.contains (region))
                regions = { region };
            for (auto r : regions)
            {
                if (in >= 0.0)  model::setFade (r, true, in, &undoManager);
                if (out >= 0.0) model::setFade (r, false, out, &undoManager);
            }
        };
        for (auto ms : { 10, 50, 250, 1000 })
            fades.addItem ("Fade in " + juce::String (ms) + " ms", [setFades, ms] { setFades (ms / 1000.0, -1.0); });
        fades.addSeparator();
        for (auto ms : { 10, 50, 250, 1000 })
            fades.addItem ("Fade out " + juce::String (ms) + " ms", [setFades, ms] { setFades (-1.0, ms / 1000.0); });
        fades.addSeparator();
        fades.addItem ("Remove fades", [setFades] { setFades (0.0, 0.0); });
        menu.addSubMenu ("Fades", fades);
    }

    menu.addSeparator();
    menu.addItem ("Delete", [this] { if (onDelete) onDelete(); });

    if (model::numTakes (region) > 0)
    {
        juce::PopupMenu takesMenu;
        addTakeItems (takesMenu, region);
        menu.addSeparator();
        menu.addSubMenu ("Takes", takesMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
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
    const bool open = region.getProperty (ids::takesOpen, false);
    menu.addItem (open ? "Close Take Folder" : "Open Take Folder (swipe across takes to comp)", [this, region, open]() mutable
    {
        region.setProperty (ids::takesOpen, ! open, nullptr);
        // Room for the lanes.
        auto track = region.getParent();
        if (! open)
            track.setProperty (ids::height, juce::jmax ((int) track.getProperty (ids::height, TimelineState::trackHeight),
                                                        juce::jmin (TimelineState::maxTrackHeight, 30 + 28 * model::numTakes (region))), nullptr);
        timeline.changed();
    });
    const bool comped = ! model::compPieces (region).empty();
    menu.addItem ("Flatten Comp (into regions)", comped, false, [this, region]
    {
        undoManager.beginNewTransaction ("Flatten comp");
        model::flattenComp (region, &undoManager);
        timeline.selectOnlyRegion ({});
        timeline.changed();
    });
    menu.addItem ("Clear Comp", comped, false, [this, region]
    {
        undoManager.beginNewTransaction ("Clear comp");
        restoreComp (region, {}, &undoManager);
    });
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
    // On a clip gain handle: back to 0 dB.
    {
        int index = -1;
        if (const auto r = regionAt (e.position, index); r.isValid() && edgeActionAt (r, index, e.position) == DragMode::gain)
        {
            resetGain (r);
            return;
        }
    }

    // In the tempo track: a new tempo change on the nearest bar, or change one.
    if (e.y >= TimelineState::tempoTop && e.y < TimelineState::rulerHeight)
    {
        if (auto change = tempoChangeAt ((float) e.x); change.isValid())
            editTempoChange (change, 0.0);
        else
        {
            const auto map = model::tempoMapOf (project);
            const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
            editTempoChange ({}, juce::jmax ((double) beatsPerBar, std::round (map.beatAt (timeline.xToTime ((float) e.x)) / beatsPerBar) * beatsPerBar));
        }
        return;
    }

    // In the marker track: rename a marker, or make one where you double-click.
    if (e.y >= TimelineState::barRulerHeight && e.y < TimelineState::tempoTop)
    {
        if (auto marker = markerAt ((float) e.x); marker.isValid() && (float) e.x - timeline.timeToX ((double) marker[ids::time]) < 90.0f)
        {
            renameMarker (marker);
            return;
        }
        undoManager.beginNewTransaction ("New marker");
        auto marker = model::addMarker (project, snap (timeline.xToTime ((float) e.x)), {}, &undoManager);
        timeline.selectedMarkerId = marker[ids::id].toString();
        timeline.changed();
        return;
    }

    if (e.y < TimelineState::rulerHeight)
        return;

    // Double-clicking an automation point removes it.
    if (const auto laneIndex = timeline.trackIndexAt (e.y); laneIndex >= 0 && laneIndex < model::numTracks (project))
    {
        const auto track = project.getChild (laneIndex);
        if ((bool) track[ids::showAutomation])
        {
            if (auto point = automationPointAt (track, laneIndex, e.position); point.isValid())
            {
                undoManager.beginNewTransaction ("Remove automation point");
                model::removePoint (point, &undoManager);
            }
            return;
        }
    }

    int trackIndex = -1;
    auto region = regionAt (e.position, trackIndex);

    if (region.isValid())
    {
        if (model::isMidiRegion (region) && onOpenPianoRoll)
            onOpenPianoRoll (region);
        else if (! model::isMidiRegion (region) && onOpenAudioEditor && ! (bool) region.getProperty (ids::takesOpen, false))
            onOpenAudioEditor (region);
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
    // The tool's own pointer (as in Logic); region edges with the Pointer or Pencil show what dragging there does.
    auto newCursor = toolicons::cursor (timeline.tool);
    juce::ignoreUnused (trackIndex);

    // The region under the pointer shows its clip gain handle.
    if (const auto hovered = region.isValid() ? region[ids::id].toString() : juce::String(); hovered != hoverRegionId)
    {
        hoverRegionId = hovered;
        repaint();
    }

    if (region.isValid() && (timeline.tool == TimelineState::Tool::pointer || timeline.tool == TimelineState::Tool::pencil))
    {
        switch (edgeActionAt (region, trackIndex, e.position))
        {
            case DragMode::gain: newCursor = juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case DragMode::trimStart:
            case DragMode::trimEnd:  newCursor = juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case DragMode::fadeIn:
            case DragMode::fadeOut:  newCursor = toolicons::cursor (TimelineState::Tool::fade); break;
            case DragMode::loopEnd:  newCursor = juce::MouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor); break;
            default: break;
        }
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

        const auto contentHeight = timeline.contentHeight();
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
        if (engine.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr
            || (onOpenProject && projectimport::canImport (juce::File (f))))
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

    // Another program's project (an .als, .flp, .ptx...): opened as a project.
    for (const auto& f : files)
        if (onOpenProject && projectimport::canImport (juce::File (f)))
        {
            repaint();
            onOpenProject (juce::File (f));
            return;
        }
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
            && ! model::isInstrumentTrack (project.getChild (trackIndex))
            && ! model::isBusTrack (project.getChild (trackIndex)))
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
