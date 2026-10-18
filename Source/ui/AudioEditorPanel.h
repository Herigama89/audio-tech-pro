#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEdit.h"
#include "PitchEditor.h"

namespace riff
{

/**
    Logic's Audio File Editor for one audio region: its waveform, a selection
    you drag, and edits on the selection (or all of it when nothing is
    selected): Normalize, Gain, Fade In/Out, Silence, Reverse, Invert, and
    Flex-style Time Stretch and Transpose. Each edit writes a new file the
    region then plays, so the original stays and Undo brings it back.
*/
class AudioEditorPanel final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::ValueTree::Listener
{
public:
    AudioEditorPanel (juce::ValueTree region, juce::UndoManager& um, juce::AudioFormatManager& formats, juce::File editsFolder);
    ~AudioEditorPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    friend class SnapshotTool;
    void run (audioedit::Request request, const juce::String& name);
    void ask (const juce::String& title, const juce::String& question, const juce::String& initial,
              std::function<void (double)> then);
    void loadWaveform();
    juce::Rectangle<int> waveArea() const;
    double xToSeconds (int x) const;
    int secondsToX (double s) const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override;

    juce::ValueTree region;
    juce::UndoManager& undoManager;
    juce::AudioFormatManager& formats;
    juce::File editsFolder;

    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail thumbnail { 256, formats, thumbCache };
    double selFrom = 0.0, selTo = 0.0, dragAnchor = 0.0;

    juce::OwnedArray<juce::TextButton> buttons;

    // Flex Pitch: the notes found in the audio; drag one up or down to move its pitch.
    juce::TextButton flexPitchButton { "Flex Pitch" };
    std::unique_ptr<PitchEditor> pitchEditor;   // (shown while Flex Pitch is on)
    void showPitchEditor (bool show);
public:
    /** In the main window's editor pane (as Logic's Audio Track Editor): changes heard as the song plays, the playhead shown */
    AudioFileCache* liveCache = nullptr;
    std::function<double()> playheadSeconds;
    std::function<void (double)> setPlayheadSeconds;
    std::function<bool()> isPlaying;
    void showFlexPitch (bool show)   { flexPitchButton.setToggleState (show, juce::sendNotificationSync); }
    bool showsFlexPitch() const      { return flexPitchButton.getToggleState(); }
    juce::ValueTree getRegion() const { return region; }
private:
    std::vector<audioedit::PitchedNote> notes;
    double notesRate = 48000.0;
    int dragNote = -1, dragSemitones = 0;
    void findNotes();
    juce::Rectangle<float> noteBox (const audioedit::PitchedNote& n, double lowMidi, double highMidi) const;
    std::pair<double, double> pitchRange() const;
    juce::Label info;

    // Flex Time: markers on the hits (and any you add); drag one sideways and the
    // audio on either side of it stretches, up to the markers next to it.
    juce::TextButton flexTimeButton { "Flex Time" };
    std::vector<double> markers;       // seconds into the region, in order
    std::vector<double> handMarkers;   // the ones you added (kept through edits)
    int dragMarker = -1;
    double dragTo = 0.0;
    void findMarkers();
    int markerAt (int x) const;
    std::pair<double, double> markerLimits (int index) const;
};

} // namespace riff
