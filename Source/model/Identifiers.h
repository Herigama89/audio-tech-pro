#pragma once

#include <JuceHeader.h>

/**
    Names used in the project tree.

    PROJECT  { name, tempo, timeSigNumerator, masterGain, metronome,
               cycleEnabled, cycleStart, cycleEnd, countIn, busComp* (Master Bus Compressor) }
      TRACK  { id, name, colour, volume, pan, mute, solo, armed, monitor, input, inputStereo,
               kind ("audio" | "instrument" | "bus"), showAutomation, automationParam,
               instrument ("synth" | "plugin"), pluginDescription (XML), pluginState (base64),
               synthWave, synthCutoff, synthResonance, synthAttack, synthDecay, synthSustain,
               synthRelease, synthGain, consoleIn, consoleHF, consoleHMF, consoleLMF, consoleLF,
               consoleDynamics }
        REGION { id, name, file, start, offset, length, gain, activeTake?, midi?,
                 fadeIn, fadeOut, fadeInCurve, fadeOutCurve, loop, loopEnd }
          TAKE { id, name, file, offset }       (only in take folders from loop recording;
                                                 the region's file/offset mirror the active take)
          NOTE { pitch, start, length, velocity }   (only in MIDI regions)
      MIXER                                     (always the last child of PROJECT)
        CHAIN { owner }                         (owner = a track id, or "master")
          INSERT { id, kind ("builtin" | "plugin"), effect, pluginDescription,
                   pluginState, bypass, <effect parameters by name> }
          SEND { id, target (a bus track id), level (linear), pre (pre-fader) }
        LANE { owner, param }                   (automation; param is "volume", "pan",
          POINT { time, value }                  or "insert:<insert id>:<parameter id>")

    Tracks are the PROJECT's children before MIXER; use model::numTracks().
    Insert chains live apart from tracks so that a track's children stay
    just its regions, and so undoing a track deletion brings its effects back.

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
    RIFF_DECLARE_ID (CC)           // in MIDI regions: { start, controller (0..127, 128 = pitch bend), value (0..1) }
    RIFF_DECLARE_ID (MIXER)
    RIFF_DECLARE_ID (CHAIN)
    RIFF_DECLARE_ID (INSERT)
    RIFF_DECLARE_ID (SEND)
    RIFF_DECLARE_ID (LANE)
    RIFF_DECLARE_ID (POINT)
    RIFF_DECLARE_ID (MARKERS)         // after MIXER: the marker track
    RIFF_DECLARE_ID (MARKER)          // { id, name, time }
    RIFF_DECLARE_ID (TEMPO)           // after MIXER: the tempo track's changes
    RIFF_DECLARE_ID (COMPSEG)         // in a take folder: { from, to (seconds from the region's start), take (index) }
    RIFF_DECLARE_ID (from)
    RIFF_DECLARE_ID (to)
    RIFF_DECLARE_ID (take)
    RIFF_DECLARE_ID (takesOpen)       // REGION: the take folder is open (its takes shown as lanes)
    RIFF_DECLARE_ID (ASSIGNMENTS)     // after MIXER: MIDI controller assignments
    RIFF_DECLARE_ID (ASSIGN)          // { channel, cc, target (track id or "master"), param ("volume", "pan", "mute", "solo") }
    RIFF_DECLARE_ID (cc)
    RIFF_DECLARE_ID (channel)
    RIFF_DECLARE_ID (TEMPOCHANGE)     // { id, beat (quarter notes from the start), bpm }
    RIFF_DECLARE_ID (LIVELOOPS)       // after MIXER: the Live Loops grid
    RIFF_DECLARE_ID (CELL)            // { id, track, scene } with a REGION inside (its start is 0)
    RIFF_DECLARE_ID (scene)
    RIFF_DECLARE_ID (track)
    RIFF_DECLARE_ID (KEYCHANGE)       // in TEMPO: { id, bar (1-based), keyRoot, keyMinor }
    RIFF_DECLARE_ID (SIGCHANGE)       // in TEMPO: { id, bar (1-based), timeSigNumerator }
    RIFF_DECLARE_ID (bar)
    RIFF_DECLARE_ID (beat)
    RIFF_DECLARE_ID (bpm)

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
    RIFF_DECLARE_ID (fadeIn)       // REGION: seconds
    RIFF_DECLARE_ID (fadeOut)
    RIFF_DECLARE_ID (fadeInCurve)  // -1 (slow start) .. 0 (straight) .. +1 (fast start); 0.5 is equal power
    RIFF_DECLARE_ID (fadeOutCurve)
    RIFF_DECLARE_ID (loop)         // REGION: repeats until loopEnd (or the next region)
    RIFF_DECLARE_ID (loopEnd)
    RIFF_DECLARE_ID (transpose)        // REGION / TRACK (MIDI): semitones, applied as it plays
    RIFF_DECLARE_ID (velocityOffset)   // REGION / TRACK (MIDI): added to every note's velocity
    RIFF_DECLARE_ID (drumTune)         // TRACK (Drum Kit): semitones
    RIFF_DECLARE_ID (drumDecay)        // TRACK (Drum Kit): 0.3 .. 3
    RIFF_DECLARE_ID (samplerFile)      // TRACK (Sampler): the sound
    RIFF_DECLARE_ID (samplerRoot)      // TRACK (Sampler): the key that plays it as recorded
    RIFF_DECLARE_ID (panWidth)         // TRACK: stereo width (Stereo Pan mode): 0 mono .. 1 .. 2
    RIFF_DECLARE_ID (icon)             // TRACK: which track icon (see TrackIcons.h)
    RIFF_DECLARE_ID (smartControls)    // TRACK: what the eight Smart Controls knobs drive, "|"-separated
    RIFF_DECLARE_ID (automationMode)   // TRACK: "off", "read" (default), "touch", "latch", "write"
    RIFF_DECLARE_ID (metronome)
    RIFF_DECLARE_ID (armed)        // record-enabled
    RIFF_DECLARE_ID (monitor)      // hear the input through the track while armed
    RIFF_DECLARE_ID (input)        // first input channel (index among the device's active inputs)
    RIFF_DECLARE_ID (inputStereo)  // true: record `input` and `input + 1`
    RIFF_DECLARE_ID (cycleEnabled)
    RIFF_DECLARE_ID (autopunch)        // PROJECT: record only between punchIn and punchOut
    RIFF_DECLARE_ID (punchIn)
    RIFF_DECLARE_ID (punchOut)
    RIFF_DECLARE_ID (replaceMode)      // PROJECT: a recording replaces what it covers
    RIFF_DECLARE_ID (midiMerge)        // PROJECT: MIDI recorded over a MIDI region joins it
    RIFF_DECLARE_ID (cycleStart)
    RIFF_DECLARE_ID (cycleEnd)
    RIFF_DECLARE_ID (countIn)      // one bar of click before recording

    // The console section of each mixer strip (TRACK properties; see ConsoleChannel.h)
    RIFF_DECLARE_ID (consoleIn)
    RIFF_DECLARE_ID (consoleHF)          // dB, -15..15
    RIFF_DECLARE_ID (consoleHMF)
    RIFF_DECLARE_ID (consoleLMF)
    RIFF_DECLARE_ID (consoleLF)
    RIFF_DECLARE_ID (consoleDynamics)    // 0..10
    RIFF_DECLARE_ID (consoleDynIn)       // the Dynamics switched in (default true)

    // Console mode (see Project.h: consolePlugin, stripPlugin)
    RIFF_DECLARE_ID (consolePlugin)      // PROJECT: the channel strip plugin every track uses (XML), or empty
    RIFF_DECLARE_ID (builtInStrip)       // TRACK: keep the built-in strip on this track

    // The Master Bus Compressor (PROJECT properties; see MasterBusCompressor.h)
    RIFF_DECLARE_ID (busCompOn)
    RIFF_DECLARE_ID (busCompThreshold)   // dB
    RIFF_DECLARE_ID (busCompRatio)       // index: 2, 4, 10
    RIFF_DECLARE_ID (busCompAttack)      // index: 0.1, 0.3, 1, 3, 10, 30 ms
    RIFF_DECLARE_ID (busCompRelease)     // index: 0.1, 0.3, 0.6, 1.2 s, Auto
    RIFF_DECLARE_ID (busCompMakeup)      // dB
    RIFF_DECLARE_ID (busCompMix)         // 0..1
    RIFF_DECLARE_ID (activeTake)   // index of the TAKE child that's playing

    // MIDI and instruments
    RIFF_DECLARE_ID (kind)
    RIFF_DECLARE_ID (midi)
    RIFF_DECLARE_ID (pitch)
    RIFF_DECLARE_ID (velocity)
    RIFF_DECLARE_ID (controller)
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

    // Mixer
    RIFF_DECLARE_ID (owner)
    RIFF_DECLARE_ID (effect)          // built-in effect type: "eq", "compressor", "delay", "reverb", "limiter"
    RIFF_DECLARE_ID (bypass)
    RIFF_DECLARE_ID (target)
    RIFF_DECLARE_ID (level)
    RIFF_DECLARE_ID (pre)
    RIFF_DECLARE_ID (param)
    RIFF_DECLARE_ID (time)
    RIFF_DECLARE_ID (value)
    RIFF_DECLARE_ID (keyRoot)         // PROJECT: the song's key, 0 = C .. 11 = B
    RIFF_DECLARE_ID (keyMinor)        // PROJECT: minor (true) or major
    RIFF_DECLARE_ID (group)           // TRACK: 0 (none) or 1..8 - linked volume, mute, solo and record
    RIFF_DECLARE_ID (vca)             // TRACK: the VCA (or folder stack) track whose fader scales this one
    RIFF_DECLARE_ID (output)          // TRACK: the bus track it plays into (empty: the main output)
    RIFF_DECLARE_ID (stackParent)     // TRACK: the main track of the stack it's in
    RIFF_DECLARE_ID (stack)           // TRACK (a stack's main track): "folder" or "summing"
    RIFF_DECLARE_ID (collapsed)       // TRACK (a stack's main track): its tracks are folded away
    RIFF_DECLARE_ID (frozen)          // TRACK: plays frozenFile instead of its regions and plugins
    RIFF_DECLARE_ID (frozenFile)
    RIFF_DECLARE_ID (stripIn)
    // The built-in channel strip's full controls (its window), besides consoleHF/HMF/LMF/LF/Dynamics:
    RIFF_DECLARE_ID (consoleInput)        // dB trim
    RIFF_DECLARE_ID (consoleOutput)       // dB trim
    RIFF_DECLARE_ID (consoleHPF)          // Hz, 0 = off
    RIFF_DECLARE_ID (consoleLPF)          // Hz, 0 = off
    RIFF_DECLARE_ID (consoleHFFreq)
    RIFF_DECLARE_ID (consoleHFBell)
    RIFF_DECLARE_ID (consoleHMFFreq)
    RIFF_DECLARE_ID (consoleHMFQ)
    RIFF_DECLARE_ID (consoleLMFFreq)
    RIFF_DECLARE_ID (consoleLMFQ)
    RIFF_DECLARE_ID (consoleLFFreq)
    RIFF_DECLARE_ID (consoleLFBell)
    RIFF_DECLARE_ID (consoleCompRatio)    // 0 = auto (follows the Dynamics knob)
    RIFF_DECLARE_ID (consoleCompRelease)  // seconds
    RIFF_DECLARE_ID (consoleCompFast)     // fast attack
    RIFF_DECLARE_ID (consoleGateThreshold)   // dB, -80 = off
    RIFF_DECLARE_ID (consoleGateRange)    // dB
    RIFF_DECLARE_ID (consoleGateRelease)  // seconds
    RIFF_DECLARE_ID (consoleGateExpander) // expander (gentle) instead of gate         // TRACK: the whole channel strip (built-in or plugin) in
    RIFF_DECLARE_ID (hidden)          // TRACK: hidden in the tracks area (Logic's Hide; it still plays)
    RIFF_DECLARE_ID (height)          // TRACK: its height in the tracks area (pixels; view only)
    RIFF_DECLARE_ID (showAutomation)
    RIFF_DECLARE_ID (automationParam)

    #undef RIFF_DECLARE_ID
}
