#pragma once

#include "AudioFileCache.h"
#include "Instrument.h"
#include "Effect.h"
#include "MasterBusCompressor.h"
#include "ConsoleChannel.h"
#include "../model/TempoMap.h"
#include <vector>

namespace riff
{

class BuiltInEffect;

/**
    An immutable snapshot of everything the audio thread needs to render the
    project, in samples at the device rate.

    Built on the message thread from the project model, handed to the engine,
    and never modified afterwards. A new edit means a new snapshot.
*/
struct PlaybackGraph
{
    /** An automation curve in samples, evaluated once per processing chunk. */
    struct Curve
    {
        std::vector<juce::int64> times;
        std::vector<float> values;

        bool empty() const noexcept { return times.empty(); }

        float valueAt (juce::int64 t) const noexcept
        {
            if (t <= times.front())
                return values.front();

            const auto it = std::upper_bound (times.begin(), times.end(), t);
            if (it == times.end())
                return values.back();

            const auto i = (size_t) (it - times.begin());
            const auto t0 = times[i - 1], t1 = times[i];
            const auto f = t1 > t0 ? (float) ((double) (t - t0) / (double) (t1 - t0)) : 1.0f;
            return values[i - 1] + (values[i] - values[i - 1]) * f;
        }
    };

    struct Send
    {
        int busIndex = 0;
        float level = 1.0f;
        bool preFader = false;
    };

    /** Automation of one parameter of a built-in effect (kept alive by the track's inserts). */
    struct ParamAutomation
    {
        Effect* effect = nullptr;
        int paramIndex = 0;
        Curve curve;
    };

    /** A MIDI note at absolute timeline positions, already clipped to its region. */
    struct Note
    {
        juce::int64 start = 0, end = 0;
        juce::uint8 pitch = 60, velocity = 100;
    };

    /** A controller or pitch bend message at an absolute timeline position. */
    struct MidiEvent
    {
        juce::int64 time = 0;
        juce::uint8 status = 0xb0, data1 = 0, data2 = 0;
    };

    struct Region
    {
        std::vector<Note> notes;    // MIDI regions only, sorted by start
        std::vector<MidiEvent> events;   // MIDI regions only, sorted by time
        SharedAudio audio;          // stereo, at the file's own rate
        juce::int64 start = 0;      // timeline position (engine samples)
        juce::int64 sourceOffset = 0;   // where the region starts in the file (file samples), when no conversion is needed
        double sourcePosition = 0.0;    // the same, exactly, for files at another rate
        double step = 1.0;              // file samples per engine sample (file rate / engine rate)
        bool resample = false;          // the file's rate differs from the engine's
        juce::int64 length = 0;     // engine samples
        float gain = 1.0f;
        juce::int64 fadeIn = 0, fadeOut = 0;      // engine samples
        float fadeInCurve = 0.0f, fadeOutCurve = 0.0f;

        bool hasFades() const noexcept { return fadeIn > 0 || fadeOut > 0; }

        /** The fades' gain at a sample position inside the region (as model::fadeGain). */
        float fadeGainAt (juce::int64 position) const noexcept
        {
            float g = 1.0f;
            if (position < fadeIn)
                g *= std::pow ((float) position / (float) fadeIn, std::exp2 (-2.0f * fadeInCurve));
            if (const auto fromEnd = length - position; fromEnd <= fadeOut && fadeOut > 0)
                g *= std::pow (juce::jmax (0.0f, (float) fromEnd / (float) fadeOut), std::exp2 (-2.0f * fadeOutCurve));
            return g;
        }
    };

    struct Track
    {
        int slot = 0;               // stable per-track index for smoothing state in the engine
        float volume = 1.0f;        // linear
        float pan = 0.0f;           // -1 .. +1
        bool audible = true;        // already accounts for mute and solo
        float vcaGain = 1.0f;       // VCA and folder stack faders on top of the track's own
        float width = 1.0f;         // stereo width: 0 mono, 1 as recorded, up to 2 wider
        int outputBus = -1;         // plays into this bus instead of the main output (-1)
        std::vector<Region> regions;

        bool isInstrument = false;
        std::shared_ptr<Instrument> instrument;   // owned by the InstrumentRack; may be null while loading
        bool receivesLiveMidi = false;            // the track your MIDI keyboard plays

        ConsoleChannel::Settings console;               // the strip's EQ and Dynamics, before the inserts
        std::vector<std::shared_ptr<Effect>> stripPlugins;  // console mode: the user's channel strip plugin, instead of `console`
        std::vector<std::shared_ptr<Effect>> inserts;   // in order, bypassed ones left out

        bool isBus = false;
        int busIndex = -1;                // for buses: which bus buffer they read
        std::vector<Send> sends;          // for ordinary tracks
        int preDelay = 0, postDelay = 0;  // delay compensation, in samples

        Curve volumeAutomation, panAutomation;
        std::vector<ParamAutomation> paramAutomation;

        bool monitorInput = false;  // armed + monitor: pass the input through this track
        int inputChannel = 0;       // index among the device's active inputs
        bool inputStereo = false;
    };

    static constexpr int maxTracks = 256;
    static constexpr int maxBuses = 32;

    /** The buses in the order they must play (indices into `tracks`): a bus that plays into
        another bus (a summing stack into a bus, say) comes before it. Empty: in track order. */
    std::vector<int> busOrder;

    double sampleRate = 44100.0;
    float masterGain = 0.8f;

    bool metronome = false;
    bool cycleEnabled = false;
    juce::int64 cycleStart = 0, cycleEnd = 0;   // samples; playback wraps from end to start
    double secondsPerBeat = 0.5;
    TempoMap tempo;                 // the tempo track (the metronome follows it)
    int beatsPerBar = 4;
    std::vector<Track> tracks;
    std::vector<std::shared_ptr<Effect>> masterInserts;
    Curve masterVolumeAutomation;
    MasterBusCompressor::Settings busComp;
    std::vector<std::shared_ptr<Effect>> busCompPlugins;   // console mode: the user's own bus compressor, instead of busComp

    int directDelay = 0;        // extra delay for tracks heard directly, matching the slowest bus
    int playbackLatency = 0;    // how late everything is heard because of plugin latency
};

} // namespace riff
