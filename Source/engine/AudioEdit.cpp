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

namespace
{
    /** Loudness every 256 samples (mono). */
    std::vector<float> envelope (const juce::AudioBuffer<float>& audio, int hop)
    {
        std::vector<float> env;
        for (int start = 0; start + hop <= audio.getNumSamples(); start += hop)
        {
            float sum = 0.0f;
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                const auto* d = audio.getReadPointer (c, start);
                for (int i = 0; i < hop; ++i)
                    sum += d[i] * d[i];
            }
            env.push_back (std::sqrt (sum / (float) (hop * audio.getNumChannels())));
        }
        return env;
    }

    /** How much louder each step got (onset strength). */
    std::vector<float> onsetStrength (const std::vector<float>& env)
    {
        std::vector<float> flux (env.size(), 0.0f);
        if (! env.empty())
            flux[0] = std::log (1.0f + 100.0f * env[0]);   // from silence before the start
        for (size_t i = 1; i < env.size(); ++i)
            flux[i] = juce::jmax (0.0f, std::log (1.0f + 100.0f * env[i]) - std::log (1.0f + 100.0f * env[i - 1]));
        return flux;
    }
}

std::vector<int> detectTransients (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    constexpr int hop = 256;
    const auto flux = onsetStrength (envelope (audio, hop));
    std::vector<int> onsets;
    if (flux.size() < 3)
        return onsets;

    // Peaks above the local average (adaptive threshold), at least 50 ms apart.
    const int window = juce::jmax (4, (int) (0.15 * sampleRate / hop));
    const int minGap = juce::jmax (1, (int) (0.05 * sampleRate / hop));
    float maxFlux = 0.0f;
    for (auto f : flux) maxFlux = juce::jmax (maxFlux, f);
    int last = -minGap;
    for (int i = 0; i + 1 < (int) flux.size(); ++i)
    {
        float mean = 0.0f;
        int count = 0;
        for (int k = juce::jmax (0, i - window); k < juce::jmin ((int) flux.size(), i + window); ++k, ++count)
            mean += flux[(size_t) k];
        mean /= (float) juce::jmax (1, count);
        if (flux[(size_t) i] > mean * 1.8f + maxFlux * 0.08f && (i == 0 || flux[(size_t) i] >= flux[(size_t) i - 1]) && flux[(size_t) i] >= flux[(size_t) i + 1]
            && i - last >= minGap)
        {
            onsets.push_back (i * hop);
            last = i;
        }
    }
    return onsets;
}

double detectTempo (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    constexpr int hop = 256;
    const auto flux = onsetStrength (envelope (audio, hop));
    const auto frameRate = sampleRate / hop;
    if (flux.size() < (size_t) (frameRate * 2.0))
        return 0.0;

    // Autocorrelation of the onset strength over beat lengths 70..180 BPM; a little weight for the
    // double and half beat, so the felt tempo wins.
    const auto score = [&] (double bpm)
    {
        const auto lag = frameRate * 60.0 / bpm;
        double s = 0.0;
        for (double l : { lag, lag * 2.0 })
        {
            const auto il = (size_t) std::llround (l);
            const auto frac = l - std::floor (l);
            for (size_t i = 0; i + il + 1 < flux.size(); ++i)
                s += flux[i] * (flux[i + il] * (1.0 - frac) + flux[i + il + 1] * frac);
        }
        return s;
    };

    double bestBpm = 0.0, best = 0.0, total = 0.0;
    int n = 0;
    for (double bpm = 70.0; bpm <= 180.0; bpm += 0.5)
    {
        const auto s = score (bpm);
        total += s;
        ++n;
        if (s > best)
        {
            best = s;
            bestBpm = bpm;
        }
    }
    // No clear peak: no beat to speak of.
    if (n == 0 || best < (total / n) * 1.3)
        return 0.0;

    // Refine to a tenth of a BPM.
    for (double bpm = bestBpm - 0.5; bpm <= bestBpm + 0.5; bpm += 0.1)
        if (const auto s = score (bpm); s > best)
        {
            best = s;
            bestBpm = bpm;
        }
    return std::round (bestBpm * 10.0) / 10.0;
}

