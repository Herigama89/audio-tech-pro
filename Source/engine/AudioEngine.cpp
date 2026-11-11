#include "AudioEngine.h"
#include "Resampler.h"

namespace riff
{

class AudioEngine::AheadThread final : public juce::Thread
{
public:
    explicit AheadThread (AudioEngine& e) : juce::Thread ("Audio Tech Pro render ahead"), engine (e) {}
    ~AheadThread() override
    {
        signalThreadShouldExit();
        event.signal();
        stopThread (4000);
    }

    void begin (double rate)
    {
        // A realtime thread, as the audio thread (the system keeps it on the fast cores)
        const auto options = juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime (juce::jmax (64, engine.aheadChunk.load()),
                                                                                                juce::jmax (8000.0, rate));
        if (! startRealtimeThread (options))
            startThread (juce::Thread::Priority::highest);
    }

    /** The audio device's workgroup, to join (macOS): scheduled with the audio thread, on the performance cores. */
    void setWorkgroup (const juce::AudioWorkgroup& g)
    {
        {
            std::lock_guard<std::mutex> lock (groupLock);
            group = g;
        }
        groupVersion.fetch_add (1);
        event.signal();
    }
    void joinWorkgroupIfChanged()
    {
        if (const auto v = groupVersion.load(); v != joined)
        {
            joined = v;
            std::lock_guard<std::mutex> lock (groupLock);
            if (group)
                group.join (token);
            else
                token.reset();   // (no device: leave the old one's, here on this thread)
        }
    }

    void wake() noexcept { event.signal(); }
    void rest (int ms) { event.wait (ms); }
    void run() override
    {
        juce::FloatVectorOperations::disableDenormalisedNumberSupport (true);
        engine.runAhead (*this);
        // Leave the device's workgroup before the thread ends, on this thread: macOS only lets the thread that joined
        // leave (the token left to the destructor, on the thread that ends us - when you choose another audio
        // interface - crashed the app)
        token.reset();
    }

private:
    AudioEngine& engine;
    juce::WaitableEvent event;
    std::mutex groupLock;
    juce::AudioWorkgroup group;
    std::atomic<juce::uint32> groupVersion { 0 };
    juce::uint32 joined = 0;
    juce::WorkgroupToken token;
};


AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (+ MP3/CoreAudio formats on macOS)
    currentGraph.store (new PlaybackGraph());
    outputSlots.fill (-1);

    for (auto& slot : takeSlots)
        slot.store (nullptr);

    // (tests: the whole engine suite can run rendering ahead, waiting for each chunk however long it takes)
    if (const auto v = juce::SystemStats::getEnvironmentVariable ("RIFF_PROCESS_AHEAD", {}); v.isNotEmpty())
    {
        processAheadWanted.store (v.getIntValue());
        aheadWaitForever.store (true);
    }

    for (auto* overrides : { &volumeOverride, &panOverride, &volumeMix, &panMix })
        for (auto& v : *overrides)
            v.store (std::numeric_limits<float>::quiet_NaN());

    diskThread.startThread (juce::Thread::Priority::high);
    liveMidi.ensureSize (8192);
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
    aheadThread.reset();
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

   #if JUCE_WINDOWS && JUCE_ASIO
    // First time on Windows: the interface's own ASIO driver, when one is installed (the lowest latency).
    // The ASIO4ALL-style generic wrappers come after a maker's own driver.
    if (savedState == nullptr)
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                auto names = type->getDeviceNames();
                std::stable_partition (names.begin(), names.end(), [] (const juce::String& n)
                                       { return ! n.containsIgnoreCase ("ASIO4ALL") && ! n.containsIgnoreCase ("FlexASIO") && ! n.containsIgnoreCase ("Generic"); });
                if (names.isEmpty())
                    break;
                const auto before = deviceManager.getAudioDeviceSetup();
                const auto beforeType = deviceManager.getCurrentAudioDeviceType();
                deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                auto setup = deviceManager.getAudioDeviceSetup();
                setup.outputDeviceName = setup.inputDeviceName = names[0];
                setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
                if (deviceManager.setAudioDeviceSetup (setup, true).isNotEmpty())
                {
                    // (it wouldn't open: back to what worked)
                    deviceManager.setCurrentAudioDeviceType (beforeType, true);
                    deviceManager.setAudioDeviceSetup (before, true);
                }
                else
                    error.clear();
                break;
            }
   #endif

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

    // A copy for the message thread.
    if (message.getRawDataSize() <= 3 && ! message.isActiveSense() && ! message.isMidiClock())
    {
        const juce::SpinLock::ScopedLockType lock (incomingLock);
        if (incomingFifo.getFreeSpace() > 0)
        {
            const auto scope = incomingFifo.write (1);
            auto& e = incomingEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
            e.size = message.getRawDataSize();
            for (int i = 0; i < e.size; ++i)
                e.data[i] = message.getRawData()[i];
        }
    }
}

bool AudioEngine::previewFile (const juce::File& file)
{
    stopPreview();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    auto buffer = std::make_unique<juce::AudioBuffer<float>> (2, juce::jmax (1, length));
    reader->read (buffer.get(), 0, length, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        buffer->copyFrom (1, 0, *buffer, 0, 0, length);

    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewHold = std::move (buffer);
    previewBuffer = previewHold.get();
    previewPosition = 0.0;
    previewDelay = 0.0;
    previewStep = reader->sampleRate / juce::jmax (1.0, sampleRate.load());
    previewPlaying.store (true);
    return true;
}

void AudioEngine::previewAudio (std::unique_ptr<juce::AudioBuffer<float>> audio, double audioRate, double delaySeconds)
{
    stopPreview();
    if (audio == nullptr || audio->getNumSamples() < 2)
        return;
    if (audio->getNumChannels() < 2)
    {
        audio->setSize (2, audio->getNumSamples(), true);
        audio->copyFrom (1, 0, *audio, 0, 0, audio->getNumSamples());
    }
    const auto rate = sampleRate.load() > 0.0 ? sampleRate.load() : 48000.0;
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewHold = std::move (audio);
    previewBuffer = previewHold.get();
    previewPosition = 0.0;
    previewDelay = juce::jmax (0.0, delaySeconds) * rate;
    previewStep = audioRate / rate;
    previewProgressNow.store (0.0f);
    previewPlaying.store (true);
}

void AudioEngine::stopPreview()
{
    std::unique_ptr<juce::AudioBuffer<float>> old;
    std::shared_ptr<Effect> oldEffect;
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewPlaying.store (false);
        previewBuffer = nullptr;
        old = std::move (previewHold);
        oldEffect = std::move (previewEffect);
        previewLoops = false;
    }
}

void AudioEngine::previewThrough (std::unique_ptr<juce::AudioBuffer<float>> audio, double audioRate, std::shared_ptr<Effect> effect)
{
    stopPreview();
    if (audio == nullptr || audio->getNumSamples() < 2 || effect == nullptr)
        return;
    if (audio->getNumChannels() < 2)
    {
        audio->setSize (2, audio->getNumSamples(), true);
        audio->copyFrom (1, 0, *audio, 0, 0, audio->getNumSamples());
    }
    const auto rate = sampleRate.load() > 0.0 ? sampleRate.load() : 48000.0;
    effect->prepare (rate, maxChunkSize);
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewHold = std::move (audio);
    previewBuffer = previewHold.get();
    previewPosition = 0.0;
    previewStep = audioRate / rate;
    previewDelay = 0.0;
    previewEffect = std::move (effect);
    previewLoops = true;
    previewPlaying.store (true);
}

void AudioEngine::renderPreview (float* left, float* right, int n) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (previewLock);
    if (! lock.isLocked() || previewBuffer == nullptr)
        return;

    const auto length = previewBuffer->getNumSamples();
    const auto* l = previewBuffer->getReadPointer (0);
    const auto* r = previewBuffer->getReadPointer (1);
    if (previewEffect != nullptr && n <= previewScratch.getNumSamples())
    {
        // Live through the effect: the region's audio (round and round), then the effect, as it is right now
        auto* sl = previewScratch.getWritePointer (0);
        auto* sr = previewScratch.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            auto index = (int) previewPosition;
            if (index + 1 >= length)
            {
                previewPosition = 0.0;
                index = 0;
            }
            const auto frac = (float) (previewPosition - index);
            sl[i] = l[index] + (l[index + 1] - l[index]) * frac;
            sr[i] = r[index] + (r[index + 1] - r[index]) * frac;
            previewPosition += previewStep;
        }
        previewEffect->process (sl, sr, n);
        previewEffect->tapOutput (sl, sr, n);
        juce::FloatVectorOperations::add (left, sl, n);
        juce::FloatVectorOperations::add (right, sr, n);
        return;
    }
    const auto gain = previewGain.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (previewDelay >= 1.0)
        {
            previewDelay -= 1.0;   // (waiting for the song's next beat)
            continue;
        }
        const auto index = (int) previewPosition;
        if (index + 1 >= length)
        {
            previewPlaying.store (false);
            return;
        }
        const auto frac = (float) (previewPosition - index);
        left[i]  += (l[index] + (l[index + 1] - l[index]) * frac) * gain;
        right[i] += (r[index] + (r[index + 1] - r[index]) * frac) * gain;
        previewPosition += previewStep;
    }
    previewProgressNow.store ((float) (previewPosition / juce::jmax (1, length)), std::memory_order_relaxed);
}

