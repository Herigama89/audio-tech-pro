#include "NoteViews.h"
#include "Theme.h"
#include "../model/Project.h"
#include <set>

namespace riff
{

//==============================================================================
// Score
//==============================================================================
ScoreView::ScoreView() {}

ScoreView::~ScoreView()
{
    region.removeListener (this);
}

void ScoreView::setRegion (juce::ValueTree r)
{
    region.removeListener (this);
    region = r;
    region.addListener (this);
    scroll = 0.0f;
    repaint();
}

int ScoreView::staffStep (int pitch, bool& sharp)
{
    // White keys get a step each; black keys sit on the note below, with a sharp.
    static constexpr int stepInOctave[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static constexpr bool black[12] { false, true, false, true, false, false, true, false, true, false, true, false };
    const auto octave = (int) std::floor ((pitch - 60) / 12.0);
    const auto pc = ((pitch % 12) + 12) % 12;
    sharp = black[pc];
    return octave * 7 + stepInOctave[pc];
}

void ScoreView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scroll = juce::jmax (0.0f, scroll - (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 300.0f);
    repaint();
}

void ScoreView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff4f1e8));   // paper
    if (! region.isValid())
        return;

    const auto project = region.getParent().getParent();
    const auto map = model::tempoMapOf (project);
    const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));
    const double regionStart = region[ids::start], offset = region[ids::offset], length = region[ids::length];
    const auto firstBeat = map.beatAt (regionStart);
    const auto lastBeat = map.beatAt (regionStart + length);

    const auto left = 70.0f;
    const auto firstBar = map.barAndBeat (firstBeat).first;
    const auto xOf = [&] (double beat) { return left + (float) (beat - map.beatOfBar (firstBar)) * pixelsPerBeat - scroll; };
    juce::ignoreUnused (beatsPerBar);

    // The grand staff: middle C between the two staves.
    const auto middleC = (float) getHeight() * 0.5f;
    const auto yOfStep = [&] (int step) { return middleC - (float) step * lineGap * 0.5f; };
    const int trebleLines[] { 2, 4, 6, 8, 10 };      // E4 G4 B4 D5 F5
    const int bassLines[] { -2, -4, -6, -8, -10 };   // A3 F3 D3 B2 G2

    g.setColour (juce::Colour (0xff2b2b2b));
    for (auto s : trebleLines) g.fillRect (10.0f, yOfStep (s) - 0.5f, (float) getWidth() - 20.0f, 1.0f);
    for (auto s : bassLines)   g.fillRect (10.0f, yOfStep (s) - 0.5f, (float) getWidth() - 20.0f, 1.0f);
    g.fillRect (10.0f, yOfStep (10), 1.5f, yOfStep (-10) - yOfStep (10));

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("Treble", juce::Rectangle<float> (14.0f, yOfStep (10) - 16.0f, 60.0f, 14.0f), juce::Justification::left, false);
    g.drawText ("Bass", juce::Rectangle<float> (14.0f, yOfStep (-2) - 16.0f, 60.0f, 14.0f), juce::Justification::left, false);

    // Bar lines and numbers.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (juce::Rectangle<int> ((int) left - 6, 0, getWidth() - (int) left, getHeight()));
    for (auto bar = firstBar; map.beatOfBar (bar) <= lastBeat + 1.0e-6; ++bar)
    {
        const auto x = xOf (map.beatOfBar (bar));
        g.setColour (juce::Colour (0xff2b2b2b));
        g.fillRect (x, yOfStep (10), 1.0f, yOfStep (-10) - yOfStep (10));
        g.setColour (juce::Colour (0xff777777));
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, yOfStep (10) - 30.0f, 30.0f, 12.0f), juce::Justification::left, false);
    }

    // The notes, to the nearest sixteenth.
    for (const auto& note : region)
    {
        if (! note.hasType (ids::NOTE))
            continue;
        const double contentStart = note[ids::start];
        if (contentStart < offset || contentStart >= offset + length)
            continue;
        const auto songTime = regionStart + contentStart - offset;
        const auto beat = std::round (map.beatAt (songTime) * 4.0) / 4.0;
        const auto beats = juce::jmax (0.25, std::round ((map.beatAt (songTime + (double) note[ids::length]) - map.beatAt (songTime)) * 4.0) / 4.0);

        bool sharp = false;
        const int pitch = note[ids::pitch];
        const auto step = staffStep (pitch, sharp);
        const auto x = xOf (beat) + 14.0f;
        const auto y = yOfStep (step);

        // Ledger lines above/below the staves (and middle C).
        g.setColour (juce::Colour (0xff2b2b2b));
        if (step >= 12)
            for (int s = 12; s <= step; s += 2) g.fillRect (x - 8.0f, yOfStep (s) - 0.5f, 16.0f, 1.0f);
        if (step <= -12)
            for (int s = -12; s >= step; s -= 2) g.fillRect (x - 8.0f, yOfStep (s) - 0.5f, 16.0f, 1.0f);
        if (step == 0)
            g.fillRect (x - 8.0f, yOfStep (0) - 0.5f, 16.0f, 1.0f);

        // Head: open for half and whole notes; a stem below a whole note; a dot when dotted.
        const juce::Rectangle<float> head (x - 5.0f, y - 3.6f, 10.0f, 7.2f);
        if (beats >= 2.0) g.drawEllipse (head, 1.6f);
        else              g.fillEllipse (head);
        if (beats < 4.0)
        {
            const bool up = step >= 0 ? step < 6 : step < -6;   // stems point away from the middle of each staff
            if (up) g.fillRect (head.getRight() - 1.2f, y - 26.0f, 1.2f, 26.0f);
            else    g.fillRect (head.getX(), y, 1.2f, 26.0f);
            // Flags for eighths and sixteenths.
            const int flags = beats <= 0.25 ? 2 : beats <= 0.5 ? 1 : 0;
            for (int f = 0; f < flags; ++f)
            {
                const auto fy = up ? y - 26.0f + (float) f * 5.0f : y + 26.0f - (float) f * 5.0f;
                const auto fx = up ? head.getRight() - 1.0f : head.getX();
                g.drawLine (fx, fy, fx + 7.0f, fy + (up ? 7.0f : -7.0f), 1.6f);
            }
        }
        const auto dotted = std::abs (beats - 1.5) < 0.01 || std::abs (beats - 3.0) < 0.01 || std::abs (beats - 0.75) < 0.01;
        if (dotted)
            g.fillEllipse (head.getRight() + 2.0f, y - 1.5f, 3.0f, 3.0f);
        if (sharp)
        {
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("#", juce::Rectangle<float> (x - 18.0f, y - 8.0f, 10.0f, 16.0f), juce::Justification::centred, false);
        }
    }
}

