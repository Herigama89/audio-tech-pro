#include "LiveLoopsGrid.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../model/Project.h"

namespace riff
{

LiveLoopsGrid::LiveLoopsGrid (juce::UndoManager& um, AudioEngine& e, ProjectPlayer& p)
    : undoManager (um), engine (e), player (p)
{
    stopAllButton.onClick = [this]
    {
        const auto at = engine.isPlaying() ? nextBar() : 0.0;
        for (const auto& t : tracks())
            if (player.getLiveCells().count (t[ids::id].toString()) > 0)
                engine.isPlaying() ? player.stopLiveCell (t[ids::id].toString(), at) : player.clearLiveCells();
        repaint();
    };
    captureButton.setTooltip ("Copy what's on the tracks inside the cycle into a scene's cells");
    captureButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Copy the cycle's regions into scene");
        for (int s = 0; s < numScenes; ++s)
            menu.addItem ("Scene " + juce::String (s + 1), [this, s]
            {
                const double a = project.getProperty (ids::cycleStart, 0.0), b = project.getProperty (ids::cycleEnd, 0.0);
                if (b <= a)
                    return;
                undoManager.beginNewTransaction ("Cycle to scene");
                model::captureScene (project, s, a, b, &undoManager);
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&captureButton));
    };
    look::key (closeButton, look::Key::dark);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &stopAllButton, &captureButton, &closeButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    startTimerHz (15);
}

LiveLoopsGrid::~LiveLoopsGrid()
{
    project.removeListener (this);
}

void LiveLoopsGrid::setProject (juce::ValueTree p)
{
    project.removeListener (this);
    project = p;
    project.addListener (this);
    repaint();
}

juce::Array<juce::ValueTree> LiveLoopsGrid::tracks() const
{
    juce::Array<juce::ValueTree> list;
    for (const auto& t : project)
        if (t.hasType (ids::TRACK) && ! model::isBusTrack (t) && ! model::isVcaTrack (t))
            list.add (t);
    return list;
}

double LiveLoopsGrid::nextBar() const
{
    return model::moveByBars (project, engine.getPlayheadSeconds(), 1);
}

juce::Rectangle<int> LiveLoopsGrid::cellBounds (int row, int scene) const
{
    return { nameWidth + scene * cellWidth + 2, headerHeight + sceneRowHeight + (row - scrollRows) * rowHeight + 2, cellWidth - 4, rowHeight - 4 };
}

void LiveLoopsGrid::triggerCell (const juce::ValueTree& track, int scene)
{
    const auto id = track[ids::id].toString();
    const auto cell = model::liveCell (project, id, scene);
    if (! cell.isValid())
        return;

    const auto& live = player.getLiveCells();
    const auto it = live.find (id);
    const bool thisOnePlaying = it != live.end() && it->second.cellId == cell[ids::id].toString()
                                && (it->second.until <= 0.0 || it->second.until > engine.getPlayheadSeconds());

    if (thisOnePlaying)
    {
        // Stop on the next bar.
        player.stopLiveCell (id, engine.isPlaying() ? nextBar() : engine.getPlayheadSeconds());
        repaint();
        return;
    }

    // Start on the next bar, or right away (from this bar) when stopped.
    double from = 0.0;
    if (engine.isPlaying())
        from = nextBar();
    else
    {
        from = model::moveByBars (project, engine.getPlayheadSeconds() + 1.0e-6, 0);
        engine.setPlayheadSeconds (from);
    }
    player.setLiveCell (id, { cell[ids::id].toString(), from, 0.0 });
    if (! engine.isPlaying())
        engine.play();
    repaint();
}

void LiveLoopsGrid::triggerScene (int scene)
{
    // Every track with a cell in the scene plays it; the others stop.
    for (const auto& t : tracks())
    {
        const auto id = t[ids::id].toString();
        if (model::liveCell (project, id, scene).isValid())
        {
            const auto& live = player.getLiveCells();
            const auto it = live.find (id);
            const auto cell = model::liveCell (project, id, scene);
            if (it == live.end() || it->second.cellId != cell[ids::id].toString() || it->second.until > 0.0)
                triggerCell (t, scene);
        }
        else if (player.getLiveCells().count (id) > 0)
            stopTrack (t);
    }
}

void LiveLoopsGrid::stopTrack (const juce::ValueTree& track)
{
    player.stopLiveCell (track[ids::id].toString(), engine.isPlaying() ? nextBar() : engine.getPlayheadSeconds());
    repaint();
}

