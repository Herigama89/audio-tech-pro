#include "MasterBusCompressor.h"
#include "../model/Identifiers.h"

namespace riff
{

MasterBusCompressor::Settings MasterBusCompressor::fromProject (const juce::ValueTree& project)
{
    Settings s;
    s.enabled     = project.getProperty (ids::busCompOn, s.enabled);
    s.thresholdDb = juce::jlimit (minThresholdDb, maxThresholdDb, (float) project.getProperty (ids::busCompThreshold, s.thresholdDb));
    s.ratio       = juce::jlimit (0, (int) ratios.size() - 1,    (int) project.getProperty (ids::busCompRatio, s.ratio));
    s.attack      = juce::jlimit (0, (int) attacksMs.size() - 1, (int) project.getProperty (ids::busCompAttack, s.attack));
    s.release     = juce::jlimit (0, (int) releasesS.size() - 1, (int) project.getProperty (ids::busCompRelease, s.release));
    s.makeupDb    = juce::jlimit (0.0f, maxMakeupDb, (float) project.getProperty (ids::busCompMakeup, s.makeupDb));
    s.mix         = juce::jlimit (0.0f, 1.0f, (float) project.getProperty (ids::busCompMix, s.mix));
    return s;
}

void MasterBusCompressor::setInProject (juce::ValueTree project, const juce::Identifier& name, const juce::var& value, juce::UndoManager* um)
{
    project.setProperty (name, value, um);
}

void MasterBusCompressor::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    wetStep = (float) (1.0 / (0.02 * sampleRate));   // the IN switch fades over 20 ms
    reset();
}

void MasterBusCompressor::reset() noexcept
{
    envelopeDb = 0.0f;
    sustained = 0.0f;
    wet = settings.enabled ? 1.0f : 0.0f;
    meterGainReduction.store (0.0f);
}

float MasterBusCompressor::coefficient (float seconds) const noexcept
{
    return seconds <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (seconds * sampleRate));
}

void MasterBusCompressor::process (float* left, float* right, int numSamples) noexcept
{
    const auto s = settings;
    const auto ratio = ratios[(size_t) s.ratio];
    const auto slope = 1.0f - 1.0f / ratio;
    constexpr float knee = 4.0f;   // dB, a gentle knee

    const auto attack = coefficient (attacksMs[(size_t) s.attack] * 0.001f);
    const bool autoMode = s.release == autoRelease;
    const auto releaseFixed = coefficient (releasesS[(size_t) s.release]);
    const auto releaseFast = coefficient (0.1f), releaseSlow = coefficient (1.2f);
    const auto sustainCoef = coefficient (1.0f);
    const auto makeup = juce::Decibels::decibelsToGain (s.makeupDb);
    const auto wetTarget = s.enabled ? 1.0f : 0.0f;

    float peakReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = left[i], r = right[i];

        // Stereo-linked detector, so the image doesn't shift.
        const auto level = juce::jmax (std::abs (l), std::abs (r));
        const auto levelDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;
        const auto over = levelDb - s.thresholdDb;

        float target = 0.0f;
        if (over >= knee * 0.5f)
            target = over * slope;
        else if (over > -knee * 0.5f)
            target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);

        float release = releaseFixed;
        if (autoMode)
        {
            // Short peaks let go fast; long stretches of compression let go slowly.
            sustained = envelopeDb + sustainCoef * (sustained - envelopeDb);
            const auto w = juce::jlimit (0.0f, 1.0f, sustained / 6.0f);
            release = releaseFast + (releaseSlow - releaseFast) * w;
        }

        const auto coef = target > envelopeDb ? attack : release;
        envelopeDb = target + coef * (envelopeDb - target);

        wet = wetTarget > wet ? juce::jmin (wetTarget, wet + wetStep) : juce::jmax (wetTarget, wet - wetStep);

        const auto gain = juce::Decibels::decibelsToGain (-envelopeDb) * makeup;
        const auto amount = wet * s.mix;              // how much of the compressed signal
        const auto g = 1.0f + amount * (gain - 1.0f);

        left[i] = l * g;
        right[i] = r * g;

        peakReduction = juce::jmax (peakReduction, envelopeDb * wet);
    }

    // Hold the largest reduction until the meter reads it.
    auto previous = meterGainReduction.load (std::memory_order_relaxed);
    while (peakReduction > previous
           && ! meterGainReduction.compare_exchange_weak (previous, peakReduction, std::memory_order_relaxed)) {}
}

juce::String MasterBusCompressor::ratioText (int index)
{
    return juce::String ((int) ratios[(size_t) juce::jlimit (0, (int) ratios.size() - 1, index)]);
}

juce::String MasterBusCompressor::attackText (int index)
{
    const auto ms = attacksMs[(size_t) juce::jlimit (0, (int) attacksMs.size() - 1, index)];
    return ms < 1.0f ? juce::String (ms, 1) : juce::String ((int) ms);
}

juce::String MasterBusCompressor::releaseText (int index)
{
    index = juce::jlimit (0, (int) releasesS.size() - 1, index);
    return index == autoRelease ? juce::String ("Auto") : juce::String (releasesS[(size_t) index], 1);
}

} // namespace riff
