#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

/**
    Drum parts (as Logic's Stem Splitter splitting the drums): a drum recording - a drum stem, a loop, a kit's
    room mic - taken apart into kick, snare, toms, hi-hat and cymbals.

    How: the hits first, then the sounds. Each hit is found where the spectrum suddenly rises, and told apart by
    what rose and how it dies away (a kick's falling thump, a snare's body with the wires' noise, a tom's steady
    ring, a hi-hat's short tick, a cymbal's long wash). Each part's sound - its picture over ~85 ms, all its
    frequencies - is learnt from the kit's own hits, and the recording is explained by those sounds struck at their
    hits (convolutive NMF guided by the hits); each part gets its share of the spectrogram. The parts add back up
    to the recording.
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
