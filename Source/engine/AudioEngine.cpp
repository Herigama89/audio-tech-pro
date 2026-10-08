#include "AudioEngine.h"

namespace riff
{

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (+ MP3/CoreAudio formats on macOS)
    currentGraph.store (new PlaybackGraph());
    prepare (44100.0, 2048);
    startTimer (250);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    retired.clear();
    delete currentGraph.exchange (nullptr);
}

juce::String AudioEngine::initialise()
{
    // Stereo in + stereo out; fall back to output only if input can't open
    // (no input device, or microphone permission denied).
    auto error = deviceManager.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceManager.addAudioCallback (this);
    return error;
}

//==============================================================================
// Graph hand-over
//==============================================================================
void AudioEngine::setGraph (std::unique_ptr<PlaybackGraph> newGraph)
{
    JUCE_ASSERT_MESSAGE_THREAD

    jassert (newGraph != nullptr);
    auto* old = currentGraph.exchange (newGraph.release(), std::memory_order_acq_rel);

    // The audio thread may be mid-block with `old`. It is safe to free once at
    // least one complete callback has started after this point.
    retired.push_back ({ std::unique_ptr<PlaybackGraph> (old), callbackCounter.load() });
    collectGarbage();
}

void AudioEngine::timerCallback()
{
    collectGarbage();
}

void AudioEngine::collectGarbage()
{
    if (! deviceRunning.load())
    {
        retired.clear(); // no callback can be running
        return;
    }

    const auto now = callbackCounter.load();
    std::erase_if (retired, [now] (const Retired& r) { return now > r.retiredAt + 1; });
}

//==============================================================================
// Transport
//==============================================================================
double AudioEngine::getPlayheadSeconds() const noexcept
{
    return (double) playheadSample.load() / sampleRate.load();
}

void AudioEngine::setPlayheadSeconds (double seconds) noexcept
{
    playheadSample.store ((juce::int64) std::llround (juce::jmax (0.0, seconds) * sampleRate.load()));
}

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
// Device callbacks
//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Some drivers deliver blocks larger than advertised, so allocate generously;
    // render() also processes in chunks if a block is bigger still.
    prepare (device->getCurrentSampleRate(), juce::jmax (device->getCurrentBufferSizeSamples(), 2048));
    deviceRunning.store (true);
}

