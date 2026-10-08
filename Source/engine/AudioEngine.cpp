#include "AudioEngine.h"

namespace riff
{

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (+ MP3/CoreAudio formats on macOS)
    currentGraph.store (new PlaybackGraph());

    for (auto& slot : takeSlots)
        slot.store (nullptr);

    diskThread.startThread (juce::Thread::Priority::high);
    prepare (44100.0, 2048);
    startTimer (250);
}

AudioEngine::~AudioEngine()
{
    stopTimer();

    if (isRecording())
        stopRecording();

    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    diskThread.stopThread (2000);
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

void AudioEngine::waitForAudioThread()
{
    // Blocks the message thread until any callback that might still be using
    // old state has finished (at most a couple of buffer lengths).
    if (! deviceRunning.load())
        return;

    const auto start = callbackCounter.load();
    const auto deadline = juce::Time::getMillisecondCounter() + 500;

    while (callbackCounter.load() < start + 2 && deviceRunning.load()
           && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (1);
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
// Recording
//==============================================================================
bool AudioEngine::isCountingIn() const noexcept
{
    return recording.load() && playheadSample.load() < recordFrom.load();
}

bool AudioEngine::startRecording (const std::vector<RecordRequest>& requests, juce::String& error,
                                  std::optional<juce::int64> recordFromSample)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (isRecording())
        return true;

    if (requests.empty() || (int) requests.size() > maxTakes)
    {
        error = "Nothing to record.";
        return false;
    }

    const auto rate = sampleRate.load();
    std::vector<std::unique_ptr<Take>> newTakes;

    auto fail = [&newTakes, &error] (const juce::String& message)
    {
        for (auto& t : newTakes)
        {
            t->writer.reset();
            t->file.deleteFile();
        }
        error = message;
        return false;
    };

    for (const auto& request : requests)
    {
        if (! request.file.getParentDirectory().createDirectory())
            return fail ("Couldn't create the folder " + request.file.getParentDirectory().getFullPathName());

        auto fileStream = std::make_unique<juce::FileOutputStream> (request.file);

        if (fileStream->failedToOpen())
            return fail ("Couldn't write " + request.file.getFullPathName());

        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (request.numChannels)
                                                       .withBitsPerSample (24));
        if (writer == nullptr)
            return fail ("Couldn't create a WAV writer for " + request.file.getFileName());

        auto take = std::make_unique<Take>();
        take->trackId = request.trackId;
        take->file = request.file;
        take->firstChannel = request.firstChannel;
        take->numChannels = request.numChannels;

        // Four seconds of FIFO between the audio thread and the disk thread.
        take->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), diskThread,
                                                                                  (int) (rate * 4.0));
        newTakes.push_back (std::move (take));
    }

    takes = std::move (newTakes);
    recordFrom.store (recordFromSample.value_or (playheadSample.load()));

    for (size_t i = 0; i < takes.size(); ++i)
        takeSlots[i].store (takes[i].get(), std::memory_order_release);

    recording.store (true, std::memory_order_release);
    return true;
}

std::vector<AudioEngine::FinishedTake> AudioEngine::stopRecording()
{
    JUCE_ASSERT_MESSAGE_THREAD

    std::vector<FinishedTake> finished;

    if (! recording.exchange (false))
        return finished;

    waitForAudioThread(); // the audio thread may still be inside recordChunk()

    for (auto& slot : takeSlots)
        slot.store (nullptr, std::memory_order_release);

    for (auto& take : takes)
    {
        take->writer.reset(); // flushes the remaining FIFO contents and closes the file

        const auto written = take->samplesWritten.load();
        const auto start = take->startSample.load();

        if (written > 0 && start >= 0)
            finished.push_back ({ take->trackId, take->file, start, written });
        else
            take->file.deleteFile(); // nothing was captured
    }

    takes.clear();
    return finished;
}

std::vector<AudioEngine::LiveTake> AudioEngine::getLiveTakes() const
{
    std::vector<LiveTake> live;
    const auto rate = sampleRate.load();

    for (const auto& take : takes)
    {
        const auto start = take->startSample.load();
        live.push_back ({ take->trackId,
                          start >= 0 ? (double) start / rate : -1.0,
                          (double) take->samplesWritten.load() / rate });
    }

    return live;
}

