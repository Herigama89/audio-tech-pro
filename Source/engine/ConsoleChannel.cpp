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
    s.enabled  = track.getProperty (ids::stripIn, true);   // (the strip's power)
    s.eqIn     = track.getProperty (ids::consoleIn, true);
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
    s.hpf = value (ids::consoleHPF, 0.0f, 0.0f, 500.0f);

    const auto on = [&track] (const juce::Identifier& id, bool fallback) { return (bool) track.getProperty (id, fallback); };
    s.pad = on (ids::consolePad, false);
    s.phase = on (ids::consolePhase, false);
    s.line = on (ids::consoleLine, true);
    s.micGain = value (ids::consoleMicGain, 20.0f, 20.0f, 70.0f);
    s.deEss = on (ids::consoleDeEss, false);
    s.deEssThreshold = value (ids::consoleDeEssThreshold, 0.0f, -20.0f, 10.0f);
    s.compKnee = on (ids::consoleCompKnee, false);
    s.compLinear = on (ids::consoleCompLinear, false);
    s.link = on (ids::consoleLink, true);
    s.externalKey = on (ids::consoleExternalKey, false);
    s.gateFast = on (ids::consoleGateFast, false);
    s.filtersToSidechain = on (ids::consoleFiltersToSidechain, false);
    s.filtersAtInput = on (ids::consoleFiltersAtInput, true);
    s.blackEq = on (ids::consoleBlackEq, false);
    s.eqToSidechain = on (ids::consoleEqToSidechain, false);
    s.dynPostEq = on (ids::consoleDynPostEq, true);
    s.deEssPostEq = on (ids::consoleDeEssPostEq, true);
    return s;
}

double ConsoleChannel::responseDb (const Settings& s, double frequency, double rate)
{
    ConsoleChannel c;
    c.sampleRate = rate;
    c.settings = s;
    c.updateFilters();

    double db = s.inputDb + s.outputDb + (s.pad ? -20.0 : 0.0) + (s.line ? 0.0 : s.micGain - 20.0);
    if (! s.eqIn || s.eqToSidechain)
    {
        if (s.filtersActive() && ! s.filtersToSidechain)
        {
            if (s.hpf > 0.0f) db += c.hpf1.magnitudeDb (frequency, rate) + c.hpf2.magnitudeDb (frequency, rate);
            if (s.lpf > 0.0f) db += c.lpf1.magnitudeDb (frequency, rate);
        }
        return db;
    }
    if (s.filtersToSidechain)
    {
        c.hpf1.setIdentity(); c.hpf2.setIdentity(); c.lpf1.setIdentity();
    }
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
    for (auto* f : { &sideHf, &sideHmf, &sideLmf, &sideLf, &sideHpf1, &sideHpf2, &sideLpf })
        f->reset();
    envelopeDb = 0.0f;
    gateGainDb = 0.0f;
    channelEnvelope = {};
    deEssLow[0] = deEssLow[1] = 0.0f;
    deEssEnv = deEssDb = 0.0f;
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
                        || differs (s.hpf, settings.hpf) || differs (s.lpf, settings.lpf) || s.blackEq != settings.blackEq;
    settings = s;

    if (eqChanged)
        updateFilters();
}