std::vector<juce::MidiMessage> AudioEngine::takeIncomingMidi()
{
    std::vector<juce::MidiMessage> messages;
    const auto ready = incomingFifo.getNumReady();
    if (ready <= 0)
        return messages;
    const auto scope = incomingFifo.read (ready);
    const auto take = [this, &messages] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& e = incomingEvents[(size_t) (start + i)];
            if (e.size > 0)
                messages.emplace_back (e.data, e.size);
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
    return messages;
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
    {
        if ((track.preDelay > 0 || track.postDelay > 0) && juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks))
            if (trackDelays[(size_t) track.slot] == nullptr)
                trackDelays[(size_t) track.slot] = std::make_unique<DelayLine>();
        if (track.chainLatency > 0 && juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks))
            if (dryDelays[(size_t) track.slot] == nullptr)
                dryDelays[(size_t) track.slot] = std::make_unique<DelayLine>();
    }

    auto* old = currentGraph.exchange (newGraph.release(), std::memory_order_acq_rel);

    // The new graph has the faders' values in it.
    masterMix.store (std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
    for (auto* mix : { &volumeMix, &panMix })
        for (auto& v : *mix)
            v.store (std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);

    // The audio thread may be mid-block with `old`. It is safe to free once at
    // least one complete callback has started after this point.
    retired.push_back ({ std::unique_ptr<PlaybackGraph> (old), callbackCounter.load(), aheadCounter.load() });
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
    // (and the render-ahead thread has started two chunks since, or rests: it takes the newest graph when it wakes)
    const auto aheadNow = aheadCounter.load();
    const bool aheadResting = aheadThread == nullptr || aheadParked.load();

    if (! deviceRunning.load() && ! bouncing.load())
    {
        // No callback can be running (a test may be calling render() itself, but on this thread)
        std::erase_if (retired, [aheadNow, aheadResting] (const Retired& r) { return aheadResting || aheadNow > r.aheadAt + 1; });
        return;
    }

    const auto now = callbackCounter.load();
    std::erase_if (retired, [now, aheadNow, aheadResting] (const Retired& r)
                   { return now > r.retiredAt + 1 && (aheadResting || aheadNow > r.aheadAt + 1); });
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

double AudioEngine::updateShownPlayhead() noexcept
{
    const auto raw = getPlayheadSeconds();
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! isPlaying())
    {
        shownPlayhead = shownFromRaw = raw;
        shownRawAt = now;
        return raw;
    }
    if (! juce::exactlyEqual (raw, shownFromRaw))
    {
        shownFromRaw = raw;
        shownRawAt = now;
    }
    // On from the audio's last step by the time since (never more than a moment: if the audio stalls, so does this)
    const auto estimate = juce::jmax (0.0, shownFromRaw + juce::jmin (now - shownRawAt, 0.08)
                                               - juce::jlimit (0.0, 0.2, outputDelaySeconds.load (std::memory_order_relaxed)));
    if (std::abs (estimate - shownPlayhead) > 0.25)
        shownPlayhead = estimate;                                // a jump: located, or round the cycle
    else
        shownPlayhead = juce::jmax (shownPlayhead, estimate);    // (never a step back: that's a judder too)
    return shownPlayhead;
}

void AudioEngine::setPlayheadSeconds (double seconds) noexcept
{
    const auto at = (juce::int64) std::llround (juce::jmax (0.0, seconds) * sampleRate.load());
    playheadSample.store (at);
    locateRequest.store (at);
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
    parkAhead();   // (the offline render plays every track itself)

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
    aheadNeedsRestart.store (true);
    unparkAhead();

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

juce::String AudioEngine::renderStems (const std::vector<StemTarget>& targets, double startSeconds, double endSeconds,
                                      int bitDepth, std::function<bool (double)> progress, bool beforeFader)
{
    stemBeforeFader = beforeFader;
    if (endSeconds <= startSeconds || targets.empty())
        return "There's nothing to export yet.";

    const auto rate = sampleRate.load();
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    juce::String error;

    for (const auto& target : targets)
    {
        target.file.getParentDirectory().createDirectory();
        target.file.deleteFile();
        auto fileStream = std::make_unique<juce::FileOutputStream> (target.file);
        if (fileStream->failedToOpen())
        {
            error = "Couldn't write " + target.file.getFullPathName();
            break;
        }
        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (bitDepth));
        if (writer == nullptr)
        {
            error = "Couldn't create " + target.file.getFileName();
            break;
        }
        writers.push_back (std::move (writer));
    }

    if (error.isEmpty())
    {
        juce::AudioBuffer<float> capture (PlaybackGraph::maxTracks * 2, 1024);
        juce::AudioBuffer<float> block (2, 1024);
        stemCapture = &capture;

        playheadSample.store ((juce::int64) std::llround (startSeconds * rate));
        playing.store (true);
        const auto total = (juce::int64) std::llround ((endSeconds - startSeconds) * rate);

        for (juce::int64 done = 0; done < total && error.isEmpty();)
        {
            const auto n = (int) juce::jmin ((juce::int64) block.getNumSamples(), total - done);
            capture.clear();
            render (nullptr, 0, block.getArrayOfWritePointers(), 2, n);

            for (size_t i = 0; i < targets.size(); ++i)
            {
                const auto slot = targets[i].slot;
                if (! juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks))
                    continue;
                const float* channels[] = { capture.getReadPointer (slot * 2), capture.getReadPointer (slot * 2 + 1) };
                if (! writers[i]->writeFromFloatArrays (channels, 2, n))
                    error = "Writing the files failed (is the disk full?).";
            }

            done += n;
            if (progress && ! progress ((double) done / (double) total))
                error = "cancelled";
        }

        stemCapture = nullptr;
        playing.store (false);
    }

    writers.clear();
    if (error.isNotEmpty())
        for (const auto& target : targets)
            target.file.deleteFile();
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
        if (measureTap)
            measureTap (block.getReadPointer (0), block.getReadPointer (1), n);
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
    // The RMS level over everything since the last read, in and out (the same stretch of audio for both).
    const auto samples = busPluginSamples.exchange (0);
    const auto in = busPluginIn.exchange (0.0f), out = busPluginOut.exchange (0.0f);
    if (samples <= 0)
        return { 0.0f, 0.0f };
    return { std::sqrt (in / (float) samples), std::sqrt (out / (float) samples) };
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
    recordClipped.store (false);   // (a clip from an earlier take doesn't count)

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
                                                       .withBitsPerSample (recordBitDepth));
        if (writer == nullptr)
            return fail ("Couldn't create a WAV writer for " + request.file.getFileName());

        auto take = std::make_unique<Take>();
        take->trackId = request.trackId;
        take->file = request.file;
        take->firstChannel = request.firstChannel;
        take->numChannels = request.numChannels;
        take->sourceSlot = request.sourceSlot;
        if (juce::isPositiveAndBelow (request.sourceSlot, PlaybackGraph::maxTracks))
            recordSourceSlot[(size_t) request.sourceSlot].store (true);   // (played live, so the take hears it as it is)
        take->printSlot = request.printSlot;
        take->printLatency = juce::jmax (0, request.printLatency);

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
    for (auto& r : recordSourceSlot)
        r.store (false);

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
        LiveTake t { take->trackId, start >= 0 ? (double) start / rate : -1.0, (double) take->samplesWritten.load() / rate, {}, take->clipped.load() };
        const auto count = take->peakCount.load (std::memory_order_acquire);
        t.peaks.resize ((size_t) count);
        for (int i = 0; i < count; ++i)
            t.peaks[(size_t) i] = take->peaks[(size_t) i].load (std::memory_order_relaxed);
        live.push_back (std::move (t));
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
    // The render threads work to the device's deadline with the audio thread (macOS: its audio workgroup)
    renderThreads.setWorkgroup (device->getWorkgroup());
    if (aheadThread != nullptr)
        aheadThread->setWorkgroup (device->getWorkgroup());

    // The callback hands over only the outputs that are switched on, packed together: where each one lands
    setActiveOutputs (device->getActiveOutputChannels());
    // (how long after a block is handed over it's heard: the drawn playhead sits there, as Logic's does)
    const auto rate = juce::jmax (1.0, device->getCurrentSampleRate());
    outputDelaySeconds.store ((device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples()) / rate);
}

void AudioEngine::setActiveOutputs (const juce::BigInteger& active)
{
    int packed = 0;
    for (int ch = 0; ch < maxHardwareOutputs; ++ch)
        outputSlots[(size_t) ch] = active[ch] ? packed++ : -1;
    deviceRunning.store (true);
}

