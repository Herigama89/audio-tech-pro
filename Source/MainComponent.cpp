#include "MainComponent.h"
#include "model/Project.h"
#include "ui/Theme.h"
#include "AppInfo.h"
#include "ui/LatencyCalibrator.h"
#include "engine/BuiltInEffects.h"
#include "ui/Commands.h"
#include "ui/KeyCommands.h"
#include "ui/TrackPicker.h"
#include "ui/RoutingMenu.h"
#include "ui/ConsoleStripWindow.h"
#include "ui/AudioEditorPanel.h"
#include "engine/ConsolePlugins.h"

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
                                                          &recordButton, &cycleButton, &countInButton, &clickButton, &punchButton, &replaceButton,
                                                          &mixerButton, &editorButton, &keysButton, &generateButton, &claudeButton, &screenButton })
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
    bind (punchButton, commands::toggleAutopunch);
    bind (replaceButton, commands::toggleReplace);
    punchButton.setTooltip ("Autopunch: record only between the punch locators (the red bar in the ruler, under the cycle)");
    replaceButton.setTooltip ("Replace: a recording replaces the parts of regions it covers");
    bind (mixerButton, commands::showMixer);
    bind (editorButton, commands::showPianoRoll);
    bind (keysButton, commands::keyCommands);
    bind (generateButton, commands::generateMidi);
    bind (claudeButton, commands::showAssistant);
    bind (screenButton, commands::recordScreen);

    // Tool, Snap and Drag, as in Logic's tracks area
    toolBox.addItemList ({ "Pointer", "Pencil", "Eraser", "Scissors", "Glue", "Mute", "Fade", "Marquee", "Zoom", "Solo", "Text" }, 1);
    toolBox.setSelectedId (1, juce::dontSendNotification);
    toolBox.setTooltip ("Tool (T): pointer, pencil (draws MIDI regions), eraser, scissors, glue (joins), mute, fade (drag at a region's start or end; Option-click removes), "
                        "marquee (select a stretch of time on tracks; Delete removes it, Cmd+T splits at its edges), zoom (click or drag; Option-click zooms out), "
                        "solo (hold on a region to hear its track alone from there), text (click a region to rename it)");
    toolBox.onChange = [this] { setTool ((TimelineState::Tool) (toolBox.getSelectedId() - 1)); };
    snapBox.addItemList ({ "Smart", "Bar", "Beat", "Division", "Off" }, 1);
    snapBox.setSelectedId (1, juce::dontSendNotification);
    snapBox.setTooltip ("Snap: where regions land when you move them (Smart follows the zoom). Hold Cmd to move freely.");
    snapBox.onChange = [this] { timeline.snapMode = (TimelineState::Snap) (snapBox.getSelectedId() - 1); };
    dragBox.addItemList ({ "Overlap", "No Overlap", "X-Fade", "Shuffle R", "Shuffle L" }, 1);
    dragBox.setSelectedId (1, juce::dontSendNotification);
    dragBox.setTooltip ("Drag: Overlap lets regions lie on top of each other; No Overlap shortens what they cover; "
                       "X-Fade crossfades where they overlap; Shuffle closes the gaps (to the right or the left).");
    dragBox.onChange = [this] { timeline.dragMode = (TimelineState::Drag) (dragBox.getSelectedId() - 1); };
    for (auto* box : { &toolBox, &snapBox, &dragBox })
    {
        box->setWantsKeyboardFocus (false);
        addAndMakeVisible (box);
    }
    bind (undoButton, commands::undo);
    bind (redoButton, commands::redo);
    stopButton.onClick = [this]
    {
        // Stop; pressed while stopped, go back to the start (as on a tape machine).
        if (engine.isPlaying())
            togglePlay();   // stops (and follows the stop workflow)
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
    screenButton.setTooltip ("Record the screen with the mix's sound (click again to stop)");
    claudeButton.setTooltip ("Claude: ask questions about your project, or tell it what to do");
    generateButton.setTooltip ("MIDI Generator: drums, bass, chords and a melody in a genre's style");
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
            model::setTempo (project, bpm, &undoManager);   // MIDI, markers and the cycle stay on their bars
        }
        refreshProjectControls();
    };
    addAndMakeVisible (tempoLabel);

    signatureLabel.setJustificationType (juce::Justification::centred);
    signatureLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain));
    signatureLabel.setColour (juce::Label::textColourId, theme::lcdText);
    signatureLabel.setTooltip ("Time signature and key. Click to change them.");
    signatureLabel.addMouseListener (this, false);
    addAndMakeVisible (signatureLabel);

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
    canvas.onSplit = [this] { splitAtPlayhead(); };
    canvas.onDelete = [this] { deleteSelection(); };
    canvas.onRepeat = [this] { repeatSelectedRegions(); };
    canvas.onJoin = [this] { joinSelectedRegions(); };
    canvas.onMuteRegions = [this] { muteSelectedRegions(); };
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
    effectRack.onEffectReady = [this]
    {
        player.requestRebuild();
        mixer.getConsolePanel().refresh();   // a bus compressor plugin may have just loaded
        mixer.pluginsChanged();              // ...or a channel strip plugin
    };

    // Console mode: the user's own bus compressor plugin on the master.
    {
        auto& console = mixer.getConsolePanel();
        console.onLoadPlugin = [this] (const juce::PluginDescription& d)
        {
            if (auto xml = d.createXml())
            {
                undoManager.beginNewTransaction ("Master Bus Compressor plugin");
                model::setBusCompPlugin (project, xml->toString (juce::XmlElement::TextFormat().singleLine()), &undoManager);
            }
        };
        console.onRemovePlugin = [this]
        {
            undoManager.beginNewTransaction ("Remove Master Bus Compressor plugin");
            for (const auto& insert : model::insertsOf (model::findChain (project, model::busCompOwner)))
                model::removeInsert (insert, &undoManager);
        };
        console.onOpenPluginWindow = [this] (juce::ValueTree insert) { openInsertWindow (insert); };

        // ...and on every track: the console chosen in the mixer's header.
        mixer.onChooseConsole = [this] (const juce::PluginDescription* d)
        {
            juce::String xml;
            if (d != nullptr)
                if (auto e = d->createXml())
                    xml = e->toString (juce::XmlElement::TextFormat().singleLine());

            undoManager.beginNewTransaction (d != nullptr ? "Console: " + d->name : juce::String ("Console: built-in"));
            model::setConsolePlugin (project, xml, &undoManager);
        };
    }
    effectRack.onLoadError = [this] (const juce::String& error) { showError ("Plugin problem", error); };
    effectRack.onEffectReplaced = [this] (const juce::String& insertId) { editorWindows.erase (insertId); };
    mixer.onOpenInsert = [this] (juce::ValueTree insert) { openInsertWindow (insert); };
    mixer.onChooseEffect = [this] (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)
    {
        showEffectMenu (owner, slot, existing, target);
    };
    mixer.onClose = [this] { closeMixer(); };
    mixer.onSelectTrack = [this] (const juce::String& id, juce::ModifierKeys mods) { selectTrackFromMixer (id, mods); };
    addChildComponent (mixer);
    addChildComponent (inspector);
    headers.inputNames = [this] { return engine.getActiveInputChannelNames(); };
    waveforms.onChange = [this] { canvas.repaint(); };

    timeline.addChangeListener (this);

    addChildComponent (paneDivider);
    paneDivider.onStart = [this] { paneHeightAtDragStart = pianoRoll.isVisible() ? pianoRoll.getHeight() : mixer.getHeight(); };
    paneDivider.onDrag = [this] (int dy)
    {
        (pianoRoll.isVisible() ? editorPaneHeight : mixerPaneHeight) = juce::jmax (150, paneHeightAtDragStart - dy);
        resized();
    };
    paneDivider.onReset = [this] { (pianoRoll.isVisible() ? editorPaneHeight : mixerPaneHeight) = 0; resized(); };
    fileCache.addChangeListener (this);
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
    autosaveMinutes = appProperties.getUserSettings()->getIntValue ("autosaveMinutes", 2);
    engine.setRecordBitDepth (appProperties.getUserSettings()->getIntValue ("recordBitDepth", 24));

    // The list of plugins found by earlier scans.
    if (auto xml = appProperties.getUserSettings()->getXmlValue ("knownPlugins"))
        rack.getKnownPlugins().recreateFromXml (*xml);

    mixer.getConsolePanel().setCandidates (console::findBusCompressors (rack.getKnownPlugins().getTypes()));
    mixer.setStripCandidates (console::findChannelStrips (rack.getKnownPlugins().getTypes()));
    rack.getKnownPlugins().addChangeListener (this);   // a new scan may find one

    // --- Menus and key commands -------------------------------------------------
    keyPreset = appProperties.getUserSettings()->getValue ("keyPreset", commands::presetNames()[0]);
    if (! commands::presetNames().contains (keyPreset))
        keyPreset = commands::presetNames()[0];
    stopReturnsToStart = appProperties.getUserSettings()->getBoolValue ("stopReturnsToStart", commands::stopReturnsToStart (keyPreset));

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
    {
        // Combined interfaces are made again before the device opens, so it can be chosen.
        aggregateSetup = aggregate::Setup::fromString (appProperties.getUserSettings()->getValue ("combinedInterfaces"));
        juce::String combineError;
        if (aggregateSetup.isActive())
            combineError = aggregate::create (aggregateSetup);

        const auto saved = appProperties.getUserSettings()->getXmlValue ("audioDevice");
        startupError = engine.initialise (saved.get());
        updateInputLabels();

        if (combineError.isNotEmpty() && startupError.isEmpty())
            startupError = "Your interfaces couldn't be combined: " + combineError
                           + "\n\nUsing one interface for now. Plug the other in and choose Combine Audio Interfaces again.";
    }
    else
        engine.prepare (48000.0, 512);
    setProject (model::createProject(), {});
    statusLabel.setText (deviceStatusText(), juce::dontSendNotification);
    createAssistant();
    if (appProperties.getUserSettings()->getBoolValue ("inspector", false))
        toggleInspector();
    screenRecorder = ScreenRecorder::create();

    setWantsKeyboardFocus (true);
    setSize (1440, 920);
    automation.makeCurrent();
    routing::toggleFreeze = [this] (juce::ValueTree track) { toggleFreeze (track); };

    // A knob turned in a plugin's own window: written as automation in Touch, Latch or Write.
    effectRack.onPluginParameter = [this] (const juce::String& insertId, int index, float value, int phase)
    {
        juce::ValueTree insert;
        for (const auto& chain : model::mixerOf (project))
            if (chain.hasType (ids::CHAIN))
                if (auto i = chain.getChildWithProperty (ids::id, insertId); i.isValid())
                    insert = i;
        if (! insert.isValid())
            return;
        const auto owner = insert.getParent()[ids::owner].toString();
        const auto param = model::insertParam (insertId, "#" + juce::String (index));
        if (value < 0.0f)
            if (auto* plugin = effectRack.getPlugin (insertId); plugin != nullptr && juce::isPositiveAndBelow (index, plugin->getParameters().size()))
                value = plugin->getParameters()[index]->getValue();

        if (phase == 0)      automation.begin (owner, param, value);
        else if (phase == 1) automation.move (owner, param, value);
        else                 automation.end (owner, param);
    };
    canvas.onOpenAudioEditor = [this] (juce::ValueTree region)
    {
        const auto id = "audio:" + region[ids::id].toString();
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
        editorWindows[id] = std::make_unique<EditorWindow> (region[ids::name].toString() + ": Audio File Editor",
                                                            new AudioEditorPanel (region, undoManager, engine.getFormatManager(),
                                                                                  recordingFolder().getChildFile ("Edits")),
                                                            true, closer);
    };
    routing::learnController = [this] (juce::ValueTree track, juce::String param)
    {
        learningTrack = track;
        learningParam = param;
        statusLabel.setText ("MIDI Learn: move a knob, fader or button on your controller for " + track[ids::name].toString()
                                 + " " + param + ".", juce::dontSendNotification);
    };
    mixer.onOpenStripWindow = [this] (juce::ValueTree track)
    {
        const auto id = "strip-window:" + track[ids::id].toString();
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
        editorWindows[id] = std::make_unique<EditorWindow> (track[ids::name].toString() + ": Channel Strip",
                                                            new ConsoleStripWindow (track, undoManager), false, closer);
    };
    routing::createStack = [this] (bool summing) { createStackFromSelection (summing); };
    TrackHeader::pluginParameterNames = [this] (const juce::String& insertId)
    {
        juce::StringArray names;
        if (auto* plugin = effectRack.getPlugin (insertId))
            for (auto* p : plugin->getParameters())
                names.add (p->getName (40));
        return names;
    };
    startTimerHz (30);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArrangeCanvas> (&canvas)]
    {
        if (safe != nullptr)
            safe->grabKeyboardFocus();
    });

    if (startupError.isNotEmpty())
        showError ("Audio device problem", startupError);
    else if (openAudioDevice)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;

            // Settings: open the last project at startup.
            if (safe->appProperties.getUserSettings()->getBoolValue ("openLastProject", false))
            {
                const juce::File last (safe->appProperties.getUserSettings()->getValue ("lastProject"));
                juce::String error;
                if (last.existsAsFile())
                    if (auto loaded = model::load (last, error); loaded.isValid())
                    {
                        loaded.setProperty (ids::name, last.getFileNameWithoutExtension(), nullptr);
                        safe->setProject (loaded, last);
                    }
            }
            safe->offerRecovery();
        });
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    keyCommandsWindow.reset();
    generatorWindow.reset();
    assistantSettingsWindow.reset();
    screenRecordWindow.reset();
    combineWindow.reset();
    engine.setScreenAudioTap (nullptr);
    screenRecorder.reset();
    if (mixMeasureJob != nullptr)
        mixMeasureJob->stopThread (10000);
    assistantPanel.reset();
    assistant.reset();
    assistantTools.reset();
    assistantTransport.reset();
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
    rack.getKnownPlugins().removeChangeListener (this);
    fileCache.removeChangeListener (this);
    undoManager.removeChangeListener (this);
    timeline.removeChangeListener (this);
    project.removeListener (this);
}

