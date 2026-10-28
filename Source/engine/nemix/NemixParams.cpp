#include "NemixParams.h"
#include <map>

namespace riff::nemix
{

const std::vector<juce::String>& tableNames()
{
    static const std::vector<juce::String> names {
        "Basic Shapes", "Pulse Width", "Saw Harmonics", "Square Harmonics", "Harmonic Sweep", "Vowels", "FM Bells", "Sync Sweep",
        "Fold", "Digital", "Growl", "Organ", "Hollow", "Bright Stack", "Brass", "Strings", "Electric Piano", "Choir", "Spectral Noise",
        "Phase Distortion", "Reese", "Bell Partials", "Wobble", "Sub Thump", "Glass", "Chip"
    };
    return names;
}

const std::vector<juce::String>& filterTypes()
{
    static const std::vector<juce::String> names { "Low Pass 12", "Low Pass 24", "Ladder", "High Pass 12", "High Pass 24", "Band Pass 12",
                                                   "Band Pass 24", "Notch", "Peak", "Comb +", "Comb -", "Formant" };
    return names;
}

const std::vector<juce::String>& warpModes()
{
    static const std::vector<juce::String> names { "Off", "Sync", "Bend +", "Bend -", "PWM", "Mirror", "Quantize", "FM", "AM", "Ring Mod" };
    return names;
}

const std::vector<juce::String>& modSources()
{
    static const std::vector<juce::String> names { "None", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Velocity", "Note",
                                                   "Mod Wheel", "Aftertouch", "Pitch Bend", "Macro 1", "Macro 2", "Macro 3", "Macro 4",
                                                   "Random", "Env 1" };
    return names;
}

const std::vector<juce::String>& lfoShapes()
{
    static const std::vector<juce::String> names { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random",
                                                   "Pluck", "Ramp", "Stairs" };
    return names;
}

const std::vector<juce::String>& syncRates()
{
    static const std::vector<juce::String> names { "Free", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T",
                                                   "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32", "1/64" };
    return names;
}

double syncBeats (int index)
{
    static const double beats[] { 0.0, 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

namespace
{
    struct Built
    {
        std::vector<ParamInfo> list;
        std::vector<bool> mod;
        std::map<juce::String, int> byId;
    };

    const Built& built()
    {
        static const Built b = []
        {
            Built r;
            const auto add = [&r] (ParamInfo p, bool modulates)
            {
                r.byId[p.id] = (int) r.list.size();
                r.list.push_back (std::move (p));
                r.mod.push_back (modulates);
            };
            const auto knob = [&] (const juce::String& id, const juce::String& name, const juce::String& suffix, float min, float max, float def, float mid)
            {
                add ({ id, name, suffix, min, max, def, mid }, true);
            };
            const auto whole = [&] (const juce::String& id, const juce::String& name, const juce::String& suffix, float min, float max, float def)
            {
                add ({ id, name, suffix, min, max, def, 0.5f * (min + max) }, false);   // (whole numbers)
            };
            const auto choice = [&] (const juce::String& id, const juce::String& name, const std::vector<juce::String>& items, int def)
            {
                add ({ id, name, "", 0.0f, (float) items.size() - 1.0f, (float) def, 0.5f * ((float) items.size() - 1.0f), items }, false);
            };
            const std::vector<juce::String> offOn { "Off", "On" }, routes { "Filter 1", "Filter 2", "Both", "Direct" };

            // Global
            knob ("master", "Master", "", 0.0f, 1.0f, 0.7f, 0.5f);
            add ({ "trim", "Output Trim", " dB", -30.0f, 40.0f, 0.0f, 5.0f }, false);   // (the presets' levels, matched)
            whole ("voices", "Voices", "", 1.0f, 32.0f, 12.0f);
            choice ("mono", "Mono", offOn, 0);
            choice ("legato", "Legato", offOn, 0);
            knob ("glide", "Glide", " s", 0.0f, 5.0f, 0.0f, 0.4f);
            whole ("bend", "Bend Range", " st", 0.0f, 24.0f, 2.0f);
            knob ("velAmp", "Velocity", "", 0.0f, 1.0f, 0.6f, 0.5f);
            for (int m = 1; m <= 4; ++m)
                knob ("macro" + juce::String (m), "Macro " + juce::String (m), "", 0.0f, 1.0f, 0.0f, 0.5f);

            // The oscillators
            const char* oscIds[] { "a", "b", "c" };
            const char* oscNames[] { "Osc A", "Osc B", "Osc C" };
            for (int o = 0; o < 3; ++o)
            {
                const juce::String p (oscIds[o]), n (oscNames[o]);
                choice (p + "On", n + " On", offOn, o == 0 ? 1 : 0);
                choice (p + "Table", n + " Table", tableNames(), 0);
                knob (p + "Pos", n + " WT Pos", "", 0.0f, 1.0f, o == 0 ? 0.67f : 0.0f, 0.5f);
                whole (p + "Oct", n + " Octave", "", -4.0f, 4.0f, 0.0f);
                knob (p + "Semi", n + " Semi", " st", -12.0f, 12.0f, 0.0f, 0.0f);
                knob (p + "Fine", n + " Fine", " ct", -100.0f, 100.0f, 0.0f, 0.0f);
                whole (p + "Unison", n + " Unison", "", 1.0f, 16.0f, 1.0f);
                knob (p + "Detune", n + " Detune", "", 0.0f, 1.0f, 0.25f, 0.3f);
                knob (p + "Blend", n + " Blend", "", 0.0f, 1.0f, 0.75f, 0.5f);
                knob (p + "Width", n + " Width", "", 0.0f, 1.0f, 0.8f, 0.5f);
                knob (p + "Level", n + " Level", "", 0.0f, 1.0f, 0.75f, 0.5f);
                knob (p + "Pan", n + " Pan", "", -1.0f, 1.0f, 0.0f, 0.0f);
                knob (p + "Phase", n + " Phase", "", 0.0f, 1.0f, 0.0f, 0.5f);
                knob (p + "Rand", n + " Random Phase", "", 0.0f, 1.0f, 1.0f, 0.5f);
                choice (p + "Warp", n + " Warp", warpModes(), 0);
                knob (p + "WarpAmt", n + " Warp Amount", "", 0.0f, 1.0f, 0.0f, 0.5f);
                choice (p + "Route", n + " Output", routes, 0);
            }
            choice ("subOn", "Sub On", offOn, 0);
            choice ("subShape", "Sub Shape", { "Sine", "Triangle", "Saw", "Square", "Pulse" }, 0);
            choice ("subOct", "Sub Octave", { "0", "-1", "-2" }, 1);
            knob ("subLevel", "Sub Level", "", 0.0f, 1.0f, 0.75f, 0.5f);
            knob ("subPan", "Sub Pan", "", -1.0f, 1.0f, 0.0f, 0.0f);
            choice ("subDirect", "Sub Direct Out", offOn, 0);
            choice ("nzOn", "Noise On", offOn, 0);
            choice ("nzType", "Noise Type", { "White", "Pink", "Vinyl", "Metallic", "Air" }, 0);
            knob ("nzLevel", "Noise Level", "", 0.0f, 1.0f, 0.4f, 0.5f);
            knob ("nzPan", "Noise Pan", "", -1.0f, 1.0f, 0.0f, 0.0f);
            knob ("nzColour", "Noise Colour", "", 0.0f, 1.0f, 0.5f, 0.5f);
            choice ("nzRoute", "Noise Output", routes, 0);
            // The sample layer: a recorded instrument from the library (piano, bass, guitar, drums, orchestra...)
            choice ("smpOn", "Sample On", offOn, 0);
            whole ("smpInst", "Sample Instrument", "", 0.0f, 255.0f, 0.0f);
            knob ("smpLevel", "Sample Level", "", 0.0f, 1.0f, 0.8f, 0.5f);
            knob ("smpPan", "Sample Pan", "", -1.0f, 1.0f, 0.0f, 0.0f);
            knob ("smpTune", "Sample Semi", " st", -24.0f, 24.0f, 0.0f, 0.0f);
            knob ("smpFine", "Sample Fine", " ct", -100.0f, 100.0f, 0.0f, 0.0f);
            knob ("smpStart", "Sample Start", "", 0.0f, 1.0f, 0.0f, 0.5f);
            choice ("smpRoute", "Sample Output", routes, 3);

            // The filters
            for (int f = 1; f <= 2; ++f)
            {
                const juce::String p ("f" + juce::String (f)), n ("Filter " + juce::String (f));
                choice (p + "On", n + " On", offOn, 0);
                choice (p + "Type", n + " Type", filterTypes(), f == 1 ? 1 : 3);
                knob (p + "Cutoff", n + " Cutoff", " Hz", 20.0f, 20000.0f, f == 1 ? 2000.0f : 200.0f, 640.0f);
                knob (p + "Res", n + " Resonance", "", 0.0f, 1.0f, 0.15f, 0.5f);
                knob (p + "Drive", n + " Drive", "", 0.0f, 1.0f, 0.0f, 0.5f);
                knob (p + "Key", n + " Key Track", "", 0.0f, 1.0f, 0.0f, 0.5f);
                knob (p + "Mix", n + " Mix", "", 0.0f, 1.0f, 1.0f, 0.5f);
                knob (p + "Pan", n + " Pan", "", -1.0f, 1.0f, 0.0f, 0.0f);
            }
            choice ("filterSerial", "Filter Routing", { "Parallel", "Serial" }, 1);

            // Envelopes and LFOs
            for (int e = 1; e <= 4; ++e)
            {
                const juce::String p ("e" + juce::String (e)), n ("Env " + juce::String (e));
                knob (p + "Attack", n + " Attack", " s", 0.0f, 10.0f, 0.0005f, 0.25f);
                knob (p + "Hold", n + " Hold", " s", 0.0f, 10.0f, 0.0f, 0.25f);
                knob (p + "Decay", n + " Decay", " s", 0.001f, 20.0f, e == 1 ? 1.0f : 0.6f, 1.0f);
                knob (p + "Sustain", n + " Sustain", "", 0.0f, 1.0f, e == 1 ? 1.0f : 0.0f, 0.5f);
                knob (p + "Release", n + " Release", " s", 0.001f, 20.0f, e == 1 ? 0.05f : 0.3f, 0.6f);
                knob (p + "Curve", n + " Curve", "", 0.0f, 1.0f, 0.6f, 0.5f);
            }
            for (int l = 1; l <= 4; ++l)
            {
                const juce::String p ("l" + juce::String (l)), n ("LFO " + juce::String (l));
                choice (p + "Shape", n + " Shape", lfoShapes(), 1);
                knob (p + "Rate", n + " Rate", " Hz", 0.01f, 40.0f, 2.0f, 2.0f);
                choice (p + "Sync", n + " Sync", syncRates(), 8);
                choice (p + "Mode", n + " Mode", { "Free", "Retrigger", "Envelope" }, 1);
                knob (p + "Rise", n + " Rise", " s", 0.0f, 10.0f, 0.0f, 0.5f);
                knob (p + "Phase", n + " Phase", "", 0.0f, 1.0f, 0.0f, 0.5f);
                knob (p + "Smooth", n + " Smooth", "", 0.0f, 1.0f, 0.0f, 0.5f);
            }

            // The effects
            choice ("dsOn", "Distortion On", offOn, 0);
            choice ("dsType", "Distortion Type", { "Tube", "Soft Clip", "Hard Clip", "Diode", "Fold", "Bitcrush", "Downsample", "Asymmetric" }, 0);
            knob ("dsDrive", "Distortion Drive", "", 0.0f, 1.0f, 0.3f, 0.5f);
            knob ("dsTone", "Distortion Tone", "", 0.0f, 1.0f, 0.5f, 0.5f);
            knob ("dsMix", "Distortion Mix", "", 0.0f, 1.0f, 1.0f, 0.5f);
            choice ("otOn", "Multiband On", offOn, 0);
            knob ("otDepth", "Multiband Depth", "", 0.0f, 1.0f, 0.5f, 0.5f);
            knob ("otTime", "Multiband Time", "", 0.0f, 1.0f, 0.5f, 0.5f);
            knob ("otUp", "Multiband Upward", "", 0.0f, 1.0f, 0.6f, 0.5f);
            knob ("otDown", "Multiband Downward", "", 0.0f, 1.0f, 0.6f, 0.5f);
            knob ("otMix", "Multiband Output", " dB", -12.0f, 12.0f, 0.0f, 0.0f);
            choice ("chOn", "Chorus On", offOn, 0);
            knob ("chRate", "Chorus Rate", " Hz", 0.01f, 10.0f, 0.8f, 1.0f);
            knob ("chDepth", "Chorus Depth", "", 0.0f, 1.0f, 0.35f, 0.5f);
            knob ("chDelay", "Chorus Delay", " ms", 1.0f, 30.0f, 8.0f, 8.0f);
            knob ("chFeedback", "Chorus Feedback", "", 0.0f, 0.95f, 0.1f, 0.4f);
            knob ("chMix", "Chorus Mix", "", 0.0f, 1.0f, 0.5f, 0.5f);
            choice ("phOn", "Phaser On", offOn, 0);
            knob ("phRate", "Phaser Rate", " Hz", 0.01f, 10.0f, 0.3f, 0.8f);
            knob ("phDepth", "Phaser Depth", "", 0.0f, 1.0f, 0.6f, 0.5f);
            knob ("phFreq", "Phaser Frequency", " Hz", 100.0f, 8000.0f, 800.0f, 900.0f);
            knob ("phFeedback", "Phaser Feedback", "", 0.0f, 0.95f, 0.5f, 0.5f);
            knob ("phMix", "Phaser Mix", "", 0.0f, 1.0f, 0.5f, 0.5f);
            choice ("dlOn", "Delay On", offOn, 0);
            choice ("dlTime", "Delay Time", syncRates(), 11);
            knob ("dlFeedback", "Delay Feedback", "", 0.0f, 0.95f, 0.35f, 0.4f);
            choice ("dlPing", "Delay Ping-Pong", offOn, 1);
            knob ("dlTone", "Delay Tone", "", 0.0f, 1.0f, 0.5f, 0.5f);
            knob ("dlMix", "Delay Mix", "", 0.0f, 1.0f, 0.25f, 0.5f);
            choice ("rvOn", "Reverb On", offOn, 0);
            knob ("rvSize", "Reverb Size", "", 0.0f, 1.0f, 0.6f, 0.5f);
            knob ("rvDecay", "Reverb Decay", " s", 0.2f, 20.0f, 2.5f, 3.0f);
            knob ("rvDamp", "Reverb Damping", "", 0.0f, 1.0f, 0.5f, 0.5f);
            knob ("rvPre", "Reverb Pre-Delay", " ms", 0.0f, 200.0f, 10.0f, 40.0f);
            knob ("rvWidth", "Reverb Width", "", 0.0f, 1.0f, 1.0f, 0.5f);
            knob ("rvMix", "Reverb Mix", "", 0.0f, 1.0f, 0.2f, 0.5f);
            choice ("eqOn", "EQ On", offOn, 0);
            knob ("eqLow", "EQ Low", " dB", -18.0f, 18.0f, 0.0f, 0.0f);
            knob ("eqLowFreq", "EQ Low Freq", " Hz", 30.0f, 1000.0f, 120.0f, 160.0f);
            knob ("eqHigh", "EQ High", " dB", -18.0f, 18.0f, 0.0f, 0.0f);
            knob ("eqHighFreq", "EQ High Freq", " Hz", 1000.0f, 18000.0f, 6000.0f, 5000.0f);
            choice ("ffOn", "FX Filter On", offOn, 0);
            choice ("ffType", "FX Filter Type", { "Low Pass", "High Pass", "Band Pass" }, 0);
            knob ("ffCutoff", "FX Filter Cutoff", " Hz", 20.0f, 20000.0f, 8000.0f, 640.0f);
            knob ("ffRes", "FX Filter Resonance", "", 0.0f, 1.0f, 0.1f, 0.5f);
            knob ("ffMix", "FX Filter Mix", "", 0.0f, 1.0f, 1.0f, 0.5f);

            // The arpeggiator
            choice ("arOn", "Arp On", offOn, 0);
            choice ("arMode", "Arp Mode", { "Up", "Down", "Up/Down", "Random", "As Played", "Chord" }, 0);
            choice ("arRate", "Arp Rate", syncRates(), 14);
            choice ("arOct", "Arp Octaves", { "1", "2", "3", "4" }, 0);
            knob ("arGate", "Arp Gate", "", 0.05f, 1.0f, 0.7f, 0.5f);
            knob ("arSwing", "Arp Swing", "", 0.0f, 1.0f, 0.0f, 0.5f);

            // The modulation matrix (the destination: a place in destinations(), set once the list is known)
            for (int s = 1; s <= numSlots; ++s)
            {
                const juce::String p ("m" + juce::String (s));
                choice (p + "Src", "Mod " + juce::String (s) + " Source", modSources(), 0);
                whole (p + "Dst", "Mod " + juce::String (s) + " Destination", "", 0.0f, 1000.0f, 0.0f);
                add ({ p + "Amt", "Mod " + juce::String (s) + " Amount", "", -1.0f, 1.0f, 0.0f, 0.0f }, false);
            }
            return r;
        }();
        return b;
    }
}

const std::vector<ParamInfo>& params() { return built().list; }

int indexOf (const juce::String& id)
{
    const auto& m = built().byId;
    const auto it = m.find (id);
    return it != m.end() ? it->second : -1;
}

bool modulatable (int index)
{
    return index >= 0 && index < count() && built().mod[(size_t) index];
}

const std::vector<int>& destinations()
{
    static const std::vector<int> list = []
    {
        std::vector<int> d;
        for (int i = 0; i < count(); ++i)
            if (modulatable (i))
                d.push_back (i);
        return d;
    }();
    return list;
}

namespace
{
    juce::NormalisableRange<float> rangeOf (const ParamInfo& p)
    {
        juce::NormalisableRange<float> r (p.min, p.max);
        const auto centre = 0.5f * (p.min + p.max);
        if (! p.isChoice() && p.midPoint > p.min && p.midPoint < p.max && std::abs (p.midPoint - centre) > 1.0e-4f * (p.max - p.min))
            r.setSkewForCentre (p.midPoint);
        return r;
    }

    const std::vector<juce::NormalisableRange<float>>& ranges()
    {
        static const auto list = []
        {
            std::vector<juce::NormalisableRange<float>> r;
            for (const auto& p : params())
                r.push_back (rangeOf (p));
            return r;
        }();
        return list;
    }
}

float toNormal (int index, float value)
{
    const auto& r = ranges()[(size_t) index];
    return r.convertTo0to1 (juce::jlimit (r.start, r.end, value));
}

float fromNormal (int index, float normal)
{
    return ranges()[(size_t) index].convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normal));
}

const Index& ix()
{
    static const Index index = []
    {
        Index x {};
        const auto at = [] (const juce::String& id) { const auto i = indexOf (id); jassert (i >= 0); return i; };
        const char* oscIds[] { "a", "b", "c" };
        for (int o = 0; o < 3; ++o)
        {
            const juce::String p (oscIds[o]);
            x.osc[o] = { at (p + "On"), at (p + "Table"), at (p + "Pos"), at (p + "Oct"), at (p + "Semi"), at (p + "Fine"), at (p + "Unison"),
                         at (p + "Detune"), at (p + "Blend"), at (p + "Width"), at (p + "Level"), at (p + "Pan"), at (p + "Phase"),
                         at (p + "Rand"), at (p + "Warp"), at (p + "WarpAmt"), at (p + "Route") };
        }
        x.subOn = at ("subOn"); x.subShape = at ("subShape"); x.subOct = at ("subOct"); x.subLevel = at ("subLevel"); x.subPan = at ("subPan"); x.subDirect = at ("subDirect");
        x.smpOn = at ("smpOn"); x.smpInst = at ("smpInst"); x.smpLevel = at ("smpLevel"); x.smpPan = at ("smpPan");
        x.smpTune = at ("smpTune"); x.smpFine = at ("smpFine"); x.smpStart = at ("smpStart"); x.smpRoute = at ("smpRoute");
        x.nzOn = at ("nzOn"); x.nzType = at ("nzType"); x.nzLevel = at ("nzLevel"); x.nzPan = at ("nzPan"); x.nzColour = at ("nzColour"); x.nzRoute = at ("nzRoute");
        for (int f = 0; f < 2; ++f)
        {
            const juce::String p ("f" + juce::String (f + 1));
            x.filter[f] = { at (p + "On"), at (p + "Type"), at (p + "Cutoff"), at (p + "Res"), at (p + "Drive"), at (p + "Key"), at (p + "Mix"), at (p + "Pan") };
        }
        x.filterSerial = at ("filterSerial");
        for (int e = 0; e < 4; ++e)
        {
            const juce::String p ("e" + juce::String (e + 1));
            x.env[e] = { at (p + "Attack"), at (p + "Hold"), at (p + "Decay"), at (p + "Sustain"), at (p + "Release"), at (p + "Curve") };
        }
        for (int l = 0; l < 4; ++l)
        {
            const juce::String p ("l" + juce::String (l + 1));
            x.lfo[l] = { at (p + "Shape"), at (p + "Rate"), at (p + "Sync"), at (p + "Mode"), at (p + "Rise"), at (p + "Phase"), at (p + "Smooth") };
        }
        for (int s = 0; s < numSlots; ++s)
        {
            const juce::String p ("m" + juce::String (s + 1));
            x.slot[s] = { at (p + "Src"), at (p + "Dst"), at (p + "Amt") };
        }
        for (int m = 0; m < 4; ++m)
            x.macro[m] = at ("macro" + juce::String (m + 1));
        x.master = at ("master"); x.trim = at ("trim"); x.voices = at ("voices"); x.mono = at ("mono"); x.legato = at ("legato"); x.glide = at ("glide");
        x.bend = at ("bend"); x.velAmp = at ("velAmp");
        x.dsOn = at ("dsOn"); x.dsType = at ("dsType"); x.dsDrive = at ("dsDrive"); x.dsTone = at ("dsTone"); x.dsMix = at ("dsMix");
        x.otOn = at ("otOn"); x.otDepth = at ("otDepth"); x.otTime = at ("otTime"); x.otUp = at ("otUp"); x.otDown = at ("otDown"); x.otMix = at ("otMix");
        x.chOn = at ("chOn"); x.chRate = at ("chRate"); x.chDepth = at ("chDepth"); x.chDelay = at ("chDelay"); x.chFeedback = at ("chFeedback"); x.chMix = at ("chMix");
        x.phOn = at ("phOn"); x.phRate = at ("phRate"); x.phDepth = at ("phDepth"); x.phFreq = at ("phFreq"); x.phFeedback = at ("phFeedback"); x.phMix = at ("phMix");
        x.dlOn = at ("dlOn"); x.dlTime = at ("dlTime"); x.dlFeedback = at ("dlFeedback"); x.dlPing = at ("dlPing"); x.dlTone = at ("dlTone"); x.dlMix = at ("dlMix");
        x.rvOn = at ("rvOn"); x.rvSize = at ("rvSize"); x.rvDecay = at ("rvDecay"); x.rvDamp = at ("rvDamp"); x.rvPre = at ("rvPre"); x.rvWidth = at ("rvWidth"); x.rvMix = at ("rvMix");
        x.eqOn = at ("eqOn"); x.eqLow = at ("eqLow"); x.eqLowFreq = at ("eqLowFreq"); x.eqHigh = at ("eqHigh"); x.eqHighFreq = at ("eqHighFreq");
        x.ffOn = at ("ffOn"); x.ffType = at ("ffType"); x.ffCutoff = at ("ffCutoff"); x.ffRes = at ("ffRes"); x.ffMix = at ("ffMix");
        x.arOn = at ("arOn"); x.arMode = at ("arMode"); x.arRate = at ("arRate"); x.arOct = at ("arOct"); x.arGate = at ("arGate"); x.arSwing = at ("arSwing");
        return x;
    }();
    return index;
}

} // namespace riff::nemix
