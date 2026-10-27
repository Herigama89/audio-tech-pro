#include "NemixFx.h"

namespace riff::nemix::fx
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }
}

void Rack::prepare (double sampleRate)
{
    rate = sampleRate;
    for (auto& l : chLine) l.prepare ((int) (0.06 * rate));
    for (auto& l : dlLine) l.prepare ((int) (4.2 * rate));
    for (auto& l : pre) l.prepare ((int) (0.25 * rate));
    const int diffusion[4] { 142, 107, 379, 277 };
    for (int i = 0; i < 4; ++i)
    {
        diffusers[i].length = (int) (diffusion[i] * rate / 29761.0);
        diffusers[i].line.prepare (diffusers[i].length + 4);
    }
    // (prime-ish lengths, scaled by Size when running; room for the largest)
    for (auto& l : fdn) l.prepare ((int) (0.2 * rate));
    reset();
}

void Rack::reset()
{
    for (auto& l : chLine) l.reset();
    for (auto& l : dlLine) l.reset();
    for (auto& l : pre) l.reset();
    for (auto& d : diffusers) d.line.reset();
    for (auto& l : fdn) l.reset();
    std::fill (std::begin (fdnLow), std::end (fdnLow), 0.0f);
    std::fill (std::begin (env), std::end (env), 0.0f);
    for (auto& s : phState) std::fill (std::begin (s), std::end (s), 0.0f);
    ff.reset(); split1.reset(); split2.reset();
    lowShelf.reset(); highShelf.reset();
    dsTone[0] = dsTone[1] = dlLow[0] = dlLow[1] = phLast[0] = phLast[1] = 0.0f;
}

void Rack::process (float* l, float* r, int n, const Settings& s) noexcept
{
    if (s.dsOn) distortion (l, r, n, s);
    if (s.ffOn) filter (l, r, n, s);
    if (s.chOn) chorus (l, r, n, s);
    if (s.phOn) phaser (l, r, n, s);
    if (s.dlOn) delay (l, r, n, s);
    if (s.otOn) squash (l, r, n, s);
    if (s.rvOn) reverb (l, r, n, s);
    if (s.eqOn) eq (l, r, n, s);
}

void Rack::distortion (float* l, float* r, int n, const Settings& s) noexcept
{
    const auto drive = 1.0f + s.dsDrive * s.dsDrive * 40.0f;
    const auto makeup = 1.0f / std::sqrt (drive) * 1.4f + 0.3f;
    // Tone: below the middle darker, above brighter (a tilt after the shaper)
    const auto toneCoeff = 1.0f - std::exp (-twoPi * (float) (400.0 + 15000.0 * s.dsTone * s.dsTone) / (float) rate);
    const auto bits = std::pow (2.0f, 12.0f - s.dsDrive * 10.0f);
    const auto hold = 1.0f + s.dsDrive * s.dsDrive * 30.0f;
    for (int i = 0; i < n; ++i)
    {
        float* io[2] { l + i, r + i };
        const auto newCrush = s.dsType == 6 && (crushCount += 1.0f) >= hold;
        if (newCrush) crushCount -= hold;
        for (int c = 0; c < 2; ++c)
        {
            const auto dry = *io[c];
            auto x = dry * drive;
            switch (s.dsType)
            {
                case 0: x = fastTanh (x + 0.15f * x * x * 0.2f); break;                         // tube: a little even warmth
                case 1: x = x / (1.0f + std::abs (x)); break;                                     // soft clip
                case 2: x = juce::jlimit (-1.0f, 1.0f, x); break;                                  // hard clip
                case 3: x = x > 0.0f ? fastTanh (x) : fastTanh (x * 0.4f) * 1.2f; break;            // diode
                case 4: x = std::sin (x * 1.2f); break;                                            // fold
                case 5: x = std::round (dry * bits) / bits; break;                                 // bitcrush
                case 6: if (newCrush) crushHold[c] = dry; x = crushHold[c]; break;                 // downsample
                case 7: x = fastTanh (x + 0.5f) - fastTanh (0.5f); break;                          // asymmetric
                default: break;
            }
            if (s.dsType < 5) x *= makeup;
            dsTone[c] += toneCoeff * (x - dsTone[c]);
            const auto toned = dsTone[c];
            *io[c] = dry + s.dsMix * (toned - dry);
        }
    }
}

void Rack::filter (float* l, float* r, int n, const Settings& s) noexcept
{
    ff.set (rate, s.ffCutoff, 0.5 + s.ffRes * 9.5);
    for (int i = 0; i < n; ++i)
    {
        float* io[2] { l + i, r + i };
        for (int c = 0; c < 2; ++c)
        {
            float lo, bp, hi;
            ff.tick (*io[c], c, lo, bp, hi);
            const auto wet = s.ffType == 0 ? lo : s.ffType == 1 ? hi : bp;
            *io[c] += s.ffMix * (wet - *io[c]);
        }
    }
}

