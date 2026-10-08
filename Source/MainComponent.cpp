#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Theme.h"
#include "ui/LatencyCalibrator.h"
#include "engine/BuiltInEffects.h"

namespace riff
{

namespace
{
    constexpr int toolbarHeight = 48;
    constexpr int headerWidth = 232;
    constexpr int scrollBarSize = 12;
    constexpr int meterWidth = 84;
    constexpr int statusHeight = 24;
}

MainComponent::MainComponent()
{
    theme::apply (getLookAndFeel());

    // --- Toolbar ----------------------------------------------------------------
    for (auto* b : { &fileButton, &mixerButton, &undoButton, &redoButton,
                     &rewindButton, &playButton, &recordButton, &clickButton, &cycleButton, &countInButton,
                     &addTrackButton, &settingsButton })
    {
        b->setWantsKeyboardFocus (false); // keep Space for play/stop
        addAndMakeVisible (b);
    }

    fileButton.onClick     = [this] { showFileMenu(); };
    mixerButton.onClick    = [this] { toggleMixer(); };
    mixerButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    undoButton.onClick     = [this] { undo(); };
    redoButton.onClick     = [this] { redo(); };
    rewindButton.onClick   = [this] { returnToStart(); };
    playButton.onClick     = [this] { togglePlay(); };
    recordButton.onClick   = [this] { toggleRecord(); };
    clickButton.onClick    = [this] { toggleMetronome(); };
    cycleButton.onClick    = [this] { toggleCycle(); };
    countInButton.onClick  = [this]
    {
        project.setProperty (ids::countIn, ! (bool) project[ids::countIn], nullptr);
    };
    cycleButton.setColour (juce::TextButton::buttonOnColourId, theme::solo);
    countInButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    recordButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
    recordButton.setColour (juce::TextButton::textColourOffId, theme::danger.brighter (0.3f));
    clickButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("Audio track", [this] { addTrack (model::audioKind); });
        menu.addItem ("Instrument track (MIDI)", [this] { addTrack (model::instrumentKind); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton));
    };
    settingsButton.onClick = [this] { showAudioMenu(); };

    fileButton.setTooltip ("New, open, save, import audio, export the song");
    mixerButton.setTooltip ("Show the mixer (X)");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    rewindButton.setTooltip ("Go to start (Return)");
    playButton.setTooltip ("Play / stop (Space)");
    recordButton.setTooltip ("Record on armed tracks (R). Arms the selected track if none are armed.");
    clickButton.setTooltip ("Metronome (K)");
    cycleButton.setTooltip ("Cycle (C). Drag in the top half of the ruler to set the range; click it to turn it on or off.\n"
                            "Recording with Cycle on makes a new take on every pass.");
    countInButton.setTooltip ("One bar of click before recording starts");
    addTrackButton.setTooltip ("New audio or instrument track");
    settingsButton.setTooltip ("Audio device settings");

    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, theme::accent);
    positionLabel.setColour (juce::Label::backgroundColourId, theme::background);
    addAndMakeVisible (positionLabel);

    tempoLabel.setEditable (false, true, false);
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    tempoLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    tempoLabel.setTooltip ("Double-click to change the tempo");
    tempoLabel.onTextChange = [this]
    {
        const auto bpm = tempoLabel.getText().getDoubleValue();
        if (bpm >= 20.0 && bpm <= 400.0)
        {
            undoManager.beginNewTransaction ("Tempo");
            project.setProperty (ids::tempo, bpm, &undoManager);
        }
        refreshProjectControls();
    };
    addAndMakeVisible (tempoLabel);

    for (auto* l : { &tempoCaption, &masterCaption })
    {
        l->setColour (juce::Label::textColourId, theme::dimText);
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (l);
    }

    masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterSlider.setRange (0.0, 1.0, 0.001);
    masterSlider.setScrollWheelEnabled (false);
    masterSlider.onDragStart = [this] { undoManager.beginNewTransaction ("Master volume"); };
    masterSlider.onValueChange = [this] { project.setProperty (ids::masterGain, masterSlider.getValue(), &undoManager); };
    addAndMakeVisible (masterSlider);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (statusLabel);

    // --- Body -------------------------------------------------------------------
    addAndMakeVisible (headers);
    addAndMakeVisible (canvas);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    // The wheel over the headers scrolls the tracks too.
    headers.onWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        canvas.mouseWheelMove (e.getEventRelativeTo (&canvas), w);
    };
    canvas.onViewChanged = [this] { updateScrollBars(); };
    canvas.onOpenPianoRoll = [this] (juce::ValueTree region) { openPianoRoll (region); };
    pianoRoll.onClose = [this] { closePianoRoll(); };
    addChildComponent (pianoRoll);
    headers.instrumentMenu = [this] (juce::ValueTree track, juce::Component& target) { showInstrumentMenu (track, target); };

    // Instruments
    rack.onInstrumentReady = [this] { player.requestRebuild(); };
    rack.onLoadError = [this] (const juce::String& trackName, const juce::String& error)
    {
        showError ("Plugin problem on \"" + trackName + "\"", error);
    };
    rack.onPluginReplaced = [this] (const juce::String& trackId) { editorWindows.erase (trackId); };

    // Effects and the mixer
    effectRack.onEffectReady = [this] { player.requestRebuild(); };
    effectRack.onLoadError = [this] (const juce::String& error) { showError ("Plugin problem", error); };
    effectRack.onEffectReplaced = [this] (const juce::String& insertId) { editorWindows.erase (insertId); };
    mixer.onOpenInsert = [this] (juce::ValueTree insert) { openInsertWindow (insert); };
    mixer.onChooseEffect = [this] (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)
    {
        showEffectMenu (owner, slot, existing, target);
    };
    mixer.onClose = [this] { closeMixer(); };
    addChildComponent (mixer);
    headers.inputNames = [this] { return engine.getActiveInputChannelNames(); };
    waveforms.onChange = [this] { canvas.repaint(); };

    timeline.addChangeListener (this);
    undoManager.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

    // --- Settings ---------------------------------------------------------------
    juce::PropertiesFile::Options settingsOptions;
    settingsOptions.applicationName = "Riff";
    settingsOptions.filenameSuffix = "settings";
    settingsOptions.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (settingsOptions);
    recordingOffsetMs = appProperties.getUserSettings()->getDoubleValue ("recordingOffsetMs", 0.0);

    // The list of plugins found by earlier scans.
    if (auto xml = appProperties.getUserSettings()->getXmlValue ("knownPlugins"))
        rack.getKnownPlugins().recreateFromXml (*xml);

    // --- Start ------------------------------------------------------------------
    startupError = engine.initialise();
    setProject (model::createProject(), {});
    statusLabel.setText (deviceStatusText(), juce::dontSendNotification);

    setWantsKeyboardFocus (true);
    setSize (1280, 760);
    startTimerHz (30);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArrangeCanvas> (&canvas)]
    {
        if (safe != nullptr)
            safe->grabKeyboardFocus();
    });

    if (startupError.isNotEmpty())
        showError ("Audio device problem", startupError);
}

