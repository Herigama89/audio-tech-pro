#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "NoteViews.h"
#include "PlayerEditor.h"

namespace riff
{

/**
    Note editor for one MIDI region, shown in the bottom pane, with FL Studio's tools:
    Draw (P), Paint (B), Delete (D), Mute (T), Slice (C), Select (E) and Zoom (Z); right-click
    or right-drag deletes notes with any tool; chord Stamps; Snap; scale highlighting; ghost
    notes from the other regions; and the Tools menu (Chop, Glue, Strum, Arpeggiate, Flam,
    Randomize, Scale Levels, Limit, Claw Machine, Articulate, Legato, Quantize...).
    Arrows move the selection (Up/Down a semitone, Shift an octave; Left/Right a grid step),
    Cmd+D (or Ctrl+B) duplicates it, Cmd+C / Cmd+V copy and paste, Option-wheel zooms the rows.

    Click empty space to add a note (drag to set its length), drag a note to
    move it, drag its right edge to resize, Delete removes the selection,
    Q quantizes. Click the keyboard to hear a note.

    Below the notes, a lane for velocity (drag the stalks, or across them) or
    a controller / pitch bend (click to add points, drag to draw, Option-click
    to remove). The header has the quantize grid and strength, Scale Quantize
    (to the song's key) and the MIDI Transform presets.
*/
class PianoRoll final : public juce::Component,
                        private juce::ValueTree::Listener
{
public:
    PianoRoll (juce::UndoManager& um, AudioEngine& engine);
    ~PianoRoll() override;

    void setRegion (juce::ValueTree region);
    juce::ValueTree getRegion() const { return region; }

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void quantizeSelection();

    /** Step input (the Step In key on): notes you play go in one after another, a grid step each. */
    bool isStepInputOn() const { return stepButton.getToggleState() && region.isValid(); }
    void stepInput (const juce::MidiMessage& message);

private:
    enum class Drag { none, move, resize, keyboard, rubberBand, velocity, controller, timeHandle, paint, erase, muting, slice, zoom };
    enum class Tool { draw, paint, erase, mute, slice, select, zoom };
    Tool tool = Tool::draw;
    void setTool (Tool t);
    juce::TextButton toolButtons[7];
    juce::ComboBox stampBox, snapBox;
    juce::TextButton scaleHighlightButton { "Scale" }, ghostButton { "Ghosts" }, toolsButton { "Tools" };
    void showToolsMenu();
    double lastLength = 0.0;            // FL's pencil: new notes as long as the last one you drew or touched
    bool muteTo = true;                 // the Mute tool's drag: muting or unmuting
    std::vector<int> stampIntervals() const;
    double snapSeconds() const;         // the Snap setting (0: off)
    juce::Array<juce::ValueTree> addNotesAt (int pitch, double start, double length);   // with the stamp, if any
    void paintGhosts (juce::Graphics& g, juce::Rectangle<int> area);
    void moveSelection (double shift, int semitones);
    void duplicateSelection();
    static constexpr int toolbarHeight = 30;
    void eraseAt (juce::Point<float> p);
    void paintAt (juce::Point<float> p);
    double lastPaintStart = -1.0;
    int lastPaintPitch = -1;

    /** Time Handles: with several notes selected, a bar over them whose right end stretches their timing. */
    juce::Rectangle<float> timeHandleBar() const;
    double handleFrom = 0.0, handleTo = 0.0;

    static constexpr int headerHeight = 28;
    static constexpr int laneHeight = 84;
    static constexpr int laneGap = 4;
    static constexpr int keyboardWidth = 56;
    int rowHeight = 12;   // (Option-wheel zooms the rows)

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

    // Coordinates: "content time" is time within the region's notes (see Identifiers.h).
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> laneArea() const;
    int laneController() const;   // -1: velocity
    double quantizeGrid() const;
    void paintLane (juce::Graphics& g);
    void laneMouse (const juce::MouseEvent& e, bool down);
    void showTransformMenu();
    void scaleQuantizeSelection();
    juce::Array<juce::ValueTree> selectedOrAllNotes() const;
    juce::ValueTree controllerPointNear (juce::Point<float> p) const;
    float laneValueToY (double v) const;
    double laneYToValue (float y) const;
    float lastLaneX = 0.0f;
    bool laneDrawing = false;   // drawing new controller points, rather than moving one
    juce::ValueTree dragEvent;
    juce::Rectangle<float> rubberBand;
    float contentToX (double c) const;
    double xToContent (float x) const;
    int yToPitch (int y) const;
    int pitchToY (int pitch) const;
    double gridSeconds() const;
    double snapContent (double c) const { return snapContent (c, snapSeconds()); }
    double snapContent (double c, double grid) const;
    juce::Rectangle<float> noteBounds (const juce::ValueTree& note) const;
    juce::ValueTree noteAt (juce::Point<float> p) const;

    void previewNote (int pitch);
    void deleteSelection();
    bool isSelected (const juce::ValueTree& note) const;

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    juce::ValueTree region;

    juce::TextButton quantizeButton { "Quantize" }, closeButton { "Close" }, scaleButton { "Scale Q" }, transformButton { "Transform" };
    juce::ComboBox gridBox, strengthBox, laneBox, viewBox;   // viewBox: Piano Roll, Score, Step Editor
    friend class SnapshotTool;
    ScoreView score;
    StepEditor stepEditor { undoManager };
    PlayerEditor playerEditor { undoManager };
    void showView (int id);
    juce::TextButton stepButton { "Step In" }, restButton { "Rest" };
    double stepContent = 0.0;     // where the next step goes (content time)
    int stepHeld = 0;
    void advanceStep();
    juce::Label titleLabel;

    double pixelsPerSecond = 160.0;
    double scrollContent = 0.0;     // content time at the left edge of the grid
    int scrollY = 0;

    juce::Array<juce::ValueTree> selection;
    Drag drag = Drag::none;
    double dragStartContent = 0.0;
    int dragStartPitch = 60, lastPreviewPitch = -1, keyboardNote = -1;
    struct Original { juce::ValueTree note; double start, length; int pitch; };
    std::vector<Original> originals;
};

} // namespace riff
