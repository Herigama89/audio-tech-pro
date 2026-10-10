#include "Commands.h"

namespace riff::commands
{

const std::vector<Info>& all()
{
    static const std::vector<Info> list
    {
        { newProject,             "New",                        "File",      "Start a new, empty project" },
        { openProject,            "Open...",                    "File",      "Open a saved project" },
        { saveProject,            "Save",                       "File",      "Save the project" },
        { saveProjectAs,          "Save As...",                 "File",      "Save the project under a new name" },
        { consolidateProject,     "Consolidate Project...",     "File",      "Copy every audio file the project uses into its folder (Audio Files), so it's all in one place" },
        { saveAsTemplate,         "Save as Template...",        "File",      "Save this project as a template to start new ones from (File > New from Template)" },
        { newAlternative,         "New Alternative...",         "File",      "Save a copy of the project beside it, to try ideas and switch between them (File > Alternatives)" },
        { revertToBackup,         "Revert to Backup...",        "File",      "Open one of the backups made each time you saved" },
        { importAudio,            "Import Audio File...",       "File",      "Put audio files on the selected track at the playhead" },
        { exportSong,             "Bounce Song to WAV...",      "File",      "Export the song, or the cycle range, to a WAV file" },
        { exportStems,            "Export All Tracks as Audio Files...", "File", "Each track (and bus) to its own WAV in a folder, with its plugins, fader and pan, ignoring solo" },
        { importTracks,           "Import Tracks from Project...", "File",   "Bring tracks (regions, plugins, sends, automation) in from another project" },
        { bounceInPlace,          "Bounce in Place",            "Track",     "Render the selected track to a new audio track and mute the original" },

        { undo,                   "Undo",                       "Edit",      "Undo the last edit" },
        { redo,                   "Redo",                       "Edit",      "Redo the edit you undid" },
        { deleteSelection,        "Delete",                     "Edit",      "Delete the selected region" },
        { splitAtPlayhead,        "Split at Playhead",          "Edit",      "Split regions at the playhead" },
        { quantize,               "Quantize",                   "Edit",      "Quantize the selected MIDI region to 16th notes" },
        { setLocatorsBySelection, "Set Locators by Selection",  "Edit",      "Set the cycle to the selected region and turn Cycle on" },
        { cutRegions,             "Cut",                        "Edit",      "Cut the selected regions" },
        { copyRegions,            "Copy",                       "Edit",      "Copy the selected regions" },
        { pasteRegions,           "Paste",                      "Edit",      "Paste at the playhead, on the selected track" },
        { selectAllRegions,       "Select All",                 "Edit",      "Select every region" },
        { repeatRegions,          "Repeat",                     "Edit",      "Repeat the selected regions once, right after them" },
        { joinRegions,            "Join Regions",               "Edit",      "Join the selected MIDI regions on a track into one" },
        { muteRegions,            "Mute Regions",               "Edit",      "Mute or unmute the selected regions" },
        { nudgeLeft,              "Nudge Left",                 "Edit",      "Move the selected regions a 16th note earlier" },
        { nudgeRight,             "Nudge Right",                "Edit",      "Move the selected regions a 16th note later" },
        { trimStartToPlayhead,    "Trim Region Start to Playhead", "Edit",   "Move the selected regions' start to the playhead" },
        { trimEndToPlayhead,      "Trim Region End to Playhead", "Edit",     "Move the selected regions' end to the playhead" },
        { loopRegions,            "Loop Regions",               "Edit",      "Repeat the selected regions up to the next region (drag the upper right edge to set how far)" },
        { regionGain,             "Region Gain...",             "Edit",      "Turn the selected audio regions up or down" },
        { removeFades,            "Remove Fades",               "Edit",      "Take the fades off the selected regions" },
        { splitByLocators,        "Split Regions by Locators",  "Edit",      "Split every region at the cycle's start and end" },
        { cutSectionBetweenLocators, "Cut Section Between Locators", "Edit", "Remove the cycle's stretch of the song, and close the gap" },
        { insertSilenceBetweenLocators, "Insert Silence Between Locators", "Edit", "Open a gap the length of the cycle at its start" },
        { repeatSectionBetweenLocators, "Repeat Section Between Locators", "Edit", "Play the cycle's stretch twice: a copy right after it" },
        { toolMenu,               "Tool Menu",                  "Edit",      "Choose a tool: pointer, pencil, eraser, scissors, glue, mute" },

        { newAudioTrack,          "New Audio Track",            "Track",     "Add an audio track" },
        { newInstrumentTrack,     "New Software Instrument Track", "Track",  "Add an instrument (MIDI) track" },
        { newAuxBus,              "New Aux Bus",                "Track",     "Add an aux bus for sends" },
        { deleteTrack,            "Delete Track",               "Track",     "Delete the selected track" },
        { muteTrack,              "Mute Track",                 "Track",     "Mute or unmute the selected track" },
        { soloTrack,              "Solo Track",                 "Track",     "Solo or unsolo the selected track" },
        { armTrack,               "Record Enable Track",        "Track",     "Arm or disarm the selected track for recording" },
        { showAutomation,         "Show Automation",            "Track",     "Show or hide automation on the selected track" },
        { selectPreviousTrack,    "Select Previous Track",      "Track",     "Select the track above" },
        { selectNextTrack,        "Select Next Track",          "Track",     "Select the track below" },
        { hideTrack,              "Hide Track",                 "Track",     "Hide the selected tracks and select the next one (they still play)" },
        { showHiddenTracks,       "Show Hidden Tracks",         "Track",     "Show or leave out the tracks you hid" },
        { duplicateTrack,         "New Track With Duplicate Settings", "Track", "A new track like the selected one (instrument, fader, console, inserts, sends), without its regions" },
        { trackColours,           "Track Colours...",           "Track",     "Choose a colour for the selected tracks" },
        { newDrummerTrack,        "New Drummer Track",          "Track",     "A drummer who plays the song in a genre: set how busy and how loud, fills and swing" },
        { newBassPlayerTrack,     "New Bass Player Track",      "Track",     "A session bass player, in the song's key" },
        { openMixerWindow,        "Open Mixer Window",          "View",      "The mixer in a window of its own (resize it, put it on another screen)" },
        { openEditorWindow,       "Open Editor Window",         "View",      "The piano roll (and the other region editors) in a window of its own" },
        { themeSettings,          "Theme...",                   "View",      "The skin (studio grey or flat dark) and its colours: hue, lightness, contrast, text, accent, meters, grid" },
        { audioSuite,             "AudioSuite...",              "Edit",      "Process the selected audio regions offline with an effect or plugin (VST3, AU, CLAP), into new files" },
        { showPerformance,        "Performance...",             "View",      "How hard the computer works: audio load, its peak, overloads, the disk, and the heaviest tracks" },
        { showNotepad,            "Notepad",                    "View",      "Project notes: lyrics, ideas, what to fix (saved with the project)" },
        { showBrowser,            "Browser",                    "View",      "Find audio files on your computer and put them on the selected track" },
        { toggleQuickHelp,        "Quick Help",                 "View",      "A box that explains whatever is under the mouse" },
        { waveformZoom,           "Waveform Zoom",              "View",      "Draw the waveforms taller, to see quiet parts (click again for more, then back)" },
        { zoomToFitAll,           "Zoom to Fit",                "View",      "The whole song across the tracks area, and every track fitting the height" },
        { toggleControlBar,       "Show Control Bar",           "View",      "The bar along the top (display, transport, tools): show or hide it" },
        { toggleToolbar,          "Show Toolbar",               "View",      "A row of keys for the common edits (split, join, repeat, quantize, bounce, freeze...)" },
        { toggleStatusBar,        "Show Status Bar",            "View",      "The line of hints along the bottom" },
        { toggleMeters,           "Show Input and Output Meters", "View",    "The meters to the right of the tracks" },
        { toggleTrackHeaders,     "Show Track Headers",         "View",      "The track names, buttons and faders at the left of the tracks" },
        { showAllPanels,          "Show All Panels",            "View",      "Inspector, control bar, toolbar, Smart Controls, meters and the status bar" },
        { hideAllPanels,          "Hide All Panels",            "View",      "Just the tracks: every panel around them hidden" },
        { liveLoops,              "Live Loops",                 "View",      "A grid of loops per track: start and stop cells and scenes in time, on the next bar" },
        { newKeysPlayerTrack,     "New Keyboard Player Track",  "Track",     "A session keyboard player playing chords in the song's key" },
        { createTrackStack,       "Create Track Stack...",      "Track",     "Put the selected tracks in a stack: a folder stack (one fader on top of theirs) or a summing stack (they play into a bus)" },
        { createFolderStack,      "Create Folder Stack",        "Track",     "Put the selected tracks in a folder stack: one fader, mute and solo for them all" },
        { createSummingStack,     "Create Summing Stack",       "Track",     "Put the selected tracks in a summing stack: they play into one aux bus" },
        { freezeTracks,           "Freeze Track",               "Track",     "Render the selected tracks with their instrument and plugins to audio, to save processing (again to unfreeze)" },
        { newVca,                 "New VCA",                    "Track",     "A VCA fader for the selected tracks: it moves them all, keeping their balance" },
        { generateMidi,           "Generate MIDI...",           "Track",     "Write drums, bass, chords and a melody in a genre's style" },

        { play,                   "Play or Stop",               "Transport", "Start or stop playback" },
        { record,                 "Record",                     "Transport", "Record on armed tracks, or punch out" },
        { goToStart,              "Go to Beginning",            "Transport", "Move the playhead to the start" },
        { rewindBar,              "Rewind One Bar",             "Transport", "Move the playhead back one bar" },
        { forwardBar,             "Forward One Bar",            "Transport", "Move the playhead forward one bar" },
        { createMarker,           "Create Marker",              "Transport", "A marker at the playhead (double-click the marker track for one anywhere)" },
        { nextMarker,             "Go to Next Marker",          "Transport", "Move the playhead to the next marker" },
        { previousMarker,         "Go to Previous Marker",      "Transport", "Move the playhead to the previous marker" },
        { deleteMarker,           "Delete Marker",              "Transport", "Delete the selected marker" },
        { toggleCycle,            "Cycle",                      "Transport", "Turn Cycle (loop) on or off" },
        { toggleMetronome,        "Metronome",                  "Transport", "Turn the click on or off" },
        { toggleCountIn,          "Count-in",                   "Transport", "One bar of click before recording" },
        { toggleAutopunch,        "Autopunch",                  "Transport", "Record only between the punch locators (the red bar under the cycle; drag it in the ruler)" },
        { toggleReplace,          "Replace",                    "Transport", "A recording replaces the parts of regions it covers" },
        { toggleMidiMerge,        "Merge MIDI Recordings",      "Transport", "MIDI recorded over a MIDI region is merged into it" },

        { showMixer,              "Mixer",                      "View",      "Show or hide the mixer" },
        { showPianoRoll,          "Piano Roll",                 "View",      "Open the selected MIDI region in the piano roll, or close it" },
        { zoomIn,                 "Zoom In",                    "View",      "Zoom in horizontally" },
        { zoomOut,                "Zoom Out",                   "View",      "Zoom out horizontally" },
        { toggleInspector,        "Inspector",                  "View",      "Show or hide the Inspector: the selected track's channel strip and the master's, on the left" },
        { zoomVerticalIn,         "Zoom Vertical In",           "View",      "Make every track taller" },
        { zoomVerticalOut,        "Zoom Vertical Out",          "View",      "Make every track shorter" },
        { undoHistory,            "Undo History...",            "View",      "Every edit you can undo; click one to go back (or forward) to it" },
        { preferences,            "Settings...",                "View",      "Autosave, backups, recording format and more" },
        { showSmartControls,      "Smart Controls",             "View",      "Eight knobs for the selected track's sound (right-click one to map it)" },
        { showLibrary,            "Library",                    "View",      "Patches: save a track's sound and put it on other tracks" },
        { showLoopBrowser,        "Loop Browser",               "View",      "Browse a folder of loops and samples: click to hear, drag onto a track" },
        { groupSettings,          "Group Settings...",          "Mix",       "Name the groups and choose what each links: volume, mute, solo, record" },
        { ioLabels,               "I/O Labels...",              "Mix",       "Your own names for the interface's inputs (they show in the input menus)" },
        { signalFlow,             "Signal Flow",                "Mix",       "The selected track's path from its input to the main output, with every stage" },
        { controllerAssignments,  "Controller Assignments...",  "View",      "The knobs and faders on your MIDI controller that move tracks (right-click a track > Learn MIDI Controller to add one)" },
        { eventList,              "Event List",                 "View",      "The selected MIDI region's notes and controllers as a list you can edit" },
        { keyCommands,            "Key Commands...",            "View",      "Show and edit the key commands" },
        { audioSettings,          "Audio Settings...",          "View",      "Choose the audio interface, sample rate and buffer size" },
        { calibrateLatency,       "Calibrate Recording Latency...", "View",  "Measure the round trip of your interface" },
        { scanPlugins,            "Plug-in Manager...",         "View",      "Scan for VST3 and Audio Unit plugins" },
        { combineInterfaces,      "Combine Audio Interfaces...", "View",     "Use several audio interfaces at once, as one" },
        { recordScreen,           "Record Screen...",           "View",      "Record the app (or the whole screen) to a video with the mix's sound; again to stop" },
        { showAssistant,          "Claude Assistant",           "View",      "Show or hide the Claude panel: ask questions, or tell it what to do" },
    };

    return list;
}

const Info* find (juce::CommandID id)
{
    for (const auto& info : all())
        if (info.id == id)
            return &info;

    return nullptr;
}

juce::StringArray presetNames()
{
    return { "Logic Pro", "Pro Tools", "Ableton Live", "FL Studio", "Cubase" };
}

bool stopReturnsToStart (const juce::String& preset)
{
    // Pro Tools, Live and FL Studio jump back to where playback started; Logic and Cubase stay put.
    return preset == "Pro Tools" || preset == "Ableton Live" || preset == "FL Studio";
}

namespace
{
    using juce::KeyPress;
    using juce::ModifierKeys;