void AudioEngine::audioDeviceStopped()
{
    deviceRunning.store (false);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    render (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

//==============================================================================
// Rendering
//==============================================================================
void AudioEngine::prepare (double newSampleRate, int maxBlockSize)
{
    // Keep the playhead at the same time if the sample rate changes.
    const auto oldRate = sampleRate.load();
    if (oldRate > 0.0 && std::abs (oldRate - newSampleRate) > 0.5)
        playheadSample.store ((juce::int64) ((double) playheadSample.load() * newSampleRate / oldRate));

    sampleRate.store (newSampleRate);

    trackBuffer.setSize (2, maxBlockSize);
    mixBuffer.setSize (2, maxBlockSize);

    for (auto& state : trackStates)
    {
        state.gainL.reset (newSampleRate, 0.03);
        state.gainR.reset (newSampleRate, 0.03);
        state.initialised = false;
    }

    masterGain.reset (newSampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (currentGraph.load()->masterGain);
}

void AudioEngine::render (const float* const* inputs, int numInputs,
                          float* const* outputs, int numOutputs, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    // Input metering (recording arrives in Phase 1).
    for (int ch = 0; ch < juce::jmin (numInputs, 2); ++ch)
    {
        if (inputs[ch] == nullptr)
            continue;
        const auto range = juce::FloatVectorOperations::findMinAndMax (inputs[ch], numSamples);
        storeMax (ch == 0 ? peakInL : peakInR, juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())));
    }

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Pick up the latest graph once per block.
    const auto* graph = currentGraph.load (std::memory_order_acquire);
    const bool isPlayingNow = playing.load();

    if (graph != nullptr)
        masterGain.setTargetValue (graph->masterGain);

    int offset = 0;

    while (offset < numSamples)
    {
        const int n = juce::jmin (numSamples - offset, mixBuffer.getNumSamples());
        auto* mixL = mixBuffer.getWritePointer (0);
        auto* mixR = mixBuffer.getWritePointer (1);
        juce::FloatVectorOperations::clear (mixL, n);
        juce::FloatVectorOperations::clear (mixR, n);

        if (graph != nullptr && isPlayingNow)
        {
            renderChunk (*graph, mixL, mixR, n);
            playheadSample.fetch_add (n);
        }

        for (int i = 0; i < n; ++i)
        {
            const auto g = masterGain.getNextValue();
            mixL[i] *= g;
            mixR[i] *= g;
        }

        if (numOutputs == 1)
        {
            if (auto* out = outputs[0])
                for (int i = 0; i < n; ++i)
                    out[offset + i] = 0.5f * (mixL[i] + mixR[i]);
        }
        else if (numOutputs >= 2)
        {
            if (auto* outL = outputs[0]) juce::FloatVectorOperations::copy (outL + offset, mixL, n);
            if (auto* outR = outputs[1]) juce::FloatVectorOperations::copy (outR + offset, mixR, n);
        }

        storeMax (peakOutL, mixBuffer.getMagnitude (0, 0, n));
        storeMax (peakOutR, mixBuffer.getMagnitude (1, 0, n));

        offset += n;
    }

    callbackCounter.fetch_add (1, std::memory_order_release);
}

void AudioEngine::renderChunk (const PlaybackGraph& graph, float* mixL, float* mixR, int n) noexcept
{
    const auto blockStart = playheadSample.load();
    const auto blockEnd = blockStart + n;

    auto* tL = trackBuffer.getWritePointer (0);
    auto* tR = trackBuffer.getWritePointer (1);

    for (const auto& track : graph.tracks)
    {
        if (track.slot < 0 || track.slot >= PlaybackGraph::maxTracks)
            continue;

        auto& state = trackStates[(size_t) track.slot];

        // Balance-style pan: unity at centre, the far side fades out.
        const auto targetL = track.audible ? track.volume * (track.pan <= 0.0f ? 1.0f : 1.0f - track.pan) : 0.0f;
        const auto targetR = track.audible ? track.volume * (track.pan >= 0.0f ? 1.0f : 1.0f + track.pan) : 0.0f;

        if (! state.initialised)
        {
            state.gainL.setCurrentAndTargetValue (targetL);
            state.gainR.setCurrentAndTargetValue (targetR);
            state.initialised = true;
        }
        else
        {
            state.gainL.setTargetValue (targetL);
            state.gainR.setTargetValue (targetR);
        }

        // Silent and not fading: nothing to do.
        if (! state.gainL.isSmoothing() && ! state.gainR.isSmoothing()
            && state.gainL.getCurrentValue() <= 0.0f && state.gainR.getCurrentValue() <= 0.0f)
            continue;

        juce::FloatVectorOperations::clear (tL, n);
        juce::FloatVectorOperations::clear (tR, n);
        bool anyAudio = false;

        for (const auto& region : track.regions)
        {
            if (region.audio == nullptr)
                continue;

            const auto regionEnd = region.start + region.length;
            const auto from = juce::jmax (blockStart, region.start);
            const auto to   = juce::jmin (blockEnd, regionEnd);

            if (from >= to)
                continue;

            const auto sourceStart = region.sourceOffset + (from - region.start);
            const auto available = (juce::int64) region.audio->getNumSamples() - sourceStart;

            if (sourceStart < 0 || available <= 0)
                continue;

            const auto count = (int) juce::jmin (to - from, available);
            const auto destOffset = (int) (from - blockStart);

            juce::FloatVectorOperations::addWithMultiply (tL + destOffset, region.audio->getReadPointer (0, (int) sourceStart), region.gain, count);
            juce::FloatVectorOperations::addWithMultiply (tR + destOffset, region.audio->getReadPointer (1, (int) sourceStart), region.gain, count);
            anyAudio = true;
        }

        if (! anyAudio)
        {
            // Still advance the gain ramps so they stay in sync with time.
            state.gainL.skip (n);
            state.gainR.skip (n);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            mixL[i] += tL[i] * state.gainL.getNextValue();
            mixR[i] += tR[i] * state.gainR.getNextValue();
        }
    }
}

} // namespace riff
