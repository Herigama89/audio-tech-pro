#pragma once

#include <JuceHeader.h>
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"
#include "ui/TimelineState.h"
#include "ui/WaveformCache.h"
#include "ui/TrackHeaders.h"
#include "ui/ArrangeCanvas.h"
#include "ui/LevelMeter.h"

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
    void importAudio();
    void splitAtPlayhead();
    void deleteSelection();
    void undo();
    void redo();
    int selectedTrackIndex() const;

    // Transport
    void togglePlay();
    void returnToStart();

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
    ProjectPlayer player { engine, fileCache };
    juce::UndoManager undoManager;
    juce::ValueTree project;
    juce::File projectFile;
    bool dirty = false;
    juce::String startupError;

    // --- View -----------------------------------------------------------------
    TimelineState timeline;
    WaveformCache waveforms { engine.getFormatManager() };

    juce::TextButton newButton { "New" }, openButton { "Open" }, saveButton { "Save" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton rewindButton { "|<" }, playButton { "Play" };
    juce::TextButton addTrackButton { "+ Track" }, importButton { "Import" }, settingsButton { "Audio" };
    juce::Label positionLabel, tempoLabel, tempoCaption { {}, "BPM" }, masterCaption { {}, "Master" }, statusLabel;
    juce::Slider masterSlider;

    TrackHeaderList headers { undoManager, timeline };
    ArrangeCanvas canvas { undoManager, timeline, engine, fileCache, waveforms };
    juce::ScrollBar hScroll { false }, vScroll { true };
    LevelMeter inputMeter { "IN" }, outputMeter { "OUT" };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;
    double lastPaintedPlayhead = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace riff