//==============================================================================
// Step Editor
//==============================================================================
namespace
{
    const std::vector<std::pair<int, const char*>>& drumKit()
    {
        static const std::vector<std::pair<int, const char*>> kit {
            { 49, "Crash" }, { 51, "Ride" }, { 46, "Open Hi-Hat" }, { 42, "Closed Hi-Hat" }, { 50, "High Tom" },
            { 47, "Mid Tom" }, { 45, "Low Tom" }, { 39, "Clap" }, { 37, "Side Stick" }, { 38, "Snare" }, { 36, "Kick" } };
        return kit;
    }
}

StepEditor::StepEditor (juce::UndoManager& um) : undoManager (um) {}

StepEditor::~StepEditor()
{
    region.removeListener (this);
}

juce::String StepEditor::rowName (int pitch)
{
    for (const auto& [p, name] : drumKit())
        if (p == pitch)
            return name;
    return juce::MidiMessage::getMidiNoteName (pitch, true, true, 3);
}

void StepEditor::setRegion (juce::ValueTree r)
{
    region.removeListener (this);
    region = r;
    region.addListener (this);
    scrollSteps = 0;
    rebuildRows();
    repaint();
}

void StepEditor::rebuildRows()
{
    std::set<int> pitches;
    for (const auto& [p, name] : drumKit())
        pitches.insert (p);
    for (const auto& n : region)
        if (n.hasType (ids::NOTE))
            pitches.insert ((int) n[ids::pitch]);
    rows.assign (pitches.rbegin(), pitches.rend());   // high at the top
}

double StepEditor::stepSeconds() const
{
    const auto project = region.getParent().getParent();
    return model::tempoMapOf (project).secondsPerBeatAt (region[ids::start]) / 4.0;
}