void AudioEngine::audioDeviceStopped()
{
    parkAhead();
    deviceRunning.store (false);
    renderThreads.setWorkgroup ({});
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto t0 = juce::Time::getHighResolutionTicks();
    deviceBlock.store (numSamples, std::memory_order_relaxed);
    render (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);

    // The Performance meter: this buffer's processing time against the time it plays for.
    const auto rate = sampleRate.load();
    if (rate > 0.0 && numSamples > 0)
    {
        const auto load = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0)
                                   / ((double) numSamples / rate));
        storeMax (loadNow, load);
        storeMax (loadPeak, load);
        callbackBusy.store (callbackBusy.load (std::memory_order_relaxed) + (double) load * numSamples / rate, std::memory_order_relaxed);
        callbackSpan.store (callbackSpan.load (std::memory_order_relaxed) + (double) numSamples / rate, std::memory_order_relaxed);
        if (load > 1.0f)
            overloads.fetch_add (1, std::memory_order_relaxed);
    }
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

    // The render threads (the machine's other cores) and every thread's scratch space
    {
        const auto wanted = wantedRenderThreads.load();
        const auto workers = wanted < 0 ? RenderThreads::suggestedWorkers() : wanted;
        if (workers != startedRenderThreads)
        {
            renderThreads.start (workers, newSampleRate, 256);
            startedRenderThreads = workers;
        }
        if ((int) scratches.size() != workers + 1)
        {
            scratches.clear();
            for (int i = 0; i <= workers; ++i)
            {
                auto s = std::make_unique<Scratch>();
                s->track.setSize (2, maxBlockSize);
                s->region.setSize (2, maxBlockSize);
                s->dry.setSize (2, maxBlockSize);
                s->stream.setSize (2, maxBlockSize * 8 + 4 * resampler::halfWidth + 16);   // up to 8x the session's rate
                s->midi.ensureSize (32768);
                scratches.push_back (std::move (s));
            }
        }
        for (auto* pass : { &mainPass, &livePass })
        {
            pass->parallel.reserve (PlaybackGraph::maxTracks + 64);
            pass->direct.setSize (2, maxBlockSize);
            pass->bus.setSize (PlaybackGraph::maxBuses * 2, maxBlockSize);
            if (pass->directDelay == nullptr)
                pass->directDelay = std::make_unique<DelayLine>();
            pass->directDelay->clear();
        }
        mainPass.usePool = true;
        mainPass.scratch = scratches.front().get();
        liveScratch.track.setSize (2, maxBlockSize);
        liveScratch.region.setSize (2, maxBlockSize);
        liveScratch.dry.setSize (2, maxBlockSize);
        liveScratch.stream.setSize (2, maxBlockSize * 8 + 4 * resampler::halfWidth + 16);
        liveScratch.midi.ensureSize (32768);
        livePass.usePool = false;
        livePass.scratch = &liveScratch;
    }
    preFaderOutputs.setSize (PlaybackGraph::maxTracks * 2, maxBlockSize);
    preFaderReady.fill (false);
    mixBuffer.setSize (2, maxBlockSize);
    masterDry.setSize (2, maxBlockSize);
    previewScratch.setSize (2, maxBlockSize);
    hardwareBuffer.setSize (maxHardwareOutputs, maxBlockSize);
    hardwareBuffer.clear();
    slotOutputs.setSize (PlaybackGraph::maxTracks * 2, maxBlockSize);
    slotOutputs.clear();
    slotInputs.setSize (PlaybackGraph::maxTracks * 2, maxBlockSize);
    slotInputs.clear();
    slotInputReady.fill (false);


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
    for (auto& tape : tapes)
        tape.prepare (newSampleRate);
    for (auto& pre : preamps)
        pre.prepare (newSampleRate);
    masterTape.prepare (newSampleRate);

    clickLength = (int) (newSampleRate * 0.035);
    clickSamplesLeft = 0;

    expectedPosition = playheadSample.load();
    wasPlaying = false;

    // Render ahead (Process Buffer Range): its thread, when it's on. Nothing renders while we're here (the device
    // is starting, or a test is setting up), so the thread can be made or ended safely.
    aheadThread.reset();
    if (const auto chunk = processAheadWanted.load(); chunk > 0)
    {
        aheadChunk.store (juce::jlimit (64, aheadMaxChunk, chunk));
        aheadLookahead.store (juce::jmax (2 * aheadChunk.load(), 1536));   // (at least ~30 ms of cushion, plus two device buffers: see runAhead)
        aheadWritten.store (0);
        aheadRead.store (0);
        aheadNeedsRestart.store (true);
        aheadPark.store (bouncing.load());
        aheadParked.store (true);
        aheadThread = std::make_unique<AheadThread> (*this);
        aheadThread->begin (newSampleRate);
    }
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
        aheadNeedsRestart.store (true);   // (the ring waited meanwhile: start it again from here after)
        callbackCounter.fetch_add (1, std::memory_order_release);
        return;
    }

    // Pick up the latest graph once per block.
    const auto* graph = currentGraph.load (std::memory_order_acquire);
    bool isPlayingNow = playing.load();

    // Render ahead: the tracks you aren't playing live come from the ring. A start, a stop or a locate restarts it
    // from there (the callback is the only one that asks).
    const bool ahead = aheadActive() && graph != nullptr;
    if (ahead)
    {
        const auto locate = locateRequest.exchange (std::numeric_limits<juce::int64>::min());

        // A new graph that makes a track live (armed, monitored, played from your keyboard) that the ring has it
        // rendered ahead: start again from here, so it's heard live at once rather than a lookahead later
        bool newlyLive = false;
        if (graph != aheadGraphSeen)
        {
            aheadGraphSeen = graph;
            std::array<int, aheadBusCarry> unused {};
            aheadRequired = liveSlotsOf (*graph, {}, unused);
        }
        if (const auto read = aheadRead.load (std::memory_order_acquire); read < aheadWritten.load (std::memory_order_acquire))
        {
            const auto& front = aheadRing[(size_t) (read % aheadSlots)];
            newlyLive = front.serial == aheadSerial.load (std::memory_order_relaxed) && (aheadRequired & ~front.live).any();
        }

        if (aheadNeedsRestart.exchange (false) || newlyLive || isPlayingNow != aheadSessionPlaying || locate != std::numeric_limits<juce::int64>::min())
        {
            aheadSessionPlaying = isPlayingNow;
            aheadStartPosition.store (locate != std::numeric_limits<juce::int64>::min() ? locate : playheadSample.load());
            aheadStartPlaying.store (isPlayingNow);
            aheadSerial.fetch_add (1, std::memory_order_acq_rel);
            aheadPrefetching = true;
            playheadSample.store (aheadStartPosition.load());
            aheadThread->wake();
        }
    }
    else
        locateRequest.store (std::numeric_limits<juce::int64>::min());
    const auto callbackStart = juce::Time::getMillisecondCounterHiRes();
    const auto rateNow = sampleRate.load();
    const bool isRecordingNow = recording.load (std::memory_order_acquire);
    const auto recordStart = recordFrom.load();

    const bool cycling = graph != nullptr && graph->cycleEnabled && graph->cycleEnd > graph->cycleStart;

    if (graph != nullptr)
        masterGain.setTargetValue ((isPlayingNow && ! graph->masterVolumeAutomation.empty())
                                       ? graph->masterVolumeAutomation.valueAt (playheadSample.load())
                                       : (std::isnan (masterMix.load (std::memory_order_relaxed)) ? graph->masterGain
                                                                                                 : masterMix.load (std::memory_order_relaxed)));

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

    // Resting, as Logic does when stopped: nothing playing or recording, nothing heard from an input, no notes
    // played in, no preview - and all that came out has been silent for two seconds (every tail rung out). Then the
    // tracks and their plugins aren't processed at all: the audio thread does next to nothing until something changes.
    if (graph != restGraphSeen)
    {
        restGraphSeen = graph;
        quietSamples = 0;   // (a change in the song: processed again until it's silent once more)
        restHearsInput = false;
        if (graph != nullptr)
            for (const auto& t : graph->tracks)
                restHearsInput = restHearsInput || t.monitorInput || t.armed || t.inputTrackSlot >= 0 || t.busHardwareInput;
    }
    {
        const bool rest = graph != nullptr && ! isPlayingNow && ! isRecordingNow && ! restHearsInput && liveMidi.isEmpty()
                          && ! previewPlaying.load (std::memory_order_relaxed) && ! bouncing.load (std::memory_order_relaxed)
                          && screenTap.load (std::memory_order_acquire) == nullptr
                          && quietSamples >= (juce::int64) (2.0 * rateNow);
        resting.store (rest, std::memory_order_relaxed);
        if (rest)
        {
            suspendedNow.store (0, std::memory_order_relaxed);
            callbackCounter.fetch_add (1, std::memory_order_release);
            return;
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
    if (! ahead && aheadLeadSeconds.load (std::memory_order_relaxed) > 0.0)
        aheadLeadSeconds.store (0.0, std::memory_order_relaxed);   // (all live again: plugins' meters as they report)

    while (offset < numSamples)
    {
        int n = juce::jmin (numSamples - offset, mixBuffer.getNumSamples());

        // Rendering ahead: this piece is (part of) the next chunk from the ring, at its place in the song
        AheadChunk* chunk = nullptr;
        if (ahead)
        {
            // (waiting for it no later than near the device's deadline: past that, silence rather than a hang)
            const auto deadline = aheadWaitForever.load (std::memory_order_relaxed)
                                      ? std::numeric_limits<double>::max()
                                      : callbackStart + 1000.0 * 0.8 * (double) numSamples / juce::jmax (1.0, rateNow);
            chunk = aheadFront (deadline);
            if (chunk == nullptr)
            {
                // Not ready in time: the song waits a block (nothing is lost, it goes on from here next time).
                // Just after a start or a locate that's the first chunk being made, not an overload.
                if (! aheadPrefetching)
                    overloads.fetch_add (1, std::memory_order_relaxed);
                break;
            }
            aheadPrefetching = false;
            n = juce::jmin (n, chunk->n - chunk->read);
            if (chunk->read == 0)
            {
                for (size_t k = 0; k < chunk->peaks.size(); ++k)   // (its tracks' levels, now that it's heard)
                    if (chunk->peaks[k] > 0.0f)
                        storeMax (trackPeaks[k], chunk->peaks[k]);
                // (which tracks came from it, and how far ahead its thread is: their plugins' meters wait that long)
                for (size_t t = 0; t < slotRenderedAhead.size(); ++t)
                    slotRenderedAhead[t].store (chunk->peaks[2 * t] > 0.0f || chunk->peaks[2 * t + 1] > 0.0f, std::memory_order_relaxed);
                const auto queued = aheadWritten.load (std::memory_order_relaxed) - aheadRead.load (std::memory_order_relaxed);
                aheadLeadSeconds.store ((double) queued * (double) chunk->n / juce::jmax (1.0, rateNow), std::memory_order_relaxed);
            }
            playheadSample.store (chunk->playing ? chunk->position + chunk->read : chunk->position);   // (stopped: it stays put)
            isPlayingNow = chunk->playing;
        }
        const auto position = playheadSample.load();

        // Split the chunk at the cycle end and at the record-from point, so
        // both happen on exactly the right sample. (Rendering ahead, the ring's chunks already end at the cycle end.)
        if (! ahead && isPlayingNow && cycling && position < graph->cycleEnd)
            n = (int) juce::jmin ((juce::int64) n, graph->cycleEnd - position);

        if (isPlayingNow && isRecordingNow && position < recordStart)
            n = (int) juce::jmin ((juce::int64) n, recordStart - position);

        auto* mixL = mixBuffer.getWritePointer (0);
        auto* mixR = mixBuffer.getWritePointer (1);
        juce::FloatVectorOperations::clear (mixL, n);
        juce::FloatVectorOperations::clear (mixR, n);

        const bool recordingChunk = isRecordingNow && isPlayingNow && position >= recordStart;
        const bool toHardware = graph != nullptr && graph->anyHardwareOut && ! bouncing.load (std::memory_order_relaxed);
        if (toHardware)
            hardwareBuffer.clear (0, n);
        auto& pass = ahead ? livePass : mainPass;
        pass.position = position;
        pass.playing = isPlayingNow;
        if (graph != nullptr)
            printInputs (*graph, pass, inputs, numInputs, offset, n);   // (armed tracks' printed inserts, before recording)
        if (recordingChunk)
            recordChunk (inputs, numInputs, offset, n);

        // Any jump (start, stop, locate, cycle wrap) silences notes from regions,
        // so nothing hangs on.
        const bool resetNotes = isPlayingNow != wasPlaying || (isPlayingNow && position != expectedPosition);

        if (graph != nullptr)
        {
            if (ahead)
            {
                // The live tracks here, with what the ahead tracks sent their buses; then the ahead tracks' mix
                livePass.slots = &chunk->live;
                livePass.exclude = false;
                livePass.busPreload.fill (nullptr);
                for (int k = 0; k < aheadBusCarry; ++k)
                    if (const auto bus = chunk->carriedBus[(size_t) k]; juce::isPositiveAndBelow (bus, PlaybackGraph::maxBuses))
                    {
                        livePass.busPreload[(size_t) (bus * 2)] = chunk->audio.getReadPointer (2 + 2 * k, chunk->read);
                        livePass.busPreload[(size_t) (bus * 2 + 1)] = chunk->audio.getReadPointer (3 + 2 * k, chunk->read);
                    }
                renderChunk (*graph, livePass, inputs, numInputs, offset, mixL, mixR, n, resetNotes, liveMidiPending);
                juce::FloatVectorOperations::add (mixL, chunk->audio.getReadPointer (0, chunk->read), n);
                juce::FloatVectorOperations::add (mixR, chunk->audio.getReadPointer (1, chunk->read), n);
            }
            else
            {
                mainPass.slots = nullptr;
                mainPass.busPreload.fill (nullptr);
                renderChunk (*graph, mainPass, inputs, numInputs, offset, mixL, mixR, n, resetNotes, liveMidiPending);
            }
            liveMidiPending = false;

            // Takes recording another track's output: what that track just played.
            if (recordingChunk)
                recordChunk (inputs, numInputs, offset, n, true);

            // Compare: the master's sum before its processing (as late as its plugins)
            const bool comparing = compare.load (std::memory_order_relaxed) && ! bouncing.load (std::memory_order_relaxed);
            const bool keepMasterDry = (comparing || masterCompareMix > 0.0f || graph->masterChainLatency > 0) && n <= masterDry.getNumSamples();
            if (keepMasterDry)
            {
                masterDry.copyFrom (0, 0, mixL, n);
                masterDry.copyFrom (1, 0, mixR, n);
                if (graph->masterChainLatency > 0)
                    masterDryDelay.process (masterDry.getWritePointer (0), masterDry.getWritePointer (1), n, graph->masterChainLatency);
            }

            // The master chain (e.g. a limiter) on the sum of every track; the click comes after it.
            for (const auto& effect : graph->masterInserts)
            {
                effect->process (mixL, mixR, n);
                effect->tapOutput (mixL, mixR, n);
            }

            // Then the Master Bus Compressor, before the master fader: the user's
            // own plugin in console mode, or the built-in one.
            if (! graph->busCompPlugins.empty())
            {
                const auto energy = [n] (const float* l, const float* r)
                {
                    float e = 0.0f;
                    for (int i = 0; i < n; ++i)
                        e += 0.5f * (l[i] * l[i] + r[i] * r[i]);
                    return e;
                };
                busPluginIn.store (busPluginIn.load (std::memory_order_relaxed) + energy (mixL, mixR), std::memory_order_relaxed);
                for (const auto& effect : graph->busCompPlugins)
                    effect->process (mixL, mixR, n);
                busPluginOut.store (busPluginOut.load (std::memory_order_relaxed) + energy (mixL, mixR), std::memory_order_relaxed);
                busPluginSamples.fetch_add (n, std::memory_order_release);
            }
            else
            {
                busComp.setSettings (graph->busComp);
                busComp.process (mixL, mixR, n);
            }

            // The master's tape machine
            if (graph->masterTape.enabled)
            {
                masterTape.setSettings (graph->masterTape);
                masterTape.process (mixL, mixR, n);
            }

            if (keepMasterDry && (comparing || masterCompareMix > 0.0f))
                masterCompareMix = blendCompare (mixL, mixR, masterDry.getReadPointer (0), masterDry.getReadPointer (1), n,
                                                 masterCompareMix, comparing, sampleRate.load());

            const bool countingIn = isRecordingNow && position < recordStart;
            if (isPlayingNow && (graph->metronome || countingIn) && ! bouncing)
                renderClick (*graph, mixL, mixR, n);
        }

        if (previewPlaying.load (std::memory_order_relaxed) && ! bouncing)
            renderPreview (mixL, mixR, n);

        if (isPlayingNow)
        {
            const auto next = position + n;

            // Wrap at the cycle end. Only when we arrive at it from inside, so the
            // playhead can still be parked after the cycle and play on from there.
            // (Rendering ahead, the next chunk says where the song goes on.)
            if (ahead)
                playheadSample.store (next);
            else if (cycling && position < graph->cycleEnd && next >= graph->cycleEnd)
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

        // Channels sent to the interface's other outputs (they don't go through the master)
        if (toHardware)
            for (int ch = 0; ch < maxHardwareOutputs; ++ch)
                if (const auto slot = outputSlots[(size_t) ch]; slot >= 0 && slot < numOutputs && outputs[slot] != nullptr)
                    juce::FloatVectorOperations::add (outputs[slot] + offset, hardwareBuffer.getReadPointer (ch), n);

        if (auto* tap = screenTap.load (std::memory_order_acquire); tap != nullptr && ! bouncing.load())
            tap->push (mixL, mixR, n, inputs, numInputs, offset);

        // The MultiMeter's copy of what's heard
        if (masterTapOn.load (std::memory_order_relaxed) && ! bouncing.load())
        {
            const auto scope = masterFifo.write (juce::jmin (n, masterFifo.getFreeSpace()));
            int read = 0;
            for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
            {
                std::copy_n (mixL + read, size, masterTapL.data() + start);
                std::copy_n (mixR + read, size, masterTapR.data() + start);
                read += size;
            }
        }

        const auto outL = mixBuffer.getMagnitude (0, 0, n), outR = mixBuffer.getMagnitude (1, 0, n);
        storeMax (peakOutL, outL);
        storeMax (peakOutR, outR);
        quietSamples = juce::jmax (outL, outR) < 1.0e-5f ? quietSamples + n : 0;   // (-100 dB: silent, for resting)

        if (chunk != nullptr)
        {
            chunk->read += n;
            if (chunk->read >= chunk->n)
            {
                aheadRead.fetch_add (1, std::memory_order_acq_rel);   // (its slot is free for the ahead thread)
                aheadThread->wake();
            }
        }

        offset += n;
    }

    suspendedNow.store (suspendedCount.exchange (0, std::memory_order_relaxed), std::memory_order_relaxed);
    callbackCounter.fetch_add (1, std::memory_order_release);
}

void AudioEngine::recordChunk (const float* const* inputs, int numInputs, int inputOffset, int n, bool fromTracks) noexcept
{
    const auto blockStart = playheadSample.load();

    for (auto& slot : takeSlots)
    {
        auto* take = slot.load (std::memory_order_acquire);
        if (take == nullptr)
            continue;

        if ((take->sourceSlot >= 0) != fromTracks)
            continue;   // (inputs before the tracks play, other tracks' outputs after)

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
            if (take->sourceSlot >= 0)
                channels[c] = take->numChannels == 1 ? slotOutput (take->sourceSlot, 0) : slotOutput (take->sourceSlot, c);
            else if (juce::isPositiveAndBelow (take->printSlot, PlaybackGraph::maxTracks) && slotInputReady[(size_t) take->printSlot])
                channels[c] = slotInputs.getReadPointer (take->printSlot * 2 + (take->numChannels == 1 ? 0 : c));   // through its printed inserts
            else
                channels[c] = (source < numInputs && inputs[source] != nullptr) ? inputs[source] + inputOffset
                                                                                 : silence.getReadPointer (c);
        }

        juce::int64 expected = -1;
        take->startSample.compare_exchange_strong (expected, blockStart - take->printLatency);   // (printed plugins' latency taken back)

        // The live waveform and clip watch: the loudest sample in each block of 512.
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < take->numChannels; ++c)
                v = juce::jmax (v, std::abs (channels[c][i]));
            take->peakNow = juce::jmax (take->peakNow, v);
            if (v >= 0.999f && ! take->clipped.load (std::memory_order_relaxed))
            {
                take->clipped.store (true);
                recordClipped.store (true);
            }
            if (++take->peakFill >= peakBlock)
            {
                if (const auto count = take->peakCount.load (std::memory_order_relaxed); count < Take::maxPeaks)
                {
                    take->peaks[(size_t) count].store ((juce::uint8) juce::jlimit (0, 255, (int) (take->peakNow * 255.0f)), std::memory_order_relaxed);
                    take->peakCount.store (count + 1, std::memory_order_release);
                }
                take->peakNow = 0.0f;
                take->peakFill = 0;
            }
        }

        if (take->writer->write (channels, n))
            take->samplesWritten.fetch_add (n);
        else
            recordOverflow.store (true); // disk too slow: this block is lost
    }
}