    constexpr int none  = 0;
    constexpr int cmd   = ModifierKeys::commandModifier;
    constexpr int shift = ModifierKeys::shiftModifier;
    constexpr int opt   = ModifierKeys::altModifier;
   #if JUCE_MAC
    constexpr int ctrl  = ModifierKeys::ctrlModifier;
    constexpr int macCtrl = ModifierKeys::ctrlModifier;   // the Mac's Control key
   #else
    constexpr int macCtrl = ModifierKeys::altModifier;    // off the Mac, Ctrl is the command key, so Alt stands in
   #endif

    KeyPress key (int code, int mods = none) { return KeyPress (code, ModifierKeys (mods), 0); }

    /**
        Logic Pro's default key commands (the "U.S. English" set), for the
        commands this app has. Where Logic uses a key for something this app
        doesn't do yet (M and S mute and solo regions in Logic), the closest
        command gets it.
    */
    juce::Array<KeyPress> logicKeys (juce::CommandID id)
    {
        switch (id)
        {
            case newProject:             return { key ('n', cmd) };
            case openProject:            return { key ('o', cmd) };
            case saveProject:            return { key ('s', cmd) };
            case saveProjectAs:          return { key ('s', shift | cmd) };
            case importAudio:            return { key ('i', shift | cmd), key ('i', cmd) };
            case exportSong:             return { key ('b', cmd), key ('e', cmd) };
            case exportStems:            return { key ('e', shift | cmd) };
           #if JUCE_MAC
            case bounceInPlace:          return { key ('b', ctrl) };
           #endif

            case undo:                   return { key ('z', cmd) };
            case redo:                   return { key ('z', shift | cmd), key ('y', cmd) };
            case deleteSelection:        return { key (KeyPress::backspaceKey), key (KeyPress::deleteKey) };
            case splitAtPlayhead:        return { key ('t', cmd) };
            case quantize:               return { key ('q') };
            case setLocatorsBySelection: return { key ('u', cmd) };

            case newAudioTrack:          return { key ('a', opt | cmd) };
            case newInstrumentTrack:     return { key ('s', opt | cmd) };
            case newAuxBus:              return {};
            case deleteTrack:            return { key (KeyPress::backspaceKey, cmd) };
           #if JUCE_MAC
            case muteTrack:              return { key ('m') };              // (Ctrl+M mutes regions, as in Logic)
            case muteRegions:            return { key ('m', ctrl) };
            case soloTrack:              return { key ('s', ctrl), key ('s') };
           #else
            // Off the Mac, Ctrl is the command key (Ctrl+S saves), so only the plain keys.
            case muteTrack:              return { key ('m') };
            case soloTrack:              return { key ('s') };
           #endif
            case armTrack:               return {};
            case showAutomation:         return { key ('a') };
            case selectPreviousTrack:    return { key (KeyPress::upKey) };
            case selectNextTrack:        return { key (KeyPress::downKey) };

            case play:                   return { key (KeyPress::spaceKey) };
            case record:                 return { key ('r') };
            case goToStart:              return { key (KeyPress::returnKey) };
            case rewindBar:              return { key (',') };
            case forwardBar:             return { key ('.') };
            case toggleCycle:            return { key ('c') };
            case toggleMetronome:        return { key ('k') };
            case toggleCountIn:          return { key ('k', shift) };
            case toggleAutopunch:        return { key ('i', opt | cmd) };
            case undoHistory:            return { key ('z', opt | cmd) };
            case preferences:            return { key (',', opt | cmd) };
            case eventList:              return { key ('d', opt | cmd) };

            case showMixer:              return { key ('x') };
            case showPianoRoll:          return { key ('p'), key ('e') };
            case openMixerWindow:        return { key ('2', cmd) };
            case openEditorWindow:       return { key ('4', cmd) };
            case zoomIn:                 return { key (KeyPress::rightKey, cmd) };
            case zoomOut:                return { key (KeyPress::leftKey, cmd) };
            case zoomVerticalIn:         return { key (KeyPress::downKey, cmd) };
            case zoomVerticalOut:        return { key (KeyPress::upKey, cmd) };
            case toggleInspector:        return { key ('i') };
            case showNotepad:            return { key ('p', opt | cmd) };
            case showBrowser:            return { key ('f') };
            case zoomToFitAll:           return { key ('z') };
            case cutRegions:             return { key ('x', cmd) };
            case copyRegions:            return { key ('c', cmd) };
            case pasteRegions:           return { key ('v', cmd) };
            case selectAllRegions:       return { key ('a', cmd) };
            case repeatRegions:          return { key ('r', cmd) };
            case joinRegions:            return { key ('j', cmd) };
            case duplicateTrack:         return { key ('d', cmd) };
            case createTrackStack:       return { key ('d', shift | cmd) };
            case nudgeLeft:              return { key (KeyPress::leftKey, opt) };
            case nudgeRight:             return { key (KeyPress::rightKey, opt) };
            case toolMenu:               return { key ('t') };
           #if JUCE_MAC
            case cutSectionBetweenLocators:    return { key ('x', ctrl | cmd) };
            case insertSilenceBetweenLocators: return { key ('z', ctrl | cmd) };
            case repeatSectionBetweenLocators: return { key ('r', ctrl | cmd) };
           #endif
            case loopRegions:            return { key ('l') };
            case showLibrary:            return { key ('y') };
            case showSmartControls:      return { key ('b') };
            case showLoopBrowser:        return { key ('o') };
            case trimStartToPlayhead:    return { key ('[', opt) };
            case trimEndToPlayhead:      return { key (']', opt) };
            case createMarker:           return { key ('\'', opt) };
            case nextMarker:             return { key ('.', opt) };
            case previousMarker:         return { key (',', opt) };
            case showHiddenTracks:       return { key ('h') };
            case trackColours:           return { key ('c', opt) };
           #if JUCE_MAC
            case hideTrack:              return { key ('h', ctrl) };
           #endif
            case keyCommands:            return { key ('k', opt) };
            case audioSettings:          return { key (',', cmd) };
            case calibrateLatency:       return {};
            case scanPlugins:            return {};
            default:                     return {};
        }
    }

