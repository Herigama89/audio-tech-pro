#pragma once

#include "../BuiltInEffects.h"

/**
    Nemix's parameters: one flat list (each with an id, range, default and the value at the knob's centre),
    so the engine, the editor, the presets and the project file all speak of them by id.

    Groups, by id prefix:
      a.. b.. c..   the three wavetable oscillators      sub..  the sub oscillator     nz..  noise
      f1.. f2..     the two filters                      e1..e4  envelopes (e1: the amp)  l1..l4  LFOs
      m1..m32       the modulation matrix: m<n>Src, m<n>Dst, m<n>Amt
      fx..          the effects rack                     ar..   arpeggiator           (and the globals)
*/
namespace riff::nemix
{
    const std::vector<ParamInfo>& params();
    int indexOf (const juce::String& id);            // -1 if there's none
    inline int count() { return (int) params().size(); }

    /** 0..1 for a value, and back (log-like where the knob's centre isn't the middle of the range). */
    float toNormal (int index, float value);
    float fromNormal (int index, float normal);

    // Lists the choices are drawn from
    const std::vector<juce::String>& tableNames();
    const std::vector<juce::String>& filterTypes();
    const std::vector<juce::String>& warpModes();
    const std::vector<juce::String>& modSources();
    const std::vector<juce::String>& lfoShapes();
    const std::vector<juce::String>& syncRates();    // index 0: free (Hz)
    double syncBeats (int index);                     // beats in a cycle for a sync rate

    constexpr int numSlots = 32;
    constexpr int maxUnison = 16;
    constexpr int maxVoices = 32;

    enum Source { srcNone, srcEnv2, srcEnv3, srcEnv4, srcLfo1, srcLfo2, srcLfo3, srcLfo4, srcVelocity, srcNote, srcModWheel,
                  srcAftertouch, srcPitchBend, srcMacro1, srcMacro2, srcMacro3, srcMacro4, srcRandom, srcEnv1, numSources };

    /** Whether a parameter can be a modulation destination (the continuous ones). */
    bool modulatable (int index);
    /** The destinations in the matrix's menu order (parameter indices). */
    const std::vector<int>& destinations();

    /** Indices of the parameters the engine reads by name, looked up once. */
    struct Index
    {
        // per oscillator (0 A, 1 B, 2 C): on, table, pos, oct, semi, fine, unison, detune, blend, width, level, pan, phase, rand, warp, warpAmt, route
        struct Osc { int on, table, pos, oct, semi, fine, unison, detune, blend, width, level, pan, phase, rand, warp, warpAmt, route; } osc[3];
        int subOn, subShape, subOct, subLevel, subPan, subDirect;
        int nzOn, nzType, nzLevel, nzPan, nzColour, nzRoute;
        int smpOn, smpInst, smpLevel, smpPan, smpTune, smpFine, smpStart, smpRoute;
        struct Filter { int on, type, cutoff, res, drive, keytrack, mix, pan; } filter[2];
        int filterSerial;
        struct Env { int attack, hold, decay, sustain, release, curve; } env[4];
        struct Lfo { int shape, rate, sync, mode, rise, phase, smooth; } lfo[4];
        struct Slot { int src, dst, amt; } slot[numSlots];
        int macro[4];
        int master, trim, voices, mono, legato, glide, bend, velAmp;
        // the effects rack
        int dsOn, dsType, dsDrive, dsTone, dsMix;
        int otOn, otDepth, otTime, otUp, otDown, otMix;
        int chOn, chRate, chDepth, chDelay, chFeedback, chMix;
        int phOn, phRate, phDepth, phFreq, phFeedback, phMix;
        int dlOn, dlTime, dlFeedback, dlPing, dlTone, dlMix;
        int rvOn, rvSize, rvDecay, rvDamp, rvPre, rvWidth, rvMix;
        int eqOn, eqLow, eqLowFreq, eqHigh, eqHighFreq;
        int ffOn, ffType, ffCutoff, ffRes, ffMix;
        // the arpeggiator
        int arOn, arMode, arRate, arOct, arGate, arSwing;
    };
    const Index& ix();
}
