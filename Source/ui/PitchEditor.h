#pragma once

#include <JuceHeader.h>
#include "../engine/PitchCorrect.h"
#include "../engine/AudioFileCache.h"

namespace riff
{

/**
    The pitch editor, as Logic's Flex Pitch in the Audio Track Editor: the region's notes as boxes on a
    piano roll over its waveform, each with its pitch curve, and an inspector beside them.

      Inspector:  Time Quantize (grid, Q, Strength), Scale Quantize (key, scale, Q), Pitch Correction,
                  Gain, Pitch Drift, Vibrato and Formant Shift for the selected notes (or all of them)
      Notes:      drag up or down by semitones (Option: cents); sideways to move them in time;
                  hot spots on the note under the mouse, as in Logic: top left and right, drift; top
                  middle, fine pitch; bottom left, gain; bottom middle, vibrato; bottom right, formant
      Tools:      Pointer, Scissors (split a note where you click), Glue (join the selected notes)
      Menus:      Edit, Functions (Set to Perfect Pitch, drift, vibrato, formant, gain to 0...), View
      Keys:       Up/Down a semitone (Option: 10 cents); Delete: back as sung; Cmd+A: all

    Changes are heard as the song plays (rendered into the audio being played, from the original
    recording each time, so it never wears down), and Undo takes them back.
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
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** The song's key, for Scale Quantize "Song key" (pitch classes, C = 0). */
    std::function<std::vector<int>()> songScale;

    /** Hearing changes as you make them, as the song plays: the player's audio files, where the edited audio goes
        live (and is written to disk behind the scenes). Without it, each change writes a new file. */
    void setAudioCache (AudioFileCache* c) { cache = c; }

    /** The song's playhead (seconds), to show and follow it; clicking the ruler moves it. */
    std::function<double()> playheadSeconds;
    std::function<void (double)> setPlayheadSeconds;
    std::function<bool()> isPlaying;

    /** Waits for the analysis (for tests and snapshots). */
    void analyseNow();
    bool isReady() const noexcept { return ready; }

    static constexpr int inspectorWidth = 210;

private:
    friend class SnapshotTool;
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;

    // --- The recording and its notes -----------------------------------------------
    void load();
    std::pair<juce::AudioBuffer<float>, pitchcorrect::Analysis> analyseSource() const;
    void useAnalysis (juce::AudioBuffer<float> audio, pitchcorrect::Analysis result);
    void resetSegmentsAndEdits();   // the notes as the region says (splits and joins), and their edits

    juce::ValueTree region;
    juce::UndoManager& undoManager;
    juce::AudioFormatManager& formats;
    juce::File editsFolder;
    juce::File sourceFile;
    double sourceOffset = 0.0, sourceLength = 0.0;
    juce::AudioBuffer<float> source;
    pitchcorrect::Analysis original;    // as found
    pitchcorrect::Analysis analysis;    // with your splits and joins
    std::vector<pitchcorrect::NoteEdit> edits;
    juce::String lastCommitted, lastSegments;
    bool ready = false;
    int loadGeneration = 0;
    bool committing = false;            // (our own changes to the region: not an undo)             // (an analysis from an earlier load is dropped)
    std::unique_ptr<juce::Thread> worker;
    float maxLevel = 1.0e-3f;
    std::vector<float> peaks;           // the waveform's peak every 256 samples
    static constexpr int peakStep = 256;

    // --- Committing, and hearing it live -------------------------------------------
    void commit (const juce::String& undoName);
    std::vector<int> targets() const;   // the selected notes, or all of them
    AudioFileCache* cache = nullptr;
    std::shared_ptr<DecodedAudio> live;
    juce::File liveFile;
    std::vector<pitchcorrect::NoteEdit> liveEdits;   // what `live` sounds like now
    bool liveDirty = false, liveWhole = false;
    juce::ThreadPool writer { 1 };
    std::atomic<int> writeGeneration { 0 };
    double shownPlayhead = -1.0;
    bool ensureLive();
    void applyLive (const std::vector<pitchcorrect::NoteEdit>& target);
    void writeLiveSoon();
    std::vector<pitchcorrect::NoteEdit> editsWithDrag() const;

    // --- Geometry -------------------------------------------------------------------
    juce::Rectangle<int> menuBarArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> rollArea() const;
    juce::Rectangle<int> keyboardArea() const;
    float timeToX (double seconds) const;
    double xToTime (float x) const;
    float midiToY (double midi) const;
    double yToMidi (float y) const;
    juce::Rectangle<float> noteBox (int index, const pitchcorrect::NoteEdit& edit) const;
    int noteAt (juce::Point<float> p) const;

    // Hot spots on a note (as Logic): which one is under the mouse
    enum class Spot { none, driftStart, finePitch, driftEnd, gain, vibrato, formant };
    Spot spotAt (int note, juce::Point<float> p) const;
    std::array<juce::Rectangle<float>, 6> spotBoxes (int note) const;

    // View
    double lowMidi = 45.0, highMidi = 75.0;
    double zoom = 1.0, scroll = 0.0;
    bool showWaveform = true, showCurves = true;

    // --- Tools, selection, dragging -----------------------------------------------------
    enum class Tool { pointer, scissors, glue };
    Tool tool = Tool::pointer;
    std::set<int> selected;
    int hoverNote = -1;
    Spot hoverSpot = Spot::none;
    int dragNote = -1;
    Spot dragSpot = Spot::none;
    enum class Axis { undecided, pitch, time } dragAxis = Axis::undecided;
    double dragShift = 0.0, dragTime = 0.0;
    std::vector<pitchcorrect::NoteEdit> dragStartEdits;   // (hot spot drags: the values they started from)
    juce::Rectangle<float> rubberBand;
    bool rubberBanding = false;
    juce::String dragReadout;

    // --- Inspector ----------------------------------------------------------------------
    juce::Label nameLabel, notesLabel;
    juce::ComboBox timeGridBox, scaleKeyBox, scaleModeBox;
    juce::TextButton timeQButton { "Q" }, scaleQButton { "Q" };
    juce::Slider strengthSlider, correctionSlider, gainSlider, driftSlider, vibratoSlider, formantSlider;
    juce::OwnedArray<juce::Label> captions;
    void setupInspector();
    void refreshInspector();
    void sliderChanged (juce::Slider& s, bool finished);
    void quantizeTime();
    void quantizeScale();
    std::vector<int> scaleNotes() const;

    // --- Menus and tools ---------------------------------------------------------------------
    juce::TextButton editMenu { "Edit" }, functionsMenu { "Functions" }, viewMenu { "View" };
    juce::TextButton pointerTool, scissorsTool, glueTool;
    void showEditMenu();
    void showFunctionsMenu();
    void showViewMenu();
    void setTool (Tool t);
    void splitAt (int note, double seconds);
    void joinSelected();
    void setForTargets (std::function<void (pitchcorrect::NoteEdit&)> change, const juce::String& undoName);
    juce::Label status;
};

} // namespace riff
