#include "Nemix.h"
#include <cstring>

namespace riff
{
using namespace nemix;

namespace
{
    constexpr int tickSize = 32;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;
    inline float fastTanh (float x) noexcept { return fx::fastTanh (x); }

    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline void panGains (float pan, float& l, float& r) noexcept
    {
        pan = juce::jlimit (-1.0f, 1.0f, pan);
        l = std::min (1.0f, 1.0f - pan);
        r = std::min (1.0f, 1.0f + pan);
    }

    /** An LFO shape at a phase (0..1): -1..1 for the cycles, 0..1 for Pluck and Ramp. */
    inline float lfoShape (int shape, double p, float held, float next) noexcept
    {
        const auto x = (float) p;
        switch (shape)
        {
            case 0: return std::sin (twoPi * x);
            case 1: return x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f;
            case 2: return 2.0f * x - 1.0f;
            case 3: return 1.0f - 2.0f * x;
            case 4: return x < 0.5f ? 1.0f : -1.0f;
            case 5: return held;
            case 6: { const auto s = x * x * (3.0f - 2.0f * x); return held + s * (next - held); }
            case 7: return std::exp (-x * 6.0f);
            case 8: return x;
            case 9: return std::floor (x * 4.0f) / 3.0f * 2.0f - 1.0f;
            default: return 0.0f;
        }
    }