void ConsoleChannel::updateFilters() noexcept
{
    // The black-knob EQ: its bells narrow as they boost or cut further, its shelves have a little overshoot
    const auto q = [this] (float base, float gain) { return settings.blackEq ? base * (0.6f + std::abs (gain) / ConsoleChannel::maxGainDb * 1.6f) : base; };
    const auto shelfQ = settings.blackEq ? 1.05 : 0.707;
    if (settings.hfBell) hfFilter.setPeak (sampleRate, settings.hfFreq, settings.hf, q (0.9f, settings.hf));
    else                 hfFilter.setHighShelf (sampleRate, settings.hfFreq, settings.hf, shelfQ);
    hmfFilter.setPeak (sampleRate, settings.hmfFreq, settings.hmf, q (settings.hmfQ, settings.hmf));
    lmfFilter.setPeak (sampleRate, settings.lmfFreq, settings.lmf, q (settings.lmfQ, settings.lmf));
    if (settings.lfBell) lfFilter.setPeak (sampleRate, settings.lfFreq, settings.lf, q (0.9f, settings.lf));
    else                 lfFilter.setLowShelf (sampleRate, settings.lfFreq, settings.lf, shelfQ);
    // 18 dB/octave high-pass (as the console's), 12 dB/octave low-pass.
    hpf1.setHighPass (sampleRate, juce::jmax (10.0f, settings.hpf), 0.54);
    hpf2.setHighPass (sampleRate, juce::jmax (10.0f, settings.hpf), 1.31);
    lpf1.setLowPass (sampleRate, juce::jmax (1000.0f, settings.lpf), 0.707);

    // The side chain's copies: the same curves, their own memory
    const auto copy = [] (Biquad& to, const Biquad& from)
    {
        const auto z1 = to.z1, z2 = to.z2;
        to = from;
        to.z1 = z1;
        to.z2 = z2;
    };
    copy (sideHf, hfFilter); copy (sideHmf, hmfFilter); copy (sideLmf, lmfFilter); copy (sideLf, lfFilter);
    copy (sideHpf1, hpf1); copy (sideHpf2, hpf2); copy (sideLpf, lpf1);
    filtersValid = true;
}

namespace
{
    inline void runFilter (Biquad& f, float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] = f.process (l[i], 0);
            r[i] = f.process (r[i], 1);
        }
    }

    inline float toDb (float level) noexcept { return level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f; }
}

void ConsoleChannel::runEq (float* l, float* r, int n) noexcept
{
    if (std::abs (settings.lf) > 0.01f)  runFilter (lfFilter, l, r, n);
    if (std::abs (settings.lmf) > 0.01f) runFilter (lmfFilter, l, r, n);
    if (std::abs (settings.hmf) > 0.01f) runFilter (hmfFilter, l, r, n);
    if (std::abs (settings.hf) > 0.01f)  runFilter (hfFilter, l, r, n);
}

void ConsoleChannel::runFilters (float* l, float* r, int n) noexcept
{
    if (settings.hpf > 0.0f) { runFilter (hpf1, l, r, n); runFilter (hpf2, l, r, n); }
    if (settings.lpf > 0.0f) runFilter (lpf1, l, r, n);
}

void ConsoleChannel::runDeEss (float* l, float* r, int n) noexcept
{
    // The highs above ~5 kHz split off; when they're louder than the threshold, only they come down (4:1, at most 12 dB)
    const auto c = (float) std::exp (-juce::MathConstants<double>::twoPi * 5000.0 / sampleRate);
    const auto attack = (float) std::exp (-1.0 / (0.0005 * sampleRate));
    const auto release = (float) std::exp (-1.0 / (0.06 * sampleRate));
    const auto threshold = settings.deEssThreshold - 20.0f;
    float* io[2] { l, r };
    for (int i = 0; i < n; ++i)
    {
        float high[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& lp = deEssLow[ch];
            lp = io[ch][i] + c * (lp - io[ch][i]);
            high[ch] = io[ch][i] - lp;
        }
        const auto level = juce::jmax (std::abs (high[0]), std::abs (high[1]));
        deEssEnv = level + (level > deEssEnv ? attack : release) * (deEssEnv - level);
        deEssDb = juce::jlimit (0.0f, 12.0f, (toDb (deEssEnv) - threshold) * 0.75f);
        const auto g = juce::Decibels::decibelsToGain (-deEssDb);
        for (int ch = 0; ch < 2; ++ch)
            io[ch][i] = deEssLow[ch] + high[ch] * g;
    }
}