void LiveLoopsGrid::showCellMenu (const juce::ValueTree& track, int scene)
{
    juce::PopupMenu menu;
    const auto id = track[ids::id].toString();
    const auto region = selectedRegion ? selectedRegion() : juce::ValueTree();
    menu.addItem ("Put the selected region here", region.isValid() && region.getParent() == track, false, [this, id, scene, region]
    {
        undoManager.beginNewTransaction ("Live Loops cell");
        model::setLiveCell (project, id, scene, region, &undoManager);
    });
    menu.addItem ("Clear cell", model::liveCell (project, id, scene).isValid(), false, [this, id, scene]
    {
        undoManager.beginNewTransaction ("Clear Live Loops cell");
        model::setLiveCell (project, id, scene, {}, &undoManager);
    });
    menu.addItem ("Copy cell to the tracks area at the playhead", model::liveCell (project, id, scene).isValid(), false, [this, track, id, scene]() mutable
    {
        const auto cell = model::liveCell (project, id, scene);
        undoManager.beginNewTransaction ("Cell to tracks area");
        model::duplicateRegion (cell.getChild (0), track, engine.getPlayheadSeconds(), &undoManager);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void LiveLoopsGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto list = tracks();

    // Scene triggers.
    if (e.y >= headerHeight && e.y < headerHeight + sceneRowHeight && e.x >= nameWidth)
    {
        const auto scene = (e.x - nameWidth) / cellWidth;
        if (juce::isPositiveAndBelow (scene, numScenes))
            triggerScene (scene);
        return;
    }

    const auto row = (e.y - headerHeight - sceneRowHeight) / rowHeight + scrollRows;
    if (e.y < headerHeight + sceneRowHeight || ! juce::isPositiveAndBelow (row, list.size()))
        return;
    const auto track = list[row];

    // The track's stop key, by its name.
    if (e.x < nameWidth)
    {
        if (e.x > nameWidth - 30)
            stopTrack (track);
        return;
    }

    const auto scene = (e.x - nameWidth) / cellWidth;
    if (! juce::isPositiveAndBelow (scene, numScenes))
        return;

    if (e.mods.isPopupMenu())
    {
        showCellMenu (track, scene);
        return;
    }

    const auto id = track[ids::id].toString();
    if (! model::liveCell (project, id, scene).isValid())
    {
        // An empty cell takes the selected region (from this track).
        if (const auto region = selectedRegion ? selectedRegion() : juce::ValueTree(); region.isValid() && region.getParent() == track)
        {
            undoManager.beginNewTransaction ("Live Loops cell");
            model::setLiveCell (project, id, scene, region, &undoManager);
        }
        return;
    }
    triggerCell (track, scene);
}

void LiveLoopsGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollRows = juce::jlimit (0, juce::jmax (0, tracks().size() - 2), scrollRows - (wheel.deltaY > 0 ? 1 : wheel.deltaY < 0 ? -1 : 0));
    repaint();
}

void LiveLoopsGrid::timerCallback()
{
    // When the song stops, the cells stop too.
    const auto playing = engine.isPlaying();
    wasPlaying = playing;
    if (! player.getLiveCells().empty())
        repaint();
}

void LiveLoopsGrid::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1f2023));
    g.setColour (theme::toolbarDark);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Live Loops", 10, 0, 200, headerHeight, juce::Justification::centredLeft);

    // Scene triggers.
    for (int s = 0; s < numScenes; ++s)
    {
        const juce::Rectangle<int> r (nameWidth + s * cellWidth + 2, headerHeight + 3, cellWidth - 4, sceneRowHeight - 6);
        g.setColour (theme::slotDark);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6 ") + juce::String (s + 1), r, juce::Justification::centred);
    }

    const auto now = engine.getPlayheadSeconds();
    const auto list = tracks();
    for (int row = scrollRows; row < list.size(); ++row)
    {
        const auto& track = list[row];
        const auto y = headerHeight + sceneRowHeight + (row - scrollRows) * rowHeight;
        if (y > getHeight())
            break;
        const auto colour = model::trackColour (track);
        g.setColour (colour);
        g.fillRect (0, y + 2, 4, rowHeight - 4);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (track[ids::name].toString(), 10, y, nameWidth - 44, rowHeight, juce::Justification::centredLeft, true);

        // The track's stop key.
        const auto id = track[ids::id].toString();
        const auto& live = player.getLiveCells();
        const auto it = live.find (id);
        const bool active = it != live.end() && (it->second.until <= 0.0 || it->second.until > now);
        g.setColour (active ? theme::mute : theme::slotDark);
        g.fillRect (juce::Rectangle<int> (nameWidth - 26, y + 9, 16, 16));

        for (int s = 0; s < numScenes; ++s)
        {
            const auto r = cellBounds (row, s).toFloat();
            const auto cell = model::liveCell (project, id, s);
            if (! cell.isValid())
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (r, 4.0f);
                continue;
            }

            const bool playing = active && it->second.cellId == cell[ids::id].toString();
            const bool queued = playing && it->second.from > now;
            g.setColour (colour.withAlpha (playing ? 0.95f : 0.45f));
            g.fillRoundedRectangle (r, 4.0f);

            // How far through its loop: a ring of progress, as on Logic's cells.
            const auto region = cell.getChild (0);
            const double length = region[ids::length];
            if (playing && ! queued && length > 0.0)
            {
                const auto phase = (float) std::fmod (now - it->second.from, length) / (float) length;
                juce::Path arc;
                arc.addCentredArc (r.getRight() - 12.0f, r.getCentreY(), 8.0f, 8.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * phase, true);
                g.setColour (juce::Colours::white);
                g.strokePath (arc, juce::PathStrokeType (2.5f));
            }
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ((queued ? juce::String ("(next bar) ") : juce::String()) + region[ids::name].toString(),
                        r.reduced (6.0f, 0.0f).withTrimmedRight (20.0f), juce::Justification::centredLeft, true);
        }
    }

    if (list.isEmpty())
    {
        g.setColour (theme::dimText);
        g.drawText ("Add tracks first: each track is a row of cells.", getLocalBounds().withTrimmedTop (headerHeight + sceneRowHeight), juce::Justification::centred);
    }
}

void LiveLoopsGrid::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (8);
    stopAllButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (8);
    captureButton.setBounds (header.removeFromRight (130));
}

} // namespace riff