    /** The formants (Hz) of the vowel at t (A E I O U across 0..1). */
    inline void vowelFormants (float t, float* f) noexcept
    {
        static const float table[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        const auto p = juce::jlimit (0.0f, 1.0f, t) * 4.0f;
        const auto i = std::min (3, (int) p);
        const auto k = p - (float) i;
        for (int j = 0; j < 3; ++j)
            f[j] = table[i][j] + k * (table[i + 1][j] - table[i][j]);
    }

    /** A filter's coefficients for one tick. */
    struct Coeffs
    {
        int type = 0;
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;      // SVF
        float lg = 0.0f, fb = 0.0f;                                        // ladder
        float combDelay = 100.0f, combFb = 0.0f, combDamp = 0.5f;          // comb
        float fg[3] {}, fa1[3] {}, fa2[3] {}, fa3[3] {}, fk = 0.3f;         // formant band passes
        float drive = 0.0f, driveGain = 1.0f, mix = 1.0f, panL = 1.0f, panR = 1.0f;
        bool on = false;
    };

    inline void svfSet (float freq, float k, double rate, float& g, float& a1, float& a2, float& a3) noexcept
    {
        g = std::tan (pi * juce::jlimit (15.0f, (float) rate * 0.47f, freq) / (float) rate);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }

    inline void svfTick (float x, float& ic1, float& ic2, float a1, float a2, float a3, float& low, float& band) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2; band = v1;
    }
}

//==============================================================================
Nemix::Nemix()
{
    resetToDefaults();
    dstSlot.fill (-1);
    for (auto& m : modNow) m.store (0.0f);
    for (auto& a : lfoShown) a.store (0.0f);
    for (auto& a : envShown) a.store (0.0f);
    monoStack.reserve (128);
    arpHeld.reserve (128);
    arpChord.reserve (128);
    events.reserve (1024);
    arpOut.reserve (512);
    // the default table is ready before the first note
    wt::get (0);
}

void Nemix::resetToDefaults() noexcept
{
    const auto& p = params();
    for (int i = 0; i < maxParams; ++i)
        values[(size_t) i].store (i < (int) p.size() ? p[(size_t) i].defaultValue : 0.0f, std::memory_order_relaxed);
}

void Nemix::setParameter (int index, float value) noexcept
{
    if (index < 0 || index >= count())
        return;
    const auto& p = params()[(size_t) index];
    value = juce::jlimit (p.min, p.max, value);
    // A table is made the first time it's chosen (here, not on the audio thread)
    for (const auto& o : ix().osc)
        if (index == o.table)
            wt::get (juce::roundToInt (value));
    if (index == ix().smpInst || (index == ix().smpOn && value > 0.5f))
        samples::request (juce::roundToInt (index == ix().smpInst ? value : getParameter (ix().smpInst)));
    values[(size_t) index].store (value, std::memory_order_relaxed);
}

float Nemix::getParameter (int index) const noexcept
{
    return index >= 0 && index < count() ? values[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

void Nemix::setPosition (juce::int64 samplePosition, bool isPlaying, double beatsPerMinute) noexcept
{
    songPosition.store (samplePosition, std::memory_order_relaxed);
    songPlaying.store (isPlaying, std::memory_order_relaxed);
    songBpm.store (beatsPerMinute > 1.0 ? beatsPerMinute : 120.0, std::memory_order_relaxed);
}

void Nemix::prepare (double rate, int maxBlockSize)
{
    sampleRate = preparedRate = rate;
    rack.prepare (rate);
    scratchL.assign ((size_t) std::max (64, maxBlockSize), 0.0f);
    scratchR.assign ((size_t) std::max (64, maxBlockSize), 0.0f);
    const auto combSize = juce::nextPowerOfTwo ((int) (rate / 20.0) + 8);
    for (auto& v : voices)
    {
        v = Voice();
        for (auto& f : v.filter)
            for (auto& b : f.comb)
                b.assign ((size_t) combSize, 0.0f);
    }
    for (const auto& o : ix().osc)
        wt::get (juce::roundToInt (getParameter (o.table)));
}

//==============================================================================
void Nemix::readSettings() noexcept
{
    const auto n = count();
    for (int i = 0; i < n; ++i)
        base[(size_t) i] = values[(size_t) i].load (std::memory_order_relaxed);

    // The matrix: the routes in use, and a slot of offsets for each destination they move
    const auto& x = ix();
    const auto& dests = nemix::destinations();
    for (int d = 0; d < numDestinations; ++d)
        dstSlot[(size_t) destinations[(size_t) d]] = -1;
    numRoutes = numDestinations = 0;
    for (int s = 0; s < numSlots; ++s)
    {
        const auto src = juce::roundToInt (base[(size_t) x.slot[s].src]);
        const auto dstIndex = juce::roundToInt (base[(size_t) x.slot[s].dst]);
        const auto amount = base[(size_t) x.slot[s].amt];
        if (src <= srcNone || src >= numSources || dstIndex < 0 || dstIndex >= (int) dests.size() || std::abs (amount) < 1.0e-5f)
            continue;
        const auto param = dests[(size_t) dstIndex];
        if (dstSlot[(size_t) param] < 0)
        {
            if (numDestinations >= (int) destinations.size())
                continue;
            dstSlot[(size_t) param] = numDestinations;
            destinations[(size_t) numDestinations++] = param;
            baseNormal[(size_t) param] = toNormal (param, base[(size_t) param]);
        }
        routes[(size_t) numRoutes++] = { src, param, 0, dstSlot[(size_t) param], amount };
    }

    blk.polyphony = juce::jlimit (1, maxVoices, juce::roundToInt (base[(size_t) x.voices]));
    blk.mono = base[(size_t) x.mono] > 0.5f;
    blk.legato = base[(size_t) x.legato] > 0.5f;
    blk.glide = base[(size_t) x.glide];
    blk.bendRange = base[(size_t) x.bend];
    blk.velAmp = base[(size_t) x.velAmp];
    blk.master = base[(size_t) x.master];
    blk.serial = base[(size_t) x.filterSerial] > 0.5f;
    blk.sampleOn = base[(size_t) x.smpOn] > 0.5f;
    blk.samplesOnly = blk.sampleOn && base[(size_t) x.subOn] < 0.5f && base[(size_t) x.nzOn] < 0.5f
                      && base[(size_t) x.osc[0].on] < 0.5f && base[(size_t) x.osc[1].on] < 0.5f && base[(size_t) x.osc[2].on] < 0.5f;
}

float Nemix::eff (const Voice& v, int index) const noexcept
{
    const auto slot = dstSlot[(size_t) index];
    if (slot < 0)
        return base[(size_t) index];
    return fromNormal (index, baseNormal[(size_t) index] + v.offsets[(size_t) slot]);
}

float Nemix::advanceEnv (Env& e, int which, double dt, const Voice& v) noexcept
{
    const auto& p = ix().env[which];
    const auto curve = 1.0f + 4.0f * eff (v, p.curve);
    switch (e.stage)
    {
        case 1:
        {
            const auto a = (double) eff (v, p.attack);
            e.t += dt;
            if (a <= 1.0e-5 || e.t >= a) { e.stage = 2; e.t = 0.0; e.level = 1.0f; }
            else { const auto x = (float) (e.t / a); e.level = e.from + (1.0f - e.from) * (1.0f - std::pow (1.0f - x, 1.0f + 0.5f * curve)); }
            break;
        }
        case 2:
            e.t += dt;
            e.level = 1.0f;
            if (e.t >= (double) eff (v, p.hold)) { e.stage = 3; e.t = 0.0; }
            break;
        case 3:
        {
            const auto d = std::max (1.0e-4, (double) eff (v, p.decay));
            const auto s = eff (v, p.sustain);
            e.t += dt;
            const auto x = (float) std::min (1.0, e.t / d);
            e.level = s + (1.0f - s) * std::pow (1.0f - x, curve);
            if (x >= 1.0f) e.stage = 4;
            break;
        }
        case 4:
            e.level = eff (v, p.sustain);
            break;
        case 5:
        {
            const auto r = std::max (1.0e-4, (double) eff (v, p.release));
            e.t += dt;
            const auto x = (float) std::min (1.0, e.t / r);
            e.level = e.from * std::pow (1.0f - x, curve);
            if (x >= 1.0f) { e.stage = 0; e.level = 0.0f; }
            break;
        }
        default:
            e.level = 0.0f;
            break;
    }
    return e.level;
}

void Nemix::computeModulation (Voice& v, int samples) noexcept
{
    const auto& x = ix();
    const auto dt = (double) samples / sampleRate;
    const auto bpm = songBpm.load (std::memory_order_relaxed);

    // The modulation envelopes and LFOs, a tick at a time
    for (int e = 1; e < 4; ++e)
        advanceEnv (v.env[(size_t) e], e, dt, v);
    for (int l = 0; l < 4; ++l)
    {
        const auto& p = x.lfo[l];
        const auto sync = juce::roundToInt (base[(size_t) p.sync]);
        const auto hz = sync > 0 ? bpm / 60.0 / syncBeats (sync) : (double) eff (v, p.rate);
        const auto mode = juce::roundToInt (base[(size_t) p.mode]);
        const auto shape = juce::roundToInt (base[(size_t) p.shape]);
        auto& ph = v.lfoPhase[(size_t) l];
        double phase;
        if (mode == 0)
            phase = freeLfoPhase[(size_t) l];
        else
        {
            if (! v.lfoDone[(size_t) l])
            {
                ph += hz * dt;
                if (ph >= 1.0)
                {
                    if (mode == 2) { ph = 0.9999; v.lfoDone[(size_t) l] = true; }
                    else
                    {
                        ph -= std::floor (ph);
                        v.lfoHeld[(size_t) l] = v.lfoNext[(size_t) l];
                        v.lfoNext[(size_t) l] = random.nextFloat() * 2.0f - 1.0f;
                    }
                }
            }
            phase = std::fmod (ph + (double) base[(size_t) p.phase], 1.0);
        }
        auto value = lfoShape (shape, phase, v.lfoHeld[(size_t) l], v.lfoNext[(size_t) l]);
        v.lfoTime[(size_t) l] += dt;
        const auto rise = eff (v, p.rise);
        if (rise > 1.0e-3f)
            value *= (float) std::min (1.0, v.lfoTime[(size_t) l] / rise);
        const auto smooth = eff (v, p.smooth);
        if (smooth > 1.0e-3f)
        {
            const auto k = (float) std::exp (-dt / (0.15 * smooth));
            v.lfoSmooth[(size_t) l] = value + k * (v.lfoSmooth[(size_t) l] - value);
            value = v.lfoSmooth[(size_t) l];
        }
        v.lfoValue[(size_t) l] = value;
    }

    // The sources, then every route into the offsets
    std::array<float, numSources> src {};
    src[srcEnv2] = v.env[1].level; src[srcEnv3] = v.env[2].level; src[srcEnv4] = v.env[3].level;
    for (int l = 0; l < 4; ++l) src[(size_t) (srcLfo1 + l)] = v.lfoValue[(size_t) l];
    src[srcVelocity] = v.velocity;
    src[srcNote] = juce::jlimit (-1.0f, 1.0f, (float) (v.pitch - 60.0) / 48.0f);
    src[srcModWheel] = modWheel;
    src[srcAftertouch] = aftertouch;
    src[srcPitchBend] = bend;
    for (int m = 0; m < 4; ++m) src[(size_t) (srcMacro1 + m)] = base[(size_t) x.macro[m]];
    src[srcRandom] = v.random;
    src[srcEnv1] = v.env[0].level;

    std::fill (v.offsets.begin(), v.offsets.begin() + numDestinations, 0.0f);
    for (int r = 0; r < numRoutes; ++r)
    {
        const auto& route = routes[(size_t) r];
        v.offsets[(size_t) route.slot] += route.amount * src[(size_t) route.src];
    }
}

//==============================================================================
void Nemix::renderVoice (Voice& v, float* left, float* right, int start, int count) noexcept
{
    const auto& x = ix();
    const auto rate = sampleRate;

    // Glide towards the note
    if (v.pitch != v.target)
    {
        const auto glide = std::max (1.0e-4f, blk.glide);
        const auto step = std::max (std::abs (v.target - v.pitch), 1.0) / (glide * rate) * count;
        v.pitch = v.pitch < v.target ? std::min (v.target, v.pitch + step) : std::max (v.target, v.pitch - step);
    }
    const auto bendSemis = bend * blk.bendRange;

    // The oscillators for this tick
    struct Osc
    {
        bool on = false;
        const wt::Table* table = nullptr;
        int level = 0, unison = 1, warp = 0, route = 0;
        float pos = 0.0f, warpAmt = 0.0f, norm = 1.0f;
        std::array<double, maxUnison> inc {};
        std::array<float, maxUnison> gl {}, gr {};
    } osc[3];

    for (int o = 0; o < 3; ++o)
    {
        const auto& p = x.osc[o];
        auto& s = osc[o];
        if (base[(size_t) p.on] < 0.5f)
            continue;
        s.table = wt::ready (juce::roundToInt (base[(size_t) p.table]));
        if (s.table == nullptr)
            continue;
        s.on = true;
        s.pos = eff (v, p.pos);
        s.unison = juce::jlimit (1, maxUnison, juce::roundToInt (base[(size_t) p.unison]));
        s.warp = juce::roundToInt (base[(size_t) p.warp]);
        s.warpAmt = eff (v, p.warpAmt);
        s.route = juce::roundToInt (base[(size_t) p.route]);
        const auto pitch = v.pitch + bendSemis + 12.0 * juce::roundToInt (base[(size_t) p.oct]) + eff (v, p.semi) + eff (v, p.fine) * 0.01;
        const auto inc0 = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0) / rate;
        const auto detune = eff (v, p.detune);
        const auto spread = detune * detune * 1.2f;   // semitones either side at the most
        const auto blend = eff (v, p.blend), width = eff (v, p.width), level = eff (v, p.level), pan = eff (v, p.pan);
        float power = 0.0f;
        double highest = inc0;
        for (int u = 0; u < s.unison; ++u)
        {
            const auto off = s.unison == 1 ? 0.0f : (float) u / (float) (s.unison - 1) * 2.0f - 1.0f;
            s.inc[(size_t) u] = inc0 * std::pow (2.0, off * spread / 12.0);
            highest = std::max (highest, s.inc[(size_t) u]);
            const auto g = 1.0f - std::abs (off) * (1.0f - blend);
            power += g * g;
            float l, r;
            panGains (pan + ((u & 1) ? -1.0f : 1.0f) * std::abs (off) * width, l, r);
            s.gl[(size_t) u] = g * l;
            s.gr[(size_t) u] = g * r;
        }
        s.norm = level / std::sqrt (std::max (1.0e-6f, power)) * (s.unison > 1 ? 1.15f : 1.0f);
        s.level = wt::levelFor (highest * (1.0 + s.warpAmt * (s.warp == 1 ? 7.0 : 0.0)));
    }

    // Sub and noise
    const auto subOn = base[(size_t) x.subOn] > 0.5f;
    const auto subShape = juce::roundToInt (base[(size_t) x.subShape]);
    const auto subInc = 440.0 * std::pow (2.0, (v.pitch + bendSemis - 12.0 * juce::roundToInt (base[(size_t) x.subOct]) - 69.0) / 12.0) / rate;
    const auto subLevel = eff (v, x.subLevel);
    float subL, subR; panGains (eff (v, x.subPan), subL, subR);
    const auto subDirect = base[(size_t) x.subDirect] > 0.5f;
    const auto nzOn = base[(size_t) x.nzOn] > 0.5f;
    const auto nzType = juce::roundToInt (base[(size_t) x.nzType]);
    const auto nzLevel = eff (v, x.nzLevel) * 0.7f;
    float nzL, nzR; panGains (eff (v, x.nzPan), nzL, nzR);
    const auto colour = eff (v, x.nzColour);
    const auto nzRoute = juce::roundToInt (base[(size_t) x.nzRoute]);
    const auto colourLow = 1.0f - std::exp (-twoPi * (200.0f + 18000.0f * colour * colour) / (float) rate);
    const auto colourHigh = 1.0f - std::exp (-twoPi * (20.0f + 6000.0f * std::max (0.0f, colour - 0.5f) * 2.0f) / (float) rate);

    // The sample layer: its step through the recording (pitch from the note, the region's root and tune), level and pan
    const auto* smpRec = (! v.smpDone && v.smpRegion != nullptr && v.smpIns != nullptr) ? &v.smpIns->recordings[(size_t) v.smpRegion->recording] : nullptr;
    double smpStep = 0.0;
    float smpGL = 0.0f, smpGR = 0.0f;
    const auto smpRoute = juce::roundToInt (base[(size_t) x.smpRoute]);
    if (smpRec != nullptr && smpRec->length > 1)
    {
        const auto semis = (v.pitch + bendSemis) - v.smpRegion->root + v.smpRegion->tuneCents * 0.01 + eff (v, x.smpTune) + eff (v, x.smpFine) * 0.01;
        smpStep = std::pow (2.0, semis / 12.0) * 44100.0 / rate;
        const auto g = std::pow (10.0f, v.smpRegion->gainDb * 0.05f) * eff (v, x.smpLevel);
        panGains (eff (v, x.smpPan) + v.smpRegion->pan, smpGL, smpGR);
        smpGL *= g; smpGR *= g;
    }
    else
        smpRec = nullptr;

    // The filters' coefficients
    Coeffs fc[2];
    for (int f = 0; f < 2; ++f)
    {
        const auto& p = x.filter[f];
        auto& c = fc[f];
        c.on = base[(size_t) p.on] > 0.5f;
        if (! c.on) continue;
        c.type = juce::roundToInt (base[(size_t) p.type]);
        const auto keytrack = eff (v, p.keytrack);
        const auto cutoff = juce::jlimit (16.0f, 22000.0f, eff (v, p.cutoff) * std::pow (2.0f, (float) (v.pitch - 60.0) / 12.0f * keytrack));
        const auto res = juce::jlimit (0.0f, 1.0f, eff (v, p.res));
        c.drive = eff (v, p.drive);
        c.driveGain = 1.0f + c.drive * 7.0f;
        c.mix = eff (v, p.mix);
        panGains (eff (v, p.pan), c.panL, c.panR);
        switch (c.type)
        {
            case 0: case 3: case 5: case 7: case 8:
                c.k = 2.0f - 1.96f * res;
                svfSet (cutoff, c.k, rate, c.g, c.a1, c.a2, c.a3);
                break;
            case 1: case 4: case 6:
                c.k = 2.0f - 1.72f * res;
                svfSet (cutoff, c.k, rate, c.g, c.a1, c.a2, c.a3);
                break;
            case 2:
                c.lg = std::min (0.98f, 1.0f - std::exp (-twoPi * cutoff / (float) rate));
                c.fb = res * 3.9f;
                break;
            case 9: case 10:
                c.combDelay = (float) rate / juce::jlimit (20.0f, 12000.0f, cutoff);
                // (high resonance rings on like a plucked string: the loss per pass small)
                c.combFb = (c.type == 9 ? 1.0f : -1.0f) * (0.998f - 0.8f * (1.0f - res) * (1.0f - res));
                c.combDamp = 0.72f;
                break;
            case 11:
            {
                float f3[3];
                vowelFormants (toNormal (x.filter[f].cutoff, cutoff), f3);
                c.fk = 0.5f - 0.42f * res;
                for (int b = 0; b < 3; ++b)
                    svfSet (f3[b], c.fk, rate, c.fg[b], c.fa1[b], c.fa2[b], c.fa3[b]);
                break;
            }
            default: break;
        }
    }

    const auto runFilter = [&] (int f, float in, int ch) noexcept -> float
    {
        auto& c = fc[f];
        auto& st = v.filter[(size_t) f];
        auto xin = in;
        if (c.drive > 0.001f)
            xin = fastTanh (xin * c.driveGain) * (1.0f / (0.65f + 0.35f * c.driveGain)) * 1.6f;
        float y = xin;
        switch (c.type)
        {
            case 0: case 3: case 5: case 7: case 8:
            {
                float low, band;
                svfTick (xin, st.ic1[0][ch], st.ic2[0][ch], c.a1, c.a2, c.a3, low, band);
                const auto high = xin - c.k * band - low;
                y = c.type == 0 ? low : c.type == 3 ? high : c.type == 5 ? band * c.k : c.type == 7 ? low + high : low - high;
                break;
            }
            case 1: case 4: case 6:
            {
                float low, band;
                svfTick (xin, st.ic1[0][ch], st.ic2[0][ch], c.a1, c.a2, c.a3, low, band);
                auto first = c.type == 1 ? low : c.type == 4 ? xin - c.k * band - low : band * c.k;
                svfTick (first, st.ic1[1][ch], st.ic2[1][ch], c.a1, c.a2, c.a3, low, band);
                y = c.type == 1 ? low : c.type == 4 ? first - c.k * band - low : band * c.k;
                break;
            }
            case 2:
            {
                auto* s = st.ladder[ch];
                const auto u = fastTanh (xin - c.fb * s[3]);
                s[0] += c.lg * (u - s[0]);
                s[1] += c.lg * (s[0] - s[1]);
                s[2] += c.lg * (s[1] - s[2]);
                s[3] += c.lg * (s[2] - s[3]);
                y = s[3] * (1.0f + c.fb * 0.25f);
                break;
            }
            case 9: case 10:
            {
                auto& buf = st.comb[(size_t) ch];
                const auto mask = (int) buf.size() - 1;
                const auto d = juce::jlimit (2.0f, (float) mask - 2.0f, c.combDelay);
                const auto di = (int) d;
                const auto frac = d - (float) di;
                const auto pos = st.combPos;
                const auto a = buf[(size_t) ((pos - di) & mask)], b = buf[(size_t) ((pos - di - 1) & mask)];
                const auto delayed = a + frac * (b - a);
                st.combLow[ch] += c.combDamp * (delayed - st.combLow[ch]);
                const auto out = xin + c.combFb * st.combLow[ch];
                buf[(size_t) ((pos + 1) & mask)] = fastTanh (out);
                y = out * 0.6f;
                break;
            }
            case 11:
            {
                y = 0.0f;
                const float weights[3] { 1.0f, 0.7f, 0.4f };
                for (int b = 0; b < 3; ++b)
                {
                    float low, band;
                    svfTick (xin, st.formant[b][0][ch], st.formant[b][1][ch], c.fa1[b], c.fa2[b], c.fa3[b], low, band);
                    y += band * weights[b];
                }
                y *= 1.6f;
                break;
            }
            default: break;
        }
        // (resonance soft-limited, as an analog filter's would be)
        y = 2.0f * fastTanh (y * 0.5f);
        return in + c.mix * (y - in);
    };

    const auto velGain = 1.0f - blk.velAmp * (1.0f - v.velocity);
    const auto dt = 1.0 / rate;
    for (int i = start; i < start + count; ++i)
    {
        float busL[3] {}, busR[3] {};   // filter 1, filter 2, direct

        for (int o = 0; o < 3; ++o)
        {
            auto& s = osc[o];
            if (! s.on) { v.last[(size_t) o] = 0.0f; continue; }
            const auto modSource = o == 0 ? v.last[1] : v.last[0];
            float accL = 0.0f, accR = 0.0f, mono = 0.0f;
            for (int u = 0; u < s.unison; ++u)
            {
                auto& ph = v.phase[(size_t) o][(size_t) u];
                ph += s.inc[(size_t) u];
                if (ph >= 1.0) ph -= 1.0;
                auto p = ph;
                const auto w = (double) s.warpAmt;
                switch (s.warp)
                {
                    case 1: p = std::fmod (p * (1.0 + w * 7.0), 1.0); break;
                    case 2: p = std::pow (p, 1.0 / (1.0 + w * 4.0)); break;
                    case 3: p = std::pow (p, 1.0 + w * 4.0); break;
                    case 4: { const auto mid = 0.5 - 0.48 * w; p = p < mid ? p * 0.5 / mid : 0.5 + (p - mid) * 0.5 / (1.0 - mid); break; }
                    case 5: { const auto m = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p; p = p + w * (m - p); break; }
                    case 6: { const auto steps = 2.0 + (1.0 - w) * 62.0; p = std::floor (p * steps) / steps; break; }
                    case 7: p = p + w * 1.5 * (double) modSource; p -= std::floor (p); break;
                    default: break;
                }
                auto smp = wt::read (*s.table, s.level, s.pos, p);
                accL += smp * s.gl[(size_t) u];
                accR += smp * s.gr[(size_t) u];
                mono += smp;
            }
            accL *= s.norm; accR *= s.norm;
            if (s.warp == 8)        { const auto g = 1.0f - s.warpAmt + s.warpAmt * (0.5f + 0.5f * modSource); accL *= g; accR *= g; }
            else if (s.warp == 9)   { const auto g = 1.0f - s.warpAmt + s.warpAmt * modSource; accL *= g; accR *= g; }
            v.last[(size_t) o] = mono / (float) s.unison;
            const auto route = s.route;
            if (route == 0 || route == 2) { busL[0] += accL; busR[0] += accR; }
            if (route == 1 || route == 2) { busL[1] += accL; busR[1] += accR; }
            if (route == 3)               { busL[2] += accL; busR[2] += accR; }
            if (route == 2) { busL[0] -= accL * 0.5f; busR[0] -= accR * 0.5f; busL[1] -= accL * 0.5f; busR[1] -= accR * 0.5f; }
        }

        if (subOn)
        {
            v.subPhase += subInc;
            if (v.subPhase >= 1.0) v.subPhase -= 1.0;
            const auto t = v.subPhase;
            float s = 0.0f;
            switch (subShape)
            {
                case 0: s = std::sin (twoPi * (float) t); break;
                case 1: s = t < 0.25 ? 4.0f * (float) t : t < 0.75 ? 2.0f - 4.0f * (float) t : 4.0f * (float) t - 4.0f; break;
                case 2: s = (float) (2.0 * t - 1.0) - polyBlep (t, subInc); break;
                case 3: s = (t < 0.5 ? 1.0f : -1.0f) + polyBlep (t, subInc) - polyBlep (std::fmod (t + 0.5, 1.0), subInc); break;
                case 4: s = (t < 0.25 ? 1.0f : -1.0f) + polyBlep (t, subInc) - polyBlep (std::fmod (t + 0.75, 1.0), subInc); break;
                default: break;
            }
            s *= subLevel * 0.8f;
            const auto bus = subDirect ? 2 : 0;
            busL[bus] += s * subL;
            busR[bus] += s * subR;
        }

        if (nzOn)
        {
            const auto white = random.nextFloat() * 2.0f - 1.0f;
            float n = white;
            switch (nzType)
            {
                case 1:
                {
                    auto* b = v.pink;
                    b[0] = 0.99886f * b[0] + white * 0.0555179f; b[1] = 0.99332f * b[1] + white * 0.0750759f;
                    b[2] = 0.96900f * b[2] + white * 0.1538520f; b[3] = 0.86650f * b[3] + white * 0.3104856f;
                    b[4] = 0.55000f * b[4] + white * 0.5329522f; b[5] = -0.7616f * b[5] - white * 0.0168980f;
                    n = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f) * 0.11f;
                    b[6] = white * 0.115926f;
                    break;
                }
                case 2:
                    v.crackle *= 0.9f;
                    if (random.nextFloat() < 0.0006f) v.crackle = (random.nextFloat() * 2.0f - 1.0f) * 1.5f;
                    n = v.crackle + white * 0.04f;
                    break;
                case 3:
                {
                    // six square waves at odd ratios (a cymbal's metal)
                    static const double ratios[6] { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
                    const auto baseInc = 540.0 * std::pow (2.0, (v.pitch - 60.0) / 12.0) / rate;
                    n = 0.0f;
                    for (int k = 0; k < 6; ++k)
                    {
                        v.metal[(size_t) k] += baseInc * ratios[k];
                        if (v.metal[(size_t) k] >= 1.0) v.metal[(size_t) k] -= 1.0;
                        n += v.metal[(size_t) k] < 0.5 ? 0.16f : -0.16f;
                    }
                    break;
                }
                case 4:
                    v.noiseHigh += 0.25f * (white - v.noiseHigh);
                    n = (white - v.noiseHigh) * 1.4f;
                    break;
                default: break;
            }
            // Colour: darker below the middle, thinner above
            v.noiseLow += colourLow * (n - v.noiseLow);
            auto c = v.noiseLow;
            if (colour > 0.5f) { v.noiseHigh += colourHigh * (c - v.noiseHigh); c -= v.noiseHigh; }
            c *= nzLevel;
            if (nzRoute == 0 || nzRoute == 2) { busL[0] += c * nzL; busR[0] += c * nzR; }
            if (nzRoute == 1 || nzRoute == 2) { busL[1] += c * nzL; busR[1] += c * nzR; }
            if (nzRoute == 3)                 { busL[2] += c * nzL; busR[2] += c * nzR; }
        }

        if (smpRec != nullptr && ! v.smpDone)
        {
            const auto i0 = (int) v.smpPos;
            if (i0 + 1 >= smpRec->length)
                v.smpDone = true;
            else
            {
                const auto f = (float) (v.smpPos - (double) i0);
                const auto l0 = smpRec->at (0, i0), l1 = smpRec->at (0, i0 + 1);
                const auto r0 = smpRec->at (1, i0), r1 = smpRec->at (1, i0 + 1);
                auto sl = (l0 + f * (l1 - l0)) * smpGL, sr = (r0 + f * (r1 - r0)) * smpGR;
                if (v.choking)
                {
                    v.choke *= 0.9985f;   // (a choked cymbal or open hat: gone in a few milliseconds)
                    sl *= v.choke; sr *= v.choke;
                    if (v.choke < 0.001f) v.smpDone = true;
                }
                v.smpPos += smpStep;
                if (smpRoute == 0 || smpRoute == 2) { busL[0] += sl; busR[0] += sr; }
                if (smpRoute == 1 || smpRoute == 2) { busL[1] += sl; busR[1] += sr; }
                if (smpRoute == 3)                  { busL[2] += sl; busR[2] += sr; }
            }
        }

        // The filters, in series (1 into 2) or side by side
        float outL = busL[2], outR = busR[2];
        float f1L = busL[0], f1R = busR[0];
        if (fc[0].on) { f1L = runFilter (0, f1L, 0) * fc[0].panL; f1R = runFilter (0, f1R, 1) * fc[0].panR; }
        if (blk.serial)
        {
            auto f2L = busL[1] + f1L, f2R = busR[1] + f1R;
            if (fc[1].on) { f2L = runFilter (1, f2L, 0) * fc[1].panL; f2R = runFilter (1, f2R, 1) * fc[1].panR; }
            outL += f2L; outR += f2R;
        }
        else
        {
            auto f2L = busL[1], f2R = busR[1];
            if (fc[1].on) { f2L = runFilter (1, f2L, 0) * fc[1].panL; f2R = runFilter (1, f2R, 1) * fc[1].panR; }
            outL += f1L + f2L; outR += f1R + f2R;
        }
        for (auto& f : v.filter)
            f.combPos = (f.combPos + 1) & ((int) f.comb[0].size() - 1);

        const auto amp = advanceEnv (v.env[0], 0, dt, v);
        v.ampNow = amp;
        const auto g = amp * velGain * 0.38f;
        left[i] += outL * g;
        right[i] += outR * g;
    }

    if (v.env[0].stage == 0)
        v.active = false;
    // (only a recording sounds and it has ended: the voice is free)
    if (blk.samplesOnly && v.smpDone)
        v.active = false;
}

//==============================================================================
void Nemix::startVoice (Voice& v, int note, float velocity, bool retrigger) noexcept
{
    const auto& x = ix();
    const auto wasActive = v.active;
    v.active = true;
    v.gate = true;
    v.held = true;
    v.note = note;
    v.velocity = velocity;
    v.age = ++counter;
    v.target = note;
    if (! wasActive || retrigger)
        v.pitch = (blk.glide > 0.0f && lastNote >= 0) ? (double) lastNote : (double) note;
    if (! retrigger && wasActive)
        return;   // legato: just glide

    v.random = random.nextFloat() * 2.0f - 1.0f;
    for (int e = 0; e < 4; ++e)
    {
        auto& env = v.env[(size_t) e];
        env.from = wasActive ? env.level : 0.0f;
        env.stage = 1;
        env.t = 0.0;
    }
    for (int l = 0; l < 4; ++l)
    {
        v.lfoPhase[(size_t) l] = 0.0;
        v.lfoTime[(size_t) l] = 0.0;
        v.lfoDone[(size_t) l] = false;
        v.lfoHeld[(size_t) l] = random.nextFloat() * 2.0f - 1.0f;
        v.lfoNext[(size_t) l] = random.nextFloat() * 2.0f - 1.0f;
        v.lfoSmooth[(size_t) l] = 0.0f;
    }
    if (! wasActive)
    {
        for (int o = 0; o < 3; ++o)
        {
            const auto start = (double) base[(size_t) x.osc[o].phase];
            const auto rnd = (double) base[(size_t) x.osc[o].rand];
            for (auto& ph : v.phase[(size_t) o])
                ph = std::fmod (start + rnd * random.nextDouble(), 1.0);
            v.last[(size_t) o] = 0.0f;
        }
        v.subPhase = 0.0;
        for (auto& f : v.filter)
        {
            std::memset (f.ic1, 0, sizeof (f.ic1));
            std::memset (f.ic2, 0, sizeof (f.ic2));
            std::memset (f.ladder, 0, sizeof (f.ladder));
            std::memset (f.formant, 0, sizeof (f.formant));
            for (auto& b : f.comb) std::fill (b.begin(), b.end(), 0.0f);
            f.combLow[0] = f.combLow[1] = 0.0f;
        }
    }
    // The sample layer: the recording for this key and velocity (round robins in turn), choking what it should
    v.smpDone = true;
    v.smpRegion = nullptr;
    v.choking = false;
    v.choke = 1.0f;
    if (blk.sampleOn)
    {
        const auto* ins = samples::find (juce::roundToInt (base[(size_t) x.smpInst]));
        if (ins != nullptr && ins->ready.load (std::memory_order_acquire))
        {
            const auto vel = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
            const auto* region = ins->pick (note, vel, roundRobin[(size_t) note]++);
            if (region != nullptr && region->recording < (int) ins->recordings.size())
            {
                v.smpIns = ins;
                v.smpRegion = region;
                v.smpDone = false;
                const auto& rec = ins->recordings[(size_t) region->recording];
                v.smpPos = (double) base[(size_t) x.smpStart] * std::min (rec.length - 2, 22050);
                if (region->group > 0)
                    for (auto& other : voices)
                        if (&other != &v && other.active && ! other.smpDone && other.smpRegion != nullptr && other.smpRegion->offBy == region->group)
                            other.choking = true;
            }
        }
    }
    computeModulation (v, 0);
}

void Nemix::releaseVoice (Voice& v) noexcept
{
    v.gate = false;
    for (auto& e : v.env)
        if (e.stage != 0 && e.stage != 5)
        {
            e.from = e.level;
            e.stage = 5;
            e.t = 0.0;
        }
}

void Nemix::noteOn (int note, float velocity) noexcept
{
    keyDown[(size_t) note] = true;
    keyVelocity[(size_t) note] = velocity;

    if (blk.mono)
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        if (monoStack.size() < 128) monoStack.push_back (note);
        auto& v = voices[0];
        const auto legatoMove = blk.legato && v.active && v.gate;
        startVoice (v, note, velocity, ! legatoMove);
        lastNote = note;
        return;
    }

    // A free voice, else the oldest released one, else the oldest
    Voice* chosen = nullptr;
    for (int i = 0; i < blk.polyphony && chosen == nullptr; ++i)
        if (! voices[(size_t) i].active)
            chosen = &voices[(size_t) i];
    if (chosen == nullptr)
    {
        for (int pass = 0; pass < 2 && chosen == nullptr; ++pass)
        {
            juce::uint64 oldest = std::numeric_limits<juce::uint64>::max();
            for (int i = 0; i < maxVoices; ++i)
            {
                auto& v = voices[(size_t) i];
                if (pass == 0 && v.gate) continue;
                if (v.active && v.age < oldest) { oldest = v.age; chosen = &v; }
            }
        }
        if (chosen != nullptr)
            chosen->active = false;   // (a fresh start for the stolen voice)
    }
    if (chosen != nullptr)
        startVoice (*chosen, note, velocity, true);
    lastNote = note;
}

void Nemix::noteOff (int note) noexcept
{
    keyDown[(size_t) note] = false;
    if (blk.mono)
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        auto& v = voices[0];
        if (v.note != note)
            return;
        if (! monoStack.empty())
        {
            // back to the note still held (gliding, no new attack)
            const auto back = monoStack.back();
            startVoice (v, back, keyVelocity[(size_t) back], ! blk.legato);
            return;
        }
        if (! sustainPedal) releaseVoice (v);
        else v.held = false;
        return;
    }
    for (auto& v : voices)
        if (v.active && v.gate && v.note == note)
        {
            if (blk.samplesOnly && v.smpIns != nullptr && v.smpIns->oneShot && ! v.smpDone)
                continue;   // (a drum hit plays to its end)
            if (sustainPedal) v.held = false;
            else releaseVoice (v);
        }
}

