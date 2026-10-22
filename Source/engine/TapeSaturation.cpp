#include "TapeSaturation.h"

namespace riff
{

TapeSaturator::Settings TapeSaturator::fromTrack (const juce::ValueTree& track)
{
    Settings s;
    s.enabled = track.getProperty ("tapeOn", false);
    s.drive = juce::jlimit (0.0f, 1.0f, (float) (double) track.getProperty ("tapeDrive", 35.0) * 0.01f);
    s.speed = juce::jlimit (0, 1, (int) track.getProperty ("tapeSpeed", 1));
    return s;
}

TapeSaturator::Settings TapeSaturator::fromProject (const juce::ValueTree& project)
{
    Settings s;
    s.enabled = project.getProperty ("masterTapeOn", false);
    s.drive = juce::jlimit (0.0f, 1.0f, (float) (double) project.getProperty ("masterTapeDrive", 35.0) * 0.01f);
    s.speed = juce::jlimit (0, 1, (int) project.getProperty ("masterTapeSpeed", 1));
    return s;
}

void TapeSaturator::prepare (double newRate)
{
    sampleRate = newRate > 0.0 ? newRate : 48000.0;
    valid = false;
    update();
    reset();
}

void TapeSaturator::reset() noexcept
{
    for (auto* f : { &preEmphasis, &deEmphasis, &headBump, &bumpDip, &topLoss })
        f->reset();
    previous = {};
    previousIntegral = {};
}

void TapeSaturator::setSettings (const Settings& s) noexcept
{
    if (s == settings && valid)
        return;
    settings = s;
    update();
}

void TapeSaturator::update() noexcept
{
    const auto slow = settings.speed == 0;
    // The record and playback equalisation: the top lifted into the tape and taken out after
    const auto emphasisDb = slow ? 9.0 : 6.0;
    preEmphasis.setHighShelf (sampleRate, slow ? 2500.0 : 4000.0, emphasisDb, 0.6);
    deEmphasis.setHighShelf (sampleRate, slow ? 2500.0 : 4000.0, -emphasisDb, 0.6);
    // The head bump: a lift in the lows where the head's gap meets the wavelength, a dip above it
    headBump.setPeak (sampleRate, slow ? 55.0 : 100.0, slow ? 2.2 : 1.6, 1.1);
    bumpDip.setPeak (sampleRate, slow ? 130.0 : 230.0, -0.8, 1.4);
    // The very top rolls off (sooner at the slower speed)
    topLoss.setLowPass (sampleRate, std::min (slow ? 16000.0 : 21000.0, sampleRate * 0.45), 0.6);
    // Drive: up to 18 dB into the tape; the output takes it back off so only the peaks change
    inGain = juce::Decibels::decibelsToGain (settings.drive * 18.0f);
    outGain = 1.0f / inGain;
    bias = 0.06f * settings.drive;   // (a little asymmetry: even harmonics, as tape's)
    valid = true;
}

/** Soft magnetic saturation, x / (1 + |x|)-like but rounder (tanh), anti-aliased by its antiderivative. */
float TapeSaturator::saturate (float x, float& prev, float& prevIntegral) const noexcept
{
    const auto u = x + bias;
    // the antiderivative of tanh: log cosh (computed safely for large values)
    const auto au = std::abs (u);
    const auto integral = au > 12.0f ? au - 0.69314718f : std::log (std::cosh (u));
    float y;
    if (std::abs (u - prev) < 1.0e-4f)
        y = std::tanh (0.5f * (u + prev));
    else
        y = (integral - prevIntegral) / (u - prev);
    prev = u;
    prevIntegral = integral;
    return y - std::tanh (bias);
}

void TapeSaturator::process (float* left, float* right, int numSamples) noexcept
{
    if (! settings.enabled)
        return;
    float* channels[] { left, right };
    for (int c = 0; c < 2; ++c)
    {
        auto* x = channels[c];
        for (int i = 0; i < numSamples; ++i)
        {
            auto v = preEmphasis.process (x[i] * inGain, c);
            v = saturate (v, previous[(size_t) c], previousIntegral[(size_t) c]);
            v = deEmphasis.process (v, c);
            v = bumpDip.process (headBump.process (v, c), c);
            x[i] = topLoss.process (v, c) * outGain;
        }
    }
}

double TapeSaturator::responseDb (const Settings& s, double frequency, double rate)
{
    TapeSaturator t;
    t.sampleRate = rate;
    t.settings = s;
    t.update();
    return t.headBump.magnitudeDb (frequency, rate) + t.bumpDip.magnitudeDb (frequency, rate) + t.topLoss.magnitudeDb (frequency, rate);
}

} // namespace riff