void AudioEngine::printInputs (const PlaybackGraph& graph, Pass& pass, const float* const* inputs, int numInputs, int inputOffset, int n) noexcept
{
    slotInputReady.fill (false);
    if (n > slotInputs.getNumSamples())
        return;
    for (const auto& track : graph.tracks)
    {
        if (! track.armed || track.printInserts.empty() || ! juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks)
            || track.inputTrackSlot >= 0 || track.isInstrument || track.isBus)
            continue;
        auto* l = slotInputs.getWritePointer (track.slot * 2);
        auto* r = slotInputs.getWritePointer (track.slot * 2 + 1);
        const auto first = track.inputChannel;
        const auto second = track.inputStereo ? first + 1 : first;
        if (first < numInputs && inputs != nullptr && inputs[first] != nullptr)
            juce::FloatVectorOperations::copy (l, inputs[first] + inputOffset, n);
        else
            juce::FloatVectorOperations::clear (l, n);
        if (second < numInputs && inputs != nullptr && inputs[second] != nullptr)
            juce::FloatVectorOperations::copy (r, inputs[second] + inputOffset, n);
        else
            juce::FloatVectorOperations::copy (r, l, n);
        for (const auto& effect : track.printInserts)
        {
            effect->setPosition (pass.position, pass.playing, pass.bpm);
            effect->process (l, r, n);
            effect->tapOutput (l, r, n);
        }
        slotInputReady[(size_t) track.slot] = true;
    }
}

