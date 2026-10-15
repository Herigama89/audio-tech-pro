// The main window's commands: what each menu item and key command does, and
// the menu bar. (Split from MainComponent.cpp to keep that file readable.)

#include "MainComponent.h"
#include "ui/ToolChooser.h"
#include "ui/ToolIcons.h"
#include "model/Project.h"
#include "ui/Commands.h"
#include "ui/KeyCommands.h"
#include "ui/GeneratorPanel.h"
#include "ui/Theme.h"
#include "AppInfo.h"

namespace riff
{

//==============================================================================
// Command list
//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    for (const auto& info : commands::all())
        ids.add (info.id);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto* c = commands::find (id);
    if (c == nullptr)
        return;

    info.setInfo (c->name, c->description, c->category, 0);

    for (const auto& key : commands::keysFor (keyPreset, id))
        info.addDefaultKeypress (key.getKeyCode(), key.getModifiers());

    const auto track = selectedTrack();
    const auto region = model::findById (project, timeline.selectedRegionId);
    const bool recording = engine.isRecording();

    switch (id)
    {
        case commands::undo:            info.setActive (undoManager.canUndo()); break;
        case commands::redo:            info.setActive (undoManager.canRedo()); break;
        case commands::deleteSelection: info.setActive (region.isValid() || timeline.hasMarquee()); break;
        case commands::cutRegions:
        case commands::copyRegions:
        case commands::repeatRegions:
        case commands::muteRegions:
        case commands::nudgeLeft:
        case commands::nudgeRight:
        case commands::trimStartToPlayhead:
        case commands::trimEndToPlayhead:
        case commands::loopRegions:
        case commands::regionGain:
        case commands::removeFades:     info.setActive (region.isValid() && ! recording); break;
        case commands::pasteRegions:    info.setActive (! clipboardRegions.isEmpty() && track.isValid() && ! recording); break;
        case commands::deleteMarker:    info.setActive (project.getChildWithName (ids::MARKERS).getChildWithProperty (ids::id, timeline.selectedMarkerId).isValid()); break;
        case commands::joinRegions:     info.setActive (timeline.selectedRegionTrees().size() > 1 && ! recording); break;
        case commands::duplicateTrack:
        case commands::createTrackStack:
        case commands::createFolderStack:
        case commands::createSummingStack:
        case commands::newVca:          info.setActive (track.isValid() && ! recording); break;
        case commands::freezeTracks:    info.setActive (track.isValid() && ! recording && ! model::isBusTrack (track) && ! model::isVcaTrack (track));
                                        info.setTicked ((bool) track[ids::frozen]); break;
        case commands::quantize:        info.setActive (region.isValid() && model::isMidiRegion (region)); break;
        case commands::setLocatorsBySelection: info.setActive (region.isValid() && ! recording); break;
        case commands::exportSong:
        case commands::exportStems:
        case commands::importTracks:    info.setActive (! recording); break;
        case commands::importProject:   info.setActive (! recording); break;
        case commands::bounceInPlace:   info.setActive (track.isValid() && ! recording); break;
        case commands::generateMidi:    info.setActive (! recording); break;

        case commands::deleteTrack:     info.setActive (track.isValid() && ! recording); break;
        case commands::muteTrack:       info.setActive (track.isValid()); info.setTicked ((bool) track[ids::mute]); break;
        case commands::soloTrack:       info.setActive (track.isValid()); info.setTicked ((bool) track[ids::solo]); break;
        case commands::armTrack:        info.setActive (track.isValid() && ! model::isBusTrack (track));
                                        info.setTicked ((bool) track[ids::armed]); break;
        case commands::showAutomation:  info.setActive (track.isValid()); info.setTicked ((bool) track[ids::showAutomation]); break;

        case commands::toggleCycle:     info.setTicked ((bool) project[ids::cycleEnabled]); info.setActive (! recording); break;
        case commands::toggleMetronome: info.setTicked ((bool) project[ids::metronome]); break;
        case commands::toggleCountIn:   info.setTicked ((bool) project[ids::countIn]); break;
        case commands::toggleAutopunch: info.setTicked ((bool) project[ids::autopunch]); break;
        case commands::toggleReplace:   info.setTicked ((bool) project[ids::replaceMode]); break;
        case commands::toggleStopOnClip: info.setTicked (engine.getStopOnClip()); break;
        case commands::toggleMidiMerge: info.setTicked ((bool) project[ids::midiMerge]); break;
        case commands::eventList:       info.setActive (region.isValid() && model::isMidiRegion (region)); break;

        case commands::showMixer:       info.setTicked (mixer.isVisible()); break;
        case commands::openMixerWindow: info.setTicked (mixerWindow != nullptr); break;
        case commands::openEditorWindow: info.setTicked (editorWindow != nullptr); break;
        case commands::showHiddenTracks: info.setTicked (timeline.showHiddenTracks); break;
        case commands::hideTrack:       info.setActive (track.isValid()); break;
        case commands::trackColours:    info.setActive (track.isValid()); break;
        case commands::toggleInspector: info.setTicked (inspector.isVisible()); break;
        case commands::toggleControlBar: info.setTicked (showControlBar); break;
        case commands::toggleToolbar:   info.setTicked (showToolbarRow); break;
        case commands::showNotepad:     info.setTicked (notepadPanel != nullptr && notepadPanel->isVisible()); break;
        case commands::showBrowser:     info.setTicked (browserPanel != nullptr && browserPanel->isVisible()); break;
        case commands::toggleQuickHelp: info.setTicked (quickHelpShown); break;
        case commands::waveformZoom:    info.setTicked (timeline.waveformZoom > 1.0f); break;
        case commands::toggleStatusBar: info.setTicked (showStatusBar); break;
        case commands::toggleMeters:    info.setTicked (showMeters); break;
        case commands::toggleTrackHeaders: info.setTicked (showTrackHeaders); break;
        case commands::toggleHeaderInserts: info.setTicked (TrackHeader::showInserts); break;
        case commands::toggleMixerComments: info.setTicked (mixer.getShowComments()); break;
        case commands::toggleExclusiveSolo: info.setTicked (appProperties.getUserSettings()->getBoolValue ("exclusiveSolo", false)); break;
        case commands::showLibrary:     info.setTicked (libraryPanel != nullptr && libraryPanel->isVisible()); break;
        case commands::showSmartControls: info.setTicked (smartControls != nullptr && smartControls->isVisible()); break;
        case commands::recordScreen:    info.setTicked (screenRecorder != nullptr && screenRecorder->isRecording());
                                        if (screenRecorder != nullptr && screenRecorder->isRecording())
                                            info.shortName = "Stop Screen Recording";
                                        break;
        case commands::showAssistant:   info.setTicked (assistantPanel != nullptr && assistantPanel->isVisible()); break;
        case commands::showPianoRoll:   info.setTicked (pianoRoll.isVisible()); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case commands::newProject:             newProject(); break;
        case commands::openProject:            openProject(); break;
        case commands::saveProject:            saveProject(); break;
        case commands::saveProjectAs:          saveProjectAs(); break;
        case commands::importAudio:            importAudio(); break;
        case commands::consolidateProject:     consolidate(); break;
        case commands::revertToBackup:         showBackups(); break;
        case commands::exportSong:             exportSong(); break;
        case commands::splitByLocators:
        case commands::cutSectionBetweenLocators:
        case commands::insertSilenceBetweenLocators:
        case commands::repeatSectionBetweenLocators:
        {
            const double a = project.getProperty (ids::cycleStart, 0.0), b = project.getProperty (ids::cycleEnd, 0.0);
            if (b <= a)
            {
                showError ("No locators", "Set the cycle range first: drag in the top half of the ruler.");
                break;
            }
            undoManager.beginNewTransaction (commands::find (info.commandID)->name);
            if (info.commandID == commands::splitByLocators)              model::splitAtTimes (project, a, b, &undoManager);
            else if (info.commandID == commands::cutSectionBetweenLocators) model::deleteSection (project, a, b, &undoManager);
            else if (info.commandID == commands::insertSilenceBetweenLocators) model::insertSilence (project, a, b, &undoManager);
            else                                                           model::duplicateSection (project, a, b, &undoManager);
            timeline.changed();
            break;
        }
        case commands::trimStartToPlayhead:    trimSelectedToPlayhead (true); break;
        case commands::trimEndToPlayhead:      trimSelectedToPlayhead (false); break;
        case commands::loopRegions:            loopSelectedRegions(); break;
        case commands::regionGain:             if (auto r = model::findById (project, timeline.selectedRegionId); r.isValid()) canvas.showGainPopup (r); break;
        case commands::removeFades:
        {
            undoManager.beginNewTransaction ("Remove fades");
            for (auto r : timeline.selectedRegionTrees())
            {
                r.setProperty (ids::fadeIn, 0.0, &undoManager);
                r.setProperty (ids::fadeOut, 0.0, &undoManager);
            }
            break;
        }
        case commands::exportStems:            exportStems(); break;
        case commands::bounceInPlace:          bounceInPlace(); break;
        case commands::importTracks:           importTracksFromProject(); break;
        case commands::importProject:          openOtherProject(); break;

        case commands::undo:                   undo(); break;
        case commands::redo:                   redo(); break;
        case commands::deleteSelection:        deleteSelection(); break;
        case commands::splitAtPlayhead:        splitAtPlayhead(); break;
        case commands::quantize:               quantizeSelection(); break;
        case commands::setLocatorsBySelection: setLocatorsBySelection(); break;
        case commands::cutRegions:             copySelectedRegions (true); break;
        case commands::copyRegions:            copySelectedRegions (false); break;
        case commands::pasteRegions:           pasteRegions(); break;
        case commands::selectAllRegions:       selectAllRegions(); break;
        case commands::repeatRegions:          repeatSelectedRegions(); break;
        case commands::joinRegions:            joinSelectedRegions(); break;
        case commands::muteRegions:            muteSelectedRegions(); break;
        case commands::nudgeLeft:              nudgeSelectedRegions (-1); break;
        case commands::nudgeRight:             nudgeSelectedRegions (1); break;
        case commands::toolMenu:               showToolMenu(); break;
        case commands::createMarker:
        {
            undoManager.beginNewTransaction ("New marker");
            const auto marker = model::addMarker (project, engine.getPlayheadSeconds(), {}, &undoManager);
            timeline.selectedMarkerId = marker[ids::id].toString();
            timeline.changed();
            break;
        }
        case commands::nextMarker:
        case commands::previousMarker:
            if (const auto m = model::adjacentMarker (project, engine.getPlayheadSeconds(), info.commandID == commands::nextMarker); m.isValid())
            {
                engine.setPlayheadSeconds ((double) m[ids::time]);
                timeline.selectedMarkerId = m[ids::id].toString();
                timeline.changed();
            }
            break;
        case commands::deleteMarker:
            if (auto m = project.getChildWithName (ids::MARKERS).getChildWithProperty (ids::id, timeline.selectedMarkerId); m.isValid())
            {
                undoManager.beginNewTransaction ("Delete marker");
                m.getParent().removeChild (m, &undoManager);
                timeline.selectedMarkerId = {};
                timeline.changed();
            }
            break;
        case commands::duplicateTrack:         duplicateSelectedTrack(); break;
        case commands::newDrummerTrack:
        case commands::newBassPlayerTrack:
        case commands::newKeysPlayerTrack:
        {
            const auto what = info.commandID == commands::newDrummerTrack ? "drums" : info.commandID == commands::newBassPlayerTrack ? "bass" : "keys";
            undoManager.beginNewTransaction (commands::find (info.commandID)->name);
            // From the bar at the playhead, eight bars (drag the region's end for more).
            const auto bar = model::moveByBars (project, engine.getPlayheadSeconds() + 1.0e-6, 0);
            auto track = generator::player::addPlayerTrack (project, what, bar, 8, &undoManager);
            timeline.selectOnly (track[ids::id].toString());
            timeline.changed();
            if (auto region = track.getChild (0); region.isValid())
                openPianoRoll (region);
            break;
        }
        case commands::createFolderStack:      createStackFromSelection (false); break;
        case commands::createSummingStack:     createStackFromSelection (true); break;
        case commands::freezeTracks:           if (auto t = selectedTrack(); t.isValid()) toggleFreeze (t); break;
        case commands::newVca:                 newVcaForSelection(); break;

        case commands::newAudioTrack:          addTrack (model::audioKind); break;
        case commands::newInstrumentTrack:     addTrack (model::instrumentKind); break;
        case commands::newAuxBus:              addTrack (model::busKind); break;
        case commands::deleteTrack:            deleteSelectedTrack(); break;
        case commands::muteTrack:              toggleTrackFlag (ids::mute, "Mute"); break;
        case commands::soloTrack:              toggleTrackFlag (ids::solo, "Solo"); break;
        case commands::armTrack:               toggleTrackFlag (ids::armed, {}); break;
        case commands::showAutomation:         toggleTrackFlag (ids::showAutomation, {}); break;
        case commands::hideTrack:              hideSelectedTracks(); break;
        case commands::showHiddenTracks:       timeline.showHiddenTracks = ! timeline.showHiddenTracks; timeline.changed(); break;
        case commands::trackColours:           headers.showColoursForSelection(); break;
        case commands::toggleInspector:        toggleInspector(); break;
        case commands::createTrackStack:       showCreateStackDialog(); break;
        case commands::showNotepad:            toggleNotepad(); break;
        case commands::showPerformance:        showPerformanceWindow(); break;
        case commands::audioSuite:             showAudioSuite(); break;
        case commands::showBrowser:            toggleBrowser(); break;
        case commands::toggleQuickHelp:        quickHelpShown = ! quickHelpShown; quickHelpBox.setVisible (quickHelpShown); quickHelpBox.toFront (false); lastHelpTarget = nullptr; updateQuickHelp(); break;
        case commands::waveformZoom:
            timeline.waveformZoom = timeline.waveformZoom >= 4.0f ? 1.0f : timeline.waveformZoom * 2.0f;
            waveformZoomButton.setButtonText (timeline.waveformZoom > 1.0f ? juce::String ((int) timeline.waveformZoom) + "x" : juce::String::fromUTF8 ("\xe2\x88\xbf"));
            canvas.repaint();
            break;
        case commands::zoomToFitAll:           zoomToFit (true, true); break;
        case commands::toggleControlBar:       setPanelShown ("viewControlBar", showControlBar, ! showControlBar); break;
        case commands::toggleToolbar:          setPanelShown ("viewFunctionToolbar", showToolbarRow, ! showToolbarRow); break;
        case commands::toggleStatusBar:        setPanelShown ("viewStatusBar", showStatusBar, ! showStatusBar); break;
        case commands::toggleMeters:           setPanelShown ("viewMeters", showMeters, ! showMeters); break;
        case commands::toggleTrackHeaders:     setPanelShown ("viewTrackHeaders", showTrackHeaders, ! showTrackHeaders); break;
        case commands::showMultiMeter:         showMultiMeter(); break;
        case commands::toggleMixerComments:
            mixer.setShowComments (! mixer.getShowComments());
            appProperties.getUserSettings()->setValue ("mixerComments", mixer.getShowComments());
            if (! mixer.isVisible() && mixer.getShowComments())
                showMixer();
            commandManager.commandStatusChanged();
            break;
        case commands::toggleExclusiveSolo:
        {
            auto* settings = appProperties.getUserSettings();
            settings->setValue ("exclusiveSolo", ! settings->getBoolValue ("exclusiveSolo", false));
            appProperties.saveIfNeeded();
            commandManager.commandStatusChanged();
            break;
        }
        case commands::toggleHeaderInserts:
            TrackHeader::showInserts = ! TrackHeader::showInserts;
            appProperties.getUserSettings()->setValue ("viewHeaderInserts", TrackHeader::showInserts);
            for (auto* c : headers.getChildren())
                c->resized();
            headers.resized();
            resized();
            break;
        case commands::showAllPanels:
        case commands::hideAllPanels:
        {
            // Logic's "everything around the tracks": the Inspector, bars, meters, Smart Controls and the Library.
            const bool show = info.commandID == commands::showAllPanels;
            if (inspector.isVisible() != show)
                toggleInspector();
            if (! show && libraryPanel != nullptr && libraryPanel->isVisible())
                commandManager.invokeDirectly (commands::showLibrary, false);
            if ((smartControls != nullptr && smartControls->isVisible()) != show)
                commandManager.invokeDirectly (commands::showSmartControls, false);
            for (auto [key, flag] : { std::pair<const char*, bool*> { "viewControlBar", &showControlBar }, { "viewFunctionToolbar", &showToolbarRow },
                                      { "viewStatusBar", &showStatusBar }, { "viewMeters", &showMeters } })
            {
                *flag = show;
                appProperties.getUserSettings()->setValue (key, show);
            }
            applyPanelVisibility();
            break;
        }
        case commands::selectPreviousTrack:    selectAdjacentTrack (-1); break;
        case commands::selectNextTrack:        selectAdjacentTrack (1); break;

        case commands::play:                   togglePlay(); break;
        case commands::record:                 toggleRecord(); break;
        case commands::goToStart:              returnToStart(); break;
        case commands::rewindBar:              moveByBars (-1); break;
        case commands::forwardBar:             moveByBars (1); break;
        case commands::toggleCycle:            toggleCycle(); break;
        case commands::toggleMetronome:        toggleMetronome(); break;
        case commands::toggleCountIn:          project.setProperty (ids::countIn, ! (bool) project[ids::countIn], nullptr); break;
        case commands::toggleAutopunch:        toggleAutopunch(); break;
        case commands::toggleReplace:          project.setProperty (ids::replaceMode, ! (bool) project[ids::replaceMode], nullptr); break;
        case commands::toggleStopOnClip:
            engine.setStopOnClip (! engine.getStopOnClip());
            appProperties.getUserSettings()->setValue ("stopOnClip", engine.getStopOnClip());
            clipStopButton.setToggleState (engine.getStopOnClip(), juce::dontSendNotification);
            break;
        case commands::toggleMidiMerge:        project.setProperty (ids::midiMerge, ! (bool) project[ids::midiMerge], nullptr); break;
        case commands::undoHistory:            showUndoHistory(); break;
        case commands::preferences:            showPreferences(); break;
        case commands::saveAsTemplate:         saveAsTemplate(); break;
        case commands::newAlternative:         newAlternative(); break;
        case commands::eventList:              showEventList(); break;
        case commands::liveLoops:              showLiveLoops(); break;
        case commands::themeSettings:          showThemeSettings(); break;
        case commands::openMixerWindow:        mixerWindow != nullptr ? dockMixer() : openMixerWindow(); break;
        case commands::openEditorWindow:       editorWindow != nullptr ? dockEditor() : openEditorWindow(); break;
        case commands::controllerAssignments:  showControllerAssignments(); break;
        case commands::showLibrary:            toggleLibrary (LibraryPanel::Page::patches); break;
        case commands::showSmartControls:      toggleSmartControls(); break;
        case commands::groupSettings:          showGroupSettings(); break;
        case commands::ioLabels:               showIOLabels(); break;
        case commands::signalFlow:             showSignalFlow(); break;
        case commands::showLoopBrowser:        toggleLibrary (LibraryPanel::Page::loops); break;

        case commands::showMixer:              toggleMixer(); break;
        case commands::showPianoRoll:          togglePianoRoll(); break;
        case commands::zoomIn:                 zoomHorizontally (1.5); break;
        case commands::zoomOut:                zoomHorizontally (1.0 / 1.5); break;
        case commands::zoomVerticalIn:         zoomVertically (1); break;
        case commands::zoomVerticalOut:        zoomVertically (-1); break;
        case commands::keyCommands:            showKeyCommands(); break;
        case commands::generateMidi:           showGenerator(); break;
        case commands::showAssistant:          toggleAssistant(); break;
        case commands::recordScreen:           toggleScreenRecording(); break;
        case commands::combineInterfaces:      showCombineInterfaces(); break;
        case commands::audioSettings:          showAudioSettings(); break;
        case commands::calibrateLatency:       showLatencyCalibration(); break;
        case commands::scanPlugins:            showPluginScanner(); break;
        default:                               return false;
    }

