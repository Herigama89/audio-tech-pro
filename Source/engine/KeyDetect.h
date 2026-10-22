#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

/**
    The song's key and scale, found by listening (as Logic's and Mixed In Key's key detection): the notes heard,
    folded into one octave (a chromagram - how much of each of the 12 pitch classes there is, from the spectral
    peaks of the audio and the notes of MIDI regions), matched against how often each degree sounds in music in
    each major and minor key (Krumhansl and Kessler's key profiles). The best match is the key; how far ahead of
    the next it is says how sure.
*/
namespace riff::keydetect
{
    using Chroma = std::array<double, 12>;   // C = 0

    /** The pitch classes heard in some audio (each moment counted alike, silence skipped), allowing for a
        recording tuned off A = 440 Hz. */
    Chroma chromaOf (const juce::AudioBuffer<float>& audio, double sampleRate);

    struct Candidate { int root = 0; bool minor = false; double score = 0.0; };
    struct Result
    {
        int root = 0;           // pitch class, C = 0
        bool minor = false;
        double score = 0.0;     // correlation with the key's profile, -1..1
        double confidence = 0.0;   // 0..1: how far ahead of the next key (not its relative)
        std::vector<Candidate> ranking;   // all 24, best first
        bool found = false;
    };

    Result detect (const Chroma& chroma);

    /** "A minor", "E♭ major" (flats for the flat keys). */
    juce::String name (int root, bool minor);
}