void AudioEngine::collectNotes (const PlaybackGraph::Track& track, juce::int64 from, juce::int64 to, juce::MidiBuffer& trackMidi) noexcept
{
    // Controllers and pitch bend.
    for (const auto& region : track.regions)
        for (const auto& event : region.events)
        {
            if (event.time >= to)
                break;
            if (event.time >= from)
            {
                const juce::uint8 bytes[3] = { event.status, event.data1, event.data2 };
                trackMidi.addEvent (bytes, 3, (int) (event.time - from));
            }
        }

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

void AudioEngine::renderChunk (const PlaybackGraph& graph, Pass& pass, const float* const* inputs, int numInputs, int inputOffset,
                               float* mixL, float* mixR, int n, bool resetNotes, bool withLiveMidi) noexcept
{
    // The song's tempo here, for the plugins (their host playhead is the pass's position)
    {
        const auto rate = sampleRate.load();
        pass.bpm = rate > 0.0 ? graph.tempo.bpmAt ((double) pass.position / rate) : 120.0;
    }

    // Routing: ordinary tracks sum into `direct` and feed the bus buffers through
    // their sends; then each bus processes what it received; both reach the mix.
    auto* dL = pass.direct.getWritePointer (0);
    auto* dR = pass.direct.getWritePointer (1);
    juce::FloatVectorOperations::clear (dL, n);
    juce::FloatVectorOperations::clear (dR, n);

    for (const auto& track : graph.tracks)
    {
        if (track.isBus && juce::isPositiveAndBelow (track.busIndex, PlaybackGraph::maxBuses))
        {
            for (int c = 0; c < 2; ++c)
            {
                auto* b = pass.bus.getWritePointer (track.busIndex * 2 + c);
                if (const auto* pre = pass.busPreload[(size_t) (track.busIndex * 2 + c)])
                    juce::FloatVectorOperations::copy (b, pre, n);   // (what the tracks rendered ahead sent it)
                else
                    juce::FloatVectorOperations::clear (b, n);
            }
        }
    }

    // Each track's time is counted for the Performance meter's track list.
    const auto timed = [&] (const PlaybackGraph::Track& track, float* l, float* r)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        renderTrack (track, pass, inputs, numInputs, inputOffset, l, r, n, resetNotes, withLiveMidi);
        if (juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks))
        {
            trackCost[(size_t) track.slot].store ((float) juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), std::memory_order_relaxed);
            trackTicks[(size_t) track.slot].fetch_add (juce::Time::getHighResolutionTicks() - t0, std::memory_order_relaxed);
            trackSamples[(size_t) track.slot].fetch_add (n, std::memory_order_relaxed);
        }
    };

    // The tracks: on several cores when there are a few that don't depend on each other (each processed through
    // its fader in parallel, then routed in the song's order, so the mix is exactly what one core would make)
    pass.parallel.clear();
    if (pass.usePool && renderThreads.workers() > 0 && scratches.size() > 1)
        for (int i = 0; i < (int) graph.tracks.size() && i < (int) pass.routeAfter.size(); ++i)
        {
            const auto& track = graph.tracks[(size_t) i];
            pass.routeAfter[(size_t) i] = false;
            pass.inParallel[(size_t) i] = false;
            if (! track.isBus && pass.plays (track.slot) && canRunInParallel (track) && (int) pass.parallel.size() < (int) pass.parallel.capacity())
            {
                pass.parallel.push_back (i);
                pass.inParallel[(size_t) i] = true;
            }
        }

    // The heaviest tracks first (by what each took last time), so no core is left with a big one at the end
    if (pass.parallel.size() >= 3)
    {
        const auto cost = [&] (int i) { const auto slot = graph.tracks[(size_t) i].slot;
                                        return juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks) ? trackCost[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; };
        for (size_t k = 1; k < pass.parallel.size(); ++k)
        {
            const auto v = pass.parallel[k];
            const auto c = cost (v);
            auto j = k;
            while (j > 0 && cost (pass.parallel[j - 1]) < c) { pass.parallel[j] = pass.parallel[j - 1]; --j; }
            pass.parallel[j] = v;
        }
    }

    if (pass.parallel.size() >= 2)
    {
        struct Context
        {
            AudioEngine* engine;
            Pass* pass;
            const PlaybackGraph* graph;
            const float* const* inputs;
            int numInputs, inputOffset, n;
            bool resetNotes, withLiveMidi;
        } context { this, &pass, &graph, inputs, numInputs, inputOffset, n, resetNotes, withLiveMidi };

        // (the heaviest first, so the cores finish together)
        renderThreads.run ((int) pass.parallel.size(), [] (void* p, int job, int thread) noexcept
        {
            auto& x = *static_cast<Context*> (p);
            auto& e = *x.engine;
            const auto index = x.pass->parallel[(size_t) job];
            const auto& track = x.graph->tracks[(size_t) index];
            const auto t0 = juce::Time::getHighResolutionTicks();
            auto& scratch = thread == 0 ? *x.pass->scratch : *e.scratches[(size_t) juce::jmin (thread, (int) e.scratches.size() - 1)];
            x.pass->routeAfter[(size_t) index] = e.processTrack (track, *x.pass, x.inputs, x.numInputs, x.inputOffset, x.n, x.resetNotes,
                                                                 x.withLiveMidi, scratch);
            if (juce::isPositiveAndBelow (track.slot, PlaybackGraph::maxTracks))
            {
                const auto ticks = juce::Time::getHighResolutionTicks() - t0;
                e.trackTicks[(size_t) track.slot].fetch_add (ticks, std::memory_order_relaxed);
                e.trackSamples[(size_t) track.slot].fetch_add (x.n, std::memory_order_relaxed);
                e.trackCost[(size_t) track.slot].store ((float) juce::Time::highResolutionTicksToSeconds (ticks), std::memory_order_relaxed);
            }
        }, &context);

        for (int i = 0; i < (int) graph.tracks.size(); ++i)
        {
            const auto& track = graph.tracks[(size_t) i];
            if (track.isBus || ! pass.plays (track.slot))
                continue;
            if (pass.inParallel[(size_t) i])
            {
                if (pass.routeAfter[(size_t) i])
                    routeTrack (track, pass, inputOffset, dL, dR, n);
            }
            else
                timed (track, dL, dR);
        }
    }
    else
        for (const auto& track : graph.tracks)
            if (! track.isBus && pass.plays (track.slot))
                timed (track, dL, dR);

    // Tracks heard directly wait for the slowest bus (delay compensation).
    if (graph.directDelay > 0)
        pass.directDelay->process (dL, dR, n, graph.directDelay);

    juce::FloatVectorOperations::add (mixL, dL, n);
    juce::FloatVectorOperations::add (mixR, dR, n);

    if (! graph.busOrder.empty())
    {
        for (const auto index : graph.busOrder)
            if (juce::isPositiveAndBelow (index, (int) graph.tracks.size()) && pass.plays (graph.tracks[(size_t) index].slot))
                timed (graph.tracks[(size_t) index], mixL, mixR);
    }
    else
        for (const auto& track : graph.tracks)
            if (track.isBus && pass.plays (track.slot))
                timed (track, mixL, mixR);
}

void AudioEngine::renderTrack (const PlaybackGraph::Track& track, Pass& pass, const float* const* inputs, int numInputs, int inputOffset,
                               float* outL, float* outR, int n, bool resetNotes, bool withLiveMidi) noexcept
{
    if (processTrack (track, pass, inputs, numInputs, inputOffset, n, resetNotes, withLiveMidi, *pass.scratch))
        routeTrack (track, pass, inputOffset, outL, outR, n);
}

float AudioEngine::blendCompare (float* wetL, float* wetR, const float* dryL, const float* dryR, int n, float mix, bool toDry, double rate) noexcept
{
    const auto step = (float) (1.0 / std::max (1.0, 0.02 * rate)) * (toDry ? 1.0f : -1.0f);
    if ((toDry && mix >= 1.0f) || (! toDry && mix <= 0.0f))
    {
        if (toDry)
        {
            juce::FloatVectorOperations::copy (wetL, dryL, n);
            juce::FloatVectorOperations::copy (wetR, dryR, n);
        }
        return toDry ? 1.0f : 0.0f;
    }
    for (int i = 0; i < n; ++i)
    {
        mix = juce::jlimit (0.0f, 1.0f, mix + step);
        wetL[i] += (dryL[i] - wetL[i]) * mix;
        wetR[i] += (dryR[i] - wetR[i]) * mix;
    }
    return mix;
}

bool AudioEngine::canRunInParallel (const PlaybackGraph::Track& track) noexcept
{
    // Not a bus (they wait for the tracks' sends), and not listening to another track this block
    if (track.isBus || (track.monitorInput && track.inputTrackSlot >= 0 && track.inputTrackSlot != track.slot))
        return false;
    for (const auto& s : track.sidechains)
        if (s.sourceSlot != track.slot)
            return false;
    return true;
}