    return true;
}

//==============================================================================
// Menu bar (Logic's menu names)
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Navigate", "Record", "Mix", "View", "AudioSuite", "Window" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;
    auto add = [&] (std::initializer_list<juce::CommandID> list)
    {
        for (auto id : list)
            menu.addCommandItem (&commandManager, id);
    };

    switch (index)
    {
        case 0:
            add ({ commands::newProject, commands::openProject, commands::importProject });
            addRecentItems (menu);
            addTemplateAndAlternativeItems (menu);
            menu.addSeparator();
            add ({ commands::saveProject, commands::saveProjectAs, commands::saveAsTemplate, commands::newAlternative, commands::revertToBackup });
            menu.addSeparator();
            add ({ commands::consolidateProject });
            menu.addSeparator();
            add ({ commands::importAudio, commands::importTracks });
            menu.addSeparator();
            add ({ commands::exportSong, commands::exportStems });
            break;

        case 1:
            add ({ commands::undo, commands::redo });
            menu.addSeparator();
            add ({ commands::cutRegions, commands::copyRegions, commands::pasteRegions, commands::deleteSelection, commands::selectAllRegions });
            menu.addSeparator();
            add ({ commands::splitAtPlayhead, commands::repeatRegions, commands::joinRegions, commands::muteRegions,
                   commands::nudgeLeft, commands::nudgeRight, commands::quantize });
            menu.addSeparator();
            add ({ commands::trimStartToPlayhead, commands::trimEndToPlayhead, commands::loopRegions, commands::regionGain, commands::removeFades });
            menu.addSeparator();
            add ({ commands::splitByLocators, commands::cutSectionBetweenLocators, commands::insertSilenceBetweenLocators, commands::repeatSectionBetweenLocators });
            menu.addSeparator();
            add ({ commands::toolMenu });
            break;

        case 2:
            add ({ commands::newAudioTrack, commands::newInstrumentTrack, commands::newAuxBus, commands::newVca, commands::duplicateTrack });
            menu.addSeparator();
            add ({ commands::newDrummerTrack, commands::newBassPlayerTrack, commands::newKeysPlayerTrack });
            menu.addSeparator();
            add ({ commands::createTrackStack, commands::createFolderStack, commands::createSummingStack, commands::freezeTracks });
            menu.addSeparator();
            add ({ commands::generateMidi, commands::bounceInPlace });
            menu.addSeparator();
            add ({ commands::muteTrack, commands::soloTrack, commands::armTrack, commands::showAutomation });
            menu.addSeparator();
            add ({ commands::trackColours, commands::hideTrack, commands::showHiddenTracks });
            menu.addSeparator();
            add ({ commands::selectPreviousTrack, commands::selectNextTrack });
            menu.addSeparator();
            add ({ commands::deleteTrack });
            break;

        case 3:
            add ({ commands::play, commands::goToStart, commands::rewindBar, commands::forwardBar });
            menu.addSeparator();
            add ({ commands::createMarker, commands::nextMarker, commands::previousMarker, commands::deleteMarker });
            menu.addSeparator();
            add ({ commands::toggleCycle, commands::setLocatorsBySelection });
            break;

        case 4:
            add ({ commands::record, commands::toggleCountIn, commands::toggleMetronome });
            menu.addSeparator();
            add ({ commands::toggleAutopunch, commands::toggleReplace, commands::toggleMidiMerge, commands::toggleStopOnClip });
            menu.addSeparator();
            add ({ commands::calibrateLatency });
            break;

        case 5:
            add ({ commands::showMixer, commands::openMixerWindow, commands::signalFlow });
            menu.addSeparator();
            add ({ commands::newAuxBus, commands::newVca, commands::createFolderStack, commands::createSummingStack });
            menu.addSeparator();
            add ({ commands::groupSettings, commands::ioLabels, commands::controllerAssignments });
            menu.addSeparator();
            add ({ commands::toggleExclusiveSolo, commands::showMultiMeter, commands::toggleMixerComments });
            menu.addSeparator();
            add ({ commands::scanPlugins });
            {
                // Which formats the plugin menus offer (VST3 only, say, so each plugin shows once).
                juce::PopupMenu formats;
                const auto shown = shownPluginFormats();
                const auto set = [this] (juce::StringArray f) { setShownPluginFormats (f); };
                formats.addItem ("All Formats (VST3, AU, CLAP)", true, shown.size() >= 3, [set] { set ({ "VST3", "AudioUnit", "CLAP" }); });
                formats.addItem ("VST3 Only", true, shown == juce::StringArray { "VST3" }, [set] { set ({ "VST3" }); });
                formats.addItem ("Audio Units Only", true, shown == juce::StringArray { "AudioUnit" }, [set] { set ({ "AudioUnit" }); });
                formats.addSeparator();
                for (const auto* f : { "VST3", "AudioUnit", "CLAP" })
                    formats.addItem (juce::String (f) == "AudioUnit" ? juce::String ("AU") : juce::String (f), true, shown.contains (f), [this, f, shown]
                    {
                        auto next = shown;
                        if (next.contains (f)) next.removeString (f); else next.add (f);
                        if (! next.isEmpty())
                            setShownPluginFormats (next);
                    });
                menu.addSubMenu ("Plugin Formats in Menus", formats);
            }
            break;

        case 6:
            // As in Logic: show or hide each panel of the main window.
            add ({ commands::toggleControlBar, commands::toggleToolbar, commands::toggleInspector, commands::showSmartControls,
                   commands::showLibrary, commands::showMixer, commands::showPianoRoll, commands::showLoopBrowser,
                   commands::toggleTrackHeaders, commands::toggleHeaderInserts, commands::toggleMeters, commands::toggleStatusBar });
            menu.addSeparator();
            add ({ commands::showAllPanels, commands::hideAllPanels });
            menu.addSeparator();
            add ({ commands::showNotepad, commands::showBrowser, commands::toggleQuickHelp });
            menu.addSeparator();
            add ({ commands::undoHistory, commands::eventList, commands::liveLoops });
            menu.addSeparator();
            add ({ commands::openMixerWindow, commands::openEditorWindow, commands::themeSettings });
            menu.addSeparator();
            add ({ commands::zoomIn, commands::zoomOut, commands::zoomVerticalIn, commands::zoomVerticalOut, commands::zoomToFitAll, commands::waveformZoom });
            menu.addSeparator();
            add ({ commands::showAssistant, commands::recordScreen });
            menu.addSeparator();
            add ({ commands::preferences, commands::keyCommands, commands::controllerAssignments, commands::audioSettings, commands::combineInterfaces });
            break;

        case 7:
            // Pro Tools' AudioSuite menu: every effect by kind; choosing one opens it for the selected regions.
            addAudioSuiteItems (menu);
            break;

        case 8:
            // Logic's Window menu: the windows you can open beside the main one.
            add ({ commands::openMixerWindow, commands::openEditorWindow });
            menu.addSeparator();
            add ({ commands::eventList, commands::undoHistory, commands::liveLoops, commands::signalFlow, commands::showPerformance });
            menu.addSeparator();
            add ({ commands::keyCommands, commands::controllerAssignments, commands::audioSettings, commands::themeSettings });
            break;

        default:
            break;
    }

    return menu;
}

