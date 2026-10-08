#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Theme.h"

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
    for (auto* b : { &newButton, &openButton, &saveButton, &undoButton, &redoButton,
                     &rewindButton, &playButton, &recordButton, &clickButton,
                     &addTrackButton, &importButton, &settingsButton })
    {
        b->setWantsKeyboardFocus (false); // keep Space for play/stop
        addAndMakeVisible (b);
    }

    newButton.onClick      = [this] { newProject(); };
    openButton.onClick     = [this] { openProject(); };
    saveButton.onClick     = [this] { saveProject(); };
    undoButton.onClick     = [this] { undo(); };
    redoButton.onClick     = [this] { redo(); };
    rewindButton.onClick   = [this] { returnToStart(); };
    playButton.onClick     = [this] { togglePlay(); };
    recordButton.onClick   = [this] { toggleRecord(); };
    clickButton.onClick    = [this] { toggleMetronome(); };
    recordButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
    recordButton.setColour (juce::TextButton::textColourOffId, theme::danger.brighter (0.3f));
    clickButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    addTrackButton.onClick = [this] { addTrack(); };
    importButton.onClick   = [this] { importAudio(); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    newButton.setTooltip ("New project (Cmd+N)");
    openButton.setTooltip ("Open project (Cmd+O)");
    saveButton.setTooltip ("Save (Cmd+S), Save As (Cmd+Shift+S)");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    rewindButton.setTooltip ("Go to start (Return)");
    playButton.setTooltip ("Play / stop (Space)");
    recordButton.setTooltip ("Record on armed tracks (R). Arms the selected track if none are armed.");
    clickButton.setTooltip ("Metronome (K)");
    addTrackButton.setTooltip ("New audio track");
    importButton.setTooltip ("Import audio files at the playhead (Cmd+I)");
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
    headers.inputNames = [this] { return engine.getActiveInputChannelNames(); };
    waveforms.onChange = [this] { canvas.repaint(); };

    timeline.addChangeListener (this);
    undoManager.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

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

    place (newButton, 52);
    place (openButton, 56);
    place (saveButton, 52, 16);
    place (undoButton, 52);
    place (redoButton, 52, 16);
    place (rewindButton, 36);
    place (playButton, 56);
    place (recordButton, 44, 10);
    place (positionLabel, 196, 10);
    place (tempoLabel, 50, 2);
    place (tempoCaption, 32, 8);
    place (clickButton, 50, 16);

    auto placeRight = [&bar] (juce::Component& c, int width, int gapBefore = 4)
    {
        c.setBounds (bar.removeFromRight (width));
        bar.removeFromRight (gapBefore);
    };

    placeRight (settingsButton, 60, 16);
    placeRight (masterSlider, 110, 2);
    placeRight (masterCaption, 48, 16);
    placeRight (importButton, 64);
    placeRight (addTrackButton, 70);

    // Status line
    statusLabel.setBounds (area.removeFromBottom (statusHeight).reduced (10, 0));

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
    const auto contentHeight = juce::jmax ((project.getNumChildren() + 1) * TimelineState::trackHeight,
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

void MainComponent::addTrack()
{
    undoManager.beginNewTransaction ("New track");
    auto track = model::addTrack (project, "Audio " + juce::String (project.getNumChildren() + 1), &undoManager);
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
        engine.stop();
    else
        engine.play();
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

    if (numInputs == 0)
    {
        showError ("No audio input",
                   "Riff can't hear any inputs. Click Audio to choose an input device.\n\n"
                   "On a Mac, also check System Settings > Privacy & Security > Microphone and allow Riff.");
        return;
    }

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
            track = model::addTrack (project, "Audio " + juce::String (project.getNumChildren() + 1), &undoManager);
            timeline.selectedTrackId = track[ids::id].toString();
            timeline.changed();
        }

        track.setProperty (ids::armed, true, nullptr);
        armed.add (track);
    }

    std::vector<AudioEngine::RecordRequest> requests;

    for (const auto& track : armed)
    {
        const int first = track.getProperty (ids::input, 0);
        const int channels = (bool) track.getProperty (ids::inputStereo, false) ? 2 : 1;

        if (first + channels > numInputs)
        {
            showError ("Input not available",
                       "\"" + track[ids::name].toString() + "\" is set to record from an input that isn't active. "
                       "Right-click its R button to pick another input, or enable more inputs under Audio.");
            return;
        }

        requests.push_back ({ track[ids::id].toString(), nextTakeFile (track), first, channels });
    }

    juce::String error;
    if (! engine.startRecording (requests, error))
    {
        showError ("Couldn't start recording", error);
        return;
    }

    if (! engine.isPlaying())
        engine.play();
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

    for (const auto& take : takes)
    {
        auto track = model::findById (project, take.trackId);
        if (! track.isValid())
            continue; // the track was deleted while recording; the file stays on disk

        auto region = model::addRecordedRegion (track, take.file, (double) take.startSample / rate,
                                                (double) take.numSamples / rate, latency, &undoManager);
        if (region.isValid())
            last = region;
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
    const auto levels = engine.takeLevels();
    inputMeter.push (levels.inL, levels.inR);
    outputMeter.push (levels.outL, levels.outR);

    const auto playhead = engine.getPlayheadSeconds();
    positionLabel.setText (formatPosition (playhead), juce::dontSendNotification);

    const bool playing = engine.isPlaying();
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);
    recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
    clickButton.setToggleState ((bool) project[ids::metronome], juce::dontSendNotification);

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