//==============================================================================
// Layout
//==============================================================================
juce::Rectangle<int> MainComponent::lcdBounds() const
{
    return juce::Rectangle<int> (560, 42).withCentre ({ getWidth() / 2, toolbarTopRow / 2 + 1 });
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
    g.drawText ("SIGNATURE   KEY", juce::Rectangle<float> ((float) signatureLabel.getX(), captionY, (float) signatureLabel.getWidth(), 11.0f),
                juce::Justification::centred, false);
    g.drawText ("TIME", juce::Rectangle<float> ((float) clockLabel.getX(), captionY, (float) clockLabel.getWidth(), 11.0f),
                juce::Justification::centred, false);

    // Captions under the Tool, Snap and Drag menus
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (theme::text.withAlpha (0.78f));
    for (auto [box, name] : { std::pair { &toolBox, "Tool" }, std::pair { &snapBox, "Snap" }, std::pair { &dragBox, "Drag" } })
        if (box->isVisible())
            g.drawText (name, box->getBounds().translated (0, box->getHeight() + 4).withHeight (13), juce::Justification::centred, false);

    // Captions under the keys
    for (auto* b : std::initializer_list<juce::Button*> { &rewindButton, &backBarButton, &forwardButton, &stopButton, &playButton,
                                                          &recordButton, &cycleButton, &countInButton, &clickButton, &punchButton, &replaceButton,
                                                          &mixerButton, &editorButton, &keysButton, &generateButton, &claudeButton, &screenButton })
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
        auto tempo = lcd.withLeft (numbers.getX()).withWidth (100);
        tempoLabel.setBounds (tempo.withTrimmedBottom (12));
        tempoCaption.setBounds (tempo.removeFromBottom (12).translated (0, 1));
        signatureLabel.setBounds (lcd.withLeft (tempo.getRight()).withTrimmedBottom (12));
    }

    // Row 2: transport keys in the middle, views on the right
    {
        constexpr int keyWidth = 44, keyHeight = 30, gap = 6;
        auto row = toolbar.withTrimmedTop (5).withHeight (keyHeight);

        const std::initializer_list<juce::Component*> transport { &rewindButton, &backBarButton, &forwardButton, &stopButton,
                                                                  &playButton, &recordButton, &cycleButton, &countInButton, &clickButton,
                                                                  &punchButton, &replaceButton };
        const int totalWidth = (int) transport.size() * (keyWidth + gap) + 14;
        int x = getWidth() / 2 - totalWidth / 2;

        for (auto* c : transport)
        {
            c->setBounds (x, row.getY(), keyWidth, keyHeight);
            x += keyWidth + gap + (c == &recordButton ? 14 : 0);
        }

        generateButton.setBounds (18, row.getY(), keyWidth, keyHeight);
        claudeButton.setBounds (18 + keyWidth + gap + 4, row.getY(), keyWidth, keyHeight);
        screenButton.setBounds (18 + 2 * (keyWidth + gap + 4), row.getY(), keyWidth, keyHeight);

        // Tool, Snap and Drag, between those keys and the transport (when there's room)
        {
            auto boxes = juce::Rectangle<int> (screenButton.getRight() + 14, row.getY() + 2, 0, keyHeight - 6);
            const auto transportLeft = getWidth() / 2 - ((int) 9 * (keyWidth + gap) + 14) / 2;
            int boxX = boxes.getX();
            for (auto [box, w] : { std::pair { &toolBox, 84 }, std::pair { &snapBox, 80 }, std::pair { &dragBox, 94 } })
            {
                box->setVisible (boxX + w < transportLeft - 12);
                box->setBounds (boxX, boxes.getY(), w, boxes.getHeight());
                boxX += w + 6;
            }
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

    // The Claude panel down the right side
    if (assistantPanel != nullptr && assistantPanel->isVisible())
        assistantPanel->setBounds (area.removeFromRight (AssistantPanel::preferredWidth));
    if (libraryPanel != nullptr && libraryPanel->isVisible())
        libraryPanel->setBounds (area.removeFromRight (LibraryPanel::preferredWidth));

    // The Inspector down the left side, beside the tracks and the editors, as in Logic
    if (inspector.isVisible())
        inspector.setBounds (area.removeFromLeft (Inspector::preferredWidth));

    // Smart Controls at the very bottom.
    if (smartControls != nullptr && smartControls->isVisible())
        smartControls->setBounds (area.removeFromBottom (150));

    // The editor or the mixer below the tracks, with a divider to drag (double-click it for the default size).
    paneDivider.setVisible (pianoRoll.isVisible() || mixer.isVisible());
    if (pianoRoll.isVisible())
    {
        const auto h = editorPaneHeight > 0 ? editorPaneHeight : juce::jlimit (200, 360, getHeight() / 3);
        pianoRoll.setBounds (area.removeFromBottom (juce::jlimit (150, juce::jmax (150, area.getHeight() - 120), h)));
        paneDivider.setBounds (area.removeFromBottom (6));
    }
    else if (mixer.isVisible())
    {
        const auto fallback = juce::jmax (200, juce::jmin (area.getHeight() - 100, ChannelStrip::minimumHeight + MixerPanel::headerHeight + 40,
                                                         (int) (getHeight() * 0.8)));
        const auto h = mixerPaneHeight > 0 ? mixerPaneHeight : fallback;
        mixer.setBounds (area.removeFromBottom (juce::jlimit (150, juce::jmax (150, area.getHeight() - 60), h)));
        paneDivider.setBounds (area.removeFromBottom (6));
    }

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
    const auto contentHeight = juce::jmax (timeline.contentHeight(),
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

    if (assistant != nullptr)
        assistant->cancel();   // its actions belong to the old project

    project.removeListener (this);
    project = newProject;
    timeline.project = project;
    projectFile = file;
    project.addListener (this);

    undoManager.clearUndoHistory();

    timeline.selectedRegionId = {};
    timeline.selectedTrackId = {};
    timeline.scrollSeconds = 0.0;
    timeline.scrollY = 0;

    player.setProject (project);
    automation.setProject (project);
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
        if (result == 1)       saveProject (then);                  // Save
        else if (result == 2)  { removeAutosave(); then(); }        // Don't Save
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
            appProperties.getUserSettings()->setValue ("lastProject", file.getFullPathName());

            // A newer autosave (the app closed before saving): offer it.
            if (const auto autosaved = model::files::newerAutosave (file); autosaved.existsAsFile())
            {
                messageBox = juce::AlertWindow::showScopedAsync (
                    juce::MessageBoxOptions::makeOptionsYesNo (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved changes?",
                        "There's an autosave of \"" + file.getFileNameWithoutExtension() + "\" from "
                            + autosaved.getLastModificationTime().formatted ("%H:%M on %d %b")
                            + ", newer than the last save. Open the autosaved version?",
                        "Open Autosave", "Open Last Save", this),
                    [this, loaded, file, autosaved] (int result)
                    {
                        if (result == 1)
                        {
                            juce::String err;
                            if (auto recovered = model::load (autosaved, err); recovered.isValid())
                            {
                                recovered.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
                                setProject (recovered, file);
                                markDirty();
                                return;
                            }
                        }
                        setProject (loaded, file);
                    });
                return;
            }

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

    // The version on disk becomes a backup before it's replaced.
    model::files::makeBackup (projectFile, appProperties.getUserSettings()->getIntValue ("backupsToKeep", 10));

    juce::String error;
    if (! model::save (project, projectFile, error))
    {
        showError ("Couldn't save", error);
        return;
    }

    dirty = false;
    removeAutosave();   // saved: the autosave isn't needed
    updateTitle();
    appProperties.getUserSettings()->setValue ("lastProject", projectFile.getFullPathName());

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
    changedSinceAutosave = true;
    if (! dirty)
    {
        dirty = true;
        updateTitle();
    }
    updateScrollBars();
}

//==============================================================================
// Autosave, backups, Consolidate
//==============================================================================
void MainComponent::autosave()
{
    if (! changedSinceAutosave || engine.isRecording() || engine.isRenderingOffline())
        return;

    rack.storePluginStates (project);
    effectRack.storePluginStates (model::mixerOf (project));

    const auto file = model::files::autosaveFileFor (projectFile, project[ids::name].toString(), sessionId);
    if (currentAutosave != file)
        removeAutosave();   // e.g. the project was saved under a new name

    file.getParentDirectory().createDirectory();
    juce::String error;
    if (model::save (project, file, error))
    {
        currentAutosave = file;
        changedSinceAutosave = false;
    }
}

void MainComponent::removeAutosave()
{
    if (currentAutosave != juce::File())
        currentAutosave.deleteFile();
    currentAutosave = juce::File();
    if (projectFile != juce::File())
        model::files::autosaveFileFor (projectFile, {}, {}).deleteFile();
    changedSinceAutosave = dirty;
}

void MainComponent::offerRecovery()
{
    // An untitled project's autosave left behind: the app closed before it was saved.
    const auto leftovers = model::files::leftoverUntitledAutosaves();
    if (leftovers.isEmpty())
        return;

    const auto newest = leftovers.getFirst();
    messageBox = juce::AlertWindow::showScopedAsync (
        juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Recover your last session?",
            "Audio Tech Pro closed before \"" + newest.getFileNameWithoutExtension().upToLastOccurrenceOf (" ", false, false)
                + "\" was saved. Its autosave is from " + newest.getLastModificationTime().formatted ("%H:%M on %d %b") + ".",
            "Recover", "Delete It", "Later", this),
        [this, leftovers] (int result)
        {
            if (result == 1)
            {
                juce::String error;
                if (auto recovered = model::load (leftovers.getFirst(), error); recovered.isValid())
                {
                    setProject (recovered, {});
                    leftovers.getFirst().deleteFile();
                    markDirty();
                    autosave();   // safe again straight away, under this session
                }
                else
                {
                    showError ("Couldn't recover", error);
                }
            }
            else if (result == 2)
            {
                for (const auto& f : leftovers)
                    f.deleteFile();
            }
        });
}

void MainComponent::consolidate()
{
    if (projectFile == juce::File())
    {
        showError ("Save the project first", "Consolidate copies the audio into the project's folder, so the project needs one. Save it, then try again.");
        return;
    }

    undoManager.beginNewTransaction ("Consolidate");
    juce::String error;
    const auto copied = model::files::consolidate (project, projectFile, &undoManager, error);
    saveProject();
    statusLabel.setText (copied == 0 ? juce::String ("Everything was already in the project folder")
                                     : "Copied " + juce::String (copied) + (copied == 1 ? " audio file" : " audio files") + " into Audio Files",
                         juce::dontSendNotification);
    if (error.isNotEmpty())
        showError ("Consolidate", error);
}

void MainComponent::showBackups()
{
    if (projectFile == juce::File())
    {
        showError ("No backups yet", "Backups are made each time you save a project.");
        return;
    }

    const auto backups = model::files::backupsOf (projectFile);
    if (backups.isEmpty())
    {
        showError ("No backups yet", "A backup is made each time you save over the project, so there's one after your second save.");
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Revert to the version saved at");
    for (const auto& b : backups)
        menu.addItem (b.getFileNameWithoutExtension().fromFirstOccurrenceOf (projectFile.getFileNameWithoutExtension() + " ", false, false)
                          .replaceFirstOccurrenceOf (" ", ",  ").replace (".", ":"), [this, b]
        {
            confirmDiscardChanges ([this, b]
            {
                juce::String error;
                auto loaded = model::load (b, error);
                if (! loaded.isValid())
                {
                    showError ("Couldn't open the backup", error);
                    return;
                }
                loaded.setProperty (ids::name, projectFile.getFileNameWithoutExtension(), nullptr);
                setProject (loaded, projectFile);   // saving will put it back in place of the current version
                markDirty();
            });
        });
    menu.showMenuAsync (juce::PopupMenu::Options());
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

    // "4/4  Cmaj", as Logic's display shows them
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const auto root = ((int) project.getProperty (ids::keyRoot, 0) % 12 + 12) % 12;
    signatureLabel.setText (juce::String ((int) project.getProperty (ids::timeSigNumerator, 4)) + "/4  "
                            + names[root] + ((bool) project.getProperty (ids::keyMinor, false) ? "min" : "maj"),
                            juce::dontSendNotification);
    canvas.repaint();
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &signatureLabel)
        showSignatureMenu();
}

void MainComponent::showSignatureMenu()
{
    juce::PopupMenu menu, keys;
    menu.addSectionHeader ("Time signature");
    const int current = project.getProperty (ids::timeSigNumerator, 4);
    for (int beats : { 2, 3, 4, 5, 6, 7, 9, 12 })
        menu.addItem (juce::String (beats) + "/4", true, beats == current, [this, beats]
        {
            undoManager.beginNewTransaction ("Time signature");
            project.setProperty (ids::timeSigNumerator, beats, &undoManager);
        });

    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int root = project.getProperty (ids::keyRoot, 0);
    const bool minor = project.getProperty (ids::keyMinor, false);
    for (bool isMinor : { false, true })
    {
        for (int r = 0; r < 12; ++r)
            keys.addItem (juce::String (names[r]) + (isMinor ? " minor" : " major"), true, r == root && isMinor == minor, [this, r, isMinor]
            {
                undoManager.beginNewTransaction ("Key");
                project.setProperty (ids::keyRoot, r, &undoManager);
                project.setProperty (ids::keyMinor, isMinor, &undoManager);
            });
        keys.addSeparator();
    }
    menu.addSeparator();
    menu.addSubMenu ("Key: " + model::keyName (project), keys);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&signatureLabel));
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

    // With a marquee selection, split at both of its edges.
    if (timeline.hasMarquee())
    {
        for (const auto& id : timeline.marqueeTracks)
        {
            auto track = model::findById (project, id);
            juce::Array<juce::ValueTree> regions;
            for (const auto& r : track)
                if (r.hasType (ids::REGION))
                    regions.add (r);
            for (auto r : regions)
            {
                model::splitRegion (r, timeline.marqueeEnd, &undoManager);
                model::splitRegion (r, timeline.marqueeStart, &undoManager);
            }
        }
        return;
    }

    // The selected regions under the playhead; otherwise every region under the
    // playhead on the selected track.
    bool splitAny = false;
    for (auto& region : timeline.selectedRegionTrees())
        if ((double) region[ids::start] < time && time < model::regionEnd (region))
            splitAny = model::splitRegion (region, time, &undoManager).isValid() || splitAny;
    if (splitAny)
        return;

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
    // A marquee selection: cut that stretch out of the regions under it.
    if (timeline.hasMarquee())
    {
        undoManager.beginNewTransaction ("Delete marquee selection");
        for (const auto& id : timeline.marqueeTracks)
        {
            auto track = model::findById (project, id);
            juce::Array<juce::ValueTree> regions;
            for (const auto& r : track)
                if (r.hasType (ids::REGION))
                    regions.add (r);
            for (auto r : regions)
            {
                if (auto right = model::splitRegion (r, timeline.marqueeEnd, &undoManager); right.isValid())
                    juce::ignoreUnused (right);
                auto middle = model::splitRegion (r, timeline.marqueeStart, &undoManager);
                auto inside = middle.isValid() ? middle : r;
                if ((double) inside[ids::start] >= timeline.marqueeStart - 1.0e-6 && model::regionEnd (inside) <= timeline.marqueeEnd + 1.0e-6)
                    model::deleteRegion (inside, &undoManager);
            }
        }
        timeline.clearMarquee();
        timeline.changed();
        return;
    }

    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty())
        return;

    undoManager.beginNewTransaction (regions.size() > 1 ? "Delete regions" : "Delete region");
    juce::Array<juce::ValueTree> tracks;
    for (auto region : regions)
    {
        tracks.addIfNotAlreadyThere (region.getParent());
        model::deleteRegion (region, &undoManager);
    }
    canvas.applyDragMode ({}, tracks);   // Shuffle closes the gaps
    timeline.selectOnlyRegion ({});
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

        // Pro Tools / Live / FL Studio workflow: back to where playback started.
        if (stopReturnsToStart)
        {
            engine.setPlayheadSeconds (playStartedAt);
            canvas.repaint();
        }
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

    playStartedAt = engine.getPlayheadSeconds();
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

    // Autopunch: playback starts here (or a bar before the punch-in when we're past it); recording at the punch-in.
    auto playFrom = start;
    recordPunchOn = project[ids::autopunch];
    recordPunchIn = project.getProperty (ids::punchIn, 0.0);
    recordPunchOut = project.getProperty (ids::punchOut, 0.0);
    if (recordPunchOn && recordPunchOut > recordPunchIn)
    {
        const auto barLength = model::secondsPerBeat (project) * (int) project.getProperty (ids::timeSigNumerator, 4);
        if (! wasPlaying && (playFrom > recordPunchIn || playFrom < recordPunchIn - 4.0 * barLength))
            playFrom = juce::jmax (0.0, recordPunchIn - barLength);
        start = juce::jmax (recordPunchIn, wasPlaying ? engine.getPlayheadSeconds() : recordPunchIn);
    }
    else
    {
        recordPunchOn = false;
    }

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
        engine.setPlayheadSecondsAllowingNegative (countIn ? playFrom - barLength : playFrom);
        engine.play();
    }
}

