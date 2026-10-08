#include "ConsoleChannel.h"
#include "../model/Identifiers.h"

namespace riff
{

ConsoleChannel::Settings ConsoleChannel::fromTrack (const juce::ValueTree& track)
{
    auto gain = [&track] (const juce::Identifier& id)
    {
        return juce::jlimit (-maxGainDb, maxGainDb, (float) track.getProperty (id, 0.0));
    };

    Settings s;
    s.enabled  = track.getProperty (ids::consoleIn, true);
    s.dynEnabled = track.getProperty (ids::consoleDynIn, true);
    s.hf       = gain (ids::consoleHF);
    s.hmf      = gain (ids::consoleHMF);
    s.lmf      = gain (ids::consoleLMF);
    s.lf       = gain (ids::consoleLF);
    s.dynamics = juce::jlimit (0.0f, maxDynamics, (float) track.getProperty (ids::consoleDynamics, 0.0));
    return s;
}

void ConsoleChannel::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    filtersValid = false;
    reset();
}

void ConsoleChannel::reset() noexcept
{
    for (auto* f : { &hfFilter, &hmfFilter, &lmfFilter, &lfFilter })
        f->reset();
    envelopeDb = 0.0f;
}

void ConsoleChannel::setSettings (const Settings& s) noexcept
{
    auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-6f; };
    const bool eqChanged = ! filtersValid
                        || differs (s.hf, settings.hf) || differs (s.hmf, settings.hmf)
                        || differs (s.lmf, settings.lmf) || differs (s.lf, settings.lf);
    settings = s;

    if (eqChanged)
        updateFilters();
}

void ConsoleChannel::updateFilters() noexcept
{
    hfFilter.setHighShelf (sampleRate, hfFrequency, settings.hf);
    hmfFilter.setPeak (sampleRate, hmfFrequency, settings.hmf, 1.0);
    lmfFilter.setPeak (sampleRate, lmfFrequency, settings.lmf, 1.0);
    lfFilter.setLowShelf (sampleRate, lfFrequency, settings.lf);
    filtersValid = true;
}

void ConsoleChannel::process (float* left, float* right, int numSamples) noexcept
{
    if (! settings.isActive())
        return;

    // EQ: only the bands that are turned away from 0 cost anything.
    auto band = [&] (Biquad& f, float gainDb)
    {
        if (std::abs (gainDb) <= 0.01f)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = f.process (left[i], 0);
            right[i] = f.process (right[i], 1);
        }
    };

    band (lfFilter, settings.lf);
    band (lmfFilter, settings.lmf);
    band (hmfFilter, settings.hmf);
    band (hfFilter, settings.hf);

    // Dynamics: one knob sets threshold, ratio and make-up together.
    const auto amount = settings.dynEnabled ? settings.dynamics : 0.0f;
    if (amount <= 0.01f)
    {
        envelopeDb = 0.0f;
        return;
    }

    const auto threshold = -3.0f * amount;               // 0 .. -30 dB
    const auto slope = 1.0f - 1.0f / (1.5f + 0.35f * amount);   // 1.5:1 .. 5:1
    const auto makeup = juce::Decibels::decibelsToGain (1.2f * amount);
    const auto attack = (float) std::exp (-1.0 / (0.005 * sampleRate));
    const auto release = (float) std::exp (-1.0 / (0.15 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        const auto levelDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;
        const auto target = juce::jmax (0.0f, (levelDb - threshold) * slope);
        envelopeDb = target + (target > envelopeDb ? attack : release) * (envelopeDb - target);

        const auto g = juce::Decibels::decibelsToGain (-envelopeDb) * makeup;
        left[i] *= g;
        right[i] *= g;
    }
}

} // namespace riff
