#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

/**
    Drum parts (as Logic's Stem Splitter splitting the drums): a drum recording - a drum stem, a loop, a kit's
    room mic - taken apart into kick, snare, toms, hi-hat and cymbals.

    How: the drums' spectrogram is factorised (non-negative matrix factorisation) into a few sounds, each started
    from what that part of a kit sounds like (the kick's low thump and beater click, the snare's body and wires,
    the toms' pitched bodies, the metal's highs) and left to learn the actual kit; each part then gets a soft mask
    of the spectrogram. The metal is split into hi-hat (short, ticking) and cymbals (washing, sustained) by how
    long its sound lasts. The parts add back up to the recording.
*/
namespace riff::drumsplit
{
    /** The parts, in the order split() returns them. */
    juce::StringArray partNames();   // Kick, Snare, Toms, Hi-Hat, Cymbals

    /** The parts of `drums` (any channels: stereo out, each the input's length). `progress` (0..1) returns false
        to cancel (then the result is empty). */
    std::vector<juce::AudioBuffer<float>> split (const juce::AudioBuffer<float>& drums, double sampleRate,
                                                 const std::function<bool (float)>& progress = {});
}
