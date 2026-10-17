#pragma once
#include "TempoMap.h"

#include <JuceHeader.h>
#include "Identifiers.h"

/**
    The project model: a juce::ValueTree with a fixed schema (see Identifiers.h).

    Every edit goes through the functions below and takes an UndoManager, so
    every edit is undoable. Group the edits of one user gesture with
    UndoManager::beginNewTransaction() before the first call.

    The UI and the engine both observe the same tree; nothing else holds a
    copy of project state.
*/
namespace riff::model
{
    constexpr int schemaVersion = 1;
    constexpr double minRegionLength = 0.01; // seconds

    // --- Creation ---------------------------------------------------------
    juce::ValueTree createProject (const juce::String& name = "Untitled");

    inline const juce::String audioKind { "audio" }, instrumentKind { "instrument" }, busKind { "bus" }, vcaKind { "vca" };

    juce::ValueTree addTrack (juce::ValueTree project, const juce::String& name, juce::UndoManager* um,
                              int index = -1, const juce::String& kind = audioKind);

    juce::ValueTree addRegion (juce::ValueTree track, const juce::File& audioFile,
                               double start, double length, juce::UndoManager* um);

    // --- Editing ----------------------------------------------------------
    /** Moves a region in time and, optionally, onto another track. */
    void moveRegion (juce::ValueTree region, juce::ValueTree targetTrack, double newStart, juce::UndoManager* um);

    /** Moves the region's left edge; the audio stays where it is on the timeline. */
    void trimRegionStart (juce::ValueTree region, double newStart, double sourceLength, juce::UndoManager* um);

    /** Moves the region's right edge. */
    void trimRegionEnd (juce::ValueTree region, double newEnd, double sourceLength, juce::UndoManager* um);

    /** Splits at a project time. Returns the new right-hand region, or an invalid tree if `time` isn't inside the region. */
    juce::ValueTree splitRegion (juce::ValueTree region, double time, juce::UndoManager* um);

    /** A copy of a region (with new ids) on a track at a time. */
    juce::ValueTree duplicateRegion (const juce::ValueTree& region, juce::ValueTree track, double start, juce::UndoManager* um);

    /** Joins MIDI regions on one track into one spanning them all (Logic's Join). Returns it, or invalid if they can't be joined. */
    juce::ValueTree joinRegions (const juce::Array<juce::ValueTree>& regions, juce::UndoManager* um);

    /** "No Overlap" drag mode: shortens or removes the other regions on the track that this one now covers.
        `sourceLength` gives an audio region's file length (0 if unknown). */
    void clearOverlaps (const juce::ValueTree& region, std::function<double (const juce::ValueTree&)> sourceLength, juce::UndoManager* um);

    // --- Fades, crossfades, loops, shuffle --------------------------------------
    /** A fade's gain at x (0..1 through the fade, rising): straight for curve 0, equal power at 0.5. */
    inline double fadeGain (double x, double curve)
    {
        x = juce::jlimit (0.0, 1.0, x);
        return std::pow (x, std::pow (2.0, -2.0 * juce::jlimit (-1.0, 1.0, curve)));
    }

    /** Sets a region's fade in (or out) length in seconds, kept inside the region. */
    void setFade (juce::ValueTree region, bool fadeIn, double seconds, juce::UndoManager* um);

    /** Where a looped region stops repeating (its own end when it doesn't loop): its loop end,
        or the next region on the track if that comes first. */
    double loopEndOf (const juce::ValueTree& region);

    /** Turns looping on (repeating up to the next region, or three more times) or off. */
    void setLoop (juce::ValueTree region, bool shouldLoop, juce::UndoManager* um);

    /** X-Fade drag mode: where the region overlaps others on its track, both fade (equal power) over the overlap. */
    void createCrossfades (const juce::ValueTree& region, juce::UndoManager* um);

    /** Shuffle drag modes: the track's regions close up, in order, against the first (left) or the last (right). */
    void shuffleTrack (juce::ValueTree track, bool towardsLeft, juce::UndoManager* um);

