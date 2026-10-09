#pragma once

#include <JuceHeader.h>

namespace riff::audioedit
{

/**
    Destructive edits for the Audio File Editor, and Flex-style time and
    pitch changes. They never touch the original file: the region's audio is
    read, processed, and written to a new file, which the region then plays.
*/
enum class Operation { normalize, gain, fadeIn, fadeOut, silence, reverse, invert, timeStretch, pitchShift, flexQuantize };

struct Request
{
    Operation operation = Operation::normalize;
    double amount = 0.0;          // gain: dB; timeStretch: length ratio (2 = twice as long); pitchShift: semitones
    double from = 0.0, to = -1.0; // the selection, seconds into the region (to < 0: all of it)

    /** flexQuantize: where a time in the audio (seconds from its start) should land (seconds from its start). */
    std::function<double (double)> target;
};

/** The onsets (hits) in the audio, in samples. */
std::vector<int> detectTransients (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Smart Tempo: the tempo the audio is played at (70 to 180 BPM), or 0 if there's no clear beat. */
double detectTempo (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Flex Time quantize: every hit moves to where `target` says, the audio between them stretched to fit (pitch kept). */
juce::AudioBuffer<float> flexQuantize (const juce::AudioBuffer<float>& audio, double sampleRate, const std::function<double (double)>& target);

/** The pitch (Hz) of the audio around a sample position (YIN), or 0 when it isn't pitched. */
double detectPitch (const juce::AudioBuffer<float>& audio, double sampleRate, int centre);

/** Flex Pitch: the notes (from hit to hit) with the pitch each is sung or played at. */
struct PitchedNote { int start, end; double midi; };   // samples; midi 0 when unpitched
std::vector<PitchedNote> detectNotes (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Reads `length` seconds of a file from `offset` (for analysis). */
juce::AudioBuffer<float> readRegion (juce::AudioFormatManager& formats, const juce::File& file, double offset, double length, double& sampleRate);

/** Time-stretches without changing pitch (WSOLA). `ratio` > 1 makes it longer. */
juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& input, double ratio, double sampleRate);

/** Changes pitch without changing length: stretch, then resample. */
juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& input, double semitones, double sampleRate);

/** Applies one edit to a buffer (in place, or replacing it for stretches). */
void apply (juce::AudioBuffer<float>& audio, const Request& request, double sampleRate);

/**
    Reads `length` seconds of `source` from `offset`, applies the edit and
    writes `dest` (24-bit WAV). Returns an error, or empty; `newLength` gets
    the result's length in seconds.
*/
juce::String processFile (juce::AudioFormatManager& formats, const juce::File& source, double offset, double length,
                          const Request& request, const juce::File& dest, double& newLength);

} // namespace riff::audioedit