void ConsoleChannel::runDynamics (float* l, float* r, int numSamples, const float* keyL, const float* keyR) noexcept
{
    const bool gate = settings.gateOn();
    const auto amount = settings.dynamics;
    const bool comp = amount > 0.01f;
    if (! comp)
        envelopeDb = 0.0f;
    if (! gate)
        gateGainDb = 0.0f;
    if (! gate && ! comp)
        return;

    const auto open = (float) std::exp (-1.0 / ((settings.gateFast ? 0.00005 : 0.0005) * sampleRate));
    const auto close = (float) std::exp (-1.0 / (settings.gateRelease * sampleRate));
    const auto threshold = -3.0f * amount;               // 0 .. -30 dB
    const auto ratio = settings.compRatio >= 1.0f ? settings.compRatio : 1.5f + 0.35f * amount;   // auto: 1.5:1 .. 5:1
    const auto slope = 1.0f - 1.0f / ratio;
    const auto knee = settings.compKnee ? 10.0f : 0.0f;
    const auto makeup = comp ? juce::Decibels::decibelsToGain (1.2f * amount) : 1.0f;
    const auto attack = (float) std::exp (-1.0 / ((settings.compFast ? 0.001 : 0.005) * sampleRate));
    const auto releaseSeconds = juce::jmax (0.01f, settings.compRelease);
    const auto release = (float) std::exp (-1.0 / (releaseSeconds * sampleRate));
    const auto linearStep = (float) (20.0 / (releaseSeconds * sampleRate));   // (linear: back 20 dB in the release time)
    const auto reduction = [&] (float levelDb)
    {
        const auto over = levelDb - threshold;
        if (knee > 0.0f && std::abs (over) < knee * 0.5f)
            return slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return juce::jmax (0.0f, over * slope);
    };
    const auto follow = [&] (float& env, float target)
    {
        if (target > env)
            env = target + attack * (env - target);
        else if (settings.compLinear)
            env = juce::jmax (target, env - linearStep);
        else
            env = target + release * (env - target);
    };

    const bool external = settings.externalKey && keyL != nullptr;
    const bool filtered = settings.filtersToSidechain && settings.filtersActive();
    const bool equalised = settings.eqToSidechain && settings.eqIn;
    for (int start = 0; start < numSamples; start += sideBlock)
    {
        const auto n = juce::jmin (sideBlock, numSamples - start);
        auto* L = l + start;
        auto* R = r + start;
        // The side chain: the key (its own sound or another track's), through the filters and the EQ if they're sent there
        const auto* kl = external ? keyL + start : L;
        const auto* kr = external ? (keyR != nullptr ? keyR + start : keyL + start) : R;
        const float* dl = kl;
        const float* dr = kr;
        if (filtered || equalised)
        {
            std::copy (kl, kl + n, sideL.begin());
            std::copy (kr, kr + n, sideR.begin());
            if (filtered)
            {
                if (settings.hpf > 0.0f) { runFilter (sideHpf1, sideL.data(), sideR.data(), n); runFilter (sideHpf2, sideL.data(), sideR.data(), n); }
                if (settings.lpf > 0.0f) runFilter (sideLpf, sideL.data(), sideR.data(), n);
            }
            if (equalised)
            {
                if (std::abs (settings.lf) > 0.01f)  runFilter (sideLf, sideL.data(), sideR.data(), n);
                if (std::abs (settings.lmf) > 0.01f) runFilter (sideLmf, sideL.data(), sideR.data(), n);
                if (std::abs (settings.hmf) > 0.01f) runFilter (sideHmf, sideL.data(), sideR.data(), n);
                if (std::abs (settings.hf) > 0.01f)  runFilter (sideHf, sideL.data(), sideR.data(), n);
            }
            dl = sideL.data();
            dr = sideR.data();
        }

        for (int i = 0; i < n; ++i)
        {
            const auto linked = juce::jmax (std::abs (dl[i]), std::abs (dr[i]));
            float g = 1.0f;
            if (gate)
            {
                const auto below = juce::jmax (0.0f, settings.gateThreshold - toDb (linked));
                const auto target = -juce::jmin (settings.gateRange, settings.gateExpander ? below : (below > 0.0f ? settings.gateRange : 0.0f));
                gateGainDb = target + (target > gateGainDb ? open : close) * (gateGainDb - target);
                g = juce::Decibels::decibelsToGain (gateGainDb);
            }
            if (! comp)
            {
                L[i] *= g;
                R[i] *= g;
                continue;
            }
            if (settings.link)
            {
                follow (envelopeDb, reduction (toDb (linked)));
                const auto c = g * juce::Decibels::decibelsToGain (-envelopeDb) * makeup;
                L[i] *= c;
                R[i] *= c;
            }
            else
            {
                // (unlinked: each side compressed by its own level)
                follow (channelEnvelope[0], reduction (toDb (std::abs (dl[i]))));
                follow (channelEnvelope[1], reduction (toDb (std::abs (dr[i]))));
                envelopeDb = juce::jmax (channelEnvelope[0], channelEnvelope[1]);
                L[i] *= g * juce::Decibels::decibelsToGain (-channelEnvelope[0]) * makeup;
                R[i] *= g * juce::Decibels::decibelsToGain (-channelEnvelope[1]) * makeup;
            }
        }
    }
}