    /** A muted region stays on screen but doesn't play. */
    inline bool isRegionMuted (const juce::ValueTree& region) { return region.getProperty (ids::mute, false); }

    /** Logic's New Track With Duplicate Settings: the same kind, instrument, fader, pan, colour, console,
        inserts and sends, but no regions; placed below. */
    juce::ValueTree duplicateTrackSettings (juce::ValueTree project, const juce::ValueTree& track, juce::UndoManager* um);

    /** File > Import > Tracks from another project: copies the chosen tracks of `source` (a loaded project)
        with their regions, inserts, sends (to buses that come along) and automation, under new ids,
        after the existing tracks. Returns the new tracks. */
    juce::Array<juce::ValueTree> importTracks (juce::ValueTree project, const juce::ValueTree& source,
                                               const juce::StringArray& trackIds, juce::UndoManager* um);

    /** Library patches: a track's sound (instrument, plugins, channel strip, sends to nowhere dropped) as a small
        project of its own, and putting one onto a track (replacing its sound; its regions stay). */
    juce::ValueTree makePatch (const juce::ValueTree& project, const juce::ValueTree& track);

    /** Reset Channel Strip: no inserts or sends, the console section flat, fader at 0 dB, pan centre. */
    void resetChannelStrip (juce::ValueTree project, juce::ValueTree track, juce::UndoManager* um);

    /** Logic's Region and Track parameters for a MIDI region's notes, as they play. */
    struct MidiPlayback
    {
        int transpose = 0, velocity = 0;
        double quantizeBeats = 0.0, swing = 50.0, dynamics = 100.0, gate = 100.0, delaySeconds = 0.0;
        int keyLow = 0, keyHigh = 127, velLow = 1, velHigh = 127;

        static MidiPlayback of (const juce::ValueTree& region, const juce::ValueTree& track);

        /** Shapes one note (absolute seconds); false if the track's key or velocity limits leave it out. */
        bool shape (const TempoMap& map, double& start, double& length, int& pitch, int& vel) const;
    };
    void applyPatch (juce::ValueTree project, juce::ValueTree track, const juce::ValueTree& patch, juce::UndoManager* um);

    /**
        Adds a region for a finished recording.

        `latencySeconds` is the round-trip latency: the recorded audio is shifted
        earlier by this much (via the region's offset) so it lines up with what
        the performer heard.
    */
    juce::ValueTree addRecordedRegion (juce::ValueTree track, const juce::File& file, double start,
                                       double recordedLength, double latencySeconds, juce::UndoManager* um);

    // --- Loop recording / take folders --------------------------------------

    /** One recorded pass: where it sits on the timeline and where it starts in the file. */
    struct TakePiece
    {
        double timelineStart = 0.0;
        double fileOffset = 0.0;
        double length = 0.0;
    };

    /**
        Splits one continuous recording into pieces on the timeline.

        Without a cycle (or if the recording never reached the cycle end) this
        is a single piece. With a cycle, the first piece runs from the record
        start to the cycle end and every later pass covers the whole cycle; the
        file was written continuously while the playhead wrapped.
    */
    std::vector<TakePiece> splitRecording (double recordStart, double recordedLength, double latency,
                                           bool cycleOn, double cycleStart, double cycleEnd);

    /** A region holding several takes of the same section; `activeIndex` is the one that plays. */
    juce::ValueTree addTakeFolder (juce::ValueTree track, const juce::File& file, double start, double length,
                                   const std::vector<double>& takeOffsets, int activeIndex, juce::UndoManager* um);

    int numTakes (const juce::ValueTree& region);
    void setActiveTake (juce::ValueTree region, int index, juce::UndoManager* um);

    /** Quick Swipe comping: in an open take folder, this take plays from `from` to `to` (seconds from the region's start). */
    void compSwipe (juce::ValueTree region, int takeIndex, double from, double to, juce::UndoManager* um);