MainComponent::~MainComponent()
{
    editorWindows.clear();
    scannerWindow.reset();

    // Stop the audio and drop the playback graph first, so plugins are released
    // while the plugin host that loaded them still exists.
    engine.getDeviceManager().closeAudioDevice();
    engine.setGraph (std::make_unique<PlaybackGraph>());

    if (auto xml = rack.getKnownPlugins().createXml())
        appProperties.getUserSettings()->setValue ("knownPlugins", xml.get());
    appProperties.saveIfNeeded();

    stopTimer();
    engine.stop();
    engine.getDeviceManager().removeChangeListener (this);
    undoManager.removeChangeListener (this);
    timeline.removeChangeListener (this);
    project.removeListener (this);
}

//==============================================================================
// Layout
//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::toolbar);
    g.fillRect (getLocalBounds().removeFromTop (toolbarHeight));
    g.setColour (theme::outline);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Toolbar
    auto bar = area.removeFromTop (toolbarHeight).reduced (10, 9);
    auto place = [&bar] (juce::Component& c, int width, int gapAfter = 4)
    {
        c.setBounds (bar.removeFromLeft (width));
        bar.removeFromLeft (gapAfter);
    };

    place (fileButton, 52, 12);
    place (undoButton, 50);
    place (redoButton, 50, 12);
    place (rewindButton, 30);
    place (playButton, 52);
    place (recordButton, 42, 8);
    place (cycleButton, 52, 8);
    place (positionLabel, 184, 8);
    place (tempoLabel, 44, 2);
    place (tempoCaption, 30, 8);
    place (countInButton, 54);
    place (clickButton, 48, 12);

    auto placeRight = [&bar] (juce::Component& c, int width, int gapBefore = 4)
    {
        c.setBounds (bar.removeFromRight (width));
        bar.removeFromRight (gapBefore);
    };

    placeRight (settingsButton, 56, 12);
    placeRight (masterSlider, 84, 2);
    placeRight (masterCaption, 44, 12);
    placeRight (mixerButton, 60);
    placeRight (addTrackButton, 64);

    // Status line
    statusLabel.setBounds (area.removeFromBottom (statusHeight).reduced (10, 0));

    if (pianoRoll.isVisible())
        pianoRoll.setBounds (area.removeFromBottom (juce::jlimit (200, 360, getHeight() / 3)));
    else if (mixer.isVisible())
        mixer.setBounds (area.removeFromBottom (juce::jlimit (300, 420, getHeight() / 2)));

    // Meters on the right
    auto meters = area.removeFromRight (meterWidth).reduced (8, 10);
    inputMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2 - 2));
    meters.removeFromLeft (4);
    outputMeter.setBounds (meters);

    // Headers | tracks area + scroll bars
    headers.setBounds (area.removeFromLeft (headerWidth).withTrimmedBottom (scrollBarSize));
    vScroll.setBounds (area.removeFromRight (scrollBarSize).withTrimmedTop (TimelineState::rulerHeight).withTrimmedBottom (scrollBarSize));
    hScroll.setBounds (area.removeFromBottom (scrollBarSize));
    canvas.setBounds (area);

    updateScrollBars();
}

void MainComponent::updateScrollBars()
{
    const auto visibleSeconds = canvas.getWidth() / timeline.pixelsPerSecond;
    const auto totalSeconds = juce::jmax (model::projectEnd (project) + 30.0, timeline.scrollSeconds + visibleSeconds);
    hScroll.setRangeLimits (0.0, totalSeconds, juce::dontSendNotification);
    hScroll.setCurrentRange (timeline.scrollSeconds, visibleSeconds, juce::dontSendNotification);

    const auto visibleHeight = canvas.getHeight() - TimelineState::rulerHeight;
    const auto contentHeight = juce::jmax ((model::numTracks (project) + 1) * TimelineState::trackHeight,
                                           timeline.scrollY + visibleHeight);
    vScroll.setRangeLimits (0.0, contentHeight, juce::dontSendNotification);
    vScroll.setCurrentRange (timeline.scrollY, visibleHeight, juce::dontSendNotification);
}

void MainComponent::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &hScroll)
        timeline.scrollSeconds = juce::jmax (0.0, newRangeStart);
    else
        timeline.scrollY = juce::jmax (0, juce::roundToInt (newRangeStart));

    timeline.changed();
}

