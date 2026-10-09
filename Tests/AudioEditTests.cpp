#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/engine/AudioEdit.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::AudioBuffer<float> sine (double freq, double seconds, double rate = 48000.0)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / rate));
        return b;
    }

    /** Frequency from upward zero crossings in the middle of the buffer. */
    double frequencyOf (const juce::AudioBuffer<float>& b, double rate = 48000.0)
    {
        const auto* d = b.getReadPointer (0);
        const int from = b.getNumSamples() / 4, to = b.getNumSamples() * 3 / 4;
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            crossings += (d[i - 1] < 0.0f && d[i] >= 0.0f) ? 1 : 0;
        return crossings / ((to - from) / rate);
    }
}

TEST_CASE ("Time stretch keeps the pitch; pitch shift keeps the length", "[audioedit]")
{
    const auto a = sine (440.0, 1.0);

    const auto longer = audioedit::timeStretch (a, 1.5, 48000.0);
    CHECK (longer.getNumSamples() == 72000);
    CHECK_THAT (frequencyOf (longer), WithinAbs (440.0, 8.0));

    const auto higher = audioedit::pitchShift (a, 12.0, 48000.0);
    CHECK (higher.getNumSamples() == 48000);
    CHECK_THAT (frequencyOf (higher), WithinAbs (880.0, 16.0));
}

TEST_CASE ("Audio File Editor edits", "[audioedit]")
{
    auto a = sine (100.0, 1.0);
    audioedit::apply (a, { audioedit::Operation::normalize }, 48000.0);
    CHECK_THAT (a.getMagnitude (0, 0, a.getNumSamples()), WithinAbs (juce::Decibels::decibelsToGain (-0.1f), 1e-3));

    audioedit::apply (a, { audioedit::Operation::silence, 0.0, 0.25, 0.5 }, 48000.0);
    CHECK (a.getMagnitude (0, 12000, 12000) == 0.0f);
    CHECK (a.getMagnitude (0, 30000, 1000) > 0.5f);

    const auto before = a.getSample (0, 40000);
    audioedit::apply (a, { audioedit::Operation::invert }, 48000.0);
    CHECK (a.getSample (0, 40000) == -before);

    audioedit::apply (a, { audioedit::Operation::fadeIn, 0.0, 0.5, 1.0 }, 48000.0);
    CHECK (std::abs (a.getSample (0, 24000)) < 0.05f);
}

namespace
{
    /** Clicks at the given times (seconds), in a second or so of silence. */
    juce::AudioBuffer<float> clicks (const std::vector<double>& times, double seconds, double rate = 48000.0)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        b.clear();
        for (auto t : times)
            for (int i = 0; i < 2000; ++i)
                if (const auto at = (int) (t * rate) + i; at < b.getNumSamples())
                    b.setSample (0, at, 0.8f * std::exp (-i / 300.0f) * (float) std::sin (i * 0.3));
        return b;
    }
}

TEST_CASE ("Smart Tempo and Flex Time", "[audioedit]")
{
    // 120 BPM: a hit every half second.
    std::vector<double> beats;
    for (int i = 0; i < 16; ++i)
        beats.push_back (i * 0.5);
    const auto steady = clicks (beats, 8.2);
    const auto onsets = audioedit::detectTransients (steady, 48000.0);
    CHECK (onsets.size() == 16);
    CHECK_THAT (audioedit::detectTempo (steady, 48000.0), WithinAbs (120.0, 1.0));

    // Hits a little late: quantized onto the half-second grid.
    const auto sloppy = clicks ({ 0.0, 0.53, 1.04, 1.49 }, 2.2);
    const auto fixed = audioedit::flexQuantize (sloppy, 48000.0, [] (double t) { return std::round (t / 0.5) * 0.5; });
    const auto after = audioedit::detectTransients (fixed, 48000.0);
    REQUIRE (after.size() == 4);
    CHECK (std::abs (after[1] - 24000) < 600);
    CHECK (std::abs (after[2] - 48000) < 600);
}

TEST_CASE ("Flex Pitch: notes and their pitch", "[audioedit]")
{
    CHECK_THAT (audioedit::detectPitch (sine (220.0, 0.5), 48000.0, 12000), WithinAbs (220.0, 2.0));

    // A, then E above it.
    auto a = sine (220.0, 0.5);
    auto e = sine (329.63, 0.5);
    juce::AudioBuffer<float> both (1, 48000);
    both.clear();
    both.copyFrom (0, 0, a, 0, 0, 24000);
    both.copyFrom (0, 24000, e, 0, 0, 24000);
    both.applyGainRamp (0, 0, 200, 0.0f, 1.0f);
    // A gap so the second note has a hit.
    both.clear (0, 23000, 1000);
    const auto notes = audioedit::detectNotes (both, 48000.0);
    REQUIRE (notes.size() >= 2);
    CHECK_THAT (notes.front().midi, WithinAbs (57.0, 0.3));
    CHECK_THAT (notes.back().midi, WithinAbs (64.0, 0.3));

    // Shifting just the second note up two semitones.
    audioedit::apply (both, { audioedit::Operation::pitchShift, 2.0, 0.5, 1.0 }, 48000.0);
    CHECK_THAT (audioedit::detectPitch (both, 48000.0, 36000), WithinAbs (369.99, 6.0));
    CHECK_THAT (audioedit::detectPitch (both, 48000.0, 12000), WithinAbs (220.0, 2.0));
}

TEST_CASE ("Flex markers moved by hand stretch only the audio either side", "[audioedit][flex]")
{
    // Clicks at 0.25, 0.5 and 0.75 s; move the middle one to 0.6 s.
    const double rate = 48000.0;
    juce::AudioBuffer<float> audio (1, 48000);
    audio.clear();
    for (auto t : { 0.25, 0.5, 0.75 })
        for (int i = 0; i < 200; ++i)
            audio.setSample (0, (int) (t * rate) + i, 0.9f * (1.0f - i / 200.0f));

    const auto out = audioedit::flexWarp (audio, rate, { { 0.25, 0.25 }, { 0.5, 0.6 }, { 0.75, 0.75 } });
    REQUIRE (std::abs (out.getNumSamples() - 48000) < 64);   // same length overall

    const auto hits = audioedit::detectTransients (out, rate);
    REQUIRE (hits.size() == 3);
    CHECK (std::abs (hits[0] / rate - 0.25) < 0.01);
    CHECK (std::abs (hits[1] / rate - 0.6) < 0.015);
    CHECK (std::abs (hits[2] / rate - 0.75) < 0.01);
}
