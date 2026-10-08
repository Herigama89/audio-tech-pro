#pragma once

#include <JuceHeader.h>

/**
    Names used in the project tree.

    PROJECT  { name, tempo, timeSigNumerator, masterGain, metronome,
               cycleEnabled, cycleStart, cycleEnd, countIn }
      TRACK  { id, name, colour, volume, pan, mute, solo, armed, monitor, input, inputStereo,
               kind ("audio" | "instrument"),
               instrument ("synth" | "plugin"), pluginDescription (XML), pluginState (base64),
               synthWave, synthCutoff, synthResonance, synthAttack, synthDecay, synthSustain,
               synthRelease, synthGain }
        REGION { id, name, file, start, offset, length, gain, activeTake?, midi? }
          TAKE { id, name, file, offset }       (only in take folders from loop recording;
                                                 the region's file/offset mirror the active take)
          NOTE { pitch, start, length, velocity }   (only in MIDI regions)

    All times are in seconds (double). The engine converts them to samples
    at the device's sample rate when it builds its playback graph.

    A region plays its content from `offset` for `length` seconds. For audio
    the content is the file; for MIDI it's the notes, whose `start` is measured
    from the beginning of that content. So trimming and splitting work the same
    way for both: only the window moves, the notes stay put.
*/
namespace riff::ids
{
    #define RIFF_DECLARE_ID(name) inline const juce::Identifier name { #name };

    // Types
    RIFF_DECLARE_ID (PROJECT)
    RIFF_DECLARE_ID (TRACK)
    RIFF_DECLARE_ID (REGION)
    RIFF_DECLARE_ID (TAKE)
    RIFF_DECLARE_ID (NOTE)

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
    RIFF_DECLARE_ID (cycleEnabled)
    RIFF_DECLARE_ID (cycleStart)
    RIFF_DECLARE_ID (cycleEnd)
    RIFF_DECLARE_ID (countIn)      // one bar of click before recording
    RIFF_DECLARE_ID (activeTake)   // index of the TAKE child that's playing

    // MIDI and instruments
    RIFF_DECLARE_ID (kind)
    RIFF_DECLARE_ID (midi)
    RIFF_DECLARE_ID (pitch)
    RIFF_DECLARE_ID (velocity)
    RIFF_DECLARE_ID (instrument)
    RIFF_DECLARE_ID (pluginDescription)
    RIFF_DECLARE_ID (pluginState)
    RIFF_DECLARE_ID (synthWave)       // 0 saw, 1 square, 2 triangle, 3 sine
    RIFF_DECLARE_ID (synthCutoff)     // Hz
    RIFF_DECLARE_ID (synthResonance)  // 0..1
    RIFF_DECLARE_ID (synthAttack)     // seconds
    RIFF_DECLARE_ID (synthDecay)
    RIFF_DECLARE_ID (synthSustain)    // 0..1
    RIFF_DECLARE_ID (synthRelease)
    RIFF_DECLARE_ID (synthGain)       // linear

    #undef RIFF_DECLARE_ID
}