//==============================================================================
// Keyboard
//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shiftCmd = cmd | juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress::spaceKey)                               { togglePlay(); return true; }
    if (key == juce::KeyPress ('r', 0, 0) || key == juce::KeyPress ('R', 0, 0)) { toggleRecord(); return true; }
    if (key == juce::KeyPress ('k', 0, 0) || key == juce::KeyPress ('K', 0, 0)) { toggleMetronome(); return true; }
    if (key == juce::KeyPress ('c', 0, 0) || key == juce::KeyPress ('C', 0, 0)) { toggleCycle(); return true; }
    if (key == juce::KeyPress ('x', 0, 0) || key == juce::KeyPress ('X', 0, 0)) { toggleMixer(); return true; }
    if (key == juce::KeyPress ('e', cmd, 0))                           { exportSong(); return true; }
    if (key == juce::KeyPress::returnKey)                              { returnToStart(); return true; }
    if (key == juce::KeyPress ('z', cmd, 0))                           { undo(); return true; }
    if (key == juce::KeyPress ('z', shiftCmd, 0)
        || key == juce::KeyPress ('y', cmd, 0))                        { redo(); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))                           { saveProject(); return true; }
    if (key == juce::KeyPress ('s', shiftCmd, 0))                      { saveProjectAs(); return true; }
    if (key == juce::KeyPress ('o', cmd, 0))                           { openProject(); return true; }
    if (key == juce::KeyPress ('n', cmd, 0))                           { newProject(); return true; }
    if (key == juce::KeyPress ('i', cmd, 0))                           { importAudio(); return true; }
    if (key == juce::KeyPress ('t', cmd, 0))                           { splitAtPlayhead(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }

    // Cmd+Left / Cmd+Right zoom, as in Logic.
    if (key == juce::KeyPress (juce::KeyPress::rightKey, cmd, 0) || key == juce::KeyPress (juce::KeyPress::leftKey, cmd, 0))
    {
        const auto factor = key.getKeyCode() == juce::KeyPress::rightKey ? 1.5 : 1.0 / 1.5;
        const auto x = juce::jlimit (0.0f, (float) canvas.getWidth(), timeline.timeToX (engine.getPlayheadSeconds()));
        const auto timeAtX = timeline.xToTime (x);
        timeline.pixelsPerSecond = juce::jlimit (TimelineState::minPixelsPerSecond, TimelineState::maxPixelsPerSecond,
                                                 timeline.pixelsPerSecond * factor);
        timeline.scrollSeconds = juce::jmax (0.0, timeAtX - (double) x / timeline.pixelsPerSecond);
        timeline.changed();
        return true;
    }

    return false;
}

//==============================================================================
// Project lifecycle
//==============================================================================
void MainComponent::setProject (juce::ValueTree newProject, const juce::File& file)
{
    if (engine.isRecording())
        finishRecording();

    closePianoRoll();
    editorWindows.clear();
    engine.stop();
    engine.setPlayheadSeconds (0.0);

    project.removeListener (this);
    project = newProject;
    projectFile = file;
    project.addListener (this);

    undoManager.clearUndoHistory();

    timeline.selectedRegionId = {};
    timeline.selectedTrackId = {};
    timeline.scrollSeconds = 0.0;
    timeline.scrollY = 0;

    player.setProject (project);
    headers.setProject (project);
    mixer.setProject (project);
    canvas.setProject (project);

    dirty = false;
    refreshProjectControls();
    updateTitle();
    timeline.changed();
}

void MainComponent::confirmDiscardChanges (std::function<void()> then)
{
    if (! dirty)
    {
        then();
        return;
    }

    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::QuestionIcon,
        "Save changes?",
        "Do you want to save the changes to \"" + project[ids::name].toString() + "\"?",
        "Save", "Don't Save", "Cancel", this);

    messageBox = juce::NativeMessageBox::showScopedAsync (options, [this, then] (int result)
    {
        if (result == 1)       saveProject (then);   // Save
        else if (result == 2)  then();               // Don't Save
    });                                              // 0: Cancel
}

void MainComponent::newProject()
{
    confirmDiscardChanges ([this] { setProject (model::createProject(), {}); });
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open a Riff project",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           "*" + model::fileExtension);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (! file.existsAsFile())
                return;

            juce::String error;
            auto loaded = model::load (file, error);

            if (! loaded.isValid())
            {
                showError ("Couldn't open project", error);
                return;
            }

            loaded.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
            setProject (loaded, file);
        });
    });
}

void MainComponent::saveProject (std::function<void()> then)
{
    if (projectFile == juce::File())
    {
        saveProjectAs (std::move (then));
        return;
    }

    // Plugins keep their own settings; copy them into the project first.
    rack.storePluginStates (project);
    effectRack.storePluginStates (model::mixerOf (project));

    juce::String error;
    if (! model::save (project, projectFile, error))
    {
        showError ("Couldn't save", error);
        return;
    }

    dirty = false;
    updateTitle();

    if (then)
        then();
}

void MainComponent::saveProjectAs (std::function<void()> then)
{
    const auto suggested = (projectFile != juce::File() ? projectFile.getParentDirectory()
                                                        : juce::File::getSpecialLocation (juce::File::userMusicDirectory))
                               .getChildFile (project[ids::name].toString() + model::fileExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Save Riff project", suggested, "*" + model::fileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, then] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return; // cancelled

        file = file.withFileExtension (model::fileExtension);
        projectFile = file;

        project.removeListener (this); // renaming isn't an edit
        project.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
        project.addListener (this);

        saveProject (then);
    });
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    messageBox = juce::AlertWindow::showScopedAsync (
        juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this),
        nullptr);
}

void MainComponent::markDirty()
{
    if (! dirty)
    {
        dirty = true;
        updateTitle();
    }
    updateScrollBars();
}

void MainComponent::updateTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (project[ids::name].toString() + (dirty ? " (edited)" : "") + " - Riff");
}

void MainComponent::refreshProjectControls()
{
    const double bpm = project.getProperty (ids::tempo, 120.0);
    tempoLabel.setText (std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String ((int) bpm) : juce::String (bpm, 1),
                        juce::dontSendNotification);
    masterSlider.setValue (project.getProperty (ids::masterGain, 0.8), juce::dontSendNotification);
    canvas.repaint();
}