//==============================================================================
// Key Commands window
//==============================================================================
void MainComponent::showKeyCommands()
{
    if (keyCommandsWindow != nullptr)
    {
        keyCommandsWindow->toFront (true);
        return;
    }

    auto* panel = new KeyCommandsPanel (*commandManager.getKeyMappings(), keyPreset,
                                        [this] (const juce::String& preset) { applyKeyPreset (preset); },
                                        stopReturnsToStart,
                                        [this] (bool on) { setStopReturnsToStart (on); });

    keyCommandsWindow = std::make_unique<EditorWindow> ("Key Commands", panel, true, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->keyCommandsWindow.reset();
        });
    });
}

//==============================================================================
// MIDI generator
//==============================================================================
void MainComponent::showGenerator()
{
    if (generatorWindow != nullptr)
    {
        generatorWindow->toFront (true);
        return;
    }

    // Start in the song's key when it has one.
    if (project.hasProperty (ids::keyRoot))
    {
        generatorSettings.root = project[ids::keyRoot];
        generatorSettings.minor = project[ids::keyMinor];
    }

    auto* panel = new GeneratorPanel (generatorSettings, generatorUsesTempo,
                                      [this] (const GeneratorPanel::Request& request) { generateMidi (request); });

    generatorWindow = std::make_unique<EditorWindow> ("MIDI Generator", panel, false, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->generatorWindow.reset();
        });
    });
}

