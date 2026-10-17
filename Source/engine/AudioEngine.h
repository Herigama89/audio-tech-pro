#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "PlaybackGraph.h"
#include "LatencyMeasure.h"
#include "DelayLine.h"
#include "BuiltInEffects.h"
#include "MasterBusCompressor.h"
#include "MixAnalysis.h"
#include "ScreenAudioTap.h"
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
    /** Opens the audio device: the one in `savedState` (from saveDeviceState) if it's still there, else the default. */
    juce::String initialise (const juce::XmlElement* savedState = nullptr);

    /** The device choice to remember between sessions (device, rate, buffer size, active channels). */
    std::unique_ptr<juce::XmlElement> saveDeviceState() const { return deviceManager.createStateXml(); }

    /** Names to show for a device's input channels (e.g. the interfaces a combined device is made of). */
    void setInputChannelLabels (const juce::String& deviceName, const juce::StringArray& labels)
    {
        labelledDevice = deviceName;
        inputLabels = labels;
    }

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    // --- Graph (message thread) ---------------------------------------------
    void setGraph (std::unique_ptr<PlaybackGraph> newGraph);

    // --- Transport (any thread) ---------------------------------------------
    void play() noexcept                      { playing.store (true); }
    void stop() noexcept                      { playing.store (false); }
    bool isPlaying() const noexcept           { return playing.load(); }
    /** Screen recording: every block of the output (and the inputs) also goes to this tap; nullptr stops it.
        The tap must outlive the engine or be removed first. */
    void setScreenAudioTap (ScreenAudioTap* tap) noexcept { screenTap.store (tap, std::memory_order_release); }

    /** True while an export or a mix measurement renders the song offline. */
    bool isRenderingOffline() const noexcept  { return bouncing.load(); }
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
        int sourceSlot = -1;    // record another track's output (its slot) instead of the device's input
        int printSlot = -1;     // its track's slot when it has printed inserts: record the input through them
        int printLatency = 0;   // how late they make it (the take is moved back by this)
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
        std::vector<juce::uint8> peaks;   // the waveform so far: the loudest sample per `peakBlock` samples (0..255)
        bool clipped = false;             // some of it hit 0 dB
    };
    static constexpr int peakBlock = 512;

    /** Recording stops by itself when an input clips (the take keeps what came before). */
    void setStopOnClip (bool on) noexcept { stopOnClip.store (on); }
    bool getStopOnClip() const noexcept { return stopOnClip.load(); }
    /** True (once) when a recorded input clipped; with Stop on Clip on, the main window then stops recording. */
    bool takeRecordingClipped() noexcept { return recordClipped.exchange (false); }

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

    /** How late playback is heard because of plugin latency (add it when placing recordings). */
    int getPlaybackLatencySamples() const noexcept;

    /** Input + output latency reported by the device, in samples. */
    int getRoundTripLatencySamples();
    int getNumActiveInputChannels();
    juce::StringArray getActiveInputChannelNames();

    // --- Meters (UI thread reads; audio thread writes peak-hold values) ----
    struct Levels { float outL = 0, outR = 0, inL = 0, inR = 0; };
    Levels takeLevels() noexcept;

    /** Post-fader peak of a track since the last call (by the track's slot in the graph). */
    std::pair<float, float> takeTrackLevel (int slot) noexcept;

    /** The spectrum analyzer listens to one track (after its EQ and Dynamics): its slot, or -1 for none. */
    void setAnalyzerSlot (int slot) noexcept { analyzerSlot.store (slot); }

    /** Takes up to `maxSamples` of the analyzed track's audio (mono) that arrived since the last call. Message thread. */
    int readAnalyzer (float* dest, int maxSamples) noexcept
    {
        const auto scope = analyzerFifo.read (juce::jmin (maxSamples, analyzerFifo.getNumReady()));
        if (scope.blockSize1 > 0) std::copy_n (analyzerBuffer.data() + scope.startIndex1, scope.blockSize1, dest);
        if (scope.blockSize2 > 0) std::copy_n (analyzerBuffer.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    /** The MultiMeter listens to the master output (after the master fader) while this is on. */
    void setMasterTap (bool on) noexcept { masterTapOn.store (on); }

    /** Takes up to `maxSamples` of the master output (left and right) that arrived since the last call. Message thread. */
    int readMaster (float* left, float* right, int maxSamples) noexcept
    {
        const auto scope = masterFifo.read (juce::jmin (maxSamples, masterFifo.getNumReady()));
        int written = 0;
        for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
        {
            std::copy_n (masterTapL.data() + start, size, left + written);
            std::copy_n (masterTapR.data() + start, size, right + written);
            written += size;
        }
        return written;
    }

    /** The channel strip's Dynamics gain reduction on a track's slot (dB, positive). */
    float getConsoleGainReduction (int slot) const noexcept
    {
        return juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks) ? consoles[(size_t) slot].getGainReductionDb() : 0.0f;
    }

    /** Console mode: the loudest RMS going into and coming out of the bus compressor plugin since the last call. */
    std::pair<float, float> takeBusPluginLevels() noexcept;

    /** The Master Bus Compressor's largest gain reduction (dB) since the last call. */
    /** A track's channel strip: its level coming in (before the strip) and going out (after it), the loudest since the last call. */
    std::pair<float, float> takeStripLevels (int slot) noexcept
    {
        if (! juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
            return {};
        return { stripInPeaks[(size_t) slot].exchange (0.0f), stripOutPeaks[(size_t) slot].exchange (0.0f) };
    }

    float takeBusCompGainReduction() noexcept
    {
        const auto gr = busComp.getGainReductionDb();
        busComp.meterRead();
        return gr;
    }

    // --- Export (message thread) ------------------------------------------------
    /**
        Renders [start, end) of the project faster than real time into a WAV
        file. The audio device is paused meanwhile. `progress` gets 0..1 and
        returns false to cancel. Returns an error message, or empty on success.
    */
    juce::String bounce (const juce::File& file, double startSeconds, double endSeconds, int bitDepth,
                         std::function<bool (double)> progress);

    /** The same in three steps, so the render can run on a background thread:
        begin and end on the message thread, renderOffline on any one thread. */
    void beginOfflineRender();
    juce::String renderOffline (const juce::File& file, double startSeconds, double endSeconds, int bitDepth,
                                std::function<bool (double)> progress);
    void endOfflineRender();

    /** Library preview: plays a file (decoded, up to 30 s) on its own, over everything; never recorded or exported. */
    bool previewFile (const juce::File& file);
    void stopPreview();
    bool isPreviewing() const noexcept { return previewPlaying.load(); }

    /** The MIDI that arrived from your devices since the last call (message thread). */
    std::vector<juce::MidiMessage> takeIncomingMidi();

    /** The recording format: 16, 24 or 32 (float) bits. */
    void setRecordBitDepth (int bits) noexcept { recordBitDepth = (bits == 16 || bits == 32) ? bits : 24; }
    int getRecordBitDepth() const noexcept { return recordBitDepth; }

    /** While a fader or pan control is written as automation, the engine plays its value
        instead of the curve. NaN (or clearAutomationOverride) hands back to the curve. */
    void setAutomationOverride (int slot, bool pan, float value) noexcept
    {
        if (juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
            (pan ? panOverride : volumeOverride)[(size_t) slot].store (value);
    }
    void clearAutomationOverride (int slot, bool pan) noexcept
    {
        setAutomationOverride (slot, pan, std::numeric_limits<float>::quiet_NaN());
    }

    /** A fader or pan moved: the engine plays the new value straight away, without a new graph
        (moving a fader no longer rebuilds everything). Cleared when the next graph arrives, which
        carries the value itself. */
    void setMasterMixValue (float value) noexcept { masterMix.store (value); }

    /** The Performance meter: how much of each buffer's time the audio took (1 = all of it),
        the highest since reset, how many buffers ran late, and each track's share. */
    struct Load
    {
        float now = 0.0f, peak = 0.0f;
        int overloads = 0;
    };
    Load takeLoad() noexcept
    {
        Load l;
        l.now = loadNow.exchange (0.0f);
        l.peak = loadPeak.load();
        l.overloads = overloads.load();
        return l;
    }
    void resetLoadPeak() noexcept { loadPeak.store (0.0f); overloads.store (0); }

    /** Seconds of processing spent on a graph slot since the last call, and the audio time that covered. */
    std::pair<double, double> takeTrackTime (int slot) noexcept
    {
        if (! juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
            return {};
        const auto ticks = trackTicks[(size_t) slot].exchange (0);
        const auto samples = trackSamples[(size_t) slot].exchange (0);
        const auto rate = sampleRate.load();
        return { juce::Time::highResolutionTicksToSeconds (ticks), rate > 0.0 ? (double) samples / rate : 0.0 };
    }
    void setMixValue (int slot, bool pan, float value) noexcept
    {
        if (juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
            (pan ? panMix : volumeMix)[(size_t) slot].store (value);
    }

    /** Renders [start, end) offline and writes each listed graph slot's own output
        (after its plugins, fader and pan) to its own file: stems, or a bounce in
        place. Between begin/endOfflineRender. */
    struct StemTarget { int slot = -1; juce::File file; };
    juce::String renderStems (const std::vector<StemTarget>& targets, double startSeconds, double endSeconds, int bitDepth,
                              std::function<bool (double)> progress, bool beforeFader = false);

    /** Measures every track and the master over [start, end) by rendering offline (between begin/endOfflineRender).
        Track results are by graph slot (see ProjectPlayer::slotFor); the master is the last entry. */
    struct MixMeasurement
    {
        std::vector<LevelStats> tracks;   // indexed by slot
        LevelStats master;
        double seconds = 0.0;
    };
    MixMeasurement measureMix (double startSeconds, double endSeconds, std::function<bool (double)> progress);

    // --- Rendering, public so tests can drive the engine without a device --
    void prepare (double newSampleRate, int maxBlockSize);
    /** Which of the interface's outputs the callback carries (set when the device starts; tests set it themselves). */
    void setActiveOutputs (const juce::BigInteger& active);

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
        int sourceSlot = -1, printSlot = -1, printLatency = 0;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::atomic<juce::int64> startSample { -1 };
        std::atomic<juce::int64> samplesWritten { 0 };
        bool isMidi = false;

        // The live waveform: written by the audio thread, read by the screen (bins up to `peakCount` are complete).
        static constexpr int maxPeaks = 48000 * 60 * 60 / peakBlock;   // an hour at 48 kHz (longer takes still record; the live picture stops there)
        std::unique_ptr<std::atomic<juce::uint8>[]> peaks { new std::atomic<juce::uint8>[maxPeaks] };
        std::atomic<int> peakCount { 0 };
        float peakNow = 0.0f;
        int peakFill = 0;
        std::atomic<bool> clipped { false };
    };
    std::atomic<bool> stopOnClip { false }, recordClipped { false };

    juce::int64 chunkPosition = 0;   // where the song is for the chunk being rendered, for plugins' playheads
    bool chunkPlaying = false;
    double chunkBpm = 120.0;
    void renderChunk (const PlaybackGraph& graph, const float* const* inputs, int numInputs, int inputOffset,
                      float* mixL, float* mixR, int numSamples, bool playingNow,
                      bool resetNotes, bool withLiveMidi) noexcept;
    void collectNotes (const PlaybackGraph::Track& track, juce::int64 from, juce::int64 to) noexcept;
    void renderTrack (const PlaybackGraph::Track& track, const float* const* inputs, int numInputs, int inputOffset,
                      float* outL, float* outR, int numSamples, bool playingNow,
                      bool resetNotes, bool withLiveMidi) noexcept;
    void recordChunk (const float* const* inputs, int numInputs, int inputOffset, int numSamples, bool fromTracks = false) noexcept;
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
    juce::TimeSliceThread diskThread { "Recorder" };
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

    // What your MIDI devices send, for the message thread (step input, controller assignments).
    juce::AbstractFifo incomingFifo { liveFifoSize };
    std::array<LiveEvent, liveFifoSize> incomingEvents {};
    juce::SpinLock incomingLock;

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
    juce::AudioBuffer<float> trackBuffer, regionBuffer, mixBuffer, silence, directBuffer, busBuffer;
public:
    static constexpr int maxHardwareOutputs = 64;
private:
    juce::AudioBuffer<float> hardwareBuffer;                   // channels sent straight to the interface's other outputs
    std::array<int, maxHardwareOutputs> outputSlots {};        // an output's number on the device -> its place in the callback (-1: off)
    juce::AudioBuffer<float> slotOutputs;   // every track's output (after its fader) this block, by slot: side chains and track inputs
    juce::AudioBuffer<float> slotInputs;    // armed tracks' input through their printed inserts, this block, by slot
    std::array<bool, PlaybackGraph::maxTracks> slotInputReady {};
    void printInputs (const PlaybackGraph& graph, const float* const* inputs, int numInputs, int inputOffset, int n) noexcept;
    const float* slotOutput (int slot, int channel) const noexcept
    {
        return juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks) && slot * 2 + 1 < slotOutputs.getNumChannels()
                   ? slotOutputs.getReadPointer (slot * 2 + channel) : silence.getReadPointer (channel);
    }
    juce::AudioBuffer<float> streamBuffer;   // a streamed file's audio for one block
    std::array<std::unique_ptr<DelayLine>, PlaybackGraph::maxTracks> trackDelays;
    std::unique_ptr<DelayLine> directDelayLine;
    struct TrackState { juce::SmoothedValue<float> gainL, gainR; bool initialised = false; };
    std::array<TrackState, PlaybackGraph::maxTracks> trackStates;
    juce::SmoothedValue<float> masterGain;
    MasterBusCompressor busComp;
    std::array<ConsoleChannel, PlaybackGraph::maxTracks> consoles;
    std::atomic<int> analyzerSlot { -1 };
    std::atomic<bool> masterTapOn { false };
    juce::AbstractFifo masterFifo { 65536 };
    std::vector<float> masterTapL = std::vector<float> (65536, 0.0f), masterTapR = std::vector<float> (65536, 0.0f);
    juce::AbstractFifo analyzerFifo { 32768 };
    std::vector<float> analyzerBuffer = std::vector<float> (32768, 0.0f);

    // Metronome click
    int clickSamplesLeft = 0, clickLength = 0;
    double clickPhase = 0.0, clickPhaseInc = 0.0;
    float clickAmplitude = 0.0f;

    std::atomic<float> peakOutL { 0 }, peakOutR { 0 }, peakInL { 0 }, peakInR { 0 };
    // The bus compressor plugin's level in and out, summed since the last read (its gain reduction is worked out from them)
    std::atomic<float> busPluginIn { 0 }, busPluginOut { 0 };
    std::atomic<int> busPluginSamples { 0 };
    static float rmsOf (const float* left, const float* right, int n) noexcept;
    std::array<std::atomic<float>, PlaybackGraph::maxTracks * 2> trackPeaks {};
    std::array<std::atomic<float>, PlaybackGraph::maxTracks> stripInPeaks {}, stripOutPeaks {};   // the channel strip's LED meters
    std::atomic<bool> bouncing { false };
    juce::String labelledDevice;
    juce::StringArray inputLabels;
    std::atomic<ScreenAudioTap*> screenTap { nullptr };
    std::array<std::atomic<float>, PlaybackGraph::maxTracks> volumeOverride {}, panOverride {};   // NaN: none (set in the constructor)
    std::array<std::atomic<float>, PlaybackGraph::maxTracks> volumeMix {}, panMix {};             // NaN: the graph's own value
    std::atomic<float> masterMix { std::numeric_limits<float>::quiet_NaN() };
    std::atomic<float> loadNow { 0.0f }, loadPeak { 0.0f };
    std::atomic<int> overloads { 0 };
    std::array<std::atomic<juce::int64>, PlaybackGraph::maxTracks> trackTicks {}, trackSamples {};
    bool measuring = false;                       // only while measureMix renders
    juce::AudioBuffer<float>* stemCapture = nullptr;   // only while renderStems renders: 2 channels per slot
    bool stemBeforeFader = false;
    int recordBitDepth = 24;

    juce::SpinLock previewLock;
    std::unique_ptr<juce::AudioBuffer<float>> previewHold;   // message thread owns it
    juce::AudioBuffer<float>* previewBuffer = nullptr;       // what the audio thread reads (under previewLock)
    double previewPosition = 0.0, previewStep = 1.0;
    std::atomic<bool> previewPlaying { false };
    void renderPreview (float* left, float* right, int n) noexcept;                      // freezing: before the fader, pan and sends
    std::vector<LevelStats> measuredTracks;       // by slot
    LevelStats measuredMaster;
    bool offlineWasAttached = false, offlineSavedPlaying = false;
    juce::int64 offlineSavedPlayhead = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace riff
