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
    s.color       = project.getProperty ("busCompColor", s.color);
    s.grit        = juce::jlimit (0.0f, 1.0f, (float) (double) project.getProperty ("busCompGrit", s.grit));
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
    // (the output transformer: a little weight in the lows, the very top eased)
    transformerLow.setPeak (sampleRate, 70.0, 0.8, 0.8);
    transformerTop.setLowPass (sampleRate, std::min (24000.0, sampleRate * 0.45), 0.6);
    reset();
}

void MasterBusCompressor::reset() noexcept
{
    envelopeDb = 0.0f;
    sustained = 0.0f;
    wet = settings.enabled ? 1.0f : 0.0f;
    colorAmount = settings.enabled && settings.color ? 1.0f : 0.0f;
    satPrev = {};
    satPrevIntegral = {};
    transformerLow.reset();
    transformerTop.reset();
    meterGainReduction.store (0.0f);
}

float MasterBusCompressor::colorStage (float x, int channel, float drive, float bias, float grit) noexcept
{
    // Into the VCA and transformer: tanh with a bias (even harmonics), anti-aliased by its antiderivative
    const auto u = x * drive + bias;
    const auto au = std::abs (u);
    const auto integral = au > 12.0f ? au - 0.69314718f : std::log (std::cosh (u));
    auto& prev = satPrev[(size_t) channel];
    auto& prevIntegral = satPrevIntegral[(size_t) channel];
    float y = std::abs (u - prev) < 1.0e-4f ? std::tanh (0.5f * (u + prev)) : (integral - prevIntegral) / (u - prev);
    prev = u;
    prevIntegral = integral;
    y -= std::tanh (bias);
    // Grit: odd harmonics on top, the sound of the stage being pushed (a squarer curve, blended in)
    if (grit > 0.0f)
    {
        const auto hard = y * (1.5f - 0.5f * y * y);   // (a cubic: firmer than tanh near the top)
        y += (juce::jlimit (-1.0f, 1.0f, hard) - y) * grit;
    }
    y = transformerTop.process (transformerLow.process (y, channel), channel);
    return y / drive;
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
    const auto colorTarget = s.enabled && s.color ? 1.0f : 0.0f;
    // Colour alone: about 4 dB into the stage; Grit drives it up to 16 dB harder
    const auto drive = juce::Decibels::decibelsToGain (4.0f + s.grit * 12.0f);
    const auto bias = 0.12f;

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

        auto outL = l * g, outR = r * g;
        colorAmount = colorTarget > colorAmount ? juce::jmin (colorTarget, colorAmount + wetStep) : juce::jmax (colorTarget, colorAmount - wetStep);
        if (colorAmount > 0.0f)
        {
            const auto cl = colorStage (outL, 0, drive, bias, s.grit), cr = colorStage (outR, 1, drive, bias, s.grit);
            outL += (cl - outL) * colorAmount;
            outR += (cr - outR) * colorAmount;
        }
        left[i] = outL;
        right[i] = outR;

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
