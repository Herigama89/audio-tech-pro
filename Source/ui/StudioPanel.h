#pragma once

#include <JuceHeader.h>
#include "../engine/studio/Studio.h"

namespace riff
{

/**
    A studio instrument's window (Drums, Keys, Bass, Guitar, Beat Pad, Sampler): its face from its picture,
    the knobs, faders, pads and keys working on it, the screen showing what it's doing, the bar of choices
    under it; and its MIX tab - each part's strip with inserts (ours or any plug-in), sends, pan and fader.
*/
class StudioPanel final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer,
                          private juce::ValueTree::Listener
{
public:
    struct Host
    {
        std::function<std::shared_ptr<studio::StudioInstrument>()> instrument;
        /** Adds or replaces an insert in a chain (the effect chooser), and opens one. */
        std::function<void (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)> chooseEffect;
        std::function<void (juce::ValueTree insert)> openInsert;
        /** Plays a note on the track (a click on a pad, a key, a drum). */
        std::function<void (int note, int velocity, bool on)> playNote;
        /** Makes a beat pad track from slices of the Sampler's recording (the files written, one per pad). */
        std::function<void (const juce::Array<juce::File>& slices)> slicesToBeatPad;
    };

    StudioPanel (juce::ValueTree project, juce::ValueTree track, juce::UndoManager& um, Host host);
    ~StudioPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** The page: 0 play, 1 mix (and the beat pad's sequencer, 2). */
    void showPage (int page);
    /** The size it opens at (it scales to its window). */
    static juce::Rectangle<int> naturalSize (const juce::String& kind);

    struct Layout;
    struct Control;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { rebuildMix(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { rebuildMix(); }

    float value (const juce::String& id) const;
    float normalised (const juce::String& id) const;
    void setValue (const juce::String& id, float v, bool newGesture);
    void setNormalised (const juce::String& id, float v, bool newGesture);
    juce::Point<float> toFace (juce::Point<float> p) const;
    Control* controlAt (juce::Point<float> facePoint);
    void press (Control& c, const juce::MouseEvent& e);
    void paintScreen (juce::Graphics&, juce::Rectangle<float> area);
    void paintMix (juce::Graphics&);
    void paintSampler (juce::Graphics&, juce::Rectangle<float> area);
    void paintPads (juce::Graphics&);
    void rebuildMix();
    void buildBar();
    void cycle (const juce::String& id, int delta);
    void showChoiceMenu (const juce::String& id, juce::Component* target);
    void applyKit (int kit);
    int kitPieceAt (juce::Point<float> facePoint) const;
    int keyAt (juce::Point<float> facePoint) const;
    juce::Rectangle<float> keyRect (int note) const;
    void slice();

    juce::ValueTree project, track, state;
    juce::UndoManager& um;
    Host host;
    juce::String kind;
    std::unique_ptr<Layout> layout;
    juce::Image face, knobFaces;
    std::vector<std::unique_ptr<Control>> controls;
    Control* dragging = nullptr;
    float dragStart = 0.0f;
    juce::Point<float> dragFrom;
    int page = 0;
    int heldNote = -1;
    int screenPage = 0;
    juce::String hoverId;
    float scale = 1.0f;
    juce::Rectangle<float> faceArea, barArea, mixArea;
    int dragMarker = -1;   // (the sampler's: start, loop start, loop end, end)

    // the bar under the face: the main choices
    juce::OwnedArray<juce::TextButton> barButtons;
    juce::OwnedArray<juce::Label> barLabels;

    // the MIX tab: per part (and the returns): output, inserts, sends, pan, mute/solo, fader
    struct Strip;
    juce::OwnedArray<Strip> strips;
    juce::Component mixHolder;
    juce::Viewport mixView;
};

} // namespace riff