bool AudioEngine::processTrack (const PlaybackGraph::Track& track, Pass& pass, const float* const* inputs, int numInputs, int inputOffset,
                                int n, bool resetNotes, bool withLiveMidi, Scratch& scratch) noexcept
{
    if (track.slot < 0 || track.slot >= PlaybackGraph::maxTracks)
        return false;

    const bool playingNow = pass.playing;
    const auto blockStart = pass.position;
    const auto blockEnd = blockStart + n;
    auto* tL = scratch.track.getWritePointer (0);
    auto* tR = scratch.track.getWritePointer (1);
    auto& trackMidi = scratch.midi;
    auto& state = trackStates[(size_t) track.slot];
    auto clearSlotOutput = [this, &track, n]
    {
        if (track.slot * 2 + 1 < slotOutputs.getNumChannels() && n <= slotOutputs.getNumSamples())
        {
            juce::FloatVectorOperations::clear (slotOutputs.getWritePointer (track.slot * 2), n);
            juce::FloatVectorOperations::clear (slotOutputs.getWritePointer (track.slot * 2 + 1), n);
        }
    };

    // Automation (read once per chunk; the gain smoothing below hides the steps).
    // A control being written as automation right now wins over the curve.
    const auto volumeLive = volumeOverride[(size_t) track.slot].load (std::memory_order_relaxed);
    const auto panLive = panOverride[(size_t) track.slot].load (std::memory_order_relaxed);
    const auto volumeMoved = volumeMix[(size_t) track.slot].load (std::memory_order_relaxed);
    const auto panMoved = panMix[(size_t) track.slot].load (std::memory_order_relaxed);
    const auto volume = ! std::isnan (volumeLive) ? volumeLive
                      : (playingNow && ! track.volumeAutomation.empty()) ? track.volumeAutomation.valueAt (blockStart)
                      : ! std::isnan (volumeMoved) ? volumeMoved : track.volume;
    const auto pan = ! std::isnan (panLive) ? panLive
                   : (playingNow && ! track.panAutomation.empty()) ? track.panAutomation.valueAt (blockStart)
                   : ! std::isnan (panMoved) ? panMoved : track.pan;
    const auto gain = volume * track.vcaGain;

    if (playingNow)
        for (const auto& automation : track.paramAutomation)
            automation.effect->setAutomatedParameter (automation.paramIndex, automation.curve.valueAt (blockStart));

    // Balance-style pan: unity at centre, the far side fades out.
    const auto targetL = track.audible ? gain * (pan <= 0.0f ? 1.0f : 1.0f - pan) : 0.0f;
    const auto targetR = track.audible ? gain * (pan >= 0.0f ? 1.0f : 1.0f + pan) : 0.0f;

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
            juce::FloatVectorOperations::copy (tL, pass.bus.getReadPointer (track.busIndex * 2), n);
            juce::FloatVectorOperations::copy (tR, pass.bus.getReadPointer (track.busIndex * 2 + 1), n);
            anyAudio = true;
        }
        // ...and an interface input, when it takes one (Pro Tools' aux input)
        if (track.busHardwareInput && track.inputChannel < numInputs && inputs[track.inputChannel] != nullptr)
        {
            const auto* inL = inputs[track.inputChannel] + inputOffset;
            const auto rightIndex = track.inputStereo ? track.inputChannel + 1 : track.inputChannel;
            const auto* inR = (rightIndex < numInputs && inputs[rightIndex] != nullptr) ? inputs[rightIndex] + inputOffset : inL;
            juce::FloatVectorOperations::add (tL, inL, n);
            juce::FloatVectorOperations::add (tR, inR, n);
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
                collectNotes (track, blockStart, blockEnd, trackMidi);

            if (withLiveMidi && track.receivesLiveMidi)
                trackMidi.addEvents (liveMidi, 0, -1, 0);

            track.instrument->setPosition (pass.position, pass.playing, pass.bpm);
            track.instrument->render (tL, tR, n, trackMidi);
            anyAudio = true;
        }
    }
    else
    {
        // Silent and not fading, with no effect tails or sends to keep running: nothing to do.
        if (! hasInserts && track.sends.empty() && ! state.gainL.isSmoothing() && ! state.gainR.isSmoothing()
            && state.gainL.getCurrentValue() <= 0.0f && state.gainR.getCurrentValue() <= 0.0f)
        {
            clearSlotOutput();
            return false;
        }

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

                const auto& audio = *region.audio;
                const auto& source = audio.buffer;
                const auto destOffset = (int) (from - blockStart);
                const auto count = (int) (to - from);

                // Fades: render into a scratch buffer, shape it, then add it in.
                const auto positionInRegion = from - region.start;
                const bool fading = region.hasFades()
                                    && (positionInRegion < region.fadeIn || (to - region.start) > region.length - region.fadeOut);
                float* dL = tL + destOffset;
                float* dR = tR + destOffset;
                if (fading)
                {
                    dL = scratch.region.getWritePointer (0);
                    dR = scratch.region.getWritePointer (1);
                    juce::FloatVectorOperations::clear (dL, count);
                    juce::FloatVectorOperations::clear (dR, count);
                }

                bool rendered = false;
                if (region.resample)
                {
                    // Another sample rate: convert on the fly, reading at fractional positions.
                    const auto position = region.sourcePosition + (double) positionInRegion * region.step;
                    if (position < (double) audio.numFrames())
                    {
                        if (audio.isStreamed())
                        {
                            // From disk: the stretch the filter needs, into a scratch buffer, then the same conversion.
                            const auto base = (juce::int64) std::floor (position) - resampler::halfWidth - 1;
                            const auto needed = (int) juce::jmin ((juce::int64) scratch.stream.getNumSamples(),
                                                                  (juce::int64) std::ceil (count * region.step) + 2 * resampler::halfWidth + 4);
                            audio.readStereo (base, needed, scratch.stream.getWritePointer (0), scratch.stream.getWritePointer (1));
                            const juce::AudioBuffer<float> window (scratch.stream.getArrayOfWritePointers(), 2, needed);
                            resampler::addResampled (window, position - (double) base, region.step, region.gain, dL, dR, count);
                        }
                        else
                        {
                            resampler::addResampled (source, position, region.step, region.gain, dL, dR, count);
                        }
                        rendered = true;
                    }
                }
                else
                {
                    const auto sourceStart = region.sourceOffset + positionInRegion;
                    const auto available = audio.numFrames() - sourceStart;

                    if (sourceStart >= 0 && available > 0)
                    {
                        const auto num = (int) juce::jmin ((juce::int64) count, available);
                        if (audio.isStreamed())
                        {
                            // From disk, straight from the mapped file.
                            auto* sL = scratch.stream.getWritePointer (0);
                            auto* sR = scratch.stream.getWritePointer (1);
                            audio.readStereo (sourceStart, num, sL, sR);
                            juce::FloatVectorOperations::addWithMultiply (dL, sL, region.gain, num);
                            juce::FloatVectorOperations::addWithMultiply (dR, sR, region.gain, num);
                        }
                        else
                        {
                            juce::FloatVectorOperations::addWithMultiply (dL, source.getReadPointer (0, (int) sourceStart), region.gain, num);
                            juce::FloatVectorOperations::addWithMultiply (dR, source.getReadPointer (1, (int) sourceStart), region.gain, num);
                        }
                        rendered = true;
                    }
                }

                if (! rendered)
                    continue;

                if (fading)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        const auto g = region.fadeGainAt (positionInRegion + i);
                        tL[destOffset + i] += dL[i] * g;
                        tR[destOffset + i] += dR[i] * g;
                    }
                }
                anyAudio = true;
            }
        }

        // Input monitoring: hear what's coming in, through this track's volume and pan.
        if (track.monitorInput && track.inputTrackSlot >= 0 && track.inputTrackSlot != track.slot)
        {
            // ...another track's output, as its input
            juce::FloatVectorOperations::add (tL, slotOutput (track.inputTrackSlot, 0), n);
            juce::FloatVectorOperations::add (tR, slotOutput (track.inputTrackSlot, track.inputStereo ? 1 : 0), n);
            anyAudio = true;
        }
        else if (track.monitorInput && slotInputReady[(size_t) track.slot])
        {
            // ...through its printed inserts (heard as it will be recorded)
            juce::FloatVectorOperations::add (tL, slotInputs.getReadPointer (track.slot * 2), n);
            juce::FloatVectorOperations::add (tR, slotInputs.getReadPointer (track.slot * 2 + 1), n);
            anyAudio = true;
        }
        else if (track.monitorInput && track.inputChannel < numInputs && inputs[track.inputChannel] != nullptr)
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
    const auto peakOf = [n] (const float* l, const float* r)
    {
        const auto a = juce::FloatVectorOperations::findMinAndMax (l, n), b = juce::FloatVectorOperations::findMinAndMax (r, n);
        return juce::jmax (std::abs (a.getStart()), std::abs (a.getEnd()), std::abs (b.getStart()), std::abs (b.getEnd()));
    };
    const auto inputPeak = peakOf (tL, tR);
    storeMax (stripInPeaks[(size_t) track.slot], inputPeak);

    // Suspended: nothing has come in, and nothing has come out of the plug-ins for a second (tails rung out),
    // so they rest until sound comes again (it wakes them in the same block).
    constexpr float silent = 1.0e-5f;   // -100 dB
    const auto rate = sampleRate.load();
    const bool suspended = suspendWhenSilent.load (std::memory_order_relaxed) && ! bouncing.load (std::memory_order_relaxed)
                           && hasInserts && inputPeak < silent && state.quietFor >= (juce::int64) rate;
    if (suspended)
        suspendedCount.fetch_add (1, std::memory_order_relaxed);

    // Compare: the sound before any of its processing, kept (as late as its plugins make the rest)
    const bool comparing = compare.load (std::memory_order_relaxed) && ! bouncing.load (std::memory_order_relaxed);   // (never in a bounce)
    const bool keepDry = (comparing || state.compareMix > 0.0f || track.chainLatency > 0) && n <= scratch.dry.getNumSamples();
    auto* dryL = scratch.dry.getWritePointer (0);
    auto* dryR = scratch.dry.getWritePointer (1);
    if (keepDry)
    {
        juce::FloatVectorOperations::copy (dryL, tL, n);
        juce::FloatVectorOperations::copy (dryR, tR, n);
        if (track.chainLatency > 0)
            if (auto* line = dryDelays[(size_t) track.slot].get())
                line->process (dryL, dryR, n, track.chainLatency);
    }

    // The analog preamp: first in the channel, its gain and colour
    if (track.preamp.enabled && ! suspended)
    {
        auto& pre = preamps[(size_t) track.slot];
        pre.setSettings (track.preamp);
        pre.process (tL, tR, n);
    }

    const auto runInserts = [&]
    {
        for (const auto& effect : track.inserts)
        {
            if (suspended)
                break;
            effect->setPosition (pass.position, pass.playing, pass.bpm);
            const PlaybackGraph::Track::Sidechain* side = nullptr;
            for (const auto& s : track.sidechains)
                if (s.effect == effect.get())
                    side = &s;
            if (side != nullptr && side->sourceSlot != track.slot)
                effect->setSidechain (slotOutput (side->sourceSlot, 0), slotOutput (side->sourceSlot, 1));
            effect->process (tL, tR, n);
            effect->tapOutput (tL, tR, n);   // (for its window's analyzer, while one is open)
            if (side != nullptr)
                effect->setSidechain (nullptr, nullptr);
        }
    };
    if (track.insertsBeforeStrip)
        runInserts();

    if (! suspended)
        for (const auto& effect : track.stripPlugins)
        {
            effect->setPosition (pass.position, pass.playing, pass.bpm);
            effect->process (tL, tR, n);
            effect->tapOutput (tL, tR, n);
        }

    if (track.console.isActive())
    {
        auto& console = consoles[(size_t) track.slot];
        console.setSettings (track.console);
        // (its EXT key: another track's sound, this block)
        const float* keyL = nullptr;
        const float* keyR = nullptr;
        for (const auto& side : track.sidechains)
            if (side.effect == nullptr && side.sourceSlot != track.slot && juce::isPositiveAndBelow (side.sourceSlot, PlaybackGraph::maxTracks))
            {
                keyL = slotOutput (side.sourceSlot, 0);
                keyR = slotOutput (side.sourceSlot, 1);
            }
        console.process (tL, tR, n, keyL, keyR);
    }
    else
        consoles[(size_t) track.slot].reset();

    storeMax (stripOutPeaks[(size_t) track.slot], peakOf (tL, tR));

    // A strip window's MultiMeter: the track's sound after its strip, in stereo
    if (track.slot == stripTapSlot.load (std::memory_order_relaxed))
    {
        const auto scope = stripTapFifo.write (juce::jmin (n, stripTapFifo.getFreeSpace()));
        std::copy_n (tL, scope.blockSize1, stripTapL.data() + scope.startIndex1);
        std::copy_n (tR, scope.blockSize1, stripTapR.data() + scope.startIndex1);
        std::copy_n (tL + scope.blockSize1, scope.blockSize2, stripTapL.data() + scope.startIndex2);
        std::copy_n (tR + scope.blockSize1, scope.blockSize2, stripTapR.data() + scope.startIndex2);
    }

    // The spectrum analyzer's track: its sound after the EQ, in mono.
    if (track.slot == analyzerSlot.load (std::memory_order_relaxed))
    {
        const auto scope = analyzerFifo.write (juce::jmin (n, analyzerFifo.getFreeSpace()));
        for (int i = 0; i < scope.blockSize1; ++i)
            analyzerBuffer[(size_t) (scope.startIndex1 + i)] = 0.5f * (tL[i] + tR[i]);
        for (int i = 0; i < scope.blockSize2; ++i)
            analyzerBuffer[(size_t) (scope.startIndex2 + i)] = 0.5f * (tL[scope.blockSize1 + i] + tR[scope.blockSize1 + i]);
    }

    if (! track.insertsBeforeStrip)
        runInserts();

    // Tape saturation: the channel (or bus) through its tape machine
    if (track.tape.enabled && ! suspended)
    {
        auto& tape = tapes[(size_t) track.slot];
        tape.setSettings (track.tape);
        tape.process (tL, tR, n);
    }

    if (keepDry && (comparing || state.compareMix > 0.0f))
        state.compareMix = blendCompare (tL, tR, dryL, dryR, n, state.compareMix, comparing, rate);

    // How long it's been quiet in and out (the plug-ins rest after a second of it)
    if (! suspended)
        state.quietFor = (inputPeak < silent && peakOf (tL, tR) < silent) ? state.quietFor + n : 0;

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
        clearSlotOutput();
        return false;
    }

    // Its sound before the fader, kept for pre-fader sends (and stems bounced before the fader)
    bool needsPreFader = stemCapture != nullptr && stemBeforeFader;
    for (const auto& send : track.sends)
        needsPreFader = needsPreFader || (send.preFader && track.audible);
    preFaderReady[(size_t) track.slot] = needsPreFader && track.slot * 2 + 1 < preFaderOutputs.getNumChannels() && n <= preFaderOutputs.getNumSamples();
    if (preFaderReady[(size_t) track.slot])
    {
        juce::FloatVectorOperations::copy (preFaderOutputs.getWritePointer (track.slot * 2), tL, n);
        juce::FloatVectorOperations::copy (preFaderOutputs.getWritePointer (track.slot * 2 + 1), tR, n);
    }

    // Stereo width (Stereo Pan mode): narrower towards mono, or wider.
    if (std::abs (track.width - 1.0f) > 1.0e-3f)
        for (int i = 0; i < n; ++i)
        {
            const auto mid = (tL[i] + tR[i]) * 0.5f, side = (tL[i] - tR[i]) * 0.5f * track.width;
            tL[i] = mid + side;
            tR[i] = mid - side;
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

    // Kept for side chains and tracks recording this one's output.
    if (track.slot * 2 + 1 < slotOutputs.getNumChannels() && n <= slotOutputs.getNumSamples())
    {
        juce::FloatVectorOperations::copy (slotOutputs.getWritePointer (track.slot * 2), tL, n);
        juce::FloatVectorOperations::copy (slotOutputs.getWritePointer (track.slot * 2 + 1), tR, n);
    }

    if (auto* sink = pass.peakSink)
    {
        // (rendered ahead: kept with its chunk, to be shown when that's heard)
        sink[(size_t) track.slot * 2] = juce::jmax (sink[(size_t) track.slot * 2], peakL);
        sink[(size_t) track.slot * 2 + 1] = juce::jmax (sink[(size_t) track.slot * 2 + 1], peakR);
    }
    else
    {
        storeMax (trackPeaks[(size_t) track.slot * 2], peakL);
        storeMax (trackPeaks[(size_t) track.slot * 2 + 1], peakR);
    }
    return true;
}

