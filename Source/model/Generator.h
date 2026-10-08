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
}
