#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Theme.h"
#include "AppInfo.h"
#include "ui/LatencyCalibrator.h"
#include "engine/BuiltInEffects.h"
#include "ui/Commands.h"
#include "ui/KeyCommands.h"

namespace riff
{

namespace
{
    constexpr int toolbarHeight = 100;    // two rows: display row, transport row
    constexpr int toolbarTopRow = 50;
    constexpr int headerWidth = 232;
    constexpr int scrollBarSize = 12;
    constexpr int meterWidth = 84;
    constexpr int statusHeight = 24;
}

MainComponent::MainComponent (bool openAudioDevice)
{
    // --- Toolbar ----------------------------------------------------------------
    for (auto* b : std::initializer_list<juce::Button*> { &fileButton, &undoButton, &redoButton, &addTrackButton, &settingsButton,
                                                          &rewindButton, &backBarButton, &forwardButton, &stopButton, &playButton,
                                                          &recordButton, &cycleButton, &countInButton, &clickButton,
                                                          &mixerButton, &editorButton, &keysButton })
    {
        b->setWantsKeyboardFocus (false); // keep Space for play/stop
        addAndMakeVisible (b);
    }

    for (auto* b : { &fileButton, &undoButton, &redoButton, &addTrackButton, &settingsButton })
        look::key (*b, look::Key::dark);

    // The keys run the same commands as the menus and shortcuts.
    auto bind = [this] (juce::Button& b, juce::CommandID id) { b.onClick = [this, id] { commandManager.invokeDirectly (id, true); }; };
    bind (rewindButton, commands::goToStart);
    bind (backBarButton, commands::rewindBar);
    bind (forwardButton, commands::forwardBar);
    bind (playButton, commands::play);
    bind (recordButton, commands::record);
    bind (cycleButton, commands::toggleCycle);
    bind (countInButton, commands::toggleCountIn);
    bind (clickButton, commands::toggleMetronome);
    bind (mixerButton, commands::showMixer);
    bind (editorButton, commands::showPianoRoll);
    bind (keysButton, commands::keyCommands);
    bind (undoButton, commands::undo);
    bind (redoButton, commands::redo);
    stopButton.onClick = [this]
    {
        // Stop; pressed while stopped, go back to the start (as on a tape machine).
        if (engine.isRecording())
            finishRecording();
        if (engine.isPlaying())
            engine.stop();
        else
            returnToStart();
    };

    fileButton.onClick = [this] { showFileMenu(); };
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addCommandItem (&commandManager, commands::newAudioTrack, "Audio track");
        menu.addCommandItem (&commandManager, commands::newInstrumentTrack, "Instrument track (MIDI)");
        menu.addCommandItem (&commandManager, commands::newAuxBus, "Aux bus (for sends)");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton));
    };
    settingsButton.onClick = [this] { showAudioMenu(); };

    fileButton.setTooltip ("New, open, save, import audio, bounce the song");
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");
    rewindButton.setTooltip ("Go to beginning (Return)");
    backBarButton.setTooltip ("Back one bar (,)");
    forwardButton.setTooltip ("Forward one bar (.)");
    stopButton.setTooltip ("Stop. Press again to go to the beginning.");
    playButton.setTooltip ("Play / stop (Space)");
    recordButton.setTooltip ("Record on armed tracks (R). Arms the selected track if none are armed.");
    cycleButton.setTooltip ("Cycle (C). Drag in the top half of the ruler to set the range.\n"
                            "Recording with Cycle on makes a new take on every pass.");
    countInButton.setTooltip ("Count-in: one bar of click before recording (Shift+K)");
    clickButton.setTooltip ("Metronome (K)");
    mixerButton.setTooltip ("Mixer (X)");
    editorButton.setTooltip ("Piano roll for the selected MIDI region (P)");
    keysButton.setTooltip ("Key commands (Option+K)");
    addTrackButton.setTooltip ("New audio track, instrument track or aux bus");
    settingsButton.setTooltip ("Audio device settings, latency calibration, key commands");

    // The display in the middle of the bar
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::plain));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, theme::lcdText);
    positionLabel.setTooltip ("Bar, beat, 16th");
    positionLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (positionLabel);

    clockLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::plain));
    clockLabel.setJustificationType (juce::Justification::centred);
    clockLabel.setColour (juce::Label::textColourId, theme::lcdText);
    clockLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (clockLabel);

    tempoLabel.setEditable (false, true, false);
    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::plain));
    tempoLabel.setColour (juce::Label::textColourId, theme::lcdText);
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

    tempoCaption.setColour (juce::Label::textColourId, theme::lcdDim);
    tempoCaption.setFont (juce::FontOptions (10.0f));
    tempoCaption.setJustificationType (juce::Justification::centred);
    tempoCaption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (tempoCaption);

    masterCaption.setColour (juce::Label::textColourId, theme::dimText);
    masterCaption.setFont (juce::FontOptions (12.0f));
    masterCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (masterCaption);

    masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterSlider.setRange (0.0, 1.0, 0.001);
    masterSlider.setScrollWheelEnabled (false);
    masterSlider.setTooltip ("Master volume");
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
    settingsOptions.applicationName = app::name;
    settingsOptions.filenameSuffix = "settings";
    settingsOptions.osxLibrarySubFolder = "Application Support";
    // Settings saved by the app before it was renamed come along once.
    {
        auto oldOptions = settingsOptions;
        oldOptions.applicationName = "Riff";
        const auto oldFile = oldOptions.getDefaultFile();
        const auto newFile = settingsOptions.getDefaultFile();
        if (! newFile.exists() && oldFile.existsAsFile())
        {
            newFile.getParentDirectory().createDirectory();
            oldFile.copyFileTo (newFile);
        }
    }

    appProperties.setStorageParameters (settingsOptions);
    recordingOffsetMs = appProperties.getUserSettings()->getDoubleValue ("recordingOffsetMs", 0.0);

    // The list of plugins found by earlier scans.
    if (auto xml = appProperties.getUserSettings()->getXmlValue ("knownPlugins"))
        rack.getKnownPlugins().recreateFromXml (*xml);

    // --- Menus and key commands -------------------------------------------------
    keyPreset = appProperties.getUserSettings()->getValue ("keyPreset", commands::presetNames()[0]);
    if (! commands::presetNames().contains (keyPreset))
        keyPreset = commands::presetNames()[0];

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);

    if (auto xml = appProperties.getUserSettings()->getXmlValue ("keyMappings"))
        commandManager.getKeyMappings()->restoreFromXml (*xml);

    addKeyListener (commandManager.getKeyMappings());
    commandManager.getKeyMappings()->addChangeListener (this);
    setApplicationCommandManagerToWatch (&commandManager);

   #if JUCE_MAC
    {
        juce::PopupMenu appMenu;
        appMenu.addCommandItem (&commandManager, commands::audioSettings, "Settings...");
        appMenu.addCommandItem (&commandManager, commands::keyCommands);
        juce::MenuBarModel::setMacMainMenu (this, &appMenu);
    }
   #endif

    // --- Start ------------------------------------------------------------------
    if (openAudioDevice)
        startupError = engine.initialise();
    else
        engine.prepare (48000.0, 512);
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
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    keyCommandsWindow.reset();
    commandManager.getKeyMappings()->removeChangeListener (this);
    removeKeyListener (commandManager.getKeyMappings());

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
juce::Rectangle<int> MainComponent::lcdBounds() const
{
    return juce::Rectangle<int> (440, 42).withCentre ({ getWidth() / 2, toolbarTopRow / 2 + 1 });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Navy control bar
    const auto bar = getLocalBounds().removeFromTop (toolbarHeight).toFloat();
    g.setGradientFill (juce::ColourGradient (theme::toolbar.brighter (0.08f), 0.0f, bar.getY(),
                                             theme::toolbarDark, 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (0, toolbarTopRow, getWidth(), 1);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0, toolbarTopRow + 1, getWidth(), 1);
    g.setColour (juce::Colours::black);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);

    // The display
    const auto lcd = lcdBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (lcd.translated (0.0f, 1.0f).expanded (1.0f), 6.0f);
    g.setGradientFill (juce::ColourGradient (theme::lcd.brighter (0.06f), 0.0f, lcd.getY(), theme::lcd, 0.0f, lcd.getBottom(), false));
    g.fillRoundedRectangle (lcd, 5.0f);
    g.setColour (theme::outline.darker (0.3f));
    g.drawRoundedRectangle (lcd.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (theme::lcdDim.withAlpha (0.4f));
    for (auto* c : { &positionLabel, &clockLabel })
        g.fillRect ((float) c->getRight() + 1.0f, lcd.getY() + 8.0f, 1.0f, lcd.getHeight() - 16.0f);

    g.setColour (theme::lcdDim);
    g.setFont (juce::FontOptions (9.5f));
    const auto captionY = lcd.getBottom() - 13.0f;
    g.drawText ("BAR   BEAT   DIV", juce::Rectangle<float> ((float) positionLabel.getX(), captionY, (float) positionLabel.getWidth(), 11.0f),
                juce::Justification::centred, false);
    g.drawText ("TIME", juce::Rectangle<float> ((float) clockLabel.getX(), captionY, (float) clockLabel.getWidth(), 11.0f),
                juce::Justification::centred, false);

    // Captions under the keys
    g.setFont (juce::FontOptions (11.0f));
    for (auto* b : std::initializer_list<juce::Button*> { &rewindButton, &backBarButton, &forwardButton, &stopButton, &playButton,
                                                          &recordButton, &cycleButton, &countInButton, &clickButton,
                                                          &mixerButton, &editorButton, &keysButton })
    {
        g.setColour (b->getToggleState() ? theme::cream : theme::text.withAlpha (0.78f));
        g.drawText (b->getName(), b->getBounds().translated (0, b->getHeight() + 1).withHeight (13).expanded (8, 0),
                    juce::Justification::centred, false);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (toolbarHeight);

    // Row 1: file and edit keys | the display | master volume and audio
    {
        auto row = toolbar.removeFromTop (toolbarTopRow).reduced (12, 10);
        auto left = [&row] (juce::Component& c, int width, int gap = 6) { c.setBounds (row.removeFromLeft (width)); row.removeFromLeft (gap); };
        auto right = [&row] (juce::Component& c, int width, int gap = 6) { c.setBounds (row.removeFromRight (width)); row.removeFromRight (gap); };

        left (fileButton, 54, 12);
        left (undoButton, 54);
        left (redoButton, 54, 12);
        left (addTrackButton, 74);

        right (settingsButton, 62, 14);
        right (masterSlider, 120, 4);
        right (masterCaption, 52);

        auto lcd = lcdBounds().reduced (6, 2);
        auto numbers = lcd.withTrimmedBottom (12);
        positionLabel.setBounds (numbers.removeFromLeft (150));
        clockLabel.setBounds (numbers.removeFromLeft (190));
        auto tempo = lcd.withLeft (numbers.getX());
        tempoLabel.setBounds (tempo.withTrimmedBottom (12));
        tempoCaption.setBounds (tempo.removeFromBottom (12).translated (0, 1));
    }

    // Row 2: transport keys in the middle, views on the right
    {
        constexpr int keyWidth = 44, keyHeight = 30, gap = 6;
        auto row = toolbar.withTrimmedTop (5).withHeight (keyHeight);

        const std::initializer_list<juce::Component*> transport { &rewindButton, &backBarButton, &forwardButton, &stopButton,
                                                                  &playButton, &recordButton, &cycleButton, &countInButton, &clickButton };
        const int totalWidth = (int) transport.size() * (keyWidth + gap) + 14;
        int x = getWidth() / 2 - totalWidth / 2;

        for (auto* c : transport)
        {
            c->setBounds (x, row.getY(), keyWidth, keyHeight);
            x += keyWidth + gap + (c == &recordButton ? 14 : 0);
        }

        x = getWidth() - 12 - 3 * keyWidth - 2 * gap;
        for (auto* c : { (juce::Component*) &mixerButton, (juce::Component*) &editorButton, (juce::Component*) &keysButton })
        {
            c->setBounds (x, row.getY(), keyWidth, keyHeight);
            x += keyWidth + gap;
        }
    }

    // Status line
    statusLabel.setBounds (area.removeFromBottom (statusHeight).reduced (10, 0));

    if (pianoRoll.isVisible())
        pianoRoll.setBounds (area.removeFromBottom (juce::jlimit (200, 360, getHeight() / 3)));
    else if (mixer.isVisible())
        mixer.setBounds (area.removeFromBottom (juce::jmin (area.getHeight() - 120,
                                                            juce::jlimit (ChannelStrip::minimumHeight + MixerPanel::headerHeight, 660,
                                                                          (int) (getHeight() * 0.68)))));

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
// Zoom
//==============================================================================
void MainComponent::zoomHorizontally (double factor)
{
    // Keeps the playhead (or the left edge, if it's off screen) where it is, as in Logic.
    const auto x = juce::jlimit (0.0f, (float) canvas.getWidth(), timeline.timeToX (engine.getPlayheadSeconds()));
    const auto timeAtX = timeline.xToTime (x);
    timeline.pixelsPerSecond = juce::jlimit (TimelineState::minPixelsPerSecond, TimelineState::maxPixelsPerSecond,
                                             timeline.pixelsPerSecond * factor);
    timeline.scrollSeconds = juce::jmax (0.0, timeAtX - (double) x / timeline.pixelsPerSecond);
    timeline.changed();
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
        fileChooser = std::make_unique<juce::FileChooser> ("Open a project",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           "*" + app::projectExtension + ";*" + app::oldProjectExtension);

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

    fileChooser = std::make_unique<juce::FileChooser> ("Save project", suggested, "*" + model::fileExtension);

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
        window->setName (project[ids::name].toString() + (dirty ? " (edited)" : "") + " - " + app::name);
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
    const auto prefix = instrument ? "Inst " : (kind == model::busKind ? "Bus " : "Audio ");
    auto track = model::addTrack (project, prefix + juce::String (model::numTracks (project) + 1), &undoManager, -1, kind);
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
                       app::name + " can't hear any inputs. Click Audio to choose an input device.\n\n"
                       "On a Mac, also check System Settings > Privacy & Security > Microphone and allow " + app::name + ".");
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
    // Device latency, your calibration offset, and plugin latency (delay compensation).
    const auto totalLatency = latency + recordingOffsetMs / 1000.0 + engine.getPlaybackLatencySamples() / rate;
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
               .getChildFile (app::name).getChildFile ("Recordings")
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

    // Menu ticks follow these.
    if (property == ids::cycleEnabled || property == ids::metronome || property == ids::countIn
        || property == ids::mute || property == ids::solo || property == ids::armed || property == ids::showAutomation)
        commandManager.commandStatusChanged();

    markDirty();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &timeline)
    {
        updateScrollBars();
        commandManager.commandStatusChanged(); // the selection changed what the Track commands act on
    }
    else if (source == commandManager.getKeyMappings())
    {
        saveKeyMappings();
    }
    else if (source == &undoManager)
    {
        undoButton.setEnabled (undoManager.canUndo());
        redoButton.setEnabled (undoManager.canRedo());
        undoButton.setTooltip (undoManager.canUndo() ? "Undo " + undoManager.getUndoDescription() : "Nothing to undo");
        redoButton.setTooltip (undoManager.canRedo() ? "Redo " + undoManager.getRedoDescription() : "Nothing to redo");
        commandManager.commandStatusChanged();
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
    editorButton.setToggleState (pianoRoll.isVisible(), juce::dontSendNotification);

    const auto playhead = engine.getPlayheadSeconds();
    positionLabel.setText (formatPosition (playhead), juce::dontSendNotification);
    clockLabel.setText (formatClock (playhead), juce::dontSendNotification);

    const bool playing = engine.isPlaying();
    if (playing != playButton.getToggleState() || engine.isRecording() != recordButton.getToggleState())
        repaint (0, toolbarTopRow, getWidth(), toolbarHeight - toolbarTopRow);   // the captions under the keys
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
        return "- " + juce::String ((int) std::ceil (-seconds / beatLength - 1e-9)) + " -";

    const auto beats = seconds / beatLength;

    const auto bar = (int) std::floor (beats / beatsPerBar) + 1;
    const auto beat = (int) std::floor (beats) % beatsPerBar + 1;
    const auto sixteenth = (int) std::floor ((beats - std::floor (beats)) * 4.0) + 1;

    return juce::String::formatted ("%3d  %d  %d", bar, beat, sixteenth);
}

juce::String MainComponent::formatClock (double seconds) const
{
    const auto totalMs = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 1000.0);
    return juce::String::formatted ("%02d:%02d:%02d.%03d", (int) (totalMs / 3600000), (int) (totalMs / 60000) % 60,
                                    (int) (totalMs / 1000) % 60, (int) (totalMs % 1000));
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
    commandManager.commandStatusChanged();
}

void MainComponent::closePianoRoll()
{
    if (! pianoRoll.isVisible())
        return;

    pianoRoll.setRegion ({});
    pianoRoll.setVisible (false);
    resized();
    canvas.grabKeyboardFocus();
    commandManager.commandStatusChanged();
}

void MainComponent::showInstrumentMenu (juce::ValueTree track, juce::Component& target)
{
    const bool isPlugin = track[ids::instrument].toString() == "plugin";
    const auto currentDescription = track[ids::pluginDescription].toString();

    juce::PopupMenu menu;
    menu.addItem (isPlugin ? "Show plugin window" : "Show synth controls", [this, track] { openInstrumentWindow (track); });
    menu.addSeparator();

    menu.addItem (app::synthName, true, ! isPlugin, [this, track]() mutable
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

    editorWindows[id] = std::make_unique<EditorWindow> (title + ": " + app::synthName, new SynthPanel (track, undoManager), false, closer);
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
    commandManager.commandStatusChanged();
}

void MainComponent::closeMixer()
{
    mixer.setVisible (false);
    resized();
    canvas.grabKeyboardFocus();
    commandManager.commandStatusChanged();
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
    menu.addSectionHeader (app::effectsName);

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
    menu.addSeparator();
    menu.addCommandItem (&commandManager, commands::keyCommands);
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