void MainComponent::replaceUnder (const juce::ValueTree& region)
{
    // Replace mode: the new recording shortens, splits or removes what it covers.
    if (! (bool) project[ids::replaceMode] || ! region.isValid())
        return;
    model::clearOverlaps (region, [this] (const juce::ValueTree& r) { return fileCache.getLengthSeconds (juce::File (r[ids::file].toString())); },
                          &undoManager);
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

        // One pass: an ordinary region (cut at the punch-out with Autopunch).
        if (pieces.size() == 1)
        {
            const auto& p = pieces.front();
            auto length = p.length;
            if (recordPunchOn)
                length = juce::jmin (length, recordPunchOut - p.timelineStart);
            if (length < model::minRegionLength)
                continue;
            auto region = model::addRegion (track, take.file, p.timelineStart, length, &undoManager);
            region.setProperty (ids::offset, p.fileOffset, &undoManager);
            replaceUnder (region);
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
            replaceUnder (region);
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
    if (tree == project && (property == ids::tempo || property == ids::masterGain || property == ids::timeSigNumerator
                            || property == ids::keyRoot || property == ids::keyMinor))
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
        mixer.setSelectedTracks (timeline.selectedTrackIds(), timeline.selectedTrackId);   // one selection, as in Logic

        // The open editor follows the selection: the selected MIDI region, or the one on the
        // selected instrument track under the playhead (else its first).
        if (pianoRoll.isVisible())
        {
            auto region = model::findById (project, timeline.selectedRegionId);
            if (! (region.isValid() && model::isMidiRegion (region)))
            {
                region = {};
                const auto track = model::findById (project, timeline.selectedTrackId);
                if (track.isValid() && model::isInstrumentTrack (track) && pianoRoll.getRegion().getParent() != track)
                {
                    const auto t = engine.getPlayheadSeconds();
                    for (const auto& r : track)
                        if (model::isMidiRegion (r) && (! region.isValid() || ((double) r[ids::start] <= t && t < model::regionEnd (r))))
                            region = r;
                }
            }
            if (region.isValid() && region != pianoRoll.getRegion())
                pianoRoll.setRegion (region);
        }
        updateScrollBars();
        commandManager.commandStatusChanged(); // the selection changed what the Track commands act on
    }
    else if (source == &fileCache)
    {
        canvas.repaint();   // "loading" labels go away
    }
    else if (source == &rack.getKnownPlugins())
    {
        mixer.getConsolePanel().setCandidates (console::findBusCompressors (rack.getKnownPlugins().getTypes()));
        mixer.setStripCandidates (console::findChannelStrips (rack.getKnownPlugins().getTypes()));
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
        if (source == &engine.getDeviceManager())
            saveDeviceState();
        statusLabel.setText (deviceStatusText(), juce::dontSendNotification);
    }
}

