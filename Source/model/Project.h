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

    juce::ValueTree addTrack (juce::ValueTree project, const juce::String& name, juce::UndoManager* um, int index = -1);

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

    // --- Cycle ----------------------------------------------------------------
    void setCycle (juce::ValueTree project, double start, double end, bool enabled);

    void deleteRegion (juce::ValueTree region, juce::UndoManager* um);
    void deleteTrack (juce::ValueTree track, juce::UndoManager* um);

    // --- Queries ----------------------------------------------------------
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

    inline const juce::String fileExtension { ".riff" };
}