void MainComponent::generateMidi (const GeneratorPanel::Request& request)
{
    if (engine.isRecording())
        return;

    const auto& s = request.settings;
    if (! (s.drums || s.bass || s.chords || s.melody))
    {
        statusLabel.setText ("Turn on at least one part to generate", juce::dontSendNotification);
        return;
    }

    generatorSettings = s;
    generatorUsesTempo = request.useGenreTempo;
    const juce::String undoName ("Generate MIDI");

    // "Try another": take back the last take, if it's still the last thing done.
    if (request.replaceLast && ! lastGenerated.isEmpty() && undoManager.canUndo()
        && undoManager.getUndoDescription() == undoName)
    {
        bool allThere = true;
        for (const auto& t : lastGenerated)
            allThere = allThere && t.getParent().isValid();
        if (allThere)
            undoManager.undo();
    }

    // Start at the bar the playhead is in (measured before any tempo change).
    const auto oldBeatsPerBar = (int) project.getProperty (ids::timeSigNumerator, 4);
    const auto oldBar = std::floor (engine.getPlayheadSeconds() / (model::secondsPerBeat (project) * oldBeatsPerBar) + 1.0e-6);

    const auto result = generator::generate (s);

    undoManager.beginNewTransaction (undoName);
    if (request.useGenreTempo)
        model::setTempo (project, result.tempo, &undoManager);
    const auto start = juce::jmax (0.0, oldBar) * model::secondsPerBeat (project) * oldBeatsPerBar;
    lastGenerated = generator::apply (project, result, s, start, false, &undoManager);
    undoManager.beginNewTransaction();

    if (! lastGenerated.isEmpty())
    {
        timeline.selectedTrackId = lastGenerated.getFirst()[ids::id].toString();
        timeline.changed();
    }

    const auto& genre = generator::genres()[(size_t) s.genre];
    statusLabel.setText ("Generated " + juce::String (genre.name) + " in " + generator::keyName (s.root, s.minor) + ", "
                         + juce::String (s.bars) + " bars at " + juce::String (juce::roundToInt ((double) project[ids::tempo])) + " BPM",
                         juce::dontSendNotification);
}

