#pragma once

#include <JuceHeader.h>

/**
    What a loop or sample is, for the Loop Browser: which instrument (its tab), loop or one-shot, its tempo and key,
    how long. Read from its name and folder first ("Dub Bass 90bpm Amin.wav"); what the name doesn't say is found by
    listening to it (a loop's tempo from where its hits fall and how long it is; its key from which notes sound most).
*/
namespace riff::sampleinfo
{
    enum class Category { drums, bass, keys, synth, guitar, strings, vocals, percussion, fx, other };
    juce::String nameOf (Category c);
    constexpr int numCategories = 10;

    struct Info
    {
        Category category = Category::other;
        bool loop = false;
        double bpm = 0.0;        // 0: none (a one-shot, or not found)
        int keyRoot = -1;        // 0 = C .. 11 = B; -1: none (drums, noise)
        bool minor = false;
        bool keyIsNote = false;  // a one-shot's single note (no major / minor)
        double seconds = 0.0;
        double bars = 0.0;       // a loop's length in 4/4 bars at its tempo

        juce::var toVar() const;
        static Info fromVar (const juce::var& v);
    };

    /** What the file's name and folder say (category, loop or one-shot, tempo, key): unknowns left as they are. */
    Info fromName (const juce::File& file, bool& categoryKnown, bool& loopKnown);

    /** Name first, then the sound: everything there is to know about it (reads up to 60 s of it). */
    Info analyse (const juce::File& file, juce::AudioFormatManager& formats);

    /** The parts that listen (exposed for the tests). */
    double tempoOf (const juce::AudioBuffer<float>& audio, double rate);          // 0: no steady beat
    std::pair<int, bool> keyOf (const juce::AudioBuffer<float>& audio, double rate, bool& isNote, float& confidence);

    /** "A min", "F# maj", "C" (a note); "" for none. */
    juce::String keyName (const Info& info);

    /** Semitones to move a sample in (root, minor) to fit a song in (songRoot, songMinor): the nearest way, up to
        a tritone; a minor sample fits a major song as its relative minor (A minor in C major: 0). */
    int shiftToKey (int root, bool minor, int songRoot, bool songMinor);

    /** The root the region's "follows the key" starts from (Pitch Source), with the same relative-key rule. */
    int sourceRootFor (int root, bool minor, bool songMinor);
}