    // Keys every preset shares: this app's own windows.
    juce::Array<KeyPress> appKeys (juce::CommandID id)
    {
        switch (id)
        {
            case keyCommands:   return { key ('k', opt) };
            case audioSettings: return { key (',', cmd) };
            case cutRegions:    return { key ('x', cmd) };
            case copyRegions:   return { key ('c', cmd) };
            case pasteRegions:  return { key ('v', cmd) };
            case selectAllRegions: return { key ('a', cmd) };
            default:            return {};
        }
    }

    /** Pro Tools' default shortcuts (Mac), for the commands this app has. */
    juce::Array<KeyPress> proToolsKeys (juce::CommandID id)
    {
        switch (id)
        {
            case newProject:             return { key ('n', cmd) };
            case openProject:            return { key ('o', cmd) };
            case saveProject:            return { key ('s', cmd) };
            case saveProjectAs:          return { key ('s', shift | cmd) };
            case importAudio:            return { key ('i', shift | cmd) };
            case exportSong:             return { key ('b', opt | cmd) };          // Bounce Mix

            case undo:                   return { key ('z', cmd) };
            case redo:                   return { key ('z', shift | cmd) };
            case deleteSelection:        return { key (KeyPress::backspaceKey), key (KeyPress::deleteKey) };
            case splitAtPlayhead:        return { key ('e', cmd) };                // Separate Clip
            case quantize:               return { key ('0', opt) };                // Quantize (Event Operations)

            case newAudioTrack:          return { key ('n', shift | cmd) };        // New Tracks
            case muteTrack:              return { key ('m', shift) };
            case soloTrack:              return { key ('s', shift) };
            case armTrack:               return { key ('r', shift) };
            case selectPreviousTrack:    return { key ('p', macCtrl) };
            case selectNextTrack:        return { key (';', macCtrl) };

            case play:                   return { key (KeyPress::spaceKey) };
            case record:                 return { key (KeyPress::F12Key), key (KeyPress::numberPad3) };
            case goToStart:              return { key (KeyPress::returnKey) };
            case toggleCycle:            return { key ('l', shift | cmd) };        // Loop Playback
            case toggleMetronome:        return { key (KeyPress::numberPad7) };
            case toggleCountIn:          return { key (KeyPress::numberPad8) };

            case showMixer:              return { key ('=', cmd) };                // Mix / Edit window
            case showPianoRoll:          return { key ('=', macCtrl) };            // MIDI Editor
            case zoomIn:                 return { key (']', cmd) };
            case zoomOut:                return { key ('[', cmd) };
            case zoomVerticalIn:         return { key (']', opt | cmd) };
            case zoomVerticalOut:        return { key ('[', opt | cmd) };
            default:                     return appKeys (id);
        }
    }

