#include "KeyDetect.h"

namespace riff::keydetect
{
namespace
{
    constexpr double majorProfile[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr double minorProfile[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double correlation (const Chroma& c, const double* profile, int root)
    {
        double mc = 0.0, mp = 0.0;
        for (int i = 0; i < 12; ++i) { mc += c[(size_t) i]; mp += profile[i]; }
        mc /= 12.0; mp /= 12.0;
        double num = 0.0, dc = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const auto x = c[(size_t) ((i + root) % 12)] - mc, y = profile[i] - mp;
            num += x * y; dc += x * x; dp += y * y;
        }
        return dc > 0.0 && dp > 0.0 ? num / std::sqrt (dc * dp) : 0.0;
    }
}

Chroma chromaOf (const juce::AudioBuffer<float>& audio, double rate)
{
    Chroma total {};
    if (audio.getNumSamples() == 0 || rate <= 0.0)
        return total;

    constexpr int order = 13, size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) size), data ((size_t) size * 2);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

    const auto channels = audio.getNumChannels(), length = audio.getNumSamples();
    const auto binHz = rate / size;
    const auto lowBin = (int) std::ceil (80.0 / binHz), highBin = juce::jmin (size / 2 - 2, (int) (5000.0 / binHz));

    // Each frame's spectral peaks: their pitch (fractional MIDI note) and strength
    struct Peak { double midi; float weight; };
    std::vector<std::vector<Peak>> frames;
    for (int at = 0; at + size <= juce::jmax (size, length); at += hop)
    {
        std::fill (data.begin(), data.end(), 0.0f);
        double energy = 0.0;
        for (int i = 0; i < size && at + i < length; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < channels; ++c)
                v += audio.getSample (c, at + i);
            v /= (float) channels;
            energy += (double) v * v;
            data[(size_t) i] = v * window[(size_t) i];
        }
        if (energy / size < 1.0e-7)   // (silence: below about -70 dB)
        {
            if (at + size >= length) break;
            continue;
        }
        fft.performFrequencyOnlyForwardTransform (data.data(), true);
        float loudest = 0.0f;
        for (int k = lowBin; k <= highBin; ++k)
            loudest = juce::jmax (loudest, data[(size_t) k]);
        std::vector<Peak> peaks;
        for (int k = lowBin; k <= highBin; ++k)
        {
            const auto m = data[(size_t) k];
            if (m < loudest * 0.01f || m <= data[(size_t) k - 1] || m < data[(size_t) k + 1])
                continue;
            // where the peak really is, between bins (a parabola through the log magnitudes)
            const auto a = std::log (data[(size_t) k - 1] + 1.0e-9f), b = std::log (m + 1.0e-9f), c = std::log (data[(size_t) k + 1] + 1.0e-9f);
            const auto denom = a - 2.0f * b + c;
            const auto offset = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
            const auto hz = ((double) k + juce::jlimit (-0.5, 0.5, (double) offset)) * binHz;
            peaks.push_back ({ 69.0 + 12.0 * std::log2 (hz / 440.0), std::sqrt (m / loudest) });
        }
        frames.push_back (std::move (peaks));
        if (at + size >= length) break;
    }

    // The recording's tuning: the peaks' average offset from the semitone (on a circle, so +49 and -49 cents agree)
    double sx = 0.0, sy = 0.0;
    for (const auto& f : frames)
        for (const auto& p : f)
        {
            const auto angle = juce::MathConstants<double>::twoPi * (p.midi - std::round (p.midi));
            sx += p.weight * std::cos (angle);
            sy += p.weight * std::sin (angle);
        }
    const auto tuning = std::atan2 (sy, sx) / juce::MathConstants<double>::twoPi;   // semitones, -0.5..0.5

    for (const auto& f : frames)
    {
        Chroma frame {};
        double sum = 0.0;
        for (const auto& p : f)
        {
            const auto m = p.midi - tuning;
            const auto nearest = std::round (m);
            const auto closeness = 1.0 - 2.0 * std::abs (m - nearest);   // 1 on the note, 0 halfway between
            if (closeness <= 0.0)
                continue;
            const auto w = p.weight * closeness;
            frame[(size_t) (((int) nearest % 12 + 12) % 12)] += w;
            sum += w;
        }
        if (sum > 0.0)
            for (int i = 0; i < 12; ++i)
                total[(size_t) i] += frame[(size_t) i] / sum;
    }
    return total;
}

Result detect (const Chroma& chroma)
{
    Result r;
    double sum = 0.0;
    for (auto v : chroma) sum += v;
    if (sum <= 0.0)
        return r;
    for (int root = 0; root < 12; ++root)
    {
        r.ranking.push_back ({ root, false, correlation (chroma, majorProfile, root) });
        r.ranking.push_back ({ root, true, correlation (chroma, minorProfile, root) });
    }
    std::sort (r.ranking.begin(), r.ranking.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
    const auto& best = r.ranking.front();
    r.root = best.root;
    r.minor = best.minor;
    r.score = best.score;
    r.found = best.score > 0.3;
    // Sure: well ahead of the next key that isn't its relative (which shares its notes)
    const auto relative = best.minor ? (best.root + 3) % 12 : (best.root + 9) % 12;
    for (size_t i = 1; i < r.ranking.size(); ++i)
        if (! (r.ranking[i].root == relative && r.ranking[i].minor != best.minor))
        {
            r.confidence = juce::jlimit (0.0, 1.0, (best.score - r.ranking[i].score) * 5.0);
            break;
        }
    return r;
}

juce::String name (int root, bool minor)
{
    static const char* sharps[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    root = (root % 12 + 12) % 12;
    // the usual spelling: flats for F, Bb, Eb, Ab, Db major and D, G, C, F, Bb minor
    const auto useFlats = minor ? (root == 2 || root == 7 || root == 0 || root == 5 || root == 10 || root == 3)
                                : (root == 5 || root == 10 || root == 3 || root == 8 || root == 1 || root == 6);
    return juce::String ((useFlats ? flats : sharps)[root]) + (minor ? " minor" : " major");
}
}