void Nemix::allNotesOff (bool hard) noexcept
{
    for (auto& v : voices)
    {
        if (hard) { v.active = false; v.env[0].stage = 0; }
        else if (v.active) releaseVoice (v);
    }
    keyDown.fill (false);
    monoStack.clear();
    arpHeld.clear();
    if (arpSounding >= 0) arpSounding = -1;
}

void Nemix::handleMidi (const juce::MidiMessage& m) noexcept
{
    const auto arpOn = base[(size_t) ix().arOn] > 0.5f;
    if (m.isNoteOn())
    {
        if (arpOn)
        {
            arpHeld.erase (std::remove_if (arpHeld.begin(), arpHeld.end(), [&m] (auto& h) { return h.first == m.getNoteNumber(); }), arpHeld.end());
            if (arpHeld.size() < 128) arpHeld.push_back ({ m.getNoteNumber(), m.getFloatVelocity() });
        }
        else
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        if (arpOn)
            arpHeld.erase (std::remove_if (arpHeld.begin(), arpHeld.end(), [&m] (auto& h) { return h.first == m.getNoteNumber(); }), arpHeld.end());
        else
            noteOff (m.getNoteNumber());
    }
    else if (m.isPitchWheel())
        bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isAftertouch() || m.isChannelPressure())
        aftertouch = (float) (m.isAftertouch() ? m.getAfterTouchValue() : m.getChannelPressureValue()) / 127.0f;
    else if (m.isController())
    {
        const auto cc = m.getControllerNumber(), value = m.getControllerValue();
        if (cc == 1) modWheel = (float) value / 127.0f;
        else if (cc == 64)
        {
            sustainPedal = value >= 64;
            if (! sustainPedal)
                for (auto& v : voices)
                    if (v.active && v.gate && ! v.held && ! keyDown[(size_t) v.note])
                        releaseVoice (v);
        }
        else if (cc == 120) allNotesOff (true);
        else if (cc == 123) allNotesOff (false);
    }
}

