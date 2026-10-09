#include "AudioEngine.h"
#include "Resampler.h"

namespace riff
{

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (+ MP3/CoreAudio formats on macOS)
    currentGraph.store (new PlaybackGraph());

    for (auto& slot : takeSlots)
        slot.store (nullptr);

    diskThread.startThread (juce::Thread::Priority::high);
    liveMidi.ensureSize (8192);
    trackMidi.ensureSize (32768);
    prepare (44100.0, 2048);
    startTimer (250);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    midiDeviceListener.reset();

    if (isRecording())
        stopRecording();

    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    diskThread.stopThread (2000);
    retired.clear();
    delete currentGraph.exchange (nullptr);
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    // The device you used last time, or stereo in + stereo out on the default one;
    // output only if input can't open (no input device, or microphone permission denied).
    juce::String error = "no saved device";
    if (savedState != nullptr)
        error = deviceManager.initialise (2, 2, savedState, true);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceManager.addAudioCallback (this);

    // Listen to every MIDI keyboard, including ones plugged in later.
    enableAllMidiInputs();
    deviceManager.addMidiInputDeviceCallback ({}, this);
    midiDeviceListener = std::make_unique<juce::MidiDeviceListConnection> (
        juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); }));

    return error;
}

void AudioEngine::enableAllMidiInputs()
{
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (device.identifier))
            deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    sendLiveMidi (message);
}

void AudioEngine::sendLiveMidi (const juce::MidiMessage& message) noexcept
{
    // Only short channel messages (notes, controllers, pitch bend...).
    if (message.getRawDataSize() > 3 || message.isSysEx() || message.isActiveSense() || message.isMidiClock())
        return;

    const juce::SpinLock::ScopedLockType lock (liveProducerLock);

    if (liveFifo.getFreeSpace() < 1)
        return;

    const auto scope = liveFifo.write (1);
    auto& e = liveEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    e.size = message.getRawDataSize();
    for (int i = 0; i < e.size; ++i)
        e.data[i] = message.getRawData()[i];
}

void AudioEngine::drainRecordedMidi()
{
    const auto ready = recordFifo.getNumReady();
    if (ready <= 0)
        return;

    const auto scope = recordFifo.read (ready);
    for (int i = 0; i < scope.blockSize1; ++i) recordedMidi.push_back (recordEvents[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) recordedMidi.push_back (recordEvents[(size_t) (scope.startIndex2 + i)]);
}

//==============================================================================
// Graph hand-over
//==============================================================================
void AudioEngine::setGraph (std::unique_ptr<PlaybackGraph> newGraph)
{
    JUCE_ASSERT_MESSAGE_THREAD

    jassert (newGraph != nullptr);

    // Delay lines are made here, on the message thread, before the audio thread
    // can see a graph that needs them. They live as long as the engine.
    for (const auto& track : newGraph->tracks)
        if ((track.preDelay > 0 || track.postDelay > 0) && juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks))
            if (trackDelays[(size_t) track.slot] == nullptr)
                trackDelays[(size_t) track.slot] = std::make_unique<DelayLine>();

    auto* old = currentGraph.exchange (newGraph.release(), std::memory_order_acq_rel);

    // The audio thread may be mid-block with `old`. It is safe to free once at
    // least one complete callback has started after this point.
    retired.push_back ({ std::unique_ptr<PlaybackGraph> (old), callbackCounter.load() });
    collectGarbage();
}

void AudioEngine::timerCallback()
{
    collectGarbage();

    if (isRecording())
        drainRecordedMidi();
}

