#include "AudioEdit.h"

namespace riff::audioedit
{

juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& input, double ratio, double sampleRate)
{
    const auto channels = input.getNumChannels();
    const auto inLength = input.getNumSamples();
    ratio = juce::jlimit (0.25, 4.0, ratio);

    // Frames of ~40 ms, half overlapping; each one's input position is searched (+-10 ms)
    // for the place that continues the previous frame most smoothly.
    const int frame = juce::nextPowerOfTwo ((int) (sampleRate * 0.04));
    const int hopOut = frame / 2;
    const double hopIn = hopOut / ratio;
    const int tolerance = (int) (sampleRate * 0.01);
    const auto outLength = (int) std::llround (inLength * ratio);

    juce::AudioBuffer<float> output (channels, outLength + frame);
    output.clear();
    if (inLength < frame * 2)
    {
        // Too short to stretch: resample instead.
        juce::AudioBuffer<float> out (channels, juce::jmax (1, outLength));
        for (int c = 0; c < channels; ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.process (1.0 / ratio, input.getReadPointer (c), out.getWritePointer (c), out.getNumSamples(), inLength, 0);
        }
        return out;
    }

    std::vector<float> window ((size_t) frame);
    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) frame);   // periodic Hann

    // A mono mix to find the best places.
    std::vector<float> mono ((size_t) inLength);
    for (int c = 0; c < channels; ++c)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), input.getReadPointer (c), 1.0f / (float) channels, inLength);

    int previous = 0;   // where the last frame was taken from
    for (int k = 0; ; ++k)
    {
        const auto outPos = k * hopOut;
        if (outPos >= outLength)
            break;

        auto nominal = (int) std::llround (k * hopIn);
        int best = juce::jlimit (0, inLength - frame, nominal);

        if (k > 0)
        {
            // Compare with what would naturally follow the previous frame.
            const auto natural = previous + hopOut;
            if (natural + hopOut < inLength)
            {
                double bestScore = -1.0e30;
                for (int d = -tolerance; d <= tolerance; d += 2)
                {
                    const auto candidate = nominal + d;
                    if (candidate < 0 || candidate + frame > inLength)
                        continue;
                    double score = 0.0;
                    for (int i = 0; i < hopOut; i += 4)
                        score += (double) mono[(size_t) (candidate + i)] * mono[(size_t) (natural + i)];
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = candidate;
                    }
                }
            }
        }
        best = juce::jlimit (0, inLength - frame, best);
        previous = best;

        for (int c = 0; c < channels; ++c)
        {
            const auto* in = input.getReadPointer (c, best);
            auto* out = output.getWritePointer (c, outPos);
            for (int i = 0; i < frame && outPos + i < output.getNumSamples(); ++i)
                out[i] += in[i] * window[(size_t) i];
        }
    }

    output.setSize (channels, outLength, true);
    return output;
}

juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& input, double semitones, double sampleRate)
{
    const auto factor = std::pow (2.0, semitones / 12.0);
    // Longer by the factor, then played faster by it: the same length, higher (or lower).
    const auto stretched = timeStretch (input, factor, sampleRate);
    juce::AudioBuffer<float> out (input.getNumChannels(), input.getNumSamples());
    out.clear();
    for (int c = 0; c < input.getNumChannels(); ++c)
    {
        juce::LagrangeInterpolator interp;
        interp.process (factor, stretched.getReadPointer (c), out.getWritePointer (c), out.getNumSamples(), stretched.getNumSamples(), 0);
    }
    return out;
}

void apply (juce::AudioBuffer<float>& audio, const Request& request, double sampleRate)
{
    const auto length = audio.getNumSamples();
    const auto from = juce::jlimit (0, length, (int) std::llround (request.from * sampleRate));
    const auto to = request.to < 0.0 ? length : juce::jlimit (from, length, (int) std::llround (request.to * sampleRate));
    const auto count = to - from;

    switch (request.operation)
    {
        case Operation::normalize:
        {
            // The loudest peak to -0.1 dBFS.
            float peak = 0.0f;
            for (int c = 0; c < audio.getNumChannels(); ++c)
                peak = juce::jmax (peak, audio.getMagnitude (c, from, count));
            if (peak > 1.0e-6f)
                audio.applyGain (from, count, juce::Decibels::decibelsToGain (-0.1f) / peak);
            break;
        }
        case Operation::gain:
            audio.applyGain (from, count, juce::Decibels::decibelsToGain ((float) request.amount));
            break;
        case Operation::fadeIn:
            audio.applyGainRamp (from, count, 0.0f, 1.0f);
            break;
        case Operation::fadeOut:
            audio.applyGainRamp (from, count, 1.0f, 0.0f);
            break;
        case Operation::silence:
            for (int c = 0; c < audio.getNumChannels(); ++c)
                audio.clear (c, from, count);
            break;
        case Operation::reverse:
            audio.reverse (from, count);
            break;
        case Operation::invert:
            audio.applyGain (from, count, -1.0f);
            break;
        case Operation::timeStretch:
            audio = timeStretch (audio, request.amount, sampleRate);
            break;
        case Operation::pitchShift:
            audio = pitchShift (audio, request.amount, sampleRate);
            break;
    }
}

juce::String processFile (juce::AudioFormatManager& formats, const juce::File& source, double offset, double length,
                          const Request& request, const juce::File& dest, double& newLength)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
        return "Couldn't read " + source.getFileName();

    const auto rate = reader->sampleRate;
    const auto start = (juce::int64) std::llround (juce::jmax (0.0, offset) * rate);
    const auto count = (int) juce::jmin ((juce::int64) std::llround (length * rate), reader->lengthInSamples - start);
    if (count <= 0)
        return "The region has no audio to edit.";

    const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> audio (channels, count);
    reader->read (&audio, 0, count, start, true, channels > 1);

    apply (audio, request, rate);

    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (dest));
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                    .withNumChannels (channels).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        return "Couldn't write " + dest.getFileName();

    newLength = audio.getNumSamples() / rate;
    return {};
}

} // namespace riff::audioedit