void MainComponent::applyKeyPreset (const juce::String& preset)
{
    keyPreset = preset;
    appProperties.getUserSettings()->setValue ("keyPreset", preset);

    // The preset's workflow comes with its keys.
    setStopReturnsToStart (commands::stopReturnsToStart (preset));

    // Registering again picks up the preset's keys as the defaults.
    commandManager.clearCommands();
    commandManager.registerAllCommandsForTarget (this);
    commandManager.getKeyMappings()->resetToDefaultMappings();
    saveKeyMappings();
}

void MainComponent::saveKeyMappings()
{
    // Only the differences from the preset are stored.
    if (auto xml = commandManager.getKeyMappings()->createXml (true))
        appProperties.getUserSettings()->setValue ("keyMappings", xml.get());

    appProperties.saveIfNeeded();
}

//==============================================================================
// Commands that act on the selection
//==============================================================================
juce::ValueTree MainComponent::selectedTrack() const
{
    return model::findById (project, timeline.selectedTrackId);
}

void MainComponent::toggleTrackFlag (const juce::Identifier& flag, const juce::String& undoName)
{
    auto track = selectedTrack();
    if (! track.isValid())
        return;

    // Mute and solo are edits (undoable); arming and showing automation aren't.
    if (undoName.isNotEmpty())
        undoManager.beginNewTransaction (undoName);

    // Every selected track follows the main one, as in Logic.
    const auto value = ! (bool) track[flag];
    for (const auto& id : timeline.selectedTrackIds())
    {
        auto t = model::findById (project, id);
        if (flag == ids::armed && model::isBusTrack (t))
            continue;
        t.setProperty (flag, value, undoName.isNotEmpty() ? &undoManager : nullptr);
    }
}

void MainComponent::selectAdjacentTrack (int delta)
{
    const auto count = model::numTracks (project);
    if (count == 0)
        return;

    const auto current = selectedTrackIndex();
    auto next = current < 0 ? (delta > 0 ? 0 : count - 1)
                            : juce::jlimit (0, count - 1, current + delta);

    // Hidden tracks are skipped.
    while (timeline.isHidden (next) && next + delta >= 0 && next + delta < count)
        next += delta;
    if (timeline.isHidden (next))
        return;

    timeline.selectOnly (project.getChild (next)[ids::id].toString());
    timeline.selectedRegionId = {};

    scrollToTrack (next);
    timeline.changed();
}

