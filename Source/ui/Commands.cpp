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
        { importAudio,            "Import Audio File...",       "File",      "Put audio files on the selected track at the playhead" },
        { exportSong,             "Bounce Song to WAV...",      "File",      "Export the song, or the cycle range, to a WAV file" },

        { undo,                   "Undo",                       "Edit",      "Undo the last edit" },
        { redo,                   "Redo",                       "Edit",      "Redo the edit you undid" },
        { deleteSelection,        "Delete",                     "Edit",      "Delete the selected region" },
        { splitAtPlayhead,        "Split at Playhead",          "Edit",      "Split regions at the playhead" },
        { quantize,               "Quantize",                   "Edit",      "Quantize the selected MIDI region to 16th notes" },
        { setLocatorsBySelection, "Set Locators by Selection",  "Edit",      "Set the cycle to the selected region and turn Cycle on" },

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
        { trackColours,           "Track Colours...",           "Track",     "Choose a colour for the selected tracks" },
        { generateMidi,           "Generate MIDI...",           "Track",     "Write drums, bass, chords and a melody in a genre's style" },

        { play,                   "Play or Stop",               "Transport", "Start or stop playback" },
        { record,                 "Record",                     "Transport", "Record on armed tracks, or punch out" },
        { goToStart,              "Go to Beginning",            "Transport", "Move the playhead to the start" },
        { rewindBar,              "Rewind One Bar",             "Transport", "Move the playhead back one bar" },
        { forwardBar,             "Forward One Bar",            "Transport", "Move the playhead forward one bar" },
        { toggleCycle,            "Cycle",                      "Transport", "Turn Cycle (loop) on or off" },
        { toggleMetronome,        "Metronome",                  "Transport", "Turn the click on or off" },
        { toggleCountIn,          "Count-in",                   "Transport", "One bar of click before recording" },

        { showMixer,              "Mixer",                      "View",      "Show or hide the mixer" },
        { showPianoRoll,          "Piano Roll",                 "View",      "Open the selected MIDI region in the piano roll, or close it" },
        { zoomIn,                 "Zoom In",                    "View",      "Zoom in horizontally" },
        { zoomOut,                "Zoom Out",                   "View",      "Zoom out horizontally" },
        { toggleInspector,        "Inspector",                  "View",      "Show or hide the Inspector: the selected track's channel strip and the master's, on the left" },
        { zoomVerticalIn,         "Zoom Vertical In",           "View",      "Make every track taller" },
        { zoomVerticalOut,        "Zoom Vertical Out",          "View",      "Make every track shorter" },
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
            case muteTrack:              return { key ('m', ctrl), key ('m') };
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

            case showMixer:              return { key ('x') };
            case showPianoRoll:          return { key ('p'), key ('e') };
            case zoomIn:                 return { key (KeyPress::rightKey, cmd) };
            case zoomOut:                return { key (KeyPress::leftKey, cmd) };
            case zoomVerticalIn:         return { key (KeyPress::downKey, cmd) };
            case zoomVerticalOut:        return { key (KeyPress::upKey, cmd) };
            case toggleInspector:        return { key ('i') };
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