void AudioEngine::collectGarbage()
{
    if (! deviceRunning.load() && ! bouncing.load())
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

int AudioEngine::getPlaybackLatencySamples() const noexcept
{
    const auto* graph = currentGraph.load();
    return graph != nullptr ? graph->playbackLatency : 0;
}

std::pair<float, float> AudioEngine::takeTrackLevel (int slot) noexcept
{
    if (! juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
        return { 0.0f, 0.0f };

    return { trackPeaks[(size_t) slot * 2].exchange (0.0f), trackPeaks[(size_t) slot * 2 + 1].exchange (0.0f) };
}

juce::String AudioEngine::bounce (const juce::File& file, double startSeconds, double endSeconds, int bitDepth,
                                  std::function<bool (double)> progress)
{
    if (isRecording() || isMeasuringLatency())
        return "Stop recording first.";

    beginOfflineRender();
    auto error = renderOffline (file, startSeconds, endSeconds, bitDepth, std::move (progress));
    endOfflineRender();
    return error;
}

void AudioEngine::beginOfflineRender()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Take the engine away from the device while we render it ourselves.
    offlineWasAttached = deviceManager.getCurrentAudioDevice() != nullptr;
    deviceManager.removeAudioCallback (this);

    offlineSavedPlayhead = playheadSample.load();
    offlineSavedPlaying = playing.load();
    bouncing.store (true); // keeps old graphs alive until the offline render has moved on
    busComp.reset();       // every bounce starts the same way
}

void AudioEngine::endOfflineRender()
{
    JUCE_ASSERT_MESSAGE_THREAD

    bouncing.store (false);
    busComp.reset();
    playing.store (offlineSavedPlaying);
    playheadSample.store (offlineSavedPlayhead);

    if (offlineWasAttached)
        deviceManager.addAudioCallback (this);
}

juce::String AudioEngine::renderOffline (const juce::File& file, double startSeconds, double endSeconds, int bitDepth,
                                         std::function<bool (double)> progress)
{
    if (endSeconds <= startSeconds)
        return "There's nothing to export yet.";

    file.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream->failedToOpen())
        return "Couldn't write " + file.getFullPathName();

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
    const auto rate = sampleRate.load();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (bitDepth));
    if (writer == nullptr)
        return "Couldn't create the WAV file.";

    playheadSample.store ((juce::int64) std::llround (startSeconds * rate));
    playing.store (true);

    const auto total = (juce::int64) std::llround ((endSeconds - startSeconds) * rate);
    juce::AudioBuffer<float> block (2, 1024);
    juce::String error;

    for (juce::int64 done = 0; done < total;)
    {
        const auto n = (int) juce::jmin ((juce::int64) block.getNumSamples(), total - done);
        render (nullptr, 0, block.getArrayOfWritePointers(), 2, n);

        if (! writer->writeFromAudioSampleBuffer (block, 0, n))
        {
            error = "Writing the file failed (is the disk full?).";
            break;
        }

        done += n;

        if (progress && ! progress ((double) done / (double) total))
        {
            error = "cancelled";
            break;
        }
    }

    writer.reset();
    playing.store (false);

    if (error.isNotEmpty())
        file.deleteFile();

    return error;
}

AudioEngine::MixMeasurement AudioEngine::measureMix (double startSeconds, double endSeconds, std::function<bool (double)> progress)
{
    MixMeasurement result;
    if (endSeconds <= startSeconds)
        return result;

    const auto rate = sampleRate.load();
    measuredTracks.assign ((size_t) PlaybackGraph::maxTracks, {});
    for (auto& s : measuredTracks)
        s.prepare (rate);
    measuredMaster.reset();
    measuredMaster.prepare (rate);
    measuring = true;

    playheadSample.store ((juce::int64) std::llround (startSeconds * rate));
    playing.store (true);

    const auto total = (juce::int64) std::llround ((endSeconds - startSeconds) * rate);
    juce::AudioBuffer<float> block (2, 1024);

    juce::int64 done = 0;
    while (done < total)
    {
        const auto n = (int) juce::jmin ((juce::int64) block.getNumSamples(), total - done);
        render (nullptr, 0, block.getArrayOfWritePointers(), 2, n);
        measuredMaster.add (block.getReadPointer (0), block.getReadPointer (1), n);
        done += n;

        if (progress && ! progress ((double) done / (double) total))
            break;
    }

    measuring = false;
    playing.store (false);

    result.tracks = std::move (measuredTracks);
    measuredTracks.clear();
    result.master = measuredMaster;
    result.seconds = (double) done / rate;
    return result;
}