void Rack::chorus (float* l, float* r, int n, const Settings& s) noexcept
{
    const auto inc = s.chRate / rate;
    const auto base = s.chDelay * 0.001f * (float) rate;
    const auto depth = s.chDepth * 0.006f * (float) rate;
    for (int i = 0; i < n; ++i)
    {
        chPhase += inc;
        if (chPhase >= 1.0) chPhase -= 1.0;
        float* io[2] { l + i, r + i };
        for (int c = 0; c < 2; ++c)
        {
            // two taps per side, a quarter cycle apart (left) and the opposite (right)
            const auto p = (float) chPhase + (c == 0 ? 0.0f : 0.5f);
            const auto d1 = base + depth * (0.5f + 0.5f * std::sin (twoPi * p));
            const auto d2 = base * 1.3f + depth * (0.5f + 0.5f * std::sin (twoPi * (p + 0.25f)));
            const auto wet = 0.5f * (chLine[c].read (d1) + chLine[c].read (d2));
            chLine[c].push (*io[c] + wet * s.chFeedback);
            *io[c] = *io[c] * (1.0f - 0.5f * s.chMix) + wet * s.chMix;
        }
    }
}

void Rack::phaser (float* l, float* r, int n, const Settings& s) noexcept
{
    const auto inc = s.phRate / rate;
    for (int i = 0; i < n; ++i)
    {
        phPhase += inc;
        if (phPhase >= 1.0) phPhase -= 1.0;
        float* io[2] { l + i, r + i };
        for (int c = 0; c < 2; ++c)
        {
            const auto lfo = 0.5f + 0.5f * std::sin (twoPi * ((float) phPhase + (c == 0 ? 0.0f : 0.25f)));
            const auto f = juce::jlimit (30.0f, (float) rate * 0.45f, s.phFreq * std::pow (2.0f, (lfo - 0.5f) * 4.0f * s.phDepth));
            const auto t = std::tan (juce::MathConstants<float>::pi * f / (float) rate);
            const auto a = (t - 1.0f) / (t + 1.0f);
            auto x = *io[c] + phLast[c] * s.phFeedback;
            for (auto& z : phState[c])
            {
                const auto y = a * x + z;
                z = x - a * y;
                x = y;
            }
            phLast[c] = x;
            *io[c] = *io[c] * (1.0f - 0.5f * s.phMix) + x * s.phMix;
        }
    }
}

void Rack::delay (float* l, float* r, int n, const Settings& s) noexcept
{
    const auto d = (float) juce::jlimit (0.001, 4.0, s.dlSeconds) * (float) rate;
    const auto toneCoeff = 1.0f - std::exp (-twoPi * (float) (800.0 + 14000.0 * s.dlTone) / (float) rate);
    for (int i = 0; i < n; ++i)
    {
        const auto inL = l[i], inR = r[i];
        auto wl = dlLine[0].read (d), wr = dlLine[1].read (d);
        dlLow[0] += toneCoeff * (wl - dlLow[0]); dlLow[1] += toneCoeff * (wr - dlLow[1]);
        wl = dlLow[0]; wr = dlLow[1];
        if (s.dlPing)
        {
            // ping-pong: the left echo feeds the right, the input enters on the left (mono)
            dlLine[0].push (0.5f * (inL + inR) + wr * s.dlFeedback);
            dlLine[1].push (wl * s.dlFeedback);
        }
        else
        {
            dlLine[0].push (inL + wl * s.dlFeedback);
            dlLine[1].push (inR + wr * s.dlFeedback);
        }
        l[i] = inL + wl * s.dlMix;
        r[i] = inR + wr * s.dlMix;
    }
}

void Rack::squash (float* l, float* r, int n, const Settings& s) noexcept
{
    // Three bands (below 88 Hz, to 2.5 kHz, above), each pushed towards a level from both sides
    split1.set (rate, 88.0, 0.707);
    split2.set (rate, 2500.0, 0.707);
    const auto attack = std::exp (-1.0f / ((0.0005f + 0.02f * s.otTime) * (float) rate));
    const auto release = std::exp (-1.0f / ((0.02f + 0.4f * s.otTime) * (float) rate));
    const auto downRatio = 1.0f + s.otDown * 9.0f;
    const auto upAmount = s.otUp * 0.75f;
    const auto out = dbToGain (s.otGainDb);
    const float threshold[3] { -18.0f, -20.0f, -24.0f }, floorDb = -42.0f;
    for (int i = 0; i < n; ++i)
    {
        float band[3][2];
        for (int c = 0; c < 2; ++c)
        {
            const auto x = c == 0 ? l[i] : r[i];
            float lo, bp, hi, lo2, bp2, hi2;
            split1.tick (x, c, lo, bp, hi);
            split2.tick (x, c, lo2, bp2, hi2);
            band[0][c] = lo;
            band[2][c] = hi2;
            band[1][c] = x - lo - hi2;
        }
        float yl = 0.0f, yr = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            const auto level = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
            env[b] = level + (level > env[b] ? attack : release) * (env[b] - level);
            const auto db = gainToDb (env[b] + 1.0e-6f);
            float gainDb = 0.0f;
            if (db > threshold[b]) gainDb = -(db - threshold[b]) * (1.0f - 1.0f / downRatio);
            else if (db > floorDb) gainDb = std::min (18.0f, (threshold[b] - db) * upAmount);
            const auto g = dbToGain (gainDb * s.otDepth);
            yl += band[b][0] * g;
            yr += band[b][1] * g;
        }
        l[i] = yl * out;
        r[i] = yr * out;
    }
}