void MainComponent::scrollToTrack (int index)
{
    // Keep it on screen (positions without the scroll).
    const auto top = timeline.trackTop (index) + timeline.scrollY - TimelineState::rulerHeight;
    const auto height = timeline.heightOf (index);
    const auto visible = canvas.getHeight() - TimelineState::rulerHeight;
    if (top < timeline.scrollY)
        timeline.scrollY = top;
    else if (top + height > timeline.scrollY + visible)
        timeline.scrollY = juce::jmax (0, top + height - visible);
}

void MainComponent::selectTrackFromMixer (const juce::String& trackId, juce::ModifierKeys mods)
{
    const auto track = model::findById (project, trackId);
    if (! track.isValid())
        return;

    // The same rules as clicking track headers.
    if (mods.isCommandDown())
        timeline.toggleTrackSelection (trackId);
    else if (mods.isShiftDown())
        timeline.extendTrackSelection (trackId);
    else if (timeline.isTrackSelected (trackId) && timeline.selectedTrackIds().size() > 1)
        timeline.selectedTrackId = trackId;            // keep the others: their faders can move together
    else if (timeline.selectedTrackId == trackId)
        return;
    else
        timeline.selectOnly (trackId);

    timeline.selectedRegionId = {};
    if (track[ids::hidden] && ! timeline.showHiddenTracks)
        timeline.showHiddenTracks = true;              // show it rather than select something invisible
    scrollToTrack (project.indexOf (track));
    timeline.changed();
}

//==============================================================================
// Region editing (Logic's Edit menu)
//==============================================================================
void MainComponent::copySelectedRegions (bool cut)
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty())
        return;

    clipboardRegions.clear();
    clipboardTrackOffsets.clear();
    int topTrack = model::numTracks (project);
    clipboardStart = std::numeric_limits<double>::max();
    for (const auto& r : regions)
    {
        topTrack = juce::jmin (topTrack, project.indexOf (r.getParent()));
        clipboardStart = juce::jmin (clipboardStart, (double) r[ids::start]);
    }
    for (const auto& r : regions)
    {
        clipboardRegions.add (r.createCopy());
        clipboardTrackOffsets.add (project.indexOf (r.getParent()) - topTrack);
    }

    if (cut)
    {
        undoManager.beginNewTransaction ("Cut");
        for (auto r : regions)
            model::deleteRegion (r, &undoManager);
        timeline.selectOnlyRegion ({});
        timeline.changed();
    }
    commandManager.commandStatusChanged();
}

void MainComponent::pasteRegions()
{
    const auto first = selectedTrackIndex();
    if (clipboardRegions.isEmpty() || first < 0 || engine.isRecording())
        return;

    const auto at = engine.getPlayheadSeconds();
    undoManager.beginNewTransaction ("Paste");
    juce::StringArray pasted;
    juce::Array<juce::ValueTree> pastedTrees;

    for (int i = 0; i < clipboardRegions.size(); ++i)
    {
        const auto& source = clipboardRegions.getReference (i);
        auto target = project.getChild (first + clipboardTrackOffsets[i]);
        if (! target.hasType (ids::TRACK) || model::isBusTrack (target)
            || model::isMidiRegion (source) != model::isInstrumentTrack (target))
            continue;   // MIDI only onto instrument tracks, audio onto audio tracks

        auto copy = model::duplicateRegion (source, target, at + (double) source[ids::start] - clipboardStart, &undoManager);
        pasted.add (copy[ids::id].toString());
        pastedTrees.add (copy);
    }

    applyNoOverlap (pastedTrees);
    if (! pasted.isEmpty())
        timeline.selectRegions (pasted);
    timeline.changed();
}

void MainComponent::selectAllRegions()
{
    juce::StringArray all;
    for (int i = 0; i < model::numTracks (project); ++i)
        if (! timeline.isHidden (i))
            for (const auto& r : project.getChild (i))
                if (r.hasType (ids::REGION))
                    all.add (r[ids::id].toString());
    timeline.selectRegions (all);
    timeline.changed();
}

void MainComponent::repeatSelectedRegions()
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty() || engine.isRecording())
        return;

    double from = std::numeric_limits<double>::max(), to = 0.0;
    for (const auto& r : regions)
    {
        from = juce::jmin (from, (double) r[ids::start]);
        to = juce::jmax (to, model::regionEnd (r));
    }

    // Logic's Repeat: a copy of the whole selection right after it.
    undoManager.beginNewTransaction ("Repeat");
    juce::StringArray copies;
    juce::Array<juce::ValueTree> copyTrees;
    for (const auto& r : regions)
    {
        auto copy = model::duplicateRegion (r, r.getParent(), (double) r[ids::start] + (to - from), &undoManager);
        copies.add (copy[ids::id].toString());
        copyTrees.add (copy);
    }
    applyNoOverlap (copyTrees);
    timeline.selectRegions (copies);
    timeline.changed();
}

void MainComponent::joinSelectedRegions()
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.size() < 2)
        return;

    undoManager.beginNewTransaction ("Join regions");
    auto joined = model::joinRegions (regions, &undoManager);
    if (! joined.isValid())
    {
        statusLabel.setText ("Join works on MIDI regions on one track", juce::dontSendNotification);
        return;
    }
    timeline.selectOnlyRegion (joined[ids::id].toString());
    timeline.changed();
}

void MainComponent::muteSelectedRegions()
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty())
        return;

    const auto value = ! model::isRegionMuted (model::findById (project, timeline.selectedRegionId).isValid()
                                                   ? model::findById (project, timeline.selectedRegionId) : regions.getFirst());
    undoManager.beginNewTransaction (value ? "Mute regions" : "Unmute regions");
    for (auto r : regions)
        r.setProperty (ids::mute, value, &undoManager);
}

void MainComponent::trimSelectedToPlayhead (bool start)
{
    const auto time = engine.getPlayheadSeconds();
    undoManager.beginNewTransaction (start ? "Trim region start" : "Trim region end");
    juce::Array<juce::ValueTree> trimmed;
    for (auto r : timeline.selectedRegionTrees())
    {
        if (time <= (double) r[ids::start] + model::minRegionLength && ! start)
            continue;
        if (time >= model::regionEnd (r) - model::minRegionLength && start)
            continue;
        const auto sourceLength = fileCache.getLengthSeconds (juce::File (r[ids::file].toString()));
        if (start)
            model::trimRegionStart (r, time, sourceLength, &undoManager);
        else
            model::trimRegionEnd (r, time, sourceLength, &undoManager);
        trimmed.add (r);
    }
    canvas.applyDragMode (trimmed, {});
}