float AudioEngine::rmsOf (const float* left, const float* right, int n) noexcept
{
    if (n <= 0)
        return 0.0f;

    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) left[i] * left[i] + (double) right[i] * right[i];
    return (float) std::sqrt (sum / (2.0 * n));
}

std::pair<float, float> AudioEngine::takeBusPluginLevels() noexcept
{
    return { busPluginIn.exchange (0.0f), busPluginOut.exchange (0.0f) };
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
            if (! t->isMidi)
                t->file.deleteFile();
        }
        error = message;
        return false;
    };

    bool anyMidi = false;

    for (const auto& request : requests)
    {
        if (request.midi)
        {
            auto take = std::make_unique<Take>();
            take->trackId = request.trackId;
            take->isMidi = true;
            newTakes.push_back (std::move (take));
            anyMidi = true;
            continue;
        }

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

    recordedMidi.clear();
    recordFifo.reset();
    midiTakeActive.store (anyMidi);

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

    midiTakeActive.store (false);
    drainRecordedMidi();

    for (auto& take : takes)
    {
        take->writer.reset(); // flushes the remaining FIFO contents and closes the file

        const auto written = take->samplesWritten.load();
        const auto start = take->startSample.load();

        if (take->isMidi)
        {
            if (written > 0 && start >= 0)
                finished.push_back ({ take->trackId, {}, start, written, true, recordedMidi });
            continue;
        }

        if (written > 0 && start >= 0)
            finished.push_back ({ take->trackId, take->file, start, written, false, {} });
        else
            take->file.deleteFile(); // nothing was captured
    }

    recordedMidi.clear();

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
        const bool labelled = device->getName() == labelledDevice;

        for (int i = 0; i < all.size(); ++i)
            if (active[i])
                names.add (labelled && inputLabels[i].isNotEmpty() ? inputLabels[i]
                                                                   : (all[i].isNotEmpty() ? all[i] : "Input " + juce::String (i + 1)));
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
void AudioEngine::prepare (double newSampleRate, int)
{
    // Every block is processed in chunks of at most maxChunkSize, so instruments
    // and plugins can be prepared once for that size whatever the device uses.
    const int maxBlockSize = maxChunkSize;

    // Keep the playhead at the same time if the sample rate changes.
    const auto oldRate = sampleRate.load();
    if (oldRate > 0.0 && std::abs (oldRate - newSampleRate) > 0.5)
        playheadSample.store ((juce::int64) ((double) playheadSample.load() * newSampleRate / oldRate));

    sampleRate.store (newSampleRate);

    trackBuffer.setSize (2, maxBlockSize);
    mixBuffer.setSize (2, maxBlockSize);
    directBuffer.setSize (2, maxBlockSize);
    busBuffer.setSize (PlaybackGraph::maxBuses * 2, maxBlockSize);

    if (directDelayLine == nullptr)
        directDelayLine = std::make_unique<DelayLine>();
    directDelayLine->clear();

    for (auto& line : trackDelays)
        if (line != nullptr)
            line->clear();
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
    busComp.setSettings (currentGraph.load()->busComp);
    busComp.prepare (newSampleRate);

    for (auto& console : consoles)
        console.prepare (newSampleRate);

    clickLength = (int) (newSampleRate * 0.035);
    clickSamplesLeft = 0;

    expectedPosition = playheadSample.load();
    wasPlaying = false;
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
        masterGain.setTargetValue ((isPlayingNow && ! graph->masterVolumeAutomation.empty())
                                       ? graph->masterVolumeAutomation.valueAt (playheadSample.load())
                                       : graph->masterGain);

    // Live MIDI that arrived since the last block lands at its start.
    liveMidi.clear();
    {
        const auto ready = liveFifo.getNumReady();
        if (ready > 0)
        {
            const auto scope = liveFifo.read (ready);
            auto take = [this] (int index) { const auto& e = liveEvents[(size_t) index]; liveMidi.addEvent (e.data, e.size, 0); };
            for (int i = 0; i < scope.blockSize1; ++i) take (scope.startIndex1 + i);
            for (int i = 0; i < scope.blockSize2; ++i) take (scope.startIndex2 + i);
        }
    }

    // Keep what you play for MIDI recording, at the timeline position it was heard.
    if (isRecordingNow && isPlayingNow && midiTakeActive.load() && ! liveMidi.isEmpty()
        && playheadSample.load() >= recordStart)
    {
        const auto at = playheadSample.load();
        for (const auto meta : liveMidi)
        {
            if (meta.numBytes < 1 || recordFifo.getFreeSpace() < 1)
                continue;

            const auto scope = recordFifo.write (1);
            auto& r = recordEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
            r.sample = at;
            r.status = meta.data[0];
            r.data1 = meta.numBytes > 1 ? meta.data[1] : 0;
            r.data2 = meta.numBytes > 2 ? meta.data[2] : 0;
        }
    }

    bool liveMidiPending = ! liveMidi.isEmpty();
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

        // Any jump (start, stop, locate, cycle wrap) silences notes from regions,
        // so nothing hangs on.
        const bool resetNotes = isPlayingNow != wasPlaying || (isPlayingNow && position != expectedPosition);

        if (graph != nullptr)
        {
            renderChunk (*graph, inputs, numInputs, offset, mixL, mixR, n, isPlayingNow, resetNotes, liveMidiPending);
            liveMidiPending = false;

            // The master chain (e.g. a limiter) on the sum of every track; the click comes after it.
            for (const auto& effect : graph->masterInserts)
                effect->process (mixL, mixR, n);

            // Then the Master Bus Compressor, before the master fader: the user's
            // own plugin in console mode, or the built-in one.
            if (! graph->busCompPlugins.empty())
            {
                storeMax (busPluginIn, rmsOf (mixL, mixR, n));
                for (const auto& effect : graph->busCompPlugins)
                    effect->process (mixL, mixR, n);
                storeMax (busPluginOut, rmsOf (mixL, mixR, n));
            }
            else
            {
                busComp.setSettings (graph->busComp);
                busComp.process (mixL, mixR, n);
            }

            const bool countingIn = isRecordingNow && position < recordStart;
            if (isPlayingNow && (graph->metronome || countingIn) && ! bouncing)
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

            expectedPosition = next;
        }

        wasPlaying = isPlayingNow;

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

        if (auto* tap = screenTap.load (std::memory_order_acquire); tap != nullptr && ! bouncing.load())
            tap->push (mixL, mixR, n, inputs, numInputs, offset);

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
        if (take == nullptr)
            continue;

        if (take->isMidi)
        {
            juce::int64 expectedStart = -1;
            take->startSample.compare_exchange_strong (expectedStart, blockStart);
            take->samplesWritten.fetch_add (n);
            continue;
        }

        if (take->writer == nullptr)
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

void AudioEngine::collectNotes (const PlaybackGraph::Track& track, juce::int64 from, juce::int64 to) noexcept
{
    // Note-offs first, so a note ending exactly where the next one starts is
    // released before it's struck again.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& region : track.regions)
        {
            for (const auto& note : region.notes)
            {
                if (note.start >= to)
                    break; // sorted by start; later notes can't end in this block either

                if (pass == 0 && note.end >= from && note.end < to)
                {
                    const juce::uint8 off[3] = { 0x80, note.pitch, 0 };
                    trackMidi.addEvent (off, 3, (int) (note.end - from));
                }
                else if (pass == 1 && note.start >= from)
                {
                    const juce::uint8 on[3] = { 0x90, note.pitch, note.velocity };
                    trackMidi.addEvent (on, 3, (int) (note.start - from));
                }
            }
        }
    }
}

void AudioEngine::renderChunk (const PlaybackGraph& graph, const float* const* inputs, int numInputs, int inputOffset,
                               float* mixL, float* mixR, int n, bool playingNow,
                               bool resetNotes, bool withLiveMidi) noexcept
{
    // Routing: ordinary tracks sum into `direct` and feed the bus buffers through
    // their sends; then each bus processes what it received; both reach the mix.
    auto* dL = directBuffer.getWritePointer (0);
    auto* dR = directBuffer.getWritePointer (1);
    juce::FloatVectorOperations::clear (dL, n);
    juce::FloatVectorOperations::clear (dR, n);

    for (const auto& track : graph.tracks)
    {
        if (track.isBus && juce::isPositiveAndBelow (track.busIndex, PlaybackGraph::maxBuses))
        {
            juce::FloatVectorOperations::clear (busBuffer.getWritePointer (track.busIndex * 2), n);
            juce::FloatVectorOperations::clear (busBuffer.getWritePointer (track.busIndex * 2 + 1), n);
        }
    }

    for (const auto& track : graph.tracks)
        if (! track.isBus)
            renderTrack (track, inputs, numInputs, inputOffset, dL, dR, n, playingNow, resetNotes, withLiveMidi);

    // Tracks heard directly wait for the slowest bus (delay compensation).
    if (graph.directDelay > 0)
        directDelayLine->process (dL, dR, n, graph.directDelay);

    juce::FloatVectorOperations::add (mixL, dL, n);
    juce::FloatVectorOperations::add (mixR, dR, n);

    for (const auto& track : graph.tracks)
        if (track.isBus)
            renderTrack (track, inputs, numInputs, inputOffset, mixL, mixR, n, playingNow, resetNotes, withLiveMidi);
}

void AudioEngine::renderTrack (const PlaybackGraph::Track& track, const float* const* inputs, int numInputs, int inputOffset,
                               float* outL, float* outR, int n, bool playingNow,
                               bool resetNotes, bool withLiveMidi) noexcept
{
    if (track.slot < 0 || track.slot >= PlaybackGraph::maxTracks)
        return;

    const auto blockStart = playheadSample.load();
    const auto blockEnd = blockStart + n;
    auto* tL = trackBuffer.getWritePointer (0);
    auto* tR = trackBuffer.getWritePointer (1);
    auto& state = trackStates[(size_t) track.slot];

    // Automation (read once per chunk; the gain smoothing below hides the steps).
    const auto volume = (playingNow && ! track.volumeAutomation.empty()) ? track.volumeAutomation.valueAt (blockStart) : track.volume;
    const auto pan = (playingNow && ! track.panAutomation.empty()) ? track.panAutomation.valueAt (blockStart) : track.pan;

    if (playingNow)
        for (const auto& automation : track.paramAutomation)
            automation.effect->setParameter (automation.paramIndex, automation.curve.valueAt (blockStart));

    // Balance-style pan: unity at centre, the far side fades out.
    const auto targetL = track.audible ? volume * (pan <= 0.0f ? 1.0f : 1.0f - pan) : 0.0f;
    const auto targetR = track.audible ? volume * (pan >= 0.0f ? 1.0f : 1.0f + pan) : 0.0f;

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

    const bool hasInserts = ! track.inserts.empty() || ! track.stripPlugins.empty();
    bool anyAudio = false;

    juce::FloatVectorOperations::clear (tL, n);
    juce::FloatVectorOperations::clear (tR, n);

    if (track.isBus)
    {
        // A bus plays whatever the sends gave it.
        if (juce::isPositiveAndBelow (track.busIndex, PlaybackGraph::maxBuses))
        {
            juce::FloatVectorOperations::copy (tL, busBuffer.getReadPointer (track.busIndex * 2), n);
            juce::FloatVectorOperations::copy (tR, busBuffer.getReadPointer (track.busIndex * 2 + 1), n);
            anyAudio = true;
        }
    }
    else if (track.isInstrument)
    {
        // Instruments always run, even when muted, so their notes start and stop on time.
        if (track.instrument != nullptr)
        {
            trackMidi.clear();

            if (resetNotes)
            {
                const juce::uint8 allOff[3] = { 0xb0, 123, 0 };
                trackMidi.addEvent (allOff, 3, 0);
            }

            if (playingNow)
                collectNotes (track, blockStart, blockEnd);

            if (withLiveMidi && track.receivesLiveMidi)
                trackMidi.addEvents (liveMidi, 0, -1, 0);

            track.instrument->render (tL, tR, n, trackMidi);
            anyAudio = true;
        }
    }
    else
    {
        // Silent and not fading, with no effect tails or sends to keep running: nothing to do.
        if (! hasInserts && track.sends.empty() && ! state.gainL.isSmoothing() && ! state.gainR.isSmoothing()
            && state.gainL.getCurrentValue() <= 0.0f && state.gainR.getCurrentValue() <= 0.0f)
            return;

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

                const auto& source = region.audio->buffer;
                const auto destOffset = (int) (from - blockStart);

                if (region.resample)
                {
                    // Another sample rate: convert on the fly, reading at fractional positions.
                    const auto position = region.sourcePosition + (double) (from - region.start) * region.step;
                    if (position >= (double) source.getNumSamples())
                        continue;

                    resampler::addResampled (source, position, region.step, region.gain, tL + destOffset, tR + destOffset, (int) (to - from));
                    anyAudio = true;
                    continue;
                }

                const auto sourceStart = region.sourceOffset + (from - region.start);
                const auto available = (juce::int64) source.getNumSamples() - sourceStart;

                if (sourceStart < 0 || available <= 0)
                    continue;

                const auto count = (int) juce::jmin (to - from, available);

                juce::FloatVectorOperations::addWithMultiply (tL + destOffset, source.getReadPointer (0, (int) sourceStart), region.gain, count);
                juce::FloatVectorOperations::addWithMultiply (tR + destOffset, source.getReadPointer (1, (int) sourceStart), region.gain, count);
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
    }

    // The strip's console EQ and Dynamics, then the insert effects, all before
    // the fader. Inserts run even on silence so delays and reverbs ring out.
    for (const auto& effect : track.stripPlugins)
        effect->process (tL, tR, n);

    if (track.console.isActive())
    {
        auto& console = consoles[(size_t) track.slot];
        console.setSettings (track.console);
        console.process (tL, tR, n);
    }

    for (const auto& effect : track.inserts)
        effect->process (tL, tR, n);

    // Delay compensation: line this track up with the slowest one.
    const auto compensation = track.isBus ? track.postDelay : track.preDelay;
    if (compensation > 0)
        if (auto* line = trackDelays[(size_t) track.slot].get())
            line->process (tL, tR, n, compensation);

    if (! anyAudio && ! hasInserts && track.sends.empty())
    {
        // Still advance the gain ramps so they stay in sync with time.
        state.gainL.skip (n);
        state.gainR.skip (n);
        return;
    }

    // Pre-fader sends (silenced with the track when it's muted).
    for (const auto& send : track.sends)
    {
        if (! send.preFader || ! track.audible || ! juce::isPositiveAndBelow (send.busIndex, PlaybackGraph::maxBuses))
            continue;

        juce::FloatVectorOperations::addWithMultiply (busBuffer.getWritePointer (send.busIndex * 2), tL, send.level, n);
        juce::FloatVectorOperations::addWithMultiply (busBuffer.getWritePointer (send.busIndex * 2 + 1), tR, send.level, n);
    }

    // Fader and pan.
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        tL[i] *= state.gainL.getNextValue();
        tR[i] *= state.gainR.getNextValue();
        peakL = juce::jmax (peakL, std::abs (tL[i]));
        peakR = juce::jmax (peakR, std::abs (tR[i]));
    }

    // Post-fader sends.
    for (const auto& send : track.sends)
    {
        if (send.preFader || ! juce::isPositiveAndBelow (send.busIndex, PlaybackGraph::maxBuses))
            continue;

        juce::FloatVectorOperations::addWithMultiply (busBuffer.getWritePointer (send.busIndex * 2), tL, send.level, n);
        juce::FloatVectorOperations::addWithMultiply (busBuffer.getWritePointer (send.busIndex * 2 + 1), tR, send.level, n);
    }

    juce::FloatVectorOperations::add (outL, tL, n);
    juce::FloatVectorOperations::add (outR, tR, n);

    if (measuring && juce::isPositiveAndBelow (track.slot, (int) measuredTracks.size()))
        measuredTracks[(size_t) track.slot].add (tL, tR, n);

    storeMax (trackPeaks[(size_t) track.slot * 2], peakL);
    storeMax (trackPeaks[(size_t) track.slot * 2 + 1], peakR);
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
