#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "PlaybackGraph.h"
#include "LatencyMeasure.h"
#include <optional>

namespace riff
{

/**
    The real-time engine.

    The UI thread builds a PlaybackGraph and passes it in with setGraph(); the
    audio thread picks it up at the start of its next block via an atomic
    pointer. Old graphs are freed on the message thread only after the audio
    thread can no longer be using them.

    Recording: the audio thread pushes input samples into a lock-free FIFO per
    take; a separate disk thread drains them into WAV files.

    Audio-thread rules (see the architecture doc): no locks, no allocation,
    no file I/O, no logging inside render().
*/
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::MidiInputCallback,
                          private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the default devices. Returns an error message, or empty on success. */
    juce::String initialise();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    // --- Graph (message thread) ---------------------------------------------
    void setGraph (std::unique_ptr<PlaybackGraph> newGraph);

    // --- Transport (any thread) ---------------------------------------------
    void play() noexcept                      { playing.store (true); }
    void stop() noexcept                      { playing.store (false); }
    bool isPlaying() const noexcept           { return playing.load(); }
    double getPlayheadSeconds() const noexcept;
    void setPlayheadSeconds (double seconds) noexcept;

    /** Like setPlayheadSeconds but allows negative times, for a count-in before bar 1. */
    void setPlayheadSecondsAllowingNegative (double seconds) noexcept
    {
        playheadSample.store ((juce::int64) std::llround (seconds * sampleRate.load()));
    }

    double getSampleRate() const noexcept     { return sampleRate.load(); }

    /** The largest block the engine ever renders in one go (instruments are prepared for this). */
    static constexpr int maxChunkSize = 4096;

    // --- Live MIDI ---------------------------------------------------------------
    /** Sends a MIDI message to the live MIDI track (from a keyboard, or the piano roll's keys). */
    void sendLiveMidi (const juce::MidiMessage& message) noexcept;

    // --- Recording (message thread) -------------------------------------------
    struct RecordRequest
    {
        juce::String trackId;
        juce::File file;
        int firstChannel = 0;   // index among the device's active inputs
        int numChannels = 1;    // 1 or 2
        bool midi = false;      // record the live MIDI input instead of audio (no file)
    };

    struct MidiEventRecord
    {
        juce::int64 sample = 0;
        juce::uint8 status = 0, data1 = 0, data2 = 0;
    };

    struct FinishedTake
    {
        juce::String trackId;
        juce::File file;
        juce::int64 startSample = 0;
        juce::int64 numSamples = 0;
        bool isMidi = false;
        std::vector<MidiEventRecord> midiEvents;
    };

    struct LiveTake
    {
        juce::String trackId;
        double startSeconds = -1.0; // -1 until the first samples arrive
        double lengthSeconds = 0.0;
    };

    static constexpr int maxTakes = 64;

    /**
        Opens a WAV file per request and starts capturing input while the
        transport plays, from `recordFromSample` onwards. If the playhead is
        before that point (a count-in), the metronome clicks until it gets there.
    */
    bool startRecording (const std::vector<RecordRequest>& requests, juce::String& error,
                         std::optional<juce::int64> recordFromSample = std::nullopt);

    /** True while the playhead is still before the record-from point. */
    bool isCountingIn() const noexcept;

    /** Stops capturing, finishes the files and returns what was recorded. */
    std::vector<FinishedTake> stopRecording();

    bool isRecording() const noexcept         { return recording.load(); }
    std::vector<LiveTake> getLiveTakes() const;

    /** True (once) if the disk couldn't keep up and some input was lost. */
    bool takeRecordingOverflow() noexcept     { return recordOverflow.exchange (false); }

    // --- Latency measurement (message thread) ----------------------------------
    /** Plays test bursts and records `inputChannel`; normal playback is muted meanwhile. */
    bool startLatencyMeasurement (int inputChannel, juce::String& error);
    bool isMeasuringLatency() const noexcept  { return measureState.load() != 0; }

    /** The result once the measurement has finished; empty while it's still running. */
    std::optional<latency::Result> takeLatencyMeasurement();

    /** Input + output latency reported by the device, in samples. */
    int getRoundTripLatencySamples();
    int getNumActiveInputChannels();
    juce::StringArray getActiveInputChannelNames();

    // --- Meters (UI thread reads; audio thread writes peak-hold values) ----
    struct Levels { float outL = 0, outR = 0, inL = 0, inR = 0; };
    Levels takeLevels() noexcept;

