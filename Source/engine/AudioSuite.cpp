#include "AudioSuite.h"

namespace riff::audiosuite
{

Result render (juce::AudioFormatManager& formats, const juce::File& source, double regionOffset, double regionLength,
               Effect& effect, const Options& options, const juce::File& outFile, std::function<bool (double)> progress)
{
    Result result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
    {
        result.error = "Couldn't read " + source.getFileName() + ".";
        return result;
    }

    const auto rate = reader->sampleRate;
    const auto fileLength = reader->lengthInSamples;
    const auto toSamples = [rate] (double s) { return (juce::int64) std::llround (s * rate); };

    // What to process: the region and its handles (inside the file), or the whole file.
    const auto start = options.wholeFile ? (juce::int64) 0 : juce::jlimit ((juce::int64) 0, fileLength, toSamples (regionOffset - options.handleSeconds));
    const auto end = options.wholeFile ? fileLength : juce::jlimit (start, fileLength, toSamples (regionOffset + regionLength + options.handleSeconds));
    const auto count = end - start;
    const auto tail = toSamples (juce::jmax (0.0, options.tailSeconds));
    if (count <= 0)
    {
        result.error = "The region has no audio in its file.";
        return result;
    }

    constexpr int block = 512;
    effect.prepare (rate, block);
    const auto latency = (juce::int64) juce::jmax (0, effect.getLatencySamples());
    const auto outLength = count + tail;
    if (outLength > (juce::int64) rate * 60 * 60)
    {
        result.error = "That's over an hour of audio; process a shorter region.";
        return result;
    }

    const auto inChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> input (inChannels, (int) count);
    reader->read (&input, 0, (int) count, start, true, inChannels > 1);

    // Through the effect in blocks; the first `latency` samples of its output are dropped.
    juce::AudioBuffer<float> out (2, (int) outLength);
    out.clear();
    juce::AudioBuffer<float> work (2, block);
    juce::int64 written = 0;
    for (juce::int64 pos = 0; pos < outLength + latency; pos += block)
    {
        const auto n = (int) juce::jmin ((juce::int64) block, outLength + latency - pos);
        work.clear();
        for (int c = 0; c < 2; ++c)
        {
            const auto from = pos, available = juce::jmax ((juce::int64) 0, juce::jmin ((juce::int64) n, count - from));
            if (available > 0)
                work.copyFrom (c, 0, input, juce::jmin (c, inChannels - 1), (int) from, (int) available);
        }
        effect.process (work.getWritePointer (0), work.getWritePointer (1), n);

        // Keep what lands after the latency.
        for (int i = 0; i < n; ++i)
        {
            const auto at = pos + i - latency;
            if (at >= 0 && at < outLength)
            {
                out.setSample (0, (int) at, work.getSample (0, i));
                out.setSample (1, (int) at, work.getSample (1, i));
                written = juce::jmax (written, at + 1);
            }
        }
        if (progress && ! progress ((double) pos / (double) (outLength + latency)))
        {
            result.error = "Cancelled.";
            return result;
        }
    }

    // A mono file stays mono unless the effect made it stereo.
    bool stereo = inChannels > 1;
    if (! stereo)
        for (int i = 0; i < out.getNumSamples() && ! stereo; ++i)
            stereo = std::abs (out.getSample (0, i) - out.getSample (1, i)) > 1.0e-6f;

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (stereo ? 2 : 1)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
    {
        result.error = "Couldn't write " + outFile.getFullPathName() + ".";
        return result;
    }
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    writer.reset();

    result.file = outFile;
    result.offset = options.wholeFile ? regionOffset : regionOffset - (double) start / rate;
    return result;
}

} // namespace riff::audiosuite