    /** The comp, start to end: which take plays where (empty when there's no comp). */
    struct CompPiece { double from, to; int take; };
    std::vector<CompPiece> compPieces (const juce::ValueTree& region);

    /** Turns a comp into ordinary regions, one per stretch (Logic's Flatten). Returns them. */
    juce::Array<juce::ValueTree> flattenComp (juce::ValueTree region, juce::UndoManager* um);

    /** Keeps the active take and removes the others. */
    void flattenTakes (juce::ValueTree region, juce::UndoManager* um);

    // --- MIDI -------------------------------------------------------------------
    bool isInstrumentTrack (const juce::ValueTree& track);
    bool isMidiRegion (const juce::ValueTree& region);

    juce::ValueTree addMidiRegion (juce::ValueTree track, double start, double length, juce::UndoManager* um);

    /** Logic's "Audio Defaults" / "MIDI Defaults" Region box: a track's settings for regions made on it,
        kept on the track as "regionDefault_<property>". New regions start with them. */
    juce::Identifier regionDefaultId (const juce::Identifier& property);
    void applyRegionDefaults (const juce::ValueTree& track, juce::ValueTree region);

    /** How an audio region plays differently from its file (Region box: Transpose, Fine Tune,
        Pitch Source, Flex with Smart Tempo). `detectedBpm` is the file's tempo found by Smart Tempo. */
    struct AudioShape { double semitones = 0.0, ratio = 1.0; bool varispeed = false; bool needsTempo = false; };
    AudioShape audioShapeOf (const juce::ValueTree& project, const juce::ValueTree& track, const juce::ValueTree& region, double detectedBpm);

    /** `start` is measured from the beginning of the region's content (see Identifiers.h). */
    juce::ValueTree addNote (juce::ValueTree region, int pitch, double start, double length, int velocity, juce::UndoManager* um);
    void moveNote (juce::ValueTree note, double newStart, int newPitch, juce::UndoManager* um);
    void resizeNote (juce::ValueTree note, double newLength, juce::UndoManager* um);
    void setNoteVelocity (juce::ValueTree note, int velocity, juce::UndoManager* um);
    void deleteNote (juce::ValueTree note, juce::UndoManager* um);

    /** Moves every note's start to the nearest grid line (in content time). */
    void quantizeNotes (juce::ValueTree region, double gridSeconds, juce::UndoManager* um);

    /** Quantizes notes (in content time) to a grid, pulled `strength` (0..1) of the way, with swing
        (0..1, delays every second grid step by up to half a step). The grid follows the song's
        bars: `regionStart - offset` is where content time 0 falls in the song. */
    void quantizeNotesTo (const juce::Array<juce::ValueTree>& notes, double gridSeconds, double strength, double swing,
                          double contentToSong, juce::UndoManager* um);

    // Controllers and pitch bend
    inline constexpr int pitchBend = 128;
    juce::ValueTree addControllerEvent (juce::ValueTree region, int controller, double start, double value, juce::UndoManager* um);
    juce::Array<juce::ValueTree> controllerEvents (const juce::ValueTree& region, int controller);   // in time order
    juce::String controllerName (int controller);

    /** Scale Quantize: notes move to the nearest note of the key's scale (major or natural minor). */
    void scaleQuantize (const juce::Array<juce::ValueTree>& notes, int keyRoot, bool minor, juce::UndoManager* um);
    bool isInScale (int pitch, int keyRoot, bool minor);

    // MIDI Transform (Logic's presets, the common ones)
    enum class Transform { humanize, fixedVelocity, crescendo, decrescendo, legato, doubleSpeed, halfSpeed, transposeUp, transposeDown, reverse,
                           randomVelocity, invertPitch, reversePitch, noteRepeat };
    void transformNotes (const juce::Array<juce::ValueTree>& notes, Transform transform, juce::UndoManager* um, int seed = 0);

    /** A raw MIDI event captured while recording, at an absolute timeline position. */
    struct RecordedMidiEvent
    {
        double time = 0.0;
        juce::uint8 status = 0, data1 = 0, data2 = 0;
    };