void MainComponent::timerCallback()
{
    updateLiveMidiTrack();

    // Automation: passes start with playback (Write mode) and go into the project when it stops.
    if (! engine.isRenderingOffline())
    {
        automation.transportChanged (engine.isPlaying());
        automation.tick();
        headers.followAutomation (engine.getPlayheadSeconds());
    }

    handleIncomingMidi();

    // Autosave every two minutes while there are unsaved changes.
    if (const auto now = juce::Time::getMillisecondCounter();
        autosaveMinutes > 0 && now - lastAutosaveMs > (juce::uint32) autosaveMinutes * 60000u)
    {
        lastAutosaveMs = now;
        if (changedSinceAutosave)
            autosave();
    }
    if (inspector.isVisible())
        inspector.setTrack (project, model::findById (project, timeline.selectedTrackId)), inspector.setRegion (model::findById (project, timeline.selectedRegionId));
    if (smartControls != nullptr && smartControls->isVisible())
        smartControls->setTrack (project, model::findById (project, timeline.selectedTrackId));
    updateScreenRecording();

    if (pianoRoll.isVisible())
        pianoRoll.repaint();

    const auto levels = engine.takeLevels();
    inputMeter.push (levels.inL, levels.inR);
    outputMeter.push (levels.outL, levels.outR);
    mixer.setMasterLevels (levels.outL, levels.outR);
    mixerButton.setToggleState (mixer.isVisible(), juce::dontSendNotification);
    editorButton.setToggleState (pianoRoll.isVisible(), juce::dontSendNotification);

    // (While the song renders offline, the playhead races ahead: keep showing where it was.)
    const bool offline = engine.isRenderingOffline();
    const auto playhead = offline ? juce::jmax (0.0, lastPaintedPlayhead) : engine.getPlayheadSeconds();
    positionLabel.setText (formatPosition (playhead), juce::dontSendNotification);
    clockLabel.setText (formatClock (playhead), juce::dontSendNotification);

    const bool playing = engine.isPlaying() && ! offline;
    if (playing != playButton.getToggleState() || engine.isRecording() != recordButton.getToggleState())
        repaint (0, toolbarTopRow, getWidth(), toolbarHeight - toolbarTopRow);   // the captions under the keys
    playButton.setToggleState (playing, juce::dontSendNotification);
    recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
    clickButton.setToggleState ((bool) project[ids::metronome], juce::dontSendNotification);
    cycleButton.setToggleState ((bool) project[ids::cycleEnabled], juce::dontSendNotification);
    countInButton.setToggleState ((bool) project[ids::countIn], juce::dontSendNotification);
    punchButton.setToggleState ((bool) project[ids::autopunch], juce::dontSendNotification);
    replaceButton.setToggleState ((bool) project[ids::replaceMode], juce::dontSendNotification);

    // Autopunch: recording stops at the punch-out locator (the region is cut exactly there).
    if (engine.isRecording() && (bool) project[ids::autopunch] && ! recordCycleOn
        && engine.getPlayheadSeconds() >= (double) project.getProperty (ids::punchOut, 0.0))
        finishRecording();

    // While audio decodes in the background, say so in the status line.
    if (const bool loading = fileCache.isLoading(); loading || wasLoadingAudio)
    {
        statusLabel.setText (loading ? "Loading audio: " + fileCache.currentlyLoading() + "..." : deviceStatusText(),
                             juce::dontSendNotification);
        wasLoadingAudio = loading;
    }

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

    juce::ignoreUnused (beatsPerBar);
    const auto position = model::barPositionAt (project, seconds);
    const auto bar = position.bar, beat = position.beat, sixteenth = position.sixteenth;

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

    // Controllers and pitch bend you played come along too.
    struct Controller { int controller; double time, value; };
    std::vector<Controller> controllers;
    for (const auto& e : events)
    {
        const auto type = e.status & 0xf0;
        if (e.time < regionStart || e.time > regionEnd)
            continue;
        if (type == 0xb0 && e.data1 < 120)
            controllers.push_back ({ e.data1, e.time - regionStart, e.data2 / 127.0 });
        else if (type == 0xe0)
            controllers.push_back ({ model::pitchBend, e.time - regionStart, ((e.data2 << 7) | e.data1) / 16383.0 });
    }

    if (notes.empty() && controllers.empty())
        return;

    auto region = model::addMidiRegion (track, regionStart, regionEnd - regionStart, &undoManager);
    for (const auto& n : notes)
        model::addNote (region, n.pitch, n.start, n.length, n.velocity, &undoManager);
    for (const auto& c : controllers)
        model::addControllerEvent (region, c.controller, c.time, c.value, &undoManager);

    // Merge: played over a MIDI region, it joins it (otherwise, with Replace, it replaces what's under it).
    if ((bool) project[ids::midiMerge])
    {
        juce::Array<juce::ValueTree> overlapping { region };
        for (const auto& r : track)
            if (r != region && model::isMidiRegion (r) && (double) r[ids::start] < model::regionEnd (region) && model::regionEnd (r) > (double) region[ids::start])
                overlapping.add (r);
        if (overlapping.size() > 1)
            if (auto joined = model::joinRegions (overlapping, &undoManager); joined.isValid())
                region = joined;
    }
    else
    {
        replaceUnder (region);
    }

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
    menu.addItem ("Revert to Backup...", [this] { showBackups(); });
    menu.addItem ("Consolidate Project...", [this] { consolidate(); });
    menu.addSeparator();
    menu.addItem ("Import Audio...", [this] { importAudio(); });
    menu.addItem ("Import Tracks from Project...", [this] { importTracksFromProject(); });
    menu.addSeparator();
    menu.addItem ("Export Song as WAV...", [this] { exportSong(); });
    menu.addItem ("Export All Tracks as Audio Files...", [this] { exportStems(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton));
}

namespace
{
    /** Renders offline on a background thread with a progress window. */
    class ExportJob final : public juce::ThreadWithProgressWindow
    {
    public:
        using Work = std::function<juce::String (std::function<bool (double)>)>;

        ExportJob (AudioEngine& e, const juce::String& title, Work w, std::function<void (const juce::String&)> done)
            : ThreadWithProgressWindow (title, true, true),
              engine (e), work (std::move (w)), onDone (std::move (done)) {}

        ExportJob (AudioEngine& e, juce::File f, double s, double en, std::function<void (const juce::String&)> done)
            : ExportJob (e, "Exporting " + f.getFileName(),
                         [&e, f, s, en] (std::function<bool (double)> progress) { return e.renderOffline (f, s, en, 24, std::move (progress)); },
                         std::move (done)) {}

        void run() override
        {
            error = work ([this] (double p)
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
        Work work;
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

    if (fileCache.isLoading())
    {
        showError ("Still loading audio", "Some audio files are still loading. Try again in a moment.");
        return;
    }

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

//==============================================================================
// Stems, bounce in place, importing tracks
//==============================================================================
void MainComponent::exportStems()
{
    if (engine.isRecording())
        return;

    if (fileCache.isLoading())
    {
        showError ("Still loading audio", "Some audio files are still loading. Try again in a moment.");
        return;
    }

    const bool cycle = project[ids::cycleEnabled];
    const double start = cycle ? (double) project[ids::cycleStart] : 0.0;
    const double end = cycle ? (double) project[ids::cycleEnd] : model::projectEnd (project) + 2.0;

    if (! cycle && model::projectEnd (project) <= 0.0)
    {
        showError ("Nothing to export", "Record or import something first.");
        return;
    }

    const auto base = projectFile != juce::File() ? projectFile.getParentDirectory()
                                                  : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a folder for the track files", base, juce::String());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, start, end] (const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();
        if (chosen == juce::File())
            return;

        const auto folder = chosen.getChildFile (juce::File::createLegalFileName (project[ids::name].toString() + " Tracks"));

        // Every track as if nothing were soloed; muted tracks and empty tracks are left out.
        player.setIgnoreSolo (true);
        player.rebuildNow();

        std::vector<AudioEngine::StemTarget> targets;
        int number = 0;
        for (const auto& track : project)
        {
            if (! track.hasType (ids::TRACK))
                continue;
            ++number;
            if ((bool) track[ids::mute] || (! model::isBusTrack (track) && track.getNumChildren() == 0))
                continue;
            const auto slot = player.slotOf (track[ids::id].toString());
            if (slot < 0)
                continue;
            const auto name = juce::String (number).paddedLeft ('0', 2) + " " + track[ids::name].toString();
            targets.push_back ({ slot, folder.getChildFile (juce::File::createLegalFileName (name) + ".wav") });
        }

        if (targets.empty())
        {
            player.setIgnoreSolo (false);
            player.rebuildNow();
            showError ("Nothing to export", "Every track is empty or muted.");
            return;
        }

        engine.stop();
        engine.beginOfflineRender();

        auto& eng = engine;
        const auto count = (int) targets.size();
        auto* job = new ExportJob (engine, "Exporting " + juce::String (count) + " tracks",
            [&eng, targets, start, end] (std::function<bool (double)> progress)
            {
                return eng.renderStems (targets, start, end, 24, std::move (progress));
            },
            [safe = juce::Component::SafePointer<MainComponent> (this), folder, count] (const juce::String& error)
            {
                if (safe == nullptr)
                    return;

                safe->player.setIgnoreSolo (false);
                safe->player.rebuildNow();

                if (error.isEmpty())
                {
                    safe->messageBox = juce::AlertWindow::showScopedAsync (
                        juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::InfoIcon, "Export finished",
                                                                      "Saved " + juce::String (count) + " track files in " + folder.getFileName() + ".",
                                                                      "Show in Finder", "OK", safe.getComponent()),
                        [folder] (int result) { if (result == 1) folder.revealToUser(); });
                }
                else if (error != "cancelled")
                {
                    safe->showError ("Export failed", error);
                }
            });

        job->launchThread();
    });
}

void MainComponent::bounceInPlace()
{
    auto track = selectedTrack();
    if (! track.isValid() || engine.isRecording())
        return;

    if (model::isBusTrack (track))
    {
        showError ("Bounce in Place", "Choose an audio or instrument track: a bus has no regions of its own.");
        return;
    }

    if ((bool) track[ids::mute])
    {
        showError ("Bounce in Place", "The track is muted. Unmute it first.");
        return;
    }

    double start = std::numeric_limits<double>::max(), end = 0.0;
    for (const auto& region : track)
    {
        if (! region.hasType (ids::REGION) || model::isRegionMuted (region))
            continue;
        start = juce::jmin (start, (double) region[ids::start]);
        end = juce::jmax (end, model::regionEnd (region));
    }

    if (end <= start)
    {
        showError ("Bounce in Place", "The track has no regions to bounce.");
        return;
    }

    if (fileCache.isLoading())
    {
        showError ("Still loading audio", "Some audio files are still loading. Try again in a moment.");
        return;
    }

    end += 2.0;   // tails: reverb, delay, release
    const auto folder = recordingFolder();
    const auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (track[ids::name].toString() + " Bounce"), ".wav", false);

    player.setIgnoreSolo (true);
    player.rebuildNow();
    const auto slot = player.slotOf (track[ids::id].toString());
    if (slot < 0)
    {
        player.setIgnoreSolo (false);
        player.rebuildNow();
        return;
    }

    engine.stop();
    engine.beginOfflineRender();

    auto& eng = engine;
    std::vector<AudioEngine::StemTarget> targets { { slot, file } };
    const auto trackId = track[ids::id].toString();

    auto* job = new ExportJob (engine, "Bouncing " + track[ids::name].toString(),
        [&eng, targets, start, end] (std::function<bool (double)> progress)
        {
            return eng.renderStems (targets, start, end, 24, std::move (progress));
        },
        [safe = juce::Component::SafePointer<MainComponent> (this), file, start, end, trackId] (const juce::String& error)
        {
            if (safe == nullptr)
                return;

            auto& self = *safe;
            self.player.setIgnoreSolo (false);

            if (error.isNotEmpty())
            {
                self.player.rebuildNow();
                if (error != "cancelled")
                    self.showError ("Bounce failed", error);
                return;
            }

            auto source = model::findById (self.project, trackId);
            if (! source.isValid())
                return;

            self.undoManager.beginNewTransaction ("Bounce in Place");
            auto bounced = model::addTrack (self.project, source[ids::name].toString() + " Bounce", &self.undoManager,
                                            self.project.indexOf (source) + 1, model::audioKind);
            model::setStripPlugin (self.project, bounced[ids::id].toString(), {}, &self.undoManager);   // already through the strip
            bounced.setProperty (ids::colour, source[ids::colour], &self.undoManager);
            model::addRegion (bounced, file, start, end - start, &self.undoManager);
            source.setProperty (ids::mute, true, &self.undoManager);

            self.timeline.selectOnly (bounced[ids::id].toString());
            self.timeline.changed();
            self.player.rebuildNow();
        });

    job->launchThread();
}

void MainComponent::toggleFreeze (juce::ValueTree track)
{
    if (! track.isValid() || engine.isRecording())
        return;

    // Unfreeze: back to the regions and plugins (the frozen file goes).
    if ((bool) track[ids::frozen])
    {
        juce::File (track[ids::frozenFile].toString()).deleteFile();
        track.removeProperty (ids::frozen, nullptr);
        track.removeProperty (ids::frozenFile, nullptr);
        player.rebuildNow();
        return;
    }

    if (model::isBusTrack (track) || model::isVcaTrack (track))
        return;

    if (fileCache.isLoading())
    {
        showError ("Still loading audio", "Some audio files are still loading. Try again in a moment.");
        return;
    }

    const auto end = model::projectEnd (project) + 3.0;   // and the tails
    const auto file = recordingFolder().getChildFile ("Freeze Files")
                          .getNonexistentChildFile (juce::File::createLegalFileName (track[ids::name].toString() + " Freeze"), ".wav", false);

    // Everything plays (solo ignored); the frozen part is what comes out of the plugins, before the fader.
    player.setIgnoreSolo (true);
    player.rebuildNow();
    const auto slot = player.slotOf (track[ids::id].toString());
    if (slot < 0)
    {
        player.setIgnoreSolo (false);
        player.rebuildNow();
        return;
    }

    engine.stop();
    engine.beginOfflineRender();
    auto& eng = engine;
    std::vector<AudioEngine::StemTarget> targets { { slot, file } };
    const auto trackId = track[ids::id].toString();

    auto* job = new ExportJob (engine, "Freezing " + track[ids::name].toString(),
        [&eng, targets, end] (std::function<bool (double)> progress)
        {
            return eng.renderStems (targets, 0.0, end, 24, std::move (progress), true);
        },
        [safe = juce::Component::SafePointer<MainComponent> (this), file, trackId] (const juce::String& error)
        {
            if (safe == nullptr)
                return;
            safe->player.setIgnoreSolo (false);
            if (error.isEmpty())
                if (auto t = model::findById (safe->project, trackId); t.isValid())
                {
                    t.setProperty (ids::frozenFile, file.getFullPathName(), nullptr);
                    t.setProperty (ids::frozen, true, nullptr);
                }
            safe->player.rebuildNow();
            if (error.isNotEmpty() && error != "cancelled")
                safe->showError ("Freeze failed", error);
        });
    job->launchThread();
}

void MainComponent::importTracksFromProject()
{
    if (engine.isRecording())
        return;

    const auto folder = projectFile != juce::File() ? projectFile.getParentDirectory().getParentDirectory()
                                                    : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Import tracks from a project", folder, "*" + model::fileExtension + ";*.riff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        auto source = model::load (file, error);
        if (! source.isValid())
        {
            showError ("Couldn't open the project", error);
            return;
        }

        if (model::numTracks (source) == 0)
        {
            showError ("No tracks", file.getFileNameWithoutExtension() + " has no tracks.");
            return;
        }

        auto picker = std::make_unique<TrackPicker> (source, "Import", [this, source] (const juce::StringArray& chosen)
        {
            undoManager.beginNewTransaction ("Import tracks");
            const auto added = model::importTracks (project, source, chosen, &undoManager);
            if (! added.isEmpty())
            {
                timeline.selectOnly (added.getFirst()[ids::id].toString());
                timeline.changed();
                markDirty();
            }
        });

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (picker.release());
        options.dialogTitle = "Import tracks from " + file.getFileNameWithoutExtension();
        options.dialogBackgroundColour = theme::background;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    });
}

void MainComponent::showAudioMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Audio device settings...", [this] { showAudioSettings(); });
    menu.addItem ("Calibrate recording latency...", [this] { showLatencyCalibration(); });
    menu.addCommandItem (&commandManager, commands::combineInterfaces);
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
        engine.getDeviceManager(), 0, 256, 2, 256, true, false, true, false);
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