//==============================================================================
// Editing
//==============================================================================
int MainComponent::selectedTrackIndex() const
{
    const auto track = model::findById (project, timeline.selectedTrackId);
    return track.isValid() ? project.indexOf (track) : -1;
}

void MainComponent::addTrack (const juce::String& kind)
{
    const bool instrument = kind == model::instrumentKind;
    undoManager.beginNewTransaction ("New track");
    auto track = model::addTrack (project, (instrument ? "Inst " : "Audio ") + juce::String (model::numTracks (project) + 1),
                                  &undoManager, -1, kind);
    timeline.selectedTrackId = track[ids::id].toString();
    timeline.changed();
}

void MainComponent::addTrack()
{
    undoManager.beginNewTransaction ("New track");
    auto track = model::addTrack (project, "Audio " + juce::String (model::numTracks (project) + 1), &undoManager);
    timeline.selectedTrackId = track[ids::id].toString();
    timeline.changed();
}

void MainComponent::importAudio()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import audio",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       engine.getFormatManager().getWildcardForAllFormats());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& chooser)
    {
        juce::StringArray paths;
        for (const auto& f : chooser.getResults())
            paths.add (f.getFullPathName());

        if (! paths.isEmpty())
            canvas.importFiles (paths, selectedTrackIndex(), canvas.snap (engine.getPlayheadSeconds()));
    });
}

void MainComponent::splitAtPlayhead()
{
    const auto time = engine.getPlayheadSeconds();
    undoManager.beginNewTransaction ("Split");

    // The selected region if it's under the playhead; otherwise every region
    // under the playhead on the selected track.
    auto selected = model::findById (project, timeline.selectedRegionId);

    if (selected.isValid() && (double) selected[ids::start] < time && time < model::regionEnd (selected))
    {
        model::splitRegion (selected, time, &undoManager);
        return;
    }

    auto track = model::findById (project, timeline.selectedTrackId);
    if (! track.isValid())
        return;

    juce::Array<juce::ValueTree> toSplit;
    for (const auto& region : track)
        toSplit.add (region);

    for (auto& region : toSplit)
        model::splitRegion (region, time, &undoManager);
}

void MainComponent::deleteSelection()
{
    auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid())
        return;

    undoManager.beginNewTransaction ("Delete region");
    model::deleteRegion (region, &undoManager);
    timeline.selectedRegionId = {};
    timeline.changed();
}

void MainComponent::undo() { undoManager.undo(); }
void MainComponent::redo() { undoManager.redo(); }

//==============================================================================
// Transport
//==============================================================================
void MainComponent::togglePlay()
{
    if (engine.isRecording())
        finishRecording();

    if (engine.isPlaying())
    {
        engine.stop();
        return;
    }

    // With Cycle on, playback starts inside the cycle.
    if ((bool) project[ids::cycleEnabled])
    {
        const double start = project[ids::cycleStart], end = project[ids::cycleEnd];
        const auto now = engine.getPlayheadSeconds();
        if (now < start || now >= end)
            engine.setPlayheadSeconds (start);
    }

    engine.play();
}

void MainComponent::toggleCycle()
{
    if (engine.isRecording())
        return; // the cycle is locked while recording

    project.setProperty (ids::cycleEnabled, ! (bool) project[ids::cycleEnabled], nullptr);
}

void MainComponent::toggleMetronome()
{
    // A live setting rather than an edit, so it isn't undoable.
    project.setProperty (ids::metronome, ! (bool) project[ids::metronome], nullptr);
}

void MainComponent::toggleRecord()
{
    if (engine.isRecording())
        finishRecording(); // punch out; playback carries on
    else
        beginRecording();
}

void MainComponent::beginRecording()
{
    const auto numInputs = engine.getNumActiveInputChannels();

    juce::Array<juce::ValueTree> armed;
    for (const auto& track : project)
        if ((bool) track[ids::armed])
            armed.add (track);

    // Nothing armed: arm the selected track, or make a new one.
    if (armed.isEmpty())
    {
        auto track = model::findById (project, timeline.selectedTrackId);

        if (! track.isValid())
        {
            undoManager.beginNewTransaction ("New track");
            track = model::addTrack (project, "Audio " + juce::String (model::numTracks (project) + 1), &undoManager);
            timeline.selectedTrackId = track[ids::id].toString();
            timeline.changed();
        }

        track.setProperty (ids::armed, true, nullptr);
        armed.add (track);
    }

    std::vector<AudioEngine::RecordRequest> requests;

    for (const auto& track : armed)
    {
        // Instrument tracks record what you play on a MIDI keyboard.
        if (model::isInstrumentTrack (track))
        {
            AudioEngine::RecordRequest request;
            request.trackId = track[ids::id].toString();
            request.midi = true;
            requests.push_back (request);
            continue;
        }

        if (numInputs == 0)
        {
            showError ("No audio input",
                       "Riff can't hear any inputs. Click Audio to choose an input device.\n\n"
                       "On a Mac, also check System Settings > Privacy & Security > Microphone and allow Riff.");
            return;
        }

        const int first = track.getProperty (ids::input, 0);
        const int channels = (bool) track.getProperty (ids::inputStereo, false) ? 2 : 1;

        if (first + channels > numInputs)
        {
            showError ("Input not available",
                       "\"" + track[ids::name].toString() + "\" is set to record from an input that isn't active. "
                       "Right-click its R button to pick another input, or enable more inputs under Audio.");
            return;
        }

        requests.push_back ({ track[ids::id].toString(), nextTakeFile (track), first, channels, false });
    }

    // Where recording begins: right here when punching in during playback;
    // otherwise at the playhead, or at the cycle start when Cycle is on.
    const auto rate = engine.getSampleRate();
    recordCycleOn = project[ids::cycleEnabled];
    recordCycleStart = project[ids::cycleStart];
    recordCycleEnd = project[ids::cycleEnd];

    const bool wasPlaying = engine.isPlaying();
    auto start = engine.getPlayheadSeconds();

    if (! wasPlaying && recordCycleOn && (start < recordCycleStart || start >= recordCycleEnd))
        start = recordCycleStart;

    juce::String error;
    if (! engine.startRecording (requests, error, (juce::int64) std::llround (start * rate)))
    {
        showError ("Couldn't start recording", error);
        return;
    }

    if (! wasPlaying)
    {
        const bool countIn = project[ids::countIn];
        const auto barLength = model::secondsPerBeat (project) * (int) project.getProperty (ids::timeSigNumerator, 4);
        engine.setPlayheadSecondsAllowingNegative (countIn ? start - barLength : start);
        engine.play();
    }
}

