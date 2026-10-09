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
    s.enabled  = (bool) track.getProperty (ids::consoleIn, true) && (bool) track.getProperty (ids::stripIn, true);
    s.dynEnabled = track.getProperty (ids::consoleDynIn, true);
    s.hf       = gain (ids::consoleHF);
    s.hmf      = gain (ids::consoleHMF);
    s.lmf      = gain (ids::consoleLMF);
    s.lf       = gain (ids::consoleLF);
    s.dynamics = juce::jlimit (0.0f, maxDynamics, (float) track.getProperty (ids::consoleDynamics, 0.0));

    auto value = [&track] (const juce::Identifier& id, float fallback, float lo, float hi)
    {
        return juce::jlimit (lo, hi, (float) (double) track.getProperty (id, fallback));
    };
    s.inputDb  = value (ids::consoleInput, 0.0f, -20.0f, 20.0f);
    s.outputDb = value (ids::consoleOutput, 0.0f, -20.0f, 20.0f);
    s.hpf      = value (ids::consoleHPF, 0.0f, 0.0f, 400.0f);
    s.lpf      = value (ids::consoleLPF, 0.0f, 0.0f, 22000.0f);
    s.hfFreq   = value (ids::consoleHFFreq, 8000.0f, 1500.0f, 16000.0f);
    s.hmfFreq  = value (ids::consoleHMFFreq, 2500.0f, 600.0f, 7000.0f);
    s.lmfFreq  = value (ids::consoleLMFFreq, 400.0f, 200.0f, 2500.0f);
    s.lfFreq   = value (ids::consoleLFFreq, 100.0f, 30.0f, 450.0f);
    s.hmfQ     = value (ids::consoleHMFQ, 1.0f, 0.1f, 4.0f);
    s.lmfQ     = value (ids::consoleLMFQ, 1.0f, 0.1f, 4.0f);
    s.hfBell   = track.getProperty (ids::consoleHFBell, false);
    s.lfBell   = track.getProperty (ids::consoleLFBell, false);
    s.compRatio   = value (ids::consoleCompRatio, 0.0f, 0.0f, 20.0f);
    s.compRelease = value (ids::consoleCompRelease, 0.15f, 0.05f, 4.0f);
    s.compFast    = track.getProperty (ids::consoleCompFast, false);
    s.gateThreshold = value (ids::consoleGateThreshold, -80.0f, -80.0f, 0.0f);
    s.gateRange     = value (ids::consoleGateRange, 20.0f, 0.0f, 40.0f);
    s.gateRelease   = value (ids::consoleGateRelease, 0.25f, 0.05f, 4.0f);
    s.gateExpander  = track.getProperty (ids::consoleGateExpander, false);
    return s;
}

double ConsoleChannel::responseDb (const Settings& s, double frequency, double rate)
{
    ConsoleChannel c;
    c.sampleRate = rate;
    c.settings = s;
    c.updateFilters();

    double db = s.inputDb + s.outputDb;
    if (std::abs (s.hf) > 0.01f)  db += c.hfFilter.magnitudeDb (frequency, rate);
    if (std::abs (s.hmf) > 0.01f) db += c.hmfFilter.magnitudeDb (frequency, rate);
    if (std::abs (s.lmf) > 0.01f) db += c.lmfFilter.magnitudeDb (frequency, rate);
    if (std::abs (s.lf) > 0.01f)  db += c.lfFilter.magnitudeDb (frequency, rate);
    if (s.hpf > 0.0f) db += c.hpf1.magnitudeDb (frequency, rate) + c.hpf2.magnitudeDb (frequency, rate);
    if (s.lpf > 0.0f) db += c.lpf1.magnitudeDb (frequency, rate);
    return db;
}

void ConsoleChannel::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    filtersValid = false;
    reset();
}

void ConsoleChannel::reset() noexcept
{
    for (auto* f : { &hfFilter, &hmfFilter, &lmfFilter, &lfFilter, &hpf1, &hpf2, &lpf1 })
        f->reset();
    envelopeDb = 0.0f;
    gateGainDb = 0.0f;
}

void ConsoleChannel::setSettings (const Settings& s) noexcept
{
    auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-6f; };
    const bool eqChanged = ! filtersValid
                        || differs (s.hf, settings.hf) || differs (s.hmf, settings.hmf)
                        || differs (s.lmf, settings.lmf) || differs (s.lf, settings.lf)
                        || differs (s.hfFreq, settings.hfFreq) || differs (s.hmfFreq, settings.hmfFreq)
                        || differs (s.lmfFreq, settings.lmfFreq) || differs (s.lfFreq, settings.lfFreq)
                        || differs (s.hmfQ, settings.hmfQ) || differs (s.lmfQ, settings.lmfQ)
                        || s.hfBell != settings.hfBell || s.lfBell != settings.lfBell
                        || differs (s.hpf, settings.hpf) || differs (s.lpf, settings.lpf);
    settings = s;

    if (eqChanged)
        updateFilters();
}

