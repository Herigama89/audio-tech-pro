#pragma once

#include <JuceHeader.h>
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"
#include "engine/InstrumentRack.h"
#include "ui/TimelineState.h"
#include "ui/WaveformCache.h"
#include "ui/TrackHeaders.h"
#include "ui/ArrangeCanvas.h"
#include "ui/LevelMeter.h"
#include "ui/PianoRoll.h"
#include "ui/InstrumentWindows.h"
#include "ui/Mixer.h"
#include "engine/EffectRack.h"

namespace riff
{

/**
    The main window's content: toolbar, track headers, tracks area, meters
    and status line. Owns the engine, the project tree and the undo history.
*/
class MainComponent final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    /** Asks to save unsaved changes, then calls `then` unless the user cancels. */
    void confirmDiscardChanges (std::function<void()> then);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void parentHierarchyChanged() override { updateTitle(); }

private:
    // Project lifecycle
    void setProject (juce::ValueTree newProject, const juce::File& file);
    void newProject();
    void openProject();
    void saveProject (std::function<void()> then = {});
    void saveProjectAs (std::function<void()> then = {});
    void showError (const juce::String& title, const juce::String& message);

    // Editing
    void addTrack();
    void addTrack (const juce::String& kind);
    void importAudio();
    void splitAtPlayhead();
    void deleteSelection();
    void undo();
    void redo();
    int selectedTrackIndex() const;

    // Transport
    void togglePlay();
    void returnToStart();
    void toggleRecord();
    void beginRecording();
    void finishRecording();
    void toggleMetronome();
    void toggleCycle();
    void showAudioMenu();
    void showLatencyCalibration();

    // Instruments and MIDI
    void showInstrumentMenu (juce::ValueTree track, juce::Component& target);
    void openInstrumentWindow (const juce::ValueTree& track);
    void showPluginScanner();
    void openPianoRoll (juce::ValueTree region);
    void closePianoRoll();
    void updateLiveMidiTrack();
    void showFileMenu();
    void exportSong();
    void showMixer();
    void closeMixer();
    void toggleMixer();
    void showEffectMenu (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target);
    void openInsertWindow (const juce::ValueTree& insert);
    void createMidiRegionFromTake (juce::ValueTree track, const AudioEngine::FinishedTake& take, juce::ValueTree& last);
    juce::File recordingFolder() const;
    juce::File nextTakeFile (const juce::ValueTree& track) const;

    // View
    void updateScrollBars();
    void updateTitle();
    void refreshProjectControls();
    juce::String deviceStatusText() const;
    juce::String formatPosition (double seconds) const;
    void showAudioSettings();

    // Listeners
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { markDirty(); }
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;
    void timerCallback() override;
    void markDirty();

    // --- Engine and model ---------------------------------------------------
    AudioEngine engine;
    AudioFileCache fileCache { engine.getFormatManager() };
    InstrumentRack rack;
    EffectRack effectRack { rack.getFormatManager() };
    ProjectPlayer player { engine, fileCache, &rack, &effectRack };
    juce::UndoManager undoManager;
    juce::ValueTree project;
    juce::File projectFile;
    bool dirty = false;
    juce::String startupError;

    // --- View -----------------------------------------------------------------
    TimelineState timeline;
    WaveformCache waveforms { engine.getFormatManager() };

    juce::TextButton fileButton { "File" }, mixerButton { "Mixer" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton rewindButton { "|<" }, playButton { "Play" }, recordButton { "Rec" }, clickButton { "Click" };
    juce::TextButton cycleButton { "Cycle" }, countInButton { "Count" };
    juce::TextButton addTrackButton { "+ Track" }, settingsButton { "Audio" };
    juce::Label positionLabel, tempoLabel, tempoCaption { {}, "BPM" }, masterCaption { {}, "Master" }, statusLabel;
    juce::Slider masterSlider;

    TrackHeaderList headers { undoManager, timeline };
    ArrangeCanvas canvas { undoManager, timeline, engine, fileCache, waveforms };
    juce::ScrollBar hScroll { false }, vScroll { true };
    LevelMeter inputMeter { "IN" }, outputMeter { "OUT" };
    PianoRoll pianoRoll { undoManager, engine };
    MixerPanel mixer { undoManager, engine, player };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;
    double lastPaintedPlayhead = -1.0;

    // Settings kept between sessions (not part of any project).
    juce::ApplicationProperties appProperties;
    double recordingOffsetMs = 0.0;

    // The cycle as it was when recording started (it can't change while recording).
    bool recordCycleOn = false;
    double recordCycleStart = 0.0, recordCycleEnd = 0.0;

    // Plugin and synth windows, by track id. Declared last so they close before
    // the instruments they show are destroyed.
    std::map<juce::String, std::unique_ptr<EditorWindow>> editorWindows;
    std::unique_ptr<EditorWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace riff
