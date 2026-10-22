#include "PreampSaturation.h"

namespace riff
{

PreampSaturator::Settings PreampSaturator::fromTrack (const juce::ValueTree& track)
{
    Settings s;
    s.enabled = track.getProperty ("preOn", false);
    s.gainDb = juce::jlimit (0.0f, 30.0f, (float) (double) track.getProperty ("preGain", 0.0));
    return s;
}

void PreampSaturator::prepare (double newRate)
{
    sampleRate = newRate > 0.0 ? newRate : 48000.0;
    valid = false;
    update();
    reset();
}

void PreampSaturator::reset() noexcept
{
    for (auto* f : { &lowsIn, &lowsOut, &lift, &top, &dcBlock })
        f->reset();
    previous = {};
    previousIntegral = {};
}

void PreampSaturator::setSettings (const Settings& s) noexcept
{
    if (s == settings && valid)
        return;
    settings = s;
    update();
}

void PreampSaturator::update() noexcept
{
    const auto drive = settings.gainDb / 30.0f;   // 0..1
    gain = juce::Decibels::decibelsToGain (settings.gainDb);
    // The input transformer: its core saturates first in the lows - they meet the clipping 4 dB hotter, and are
    // taken back down after, so what's left of them is their harmonics
    lowsIn.setLowShelf (sampleRate, 150.0, 4.0, 0.6);
    lowsOut.setLowShelf (sampleRate, 150.0, -4.0, 0.6);
    // The transformers' low-end lift (a little more when driven) and their soft top
    lift.setPeak (sampleRate, 70.0, 0.6 + 0.6 * drive, 0.8);
    top.setLowPass (sampleRate, std::min (24000.0, sampleRate * 0.45), 0.55);
    dcBlock.setFirstOrderHighPass (sampleRate, 8.0);
    // Class A: lopsided, so even harmonics come with the odd
    bias = 0.12f + 0.1f * drive;
    biasOut = std::tanh (bias);
    valid = true;
}

/** Soft clipping towards the headroom (tanh, its antiderivative anti-aliasing it), lopsided by the bias. */
float PreampSaturator::clip (float x, int c) noexcept
{
    const auto u = x / headroom + bias;
    const auto au = std::abs (u);
    const auto integral = au > 12.0f ? au - 0.69314718f : std::log (std::cosh (u));
    auto& prev = previous[(size_t) c];
    auto& prevIntegral = previousIntegral[(size_t) c];
    const auto y = std::abs (u - prev) < 1.0e-4f ? std::tanh (0.5f * (u + prev)) : (integral - prevIntegral) / (u - prev);
    prev = u;
    prevIntegral = integral;
    // back to the signal's scale, with the slope at zero 1 (so quiet signals pass at the gain set)
    const auto slope = 1.0f - biasOut * biasOut;
    return (y - biasOut) * headroom / slope;
}

void PreampSaturator::process (float* left, float* right, int numSamples) noexcept
{
    if (! settings.enabled)
        return;
    float* channels[] { left, right };
    for (int c = 0; c < 2; ++c)
    {
        auto* x = channels[c];
        for (int i = 0; i < numSamples; ++i)
        {
            auto v = lowsIn.process (x[i] * gain, c);
            v = clip (v, c);
            v = lowsOut.process (v, c);
            v = lift.process (v, c);
            v = top.process (v, c);
            x[i] = dcBlock.process (v, c);
        }
    }
}

} // namespace riff
