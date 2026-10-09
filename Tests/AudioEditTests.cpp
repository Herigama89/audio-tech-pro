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
