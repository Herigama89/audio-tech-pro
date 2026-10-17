#pragma once

#include <JuceHeader.h>

/**
    The piano roll's note tools, as FL Studio's (Tools menu and the toolbar), on a region's notes
    (content time, seconds). Each is part of the caller's undo step.
*/
namespace riff::notetools
{
    /** Chop: each note cut into pieces of `step` (the last piece may be shorter). */
    void chop (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double step, juce::UndoManager* um);

    /** Glue: notes of the same pitch that touch (or overlap) become one. */
    void glue (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, juce::UndoManager* um);

    /** Slice: the notes under a time cut in two there. */
    void slice (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double at, juce::UndoManager* um);

    /** Strum: the notes of each chord (same start) spread out, lowest first (or highest first), `spread` seconds apart. */
    void strum (const juce::Array<juce::ValueTree>& notes, double spread, bool downwards, juce::UndoManager* um);

    /** Flam: a softer grace note `before` seconds ahead of each note. */
    void flam (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double before, juce::UndoManager* um);

    /** Arpeggiate: each chord played one note at a time, `step` apart, for as long as it lasts.
        pattern: 0 up, 1 down, 2 up and down. */
    void arpeggiate (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double step, int pattern, juce::UndoManager* um);

    /** Randomize: start (up to `timing` seconds either way) and velocity (up to `velocity` either way). */
    void randomize (const juce::Array<juce::ValueTree>& notes, double timing, int velocity, int seed, juce::UndoManager* um);

    /** Scale Levels: velocities scaled (percent) about their average, and offset. */
    void scaleVelocity (const juce::Array<juce::ValueTree>& notes, double percent, int offset, juce::UndoManager* um);

    /** Limit: notes moved by octaves into a key range. */
    void limit (const juce::Array<juce::ValueTree>& notes, int lowest, int highest, juce::UndoManager* um);

    /** Claw Machine: of every `group` notes (in time order), keep the first `keep`; the rest go. */
    void claw (const juce::Array<juce::ValueTree>& notes, int group, int keep, juce::UndoManager* um);

    /** Articulate: every note's length a percentage of the gap to the next note (staccato to legato). */
    void articulate (const juce::Array<juce::ValueTree>& notes, double percent, juce::UndoManager* um);

    /** Chord stamps: name and intervals above the root. */
    struct Stamp { const char* name; std::vector<int> intervals; };
    const std::vector<Stamp>& stamps();

    /** Mute: notes that don't play (greyed). */
    void setMuted (const juce::Array<juce::ValueTree>& notes, bool muted, juce::UndoManager* um);
    bool isMuted (const juce::ValueTree& note);

    /** Copies of the notes, `shift` seconds later (Duplicate, and paste). Returns the copies. */
    juce::Array<juce::ValueTree> duplicate (juce::ValueTree region, const juce::Array<juce::ValueTree>& notes, double shift, juce::UndoManager* um);
}