juce::ValueTree StepEditor::noteAt (int row, int step) const
{
    const auto from = (double) region[ids::offset] + step * stepSeconds();
    for (const auto& n : region)
        if (n.hasType (ids::NOTE) && (int) n[ids::pitch] == rows[(size_t) row]
            && (double) n[ids::start] >= from - stepSeconds() * 0.5 && (double) n[ids::start] < from + stepSeconds() * 0.5)
            return n;
    return {};
}

bool StepEditor::cellAt (juce::Point<int> p, int& row, int& step) const
{
    if (p.x < labelWidth || p.y < 0)
        return false;
    row = p.y / rowHeight;
    step = (p.x - labelWidth) / cellWidth + scrollSteps;
    const auto steps = (int) std::floor ((double) region[ids::length] / stepSeconds() + 1.0e-6);
    return juce::isPositiveAndBelow (row, (int) rows.size()) && juce::isPositiveAndBelow (step, steps);
}

void StepEditor::mouseDown (const juce::MouseEvent& e)
{
    int row = 0, step = 0;
    touched.clear();
    velocityNote = {};
    if (! region.isValid() || ! cellAt (e.getPosition(), row, step))
        return;

    auto existing = noteAt (row, step);
    if (e.mods.isAltDown() && existing.isValid())
    {
        undoManager.beginNewTransaction ("Step velocity");
        velocityNote = existing;
        velocityStart = existing[ids::velocity];
        return;
    }

    undoManager.beginNewTransaction ("Step Editor");
    painting = ! existing.isValid();
    mouseDrag (e);
}

void StepEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (velocityNote.isValid())
    {
        model::setNoteVelocity (velocityNote, juce::jlimit (1, 127, velocityStart - e.getDistanceFromDragStartY()), &undoManager);
        return;
    }

    int row = 0, step = 0;
    if (! region.isValid() || ! cellAt (e.getPosition(), row, step) || touched.count ({ row, step }) > 0)
        return;
    touched.insert ({ row, step });

    auto existing = noteAt (row, step);
    if (painting && ! existing.isValid())
        model::addNote (region, rows[(size_t) row], (double) region[ids::offset] + step * stepSeconds(), stepSeconds() * 0.9, 100, &undoManager);
    else if (! painting && existing.isValid())
        model::deleteNote (existing, &undoManager);
}

void StepEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto steps = (int) std::floor ((double) region[ids::length] / stepSeconds());
    scrollSteps = juce::jlimit (0, juce::jmax (0, steps - 4), scrollSteps - juce::roundToInt ((wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 20.0f));
    repaint();
}

void StepEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (! region.isValid())
        return;

    const auto steps = (int) std::floor ((double) region[ids::length] / stepSeconds() + 1.0e-6);
    const auto colour = model::trackColour (region.getParent());
    const auto project = region.getParent().getParent();
    const int beatsPerBar = juce::jmax (1, (int) project.getProperty (ids::timeSigNumerator, 4));

    for (size_t r = 0; r < rows.size(); ++r)
    {
        const auto y = (int) r * rowHeight;
        g.setColour (r % 2 == 0 ? theme::laneA : theme::laneB);
        g.fillRect (0, y, getWidth(), rowHeight);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (rowName (rows[r]), 8, y, labelWidth - 12, rowHeight, juce::Justification::centredLeft, true);

        for (int s = scrollSteps; s < steps; ++s)
        {
            const auto x = labelWidth + (s - scrollSteps) * cellWidth;
            if (x > getWidth())
                break;
            const auto cell = juce::Rectangle<int> (x + 2, y + 3, cellWidth - 4, rowHeight - 6);
            const auto beatShade = (s / 4) % 2 == 0;
            g.setColour (beatShade ? theme::slotDark : theme::slotDark.brighter (0.08f));
            g.fillRoundedRectangle (cell.toFloat(), 2.0f);
            if (const auto note = noteAt ((int) r, s); note.isValid())
            {
                const auto vel = (int) note[ids::velocity] / 127.0f;
                g.setColour (colour.withMultipliedBrightness (0.5f + 0.7f * vel));
                g.fillRoundedRectangle (cell.toFloat(), 2.0f);
            }
            if (s % (4 * beatsPerBar) == 0)
            {
                g.setColour (theme::gridBar);
                g.fillRect (x, 0, 1, (int) rows.size() * rowHeight);
            }
        }
    }
}

} // namespace riff
