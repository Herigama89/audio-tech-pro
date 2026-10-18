#pragma once

#include <JuceHeader.h>
#include "../engine/PitchCorrect.h"

namespace riff
{

/**
    The pitch editor (Melodyne's blobs, Logic's Flex Pitch): an audio region's notes as blobs on a
    piano roll, each at the pitch it's sung, thick where it's loud, with its pitch curve through it.

      - drag a note up or down: by semitones (Option: by cents); every selected note moves with it
      - double-click a note: onto the nearest semitone
      - Correct: the selected notes (or all) onto the nearest semitone, or the song's key, by the amount set
      - Straighten: their drift and vibrato flattened towards each note's centre
      - Up/Down arrows: a semitone (Option: 10 cents); Delete: the note back as sung; Cmd+A: all

    The audio is rendered again from the original recording after each change (the edits are kept on
    the region), so it never wears down, and Undo brings back the last version.
*/
class PitchEditor final : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::Timer
{
public:
    PitchEditor (juce::ValueTree region, juce::UndoManager& um, juce::AudioFormatManager& formats, juce::File editsFolder);
    ~PitchEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** The song's key, for Correct to Key (pitch classes, C = 0); empty: chromatic. */
    std::function<std::vector<int>()> songScale;

    /** Waits for the analysis (for tests and snapshots). */
    void analyseNow();
    bool isReady() const noexcept { return ready; }

private:
    friend class SnapshotTool;
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;

    void load();
    std::pair<juce::AudioBuffer<float>, pitchcorrect::Analysis> analyseSource() const;
    void useAnalysis (juce::AudioBuffer<float> audio, pitchcorrect::Analysis result);
    void commit (const juce::String& undoName);
    std::vector<int> targets() const;   // the selected notes, or all of them

    juce::Rectangle<int> rollArea() const;
    float timeToX (double seconds) const;
    double xToTime (float x) const;
    float midiToY (double midi) const;
    double yToMidi (float y) const;
    int noteAt (juce::Point<float> p) const;
    juce::Path blobFor (size_t index, double shiftNow) const;

    juce::ValueTree region;
    juce::UndoManager& undoManager;
    juce::AudioFormatManager& formats;
    juce::File editsFolder;

    // The original recording the edits apply to
    juce::File sourceFile;
    double sourceOffset = 0.0, sourceLength = 0.0;
    juce::AudioBuffer<float> source;
    pitchcorrect::Analysis analysis;
    std::vector<pitchcorrect::NoteEdit> edits;
    juce::String lastCommitted;
    bool ready = false;
    std::unique_ptr<juce::Thread> worker;
    float maxLevel = 1.0e-3f;

    // View
    double lowMidi = 45.0, highMidi = 75.0;
    double zoom = 1.0, scroll = 0.0;   // (1: the whole region fits)

    // Selection and dragging
    std::set<int> selected;
    int dragNote = -1;
    double dragShift = 0.0;            // semitones being dragged
    juce::Rectangle<float> rubberBand;
    bool rubberBanding = false;

    juce::TextButton correctButton { "Correct" }, keyButton { "To Key" }, straightenButton { "Straighten" }, resetButton { "Reset" };
    juce::Slider amountSlider;
    juce::Label status;
};

} // namespace riff