    /** Ableton Live's default shortcuts (Mac). */
    juce::Array<KeyPress> abletonKeys (juce::CommandID id)
    {
        switch (id)
        {
            case newProject:             return { key ('n', cmd) };
            case openProject:            return { key ('o', cmd) };
            case saveProject:            return { key ('s', cmd) };
            case saveProjectAs:          return { key ('s', shift | cmd) };
            case exportSong:             return { key ('r', shift | cmd) };        // Export Audio/Video

            case undo:                   return { key ('z', cmd) };
            case redo:                   return { key ('z', shift | cmd), key ('y', cmd) };
            case deleteSelection:        return { key (KeyPress::backspaceKey), key (KeyPress::deleteKey) };
            case splitAtPlayhead:        return { key ('e', cmd) };                // Split
            case quantize:               return { key ('u', cmd) };
            case setLocatorsBySelection: return { key ('l', cmd) };                // Loop Selection

            case newAudioTrack:          return { key ('t', cmd) };
            case newInstrumentTrack:     return { key ('t', shift | cmd) };        // MIDI track
            case newAuxBus:              return { key ('t', opt | cmd) };          // Return track
            case showAutomation:         return { key ('a') };                     // Automation Mode
            case selectPreviousTrack:    return { key (KeyPress::upKey) };
            case selectNextTrack:        return { key (KeyPress::downKey) };

            case play:                   return { key (KeyPress::spaceKey) };
            case record:                 return { key (KeyPress::F9Key) };         // Arrangement Record
            case goToStart:              return { key (KeyPress::homeKey) };

            case showMixer:              return { key ('m', opt | cmd) };
            case showPianoRoll:          return { key ('l', opt | cmd) };          // Detail View
            case zoomIn:                 return { key ('=') };
            case zoomOut:                return { key ('-') };
            default:                     return appKeys (id);
        }
    }

