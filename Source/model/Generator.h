#pragma once

#include <JuceHeader.h>
#include <vector>

/**
    The MIDI genre generator: writes drums, bass, chords and a melody in the
    style of a genre, in a key, for a number of bars.

    Pure and repeatable: the same settings and seed always give the same
    notes. apply() puts the result into a project as instrument tracks with
    MIDI regions (one undo step).
*/
namespace riff::generator
{
    struct Genre
    {
        const char* name;
        double tempo;          // BPM
        float swing;           // 0 straight .. 1 full triplet swing (on 16ths)
        bool minor;            // the usual scale
        const char* about;     // one line for the window
    };

    const std::vector<Genre>& genres();

    enum class PartKind { drums, bass, chords, melody };

    struct Settings
    {
        int genre = 0;
        int root = 0;          // 0 = C .. 11 = B
        bool minor = true;
        int bars = 8;
        bool drums = true, bass = true, chords = true, melody = true;
        float variation = 0.5f; // 0 = steady loop .. 1 = lots of fills and changes
        juce::uint32 seed = 1;
    };

    struct Note
    {
        int pitch;
        double start;          // beats from the start of the part
        double length;         // beats
        int velocity;
    };

    struct Part
    {
        PartKind kind;
        juce::String name;
        std::vector<Note> notes;
    };

    struct Result
    {
        double tempo = 120.0;
        std::vector<Part> parts;
    };

    Result generate (const Settings& settings);

    /** General MIDI drum notes the drum parts use. */
    namespace gm
    {
        constexpr int kick = 36, rim = 37, snare = 38, clap = 39, closedHat = 42, pedalHat = 44, openHat = 46,
                      lowTom = 45, highTom = 48, crash = 49, ride = 51, cowbell = 56, shaker = 70;
    }

    /** The pitch classes (0..11) of a key's scale. */
    std::vector<int> scaleOf (int root, bool minor);

    juce::String keyName (int root, bool minor);

    /**
        Adds the result to a project: an instrument track per part with one MIDI
        region of `bars` bars starting at `startSeconds`, named after the genre.
        Sets the project's tempo first when `setTempo` is true. Returns the new tracks.
    */
    juce::Array<juce::ValueTree> apply (juce::ValueTree project, const Result& result, const Settings& settings,
                                        double startSeconds, bool setTempo, juce::UndoManager* um);

    /**
        Logic's Drummer and Session Players: a region that a player plays, in a genre, set by
        how complex and how loud (an XY pad), fills, swing and (for drums) which pieces. Its
        notes are written from those settings; change one and they're written again.
    */
    namespace player
    {
        inline const juce::Identifier kind { "player" };            // "drums", "bass" or "keys"
        inline const juce::Identifier genre { "playerGenre" };
        inline const juce::Identifier complexity { "playerComplexity" };   // 0 simple .. 1 complex
        inline const juce::Identifier loudness { "playerLoudness" };       // 0 soft .. 1 loud
        inline const juce::Identifier fills { "playerFills" };             // 0 .. 1
        inline const juce::Identifier swing { "playerSwing" };             // 0 .. 1 (on top of the genre's)
        inline const juce::Identifier pieces { "playerPieces" };           // drums: bit 0 kick+snare, 1 hats, 2 cymbals, 3 toms, 4 percussion
        inline const juce::Identifier seed { "playerSeed" };

        /** Writes the region's notes again from its settings. */
        void regenerate (juce::ValueTree region, juce::UndoManager* um);

        /** A new player track (Drum Kit for drums, the synth for bass and keys) with a region of `bars` bars at `start`. */
        juce::ValueTree addPlayerTrack (juce::ValueTree project, const juce::String& kind, double start, int bars, juce::UndoManager* um);
    }
}
