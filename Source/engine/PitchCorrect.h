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

    /** What's been done to one note (Logic's Flex Pitch hot spots and Pitch Correction). */
    struct NoteEdit
    {
        double shift = 0.0;     // semitones (fractional: cents)
        double drift = 0.0;     // 0..1: its slow drift (scoops, sags) flattened towards its centre
        double gain = 0.0;      // dB
        double vibrato = 1.0;   // its fast wobble: 0 none, 1 as sung, up to 2 twice as wide
        double formant = 0.0;   // semitones: the voice's character brighter (up) or darker (down), the pitch kept
        double snap = 0.0;      // 0..1: Pitch Correction, how far its centre goes onto the nearest semitone
        double time = 0.0;      // seconds: moved later (or earlier), the audio either side stretching
        bool operator== (const NoteEdit&) const = default;
        bool isNeutral() const
        {
            return std::abs (shift) < 1.0e-4 && drift < 1.0e-4 && std::abs (gain) < 1.0e-4 && std::abs (vibrato - 1.0) < 1.0e-4
                && std::abs (formant) < 1.0e-4 && snap < 1.0e-4 && std::abs (time) < 1.0e-5;
        }
    };

    /** Where a note's centre sounds after its edit (its shift and correction). */
    double centreAfter (const Note& note, const NoteEdit& edit);

    /** A note's pitch curve after its edit (frames startFrame..endFrame-1; 0 where unpitched): drift, vibrato, shift. */
    std::vector<float> curveAfter (const Analysis& analysis, int noteIndex, const NoteEdit& edit);

    /** Split a note at a frame (both halves keep its edit), or join neighbouring notes into one (the first one's edit). */
    void splitNote (Analysis& analysis, std::vector<NoteEdit>& edits, int noteIndex, int frame);
    void joinNotes (Analysis& analysis, std::vector<NoteEdit>& edits, std::vector<int> noteIndices);

    /** The notes' boundaries as text (kept on the region after a split or join) and back. */
    juce::String segmentsToString (const Analysis& analysis);
    void applySegments (Analysis& analysis, const juce::String& text);

    /** The audio with the notes moved (one edit per note, in the analysis' order). */
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& audio, const Analysis& analysis, const std::vector<NoteEdit>& edits);

    /** The samples a change to one note touches (the note, with a little either side). */
    std::pair<int, int> noteSpan (const Analysis& analysis, int noteIndex);

    /** Renders samples [from, to) of the result into `dest` (as long as the audio; its channels from the audio's),
        crossfading into what's there at the ends: for hearing changes to a few notes straight away. */
    void renderInto (juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& audio, const Analysis& analysis,
                     const std::vector<NoteEdit>& edits, int from, int to);

    /** The edits as text (kept on the region) and back; the list is sized to `numNotes`. */
    juce::String toString (const std::vector<NoteEdit>& edits);
    std::vector<NoteEdit> fromString (const juce::String& text, size_t numNotes);

    /** Pitch correction (Logic's slider): each note moved `amount` (0..1) of the way to the nearest semitone,
        or the nearest note of `scale` (pitch classes 0..11, C = 0) when it's given. */
    void correct (std::vector<NoteEdit>& edits, const Analysis& analysis, const std::vector<int>& which, double amount,
                  const std::vector<int>& scale = {});
}