    /** FL Studio's default shortcuts (Mac, where Ctrl is Cmd). */
    juce::Array<KeyPress> flStudioKeys (juce::CommandID id)
    {
        switch (id)
        {
            case newProject:             return { key ('n', cmd) };
            case openProject:            return { key ('o', cmd) };
            case saveProject:            return { key ('s', cmd) };
            case saveProjectAs:          return { key ('s', shift | cmd) };
            case importAudio:            return { key ('i', cmd) };
            case exportSong:             return { key ('r', cmd) };                // Export WAV

            case undo:                   return { key ('z', cmd) };
            case redo:                   return { key ('z', shift | cmd) };
            case deleteSelection:        return { key (KeyPress::backspaceKey), key (KeyPress::deleteKey) };
            case splitAtPlayhead:        return { key ('c') };                     // Slice
            case quantize:               return { key ('q', opt) };                // Quick quantize (Cmd+Q quits on a Mac)
            case selectPreviousTrack:    return { key (KeyPress::upKey) };
            case selectNextTrack:        return { key (KeyPress::downKey) };

            case play:                   return { key (KeyPress::spaceKey) };
            case record:                 return { key ('r') };
            case toggleMetronome:        return { key ('m', cmd) };
            case toggleCountIn:          return { key ('p', cmd) };                // Countdown before recording

            case showMixer:              return { key (KeyPress::F9Key) };
            case showPianoRoll:          return { key (KeyPress::F7Key) };
            default:                     return appKeys (id);
        }
    }