    struct RecordedNote { int pitch; double start; double length; int velocity; };

    /**
        Pairs note-ons with note-offs. Times come back relative to `regionStart`.
        Notes still held at the end, or released after a cycle wrap, end at `regionEnd`.
    */
    std::vector<RecordedNote> notesFromEvents (const std::vector<RecordedMidiEvent>& events,
                                               double regionStart, double regionEnd);

    // --- Cycle ----------------------------------------------------------------
    void setCycle (juce::ValueTree project, double start, double end, bool enabled);

    void deleteRegion (juce::ValueTree region, juce::UndoManager* um);
    void deleteTrack (juce::ValueTree track, juce::UndoManager* um);

    /** Moves tracks (kept in their order, together) so the first lands at `newIndex` of the track list as it is now. */
    void moveTracks (juce::ValueTree project, const juce::Array<juce::ValueTree>& tracks, int newIndex, juce::UndoManager* um);

    /** The colours offered for tracks. */
    const juce::Array<juce::Colour>& trackPalette();

    // --- Mixer ----------------------------------------------------------------
    inline const juce::String masterOwner { "master" };

    /** Console mode: the chain holding the user's own bus compressor plugin for the master. */
    inline const juce::String busCompOwner { "busComp" };

    /** The bus compressor plugin on the master, or an invalid tree. */
    juce::ValueTree busCompPlugin (const juce::ValueTree& project);

    /** Puts a plugin in the master's bus compressor place, replacing any there. */
    juce::ValueTree setBusCompPlugin (juce::ValueTree project, const juce::String& descriptionXml, juce::UndoManager* um);

    /** Console mode: the chain holding a track's channel strip plugin (in place of the built-in EQ and Dynamics). */
    inline juce::String stripOwner (const juce::String& trackId) { return "strip:" + trackId; }

    /** A track's channel strip plugin, or an invalid tree. */
    juce::ValueTree stripPlugin (const juce::ValueTree& project, const juce::String& trackId);

    /** Puts a channel strip plugin on a track (replacing any), or takes it off with an empty description. */
    juce::ValueTree setStripPlugin (juce::ValueTree project, const juce::String& trackId, const juce::String& descriptionXml, juce::UndoManager* um);

    /**
        The console for the whole mixer: a channel strip plugin's description, or
        empty for the built-in strip. Setting it puts the plugin on every track
        (except ones switched to the built-in strip by hand), and new tracks get
        it too.
    */
    juce::String consolePlugin (const juce::ValueTree& project);
    void setConsolePlugin (juce::ValueTree project, const juce::String& descriptionXml, juce::UndoManager* um);
    inline const juce::String builtinKind { "builtin" }, pluginKind { "plugin" };

    /** The MIXER node, created (unundoably) if the project doesn't have one yet. */
    juce::ValueTree mixerOf (juce::ValueTree project);

    /** The insert chain of a track (or masterOwner), created if needed. */
    juce::ValueTree chainFor (juce::ValueTree project, const juce::String& owner);

    /** The chain if it exists, without creating it. */
    juce::ValueTree findChain (const juce::ValueTree& project, const juce::String& owner);

    juce::ValueTree addBuiltinInsert (juce::ValueTree chain, const juce::String& effectType, int index, juce::UndoManager* um);
    juce::ValueTree addPluginInsert (juce::ValueTree chain, const juce::String& descriptionXml, int index, juce::UndoManager* um);
    void removeInsert (juce::ValueTree insert, juce::UndoManager* um);

    /** Logic's insert slots: an insert stays in the slot you put it in, with empty slots above it if you like.
        (Projects from before slots: an insert's slot is its place in the chain.) */
    int insertSlot (const juce::ValueTree& insert);
    juce::ValueTree insertAtSlot (const juce::ValueTree& chain, int slot);

    /** Puts a new insert (made by `make` at a chain index) in a slot, replacing what's there, as part of the current undo step. */
    juce::ValueTree placeInsert (juce::ValueTree chain, int slot,
                                 const std::function<juce::ValueTree (juce::ValueTree chain, int index)>& make, juce::UndoManager* um);

