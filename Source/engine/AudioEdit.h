#pragma once

#include <JuceHeader.h>

namespace riff::audioedit
{

/**
    Destructive edits for the Audio File Editor, and Flex-style time and
    pitch changes. They never touch the original file: the region's audio is
    read, processed, and written to a new file, which the region then plays.
*/
enum class Operation { normalize, gain, fadeIn, fadeOut, silence, reverse, invert, timeStretch, pitchShift };

struct Request
{
    Operation operation = Operation::normalize;
    double amount = 0.0;          // gain: dB; timeStretch: length ratio (2 = twice as long); pitchShift: semitones
    double from = 0.0, to = -1.0; // the selection, seconds into the region (to < 0: all of it)
};

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