void AudioEngine::routeTrack (const PlaybackGraph::Track& track, Pass& pass, int inputOffset, float* outL, float* outR, int n) noexcept
{
    if (track.slot < 0 || track.slot >= PlaybackGraph::maxTracks)
        return;
    const auto* tL = slotOutput (track.slot, 0);
    const auto* tR = slotOutput (track.slot, 1);
    const auto pre = preFaderReady[(size_t) track.slot];
    const auto* pL = pre ? preFaderOutputs.getReadPointer (track.slot * 2) : tL;
    const auto* pR = pre ? preFaderOutputs.getReadPointer (track.slot * 2 + 1) : tR;

    if (pre && stemCapture != nullptr && stemBeforeFader && track.slot * 2 + 1 < stemCapture->getNumChannels()
        && inputOffset + n <= stemCapture->getNumSamples())
    {
        juce::FloatVectorOperations::add (stemCapture->getWritePointer (track.slot * 2) + inputOffset, pL, n);
        juce::FloatVectorOperations::add (stemCapture->getWritePointer (track.slot * 2 + 1) + inputOffset, pR, n);
    }

    // Pre-fader sends (silenced with the track when it's muted).
    for (const auto& send : track.sends)
    {
        if (! pre || ! send.preFader || ! track.audible || ! juce::isPositiveAndBelow (send.busIndex, PlaybackGraph::maxBuses))
            continue;

        juce::FloatVectorOperations::addWithMultiply (pass.bus.getWritePointer (send.busIndex * 2), pL, send.level, n);
        juce::FloatVectorOperations::addWithMultiply (pass.bus.getWritePointer (send.busIndex * 2 + 1), pR, send.level, n);
    }

    // Post-fader sends.
    for (const auto& send : track.sends)
    {
        if (send.preFader || ! juce::isPositiveAndBelow (send.busIndex, PlaybackGraph::maxBuses))
            continue;

        juce::FloatVectorOperations::addWithMultiply (pass.bus.getWritePointer (send.busIndex * 2), tL, send.level, n);
        juce::FloatVectorOperations::addWithMultiply (pass.bus.getWritePointer (send.busIndex * 2 + 1), tR, send.level, n);
    }

    // Into a bus (a summing stack, or an output chosen on the strip), the interface's other outputs, or the main output.
    if (track.hardwareOut >= 0 && track.hardwareOut < maxHardwareOutputs && ! bouncing.load (std::memory_order_relaxed))
    {
        if (track.hardwareMono || track.hardwareOut + 1 >= maxHardwareOutputs)
        {
            auto* out = hardwareBuffer.getWritePointer (track.hardwareOut);
            for (int i = 0; i < n; ++i)
                out[i] += 0.5f * (tL[i] + tR[i]);
        }
        else
        {
            juce::FloatVectorOperations::add (hardwareBuffer.getWritePointer (track.hardwareOut), tL, n);
            juce::FloatVectorOperations::add (hardwareBuffer.getWritePointer (track.hardwareOut + 1), tR, n);
        }
    }
    else if (juce::isPositiveAndBelow (track.outputBus, PlaybackGraph::maxBuses) && track.outputBus != track.busIndex)
    {
        juce::FloatVectorOperations::add (pass.bus.getWritePointer (track.outputBus * 2), tL, n);
        juce::FloatVectorOperations::add (pass.bus.getWritePointer (track.outputBus * 2 + 1), tR, n);
    }
    else
    {
        juce::FloatVectorOperations::add (outL, tL, n);
        juce::FloatVectorOperations::add (outR, tR, n);
    }

    if (measuring && juce::isPositiveAndBelow (track.slot, (int) measuredTracks.size()))
        measuredTracks[(size_t) track.slot].add (tL, tR, n);

    if (stemCapture != nullptr && ! stemBeforeFader && track.slot * 2 + 1 < stemCapture->getNumChannels()
        && inputOffset + n <= stemCapture->getNumSamples())
    {
        juce::FloatVectorOperations::add (stemCapture->getWritePointer (track.slot * 2) + inputOffset, tL, n);
        juce::FloatVectorOperations::add (stemCapture->getWritePointer (track.slot * 2 + 1) + inputOffset, tR, n);
    }

}