    /** Dragging an insert (as in Logic): to another slot of its channel or of another channel (the owner's id),
        the inserts from that slot down moving down one to make room. `copy` (Option held) leaves the original
        where it was and places a copy with its settings. Its automation goes with a moved insert.
        Returns the insert in its new place. One undo step: the caller begins the transaction. */
    juce::ValueTree moveInsert (juce::ValueTree project, juce::ValueTree insert, const juce::String& toOwner, int slot,
                                bool copy, juce::UndoManager* um);

    // --- Buses and sends --------------------------------------------------------
    bool isBusTrack (const juce::ValueTree& track);

    /** Whether playing `track` into the track `targetId` would come back round to it (a bus into itself, through others). */
    /** Cubase-style ARA Extensions: whether some of the track's regions are marked for its ARA plugin
        (then only those are the plugin's, the rest play as usual; otherwise the plugin takes every region). */
    bool hasAraRegions (const juce::ValueTree& track);

    bool outputWouldLoop (const juce::ValueTree& project, const juce::ValueTree& track, const juce::String& targetId);

    /** A channel's output set to the audio interface's own outputs, past the master: "hw:2" is outputs 3-4,
        "hwm:4" output 5 alone. Returns { first output (0-based), 1 or 2 outputs }, or { -1, 0 } when it isn't one. */
    std::pair<int, int> hardwareOutput (const juce::String& outputId);
    juce::String hardwareOutputId (int firstOutput, bool mono);
    /** "Output 3-4" (or the interface's names for them, when given) */
    juce::String hardwareOutputName (const juce::String& outputId, const juce::StringArray& channelNames = {});

    /** Logic's channel format: a mono channel plays (and records) one channel, panned; stereo plays both.
        Unset means stereo. Switching also sets the input to a mono input or a stereo pair. */
    bool isStereoChannel (const juce::ValueTree& track);
    void setStereoChannel (juce::ValueTree track, bool stereo, juce::UndoManager* um);

    // --- Groups, VCAs, outputs, stacks, freeze ------------------------------------
    inline bool isVcaTrack (const juce::ValueTree& track) { return track[ids::kind].toString() == vcaKind; }

    /** The track and every other track in its group (just the track when it has none). */
    juce::Array<juce::ValueTree> groupMembers (const juce::ValueTree& project, const juce::ValueTree& track);

    /** Group settings: a group's name, and whether it links "volume", "mute", "solo" or "record" (all on unless switched off). */
    juce::String groupName (const juce::ValueTree& project, int group);
    bool groupLinks (const juce::ValueTree& project, int group, const juce::String& what);
    inline juce::Identifier groupProperty (int group, const juce::String& what) { return juce::Identifier ("group" + juce::String (group) + what); }

    /** Tracks controlled by a VCA (or folder stack) track. */
    juce::Array<juce::ValueTree> vcaMembers (const juce::ValueTree& project, const juce::ValueTree& vcaTrack);

    /** The VCA gain on a track (its VCA's fader, and that VCA's own VCA), and whether a VCA mutes or solos it. */
    double vcaGainOf (const juce::ValueTree& project, const juce::ValueTree& track);
    bool vcaMutes (const juce::ValueTree& project, const juce::ValueTree& track);
    bool vcaSolos (const juce::ValueTree& project, const juce::ValueTree& track);

    /** Logic's Create Track Stack: a folder stack (a VCA-like main track) or a summing stack (an aux bus
        the tracks play into). The tracks move under the new main track, which is returned. */
    juce::ValueTree createTrackStack (juce::ValueTree project, const juce::Array<juce::ValueTree>& tracks, bool summing, juce::UndoManager* um);

    /** Takes the tracks out of a stack and removes its main track. */
    void flattenStack (juce::ValueTree project, juce::ValueTree mainTrack, juce::UndoManager* um);

