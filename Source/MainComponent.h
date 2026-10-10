#pragma once

#include <JuceHeader.h>
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"
#include "engine/AutomationWriter.h"
#include "engine/InstrumentRack.h"
#include "ui/TimelineState.h"
#include "ui/WaveformCache.h"
#include "ui/TrackHeaders.h"
#include "ui/ArrangeCanvas.h"
#include "ui/LevelMeter.h"
#include "ui/PianoRoll.h"
#include "ui/InstrumentWindows.h"
#include "ui/Mixer.h"
#include "ui/Inspector.h"
#include "engine/EffectRack.h"
#include "ui/ConsoleLook.h"
#include "ui/GeneratorPanel.h"
#include "ui/AssistantPanel.h"
#include "ui/ScreenRecorder.h"
#include "ui/ScreenRecordPanel.h"
#include "ui/LibraryPanel.h"
#include "ui/SidePanels.h"
#include "ui/PerformanceMeter.h"
#include "ui/SmartControls.h"
#include "ui/CombineInterfacesPanel.h"
#include "engine/AggregateDevice.h"
#include "model/ProjectFiles.h"
#include "engine/ScreenAudioTap.h"
#include "ai/Assistant.h"
#include "ai/AssistantTools.h"
#include "model/Generator.h"

namespace riff
{

/**
    The main window's content: toolbar, track headers, tracks area, meters
    and status line. Owns the engine, the project tree and the undo history.
*/
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::ValueTree::Listener,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    /** `openAudioDevice` false skips the audio device (for the snapshot tool). */
    explicit MainComponent (bool openAudioDevice = true);
    ~MainComponent() override;

    /** For the start-up scan: the plugin formats, the list of plugins found, and where a crash is noted. */
    juce::AudioPluginFormatManager& getPluginFormats() noexcept { return rack.getFormatManager(); }
    juce::KnownPluginList& getKnownPlugins() noexcept           { return rack.getKnownPlugins(); }
    juce::File pluginScanCrashFile();
    void saveKnownPlugins();

    /** Asks to save unsaved changes, then calls `then` unless the user cancels. */
    void confirmDiscardChanges (std::function<void()> then);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override { updateTitle(); }

    // Commands (menus and key commands)
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform (const InvocationInfo& info) override;

