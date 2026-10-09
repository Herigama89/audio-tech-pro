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
        // Transport (Logic calls this menu "Record" and "Navigate")
        play = 0x2301, record, goToStart, rewindBar, forwardBar, toggleCycle, toggleMetronome, toggleCountIn,
        // View / window
        showMixer = 0x2401, showPianoRoll, zoomIn, zoomOut, keyCommands, audioSettings, calibrateLatency, scanPlugins, showAssistant, recordScreen, combineInterfaces
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