void MainComponent::finishRecording()
{
    const auto takes = engine.stopRecording();
    if (takes.empty())
        return;

    const auto rate = engine.getSampleRate();
    const auto latency = engine.getRoundTripLatencySamples() / rate;

    undoManager.beginNewTransaction ("Record");
    juce::ValueTree last;
    const auto totalLatency = latency + recordingOffsetMs / 1000.0;
    const auto cycleLength = recordCycleEnd - recordCycleStart;

    for (const auto& take : takes)
    {
        auto track = model::findById (project, take.trackId);
        if (! track.isValid())
            continue; // the track was deleted while recording; the file stays on disk

        if (take.isMidi)
        {
            createMidiRegionFromTake (track, take, last);
            continue;
        }

        const auto pieces = model::splitRecording ((double) take.startSample / rate, (double) take.numSamples / rate,
                                                   totalLatency, recordCycleOn, recordCycleStart, recordCycleEnd);
        if (pieces.empty())
            continue;

        // One pass: an ordinary region.
        if (pieces.size() == 1)
        {
            const auto& p = pieces.front();
            auto region = model::addRegion (track, take.file, p.timelineStart, p.length, &undoManager);
            region.setProperty (ids::offset, p.fileOffset, &undoManager);
            last = region;
            continue;
        }

        // Several passes over the cycle: whole passes become takes in one take
        // folder. A partial first pass (punched in mid-cycle) stays its own region,
        // and a last pass shorter than a quarter of the cycle is dropped.
        std::vector<double> takeOffsets;

        for (size_t i = 0; i < pieces.size(); ++i)
        {
            const auto& p = pieces[i];
            const bool startsAtCycle = std::abs (p.timelineStart - recordCycleStart) < 1e-6;
            const bool isLast = i == pieces.size() - 1;

            if (! startsAtCycle)
            {
                auto region = model::addRegion (track, take.file, p.timelineStart, p.length, &undoManager);
                region.setProperty (ids::offset, p.fileOffset, &undoManager);
                last = region;
            }
            else if (! isLast || p.length >= cycleLength * 0.25)
            {
                takeOffsets.push_back (p.fileOffset);
            }
        }

        if (takeOffsets.size() == 1)
        {
            auto region = model::addRegion (track, take.file, recordCycleStart, cycleLength, &undoManager);
            region.setProperty (ids::offset, takeOffsets.front(), &undoManager);
            last = region;
        }
        else if (takeOffsets.size() > 1)
        {
            // The newest take plays, as in Logic.
            last = model::addTakeFolder (track, take.file, recordCycleStart, cycleLength, takeOffsets,
                                         (int) takeOffsets.size() - 1, &undoManager);
        }
    }

    if (last.isValid())
    {
        timeline.selectedRegionId = last[ids::id].toString();
        timeline.changed();
    }
}

juce::File MainComponent::recordingFolder() const
{
    // Saved projects record next to the project file; unsaved ones go to Music/Riff/Recordings.
    if (projectFile != juce::File())
        return projectFile.getParentDirectory().getChildFile ("Audio Files");

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Riff").getChildFile ("Recordings")
               .getChildFile (juce::File::createLegalFileName (project[ids::name].toString()));
}

juce::File MainComponent::nextTakeFile (const juce::ValueTree& track) const
{
    // Logic-style names: "Vocals#01.wav", "Vocals#02.wav", ...
    const auto folder = recordingFolder();
    const auto base = juce::File::createLegalFileName (track[ids::name].toString());

    for (int n = 1; n < 10000; ++n)
    {
        auto f = folder.getChildFile (base + "#" + juce::String (n).paddedLeft ('0', 2) + ".wav");
        if (! f.exists())
            return f;
    }

    return folder.getNonexistentChildFile (base, ".wav");
}

void MainComponent::returnToStart()
{
    engine.setPlayheadSeconds (0.0);
    timeline.scrollSeconds = 0.0;
    timeline.changed();
}

//==============================================================================
// Listeners and timer
//==============================================================================
void MainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == project && (property == ids::tempo || property == ids::masterGain))
        refreshProjectControls();

    markDirty();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &timeline)
    {
        updateScrollBars();
    }
    else if (source == &undoManager)
    {
        undoButton.setEnabled (undoManager.canUndo());
        redoButton.setEnabled (undoManager.canRedo());
        undoButton.setTooltip (undoManager.canUndo() ? "Undo " + undoManager.getUndoDescription() : "Nothing to undo");
        redoButton.setTooltip (undoManager.canRedo() ? "Redo " + undoManager.getRedoDescription() : "Nothing to redo");
    }
    else
    {
        statusLabel.setText (deviceStatusText(), juce::dontSendNotification);
    }
}