//==============================================================================
int Nemix::arpNoteForStep (juce::int64 step) noexcept
{
    if (arpHeld.empty())
        return -1;
    const auto& x = ix();
    const auto mode = juce::roundToInt (base[(size_t) x.arMode]);
    const auto octaves = juce::roundToInt (base[(size_t) x.arOct]) + 1;
    std::array<int, 128> notes {};
    int n = 0;
    for (const auto& h : arpHeld) if (n < 128) notes[(size_t) n++] = h.first;
    if (mode != 4) std::sort (notes.begin(), notes.begin() + n);
    std::array<int, 512> seq {};
    int len = 0;
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < n && len < 512; ++i)
            seq[(size_t) len++] = juce::jlimit (0, 127, notes[(size_t) i] + 12 * o);
    if (len == 0) return -1;
    switch (mode)
    {
        case 1: return seq[(size_t) (len - 1 - (int) (step % len))];
        case 2:
        {
            if (len == 1) return seq[0];
            const auto cycle = 2 * len - 2;
            const auto p = (int) (step % cycle);
            return seq[(size_t) (p < len ? p : cycle - p)];
        }
        case 3: return seq[(size_t) random.nextInt (len)];
        default: return seq[(size_t) (step % len)];
    }
}

void Nemix::arpBlock (int numSamples, std::vector<std::pair<int, juce::MidiMessage>>& out) noexcept
{
    const auto& x = ix();
    const auto rateIndex = std::max (1, juce::roundToInt (base[(size_t) x.arRate]));
    const auto beatsPerStep = syncBeats (rateIndex);
    const auto bpm = songBpm.load (std::memory_order_relaxed);
    const auto stepsPerSample = bpm / 60.0 / beatsPerStep / sampleRate;
    const auto gate = (double) base[(size_t) x.arGate];
    const auto swing = (double) base[(size_t) x.arSwing] * 0.33;
    const auto playing = songPlaying.load (std::memory_order_relaxed);
    const auto chordMode = juce::roundToInt (base[(size_t) x.arMode]) == 5;

    const auto t0 = playing ? (double) songPosition.load (std::memory_order_relaxed) * stepsPerSample : arpFreeSteps;
    const auto t1 = t0 + numSamples * stepsPerSample;
    if (! playing) arpFreeSteps = t1;

    const auto sampleAt = [&] (double t) { return juce::jlimit (0, numSamples - 1, (int) ((t - t0) / stepsPerSample)); };
    const auto endSounding = [&] (int at)
    {
        if (chordMode) for (const auto n : arpChord) out.push_back ({ at, juce::MidiMessage::noteOff (1, n) });
        else if (arpSounding >= 0) out.push_back ({ at, juce::MidiMessage::noteOff (1, arpSounding) });
        arpChord.clear();
        arpSounding = -1;
    };

    // Ending the note that's sounding
    if ((arpSounding >= 0 || ! arpChord.empty()) && (arpOffAt < t1 || arpHeld.empty()))
        endSounding (arpHeld.empty() ? 0 : sampleAt (std::max (t0, arpOffAt)));

    if (arpHeld.empty())
    {
        arpLastStep = -1;
        if (! playing) arpFreeSteps = 0.0;
        return;
    }

    for (auto s = (juce::int64) std::floor (t0) - 1; s <= (juce::int64) std::floor (t1); ++s)
    {
        const auto when = (double) s + ((s & 1) ? swing : 0.0);
        if (when < t0 || when >= t1 || s == arpLastStep)
            continue;
        arpLastStep = s;
        const auto at = sampleAt (when);
        endSounding (at);
        if (chordMode)
        {
            for (const auto& h : arpHeld)
            {
                out.push_back ({ at, juce::MidiMessage::noteOn (1, h.first, h.second) });
                arpChord.push_back (h.first);
            }
        }
        else
        {
            const auto note = arpNoteForStep (s < 0 ? 0 : s);
            if (note < 0) continue;
            float vel = 0.8f;
            for (const auto& h : arpHeld) if (h.first % 12 == note % 12) vel = h.second;
            out.push_back ({ at, juce::MidiMessage::noteOn (1, note, vel) });
            arpSounding = note;
        }
        arpOffAt = when + std::max (0.05, gate);
        if (arpOffAt < t1)
            endSounding (sampleAt (arpOffAt));
    }
}

