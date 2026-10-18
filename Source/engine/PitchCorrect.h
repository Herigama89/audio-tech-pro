#pragma once

#include <JuceHeader.h>

/**
    The pitch editor's engine (as Melodyne and Logic's Flex Pitch): a monophonic voice or instrument
    tracked as a pitch curve, cut into notes, and each note moved and straightened, rendered by
    pitch-synchronous overlap-add (PSOLA: the timing and the voice's character, its formants, stay).

    Edits are kept as numbers, and the audio is always rendered from the original recording, so
    changing a note again never wears the sound down.
*/
namespace riff::pitchcorrect
{
    struct Note
    {
        int startFrame = 0, endFrame = 0;   // [start, end) in analysis frames
        double centre = 0.0;                // the pitch it's sung at (MIDI note, fractional): the median of its curve
        float level = 0.0f;                 // its loudness (RMS, linear)
    };

    struct Analysis
    {
        double sampleRate = 48000.0;
        int hop = 256;                      // samples per frame
        int numSamples = 0;
        std::vector<float> midi;            // the pitch curve per frame (MIDI note, fractional); 0: unpitched
        std::vector<float> level;           // RMS per frame (linear)
        std::vector<Note> notes;

        double frameToSeconds (double frame) const { return frame * hop / sampleRate; }
    };

    /** Tracks the pitch (YIN, every ~5 ms) and finds the notes. */
    Analysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** What's been done to one note. */
    struct NoteEdit
    {
        double shift = 0.0;   // semitones (fractional: cents)
        double drift = 0.0;   // 0..1: how far its wobble (drift and vibrato) is straightened towards its centre
        double gain = 0.0;    // dB
        bool operator== (const NoteEdit&) const = default;
        bool isNeutral() const { return std::abs (shift) < 1.0e-4 && drift < 1.0e-4 && std::abs (gain) < 1.0e-4; }
    };

    /** The audio with the notes moved (one edit per note, in the analysis' order). */
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& audio, const Analysis& analysis, const std::vector<NoteEdit>& edits);

    /** The edits as text (kept on the region) and back; the list is sized to `numNotes`. */
    juce::String toString (const std::vector<NoteEdit>& edits);
    std::vector<NoteEdit> fromString (const juce::String& text, size_t numNotes);

    /** Pitch correction (Logic's slider): each note moved `amount` (0..1) of the way to the nearest semitone,
        or the nearest note of `scale` (pitch classes 0..11, C = 0) when it's given. */
    void correct (std::vector<NoteEdit>& edits, const Analysis& analysis, const std::vector<int>& which, double amount,
                  const std::vector<int>& scale = {});
}