void MainComponent::timerCallback()
{
    updateLiveMidiTrack();

    if (pianoRoll.isVisible())
        pianoRoll.repaint();

    const auto levels = engine.takeLevels();
    inputMeter.push (levels.inL, levels.inR);
    outputMeter.push (levels.outL, levels.outR);
    mixer.setMasterLevels (levels.outL, levels.outR);
    mixerButton.setToggleState (mixer.isVisible(), juce::dontSendNotification);

    const auto playhead = engine.getPlayheadSeconds();
    positionLabel.setText (formatPosition (playhead), juce::dontSendNotification);

    const bool playing = engine.isPlaying();
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);
    recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
    clickButton.setToggleState ((bool) project[ids::metronome], juce::dontSendNotification);
    cycleButton.setToggleState ((bool) project[ids::cycleEnabled], juce::dontSendNotification);
    countInButton.setToggleState ((bool) project[ids::countIn], juce::dontSendNotification);

    if (engine.takeRecordingOverflow())
        showError ("Recording glitch", "The disk couldn't keep up, so a moment of audio was lost. "
                                       "Try a larger buffer size under Audio, or record to a faster drive.");

    // Follow the playhead: page forward when it reaches the right edge.
    if (playing)
    {
        const auto x = timeline.timeToX (playhead);
        if (x > (float) canvas.getWidth() - 20.0f || x < 0.0f)
        {
            timeline.scrollSeconds = juce::jmax (0.0, playhead - 0.05 * canvas.getWidth() / timeline.pixelsPerSecond);
            timeline.changed();
        }
    }

    if (! juce::exactlyEqual (playhead, lastPaintedPlayhead))
    {
        lastPaintedPlayhead = playhead;
        canvas.repaint();
    }
}

juce::String MainComponent::formatPosition (double seconds) const
{
    // Bars.Beats.16ths (from the tempo) and minutes:seconds
    const auto beatLength = model::secondsPerBeat (project);
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);

    if (seconds < 0.0) // count-in: show the beats left
        return "Count-in   " + juce::String ((int) std::ceil (-seconds / beatLength - 1e-9));

    const auto beats = seconds / beatLength;

    const auto bar = (int) std::floor (beats / beatsPerBar) + 1;
    const auto beat = (int) std::floor (beats) % beatsPerBar + 1;
    const auto sixteenth = (int) std::floor ((beats - std::floor (beats)) * 4.0) + 1;

    const auto totalMs = (juce::int64) std::llround (seconds * 1000.0);
    return juce::String::formatted ("%3d.%d.%d   %02d:%02d.%03d", bar, beat, sixteenth,
                                    (int) (totalMs / 60000), (int) (totalMs / 1000) % 60, (int) (totalMs % 1000));
}

juce::String MainComponent::deviceStatusText() const
{
    auto* device = const_cast<AudioEngine&> (engine).getDeviceManager().getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device open - click Audio to choose one";

    const auto sr = device->getCurrentSampleRate();
    const auto bs = device->getCurrentBufferSizeSamples();
    const auto latencyMs = 1000.0 * (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + 2 * bs) / sr;

    return device->getName() + "  |  " + juce::String (sr / 1000.0, 1) + " kHz  |  buffer " + juce::String (bs)
         + "  |  ~" + juce::String (latencyMs, 1) + " ms round trip"
         + "        Drag regions to move (hold Cmd to skip the grid), drag edges to trim, Cmd+scroll to zoom";
}

//==============================================================================
// Instruments and MIDI
//==============================================================================
void MainComponent::createMidiRegionFromTake (juce::ValueTree track, const AudioEngine::FinishedTake& take, juce::ValueTree& last)
{
    const auto rate = engine.getSampleRate();
    const auto start = (double) take.startSample / rate;
    const auto length = (double) take.numSamples / rate;

    // With Cycle on, passes overlap and their notes merge into one region over the cycle.
    const bool wrapped = recordCycleOn && start < recordCycleEnd && start + length > recordCycleEnd;
    const auto regionStart = wrapped ? juce::jmin (start, recordCycleStart) : start;
    const auto regionEnd = wrapped ? recordCycleEnd : start + length;

    std::vector<model::RecordedMidiEvent> events;
    for (const auto& e : take.midiEvents)
        events.push_back ({ (double) e.sample / rate, e.status, e.data1, e.data2 });

    const auto notes = model::notesFromEvents (events, regionStart, regionEnd);
    if (notes.empty())
        return;

    auto region = model::addMidiRegion (track, regionStart, regionEnd - regionStart, &undoManager);
    for (const auto& n : notes)
        model::addNote (region, n.pitch, n.start, n.length, n.velocity, &undoManager);

    last = region;
}

void MainComponent::updateLiveMidiTrack()
{
    // Your MIDI keyboard plays the first armed instrument track, otherwise the
    // selected one (or the one open in the piano roll).
    juce::String target;

    for (const auto& track : project)
        if (model::isInstrumentTrack (track) && (bool) track[ids::armed])
        {
            target = track[ids::id].toString();
            break;
        }

    if (target.isEmpty())
    {
        const auto selected = model::findById (project, timeline.selectedTrackId);
        if (model::isInstrumentTrack (selected))
            target = selected[ids::id].toString();
        else if (pianoRoll.isVisible() && pianoRoll.getRegion().isValid())
            target = pianoRoll.getRegion().getParent()[ids::id].toString();
    }

    player.setLiveMidiTrack (target);
}

void MainComponent::openPianoRoll (juce::ValueTree region)
{
    timeline.selectedRegionId = region[ids::id].toString();
    timeline.selectedTrackId = region.getParent()[ids::id].toString();
    timeline.changed();

    mixer.setVisible (false);
    pianoRoll.setVisible (true);
    resized();
    pianoRoll.setRegion (region);
    pianoRoll.grabKeyboardFocus();
}

void MainComponent::closePianoRoll()
{
    if (! pianoRoll.isVisible())
        return;

    pianoRoll.setRegion ({});
    pianoRoll.setVisible (false);
    resized();
    canvas.grabKeyboardFocus();
}