    // The menu bar (the Mac's main menu)
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }

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
    void copySelectedRegions (bool cut);
    void pasteRegions();
    void selectAllRegions();
    void repeatSelectedRegions();
    void joinSelectedRegions();
    void muteSelectedRegions();
    void nudgeSelectedRegions (int direction);
    void duplicateSelectedTrack();
    void showToolMenu (bool withShortcuts = false);
    void setTool (TimelineState::Tool tool);
    void applyNoOverlap (const juce::Array<juce::ValueTree>& regions);

    // The region clipboard: copies, with each one's track offset from the topmost and its time
    juce::Array<juce::ValueTree> clipboardRegions;
    juce::Array<int> clipboardTrackOffsets;
    double clipboardStart = 0.0;
    void undo();
    void redo();
    int selectedTrackIndex() const;

    // Transport
    void togglePlay();
    void returnToStart();
    void toggleRecord();
    void beginRecording();
    void finishRecording();
    void replaceUnder (const juce::ValueTree& region);
    bool recordPunchOn = false;
    double recordPunchIn = 0.0, recordPunchOut = 0.0;
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
    void exportStems();
    void toggleAutopunch();
    void showUndoHistory();
    void showPreferences();
    void saveAsTemplate();
    void newAlternative();
    void showEventList();
    void showLiveLoops();
    void showThemeSettings();
    void showChannelSettingMenu (juce::ValueTree track, juce::Component& target);
    juce::ValueTree copiedChannelStrip;   // Copy Channel Strip
    void applyTheme (const theme::Settings& settings);
    void showControllerAssignments();
    void toggleLibrary (LibraryPanel::Page page);
    void toggleSmartControls();
    void showGroupSettings();
    void showIOLabels();
    void showSignalFlow();
    std::unique_ptr<SmartControls> smartControls;
    std::unique_ptr<LibraryPanel> libraryPanel;
    void handleIncomingMidi();
    juce::ValueTree learningTrack;      // MIDI Learn: waiting for a controller for this track's...
    juce::String learningParam;         // ... parameter
    std::map<juce::String, bool> lastSwitchState;   // mute/solo assignments: pressed or not
    void addTemplateAndAlternativeItems (juce::PopupMenu& menu);
    juce::File templatesFolder() const;
    int autosaveMinutes = 2;
    bool regeneratingPlayer = false;
    void toggleFreeze (juce::ValueTree track);
    void createStackFromSelection (bool summing);
    void newVcaForSelection();
    void trimSelectedToPlayhead (bool start);
    void loopSelectedRegions();
    void bounceInPlace();
    void importTracksFromProject();
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
    void showSignatureMenu();
    void mouseDown (const juce::MouseEvent& e) override;
    juce::String deviceStatusText() const;
    juce::String formatPosition (double seconds) const;
    juce::String formatClock (double seconds) const;
    juce::Rectangle<int> lcdBounds() const;
    void showAudioSettings();
    void showKeyCommands();
    void showGenerator();
    void toggleAssistant();
    void toggleScreenRecording();
    void showCombineInterfaces();
    juce::String combineInterfaces (const aggregate::Setup& setup);
    void separateInterfaces();
    juce::String useAudioDevice (const juce::String& name, int numInputsToEnable);
    void updateInputLabels();
    void saveDeviceState();
    void startScreenRecording (const ScreenRecordSettings& settings);
    void stopScreenRecording (const juce::String& failure = {});
    void updateScreenRecording();
    void showAssistantSettings();
    void createAssistant();
    ai::AppHooks makeAssistantHooks();
    juce::String assistantManual() const;
    void measureMixForAssistant (double start, double end, std::function<void (ai::AppHooks::Measurement)> done);
    void generateMidi (const GeneratorPanel::Request& request);
    void applyKeyPreset (const juce::String& preset);
    void saveKeyMappings();
    void zoomHorizontally (double factor);

    // Commands that act on the selection
    juce::ValueTree selectedTrack() const;
    void toggleTrackFlag (const juce::Identifier& flag, const juce::String& undoName);
    void selectAdjacentTrack (int delta);
    void scrollToTrack (int index);
    void zoomVertically (int steps);
    void selectTrackFromMixer (const juce::String& trackId, juce::ModifierKeys mods = {});
    void hideSelectedTracks();
    void toggleInspector();
    void showCreateStackDialog();
    void showInspectorTrackMenu (juce::ValueTree track, juce::Component& target);

    // Logic's local menus over the track headers: Edit, Functions, View
    juce::TextButton localEditButton { "Edit" }, localFunctionsButton { "Functions" }, localViewButton { "View" };
    void showLocalMenu (int which, juce::Component& target);
    void showTrackColours();
    void moveByBars (int bars);
    void quantizeSelection();
    void setLocatorsBySelection();
    void togglePianoRoll();
    void deleteSelectedTrack();

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
    ScreenAudioTap screenTap;   // (before the engine, which may hold it while recording)
    AudioEngine engine;
    AudioFileCache fileCache { engine.getFormatManager(), true };   // decodes in the background
    bool wasLoadingAudio = false;
    InstrumentRack rack;
    EffectRack effectRack { rack.getFormatManager() };
    ProjectPlayer player { engine, fileCache, &rack, &effectRack };
    juce::UndoManager undoManager;
    AutomationWriter automation { engine, player, undoManager };
    juce::ValueTree project;
    juce::File projectFile;
    bool dirty = false;

    // Autosave: every couple of minutes while there are unsaved changes
    juce::String sessionId = juce::Uuid().toString().substring (0, 8);
    bool changedSinceAutosave = false;
    juce::uint32 lastAutosaveMs = 0;
    juce::File currentAutosave;
    void autosave();
    void removeAutosave();
    void offerRecovery();
    void consolidate();
    void showBackups();
    juce::String startupError;

    // --- View -----------------------------------------------------------------
    TimelineState timeline;
    WaveformCache waveforms { engine.getFormatManager() };

    juce::TextButton fileButton { "File" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton addTrackButton { "+ Track" }, settingsButton { "Audio" };

    // Transport keys (cream, with icons; their captions are painted underneath)
    IconButton rewindButton    { "Start",   IconButton::Icon::toStart };
    IconButton backBarButton   { "Rewind",  IconButton::Icon::rewind };
    IconButton forwardButton   { "Forward", IconButton::Icon::forward };
    IconButton stopButton      { "Stop",    IconButton::Icon::stop };
    IconButton playButton      { "Play",    IconButton::Icon::play,   juce::Colour (0xff3d9f4f) };
    IconButton recordButton    { "Rec",     IconButton::Icon::record, juce::Colour (0xffd8352d) };
    IconButton cycleButton     { "Cycle",   IconButton::Icon::cycle,  juce::Colour (0xffe9b949) };
    IconButton countInButton   { "Count",   IconButton::Icon::countIn, juce::Colour (0xff5a9cff) };
    IconButton clickButton     { "Click",   IconButton::Icon::metronome, juce::Colour (0xff5a9cff) };
    IconButton punchButton     { "Punch",   IconButton::Icon::punch,  juce::Colour (0xffd8352d) };
    IconButton replaceButton   { "Replace", IconButton::Icon::replace, juce::Colour (0xffd8352d) };
    IconButton mixerButton     { "Mixer",   IconButton::Icon::mixer,  juce::Colour (0xff5a9cff) };
    IconButton editorButton    { "Editor",  IconButton::Icon::pianoRoll, juce::Colour (0xff5a9cff) };
    IconButton keysButton      { "Keys",    IconButton::Icon::keys };
    IconButton inspectorButton { "Inspector", IconButton::Icon::inspector, juce::Colour (0xff5a9cff) };
    IconButton libraryButton   { "Library", IconButton::Icon::library, juce::Colour (0xff5a9cff) };
    IconButton smartButton     { "Smart",   IconButton::Icon::smartControls, juce::Colour (0xff5a9cff) };
    IconButton helpButton      { "Help",    IconButton::Icon::quickHelp, juce::Colour (0xff5a9cff) };
    IconButton toolbarButton   { "Toolbar", IconButton::Icon::toolbar, juce::Colour (0xff5a9cff) };
    IconButton listButton      { "Lists",   IconButton::Icon::listEditors, juce::Colour (0xff5a9cff) };
    IconButton notesButton     { "Notes",   IconButton::Icon::notepad, juce::Colour (0xff5a9cff) };
    IconButton loopsButton     { "Loops",   IconButton::Icon::loopBrowser, juce::Colour (0xff5a9cff) };
    IconButton browserButton   { "Browser", IconButton::Icon::browser, juce::Colour (0xff5a9cff) };

    // Logic's Toolbar (a row of edit keys) and the bar over the tracks (menus, tool, snap, drag, zoom)
    juce::OwnedArray<juce::TextButton> functionKeys;
    juce::Rectangle<int> functionBarArea, tracksBarArea;
    juce::TextButton waveformZoomButton, fitVerticalButton, fitHorizontalButton;
    juce::Slider verticalZoom, horizontalZoom;
    static constexpr int functionBarHeight = 34, tracksBarHeight = 30;
    void zoomToFit (bool horizontally, bool vertically);
    void syncZoomSliders();

    // Quick Help, Notepad and Browser
    bool quickHelpShown = false;
    juce::Label quickHelpBox;
    juce::Component* lastHelpTarget = nullptr;
    void updateQuickHelp();
    std::unique_ptr<NotepadPanel> notepadPanel;
    std::unique_ptr<BrowserPanel> browserPanel;
    void toggleNotepad();
    void toggleBrowser();

    // View > Show / Hide (kept between sessions)
    bool showControlBar = true, showToolbarRow = false, showStatusBar = true, showMeters = true, showTrackHeaders = true;
    int barHeight() const;
    void setPanelShown (const juce::String& key, bool& flag, bool shown);
    void applyPanelVisibility();
    IconButton generateButton  { "Generate", IconButton::Icon::generate, juce::Colour (0xffd3ae55) };
    juce::ComboBox toolBox, snapBox, dragBox;   // Logic's tool, Snap and Drag menus
    IconButton screenButton    { "Screen",  IconButton::Icon::screen, juce::Colour (0xffd8352d) };
    IconButton claudeButton    { "Claude",  IconButton::Icon::chat,  juce::Colour (0xffd97757) };

    juce::Label signatureLabel;   // time signature and key, in the display (click to change)
    juce::Label positionLabel, clockLabel, tempoLabel, tempoCaption { {}, "BPM" },
                masterCaption { {}, "Master" }, statusLabel;
    juce::Slider masterSlider;

    TrackHeaderList headers { undoManager, timeline };
    ArrangeCanvas canvas { undoManager, timeline, engine, fileCache, waveforms };
    juce::ScrollBar hScroll { false }, vScroll { true };
    LevelMeter inputMeter { "IN" }, outputMeter { "OUT" };
    PianoRoll pianoRoll { undoManager, engine };
    MixerPanel mixer { undoManager, engine, player, effectRack };

    /** The mixer or the editor in a window of its own (Logic's Mixer and Piano Roll windows): resizable, anywhere. */
    struct DetachedWindow final : juce::DocumentWindow
    {
        DetachedWindow (const juce::String& title, juce::Component& content, const juce::String& savedState, std::function<void()> onCloseRequested);
        void closeButtonPressed() override;
        std::function<void()> onClose;
    };
    std::unique_ptr<DetachedWindow> mixerWindow, editorWindow;
    void openMixerWindow();
    void dockMixer();
    void openEditorWindow();
    void dockEditor();
    bool mixerInPane() const   { return mixer.isVisible() && mixerWindow == nullptr; }
    bool editorInPane() const  { return pianoRoll.isVisible() && editorWindow == nullptr; }

    /** The bar between the tracks and the editor or mixer below: drag it to resize them. */
    struct PaneDivider final : juce::Component
    {
        PaneDivider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff141516));
            g.setColour (isMouseOverOrDragging() ? juce::Colour (0xff2f8cff) : juce::Colour (0xff3e4045));
            g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (40.0f, 2.0f), 1.0f);
        }
        void mouseDown (const juce::MouseEvent&) override { if (onStart) onStart(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
        std::function<void()> onStart, onReset;
        std::function<void (int)> onDrag;
    };
    PaneDivider paneDivider;
    int editorPaneHeight = 0, mixerPaneHeight = 0, paneHeightAtDragStart = 0;   // 0: the default size
    Inspector inspector { mixer };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;
    double lastPaintedPlayhead = -1.0;

    // Settings kept between sessions (not part of any project).
    juce::ApplicationProperties appProperties;

    // Menus and key commands
    juce::ApplicationCommandManager commandManager;
    juce::String keyPreset;
    bool stopReturnsToStart = false;   // workflow: jump back to where playback started when stopping
    double playStartedAt = 0.0;
    std::unique_ptr<EditorWindow> keyCommandsWindow;

    // MIDI generator
    std::unique_ptr<EditorWindow> generatorWindow;
    generator::Settings generatorSettings;
    bool generatorUsesTempo = true;
    juce::Array<juce::ValueTree> lastGenerated;   // the tracks "Try another" replaces

    // Claude assistant
    std::unique_ptr<ai::HttpTransport> assistantTransport;
    std::unique_ptr<ai::AssistantTools> assistantTools;
    std::unique_ptr<ai::Assistant> assistant;
    std::unique_ptr<AssistantPanel> assistantPanel;
    std::unique_ptr<EditorWindow> assistantSettingsWindow;
    std::unique_ptr<juce::Thread> mixMeasureJob;

    // Several interfaces as one
    aggregate::Setup aggregateSetup;
    std::unique_ptr<EditorWindow> combineWindow;

    // Screen recording
    std::unique_ptr<ScreenRecorder> screenRecorder;
    std::unique_ptr<EditorWindow> screenRecordWindow;
    juce::Time screenRecordStartedAt;
    bool screenRecordStarting = false;
    double recordingOffsetMs = 0.0;

    // The cycle as it was when recording started (it can't change while recording).
    bool recordCycleOn = false;
    double recordCycleStart = 0.0, recordCycleEnd = 0.0;

    // Plugin and synth windows, by track id. Declared last so they close before
    // the instruments they show are destroyed.
    std::map<juce::String, std::unique_ptr<EditorWindow>> editorWindows;

    // The Performance meter (CPU and disk) and its window
    PerformanceMeter performanceMeter;
    PerformanceReading lastReading;
    std::map<juce::String, float> trackLoads;   // smoothed, by track id
    double lastPerformanceMs = 0.0;
    void updatePerformance();
    void showPerformanceWindow();
    std::unique_ptr<EditorWindow> scannerWindow;
    bool liveLoopsWasPlaying = false;

    friend class SnapshotTool;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace riff
