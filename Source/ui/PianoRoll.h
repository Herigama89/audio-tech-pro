#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"

namespace riff
{

/**
    Note editor for one MIDI region, shown in the bottom pane.

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

private:
    enum class Drag { none, move, resize, keyboard, rubberBand, velocity, controller };

    static constexpr int headerHeight = 28;
    static constexpr int laneHeight = 84;
    static constexpr int laneGap = 4;
    static constexpr int keyboardWidth = 56;
    static constexpr int rowHeight = 12;

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
    double snapContent (double c) const { return snapContent (c, gridSeconds()); }
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
    juce::ComboBox gridBox, strengthBox, laneBox;
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
