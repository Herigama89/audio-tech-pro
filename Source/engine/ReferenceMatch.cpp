#include "ReferenceMatch.h"

namespace riff::reference
{
namespace
{
    double bandFrequency (int k) noexcept { return 1000.0 * std::pow (2.0, (k - 17) / 3.0); }
}

void Analyser::prepare (double rate)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    for (int k = 0; k < bands; ++k)
    {
        filters[(size_t) k].setBandPass (sampleRate, std::min (bandFrequency (k), sampleRate * 0.45), 4.32);
        filters[(size_t) k].reset();
    }
    energy.fill (0.0);
    samples = mid = side = 0.0;
    peak = 0.0f;
    loudness.prepare (sampleRate);
}

void Analyser::add (const float* left, const float* right, int n) noexcept
{
    if (right == nullptr)
        right = left;
    for (int i = 0; i < n; ++i)
    {
        const auto l = left[i], r = right[i];
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        const auto m = 0.5f * (l + r), s = 0.5f * (l - r);
        mid += (double) m * m;
        side += (double) s * s;
        for (size_t k = 0; k < (size_t) bands; ++k)
        {
            const auto y = filters[k].process (m, 0);
            energy[k] += (double) y * y;
        }
    }
    samples += n;
    loudness.process (left, right, n);
}

Profile Analyser::result() const
{
    Profile p;
    p.seconds = samples / sampleRate;
    if (samples < 1.0)
        return p;
    for (int k = 0; k < bands; ++k)
        p.bandDb[(size_t) k] = (float) juce::jmax (-118.0, 10.0 * std::log10 (energy[(size_t) k] / samples + 1.0e-14));
    const auto integrated = loudness.integrated();
    p.lufs = std::isfinite (integrated) ? integrated : -100.0;
    p.peakDb = juce::Decibels::gainToDecibels ((double) peak, -100.0);
    p.sideDb = mid > 1.0e-12 ? 10.0 * std::log10 ((side + 1.0e-14) / mid) : -100.0;
    return p;
}

Profile analyseFile (const juce::File& file, juce::String& error, std::function<bool()> keepGoing)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Couldn't read " + file.getFileName() + " (try a WAV, AIFF, FLAC or MP3).";
        return {};
    }
    Analyser analyser;
    analyser.prepare (reader->sampleRate);
    juce::AudioBuffer<float> block ((int) juce::jmax (1u, juce::jmin (2u, reader->numChannels)), 8192);
    for (juce::int64 at = 0; at < reader->lengthInSamples; at += block.getNumSamples())
    {
        if (keepGoing && ! keepGoing())
            return {};
        const auto n = (int) juce::jmin ((juce::int64) block.getNumSamples(), reader->lengthInSamples - at);
        reader->read (&block, 0, n, at, true, true);
        analyser.add (block.getReadPointer (0), block.getReadPointer (block.getNumChannels() > 1 ? 1 : 0), n);
    }
    const auto profile = analyser.result();
    if (! profile.valid())
        error = file.getFileName() + " is too short or too quiet to match.";
    return profile;
}

std::vector<Step> plan (const Profile& mix, const Profile& ref)
{
    std::vector<Step> steps;
    if (! mix.valid() || ! ref.valid())
        return steps;

    // Tone: Match EQ with the reference as its template and the mix as its current material (both already learned)
    {
        Step eq { "matchEq", { { "apply", 80.0f }, { "smoothing", 40.0f }, { "analyzer", 1.0f } }, {} };
        for (int k = 0; k < bands; ++k)
        {
            eq.params.push_back ({ "t" + juce::String (k + 1), ref.bandDb[(size_t) k] });
            eq.params.push_back ({ "c" + juce::String (k + 1), mix.bandDb[(size_t) k] });
        }
        // (what it changes most, in words)
        double meanR = 0.0, meanM = 0.0;
        for (int k = 3; k < 29; ++k) { meanR += ref.bandDb[(size_t) k]; meanM += mix.bandDb[(size_t) k]; }
        meanR /= 26.0; meanM /= 26.0;
        const auto region = [&] (int from, int to)
        {
            double d = 0.0;
            for (int k = from; k <= to; ++k)
                d += (ref.bandDb[(size_t) k] - meanR) - (mix.bandDb[(size_t) k] - meanM);
            return d / (to - from + 1) * 0.8;
        };
        const auto lows = region (3, 12), mids = region (13, 21), highs = region (22, 29);
        const auto word = [] (double db) { return (db >= 0.0 ? "+" : "") + juce::String (db, 1) + " dB"; };
        eq.why = "Match EQ: the reference's tone (lows " + word (lows) + ", mids " + word (mids) + ", highs " + word (highs) + ")";
        steps.push_back (eq);
    }

    // Density: the mix's peaks further above its loudness than the reference's - bus compression takes part of that
    const auto denser = mix.density() - ref.density();   // LU the mix has to lose
    float compressionDb = 0.0f;
    if (denser > 1.5)
    {
        compressionDb = (float) juce::jlimit (1.0, 5.0, denser * 0.35);
        const auto ratio = denser > 8.0 ? 4.0f : 2.0f;
        const auto loud = (float) mix.lufs + 4.0f;   // (where its loud passages sit, RMS-ish)
        const auto threshold = juce::jlimit (-50.0f, -2.0f, loud - compressionDb / (1.0f - 1.0f / ratio));
        steps.push_back ({ "studioComp",
                           { { "model", 3.0f }, { "threshold", threshold }, { "ratio", ratio }, { "attack", 10.0f }, { "release", 200.0f },
                             { "autoRelease", 1.0f }, { "makeup", compressionDb * 0.5f }, { "mix", 100.0f } },
                           "Bus compression (Classic VCA, " + juce::String (ratio, 0) + ":1): about " + juce::String (compressionDb, 1)
                               + " dB on the loud parts, for the reference's density" });
    }

    // Width: more or less in the sides than the reference
    if (const auto wider = ref.sideDb - mix.sideDb; std::abs (wider) > 1.5 && mix.sideDb > -60.0 && ref.sideDb > -60.0)
    {
        const auto spread = (float) juce::jlimit (0.6, 1.6, 1.0 + juce::jlimit (-8.0, 8.0, wider) / 14.0);
        steps.push_back ({ "directionMixer", { { "input", 0.0f }, { "spread", spread } },
                           juce::String (wider > 0.0 ? "Wider" : "Narrower") + " stereo (Direction Mixer spread " + juce::String (spread, 2)
                               + "), as wide as the reference" });
    }

    // Loudness: up to the reference's level, its peaks held at its ceiling
    {
        const auto after = mix.lufs - compressionDb * 0.5;   // (about where the mix sits after the compression)
        const auto gain = (float) juce::jlimit (0.0, 24.0, ref.lufs - after);
        const auto ceiling = (float) juce::jlimit (-3.0, -0.3, std::round ((ref.peakDb - 0.05) * 10.0) / 10.0);
        steps.push_back ({ "adaptiveLimiter", { { "gain", gain }, { "ceiling", ceiling }, { "truePeak", 1.0f } },
                           "Adaptive Limiter: +" + juce::String (gain, 1) + " dB to the reference's loudness ("
                               + juce::String (ref.lufs, 1) + " LUFS), peaks held at " + juce::String (ceiling, 1) + " dB" });
    }
    return steps;
}
}