bool AudioEngine::startLatencyMeasurement (int inputChannel, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (measureState.load() != 0 || isRecording())
    {
        error = "Stop recording first.";
        return false;
    }

    const auto rate = sampleRate.load();
    measureBurst = latency::makeBurst (rate);
    measurePositions = latency::burstPositions (rate);
    measureBuffer.setSize (1, latency::measurementLength (rate));
    measureBuffer.clear();
    measurePos = 0;
    measureInput = inputChannel;

    measureState.store (1, std::memory_order_release); // the audio thread takes over from here
    return true;
}

std::optional<latency::Result> AudioEngine::takeLatencyMeasurement()
{
    if (measureState.load (std::memory_order_acquire) != 2)
        return std::nullopt;

    auto result = latency::estimateDelay (measureBuffer.getReadPointer (0), measureBuffer.getNumSamples(), sampleRate.load());
    measureState.store (0);
    return result;
}

int AudioEngine::getRoundTripLatencySamples()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    return 0;
}

int AudioEngine::getNumActiveInputChannels()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

juce::StringArray AudioEngine::getActiveInputChannelNames()
{
    juce::StringArray names;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto all = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();

        for (int i = 0; i < all.size(); ++i)
            if (active[i])
                names.add (all[i].isNotEmpty() ? all[i] : "Input " + juce::String (i + 1));
    }

    return names;
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
    silence.setSize (2, maxBlockSize);
    silence.clear();

    for (auto& state : trackStates)
    {
        state.gainL.reset (newSampleRate, 0.03);
        state.gainR.reset (newSampleRate, 0.03);
        state.initialised = false;
    }

    masterGain.reset (newSampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (currentGraph.load()->masterGain);

    clickLength = (int) (newSampleRate * 0.035);
    clickSamplesLeft = 0;
}

