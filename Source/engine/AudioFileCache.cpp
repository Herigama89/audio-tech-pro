#include "AudioFileCache.h"

namespace riff
{

/** Decodes queued files one at a time, off the message thread. */
class AudioFileCache::Worker final : public juce::Thread
{
public:
    explicit Worker (AudioFileCache& c) : juce::Thread ("Audio file loader"), cache (c) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            Key key;
            {
                const juce::ScopedLock sl (cache.lock);
                if (cache.queue.empty())
                {
                    cache.loadingName = {};
                    idle.signal();
                }
                else
                {
                    key = cache.queue.front();
                    cache.queue.pop_front();
                    cache.loadingName = juce::File (key.path).getFileName();
                }
            }

            if (key.path.isEmpty())
            {
                wait (-1);   // until notify()
                continue;
            }

            auto result = decode (cache.formatManager, key);

            {
                const juce::ScopedLock sl (cache.lock);
                cache.finished.push_back (std::move (result));
            }
            cache.triggerAsyncUpdate();
        }
    }

    juce::WaitableEvent idle { true };

private:
    AudioFileCache& cache;
};

//==============================================================================
AudioFileCache::AudioFileCache (juce::AudioFormatManager& formats, bool loadInBackground)
    : formatManager (formats)
{
    if (loadInBackground)
    {
        worker = std::make_unique<Worker> (*this);
        worker->startThread (juce::Thread::Priority::low);
    }
}

AudioFileCache::~AudioFileCache()
{
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->notify();
        worker->stopThread (10000);
    }
    cancelPendingUpdate();
}

SharedAudio AudioFileCache::get (const juce::File& file, double targetSampleRate)
{
    const Key key { file.getFullPathName(), targetSampleRate };

    if (auto it = entries.find (key); it != entries.end())
        return it->second;

    if (failed.count (key) > 0)
        return nullptr;

    if (worker == nullptr)
    {
        // Synchronous mode: decode right here.
        auto result = decode (formatManager, key);
        if (result.audio == nullptr)
        {
            failed.insert (key);
            return nullptr;
        }
        lengths[key.path] = result.lengthSeconds;
        entries[key] = result.audio;
        return result.audio;
    }

    {
        const juce::ScopedLock sl (lock);
        if (pending.insert (key).second)
        {
            queue.push_back (key);
            worker->idle.reset();
            worker->notify();
        }
    }

    return nullptr;
}

bool AudioFileCache::isLoading() const
{
    const juce::ScopedLock sl (lock);
    return ! pending.empty();
}

bool AudioFileCache::isPending (const juce::File& file) const
{
    const auto path = file.getFullPathName();
    const juce::ScopedLock sl (lock);
    for (const auto& key : pending)
        if (key.path == path)
            return true;
    return false;
}

juce::String AudioFileCache::currentlyLoading() const
{
    const juce::ScopedLock sl (lock);
    return loadingName;
}

bool AudioFileCache::waitUntilIdle (int timeoutMs)
{
    if (worker == nullptr)
        return true;

    const auto ok = worker->idle.wait (timeoutMs);
    collectFinished();
    return ok;
}

void AudioFileCache::handleAsyncUpdate()
{
    collectFinished();
}

void AudioFileCache::collectFinished()
{
    std::vector<Result> done;
    {
        const juce::ScopedLock sl (lock);
        done.swap (finished);
        for (const auto& r : done)
            pending.erase (r.key);
    }

    if (done.empty())
        return;

    for (auto& r : done)
    {
        if (r.audio != nullptr)
        {
            entries[r.key] = r.audio;
            lengths[r.key.path] = r.lengthSeconds;
        }
        else
        {
            failed.insert (r.key);
        }
    }

    sendChangeMessage();   // the player rebuilds, the arrange window repaints
}

double AudioFileCache::getLengthSeconds (const juce::File& file)
{
    const auto path = file.getFullPathName();

    if (auto it = lengths.find (path); it != lengths.end())
        return it->second;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0)
        return 0.0;

    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
    lengths[path] = seconds;
    return seconds;
}

void AudioFileCache::purge (double keepSampleRate)
{
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (std::abs (it->first.sampleRate - keepSampleRate) > 0.5)
            it = entries.erase (it);
        else
            ++it;
    }

    for (auto it = failed.begin(); it != failed.end();)
    {
        if (std::abs (it->sampleRate - keepSampleRate) > 0.5)
            it = failed.erase (it);
        else
            ++it;
    }
}

AudioFileCache::Result AudioFileCache::decode (juce::AudioFormatManager& formats, const Key& key)
{
    Result result { key, nullptr, 0.0 };
    const juce::File file (key.path);
    const auto targetSampleRate = key.sampleRate;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        return result;

    const auto numSourceSamples = (int) std::min (reader->lengthInSamples, (juce::int64) (std::numeric_limits<int>::max() / 4));
    const auto numSourceChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    juce::AudioBuffer<float> source (numSourceChannels, numSourceSamples);
    reader->read (&source, 0, numSourceSamples, 0, true, numSourceChannels > 1);

    result.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

    // Same rate: just make it stereo.
    if (std::abs (reader->sampleRate - targetSampleRate) < 0.5)
    {
        auto audio = std::make_shared<juce::AudioBuffer<float>> (2, numSourceSamples);
        audio->copyFrom (0, 0, source, 0, 0, numSourceSamples);
        audio->copyFrom (1, 0, source, numSourceChannels > 1 ? 1 : 0, 0, numSourceSamples);
        result.audio = audio;
        return result;
    }

    // Different rate: resample once here, so the audio thread never has to.
    const auto ratio = reader->sampleRate / targetSampleRate; // input samples per output sample
    const auto numOut = (int) std::ceil (numSourceSamples / ratio);

    // The interpolator delays its output; produce extra samples and drop the
    // start so audio lands at exactly the same time as the original.
    const auto latencyOut = (int) std::round (juce::WindowedSincInterpolator::getBaseLatency() / ratio);

    auto audio = std::make_shared<juce::AudioBuffer<float>> (2, numOut);
    const bool mono = numSourceChannels == 1;

    // Each channel on its own thread (a mono file is resampled once and copied).
    auto resample = [&] (int ch, float* dest)
    {
        juce::HeapBlock<float> temp ((size_t) (numOut + latencyOut));
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, source.getReadPointer (juce::jmin (ch, numSourceChannels - 1)), temp.get(),
                              numOut + latencyOut, numSourceSamples, 0);
        juce::FloatVectorOperations::copy (dest, temp.get() + latencyOut, numOut);
    };

    if (mono)
    {
        resample (0, audio->getWritePointer (0));
        audio->copyFrom (1, 0, *audio, 0, 0, numOut);
    }
    else
    {
        std::thread right ([&] { resample (1, audio->getWritePointer (1)); });
        resample (0, audio->getWritePointer (0));
        right.join();
    }

    result.audio = audio;
    return result;
}

} // namespace riff
