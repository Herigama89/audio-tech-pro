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

            auto result = decode (cache.formatManager, key, cache.streamAbove.load());

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
        auto result = decode (formatManager, key, streamAbove.load());
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
        if (provided.count (r.key) > 0)
            continue;   // (made in memory meanwhile: that one stays)
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

void AudioFileCache::put (const juce::File& file, SharedAudio audio)
{
    const auto key = file.getFullPathName();
    {
        const juce::ScopedLock sl (lock);
        pending.erase (key);
        queue.erase (std::remove (queue.begin(), queue.end(), key), queue.end());
    }
    failed.erase (key);
    provided.insert (key);
    if (audio != nullptr)
        lengths[key] = (double) audio->numFrames() / juce::jmax (1.0, audio->sampleRate);
    entries[key] = std::move (audio);
    sendChangeMessage();
}

void AudioFileCache::forget (const juce::File& file)
{
    const auto key = file.getFullPathName();
    entries.erase (key);
    failed.erase (key);
    lengths.erase (key);
    provided.erase (key);
}

void DecodedAudio::readStereo (juce::int64 start, int num, float* left, float* right) const noexcept
{
    if (num <= 0)
        return;
    const auto frames = numFrames();

    // Silence before the start and after the end.
    int lead = 0;
    if (start < 0)
    {
        lead = (int) juce::jmin ((juce::int64) num, -start);
        juce::FloatVectorOperations::clear (left, lead);
        juce::FloatVectorOperations::clear (right, lead);
        start = 0;
    }
    const auto count = (int) juce::jlimit ((juce::int64) 0, (juce::int64) (num - lead), frames - start);
    if (count < num - lead)
    {
        juce::FloatVectorOperations::clear (left + lead + count, num - lead - count);
        juce::FloatVectorOperations::clear (right + lead + count, num - lead - count);
    }
    if (count <= 0)
        return;

    if (streamed == nullptr)
    {
        juce::FloatVectorOperations::copy (left + lead, buffer.getReadPointer (0, (int) start), count);
        juce::FloatVectorOperations::copy (right + lead, buffer.getReadPointer (1, (int) start), count);
        return;
    }

    float* dest[2] = { left + lead, right + lead };
    streamed->read (dest, streamedChannels > 1 ? 2 : 1, start, count);
    if (streamedChannels == 1)
        juce::FloatVectorOperations::copy (right + lead, left + lead, count);   // mono plays in both sides
}

void DecodedAudio::touch (juce::int64 start, juce::int64 num) const noexcept
{
    if (streamed == nullptr)
        return;
    const auto from = juce::jmax ((juce::int64) 0, start), to = juce::jmin (streamedFrames, start + num);
    for (auto i = from; i < to; i += 256)   // (a page is a few hundred frames)
        streamed->touchSample (i);
}

AudioFileCache::Result AudioFileCache::decode (juce::AudioFormatManager& formats, const Key& key, juce::int64 streamAboveBytes)
{
    Result result { key, nullptr, 0.0 };
    const juce::File file (key);

    // Long WAV and AIFF files stream from disk.
    if (file.hasFileExtension ("wav;wave;aif;aiff"))
        if (auto* format = formats.findFormatForFileExtension (file.getFileExtension()))
            if (std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped { format->createMemoryMappedReader (file) };
                mapped != nullptr && mapped->sampleRate > 0 && mapped->lengthInSamples > 0
                && mapped->lengthInSamples * 2 * (juce::int64) sizeof (float) > streamAboveBytes
                && (mapped->bitsPerSample == 16 || mapped->bitsPerSample == 24 || mapped->bitsPerSample == 32 || mapped->bitsPerSample == 8)
                && mapped->mapEntireFile())
            {
                auto audio = std::make_shared<DecodedAudio>();
                audio->sampleRate = mapped->sampleRate;
                audio->streamedFrames = mapped->lengthInSamples;
                audio->streamedChannels = (int) juce::jlimit (1u, 2u, mapped->numChannels);
                result.lengthSeconds = (double) mapped->lengthInSamples / mapped->sampleRate;
                audio->streamed = std::move (mapped);
                result.audio = audio;
                return result;
            }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

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
