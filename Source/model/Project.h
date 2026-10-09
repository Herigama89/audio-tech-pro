#pragma once

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

    inline const juce::String audioKind { "audio" }, instrumentKind { "instrument" }, busKind { "bus" };

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

    /** Keeps the active take and removes the others. */
    void flattenTakes (juce::ValueTree region, juce::UndoManager* um);

    // --- MIDI -------------------------------------------------------------------
    bool isInstrumentTrack (const juce::ValueTree& track);
    bool isMidiRegion (const juce::ValueTree& region);

    juce::ValueTree addMidiRegion (juce::ValueTree track, double start, double length, juce::UndoManager* um);

    /** `start` is measured from the beginning of the region's content (see Identifiers.h). */
    juce::ValueTree addNote (juce::ValueTree region, int pitch, double start, double length, int velocity, juce::UndoManager* um);
    void moveNote (juce::ValueTree note, double newStart, int newPitch, juce::UndoManager* um);
    void resizeNote (juce::ValueTree note, double newLength, juce::UndoManager* um);
    void setNoteVelocity (juce::ValueTree note, int velocity, juce::UndoManager* um);
    void deleteNote (juce::ValueTree note, juce::UndoManager* um);

    /** Moves every note's start to the nearest grid line (in content time). */
    void quantizeNotes (juce::ValueTree region, double gridSeconds, juce::UndoManager* um);

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

    // --- Buses and sends --------------------------------------------------------
    bool isBusTrack (const juce::ValueTree& track);
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