void AudioEngine::renderClick (const PlaybackGraph& graph, float* mixL, float* mixR, int n) noexcept
{
    const auto rate = sampleRate.load();
    if (graph.secondsPerBeat * rate < 1.0)
        return;

    const auto blockStart = playheadSample.load();
    const auto& tempo = graph.tempo;
    const auto beatToSample = [&tempo, rate] (juce::int64 b)
    {
        // Count-in beats before 0 use the starting tempo.
        return (juce::int64) std::llround ((b >= 0 ? tempo.secondsAt ((double) b) : (double) b * tempo.segments.front().secondsPerBeat) * rate);
    };

    // The first beat at or after the start of this chunk (on the tempo track).
    const auto startSeconds = (double) blockStart / rate;
    const auto beatNow = startSeconds >= 0.0 ? tempo.beatAt (startSeconds) : startSeconds / tempo.segments.front().secondsPerBeat;
    auto beat = (juce::int64) std::ceil (beatNow - 1e-9);
    auto nextBeatSample = beatToSample (beat);

    for (int i = 0; i < n; ++i)
    {
        if (blockStart + i == nextBeatSample)
        {
            const bool downbeat = beat >= 0 ? tempo.barAndBeat ((double) beat).second < 1.0e-6
                                            : graph.beatsPerBar > 0 && ((beat % graph.beatsPerBar) + graph.beatsPerBar) % graph.beatsPerBar == 0;
            clickPhaseInc = juce::MathConstants<double>::twoPi * (downbeat ? 1600.0 : 1100.0) / rate;
            clickAmplitude = downbeat ? 0.5f : 0.32f;
            clickPhase = 0.0;
            clickSamplesLeft = clickLength;

            ++beat;
            nextBeatSample = beatToSample (beat);
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

//==============================================================================
// Render ahead (Logic's Process Buffer Range)
//==============================================================================
void AudioEngine::parkAhead()
{
    if (aheadThread == nullptr)
        return;
    aheadPark.store (true);
    aheadThread->wake();
    // (wait for it to finish the chunk it's on: then nothing but the caller plays the tracks)
    const auto until = juce::Time::getMillisecondCounter() + 2000;
    while (! aheadParked.load() && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (1);
}

void AudioEngine::unparkAhead()
{
    if (aheadThread == nullptr)
        return;
    aheadNeedsRestart.store (true);
    aheadPark.store (false);
    aheadThread->wake();
}

AudioEngine::SlotSet AudioEngine::liveSlotsOf (const PlaybackGraph& graph, SlotSet live, std::array<int, aheadBusCarry>& carried) const noexcept
{
    // Live: what you play or hear through right now - the track your keyboard plays, armed and monitored tracks,
    // tracks straight to the interface's outputs, another take's source - and what they need or feed.
    std::array<int, PlaybackGraph::maxBuses> busSlot;
    busSlot.fill (-1);
    for (const auto& t : graph.tracks)
    {
        if (! juce::isPositiveAndBelow (t.slot, PlaybackGraph::maxTracks))
            continue;
        if (t.isBus && juce::isPositiveAndBelow (t.busIndex, PlaybackGraph::maxBuses))
            busSlot[(size_t) t.busIndex] = t.slot;
        if (t.receivesLiveMidi || t.monitorInput || t.armed || t.hardwareOut >= 0 || t.inputTrackSlot >= 0 || t.busHardwareInput
            || recordSourceSlot[(size_t) t.slot].load (std::memory_order_relaxed))
            live.set ((size_t) t.slot);
    }

    const auto mark = [&] (int slot, bool& changed)
    {
        if (juce::isPositiveAndBelow (slot, PlaybackGraph::maxTracks) && ! live.test ((size_t) slot))
        {
            live.set ((size_t) slot);
            changed = true;
        }
    };
    const auto busOf = [&] (int bus) { return juce::isPositiveAndBelow (bus, PlaybackGraph::maxBuses) ? busSlot[(size_t) bus] : -1; };

    for (int round = 0; round < 64; ++round)
    {
        bool changed = false;
        for (const auto& t : graph.tracks)
        {
            if (! juce::isPositiveAndBelow (t.slot, PlaybackGraph::maxTracks))
                continue;
            const bool isLive = live.test ((size_t) t.slot);
            for (const auto& side : t.sidechains)
            {
                if (isLive) mark (side.sourceSlot, changed);                                         // its key, as it is now
                else if (juce::isPositiveAndBelow (side.sourceSlot, PlaybackGraph::maxTracks) && live.test ((size_t) side.sourceSlot))
                    mark (t.slot, changed);                                                         // keyed by a live track
            }
            if (! isLive)
                continue;
            mark (t.inputTrackSlot, changed);
            for (const auto& send : t.sends)
                mark (busOf (send.busIndex), changed);                                             // what it feeds
            if (t.outputBus != t.busIndex)
                mark (busOf (t.outputBus), changed);
        }

        // The live buses that ahead tracks also send to carry those sends in the chunk: at most aheadBusCarry of
        // them (past that, their senders play live too)
        if (! changed)
        {
            carried.fill (-1);
            int count = 0;
            for (int bus = 0; bus < PlaybackGraph::maxBuses; ++bus)
            {
                const auto slot = busSlot[(size_t) bus];
                if (slot < 0 || ! live.test ((size_t) slot))
                    continue;
                bool fedAhead = false;
                for (const auto& t : graph.tracks)
                {
                    if (! juce::isPositiveAndBelow (t.slot, PlaybackGraph::maxTracks) || live.test ((size_t) t.slot))
                        continue;
                    bool feeds = t.outputBus == bus && t.outputBus != t.busIndex;
                    for (const auto& send : t.sends)
                        feeds = feeds || send.busIndex == bus;
                    if (! feeds)
                        continue;
                    if (count >= aheadBusCarry)
                        mark (t.slot, changed);
                    fedAhead = true;
                }
                if (fedAhead && count < aheadBusCarry)
                    carried[(size_t) count++] = bus;
            }
        }
        if (! changed)
            break;
    }
    return live;
}

AudioEngine::AheadChunk* AudioEngine::aheadFront (double deadline) noexcept
{
    const auto serial = aheadSerial.load (std::memory_order_acquire);
    for (int spins = 0;; ++spins)
    {
        const auto read = aheadRead.load (std::memory_order_acquire);
        if (read < aheadWritten.load (std::memory_order_acquire))
        {
            auto& chunk = aheadRing[(size_t) (read % aheadSlots)];
            if (chunk.serial == serial && chunk.read < chunk.n)
                return &chunk;
            aheadRead.fetch_add (1, std::memory_order_acq_rel);   // (from before a restart: thrown away)
            aheadThread->wake();
            continue;
        }
        if ((spins & 63) == 0 && juce::Time::getMillisecondCounterHiRes() > deadline)
            return nullptr;
        if ((spins & 1023) == 0)
            aheadThread->wake();
        std::this_thread::yield();
    }
}

void AudioEngine::runAhead (AheadThread& self)
{
    juce::uint32 serial = 0;
    juce::int64 position = 0;
    bool playingNow = false, resetNotes = true, first = true, expectedPlaying = false;
    juce::int64 expected = 0;
    SlotSet sticky;
    std::array<int, aheadBusCarry> carried {};
    while (! self.threadShouldExit())
    {
        if (aheadPark.load())
        {
            aheadParked.store (true);
            self.rest (50);
            continue;
        }
        aheadParked.store (false);

        // Join the audio device's workgroup (macOS), as the render threads do
        self.joinWorkgroupIfChanged();

        // A restart (start, stop, locate): from there, with what's live now
        if (const auto s = aheadSerial.load (std::memory_order_acquire); s != serial)
        {
            serial = s;
            position = aheadStartPosition.load();
            playingNow = aheadStartPlaying.load();
            resetNotes = true;
            first = true;
            sticky.reset();
            // (whatever is still in the ring is old: the callback throws it away)
        }

        // Far enough ahead (or the ring is full): wait for the callback to play some
        const auto written = aheadWritten.load (std::memory_order_acquire), read = aheadRead.load (std::memory_order_acquire);
        juce::int64 queued = 0;
        for (auto k = read; k < written; ++k)
        {
            const auto& c = aheadRing[(size_t) (k % aheadSlots)];
            if (c.serial == serial)
                queued += c.n;   // (what's left of the one playing counts whole: near enough)
        }
        const auto lookahead = (juce::int64) aheadLookahead.load() + 2 * (juce::int64) deviceBlock.load();
        if (written - read >= (juce::uint64) aheadSlots - 1 || queued >= lookahead)
        {
            self.rest (playingNow ? 1 : 10);   // (stopped: nothing's in a hurry - it doesn't wake 1000 times a second)
            continue;
        }

        aheadCounter.fetch_add (1, std::memory_order_acq_rel);
        const auto* graph = currentGraph.load (std::memory_order_acquire);
        if (graph == nullptr)
        {
            self.rest (5);
            continue;
        }

        const auto t0 = juce::Time::getHighResolutionTicks();

        // This chunk: the first after a restart as small as the device's buffer (the callback is waiting for it),
        // then larger; it ends at the cycle end, where the song goes back to the cycle start
        int n = first ? juce::jlimit (16, aheadMaxChunk, deviceBlock.load()) : aheadChunk.load();
        const bool cycling = graph->cycleEnabled && graph->cycleEnd > graph->cycleStart;
        if (playingNow && cycling && position < graph->cycleEnd)
            n = (int) juce::jmin ((juce::int64) n, graph->cycleEnd - position);
        n = juce::jmax (1, n);

        auto& chunk = aheadRing[(size_t) (written % aheadSlots)];
        chunk.live = liveSlotsOf (*graph, sticky, carried);
        sticky |= chunk.live;   // (a track that was live stays live until the next restart: no hand-back while playing)
        chunk.carriedBus = carried;
        chunk.position = position;
        chunk.playing = playingNow;
        chunk.n = n;
        chunk.read = 0;
        chunk.audio.clear (0, n);

        if (! (playingNow == expectedPlaying && (! playingNow || position == expected)))
            resetNotes = true;

        mainPass.position = position;
        mainPass.playing = playingNow;
        mainPass.slots = &chunk.live;
        mainPass.exclude = true;
        mainPass.busPreload.fill (nullptr);
        chunk.peaks.fill (0.0f);
        mainPass.peakSink = chunk.peaks.data();
        renderChunk (*graph, mainPass, nullptr, 0, 0, chunk.audio.getWritePointer (0), chunk.audio.getWritePointer (1), n, resetNotes, false);
        mainPass.peakSink = nullptr;

        // What the ahead tracks sent the live buses
        for (int k = 0; k < aheadBusCarry; ++k)
            if (const auto bus = carried[(size_t) k]; juce::isPositiveAndBelow (bus, PlaybackGraph::maxBuses))
            {
                chunk.audio.copyFrom (2 + 2 * k, 0, mainPass.bus, bus * 2, 0, n);
                chunk.audio.copyFrom (3 + 2 * k, 0, mainPass.bus, bus * 2 + 1, 0, n);
            }

        chunk.serial = serial;
        aheadWritten.fetch_add (1, std::memory_order_acq_rel);

        resetNotes = false;
        first = false;
        expectedPlaying = playingNow;
        if (playingNow)
        {
            // (as the callback does: a cycle wrap is a jump, so the notes are reset there)
            const auto next = position + n;
            expected = next;
            position = (cycling && position < graph->cycleEnd && next >= graph->cycleEnd) ? graph->cycleStart : next;
        }
        else
            expected = position;

        // The load: this chunk's processing time against the time it plays for
        const auto rate = sampleRate.load();
        if (rate > 0.0)
        {
            const auto took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            aheadBusy.store (aheadBusy.load (std::memory_order_relaxed) + took, std::memory_order_relaxed);
            aheadSpan.store (aheadSpan.load (std::memory_order_relaxed) + n / rate, std::memory_order_relaxed);
            storeMax (loadNow, (float) (took * rate / n));
            storeMax (loadPeak, (float) (took * rate / n));
        }
    }
    aheadParked.store (true);
}

} // namespace riff