    /** Cubase's default key commands (Mac). */
    juce::Array<KeyPress> cubaseKeys (juce::CommandID id)
    {
        switch (id)
        {
            case newProject:             return { key ('n', cmd) };
            case openProject:            return { key ('o', cmd) };
            case saveProject:            return { key ('s', cmd) };
            case saveProjectAs:          return { key ('s', shift | cmd) };

            case undo:                   return { key ('z', cmd) };
            case redo:                   return { key ('z', shift | cmd) };
            case deleteSelection:        return { key (KeyPress::backspaceKey), key (KeyPress::deleteKey) };
            case splitAtPlayhead:        return { key ('x', opt) };                // Split at Cursor
            case quantize:               return { key ('q') };
            case setLocatorsBySelection: return { key ('p') };                     // Locators to Selection

            case muteTrack:              return { key ('m') };
            case soloTrack:              return { key ('s') };
            case armTrack:               return { key ('r') };
            case selectPreviousTrack:    return { key (KeyPress::upKey) };
            case selectNextTrack:        return { key (KeyPress::downKey) };

            case play:                   return { key (KeyPress::spaceKey) };
            case record:                 return { key (KeyPress::numberPadMultiply) };
            case goToStart:              return { key (KeyPress::homeKey), key (KeyPress::numberPadDecimalPoint) };
            case rewindBar:              return { key (KeyPress::numberPadSubtract) };
            case forwardBar:             return { key (KeyPress::numberPadAdd) };
            case toggleCycle:            return { key (KeyPress::numberPadDivide) };
            case toggleMetronome:        return { key ('c') };

            case showMixer:              return { key (KeyPress::F3Key) };
            case showPianoRoll:          return { key (KeyPress::returnKey) };     // Open/Close Editor
            case zoomIn:                 return { key ('h') };
            case zoomOut:                return { key ('g') };
            case zoomVerticalIn:         return { key ('h', shift) };
            case zoomVerticalOut:        return { key ('g', shift) };
            default:                     return appKeys (id);
        }
    }
}

juce::Array<juce::KeyPress> keysFor (const juce::String& preset, juce::CommandID id)
{
    if (preset == "Pro Tools")    return proToolsKeys (id);
    if (preset == "Ableton Live") return abletonKeys (id);
    if (preset == "FL Studio")    return flStudioKeys (id);
    if (preset == "Cubase")       return cubaseKeys (id);
    return logicKeys (id);
}

juce::String describe (const juce::KeyPress& key)
{
    return key.getTextDescriptionWithIcons();
}

} // namespace riff::commands
