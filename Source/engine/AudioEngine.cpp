#include "AudioEngine.h"

namespace riff
{

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (+ MP3/CoreAudio formats where available)
    readAheadThread.startThread (juce::Thread::Priority::high);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    transport.setSource (nullptr);
    readerSource.reset();
    readAheadThread.stopThread (2000);
}

juce::String AudioEngine::initialise()
{
    // Ask for stereo in + stereo out. If input can't be opened (no device or
    // permission denied) fall back to output only so playback still works.
    auto error = deviceManager.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceManager.addAudioCallback (this);
    return error;
}

//==============================================================================
bool AudioEngine::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return false;

    const auto fileSampleRate = reader->sampleRate;

    transport.stop();
    transport.setSource (nullptr);

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);

    // 32k samples of read-ahead are buffered on readAheadThread, so the audio
    // thread never touches the disk. The transport also resamples if the file's
    // rate differs from the device's.
    transport.setSource (readerSource.get(), 32768, &readAheadThread, fileSampleRate, 2);
    loadedFile = file;
    return true;
}

void AudioEngine::play()                              { if (readerSource != nullptr) transport.start(); }
void AudioEngine::stop()                              { transport.stop(); }
void AudioEngine::rewind()                            { transport.setPosition (0.0); }
bool AudioEngine::isPlaying() const                   { return transport.isPlaying(); }
double AudioEngine::getPositionSeconds() const        { return transport.getCurrentPosition(); }
double AudioEngine::getLengthSeconds() const          { return transport.getLengthInSeconds(); }
void AudioEngine::setPositionSeconds (double seconds) { transport.setPosition (juce::jmax (0.0, seconds)); }

AudioEngine::Levels AudioEngine::takeLevels() noexcept
{
    return { peakOutL.exchange (0.0f), peakOutR.exchange (0.0f),
             peakInL.exchange (0.0f),  peakInR.exchange (0.0f) };
}

void AudioEngine::storeMax (std::atomic<float>& target, float value) noexcept
{
    auto current = target.load (std::memory_order_relaxed);
    while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
    {}
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sr = device->getCurrentSampleRate();
    const auto bs = device->getCurrentBufferSizeSamples();

    sampleRate.store (sr);
    blockSize.store (bs);

    // Allocate here (not in the callback). Some drivers occasionally deliver
    // larger blocks than advertised, so the callback also processes in chunks.
    scratch.setSize (2, juce::jmax (bs, 4096));

    transport.prepareToPlay (bs, sr);

    smoothedGain.reset (sr, 0.02);
    smoothedGain.setCurrentAndTargetValue (masterGain.load());
    smoothedToneLevel.reset (sr, 0.01);
    smoothedToneLevel.setCurrentAndTargetValue (toneEnabled.load() ? 1.0f : 0.0f);
    tonePhase = 0.0;
}

void AudioEngine::audioDeviceStopped()
{
    transport.releaseResources();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    // ---- Input metering (recording comes in Phase 1) ----------------------
    for (int ch = 0; ch < juce::jmin (numInputChannels, 2); ++ch)
    {
        if (inputChannelData[ch] == nullptr)
            continue;

        const auto range = juce::FloatVectorOperations::findMinAndMax (inputChannelData[ch], numSamples);
        storeMax (ch == 0 ? peakInL : peakInR, juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())));
    }

    // ---- Clear outputs ---------------------------------------------------
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (numOutputChannels == 0)
        return;

    smoothedGain.setTargetValue (masterGain.load (std::memory_order_relaxed));
    smoothedToneLevel.setTargetValue (toneEnabled.load (std::memory_order_relaxed) ? 1.0f : 0.0f);

    const auto sr = sampleRate.load (std::memory_order_relaxed);
    const auto phaseInc = juce::MathConstants<double>::twoPi * toneFrequency.load (std::memory_order_relaxed) / sr;

    int offset = 0;

    while (offset < numSamples)
    {
        const int n = juce::jmin (numSamples - offset, scratch.getNumSamples());

        // A non-owning view onto the pre-allocated scratch memory: no allocation.
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, n);
        view.clear();

        // 1. File playback (pulls from the read-ahead buffer).
        juce::AudioSourceChannelInfo info (view);
        transport.getNextAudioBlock (info);

        auto* left  = view.getWritePointer (0);
        auto* right = view.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            // 2. Test tone, faded in/out to avoid clicks.
            const auto toneLevel = smoothedToneLevel.getNextValue();
            if (toneLevel > 0.0f)
            {
                const auto s = 0.2f * toneLevel * (float) std::sin (tonePhase);
                left[i]  += s;
                right[i] += s;
            }
            tonePhase += phaseInc;
            if (tonePhase >= juce::MathConstants<double>::twoPi)
                tonePhase -= juce::MathConstants<double>::twoPi;

            // 3. Master gain, smoothed per sample.
            const auto g = smoothedGain.getNextValue();
            left[i]  *= g;
            right[i] *= g;
        }

        // 4. Write to the device.
        if (numOutputChannels == 1)
        {
            if (auto* out = outputChannelData[0])
                for (int i = 0; i < n; ++i)
                    out[offset + i] = 0.5f * (left[i] + right[i]);
        }
        else
        {
            if (auto* outL = outputChannelData[0]) juce::FloatVectorOperations::copy (outL + offset, left, n);
            if (auto* outR = outputChannelData[1]) juce::FloatVectorOperations::copy (outR + offset, right, n);
        }

        storeMax (peakOutL, view.getMagnitude (0, 0, n));
        storeMax (peakOutR, view.getMagnitude (1, 0, n));

        offset += n;
    }
}

} // namespace riff