void MainComponent::showInstrumentMenu (juce::ValueTree track, juce::Component& target)
{
    const bool isPlugin = track[ids::instrument].toString() == "plugin";
    const auto currentDescription = track[ids::pluginDescription].toString();

    juce::PopupMenu menu;
    menu.addItem (isPlugin ? "Show plugin window" : "Show synth controls", [this, track] { openInstrumentWindow (track); });
    menu.addSeparator();

    menu.addItem ("Riff Synth", true, ! isPlugin, [this, track]() mutable
    {
        undoManager.beginNewTransaction ("Choose instrument");
        track.setProperty (ids::instrument, "synth", &undoManager);
    });

    // Instrument plugins found by the scanner, grouped by maker.
    std::map<juce::String, juce::PopupMenu> byMaker;
    int numInstruments = 0;

    for (const auto& type : rack.getKnownPlugins().getTypes())
    {
        if (! type.isInstrument)
            continue;

        ++numInstruments;
        const auto xml = type.createXml()->toString (juce::XmlElement::TextFormat().singleLine());
        const auto label = type.name + "  (" + type.pluginFormatName + ")";

        byMaker[type.manufacturerName.isNotEmpty() ? type.manufacturerName : "Other"]
            .addItem (label, true, isPlugin && xml == currentDescription, [this, track, xml]() mutable
            {
                undoManager.beginNewTransaction ("Choose instrument");
                track.setProperty (ids::instrument, "plugin", &undoManager);
                track.setProperty (ids::pluginDescription, xml, &undoManager);
                track.setProperty (ids::pluginState, juce::String(), &undoManager);

                // Open the plugin's window once it has loaded.
                juce::Timer::callAfterDelay (600, [safe = juce::Component::SafePointer<MainComponent> (this), track]
                {
                    if (safe != nullptr)
                        safe->openInstrumentWindow (track);
                });
            });
    }

    if (numInstruments > 0)
    {
        juce::PopupMenu plugins;
        for (auto& [maker, sub] : byMaker)
            plugins.addSubMenu (maker, sub);
        menu.addSubMenu ("Plugins", plugins);
    }
    else
    {
        menu.addItem ("No plugins yet. Scan for them first.", false, false, [] {});
    }

    menu.addSeparator();
    menu.addItem ("Scan for plugins...", [this] { showPluginScanner(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

void MainComponent::openInstrumentWindow (const juce::ValueTree& track)
{
    const auto id = track[ids::id].toString();

    if (auto it = editorWindows.find (id); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    auto closer = [this, id]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), id]
        {
            if (safe != nullptr)
                safe->editorWindows.erase (id);
        });
    };

    const auto title = track[ids::name].toString();

    if (track[ids::instrument].toString() == "plugin")
    {
        auto* plugin = rack.getPlugin (id);
        if (plugin == nullptr)
        {
            showError ("Plugin not loaded yet", "The plugin is still loading, or it failed to load. Try again in a moment.");
            return;
        }

        if (auto* editor = plugin->createEditorAndMakeActive())
            editorWindows[id] = std::make_unique<EditorWindow> (title + ": " + plugin->getName(), editor,
                                                                editor->isResizable(), closer);
        else
            showError ("No window", plugin->getName() + " doesn't have its own window.");
        return;
    }

    editorWindows[id] = std::make_unique<EditorWindow> (title + ": Riff Synth", new SynthPanel (track, undoManager), false, closer);
}

void MainComponent::showPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    // If a plugin crashes Riff while scanning, it's listed in this file and skipped next time.
    const auto deadMansPedal = appProperties.getUserSettings()->getFile().getSiblingFile ("PluginScanCrashes.txt");

    auto* list = new juce::PluginListComponent (rack.getFormatManager(), rack.getKnownPlugins(),
                                                deadMansPedal, appProperties.getUserSettings(), true);
    list->setSize (760, 520);

    // Closing the window saves the list for next time.
    scannerWindow = std::make_unique<EditorWindow> ("Plugins (use Options to scan)", list, true, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            if (auto xml = safe->rack.getKnownPlugins().createXml())
                safe->appProperties.getUserSettings()->setValue ("knownPlugins", xml.get());
            safe->appProperties.saveIfNeeded();
            safe->scannerWindow.reset();
        });
    });
}

//==============================================================================
// Mixer and effects
//==============================================================================
void MainComponent::showMixer()
{
    closePianoRoll();
    mixer.setVisible (true);
    resized();
}

void MainComponent::closeMixer()
{
    mixer.setVisible (false);
    resized();
    canvas.grabKeyboardFocus();
}

void MainComponent::toggleMixer()
{
    if (mixer.isVisible())
        closeMixer();
    else
        showMixer();
}