void MainComponent::loopSelectedRegions()
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty())
        return;
    const auto main = model::findById (project, timeline.selectedRegionId);
    const bool on = ! (bool) (main.isValid() ? main : regions.getFirst()).getProperty (ids::loop, false);
    undoManager.beginNewTransaction (on ? "Loop regions" : "Stop looping regions");
    for (auto r : regions)
        model::setLoop (r, on, &undoManager);
}

void MainComponent::nudgeSelectedRegions (int direction)
{
    const auto regions = timeline.selectedRegionTrees();
    if (regions.isEmpty() || engine.isRecording())
        return;

    const auto step = model::secondsPerBeat (project) / 4.0 * direction;
    double earliest = std::numeric_limits<double>::max();
    for (const auto& r : regions)
        earliest = juce::jmin (earliest, (double) r[ids::start]);
    if (earliest + step < 0.0)
        return;

    undoManager.beginNewTransaction ("Nudge");
    for (auto r : regions)
        model::moveRegion (r, r.getParent(), (double) r[ids::start] + step, &undoManager);
}

void MainComponent::duplicateSelectedTrack()
{
    auto track = selectedTrack();
    if (! track.isValid() || engine.isRecording())
        return;

    undoManager.beginNewTransaction ("New track with duplicate settings");
    auto copy = model::duplicateTrackSettings (project, track, &undoManager);
    timeline.selectOnly (copy[ids::id].toString());
    timeline.changed();
}

void MainComponent::createStackFromSelection (bool summing)
{
    juce::Array<juce::ValueTree> tracks;
    for (const auto& id : timeline.selectedTrackIds())
        if (auto t = model::findById (project, id); t.isValid() && t[ids::stack].toString().isEmpty())
            tracks.add (t);
    if (tracks.isEmpty())
        return;

    undoManager.beginNewTransaction (summing ? "Create summing stack" : "Create folder stack");
    auto main = model::createTrackStack (project, tracks, summing, &undoManager);
    timeline.selectOnly (main[ids::id].toString());
    timeline.changed();
}

void MainComponent::showCreateStackDialog()
{
    // Logic's Create Track Stack: choose the kind.
    const auto count = timeline.selectedTrackIds().size();
    if (count == 0)
        return;
    auto* w = new juce::AlertWindow ("Create Track Stack",
                                     juce::String (count) + (count == 1 ? " track" : " tracks") + " go into the stack.\n\n"
                                     "Folder Stack: the main track is a fader on top of theirs (like a VCA): it turns them all up or down "
                                     "together, and their own faders stay as they are.\n\n"
                                     "Summing Stack: the tracks play into an aux bus (the main track). Its fader sets the bus's level only; "
                                     "the tracks' own faders stay independent.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addButton ("Summing Stack", 2, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Folder Stack", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
    {
        if (safe != nullptr && result > 0)
            safe->createStackFromSelection (result == 2);
    }), true);
}

void MainComponent::newVcaForSelection()
{
    undoManager.beginNewTransaction ("New VCA");
    auto vca = model::addTrack (project, "VCA", &undoManager, -1, model::vcaKind);
    for (const auto& id : timeline.selectedTrackIds())
        if (auto t = model::findById (project, id); t.isValid() && t != vca && ! model::isVcaTrack (t))
            t.setProperty (ids::vca, vca[ids::id].toString(), &undoManager);
    timeline.selectOnly (vca[ids::id].toString());
    timeline.changed();
}

void MainComponent::applyNoOverlap (const juce::Array<juce::ValueTree>& regions)
{
    canvas.applyDragMode (regions, {});
}

void MainComponent::showToolMenu (bool withShortcuts)
{
    // Logic's Tool menu: every tool with its icon. Right-clicking an empty part of the
    // tracks area adds the everyday edits under it.
    // T: the tool chooser, which takes Logic's tool keys (T, P, E, X, I, G, S, M, Z, A, R, N).
    if (! withShortcuts)
    {
        auto chooser = std::make_unique<ToolChooser> (timeline.tool, [safe = juce::Component::SafePointer<MainComponent> (this)] (TimelineState::Tool t)
        {
            if (safe != nullptr)
                safe->setTool (t);
        });
        const auto at = canvas.isShowing() && canvas.getScreenBounds().contains (juce::Desktop::getMousePosition())
                            ? juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (2)
                            : toolBox.getScreenBounds();
        juce::CallOutBox::launchAsynchronously (std::move (chooser), at, nullptr);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Tools");
    for (const auto tool : toolicons::all)
    {
        juce::PopupMenu::Item item (juce::String (toolicons::name (tool)) + "    (" + toolicons::hint (tool) + ")");
        item.shortcutKeyDescription = "T " + juce::String::charToString (toolicons::key (tool)).toUpperCase();
        item.image = toolicons::drawable (tool, theme::text);
        item.isTicked = timeline.tool == tool;
        item.action = [this, t = tool] { setTool (t); };
        menu.addItem (std::move (item));
    }

    if (withShortcuts)
    {
        menu.addSeparator();
        menu.addCommandItem (&commandManager, commands::pasteRegions);
        menu.addCommandItem (&commandManager, commands::selectAllRegions);
        menu.addCommandItem (&commandManager, commands::undo);
        menu.addCommandItem (&commandManager, commands::redo);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, commands::newAudioTrack);
        menu.addCommandItem (&commandManager, commands::newInstrumentTrack);
        menu.addCommandItem (&commandManager, commands::importAudio);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, commands::createMarker);
        menu.addCommandItem (&commandManager, commands::setLocatorsBySelection);
        menu.addCommandItem (&commandManager, commands::toggleCycle);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MainComponent::setTool (TimelineState::Tool tool)
{
    timeline.tool = tool;
    canvas.setMouseCursor (toolicons::cursor (tool));
    toolBox.setSelectedId ((int) tool + 1, juce::dontSendNotification);
    statusLabel.setText (toolBox.getText() + " tool", juce::dontSendNotification);
}

void MainComponent::toggleInspector()
{
    inspector.setVisible (! inspector.isVisible());
    if (inspector.isVisible())
        inspector.setTrack (project, model::findById (project, timeline.selectedTrackId)), inspector.setRegion (model::findById (project, timeline.selectedRegionId));
    else
        inspector.setTrack ({}, {});
    appProperties.getUserSettings()->setValue ("inspector", inspector.isVisible());
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::hideSelectedTracks()
{
    const auto selection = timeline.selectedTrackIds();
    if (selection.isEmpty())
        return;

    const auto last = project.indexOf (model::findById (project, selection[selection.size() - 1]));
    const bool hide = ! (bool) model::findById (project, timeline.selectedTrackId)[ids::hidden];

    undoManager.beginNewTransaction (hide ? "Hide tracks" : "Show tracks");
    for (const auto& id : selection)
        model::findById (project, id).setProperty (ids::hidden, hide, &undoManager);

    // As in Logic: hide this one and select the next track.
    if (hide && ! timeline.showHiddenTracks)
        for (int i = last + 1; i < model::numTracks (project); ++i)
            if (! (bool) project.getChild (i)[ids::hidden])
            {
                timeline.selectOnly (project.getChild (i)[ids::id].toString());
                break;
            }

    timeline.changed();
}

void MainComponent::zoomVertically (int steps)
{
    // Every track a step taller (or shorter), keeping their differences, as Logic's Zoom Vertical does.
    for (int i = 0; i < model::numTracks (project); ++i)
    {
        auto track = project.getChild (i);
        const auto h = timeline.heightOf (i);
        const auto next = juce::jlimit (TimelineState::minTrackHeight, TimelineState::maxTrackHeight,
                                        juce::roundToInt (h * std::pow (1.25, steps)));
        track.setProperty (ids::height, next, nullptr);
    }
    timeline.changed();
}

void MainComponent::moveByBars (int bars)
{
    // From inside a bar, "back" goes to that bar's start first, as in Logic (bars follow the tempo track).
    const auto time = model::moveByBars (project, engine.getPlayheadSeconds(), bars);
    engine.setPlayheadSeconds (time);

    // Follow the playhead if it leaves the screen.
    const auto x = timeline.timeToX (time);
    if (x < 0.0f || x > (float) canvas.getWidth())
    {
        timeline.scrollSeconds = juce::jmax (0.0, time - 2.0 * model::secondsPerBeat (project) * (int) project.getProperty (ids::timeSigNumerator, 4));
        timeline.changed();
    }

    canvas.repaint();
}

void MainComponent::quantizeSelection()
{
    auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || ! model::isMidiRegion (region))
        return;

    const double bpm = project.getProperty (ids::tempo, 120.0);
    undoManager.beginNewTransaction ("Quantize");
    model::quantizeNotes (region, 60.0 / bpm / 4.0, &undoManager);
}

void MainComponent::setLocatorsBySelection()
{
    const auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || engine.isRecording())
        return;

    model::setCycle (project, (double) region[ids::start], model::regionEnd (region), true);
}

