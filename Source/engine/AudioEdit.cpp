#include "AudioEdit.h"
#include "TimeStretch.h"
#include "../model/Project.h"

namespace riff::audioedit
{

juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& input, double ratio, double sampleRate)
{
    return stretch::process (input, juce::jlimit (0.25, 4.0, ratio), sampleRate);
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

namespace
{
    /** Stretches the audio between anchors (in samples, from, to), first and last included. */
    juce::AudioBuffer<float> warp (const juce::AudioBuffer<float>& audio, double sampleRate, const std::vector<std::pair<int, int>>& anchors)
    {
        const auto endTarget = anchors.back().second;
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
    anchors.push_back ({ length, anchors.back().second + (length - anchors.back().first) });
    return warp (audio, sampleRate, anchors);
}

juce::AudioBuffer<float> flexWarp (const juce::AudioBuffer<float>& audio, double sampleRate, const std::vector<std::pair<double, double>>& markers)
{
    const auto length = audio.getNumSamples();
    std::vector<std::pair<int, int>> anchors { { 0, 0 } };
    for (const auto& [from, to] : markers)
    {
        const auto f = (int) std::llround (from * sampleRate), t = (int) std::llround (to * sampleRate);
        if (f > anchors.back().first + 64 && f < length - 64 && t > anchors.back().second + 64)
            anchors.push_back ({ f, t });
    }
    anchors.push_back ({ length, anchors.back().second + (length - anchors.back().first) });
    return warp (audio, sampleRate, anchors);
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
        case Operation::flexWarp:
            audio = flexWarp (audio, sampleRate, request.anchors);
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

juce::String consolidate (juce::AudioFormatManager& formats, const std::vector<ConsolidatePart>& parts, const juce::File& dest,
                          double& start, double& length)
{
    if (parts.empty())
        return "Nothing to consolidate";
    start = std::numeric_limits<double>::max();
    double end = 0.0, rate = 0.0;
    for (const auto& p : parts)
    {
        start = juce::jmin (start, p.start);
        end = juce::jmax (end, p.start + p.length);
        if (rate <= 0.0)
            if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (p.file)); reader != nullptr)
                rate = reader->sampleRate;
    }
    if (rate <= 0.0)
        return "The regions' audio files couldn't be read";
    length = end - start;
    juce::AudioBuffer<float> mix (2, juce::jmax (1, (int) std::ceil (length * rate)));
    mix.clear();

    for (const auto& p : parts)
    {
        double fileRate = 0.0;
        auto audio = readRegion (formats, p.file, p.offset, p.length, fileRate);
        if (fileRate <= 0.0 || audio.getNumSamples() < 2)
            continue;
        if (std::abs (p.semitones) > 1.0e-3)
            audio = pitchShift (audio, p.semitones, fileRate);

        // At the mix's rate
        if (std::abs (fileRate - rate) > 0.5)
        {
            const auto ratio = fileRate / rate;
            juce::AudioBuffer<float> converted (audio.getNumChannels(), (int) std::floor (audio.getNumSamples() / ratio));
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process (ratio, audio.getReadPointer (c), converted.getWritePointer (c), converted.getNumSamples());
            }
            audio = std::move (converted);
        }

        const auto at = (int) std::llround ((p.start - start) * rate);
        const auto n = juce::jmin (audio.getNumSamples(), (int) std::llround (p.length * rate), mix.getNumSamples() - at);
        const auto fadeInSamples = p.fadeIn * rate, fadeOutSamples = p.fadeOut * rate;
        for (int i = 0; i < n; ++i)
        {
            auto g = (double) p.gain;
            if (i < fadeInSamples)
                g *= model::fadeGain (i / fadeInSamples, p.fadeInCurve);
            if (const auto fromEnd = n - i; fromEnd <= fadeOutSamples)
                g *= model::fadeGain (fromEnd / fadeOutSamples, p.fadeOutCurve);
            for (int c = 0; c < 2; ++c)
                mix.getWritePointer (c)[at + i] += (float) g * audio.getReadPointer (juce::jmin (c, audio.getNumChannels() - 1))[i];
        }
    }

    dest.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dest);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return "Couldn't write " + dest.getFullPathName();
    writer->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples());
    return {};
}

namespace
{
    /** Loudness in 20 bands (log), every `hop` seconds: what the alignment compares. */
    std::vector<std::array<float, 20>> bandFrames (const juce::AudioBuffer<float>& audio, double rate, double hop)
    {
        constexpr int order = 10, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) size), data ((size_t) size * 2);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));

        // Band edges, 80 Hz to 9 kHz, spaced evenly in pitch
        std::array<int, 21> edges {};
        for (int b = 0; b <= 20; ++b)
            edges[(size_t) b] = juce::jlimit (1, size / 2 - 1, (int) std::lround (80.0 * std::pow (9000.0 / 80.0, b / 20.0) * size / rate));

        const auto step = juce::jmax (1, (int) std::lround (hop * rate));
        std::vector<std::array<float, 20>> frames;
        for (int start = -size / 2; start < audio.getNumSamples() - size / 2; start += step)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < size; ++i)
            {
                const auto at = start + i;
                if (at < 0 || at >= audio.getNumSamples())
                    continue;
                float v = 0.0f;
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    v += audio.getSample (c, at);
                data[(size_t) i] = v * window[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (data.data());
            std::array<float, 20> f {};
            for (int b = 0; b < 20; ++b)
            {
                double e = 0.0;
                for (int k = edges[(size_t) b]; k <= juce::jmax (edges[(size_t) b], edges[(size_t) b + 1] - 1); ++k)
                    e += (double) data[(size_t) k] * data[(size_t) k];
                f[(size_t) b] = (float) std::log10 (1.0e-7 + e);
            }
            frames.push_back (f);
        }
        return frames;
    }
}

