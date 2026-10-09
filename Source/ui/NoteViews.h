#pragma once

#include <JuceHeader.h>
#include <set>

namespace riff
{

/**
    The MIDI region as music notation: a grand staff (treble and bass), the
    notes on it shown to the nearest sixteenth, with accidentals, ledger lines
    and bar lines. Click the staff to write a note (a quarter note on the
    sixteenth grid), drag a note up or down, Delete removes the selected one.
*/
class ScoreView final : public juce::Component,
                        private juce::ValueTree::Listener
{
public:
    ScoreView();
    ~ScoreView() override;

    void setRegion (juce::ValueTree region);
    void setUndoManager (juce::UndoManager* um) { undoManager = um; }
    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** The white key a staff step is on (inverse of staffStep for naturals). */
    static int pitchOfStep (int step);

    /** Staff steps from middle C (C4 = 0, D4 = 1 ... B4 = 6, C5 = 7), and whether it needs a sharp. */
    static int staffStep (int pitch, bool& sharp);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }

    juce::ValueTree region;
    juce::UndoManager* undoManager = nullptr;
    float scroll = 0.0f;
    // Where things were drawn (for the mouse).
    float layoutLeft = 70.0f, layoutMiddleC = 0.0f;
    double layoutFirstBarBeat = 0.0;
    std::vector<std::pair<juce::ValueTree, juce::Rectangle<float>>> heads;
    juce::ValueTree selected;
    int dragStartPitch = 60;
    static constexpr float pixelsPerBeat = 70.0f, lineGap = 8.0f;
};

/**
    Logic's Step Editor / Step Sequencer for a MIDI region: a row per sound
    (the General MIDI drum kit, plus any other notes in the region) and a
    column per sixteenth. Click (or drag across) steps to turn them on and
    off; Option-drag up or down on a step sets its velocity.
*/
class StepEditor final : public juce::Component,
                         private juce::ValueTree::Listener
{
public:
    explicit StepEditor (juce::UndoManager& um);
    ~StepEditor() override;

    void setRegion (juce::ValueTree region);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    /** The General MIDI name of a drum note, or the note's name. */
    static juce::String rowName (int pitch);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { rebuildRows(); repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }

    void rebuildRows();
    double stepSeconds() const;
    juce::ValueTree noteAt (int row, int step) const;
    bool cellAt (juce::Point<int> p, int& row, int& step) const;

    juce::UndoManager& undoManager;
    juce::ValueTree region;
    std::vector<int> rows;   // pitches, top to bottom
    int scrollSteps = 0;
    bool painting = true;    // what a drag sets: on or off
    juce::ValueTree velocityNote;
    int velocityStart = 100;
    std::set<std::pair<int, int>> touched;

    static constexpr int labelWidth = 110, rowHeight = 22, cellWidth = 22;
};

} // namespace riff
