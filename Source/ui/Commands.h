#pragma once

#include <JuceHeader.h>
#include <vector>

/**
    Every command the app can run from a menu or a key, and the key presets
    ("Logic Pro" for now; other DAWs' layouts come later).

    The command list is plain data so it can be tested without a window.
*/
namespace riff::commands
{
    enum ID : juce::CommandID
    {
        // File
        newProject = 0x2001, openProject, saveProject, saveProjectAs, importAudio, exportSong,
        // Edit
        undo = 0x2101, redo, deleteSelection, splitAtPlayhead, quantize, setLocatorsBySelection,
        // Track
        newAudioTrack = 0x2201, newInstrumentTrack, newAuxBus, deleteTrack,
        muteTrack, soloTrack, armTrack, showAutomation, selectPreviousTrack, selectNextTrack, generateMidi,
        hideTrack, showHiddenTracks, trackColours, duplicateTrack,
        // Transport (Logic calls this menu "Record" and "Navigate")
        play = 0x2301, record, goToStart, rewindBar, forwardBar, toggleCycle, toggleMetronome, toggleCountIn,
        // View / window
        showMixer = 0x2401, showPianoRoll, zoomIn, zoomOut, keyCommands, audioSettings, calibrateLatency, scanPlugins, showAssistant, recordScreen, combineInterfaces,
        zoomVerticalIn, zoomVerticalOut, toggleInspector,
        // Edit (added later; ids kept stable for saved key mappings)
        cutRegions = 0x2501, copyRegions, pasteRegions, selectAllRegions, repeatRegions, joinRegions, muteRegions,
        nudgeLeft, nudgeRight, toolMenu,
        createMarker = 0x2601, nextMarker, previousMarker, deleteMarker,
        consolidateProject = 0x2701, revertToBackup, recoverAutosave, exportStems, bounceInPlace, importTracks,
        trimStartToPlayhead = 0x2801, trimEndToPlayhead, loopRegions, regionGain, removeFades,
        createFolderStack = 0x2901, createSummingStack, freezeTracks, newVca,
        toggleAutopunch = 0x2a01, toggleReplace, toggleMidiMerge, undoHistory, preferences, saveAsTemplate, newAlternative, eventList,
        controllerAssignments, showLibrary, showLoopBrowser, showSmartControls,
        splitByLocators = 0x2b01, cutSectionBetweenLocators, insertSilenceBetweenLocators, repeatSectionBetweenLocators, groupSettings, ioLabels, signalFlow,
        newDrummerTrack = 0x2c01, newBassPlayerTrack, newKeysPlayerTrack, liveLoops,
        openMixerWindow = 0x2d01, openEditorWindow, themeSettings,
        toggleControlBar = 0x2e01, toggleToolbar, toggleStatusBar, toggleMeters, toggleTrackHeaders, showAllPanels, hideAllPanels, createTrackStack,
        showNotepad = 0x2f01, showBrowser, toggleQuickHelp, waveformZoom, zoomToFitAll, showPerformance
    };

    struct Info
    {
        ID id;
        const char* name;
        const char* category;
        const char* description;
    };

    /** Every command, in menu order. */
    const std::vector<Info>& all();
    const Info* find (juce::CommandID id);

    /** The key presets the Key Commands window offers. The first is the default. */
    juce::StringArray presetNames();

    /** Workflow: true if, in this DAW, stopping jumps back to where playback started. */
    bool stopReturnsToStart (const juce::String& preset);

    /** The keys a preset assigns to a command (often one, sometimes none or two). */
    juce::Array<juce::KeyPress> keysFor (const juce::String& preset, juce::CommandID id);

    /** A readable name for a key, using Mac symbols on a Mac. */
    juce::String describe (const juce::KeyPress& key);
}