void MainComponent::togglePianoRoll()
{
    if (pianoRoll.isVisible())
    {
        closePianoRoll();
        return;
    }

    // The selected MIDI region, or the first one on the selected track.
    auto region = model::findById (project, timeline.selectedRegionId);

    if (! region.isValid() || ! model::isMidiRegion (region))
    {
        region = {};
        for (const auto& r : selectedTrack())
            if (model::isMidiRegion (r))
            {
                region = r;
                break;
            }
    }

    if (region.isValid())
        openPianoRoll (region);
}

//==============================================================================
// The mixer and the editor in windows of their own (Logic's Mixer and Piano Roll windows)
//==============================================================================
MainComponent::DetachedWindow::DetachedWindow (const juce::String& title, juce::Component& content, const juce::String& savedState,
                                               std::function<void()> onCloseRequested)
    : DocumentWindow (title, theme::background, DocumentWindow::allButtons), onClose (std::move (onCloseRequested))
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (&content, false);
    setResizable (true, false);
    setResizeLimits (420, 240, 16000, 16000);
    if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
        centreWithSize (1180, 700);
    setVisible (true);
    toFront (true);
}

void MainComponent::DetachedWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

void MainComponent::openMixerWindow()
{
    if (mixerWindow != nullptr)
    {
        mixerWindow->toFront (true);
        return;
    }
    const bool wasInPane = mixer.isVisible();
    removeChildComponent (&mixer);
    mixer.setVisible (true);
    mixerWindow = std::make_unique<DetachedWindow> ("Mixer", mixer, appProperties.getUserSettings()->getValue ("mixerWindow"),
                                                    [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                                             { if (safe != nullptr) safe->dockMixer(); }); });
    juce::ignoreUnused (wasInPane);
    mixerWindow->addKeyListener (commandManager.getKeyMappings());   // the app's keys work there too
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::dockMixer()
{
    if (mixerWindow == nullptr)
        return;
    appProperties.getUserSettings()->setValue ("mixerWindow", mixerWindow->getWindowStateAsString());
    mixerWindow->clearContentComponent();
    mixerWindow.reset();
    addChildComponent (mixer);
    mixer.setVisible (false);   // back as the pane (X shows it)
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::openEditorWindow()
{
    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    // The region in the editor now, or the selected MIDI region, or the first one on the selected track.
    auto region = model::findById (project, timeline.selectedRegionId);
    if (! region.isValid() || ! model::isMidiRegion (region))
    {
        region = {};
        for (const auto& r : selectedTrack())
            if (model::isMidiRegion (r))
            {
                region = r;
                break;
            }
    }
    if (! region.isValid())
    {
        showError ("Editor", "Select a MIDI region first.");
        return;
    }

    if (pianoRoll.isVisible())
        pianoRoll.setVisible (false);
    removeChildComponent (&pianoRoll);
    pianoRoll.setVisible (true);
    editorWindow = std::make_unique<DetachedWindow> ("Editor", pianoRoll, appProperties.getUserSettings()->getValue ("editorWindow"),
                                                     [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                                              { if (safe != nullptr) safe->dockEditor(); }); });
    editorWindow->addKeyListener (commandManager.getKeyMappings());
    pianoRoll.setRegion (region);
    editorWindow->setName ("Editor: " + region[ids::name].toString());
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::dockEditor()
{
    if (editorWindow == nullptr)
        return;
    appProperties.getUserSettings()->setValue ("editorWindow", editorWindow->getWindowStateAsString());
    editorWindow->clearContentComponent();
    editorWindow.reset();
    pianoRoll.setRegion ({});
    addChildComponent (pianoRoll);
    pianoRoll.setVisible (false);
    resized();
    canvas.grabKeyboardFocus();
    commandManager.commandStatusChanged();
}

void MainComponent::deleteSelectedTrack()
{
    auto track = selectedTrack();
    if (! track.isValid() || engine.isRecording())
        return;

    const auto selection = timeline.selectedTrackIds();
    const auto index = project.indexOf (model::findById (project, selection[0]));
    undoManager.beginNewTransaction (selection.size() > 1 ? "Delete tracks" : "Delete track");
    for (const auto& id : selection)
        model::deleteTrack (model::findById (project, id), &undoManager);

    // Select the track that took its place, so Cmd+Delete can be pressed again.
    const auto count = model::numTracks (project);
    timeline.selectedTrackId = count > 0 ? project.getChild (juce::jmin (index, count - 1))[ids::id].toString() : juce::String();
    timeline.selectedRegionId = {};
    timeline.changed();
}

} // namespace riff