//==============================================================================
void Nemix::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);
    readSettings();
    const auto& x = ix();

    // The events: what came in, then (with the arpeggiator on) its steps in place of the notes
    events.clear();
    const auto arpOn = base[(size_t) x.arOn] > 0.5f;
    for (const auto meta : midi)
        if (events.size() < 1000)
            events.push_back ({ meta.samplePosition, meta.getMessage() });
    if (arpOn || arpWasOn)
    {
        // (turned off: let go of what it's holding)
        if (! arpOn)
        {
            arpHeld.clear();
            if (arpSounding >= 0) noteOff (arpSounding);
            for (const auto n : arpChord) noteOff (n);
            arpChord.clear();
            arpSounding = -1;
        }
        arpWasOn = arpOn;
    }
    std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    // The block, cut at each event and every tick
    size_t next = 0;
    int pos = 0;
    if (arpOn)
    {
        // Notes in first (so the held list is up to date), then the steps come out
        arpOut.clear();
        for (const auto& e : events)
            if (e.second.isNoteOnOrOff()) handleMidi (e.second);
        arpBlock (numSamples, arpOut);
        events.erase (std::remove_if (events.begin(), events.end(), [] (const auto& e) { return e.second.isNoteOnOrOff(); }), events.end());
        for (auto& e : arpOut)
            if (events.size() < 1000) events.push_back (e);
        std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    }

    while (pos < numSamples)
    {
        while (next < events.size() && events[next].first <= pos)
        {
            const auto& m = events[next].second;
            if (arpOn && m.isNoteOnOrOff())
            {
                if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
                else noteOff (m.getNoteNumber());
            }
            else
                handleMidi (m);
            ++next;
        }
        auto end = std::min (numSamples, pos + tickSize);
        if (next < events.size())
            end = std::min (end, std::max (pos + 1, events[next].first));
        const auto len = end - pos;

        // Free-running LFOs move once for everyone
        const auto bpm = songBpm.load (std::memory_order_relaxed);
        for (int l = 0; l < 4; ++l)
        {
            const auto sync = juce::roundToInt (base[(size_t) x.lfo[l].sync]);
            const auto hz = sync > 0 ? bpm / 60.0 / syncBeats (sync) : (double) base[(size_t) x.lfo[l].rate];
            freeLfoPhase[(size_t) l] = std::fmod (freeLfoPhase[(size_t) l] + hz * len / sampleRate, 1.0);
        }

        for (auto& v : voices)
            if (v.active)
            {
                computeModulation (v, len);
                renderVoice (v, left, right, pos, len);
            }
        pos = end;
    }

    // The newest voice's modulation (for the effects, and the editor's rings)
    const Voice* newest = nullptr;
    int active = 0;
    for (const auto& v : voices)
        if (v.active)
        {
            ++active;
            if (newest == nullptr || v.age > newest->age) newest = &v;
        }
    voicesNow.store (active, std::memory_order_relaxed);
    for (int d = 0; d < shownCount; ++d)
        modNow[(size_t) shownLast[(size_t) d]].store (0.0f, std::memory_order_relaxed);
    shownCount = numDestinations;
    for (int d = 0; d < numDestinations; ++d)
    {
        shownLast[(size_t) d] = destinations[(size_t) d];
        modNow[(size_t) destinations[(size_t) d]].store (newest != nullptr ? newest->offsets[(size_t) d] : 0.0f, std::memory_order_relaxed);
    }
    for (int k = 0; k < 4; ++k)
    {
        lfoShown[(size_t) k].store (newest != nullptr ? newest->lfoValue[(size_t) k] : 0.0f, std::memory_order_relaxed);
        envShown[(size_t) k].store (newest != nullptr ? newest->env[(size_t) k].level : 0.0f, std::memory_order_relaxed);
    }

    // The effects (their knobs moved by the newest voice's modulation)
    const auto fxv = [&] (int index)
    {
        const auto slot = dstSlot[(size_t) index];
        if (slot < 0 || newest == nullptr) return base[(size_t) index];
        return fromNormal (index, baseNormal[(size_t) index] + newest->offsets[(size_t) slot]);
    };
    const auto on = [&] (int index) { return base[(size_t) index] > 0.5f; };
    auto& s = fxSettings;
    s.dsOn = on (x.dsOn); s.dsType = juce::roundToInt (base[(size_t) x.dsType]); s.dsDrive = fxv (x.dsDrive); s.dsTone = fxv (x.dsTone); s.dsMix = fxv (x.dsMix);
    s.ffOn = on (x.ffOn); s.ffType = juce::roundToInt (base[(size_t) x.ffType]); s.ffCutoff = fxv (x.ffCutoff); s.ffRes = fxv (x.ffRes); s.ffMix = fxv (x.ffMix);
    s.chOn = on (x.chOn); s.chRate = fxv (x.chRate); s.chDepth = fxv (x.chDepth); s.chDelay = fxv (x.chDelay); s.chFeedback = fxv (x.chFeedback); s.chMix = fxv (x.chMix);
    s.phOn = on (x.phOn); s.phRate = fxv (x.phRate); s.phDepth = fxv (x.phDepth); s.phFreq = fxv (x.phFreq); s.phFeedback = fxv (x.phFeedback); s.phMix = fxv (x.phMix);
    s.dlOn = on (x.dlOn);
    {
        const auto t = juce::roundToInt (base[(size_t) x.dlTime]);
        s.dlSeconds = t > 0 ? syncBeats (t) * 60.0 / songBpm.load() : 0.3;
    }
    s.dlFeedback = fxv (x.dlFeedback); s.dlPing = on (x.dlPing); s.dlTone = fxv (x.dlTone); s.dlMix = fxv (x.dlMix);
    s.otOn = on (x.otOn); s.otDepth = fxv (x.otDepth); s.otTime = fxv (x.otTime); s.otUp = fxv (x.otUp); s.otDown = fxv (x.otDown); s.otGainDb = fxv (x.otMix);
    s.rvOn = on (x.rvOn); s.rvSize = fxv (x.rvSize); s.rvDecay = fxv (x.rvDecay); s.rvDamp = fxv (x.rvDamp); s.rvPre = fxv (x.rvPre); s.rvWidth = fxv (x.rvWidth); s.rvMix = fxv (x.rvMix);
    s.eqOn = on (x.eqOn); s.eqLow = fxv (x.eqLow); s.eqLowFreq = fxv (x.eqLowFreq); s.eqHigh = fxv (x.eqHigh); s.eqHighFreq = fxv (x.eqHighFreq);
    rack.process (left, right, numSamples, s);

    const auto master = fxv (x.master) * std::pow (10.0f, base[(size_t) x.trim] * 0.05f);
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] *= master;
        right[i] *= master;
    }
}

} // namespace riff
