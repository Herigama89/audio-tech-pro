#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace riff
{

/**
    Phase 0 audio engine.

    Owns the audio device and implements the real-time callback directly
    (rather than going through AudioSourcePlayer) so the shape matches the
    final engine: one callback that renders everything, controlled from the
    UI thread only through atomics.

    Rules for the audio thread (see the architecture doc):
      - no locks we own, no allocation, no file I/O, no logging.
      - file reading happens on `readAheadThread`; the transport just pulls
        already-buffered samples.
*/
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the default input/output devices. Returns an error message, or empty on success. */
    juce::String initialise();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    // --- Test tone ----------------------------------------------------------
    void setToneEnabled (bool on) noexcept           { toneEnabled.store (on); }
    bool isToneEnabled() const noexcept              { return toneEnabled.load(); }
    void setToneFrequency (float hz) noexcept        { toneFrequency.store (hz); }

    // --- File playback (message thread only) --------------------------------
    bool loadFile (const juce::File& file);
    void play();
    void stop();
    void rewind();
    bool isPlaying() const;
    double getPositionSeconds() const;
    double getLengthSeconds() const;
    void setPositionSeconds (double seconds);
    juce::File getLoadedFile() const                 { return loadedFile; }

    // --- Master -------------------------------------------------------------
    void setMasterGain (float linearGain) noexcept   { masterGain.store (linearGain); }

    // --- Meters (read from UI thread; audio thread writes peak-hold values) -
    struct Levels { float outL = 0, outR = 0, inL = 0, inR = 0; };
    Levels takeLevels() noexcept;

    double getSampleRate() const noexcept            { return sampleRate.load(); }
    int getBlockSize() const noexcept                { return blockSize.load(); }

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    static void storeMax (std::atomic<float>& target, float value) noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    juce::TimeSliceThread readAheadThread { "Riff disk read-ahead" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::File loadedFile;

    // Pre-allocated scratch buffer the callback renders into (sized in aboutToStart).
    juce::AudioBuffer<float> scratch;

    // Shared state between UI and audio thread.
    std::atomic<bool>  toneEnabled   { false };
    std::atomic<float> toneFrequency { 440.0f };
    std::atomic<float> masterGain    { 0.8f };
    std::atomic<double> sampleRate   { 44100.0 };
    std::atomic<int>    blockSize    { 512 };

    std::atomic<float> peakOutL { 0 }, peakOutR { 0 }, peakInL { 0 }, peakInR { 0 };

    // Audio-thread-only state.
    double tonePhase = 0.0;
    juce::SmoothedValue<float> smoothedGain;
    juce::SmoothedValue<float> smoothedToneLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace riff