void MainComponent::showEffectMenu (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)
{
    // Puts the chosen effect in the slot (replacing what's there), as one undo step.
    auto place = [this, owner, slot, existing] (std::function<juce::ValueTree (juce::ValueTree chain, int index)> make)
    {
        undoManager.beginNewTransaction (existing.isValid() ? "Replace effect" : "Add effect");
        auto chain = model::chainFor (project, owner);
        auto index = existing.isValid() ? chain.indexOf (existing) : juce::jmin (slot, chain.getNumChildren());

        if (existing.isValid())
            model::removeInsert (existing, &undoManager);

        auto insert = make (chain, index);

        // Open its window (plugins need a moment to load).
        juce::Timer::callAfterDelay (insert[ids::kind].toString() == model::pluginKind ? 600 : 50,
                                     [safe = juce::Component::SafePointer<MainComponent> (this), insert]
        {
            if (safe != nullptr && insert.getParent().isValid())
                safe->openInsertWindow (insert);
        });
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Riff effects");

    for (const auto& info : builtInEffects())
        menu.addItem (info.name, [this, place, type = info.type]
        {
            place ([this, type] (juce::ValueTree chain, int index)
                   { return model::addBuiltinInsert (chain, type, index, &undoManager); });
        });

    std::map<juce::String, juce::PopupMenu> byMaker;
    int numEffects = 0;

    for (const auto& type : rack.getKnownPlugins().getTypes())
    {
        if (type.isInstrument)
            continue;

        ++numEffects;
        const auto xml = type.createXml()->toString (juce::XmlElement::TextFormat().singleLine());
        byMaker[type.manufacturerName.isNotEmpty() ? type.manufacturerName : "Other"]
            .addItem (type.name + "  (" + type.pluginFormatName + ")", [this, place, xml]
            {
                place ([this, xml] (juce::ValueTree chain, int index)
                       { return model::addPluginInsert (chain, xml, index, &undoManager); });
            });
    }

    menu.addSeparator();

    if (numEffects > 0)
    {
        juce::PopupMenu plugins;
        for (auto& [maker, sub] : byMaker)
            plugins.addSubMenu (maker, sub);
        menu.addSubMenu ("Plugins", plugins);
    }
    else
    {
        menu.addItem ("No plugins yet. Scan for them first.", false, false, [] {});
    }

    menu.addItem ("Scan for plugins...", [this] { showPluginScanner(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

void MainComponent::openInsertWindow (const juce::ValueTree& insert)
{
    const auto id = insert[ids::id].toString();

    if (auto it = editorWindows.find (id); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    auto closer = [this, id]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), id]
        {
            if (safe != nullptr)
                safe->editorWindows.erase (id);
        });
    };

    // Title: "<track name>: <effect>" (or "Master: ...").
    const auto owner = insert.getParent()[ids::owner].toString();
    const auto track = model::findById (project, owner);
    const auto where = track.isValid() ? track[ids::name].toString() : juce::String ("Master");

    if (insert[ids::kind].toString() == model::pluginKind)
    {
        auto* plugin = effectRack.getPlugin (id);
        if (plugin == nullptr)
        {
            showError ("Plugin not loaded yet", "The plugin is still loading, or it failed to load. Try again in a moment.");
            return;
        }

        if (auto* editor = plugin->createEditorAndMakeActive())
            editorWindows[id] = std::make_unique<EditorWindow> (where + ": " + plugin->getName(), editor,
                                                                editor->isResizable(), closer);
        else
            showError ("No window", plugin->getName() + " doesn't have its own window.");
        return;
    }

    const auto* info = findBuiltInEffect (insert[ids::effect].toString());
    editorWindows[id] = std::make_unique<EditorWindow> (where + ": " + (info != nullptr ? info->name : juce::String ("Effect")),
                                                        new EffectPanel (insert, undoManager), false, closer);
}

//==============================================================================
// File menu and export
//==============================================================================
void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("New", [this] { newProject(); });
    menu.addItem ("Open...", [this] { openProject(); });
    menu.addItem ("Save", [this] { saveProject(); });
    menu.addItem ("Save As...", [this] { saveProjectAs(); });
    menu.addSeparator();
    menu.addItem ("Import Audio...", [this] { importAudio(); });
    menu.addItem ("Export Song as WAV...", [this] { exportSong(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton));
}

namespace
{
    /** Renders the song on a background thread with a progress window. */
    class ExportJob final : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (AudioEngine& e, juce::File f, double s, double en, std::function<void (const juce::String&)> done)
            : ThreadWithProgressWindow ("Exporting " + f.getFileName(), true, true),
              engine (e), file (std::move (f)), start (s), end (en), onDone (std::move (done)) {}

        void run() override
        {
            error = engine.renderOffline (file, start, end, 24, [this] (double p)
            {
                setProgress (p);
                return ! threadShouldExit();
            });
        }

        void threadComplete (bool userPressedCancel) override
        {
            engine.endOfflineRender();
            onDone (userPressedCancel ? juce::String ("cancelled") : error);
            delete this;
        }

    private:
        AudioEngine& engine;
        juce::File file;
        double start, end;
        std::function<void (const juce::String&)> onDone;
        juce::String error;
    };
}

void MainComponent::exportSong()
{
    if (engine.isRecording())
        return;

    // With Cycle on, export the cycle; otherwise the whole song plus two seconds for tails.
    const bool cycle = project[ids::cycleEnabled];
    const double start = cycle ? (double) project[ids::cycleStart] : 0.0;
    const double end = cycle ? (double) project[ids::cycleEnd] : model::projectEnd (project) + 2.0;

    if (! cycle && model::projectEnd (project) <= 0.0)
    {
        showError ("Nothing to export", "Record or import something first.");
        return;
    }

    const auto folder = projectFile != juce::File() ? projectFile.getParentDirectory()
                                                    : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Export song", folder.getChildFile (project[ids::name].toString() + ".wav"), "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, start, end] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        file = file.withFileExtension (".wav");
        engine.stop();
        engine.beginOfflineRender();

        auto* job = new ExportJob (engine, file, start, end, [safe = juce::Component::SafePointer<MainComponent> (this), file] (const juce::String& error)
        {
            if (safe == nullptr)
                return;

            if (error.isEmpty())
            {
                safe->messageBox = juce::AlertWindow::showScopedAsync (
                    juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::InfoIcon, "Export finished",
                                                                  "Saved " + file.getFileName() + ".", "Show in Finder", "OK", safe.getComponent()),
                    [file] (int result) { if (result == 1) file.revealToUser(); });
            }
            else if (error != "cancelled")
            {
                safe->showError ("Export failed", error);
            }
        });

        job->launchThread();
    });
}

void MainComponent::showAudioMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Audio device settings...", [this] { showAudioSettings(); });
    menu.addItem ("Calibrate recording latency...", [this] { showLatencyCalibration(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

void MainComponent::showLatencyCalibration()
{
    if (engine.isRecording())
        return;

    engine.stop();

    auto calibrator = std::make_unique<LatencyCalibrator> (engine, recordingOffsetMs, [this] (double ms)
    {
        recordingOffsetMs = ms;
        appProperties.getUserSettings()->setValue ("recordingOffsetMs", ms);
        appProperties.saveIfNeeded();
    });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (calibrator.release());
    options.dialogTitle = "Calibrate recording latency";
    options.dialogBackgroundColour = theme::background;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 2, 2, 2, true, false, true, false);
    selector->setSize (520, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = theme::background;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace riff