    /** True when the track is folded away inside a collapsed stack. */
    bool isInCollapsedStack (const juce::ValueTree& project, const juce::ValueTree& track);
    juce::ValueTree addSend (juce::ValueTree chain, const juce::String& busId, double level, juce::UndoManager* um);
    juce::Array<juce::ValueTree> sendsOf (const juce::ValueTree& chain);
    juce::Array<juce::ValueTree> insertsOf (const juce::ValueTree& chain);

    // --- Automation -------------------------------------------------------------
    inline const juce::String volumeParam { "volume" }, panParam { "pan" };

    juce::String insertParam (const juce::String& insertId, const juce::String& paramId);

    /** The lane for an owner's parameter: found, or created when `create` is true. */
    juce::ValueTree laneFor (juce::ValueTree project, const juce::String& owner, const juce::String& param, bool create);

    /** Adds a point, keeping the lane's points in time order. */
    juce::ValueTree addPoint (juce::ValueTree lane, double time, double value, juce::UndoManager* um);
    void movePoint (juce::ValueTree point, double time, double value, juce::UndoManager* um);
    void removePoint (juce::ValueTree point, juce::UndoManager* um);

    /** Removes every point of one lane (the lane itself goes too). Returns false if there was nothing to clear. */
    bool clearAutomation (juce::ValueTree project, const juce::String& owner, const juce::String& param, juce::UndoManager* um);

    /** Removes every automation lane that belongs to `owner` (a track id). Returns the number removed. */
    int clearAllAutomation (juce::ValueTree project, const juce::String& owner, juce::UndoManager* um);

    /** The lane's value at a time: linear between points, held before the first and after the last. */
    double valueAt (const juce::ValueTree& lane, double time, double fallback);

    struct ParamRange { double min, max, defaultValue; bool decibels; juce::String name; };

    /** What a parameter can be set to (volume is drawn in dB but stored as gain). */
    ParamRange rangeOf (const juce::ValueTree& project, const juce::String& param);

    // --- Tempo track ------------------------------------------------------------
    /** The TEMPO node (changes after the start; the starting tempo is the project's tempo), created if needed. */
    juce::ValueTree tempoTrackOf (juce::ValueTree project);

    /** A tempo change at a beat (quarter notes from the start). */
    juce::ValueTree addTempoChange (juce::ValueTree project, double beat, double bpm, juce::UndoManager* um);

    /** The changes in beat order. */
    juce::Array<juce::ValueTree> sortedTempoChanges (const juce::ValueTree& project);

    /** A time signature change at a bar (1-based): `beatsPerBar` beats a bar from there on. */
    juce::ValueTree addSignatureChange (juce::ValueTree project, int bar, int beatsPerBar, juce::UndoManager* um);
    juce::Array<juce::ValueTree> sortedSignatureChanges (const juce::ValueTree& project);

    /** A key change at a bar (1-based). */
    juce::ValueTree addKeyChange (juce::ValueTree project, int bar, int keyRoot, bool minor, juce::UndoManager* um);
    juce::Array<juce::ValueTree> sortedKeyChanges (const juce::ValueTree& project);

    /** The key at a time: root (0 = C) and minor. */
    std::pair<int, bool> keyAt (const juce::ValueTree& project, double seconds);
    juce::String keyName (int root, bool minor);

    /** The whole tempo map: the project's tempo, then each change. */
    TempoMap tempoMapOf (const juce::ValueTree& project);

    /**
        After the tempo changed (from `before` to the project's map now): MIDI regions and their notes, markers,
        the cycle and the punch range move so they stay on the same bars and beats, as in Logic. Audio stays put.
    */
    void followTempo (juce::ValueTree project, const TempoMap& before, juce::UndoManager* um);

    /** Sets the starting tempo, with everything on the grid following it. */
    void setTempo (juce::ValueTree project, double bpm, juce::UndoManager* um);

    /** Bars and beats for a time, using the tempo map: bar (1-based), beat (1-based), sixteenth (1-based), fraction. */
    struct BarPosition { int bar, beat, sixteenth; double fraction; };
    BarPosition barPositionAt (const juce::ValueTree& project, double seconds);

