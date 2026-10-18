#pragma once

#include <JuceHeader.h>

namespace riff::audioedit
{

/**
    Destructive edits for the Audio File Editor, and Flex-style time and
    pitch changes. They never touch the original file: the region's audio is
    read, processed, and written to a new file, which the region then plays.
*/
enum class Operation { normalize, gain, fadeIn, fadeOut, silence, reverse, invert, timeStretch, pitchShift, flexQuantize, flexWarp };

struct Request
{
    Operation operation = Operation::normalize;
    double amount = 0.0;          // gain: dB; timeStretch: length ratio (2 = twice as long); pitchShift: semitones
    double from = 0.0, to = -1.0; // the selection, seconds into the region (to < 0: all of it)

    /** flexQuantize: where a time in the audio (seconds from its start) should land (seconds from its start). */
    std::function<double (double)> target;

    /** flexWarp: flex markers moved by hand, as (from, to) seconds from the region's start, in order.
        The audio between neighbouring markers stretches to fit; the rest stays. */
    std::vector<std::pair<double, double>> anchors;
};

/** The onsets (hits) in the audio, in samples. */
std::vector<int> detectTransients (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Smart Tempo: the tempo the audio is played at (70 to 180 BPM), or 0 if there's no clear beat. */
double detectTempo (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Flex Time quantize: every hit moves to where `target` says, the audio between them stretched to fit (pitch kept). */
/** Flex markers: each (from, to) pair, in seconds, moves that point of the audio; the audio between stretches. */
juce::AudioBuffer<float> flexWarp (const juce::AudioBuffer<float>& audio, double sampleRate, const std::vector<std::pair<double, double>>& anchors);

juce::AudioBuffer<float> flexQuantize (const juce::AudioBuffer<float>& audio, double sampleRate, const std::function<double (double)>& target);

/** The pitch (Hz) of the audio around a sample position (YIN), or 0 when it isn't pitched. */
double detectPitch (const juce::AudioBuffer<float>& audio, double sampleRate, int centre);

/** Flex Pitch: the notes (from hit to hit) with the pitch each is sung or played at. */
struct PitchedNote { int start, end; double midi; };   // samples; midi 0 when unpitched
std::vector<PitchedNote> detectNotes (const juce::AudioBuffer<float>& audio, double sampleRate);

/**
    Audio alignment (as VocAlign and Cubase's AudioAlign): how to warp a dub so its timing follows a guide.
    Both are compared as loudness in frequency bands every 10 ms, and matched (dynamic time warping), each moment
    of the dub allowed to move up to `maxShift` seconds. `dubOffset` is where the dub starts after the guide's start
    (seconds, negative when before). `strength` 0..1: how far each moment goes towards the guide.
    Returns flexWarp markers: (from, to) seconds from the dub's start.
*/
std::vector<std::pair<double, double>> alignToGuide (const juce::AudioBuffer<float>& guide, double guideRate,
                                                     const juce::AudioBuffer<float>& dub, double dubRate,
                                                     double dubOffset, double strength = 1.0, double maxShift = 0.5);

/** Consolidate: audio regions (as they play: gain, fades, transpose) mixed into one new file, gaps silent. */
struct ConsolidatePart
{
    juce::File file;
    double offset = 0.0, length = 0.0, start = 0.0;   // seconds: into the file, how long, where on the timeline
    float gain = 1.0f;
    double fadeIn = 0.0, fadeOut = 0.0, fadeInCurve = 0.0, fadeOutCurve = 0.0;
    double semitones = 0.0;                            // the region's transpose (and fine tune)
};
/** Writes them as one 24-bit stereo WAV at the first file's rate; `start` and `length` get where it goes on the timeline. */
juce::String consolidate (juce::AudioFormatManager& formats, const std::vector<ConsolidatePart>& parts, const juce::File& dest,
                          double& start, double& length);

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