void ConsoleChannel::updateFilters() noexcept
{
    if (settings.hfBell) hfFilter.setPeak (sampleRate, settings.hfFreq, settings.hf, 0.9);
    else                 hfFilter.setHighShelf (sampleRate, settings.hfFreq, settings.hf);
    hmfFilter.setPeak (sampleRate, settings.hmfFreq, settings.hmf, settings.hmfQ);
    lmfFilter.setPeak (sampleRate, settings.lmfFreq, settings.lmf, settings.lmfQ);
    if (settings.lfBell) lfFilter.setPeak (sampleRate, settings.lfFreq, settings.lf, 0.9);
    else                 lfFilter.setLowShelf (sampleRate, settings.lfFreq, settings.lf);
    // 18 dB/octave high-pass (as the console's), 12 dB/octave low-pass.
    hpf1.setHighPass (sampleRate, juce::jmax (10.0f, settings.hpf), 0.54);
    hpf2.setHighPass (sampleRate, juce::jmax (10.0f, settings.hpf), 1.31);
    lpf1.setLowPass (sampleRate, juce::jmax (1000.0f, settings.lpf), 0.707);
    filtersValid = true;
}

void ConsoleChannel::process (float* left, float* right, int numSamples) noexcept
{
    if (! settings.isActive())
        return;

    auto trim = [&] (float db)
    {
        if (std::abs (db) <= 0.01f)
            return;
        const auto g = juce::Decibels::decibelsToGain (db);
        juce::FloatVectorOperations::multiply (left, g, numSamples);
        juce::FloatVectorOperations::multiply (right, g, numSamples);
    };

    auto run = [&] (Biquad& f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = f.process (left[i], 0);
            right[i] = f.process (right[i], 1);
        }
    };

    trim (settings.inputDb);

    // Filters, then the EQ: only what's switched in or turned away from 0 costs anything.
    if (settings.hpf > 0.0f) { run (hpf1); run (hpf2); }
    if (settings.lpf > 0.0f) run (lpf1);

    if (std::abs (settings.lf) > 0.01f)  run (lfFilter);
    if (std::abs (settings.lmf) > 0.01f) run (lmfFilter);
    if (std::abs (settings.hmf) > 0.01f) run (hmfFilter);
    if (std::abs (settings.hf) > 0.01f)  run (hfFilter);

    // Gate / expander.
    if (settings.dynEnabled && settings.gateOn())
    {
        const auto open = (float) std::exp (-1.0 / (0.0005 * sampleRate));
        const auto close = (float) std::exp (-1.0 / (settings.gateRelease * sampleRate));
        for (int i = 0; i < numSamples; ++i)
        {
            const auto level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
            const auto levelDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;
            const auto below = juce::jmax (0.0f, settings.gateThreshold - levelDb);
            const auto target = -juce::jmin (settings.gateRange, settings.gateExpander ? below : (below > 0.0f ? settings.gateRange : 0.0f));
            gateGainDb = target + (target > gateGainDb ? open : close) * (gateGainDb - target);
            const auto g = juce::Decibels::decibelsToGain (gateGainDb);
            left[i] *= g;
            right[i] *= g;
        }
    }

    // Compressor: the Dynamics knob sets how much (threshold and make-up); the ratio follows it unless set.
    const auto amount = settings.dynEnabled ? settings.dynamics : 0.0f;
    if (amount <= 0.01f)
    {
        envelopeDb = 0.0f;
        trim (settings.outputDb);
        return;
    }

    const auto threshold = -3.0f * amount;               // 0 .. -30 dB
    const auto ratio = settings.compRatio >= 1.0f ? settings.compRatio : 1.5f + 0.35f * amount;   // auto: 1.5:1 .. 5:1
    const auto slope = 1.0f - 1.0f / ratio;
    const auto makeup = juce::Decibels::decibelsToGain (1.2f * amount);
    const auto attack = (float) std::exp (-1.0 / ((settings.compFast ? 0.001 : 0.005) * sampleRate));
    const auto release = (float) std::exp (-1.0 / (juce::jmax (0.01f, settings.compRelease) * sampleRate));

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

    trim (settings.outputDb);
}

} // namespace riff