void ConsoleChannel::process (float* left, float* right, int numSamples, const float* keyLeft, const float* keyRight) noexcept
{
    if (! settings.isActive())
    {
        envelopeDb = gateGainDb = deEssDb = 0.0f;
        return;
    }

    auto trim = [&] (float db)
    {
        if (std::abs (db) <= 0.01f)
            return;
        const auto g = juce::Decibels::decibelsToGain (db);
        juce::FloatVectorOperations::multiply (left, g, numSamples);
        juce::FloatVectorOperations::multiply (right, g, numSamples);
    };

    // The input: the pad, the trim, and the mic preamp (its gain, and its colour as it's driven)
    trim (settings.inputDb + (settings.pad ? -20.0f : 0.0f) + (settings.line ? 0.0f : settings.micGain - 20.0f));
    if (! settings.line && settings.micGain > 20.5f)
    {
        const auto drive = 1.0f + (settings.micGain - 20.0f) / 50.0f * 1.5f;
        for (auto* ch : { left, right })
            for (int i = 0; i < numSamples; ++i)
                ch[i] = std::tanh (ch[i] * drive) / drive;
    }
    if (settings.phase)
    {
        juce::FloatVectorOperations::negate (left, left, numSamples);
        juce::FloatVectorOperations::negate (right, right, numSamples);
    }

    const bool filtersInPath = settings.filtersActive() && ! settings.filtersToSidechain;
    const bool eqInPath = settings.eqActive() && ! settings.eqToSidechain;
    if (filtersInPath && settings.filtersAtInput)
        runFilters (left, right, numSamples);
    if (settings.deEss && ! settings.deEssPostEq)
        runDeEss (left, right, numSamples);
    else if (! settings.deEss)
        deEssDb = 0.0f;
    if (! settings.dynPostEq && settings.dynEnabled)
        runDynamics (left, right, numSamples, keyLeft, keyRight);
    if (eqInPath)
        runEq (left, right, numSamples);
    if (filtersInPath && ! settings.filtersAtInput)
        runFilters (left, right, numSamples);
    if (settings.deEss && settings.deEssPostEq)
        runDeEss (left, right, numSamples);
    if (settings.dynPostEq && settings.dynEnabled)
        runDynamics (left, right, numSamples, keyLeft, keyRight);
    if (! settings.dynEnabled)
        envelopeDb = gateGainDb = 0.0f;

    trim (settings.outputDb);
}

} // namespace riff