void Rack::reverb (float* l, float* r, int n, const Settings& s) noexcept
{
    static const float lengths[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    const auto scale = (float) rate / 44100.0f * (0.35f + 1.25f * s.rvSize);
    for (int k = 0; k < 8; ++k)
        fdnLength[k] = std::min ((int) (lengths[k] * scale), (int) fdn[k].buf.size() - 8);
    float gains[8];
    for (int k = 0; k < 8; ++k)
        gains[k] = std::pow (10.0f, -3.0f * (float) fdnLength[k] / (std::max (0.1f, s.rvDecay) * (float) rate));
    const auto damp = 1.0f - std::exp (-twoPi * (float) (1500.0 + 16000.0 * (1.0 - s.rvDamp) * (1.0 - s.rvDamp)) / (float) rate);
    const auto preSamples = std::max (1.0f, s.rvPre * 0.001f * (float) rate);
    const auto modInc = 0.6 / rate;
    for (int i = 0; i < n; ++i)
    {
        pre[0].push (l[i]); pre[1].push (r[i]);
        auto inL = pre[0].read (preSamples), inR = pre[1].read (preSamples);
        // diffuse the input (a few allpasses)
        auto x = 0.5f * (inL + inR);
        for (auto& a : diffusers)
        {
            const auto delayed = a.line.read ((float) a.length);
            const auto v = x + 0.6f * delayed;
            a.line.push (v);
            x = delayed - 0.6f * v;
        }
        // the network: read every line, mix with a Householder matrix, feed back damped
        fdnMod += modInc;
        if (fdnMod >= 1.0) fdnMod -= 1.0;
        float y[8], sum = 0.0f;
        for (int k = 0; k < 8; ++k)
        {
            const auto wobble = (k & 1 ? 1.0f : -1.0f) * 6.0f * std::sin (twoPi * ((float) fdnMod + (float) k * 0.125f));
            y[k] = fdn[k].read ((float) fdnLength[k] + wobble);
            sum += y[k];
        }
        const auto h = sum * 0.25f;
        for (int k = 0; k < 8; ++k)
        {
            auto v = (y[k] - h) * gains[k];
            fdnLow[k] += damp * (v - fdnLow[k]);
            fdn[k].push (fdnLow[k] + (k < 4 ? x : -x) * 0.35f + (k & 1 ? inR : inL) * 0.15f);
        }
        auto wl = (y[0] + y[2] + y[4] + y[6]) * 0.35f, wr = (y[1] + y[3] + y[5] + y[7]) * 0.35f;
        const auto mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * s.rvWidth;
        wl = mid + side; wr = mid - side;
        l[i] = l[i] * (1.0f - 0.4f * s.rvMix) + wl * s.rvMix;
        r[i] = r[i] * (1.0f - 0.4f * s.rvMix) + wr * s.rvMix;
    }
}

void Rack::eq (float* l, float* r, int n, const Settings& s) noexcept
{
    if (s.eqLow != lastEq[0] || s.eqLowFreq != lastEq[1] || s.eqHigh != lastEq[2] || s.eqHighFreq != lastEq[3])
    {
        Biquad a, b;
        a.setLowShelf (rate, s.eqLowFreq, s.eqLow);
        b.setHighShelf (rate, s.eqHighFreq, s.eqHigh);
        lowShelf.b0 = a.b0; lowShelf.b1 = a.b1; lowShelf.b2 = a.b2; lowShelf.a1 = a.a1; lowShelf.a2 = a.a2;
        highShelf.b0 = b.b0; highShelf.b1 = b.b1; highShelf.b2 = b.b2; highShelf.a1 = b.a1; highShelf.a2 = b.a2;
        lastEq[0] = s.eqLow; lastEq[1] = s.eqLowFreq; lastEq[2] = s.eqHigh; lastEq[3] = s.eqHighFreq;
    }
    for (int i = 0; i < n; ++i)
    {
        l[i] = highShelf.process (lowShelf.process (l[i], 0), 0);
        r[i] = highShelf.process (lowShelf.process (r[i], 1), 1);
    }
}

} // namespace riff::nemix::fx
