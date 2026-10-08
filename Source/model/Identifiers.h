#pragma once

#include <JuceHeader.h>

/**
    Names used in the project tree.

    PROJECT  { name, tempo, timeSigNumerator, masterGain, metronome }
      TRACK  { id, name, colour, volume, pan, mute, solo, armed, monitor, input, inputStereo }
        REGION { id, name, file, start, offset, length, gain }

    All times are in seconds (double). The engine converts them to samples
    at the device's sample rate when it builds its playback graph.
*/
namespace riff::ids
{
    #define RIFF_DECLARE_ID(name) inline const juce::Identifier name { #name };

    // Types
    RIFF_DECLARE_ID (PROJECT)
    RIFF_DECLARE_ID (TRACK)
    RIFF_DECLARE_ID (REGION)

    // Properties
    RIFF_DECLARE_ID (id)
    RIFF_DECLARE_ID (name)
    RIFF_DECLARE_ID (tempo)
    RIFF_DECLARE_ID (timeSigNumerator)
    RIFF_DECLARE_ID (masterGain)
    RIFF_DECLARE_ID (colour)
    RIFF_DECLARE_ID (volume)
    RIFF_DECLARE_ID (pan)
    RIFF_DECLARE_ID (mute)
    RIFF_DECLARE_ID (solo)
    RIFF_DECLARE_ID (file)
    RIFF_DECLARE_ID (start)
    RIFF_DECLARE_ID (offset)
    RIFF_DECLARE_ID (length)
    RIFF_DECLARE_ID (gain)
    RIFF_DECLARE_ID (metronome)
    RIFF_DECLARE_ID (armed)        // record-enabled
    RIFF_DECLARE_ID (monitor)      // hear the input through the track while armed
    RIFF_DECLARE_ID (input)        // first input channel (index among the device's active inputs)
    RIFF_DECLARE_ID (inputStereo)  // true: record `input` and `input + 1`

    #undef RIFF_DECLARE_ID
}
