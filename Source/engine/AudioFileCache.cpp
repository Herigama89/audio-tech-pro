#include "AudioFileCache.h"

namespace riff
{

SharedAudio AudioFileCache::get (const juce::File& file, double targetSampleRate)
{
    const Key key { file.getFullPathName(), targetSampleRate };

    if (auto it = entries.find (key); it != entries.end())
        return it->second;

    auto audio = decode (file, targetSampleRate);

    if (audio != nullptr)
        entries[key] = audio;

    return audio;
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
}

SharedAudio AudioFileCache::decode (const juce::File& file, double targetSampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        return nullptr;

    const auto numSourceSamples = (int) std::min (reader->lengthInSamples, (juce::int64) (std::numeric_limits<int>::max() / 4));
    const auto numSourceChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    juce::AudioBuffer<float> source (numSourceChannels, numSourceSamples);
    reader->read (&source, 0, numSourceSamples, 0, true, numSourceChannels > 1);

    lengths[file.getFullPathName()] = (double) reader->lengthInSamples / reader->sampleRate;

    // Same rate: just make it stereo.
    if (std::abs (reader->sampleRate - targetSampleRate) < 0.5)
    {
        auto result = std::make_shared<juce::AudioBuffer<float>> (2, numSourceSamples);
        result->copyFrom (0, 0, source, 0, 0, numSourceSamples);
        result->copyFrom (1, 0, source, numSourceChannels > 1 ? 1 : 0, 0, numSourceSamples);
        return result;
    }

    // Different rate: resample once here, so the audio thread never has to.
    const auto ratio = reader->sampleRate / targetSampleRate; // input samples per output sample
    const auto numOut = (int) std::ceil (numSourceSamples / ratio);

    // The interpolator delays its output; produce extra samples and drop the
    // start so audio lands at exactly the same time as the original.
    const auto latencyOut = (int) std::round (juce::WindowedSincInterpolator::getBaseLatency() / ratio);

    auto result = std::make_shared<juce::AudioBuffer<float>> (2, numOut);
    juce::HeapBlock<float> temp ((size_t) (numOut + latencyOut));

    for (int ch = 0; ch < 2; ++ch)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio,
                              source.getReadPointer (juce::jmin (ch, numSourceChannels - 1)),
                              temp.get(),
                              numOut + latencyOut,
                              numSourceSamples,
                              0);
        result->copyFrom (ch, 0, temp.get() + latencyOut, numOut);
    }

    return result;
}

} // namespace riff
