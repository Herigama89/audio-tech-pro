#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "PlaybackGraph.h"

namespace riff
{

/**
    The real-time engine.

    The UI thread builds a PlaybackGraph and passes it in with setGraph(); the
    audio thread picks it up at the start of its next block via an atomic
    pointer. Old graphs are freed on the message thread only after the audio
    thread can no longer be using them.

    Audio-thread rules (see the architecture doc): no locks, no allocation,
    no file I/O, no logging inside render().
*/
class AudioEngine final : private juce::AudioIODeviceCallback,
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

    double getSampleRate() const noexcept     { return sampleRate.load(); }

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

    void timerCallback() override;          // frees retired graphs
    void collectGarbage();

    void renderChunk (const PlaybackGraph& graph, float* outL, float* outR, int numSamples) noexcept;
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

    // --- Audio-thread state (sized in prepare) --------------------------------
    juce::AudioBuffer<float> trackBuffer, mixBuffer;
    struct TrackState { juce::SmoothedValue<float> gainL, gainR; bool initialised = false; };
    std::array<TrackState, PlaybackGraph::maxTracks> trackStates;
    juce::SmoothedValue<float> masterGain;

    std::atomic<float> peakOutL { 0 }, peakOutR { 0 }, peakInL { 0 }, peakInR { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace riff