juce::AudioBuffer<float> flexQuantize (const juce::AudioBuffer<float>& audio, double sampleRate, const std::function<double (double)>& target)
{
    const auto onsets = detectTransients (audio, sampleRate);
    const auto length = audio.getNumSamples();

    // Anchors: (where in the audio, where it should go), in samples, keeping order.
    std::vector<std::pair<int, int>> anchors { { 0, 0 } };
    for (auto o : onsets)
    {
        const auto to = (int) std::llround (target (o / sampleRate) * sampleRate);
        if (o > anchors.back().first + 64 && to > anchors.back().second + 64)
            anchors.push_back ({ o, to });
    }
    const auto endTarget = anchors.back().second + (length - anchors.back().first);
    anchors.push_back ({ length, endTarget });

    juce::AudioBuffer<float> out (audio.getNumChannels(), juce::jmax (1, endTarget));
    out.clear();
    int written = 0;
    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto inLen = anchors[i + 1].first - anchors[i].first;
        const auto outLen = anchors[i + 1].second - anchors[i].second;
        if (inLen <= 0 || outLen <= 0)
            continue;
        juce::AudioBuffer<float> piece (audio.getNumChannels(), inLen);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            piece.copyFrom (c, 0, audio, c, anchors[i].first, inLen);
        const auto stretched = std::abs (outLen - inLen) < 8 ? piece : timeStretch (piece, (double) outLen / inLen, sampleRate);
        const auto n = juce::jmin (stretched.getNumSamples(), out.getNumSamples() - written);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            out.copyFrom (c, written, stretched, c, 0, n);
        written += n;
    }
    out.setSize (out.getNumChannels(), juce::jmax (1, written), true);
    return out;
}

double detectPitch (const juce::AudioBuffer<float>& audio, double sampleRate, int centre)
{
    const int window = 2048, maxLag = juce::jmin (window / 2, (int) (sampleRate / 60.0)), minLag = (int) (sampleRate / 1000.0);
    const auto start = juce::jlimit (0, juce::jmax (0, audio.getNumSamples() - window - maxLag), centre - window / 2);
    if (audio.getNumSamples() < window + maxLag)
        return 0.0;

    std::vector<float> x ((size_t) (window + maxLag));
    for (int i = 0; i < window + maxLag; ++i)
    {
        float v = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c)
            v += audio.getSample (c, start + i);
        x[(size_t) i] = v;
    }

    // YIN: the difference function, normalised by its running mean; the first dip under the threshold.
    std::vector<double> d ((size_t) maxLag + 1, 0.0);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; i += 2)
        {
            const double diff = x[(size_t) i] - x[(size_t) (i + tau)];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }
    double running = 0.0;
    std::vector<double> cmnd ((size_t) maxLag + 1, 1.0);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }
    for (int tau = juce::jmax (2, minLag); tau < maxLag; ++tau)
    {
        if (cmnd[(size_t) tau] < 0.15)
        {
            while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                ++tau;
            // Parabolic interpolation around the dip.
            const auto a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
            const auto shift = (a - c) / (2.0 * (a - 2.0 * b + c) + 1.0e-12);
            return sampleRate / (tau + juce::jlimit (-1.0, 1.0, shift));
        }
    }
    return 0.0;
}

std::vector<PitchedNote> detectNotes (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    auto onsets = detectTransients (audio, sampleRate);
    if (onsets.empty() || onsets.front() > 0)
        onsets.insert (onsets.begin(), 0);
    std::vector<PitchedNote> notes;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const auto start = onsets[i];
        const auto end = i + 1 < onsets.size() ? onsets[i + 1] : audio.getNumSamples();
        if (end - start < 1024)
            continue;
        // The pitch a little after the hit (past the attack).
        const auto f = detectPitch (audio, sampleRate, start + juce::jmin ((end - start) / 2, (int) (0.08 * sampleRate)));
        notes.push_back ({ start, end, f > 0.0 ? 69.0 + 12.0 * std::log2 (f / 440.0) : 0.0 });
    }
    return notes;
}

juce::AudioBuffer<float> readRegion (juce::AudioFormatManager& formats, const juce::File& file, double offset, double length, double& sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return {};
    sampleRate = reader->sampleRate;
    const auto start = (juce::int64) std::llround (juce::jmax (0.0, offset) * sampleRate);
    const auto count = (int) juce::jmax ((juce::int64) 0, juce::jmin ((juce::int64) std::llround (length * sampleRate), reader->lengthInSamples - start));
    const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> audio (channels, juce::jmax (1, count));
    audio.clear();
    if (count > 0)
        reader->read (&audio, 0, count, start, true, channels > 1);
    return audio;
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
            if (request.to < 0.0)
            {
                audio = pitchShift (audio, request.amount, sampleRate);
            }
            else if (count > 256)
            {
                // Flex Pitch on one note: shift just that stretch, with short crossfades either side.
                juce::AudioBuffer<float> piece (audio.getNumChannels(), count);
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    piece.copyFrom (c, 0, audio, c, from, count);
                const auto shifted = pitchShift (piece, request.amount, sampleRate);
                const auto fade = juce::jmin (count / 4, (int) (0.005 * sampleRate));
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    for (int i = 0; i < count; ++i)
                    {
                        const auto w = i < fade ? (float) i / (float) fade : i >= count - fade ? (float) (count - i) / (float) fade : 1.0f;
                        audio.setSample (c, from + i, audio.getSample (c, from + i) * (1.0f - w) + shifted.getSample (c, i) * w);
                    }
            }
            break;
        case Operation::flexQuantize:
            if (request.target)
                audio = flexQuantize (audio, sampleRate, request.target);
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
