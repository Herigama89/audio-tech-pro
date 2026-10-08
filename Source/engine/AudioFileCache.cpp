#include "AudioFileCache.h"
#include "Resampler.h"

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
                    cache.loadingName = juce::File (key).getFileName();
                }
            }

            if (key.isEmpty())
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
    resampler::initialise();

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

SharedAudio AudioFileCache::get (const juce::File& file)
{
    const Key key = file.getFullPathName();

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
        lengths[key] = result.lengthSeconds;
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
    const juce::ScopedLock sl (lock);
    return pending.count (file.getFullPathName()) > 0;
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
            lengths[r.key] = r.lengthSeconds;
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

void AudioFileCache::forget (const juce::File& file)
{
    const auto key = file.getFullPathName();
    entries.erase (key);
    failed.erase (key);
    lengths.erase (key);
}

AudioFileCache::Result AudioFileCache::decode (juce::AudioFormatManager& formats, const Key& key)
{
    Result result { key, nullptr, 0.0 };
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (key)));

    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        return result;

    const auto numSamples = (int) std::min (reader->lengthInSamples, (juce::int64) (std::numeric_limits<int>::max() / 4));
    const auto numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = reader->sampleRate;
    audio->buffer.setSize (2, numSamples);
    reader->read (&audio->buffer, 0, numSamples, 0, true, numChannels > 1);

    if (numChannels == 1)
        audio->buffer.copyFrom (1, 0, audio->buffer, 0, 0, numSamples);   // mono plays in both sides

    result.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    result.audio = audio;
    return result;
}

} // namespace riff