std::vector<std::pair<double, double>> alignToGuide (const juce::AudioBuffer<float>& guide, double guideRate,
                                                     const juce::AudioBuffer<float>& dub, double dubRate,
                                                     double dubOffset, double strength, double maxShift)
{
    constexpr double hop = 0.01;
    const auto g = bandFrames (guide, guideRate, hop);
    const auto d = bandFrames (dub, dubRate, hop);
    if (g.size() < 4 || d.size() < 4)
        return {};

    // Each signal's frames less its own average loudness (a quieter dub still matches)
    const auto centred = [] (std::vector<std::array<float, 20>> frames)
    {
        std::array<double, 20> mean {};
        for (const auto& f : frames)
            for (size_t b = 0; b < 20; ++b)
                mean[b] += f[b];
        for (auto& m : mean)
            m /= (double) frames.size();
        for (auto& f : frames)
            for (size_t b = 0; b < 20; ++b)
                f[b] -= (float) mean[b];
        return frames;
    };
    const auto G = centred (g), D = centred (d);
    const auto cost = [&] (int gi, int di)
    {
        double c = 0.0;
        for (size_t b = 0; b < 20; ++b)
        {
            const auto diff = (double) G[(size_t) gi][b] - D[(size_t) di][b];
            c += diff * diff;
        }
        return std::sqrt (c);
    };

    // Dynamic time warping in a band: dub frame di may meet guide frames within maxShift of where it is now
    const int band = juce::jmax (2, (int) std::lround (maxShift / hop));
    const int offsetFrames = (int) std::lround (dubOffset / hop);
    const int nd = (int) D.size(), ng = (int) G.size();
    const auto lo = [&] (int di) { return juce::jmax (0, di + offsetFrames - band); };
    const auto hi = [&] (int di) { return juce::jmin (ng - 1, di + offsetFrames + band); };
    const auto width = 2 * band + 1;
    constexpr double inf = 1.0e30;
    std::vector<double> acc ((size_t) nd * (size_t) width, inf);
    std::vector<std::int8_t> from ((size_t) nd * (size_t) width, -1);
    const auto at = [&] (int di, int gi) -> size_t { return (size_t) di * (size_t) width + (size_t) (gi - (di + offsetFrames - band)); };
    const auto inBand = [&] (int di, int gi) { return di >= 0 && di < nd && gi >= lo (di) && gi <= hi (di); };

    for (int di = 0; di < nd; ++di)
        for (int gi = lo (di); gi <= hi (di); ++gi)
        {
            const auto c = cost (gi, di);
            double best = (di == 0) ? 0.0 : inf;
            std::int8_t step = -1;
            if (di > 0 && gi > 0 && inBand (di - 1, gi - 1) && acc[at (di - 1, gi - 1)] < best)      { best = acc[at (di - 1, gi - 1)]; step = 0; }
            if (di > 0 && inBand (di - 1, gi) && acc[at (di - 1, gi)] + 0.3 * c < best)               { best = acc[at (di - 1, gi)] + 0.3 * c; step = 1; }
            if (gi > 0 && inBand (di, gi - 1) && acc[at (di, gi - 1)] + 0.3 * c < best)               { best = acc[at (di, gi - 1)] + 0.3 * c; step = 2; }
            if (best >= inf)
                continue;
            acc[at (di, gi)] = best + c;
            from[at (di, gi)] = step;
        }

    // The best end in the last dub frame, then back along the path: the guide frame each dub frame matched
    int gi = -1;
    double bestEnd = inf;
    for (int g2 = lo (nd - 1); g2 <= hi (nd - 1); ++g2)
        if (acc[at (nd - 1, g2)] < bestEnd)
        {
            bestEnd = acc[at (nd - 1, g2)];
            gi = g2;
        }
    if (gi < 0)
        return {};
    std::vector<double> matched ((size_t) nd, -1.0);
    for (int di = nd - 1; di >= 0 && gi >= 0;)
    {
        matched[(size_t) di] = matched[(size_t) di] < 0.0 ? gi : 0.5 * (matched[(size_t) di] + gi);
        const auto s = from[at (di, gi)];
        if (s == 0)      { --di; --gi; }
        else if (s == 1) { --di; }
        else if (s == 2) { --gi; }
        else break;
    }

    // Smoothed (median of 9), and only forwards: where each dub moment goes, as markers every 50 ms
    std::vector<double> target ((size_t) nd);
    for (int di = 0; di < nd; ++di)
    {
        std::vector<double> near;
        for (int k = juce::jmax (0, di - 4); k <= juce::jmin (nd - 1, di + 4); ++k)
            if (matched[(size_t) k] >= 0.0)
                near.push_back (matched[(size_t) k] - (k - di));
        std::nth_element (near.begin(), near.begin() + (long) near.size() / 2, near.end());
        target[(size_t) di] = near.empty() ? di + offsetFrames : near[near.size() / 2];
    }
    const auto dubLength = dub.getNumSamples() / dubRate;
    std::vector<std::pair<double, double>> markers;
    double lastTo = 0.0, lastFrom = 0.0;
    for (int di = 5; di < nd - 5; di += 5)
    {
        const auto fromSeconds = di * hop;
        const auto toSeconds = fromSeconds + strength * ((target[(size_t) di] - offsetFrames) * hop - fromSeconds);
        if (fromSeconds <= 0.02 || fromSeconds >= dubLength - 0.02 || toSeconds <= lastTo + 0.004 || fromSeconds <= lastFrom + 0.004)
            continue;
        markers.push_back ({ fromSeconds, toSeconds });
        lastTo = toSeconds;
        lastFrom = fromSeconds;
    }
    return markers;
}

} // namespace riff::audioedit