    // --- Rendering, public so tests can drive the engine without a device --
    void prepare (double newSampleRate, int maxBlockSize);
    void render (const float* const* inputs, int numInputs,
                 float* const* outputs, int numOutputs, int numSamples) noexcept;

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override;
    void enableAllMidiInputs();
    void drainRecordedMidi();

    void timerCallback() override;          // frees retired graphs, collects recorded MIDI
    void collectGarbage();
    void waitForAudioThread();

    struct Take
    {
        juce::String trackId;
        juce::File file;
        int firstChannel = 0, numChannels = 1;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::atomic<juce::int64> startSample { -1 };
        std::atomic<juce::int64> samplesWritten { 0 };
        bool isMidi = false;
    };

    void renderChunk (const PlaybackGraph& graph, const float* const* inputs, int numInputs, int inputOffset,
                      float* mixL, float* mixR, int numSamples, bool playingNow,
                      bool resetNotes, bool withLiveMidi) noexcept;
    void collectNotes (const PlaybackGraph::Track& track, juce::int64 from, juce::int64 to) noexcept;
    void recordChunk (const float* const* inputs, int numInputs, int inputOffset, int numSamples) noexcept;
    void renderClick (const PlaybackGraph& graph, float* mixL, float* mixR, int numSamples) noexcept;
    void renderMeasurement (const float* const* inputs, int numInputs,
                            float* const* outputs, int numOutputs, int numSamples) noexcept;
    static void storeMax (std::atomic<float>& target, float value) noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    // --- Graph hand-over -----------------------------------------------------
    std::atomic<PlaybackGraph*> currentGraph { nullptr };
    std::atomic<juce::uint64> callbackCounter { 0 };
    std::atomic<bool> deviceRunning { false };

    struct Retired { std::unique_ptr<PlaybackGraph> graph; juce::uint64 retiredAt; };
    std::vector<Retired> retired;           // message thread only

    // --- Transport -------------------------------------------------------------
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> playheadSample { 0 };
    std::atomic<double> sampleRate { 44100.0 };

    // --- Recording ---------------------------------------------------------------
    juce::TimeSliceThread diskThread { "Riff recorder" };
    std::vector<std::unique_ptr<Take>> takes;                   // owned by the message thread
    std::array<std::atomic<Take*>, maxTakes> takeSlots {};      // what the audio thread sees
    std::atomic<bool> recording { false };
    std::atomic<bool> recordOverflow { false };
    std::atomic<juce::int64> recordFrom { 0 };
    std::atomic<bool> midiTakeActive { false };

    // --- MIDI ----------------------------------------------------------------------
    // Live input: many producers (MIDI thread, UI) guarded by a spin lock that the
    // audio thread never takes; the audio thread is the only consumer.
    struct LiveEvent { juce::uint8 data[3]; int size; };
    static constexpr int liveFifoSize = 1024;
    juce::AbstractFifo liveFifo { liveFifoSize };
    std::array<LiveEvent, liveFifoSize> liveEvents {};
    juce::SpinLock liveProducerLock;

    // Recorded MIDI: the audio thread produces, the message thread consumes.
    static constexpr int recordFifoSize = 32768;
    juce::AbstractFifo recordFifo { recordFifoSize };
    std::array<MidiEventRecord, recordFifoSize> recordEvents {};
    std::vector<MidiEventRecord> recordedMidi;   // message thread

    juce::MidiBuffer liveMidi, trackMidi;        // audio thread scratch, pre-sized
    juce::int64 expectedPosition = 0;
    bool wasPlaying = false;
    std::unique_ptr<juce::MidiDeviceListConnection> midiDeviceListener;

    // --- Latency measurement ---------------------------------------------------
    std::atomic<int> measureState { 0 };     // 0 idle, 1 running, 2 finished
    juce::AudioBuffer<float> measureBuffer;  // allocated before measureState becomes 1
    std::vector<float> measureBurst;
    std::vector<int> measurePositions;
    int measurePos = 0, measureInput = 0;

    // --- Audio-thread state (sized in prepare) --------------------------------
    juce::AudioBuffer<float> trackBuffer, mixBuffer, silence;
    struct TrackState { juce::SmoothedValue<float> gainL, gainR; bool initialised = false; };
    std::array<TrackState, PlaybackGraph::maxTracks> trackStates;
    juce::SmoothedValue<float> masterGain;

    // Metronome click
    int clickSamplesLeft = 0, clickLength = 0;
    double clickPhase = 0.0, clickPhaseInc = 0.0;
    float clickAmplitude = 0.0f;

    std::atomic<float> peakOutL { 0 }, peakOutR { 0 }, peakInL { 0 }, peakInR { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace riff