void AudioEngine::render (const float* const* inputs, int numInputs,
                          float* const* outputs, int numOutputs, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    if (inputs == nullptr)
        numInputs = 0;

    // Input metering
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

    // Latency measurement takes over the device while it runs.
    if (measureState.load (std::memory_order_acquire) == 1)
    {
        renderMeasurement (inputs, numInputs, outputs, numOutputs, numSamples);
        callbackCounter.fetch_add (1, std::memory_order_release);
        return;
    }

    // Pick up the latest graph once per block.
    const auto* graph = currentGraph.load (std::memory_order_acquire);
    const bool isPlayingNow = playing.load();
    const bool isRecordingNow = recording.load (std::memory_order_acquire);
    const auto recordStart = recordFrom.load();

    const bool cycling = graph != nullptr && graph->cycleEnabled && graph->cycleEnd > graph->cycleStart;

    if (graph != nullptr)
        masterGain.setTargetValue (graph->masterGain);

    int offset = 0;

    while (offset < numSamples)
    {
        int n = juce::jmin (numSamples - offset, mixBuffer.getNumSamples());
        const auto position = playheadSample.load();

        // Split the chunk at the cycle end and at the record-from point, so
        // both happen on exactly the right sample.
        if (isPlayingNow && cycling && position < graph->cycleEnd)
            n = (int) juce::jmin ((juce::int64) n, graph->cycleEnd - position);

        if (isPlayingNow && isRecordingNow && position < recordStart)
            n = (int) juce::jmin ((juce::int64) n, recordStart - position);

        auto* mixL = mixBuffer.getWritePointer (0);
        auto* mixR = mixBuffer.getWritePointer (1);
        juce::FloatVectorOperations::clear (mixL, n);
        juce::FloatVectorOperations::clear (mixR, n);

        if (isRecordingNow && isPlayingNow && position >= recordStart)
            recordChunk (inputs, numInputs, offset, n);

        if (graph != nullptr)
        {
            renderChunk (*graph, inputs, numInputs, offset, mixL, mixR, n, isPlayingNow);

            const bool countingIn = isRecordingNow && position < recordStart;
            if (isPlayingNow && (graph->metronome || countingIn))
                renderClick (*graph, mixL, mixR, n);
        }

        if (isPlayingNow)
        {
            const auto next = position + n;

            // Wrap at the cycle end. Only when we arrive at it from inside, so the
            // playhead can still be parked after the cycle and play on from there.
            if (cycling && position < graph->cycleEnd && next >= graph->cycleEnd)
                playheadSample.store (graph->cycleStart);
            else
                playheadSample.store (next);
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

void AudioEngine::recordChunk (const float* const* inputs, int numInputs, int inputOffset, int n) noexcept
{
    const auto blockStart = playheadSample.load();

    for (auto& slot : takeSlots)
    {
        auto* take = slot.load (std::memory_order_acquire);
        if (take == nullptr || take->writer == nullptr)
            continue;

        const float* channels[2];
        for (int c = 0; c < take->numChannels; ++c)
        {
            const auto source = take->firstChannel + c;
            channels[c] = (source < numInputs && inputs[source] != nullptr) ? inputs[source] + inputOffset
                                                                             : silence.getReadPointer (c);
        }

        juce::int64 expected = -1;
        take->startSample.compare_exchange_strong (expected, blockStart);

        if (take->writer->write (channels, n))
            take->samplesWritten.fetch_add (n);
        else
            recordOverflow.store (true); // disk too slow: this block is lost
    }
}

void AudioEngine::renderChunk (const PlaybackGraph& graph, const float* const* inputs, int numInputs, int inputOffset,
                               float* mixL, float* mixR, int n, bool playingNow) noexcept
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

        if (playingNow)
        {
            for (const auto& region : track.regions)
            {
                if (region.audio == nullptr)
                    continue;

                const auto from = juce::jmax (blockStart, region.start);
                const auto to   = juce::jmin (blockEnd, region.start + region.length);

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
        }

        // Input monitoring: hear what's coming in, through this track's volume and pan.
        if (track.monitorInput && track.inputChannel < numInputs && inputs[track.inputChannel] != nullptr)
        {
            const auto* inL = inputs[track.inputChannel] + inputOffset;
            const auto rightIndex = track.inputStereo ? track.inputChannel + 1 : track.inputChannel;
            const auto* inR = (rightIndex < numInputs && inputs[rightIndex] != nullptr) ? inputs[rightIndex] + inputOffset : inL;

            juce::FloatVectorOperations::add (tL, inL, n);
            juce::FloatVectorOperations::add (tR, inR, n);
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

void AudioEngine::renderClick (const PlaybackGraph& graph, float* mixL, float* mixR, int n) noexcept
{
    const auto rate = sampleRate.load();
    const auto samplesPerBeat = graph.secondsPerBeat * rate;

    if (samplesPerBeat < 1.0)
        return;

    const auto blockStart = playheadSample.load();

    // The first beat at or after the start of this chunk.
    auto beat = (juce::int64) std::ceil ((double) blockStart / samplesPerBeat - 1e-9);
    auto nextBeatSample = (juce::int64) std::llround ((double) beat * samplesPerBeat);

    for (int i = 0; i < n; ++i)
    {
        if (blockStart + i == nextBeatSample)
        {
            const bool downbeat = graph.beatsPerBar > 0 && ((beat % graph.beatsPerBar) + graph.beatsPerBar) % graph.beatsPerBar == 0;
            clickPhaseInc = juce::MathConstants<double>::twoPi * (downbeat ? 1600.0 : 1100.0) / rate;
            clickAmplitude = downbeat ? 0.5f : 0.32f;
            clickPhase = 0.0;
            clickSamplesLeft = clickLength;

            ++beat;
            nextBeatSample = (juce::int64) std::llround ((double) beat * samplesPerBeat);
        }

        if (clickSamplesLeft > 0)
        {
            // A short sine "tick" with an exponential decay.
            const auto t = 1.0f - (float) clickSamplesLeft / (float) clickLength;
            const auto s = clickAmplitude * std::exp (-6.0f * t) * (float) std::sin (clickPhase);
            mixL[i] += s;
            mixR[i] += s;
            clickPhase += clickPhaseInc;
            --clickSamplesLeft;
        }
    }
}

void AudioEngine::renderMeasurement (const float* const* inputs, int numInputs,
                                     float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const auto total = measureBuffer.getNumSamples();
    const auto burstLength = (int) measureBurst.size();
    auto* captured = measureBuffer.getWritePointer (0);
    const float* in = (measureInput < numInputs && inputs[measureInput] != nullptr) ? inputs[measureInput] : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto pos = measurePos + i;
        float out = 0.0f;

        if (pos < total)
        {
            for (const auto start : measurePositions)
                if (pos >= start && pos < start + burstLength)
                    out = measureBurst[(size_t) (pos - start)];

            captured[pos] = in != nullptr ? in[i] : 0.0f;
        }

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][i] = out;
    }

    measurePos += numSamples;

    if (measurePos >= total)
        measureState.store (2, std::memory_order_release);
}

} // namespace riff