    /** The time `bars` bars away from `seconds` (on the bar grid). */
    double moveByBars (const juce::ValueTree& project, double seconds, int bars);

    // --- Editing whole sections of the song (Logic's locator edits, arrangement) ----
    /** Splits every region that crosses `a` or `b`. */
    void splitAtTimes (juce::ValueTree project, double a, double b, juce::UndoManager* um);

    /** Cuts the time from `a` to `b` out of the song: what's in it goes, everything after moves earlier. */
    void deleteSection (juce::ValueTree project, double a, double b, juce::UndoManager* um);

    /** Opens a gap from `a` to `b`: everything from `a` on moves later. */
    void insertSilence (juce::ValueTree project, double a, double b, juce::UndoManager* um);

    /** Plays the section from `a` to `b` twice: a copy right after it, everything later moved along. */
    void duplicateSection (juce::ValueTree project, double a, double b, juce::UndoManager* um);

    /** The section a marker starts: to the next marker (or the end of the song). */
    std::pair<double, double> markerSection (const juce::ValueTree& project, const juce::ValueTree& marker);

    // --- Live Loops ----------------------------------------------------------------
    /** The cell for a track and scene, or invalid. */
    juce::ValueTree liveCell (const juce::ValueTree& project, const juce::String& trackId, int scene);

    /** Puts a copy of a region into a cell (replacing what was there); it loops for its own length. */
    juce::ValueTree setLiveCell (juce::ValueTree project, const juce::String& trackId, int scene, const juce::ValueTree& region, juce::UndoManager* um);

    /** Copies what's on each track between `a` and `b` into the cells of a scene (Logic's "copy to scene"). */
    void captureScene (juce::ValueTree project, int scene, double a, double b, juce::UndoManager* um);

    // --- Markers (Logic's marker track) -----------------------------------------
    /** The MARKERS node (after the MIXER), created if needed. */
    juce::ValueTree markersOf (juce::ValueTree project);

    /** Adds a marker; with an empty name it's called "Marker n". */
    juce::ValueTree addMarker (juce::ValueTree project, double time, const juce::String& name, juce::UndoManager* um);

    /** The markers in time order. */
    juce::Array<juce::ValueTree> sortedMarkers (const juce::ValueTree& project);

    /** The first marker after `time` (or before it, with `forwards` false), or invalid. */
    juce::ValueTree adjacentMarker (const juce::ValueTree& project, double time, bool forwards);

    /** The song's key as text, e.g. "C major" or "F# minor". */
    juce::String keyName (const juce::ValueTree& project);

    // --- Queries ----------------------------------------------------------
    /** Tracks are the first children of the project; the MIXER node comes after them. */
    int numTracks (const juce::ValueTree& project);
    juce::ValueTree findById (const juce::ValueTree& project, const juce::String& id);
    juce::ValueTree trackOf (const juce::ValueTree& region);
    double regionEnd (const juce::ValueTree& region);
    double projectEnd (const juce::ValueTree& project);
    juce::Colour trackColour (const juce::ValueTree& track);
    double secondsPerBeat (const juce::ValueTree& project);
    juce::String inputName (const juce::ValueTree& track, const juce::StringArray& activeInputNames);

    // --- Files ------------------------------------------------------------
    /** Serialises to JSON. Region file paths are stored relative to `projectFile`'s folder when possible. */
    juce::String toJson (const juce::ValueTree& project, const juce::File& projectFile);

    /** Parses JSON written by toJson(). Returns an invalid tree and sets `error` on failure. */
    juce::ValueTree fromJson (const juce::String& json, const juce::File& projectFile, juce::String& error);

    bool save (const juce::ValueTree& project, const juce::File& file, juce::String& error);
    juce::ValueTree load (const juce::File& file, juce::String& error);

    inline const juce::String fileExtension { ".atp" };      // see AppInfo.h; ".riff" files still open
}
