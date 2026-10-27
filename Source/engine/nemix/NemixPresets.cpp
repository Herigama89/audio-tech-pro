#include "NemixPresets.h"

namespace riff::nemix
{
namespace
{
    int table (const char* name)
    {
        const auto& names = tableNames();
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == name) return (int) i;
        jassertfalse;
        return 0;
    }

    /** Builds a preset by parameter id, and routes in the matrix. */
    struct B
    {
        Preset p;
        int nextSlot = 0;
        B (const juce::String& name, const juce::String& category, const juce::StringArray& genres)
        {
            p.name = name;
            p.category = category;
            p.genres = genres;
        }
        B& set (const juce::String& id, float value)
        {
            const auto i = indexOf (id);
            jassert (i >= 0);
            if (i < 0) return *this;
            for (auto& e : p.sound)
                if (e.first == i) { e.second = value; return *this; }
            p.sound.push_back ({ i, value });
            return *this;
        }
        B& tbl (const char* osc, const char* name, float pos) { return set (juce::String (osc) + "Table", (float) table (name)).set (juce::String (osc) + "Pos", pos); }
        B& mod (int source, const juce::String& dest, float amount)
        {
            const auto param = indexOf (dest);
            const auto& d = destinations();
            const auto at = (int) (std::find (d.begin(), d.end(), param) - d.begin());
            jassert (at < (int) d.size());
            if (nextSlot >= numSlots || at >= (int) d.size()) return *this;
            const auto s = juce::String (++nextSlot);
            return set ("m" + s + "Src", (float) source).set ("m" + s + "Dst", (float) at).set ("m" + s + "Amt", amount);
        }
        B& amp (float a, float d, float s, float r) { return set ("e1Attack", a).set ("e1Decay", d).set ("e1Sustain", s).set ("e1Release", r); }
        B& env (int n, float a, float d, float s, float r)
        {
            const auto e = "e" + juce::String (n);
            return set (e + "Attack", a).set (e + "Decay", d).set (e + "Sustain", s).set (e + "Release", r);
        }
        B& filt (int n, int type, float cutoff, float res, float drive = 0.0f)
        {
            const auto f = "f" + juce::String (n);
            return set (f + "On", 1.0f).set (f + "Type", (float) type).set (f + "Cutoff", cutoff).set (f + "Res", res).set (f + "Drive", drive);
        }
        B& lfo (int n, int shape, int sync, float rateHz = 2.0f, int mode = 1)
        {
            const auto l = "l" + juce::String (n);
            return set (l + "Shape", (float) shape).set (l + "Sync", (float) sync).set (l + "Rate", rateHz).set (l + "Mode", (float) mode);
        }
        B& uni (const char* osc, int voices, float detune, float blend = 0.75f, float width = 0.9f)
        {
            const juce::String o (osc);
            return set (o + "Unison", (float) voices).set (o + "Detune", detune).set (o + "Blend", blend).set (o + "Width", width);
        }
        B& on (const char* osc) { return set (juce::String (osc) + "On", 1.0f); }
        B& off (const char* osc) { return set (juce::String (osc) + "On", 0.0f); }
        B& mono (float glide, bool legato = true) { return set ("mono", 1.0f).set ("legato", legato ? 1.0f : 0.0f).set ("glide", glide); }
        B& reverb (float mix, float decay = 2.5f, float size = 0.6f) { return set ("rvOn", 1.0f).set ("rvMix", mix).set ("rvDecay", decay).set ("rvSize", size); }
        B& delay (float mix, int time = 11, float feedback = 0.35f) { return set ("dlOn", 1.0f).set ("dlMix", mix).set ("dlTime", (float) time).set ("dlFeedback", feedback); }
        B& chorus (float mix, float depth = 0.35f, float rate = 0.8f) { return set ("chOn", 1.0f).set ("chMix", mix).set ("chDepth", depth).set ("chRate", rate); }
        B& dist (int type, float drive, float mix = 1.0f, float tone = 0.5f) { return set ("dsOn", 1.0f).set ("dsType", (float) type).set ("dsDrive", drive).set ("dsMix", mix).set ("dsTone", tone); }
        B& squash (float depth, float up = 0.6f, float down = 0.6f) { return set ("otOn", 1.0f).set ("otDepth", depth).set ("otUp", up).set ("otDown", down); }
        B& eq (float low, float lowFreq, float high, float highFreq) { return set ("eqOn", 1.0f).set ("eqLow", low).set ("eqLowFreq", lowFreq).set ("eqHigh", high).set ("eqHighFreq", highFreq); }
        B& sub (int shape, int oct, float level) { return set ("subOn", 1.0f).set ("subShape", (float) shape).set ("subOct", (float) oct).set ("subLevel", level); }
        B& noise (int type, float level, float colour = 0.5f, int route = 0) { return set ("nzOn", 1.0f).set ("nzType", (float) type).set ("nzLevel", level).set ("nzColour", colour).set ("nzRoute", (float) route); }
        B& arp (int mode, int rate, int octaves, float gate = 0.6f) { return set ("arOn", 1.0f).set ("arMode", (float) mode).set ("arRate", (float) rate).set ("arOct", (float) octaves - 1.0f).set ("arGate", gate); }
        B& master (float m) { return set ("master", m); }
        B& phaser_on (float mix = 0.4f) { return set ("phOn", 1.0f).set ("phMix", mix); }
        operator Preset() const { return p; }
    };

    // Filter types, LFO shapes, sync rates, sources (readable names for the tables below)
    enum { LP12, LP24, LADDER, HP12, HP24, BP12, BP24, NOTCH, PEAK, COMB, COMBNEG, FORMANT };
    enum { SINE, TRI, SAWUP, SAWDOWN, SQUARE, SH, SMOOTH, PLUCK, RAMP, STAIRS };
    enum { FREE, BARS8, BARS4, BARS2, BAR1, HALF, HALFT, QD, Q, QT, ED, E, ET, SD, S, ST, T32, T64 };
    enum { TUBE, SOFT, HARD, DIODE, FOLD, CRUSH, DOWNSAMPLE, ASYM };
    enum { UP, DOWN, UPDOWN, RANDOM, PLAYED, CHORD };

    const juce::StringArray urban { "Trap", "Hip Hop", "Drill" };

    std::vector<Preset> build()
    {
        std::vector<Preset> v;
        const auto add = [&v] (const Preset& p) { v.push_back (p); };

        //======================================================================
        // 808s: a sine with a click of pitch at the start, gliding (mono) between notes
        struct Eight { const char* name; float decay, drive, glide; int dtype; float punch, tone; juce::StringArray genres; };
        const Eight eights[] {
            { "808 Classic", 1.6f, 0.15f, 0.06f, TUBE, 0.22f, 0.5f, { "Trap", "Hip Hop" } },
            { "808 Long Slide", 3.0f, 0.2f, 0.16f, TUBE, 0.2f, 0.5f, { "Trap", "Drill" } },
            { "808 Drill Glide", 2.4f, 0.35f, 0.2f, TUBE, 0.25f, 0.55f, { "Drill", "Trap" } },
            { "808 Distorted", 2.0f, 0.55f, 0.08f, ASYM, 0.25f, 0.6f, { "Trap", "Hip Hop" } },
            { "808 Fuzz Monster", 2.2f, 0.7f, 0.1f, FOLD, 0.3f, 0.45f, { "Trap", "Drill" } },
            { "808 Punch Short", 0.55f, 0.25f, 0.04f, TUBE, 0.35f, 0.6f, { "Hip Hop", "Trap", "Reggaeton" } },
            { "808 Mellow Round", 1.8f, 0.0f, 0.05f, TUBE, 0.15f, 0.4f, { "R&B", "Hip Hop", "Lo-Fi" } },
            { "808 Hard Clip", 1.5f, 0.5f, 0.07f, HARD, 0.3f, 0.6f, { "Trap", "Drill" } },
            { "808 Reggaeton Boom", 1.1f, 0.25f, 0.05f, SOFT, 0.28f, 0.5f, { "Reggaeton", "Latin", "Dancehall" } },
            { "808 Afro Bounce", 0.9f, 0.2f, 0.04f, TUBE, 0.3f, 0.5f, { "Afrobeats", "Dancehall" } },
            { "808 Phonk Cowbass", 1.4f, 0.6f, 0.08f, DIODE, 0.32f, 0.65f, { "Trap", "Hip Hop" } },
            { "808 Sub Only", 2.5f, 0.0f, 0.1f, TUBE, 0.1f, 0.4f, { "Trap", "R&B", "Pop" } },
        };
        for (const auto& e : eights)
        {
            B b (e.name, "808", e.genres);
            b.tbl ("a", "Sub Thump", 0.35f).amp (0.001f, e.decay, 0.0f, 0.25f).set ("e1Curve", 0.45f)
             .env (2, 0.0f, 0.07f, 0.0f, 0.05f).mod (srcEnv2, "aSemi", e.punch)
             .mono (e.glide).set ("velAmp", 0.3f);
            if (e.drive > 0.0f) b.dist (e.dtype, e.drive, 1.0f, e.tone);
            b.eq (2.0f, 70.0f, -3.0f, 6000.0f).master (0.8f);
            add (b);
        }

        //======================================================================
        // Basses
        add (B ("Deep Sub", "Bass", { "Trap", "Dubstep", "Reggae", "Hip Hop" }).tbl ("a", "Basic Shapes", 0.05f).sub (0, 1, 0.5f).amp (0.003f, 1.0f, 1.0f, 0.12f).mono (0.03f));
        add (B ("Dub Bass", "Bass", { "Reggae", "Dancehall", "Dub" }).tbl ("a", "Basic Shapes", 0.25f).filt (1, LP24, 380.0f, 0.2f, 0.25f)
             .amp (0.004f, 0.6f, 0.75f, 0.1f).env (2, 0.0f, 0.18f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.18f).sub (0, 1, 0.45f).eq (3.0f, 90.0f, -4.0f, 3000.0f));
        add (B ("Reggae Roots Bass", "Bass", { "Reggae", "Dub" }).tbl ("a", "Basic Shapes", 0.18f).filt (1, LADDER, 300.0f, 0.15f, 0.2f)
             .amp (0.006f, 0.45f, 0.6f, 0.08f).set ("velAmp", 0.8f).eq (4.0f, 80.0f, -6.0f, 2500.0f));
        add (B ("Reggaeton Bass", "Bass", { "Reggaeton", "Latin" }).tbl ("a", "Basic Shapes", 0.66f).sub (0, 1, 0.6f).filt (1, LP24, 520.0f, 0.25f)
             .amp (0.002f, 0.35f, 0.3f, 0.08f).env (2, 0.0f, 0.12f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.25f).mono (0.03f, false));
        add (B ("Dembow Pluck Bass", "Bass", { "Reggaeton", "Dancehall" }).tbl ("a", "Square Harmonics", 0.5f).filt (1, LP24, 400.0f, 0.3f, 0.2f)
             .amp (0.001f, 0.25f, 0.0f, 0.06f).env (2, 0.0f, 0.09f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.35f).sub (0, 1, 0.5f));
        add (B ("Hip Hop Square Bass", "Bass", { "Hip Hop", "Lo-Fi" }).tbl ("a", "Basic Shapes", 1.0f).filt (1, LP24, 650.0f, 0.15f).amp (0.003f, 0.8f, 0.5f, 0.1f)
             .sub (0, 1, 0.4f).mono (0.02f, false));
        add (B ("House Saw Bass", "Bass", { "House", "EDM" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 2, 0.12f).filt (1, LP24, 300.0f, 0.35f)
             .amp (0.001f, 0.3f, 0.2f, 0.08f).env (2, 0.0f, 0.15f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.4f).sub (0, 1, 0.4f));
        add (B ("Reese Classic", "Bass", { "Drum & Bass", "Dubstep", "Techno" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 2, 0.3f, 1.0f, 0.3f)
             .on ("b").tbl ("b", "Basic Shapes", 0.67f).set ("bFine", -12.0f).filt (1, LP24, 900.0f, 0.2f, 0.2f)
             .lfo (1, SINE, BARS2).mod (srcLfo1, "f1Cutoff", 0.12f).sub (0, 1, 0.4f).mono (0.04f).squash (0.25f));
        add (B ("Reese Dark Drift", "Bass", { "Drum & Bass", "Techno", "Cinematic" }).tbl ("a", "Reese", 0.3f).uni ("a", 4, 0.3f, 0.8f, 0.4f)
             .filt (1, LP24, 600.0f, 0.25f, 0.3f).lfo (1, SINE, BAR1).mod (srcLfo1, "aPos", 0.35f).sub (0, 1, 0.5f).mono (0.05f).dist (TUBE, 0.25f));
        add (B ("Dubstep Wobble", "Bass", { "Dubstep", "EDM" }).tbl ("a", "Wobble", 0.2f).uni ("a", 3, 0.15f).lfo (1, SINE, E)
             .mod (srcLfo1, "aPos", 0.6f).filt (1, LP24, 1800.0f, 0.3f, 0.3f).mod (srcLfo1, "f1Cutoff", 0.3f).sub (0, 1, 0.6f)
             .dist (TUBE, 0.4f).squash (0.5f).mono (0.03f));
        add (B ("Dubstep Growl Yoy", "Bass", { "Dubstep", "EDM" }).tbl ("a", "Growl", 0.3f).uni ("a", 2, 0.1f).lfo (1, TRI, Q)
             .mod (srcLfo1, "aPos", 0.55f).filt (1, FORMANT, 800.0f, 0.5f).mod (srcLfo1, "f1Cutoff", 0.45f).sub (0, 1, 0.55f)
             .dist (FOLD, 0.35f, 0.6f).squash (0.6f).mono (0.03f));
        add (B ("Neuro Snarl", "Bass", { "Drum & Bass", "Dubstep" }).tbl ("a", "Phase Distortion", 0.4f).set ("aWarp", 1.0f).set ("aWarpAmt", 0.3f)
             .lfo (1, SAWDOWN, E).mod (srcLfo1, "aWarpAmt", 0.4f).filt (1, BP12, 1200.0f, 0.4f, 0.4f).mod (srcLfo1, "f1Cutoff", 0.3f)
             .sub (0, 1, 0.6f).set ("subDirect", 1.0f).dist (HARD, 0.45f).squash (0.6f).mono (0.02f));
        add (B ("Acid Squelch", "Bass", { "Techno", "House", "Acid" }).tbl ("a", "Basic Shapes", 0.67f).filt (1, LADDER, 500.0f, 0.82f, 0.3f)
             .amp (0.001f, 0.3f, 0.5f, 0.06f).env (2, 0.0f, 0.22f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.4f).mod (srcVelocity, "f1Cutoff", 0.15f)
             .mono (0.07f).dist (DIODE, 0.3f).delay (0.15f, ED));
        add (B ("Funk Finger Bass", "Bass", { "Funk", "Soul", "R&B", "Pop" }).tbl ("a", "Basic Shapes", 0.4f).filt (1, LADDER, 700.0f, 0.2f, 0.2f)
             .amp (0.002f, 0.9f, 0.3f, 0.07f).env (2, 0.0f, 0.1f, 0.2f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.25f).mod (srcVelocity, "f1Cutoff", 0.2f)
             .set ("velAmp", 0.8f).sub (0, 1, 0.3f));
        add (B ("Moog-Style Bass", "Bass", { "Funk", "Synthwave", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).on ("b").tbl ("b", "Basic Shapes", 1.0f)
             .set ("bOct", -1.0f).filt (1, LADDER, 450.0f, 0.4f, 0.4f).amp (0.002f, 0.6f, 0.6f, 0.1f).env (2, 0.0f, 0.25f, 0.2f, 0.1f)
             .mod (srcEnv2, "f1Cutoff", 0.3f).mono (0.05f));
        add (B ("Synthwave Pulse Bass", "Bass", { "Synthwave", "Pop", "80s" }).tbl ("a", "Pulse Width", 0.3f).filt (1, LP24, 900.0f, 0.2f)
             .lfo (1, TRI, FREE, 0.4f, 0).mod (srcLfo1, "aPos", 0.2f).amp (0.002f, 0.3f, 0.6f, 0.1f).chorus (0.25f));
        add (B ("Upright Jazz Bass (synth)", "Bass", { "Jazz", "Soul", "Lo-Fi" }).tbl ("a", "Basic Shapes", 0.28f).filt (1, LP12, 700.0f, 0.1f)
             .amp (0.004f, 1.1f, 0.0f, 0.18f).env (2, 0.0f, 0.06f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.25f).noise (4, 0.12f, 0.6f)
             .set ("nzLevel", 0.08f).set ("velAmp", 0.85f).eq (2.0f, 100.0f, -6.0f, 4000.0f));
        add (B ("Rock Pick Bass (synth)", "Bass", { "Rock", "Pop", "Punk" }).tbl ("a", "Basic Shapes", 0.67f).on ("b").tbl ("b", "Basic Shapes", 1.0f)
             .filt (1, LP24, 1500.0f, 0.15f, 0.3f).amp (0.001f, 0.8f, 0.6f, 0.1f).env (2, 0.0f, 0.08f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.2f)
             .dist (TUBE, 0.35f, 0.7f));
        add (B ("Afro Groove Bass", "Bass", { "Afrobeats", "Amapiano", "Dancehall" }).tbl ("a", "Sub Thump", 0.6f).filt (1, LP24, 800.0f, 0.2f)
             .amp (0.002f, 0.45f, 0.2f, 0.08f).env (2, 0.0f, 0.1f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.2f).mono (0.03f, false));
        add (B ("Amapiano Log Drum", "Bass", { "Amapiano", "Afrobeats" }).tbl ("a", "Sub Thump", 0.8f).filt (1, BP12, 220.0f, 0.55f, 0.3f)
             .amp (0.001f, 0.35f, 0.0f, 0.1f).env (2, 0.0f, 0.12f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.35f).mod (srcEnv2, "aSemi", 0.12f)
             .set ("f1Key", 1.0f).dist (TUBE, 0.3f).master (0.8f));
        add (B ("Techno Rumble", "Bass", { "Techno" }).tbl ("a", "Basic Shapes", 0.4f).filt (1, LP24, 180.0f, 0.4f, 0.5f)
             .amp (0.001f, 0.5f, 0.0f, 0.3f).reverb (0.35f, 3.0f).set ("rvDamp", 0.8f).dist (TUBE, 0.5f).master (0.8f));
        add (B ("Trap Sub Saw", "Bass", { "Trap", "Drill" }).tbl ("a", "Saw Harmonics", 0.3f).sub (0, 1, 0.7f).filt (1, LP24, 380.0f, 0.1f)
             .amp (0.002f, 1.5f, 0.0f, 0.2f).mono (0.1f).dist (TUBE, 0.25f));
        add (B ("Drill Slide Bass", "Bass", { "Drill" }).tbl ("a", "Sub Thump", 0.5f).amp (0.001f, 2.5f, 0.0f, 0.25f).mono (0.22f)
             .env (2, 0.0f, 0.06f, 0.0f, 0.05f).mod (srcEnv2, "aSemi", 0.25f).dist (ASYM, 0.45f).eq (2.0f, 60.0f, -2.0f, 6000.0f));
        add (B ("Pop Synth Bass", "Bass", { "Pop", "Dance" }).tbl ("a", "Saw Harmonics", 0.55f).uni ("a", 3, 0.15f).filt (1, LP24, 1100.0f, 0.15f)
             .amp (0.002f, 0.5f, 0.7f, 0.1f).sub (0, 1, 0.45f).squash (0.25f));
        add (B ("Wavy Chip Bass", "Bass", { "Chiptune", "Hip Hop" }).tbl ("a", "Chip", 0.3f).amp (0.001f, 0.6f, 0.6f, 0.06f).mono (0.04f).filt (1, LP12, 2500.0f, 0.1f));

        //======================================================================
        // Leads
        add (B ("Supersaw Lead", "Lead", { "EDM", "Trance", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 7, 0.45f)
             .on ("b").tbl ("b", "Basic Shapes", 0.67f).set ("bOct", 1.0f).uni ("b", 5, 0.4f).set ("bLevel", 0.45f)
             .filt (1, LP24, 7000.0f, 0.1f).amp (0.005f, 1.0f, 1.0f, 0.3f).reverb (0.22f).delay (0.15f, ED).master (0.6f));
        add (B ("Trance Hoover Lead", "Lead", { "Trance", "EDM", "Rave" }).tbl ("a", "Pulse Width", 0.5f).uni ("a", 8, 0.55f)
             .lfo (1, SINE, FREE, 0.3f, 0).mod (srcLfo1, "aPos", 0.3f).mono (0.12f).filt (1, LP24, 4500.0f, 0.2f).chorus (0.3f).reverb (0.2f));
        add (B ("Sync Scream", "Lead", { "EDM", "Electro", "Dubstep" }).tbl ("a", "Basic Shapes", 0.67f).set ("aWarp", 1.0f).set ("aWarpAmt", 0.3f)
             .env (2, 0.0f, 0.5f, 0.2f, 0.2f).mod (srcEnv2, "aWarpAmt", 0.5f).uni ("a", 3, 0.2f).dist (HARD, 0.25f).delay (0.2f, ED).mono (0.05f));
        add (B ("Trap Bell Lead", "Lead", { "Trap", "Drill", "Hip Hop" }).tbl ("a", "FM Bells", 0.45f).on ("b").tbl ("b", "Glass", 0.4f)
             .set ("bOct", 1.0f).set ("bLevel", 0.3f).amp (0.001f, 1.4f, 0.0f, 0.6f).reverb (0.3f, 2.5f).delay (0.18f, ED));
        add (B ("Drill Flute Lead", "Lead", { "Drill", "Trap" }).tbl ("a", "Hollow", 0.25f).noise (4, 0.12f, 0.75f).amp (0.03f, 0.3f, 0.9f, 0.18f)
             .lfo (1, SINE, FREE, 5.2f, 1).set ("l1Rise", 0.5f).mod (srcLfo1, "aFine", 0.12f).filt (1, LP12, 4000.0f, 0.1f).reverb (0.28f).mono (0.06f, false));
        add (B ("Hip Hop Whistle", "Lead", { "Hip Hop", "G-Funk", "West Coast" }).tbl ("a", "Basic Shapes", 0.0f).amp (0.02f, 0.3f, 1.0f, 0.15f)
             .mono (0.12f).lfo (1, SINE, FREE, 5.5f, 1).set ("l1Rise", 0.4f).mod (srcLfo1, "aFine", 0.2f).mod (srcModWheel, "aFine", 0.3f).reverb (0.2f).delay (0.12f, ED));
        add (B ("G-Funk Portamento Lead", "Lead", { "Hip Hop", "G-Funk", "West Coast" }).tbl ("a", "Basic Shapes", 0.34f).on ("b").tbl ("b", "Basic Shapes", 0.0f)
             .set ("bFine", 6.0f).mono (0.18f).lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.6f).mod (srcLfo1, "aFine", 0.15f).filt (1, LP12, 3500.0f, 0.1f).chorus (0.2f));
        add (B ("Acid Lead", "Lead", { "Techno", "Acid", "House" }).tbl ("a", "Basic Shapes", 1.0f).filt (1, LADDER, 900.0f, 0.75f, 0.4f)
             .env (2, 0.0f, 0.3f, 0.2f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.35f).mono (0.08f).delay (0.25f, ED).dist (DIODE, 0.3f));
        add (B ("Chiptune Lead", "Lead", { "Chiptune", "Pop" }).tbl ("a", "Chip", 0.1f).amp (0.001f, 0.2f, 0.8f, 0.05f).lfo (1, SQUARE, ST)
             .mod (srcLfo1, "aPos", 0.25f).mono (0.0f, false));
        add (B ("Reggaeton Square Lead", "Lead", { "Reggaeton", "Latin", "Pop" }).tbl ("a", "Basic Shapes", 1.0f).uni ("a", 3, 0.12f)
             .filt (1, LP12, 3000.0f, 0.15f).amp (0.002f, 0.4f, 0.6f, 0.15f).delay (0.2f, ED).reverb (0.18f));
        add (B ("Dancehall Synth Lead", "Lead", { "Dancehall", "Reggae", "Afrobeats" }).tbl ("a", "Square Harmonics", 0.6f).amp (0.002f, 0.3f, 0.5f, 0.1f)
             .filt (1, BP12, 1500.0f, 0.3f).delay (0.3f, ED, 0.45f).set ("dlPing", 1.0f));
        add (B ("Synthwave Lead", "Lead", { "Synthwave", "80s", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 4, 0.25f)
             .on ("b").tbl ("b", "Pulse Width", 0.2f).set ("bLevel", 0.5f).filt (1, LP24, 4500.0f, 0.15f).chorus (0.35f).delay (0.25f, QD).reverb (0.25f));
        add (B ("Screech Growl Lead", "Lead", { "Dubstep", "EDM" }).tbl ("a", "Growl", 0.7f).uni ("a", 4, 0.2f).lfo (1, SINE, S)
             .mod (srcLfo1, "aPos", 0.2f).dist (HARD, 0.35f).squash (0.5f).mono (0.05f));
        add (B ("Jazz Fusion Lead", "Lead", { "Jazz", "Fusion", "Funk" }).tbl ("a", "Square Harmonics", 0.45f).filt (1, LP24, 2400.0f, 0.2f)
             .amp (0.01f, 0.5f, 0.8f, 0.2f).mod (srcModWheel, "aFine", 0.25f).lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.4f).mod (srcLfo1, "aFine", 0.06f)
             .mono (0.05f).reverb (0.2f));
        add (B ("Rock Synth Guitar Lead", "Lead", { "Rock", "Metal" }).tbl ("a", "Basic Shapes", 0.67f).on ("b").tbl ("b", "Basic Shapes", 1.0f)
             .set ("bFine", 7.0f).dist (HARD, 0.65f).filt (1, LP24, 3200.0f, 0.2f).amp (0.003f, 1.0f, 0.85f, 0.25f)
             .lfo (1, SINE, FREE, 5.5f, 1).set ("l1Rise", 0.7f).mod (srcLfo1, "aFine", 0.12f).mono (0.03f).eq (-3.0f, 150.0f, -4.0f, 6500.0f).delay (0.15f, QD).reverb (0.2f));
        add (B ("R&B Silk Lead", "Lead", { "R&B", "Soul", "Pop" }).tbl ("a", "Basic Shapes", 0.2f).amp (0.02f, 0.6f, 0.8f, 0.3f).mono (0.08f)
             .lfo (1, SINE, FREE, 4.8f, 1).set ("l1Rise", 0.6f).mod (srcLfo1, "aFine", 0.1f).chorus (0.25f).reverb (0.3f));
        add (B ("Pop Pluck Lead", "Lead", { "Pop", "EDM" }).tbl ("a", "Saw Harmonics", 0.75f).uni ("a", 5, 0.3f).filt (1, LP24, 2000.0f, 0.2f)
             .amp (0.002f, 0.5f, 0.4f, 0.2f).env (2, 0.0f, 0.25f, 0.2f, 0.2f).mod (srcEnv2, "f1Cutoff", 0.3f).delay (0.2f, ED).reverb (0.2f));
        add (B ("Vowel Talker", "Lead", { "Electro", "Funk", "EDM" }).tbl ("a", "Vowels", 0.0f).lfo (1, TRI, Q).mod (srcLfo1, "aPos", 0.5f)
             .mod (srcModWheel, "aPos", 0.5f).amp (0.01f, 0.5f, 0.9f, 0.15f).mono (0.06f).chorus (0.2f));
        add (B ("Retro Square Lead", "Lead", { "80s", "Synthwave", "Pop" }).tbl ("a", "Pulse Width", 0.0f).lfo (1, SINE, FREE, 0.5f, 0).mod (srcLfo1, "aPos", 0.35f)
             .filt (1, LP24, 3000.0f, 0.2f).amp (0.005f, 0.4f, 0.8f, 0.2f).chorus (0.3f).delay (0.2f, ED));

        //======================================================================
        // Plucks
        add (B ("Trap Pluck", "Pluck", { "Trap", "Hip Hop" }).tbl ("a", "Saw Harmonics", 0.7f).filt (1, LP24, 900.0f, 0.2f).amp (0.001f, 0.3f, 0.0f, 0.2f)
             .env (2, 0.0f, 0.12f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.4f).reverb (0.25f).delay (0.15f, ED));
        add (B ("House Pluck", "Pluck", { "House", "EDM", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 5, 0.3f).filt (1, LP24, 700.0f, 0.3f)
             .amp (0.001f, 0.28f, 0.0f, 0.2f).env (2, 0.0f, 0.15f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.45f).delay (0.25f, ED).reverb (0.2f));
        add (B ("Future Bass Pluck", "Pluck", { "Future Bass", "EDM", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 7, 0.4f).filt (1, LP24, 1500.0f, 0.15f)
             .amp (0.001f, 0.4f, 0.0f, 0.3f).env (2, 0.0f, 0.2f, 0.0f, 0.2f).mod (srcEnv2, "f1Cutoff", 0.35f).chorus (0.3f).reverb (0.3f).squash (0.3f));
        add (B ("Marimba (synth)", "Mallets", { "Afrobeats", "Pop", "Latin", "Orchestral" }).tbl ("a", "FM Bells", 0.15f).amp (0.001f, 0.45f, 0.0f, 0.3f)
             .env (2, 0.0f, 0.08f, 0.0f, 0.1f).mod (srcEnv2, "aPos", 0.25f).reverb (0.15f));
        add (B ("Kalimba (synth)", "Mallets", { "Afrobeats", "Lo-Fi", "Ambient" }).tbl ("a", "Bell Partials", 0.4f).amp (0.001f, 0.6f, 0.0f, 0.4f)
             .on ("b").tbl ("b", "Basic Shapes", 0.0f).set ("bLevel", 0.4f).noise (4, 0.25f, 0.8f).env (2, 0.0f, 0.015f, 0.0f, 0.02f).mod (srcEnv2, "nzLevel", 0.6f)
             .set ("nzLevel", 0.0f).reverb (0.25f));
        add (B ("Harp Pluck (synth)", "Pluck", { "Orchestral", "Cinematic", "Pop" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f)
             .filt (1, COMB, 261.6f, 0.92f).set ("f1Key", 1.0f).amp (0.001f, 2.5f, 0.0f, 1.2f).env (2, 0.0f, 0.012f, 0.0f, 0.01f)
             .mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f).filt (2, LP12, 4500.0f, 0.1f).reverb (0.3f, 3.0f).master (0.8f));
        add (B ("Nylon Guitar Pluck (synth)", "Guitar", { "Latin", "Reggaeton", "Pop", "Jazz" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f)
             .filt (1, COMB, 261.6f, 0.9f).set ("f1Key", 1.0f).amp (0.001f, 1.8f, 0.0f, 0.4f).env (2, 0.0f, 0.01f, 0.0f, 0.01f)
             .mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f).filt (2, LP12, 2800.0f, 0.1f).mod (srcVelocity, "f2Cutoff", 0.15f)
             .reverb (0.2f).master (0.85f));
        add (B ("Steel Guitar Pluck (synth)", "Guitar", { "Pop", "Country", "Rock", "Reggae" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f)
             .filt (1, COMB, 261.6f, 0.95f).set ("f1Key", 1.0f).amp (0.001f, 2.5f, 0.0f, 0.5f).env (2, 0.0f, 0.008f, 0.0f, 0.01f)
             .mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f).filt (2, LP12, 6000.0f, 0.05f).chorus (0.15f).reverb (0.2f).master (0.85f));
        add (B ("Muted Guitar Chop (synth)", "Guitar", { "Reggae", "Funk", "Afrobeats" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f)
             .filt (1, COMB, 261.6f, 0.8f).set ("f1Key", 1.0f).amp (0.001f, 0.18f, 0.0f, 0.06f).env (2, 0.0f, 0.01f, 0.0f, 0.01f)
             .mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f).filt (2, LP12, 2200.0f, 0.1f).delay (0.15f, ED).master (0.9f));
        add (B ("Reggae Skank Chop", "Chords", { "Reggae", "Dub", "Ska" }).tbl ("a", "Square Harmonics", 0.6f).filt (1, BP12, 1400.0f, 0.3f)
             .amp (0.001f, 0.12f, 0.0f, 0.05f).set ("f1Key", 0.5f).delay (0.3f, ED, 0.5f).reverb (0.2f, 1.8f));
        add (B ("Steel Drum (synth)", "Mallets", { "Reggae", "Dancehall", "Latin", "Afrobeats" }).tbl ("a", "FM Bells", 0.35f).on ("b").tbl ("b", "Bell Partials", 0.6f)
             .set ("bLevel", 0.35f).amp (0.001f, 0.8f, 0.0f, 0.3f).env (2, 0.0f, 0.15f, 0.0f, 0.1f).mod (srcEnv2, "aPos", 0.2f).reverb (0.2f));
        add (B ("Lo-Fi Pluck", "Pluck", { "Lo-Fi", "Hip Hop", "Chill" }).tbl ("a", "Electric Piano", 0.3f).filt (1, LP12, 2000.0f, 0.1f).amp (0.002f, 0.5f, 0.0f, 0.3f)
             .dist (CRUSH, 0.35f, 0.5f).noise (2, 0.15f, 0.4f, 3).chorus (0.2f, 0.5f, 0.3f).reverb (0.2f));
        add (B ("Bubble Pluck", "Pluck", { "House", "Pop", "Afrobeats" }).tbl ("a", "Harmonic Sweep", 0.1f).amp (0.001f, 0.2f, 0.0f, 0.1f)
             .env (2, 0.0f, 0.12f, 0.0f, 0.1f).mod (srcEnv2, "aPos", 0.5f).delay (0.2f, ED).reverb (0.2f));
        add (B ("Glass Pluck", "Pluck", { "Ambient", "Pop", "Cinematic" }).tbl ("a", "Glass", 0.5f).amp (0.001f, 0.9f, 0.0f, 0.6f).reverb (0.35f, 3.5f).delay (0.2f, QD));
        add (B ("Afro Pluck", "Pluck", { "Afrobeats", "Amapiano" }).tbl ("a", "Bell Partials", 0.8f).amp (0.001f, 0.3f, 0.0f, 0.15f).filt (1, LP12, 3000.0f, 0.1f)
             .delay (0.2f, ED).reverb (0.2f));
        add (B ("Drill Plucked Bell", "Pluck", { "Drill", "Trap" }).tbl ("a", "Bell Partials", 0.6f).on ("b").tbl ("b", "FM Bells", 0.3f).set ("bOct", 1.0f).set ("bLevel", 0.25f)
             .amp (0.001f, 0.7f, 0.0f, 0.5f).reverb (0.3f, 3.0f).delay (0.15f, ED));
        add (B ("Koto-ish Pluck", "Pluck", { "World", "Cinematic", "Trap" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f).filt (1, COMB, 261.6f, 0.88f)
             .set ("f1Key", 1.0f).amp (0.001f, 1.4f, 0.0f, 0.3f).env (2, 0.0f, 0.006f, 0.0f, 0.01f).mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f)
             .filt (2, BP12, 2200.0f, 0.2f).set ("f2Mix", 0.6f).reverb (0.25f).master (0.85f));

        //======================================================================
        // Keys, pianos, organs
        add (B ("Electric Piano", "Keys", { "R&B", "Soul", "Jazz", "Pop" }).tbl ("a", "Electric Piano", 0.0f).amp (0.002f, 2.5f, 0.0f, 0.4f)
             .env (2, 0.0f, 0.6f, 0.0f, 0.3f).mod (srcEnv2, "aPos", 0.6f).mod (srcVelocity, "aPos", -0.3f).set ("velAmp", 0.8f)
             .chorus (0.25f, 0.3f, 0.5f).reverb (0.18f));
        add (B ("Lo-Fi Keys", "Keys", { "Lo-Fi", "Hip Hop", "Chill" }).tbl ("a", "Electric Piano", 0.2f).amp (0.003f, 2.0f, 0.2f, 0.4f)
             .filt (1, LP12, 1800.0f, 0.1f).dist (CRUSH, 0.3f, 0.4f).noise (2, 0.2f, 0.4f, 3).chorus (0.3f, 0.6f, 0.25f).reverb (0.2f));
        add (B ("Tremolo EP", "Keys", { "Soul", "R&B", "Gospel" }).tbl ("a", "Electric Piano", 0.1f).amp (0.002f, 3.0f, 0.1f, 0.5f)
             .lfo (1, SINE, FREE, 4.5f, 0).mod (srcLfo1, "aPan", 0.6f).mod (srcLfo1, "aLevel", 0.15f).reverb (0.2f));
        add (B ("Wurly Grit", "Keys", { "Soul", "Rock", "Lo-Fi" }).tbl ("a", "Electric Piano", 0.3f).on ("b").tbl ("b", "Hollow", 0.3f).set ("bLevel", 0.3f)
             .amp (0.002f, 1.8f, 0.2f, 0.3f).dist (TUBE, 0.35f, 0.6f).mod (srcVelocity, "dsDrive", 0.3f).reverb (0.15f));
        add (B ("Synth Piano", "Piano", { "Pop", "EDM", "House" }).tbl ("a", "Saw Harmonics", 0.8f).filt (1, LP12, 2500.0f, 0.1f).amp (0.002f, 2.5f, 0.0f, 0.5f)
             .env (2, 0.0f, 0.7f, 0.1f, 0.3f).mod (srcEnv2, "f1Cutoff", 0.25f).mod (srcVelocity, "f1Cutoff", 0.25f).set ("velAmp", 0.8f).reverb (0.22f));
        add (B ("Felt Piano (synth)", "Piano", { "Lo-Fi", "Ambient", "Cinematic" }).tbl ("a", "Basic Shapes", 0.2f).on ("b").tbl ("b", "Electric Piano", 0.5f).set ("bLevel", 0.4f)
             .filt (1, LP12, 1200.0f, 0.05f).amp (0.004f, 3.0f, 0.0f, 0.6f).mod (srcVelocity, "f1Cutoff", 0.2f).noise (1, 0.05f, 0.3f).reverb (0.35f, 3.0f));
        add (B ("House Piano Stab", "Piano", { "House", "Dance" }).tbl ("a", "Saw Harmonics", 0.9f).on ("b").tbl ("b", "Bell Partials", 0.8f).set ("bLevel", 0.3f)
             .filt (1, LP12, 4500.0f, 0.1f).amp (0.001f, 0.9f, 0.0f, 0.3f).reverb (0.2f).squash (0.25f));
        add (B ("Gospel Organ", "Organ", { "Gospel", "Soul", "R&B" }).tbl ("a", "Organ", 0.75f).amp (0.005f, 0.1f, 1.0f, 0.08f).chorus (0.4f, 0.5f, 6.0f)
             .dist (TUBE, 0.2f, 0.5f).mod (srcModWheel, "chRate", -0.5f).reverb (0.15f));
        add (B ("Jazz Organ", "Organ", { "Jazz", "Soul", "Funk" }).tbl ("a", "Organ", 0.4f).amp (0.003f, 0.1f, 1.0f, 0.06f).noise (4, 0.05f, 0.9f)
             .env (2, 0.0f, 0.02f, 0.0f, 0.01f).mod (srcEnv2, "nzLevel", 0.5f).set ("nzLevel", 0.0f).chorus (0.35f, 0.4f, 5.5f).reverb (0.12f));
        add (B ("Rock Organ Drive", "Organ", { "Rock", "Blues" }).tbl ("a", "Organ", 1.0f).amp (0.003f, 0.1f, 1.0f, 0.08f).dist (TUBE, 0.55f).chorus (0.35f, 0.5f, 6.2f)
             .eq (-2.0f, 120.0f, -3.0f, 6000.0f));
        add (B ("Church Organ (synth)", "Organ", { "Gospel", "Cinematic", "Orchestral" }).tbl ("a", "Organ", 0.9f).on ("b").tbl ("b", "Organ", 0.3f)
             .set ("bOct", -1.0f).set ("bLevel", 0.5f).amp (0.06f, 0.1f, 1.0f, 0.8f).reverb (0.45f, 6.0f, 0.9f));
        add (B ("Clav Funk (synth)", "Keys", { "Funk", "Soul" }).tbl ("a", "Pulse Width", 0.75f).filt (1, BP12, 1600.0f, 0.4f).set ("f1Key", 0.6f)
             .amp (0.001f, 0.6f, 0.0f, 0.06f).env (2, 0.0f, 0.15f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.25f).mod (srcVelocity, "f1Cutoff", 0.15f));
        add (B ("Toy Piano", "Keys", { "Pop", "Lo-Fi", "Cinematic" }).tbl ("a", "Glass", 0.3f).on ("b").tbl ("b", "Bell Partials", 0.9f).set ("bOct", 1.0f).set ("bLevel", 0.3f)
             .amp (0.001f, 0.7f, 0.0f, 0.3f).reverb (0.2f));

        //======================================================================
        // Pads
        add (B ("Warm Analog Pad", "Pad", { "Pop", "R&B", "Ambient" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 5, 0.3f).filt (1, LP24, 1300.0f, 0.15f)
             .amp (0.7f, 1.0f, 1.0f, 1.8f).lfo (1, SINE, FREE, 0.15f, 0).mod (srcLfo1, "f1Cutoff", 0.08f).chorus (0.3f).reverb (0.35f, 4.0f).master (0.6f));
        add (B ("Dark Cinema Pad", "Pad", { "Cinematic", "Trap", "Ambient" }).tbl ("a", "Strings", 0.4f).uni ("a", 6, 0.25f).on ("b").tbl ("b", "Choir", 0.5f).set ("bOct", -1.0f)
             .filt (1, LP24, 700.0f, 0.2f).amp (1.5f, 2.0f, 1.0f, 3.0f).env (2, 3.0f, 2.0f, 1.0f, 2.0f).mod (srcEnv2, "f1Cutoff", 0.2f).reverb (0.5f, 7.0f, 0.9f).master (0.6f));
        add (B ("Choir Pad", "Vocal", { "Gospel", "Cinematic", "Orchestral", "Ambient" }).tbl ("a", "Choir", 0.5f).uni ("a", 6, 0.22f).amp (0.5f, 1.0f, 1.0f, 2.0f)
             .lfo (1, SINE, FREE, 0.2f, 0).mod (srcLfo1, "aPos", 0.2f).chorus (0.3f).reverb (0.45f, 5.0f));
        add (B ("Vocal Aahs (synth)", "Vocal", { "Pop", "R&B", "Gospel" }).tbl ("a", "Vowels", 0.0f).uni ("a", 5, 0.2f).amp (0.25f, 1.0f, 1.0f, 1.2f)
             .lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.8f).mod (srcLfo1, "aFine", 0.08f).filt (1, LP12, 4000.0f, 0.1f).reverb (0.35f, 3.5f));
        add (B ("Glass Pad", "Pad", { "Ambient", "Pop", "Cinematic" }).tbl ("a", "Glass", 0.3f).uni ("a", 4, 0.2f).lfo (1, TRI, BARS2).mod (srcLfo1, "aPos", 0.35f)
             .amp (0.8f, 1.0f, 1.0f, 2.5f).reverb (0.45f, 5.0f).delay (0.2f, QD));
        add (B ("Ambient Wash", "Pad", { "Ambient", "Cinematic", "Lo-Fi" }).tbl ("a", "Spectral Noise", 0.3f).uni ("a", 8, 0.35f).lfo (1, SMOOTH, BARS4).mod (srcLfo1, "aPos", 0.6f)
             .filt (1, LP24, 2500.0f, 0.15f).amp (2.0f, 1.0f, 1.0f, 4.0f).reverb (0.6f, 9.0f, 1.0f).master (0.55f));
        add (B ("Sweep Pad", "Pad", { "Trance", "EDM", "Synthwave" }).tbl ("a", "Saw Harmonics", 0.6f).uni ("a", 7, 0.3f).filt (1, LP24, 900.0f, 0.4f)
             .lfo (1, TRI, BARS4).mod (srcLfo1, "f1Cutoff", 0.3f).amp (0.6f, 1.0f, 1.0f, 2.0f).phaser_on().reverb (0.35f).master (0.6f));
        add (B ("R&B Velvet Pad", "Pad", { "R&B", "Soul", "Neo Soul" }).tbl ("a", "Electric Piano", 0.5f).uni ("a", 4, 0.2f).on ("b").tbl ("b", "Basic Shapes", 0.2f)
             .set ("bLevel", 0.5f).filt (1, LP12, 2000.0f, 0.1f).amp (0.3f, 1.0f, 1.0f, 1.5f).chorus (0.35f).reverb (0.3f));
        add (B ("Lo-Fi Tape Pad", "Pad", { "Lo-Fi", "Hip Hop", "Chill" }).tbl ("a", "Strings", 0.3f).uni ("a", 4, 0.25f).filt (1, LP12, 1400.0f, 0.1f)
             .amp (0.4f, 1.0f, 1.0f, 1.2f).lfo (1, SINE, FREE, 0.4f, 0).mod (srcLfo1, "aFine", 0.05f).dist (CRUSH, 0.25f, 0.4f).noise (2, 0.2f, 0.4f, 3).reverb (0.3f));
        add (B ("Trap Dark Pad", "Pad", { "Trap", "Drill", "Hip Hop" }).tbl ("a", "Choir", 0.3f).uni ("a", 5, 0.25f).filt (1, LP24, 1100.0f, 0.15f)
             .amp (0.6f, 1.0f, 1.0f, 2.0f).reverb (0.45f, 5.0f).master (0.6f));
        add (B ("Gospel Pad", "Pad", { "Gospel", "Soul", "R&B" }).tbl ("a", "Organ", 0.5f).uni ("a", 4, 0.15f).on ("b").tbl ("b", "Choir", 0.6f).set ("bLevel", 0.5f)
             .amp (0.4f, 1.0f, 1.0f, 1.5f).chorus (0.3f).reverb (0.35f, 4.0f));
        add (B ("Synthwave Brass Pad", "Pad", { "Synthwave", "80s" }).tbl ("a", "Brass", 0.6f).uni ("a", 5, 0.3f).filt (1, LP24, 2500.0f, 0.15f)
             .amp (0.15f, 1.0f, 1.0f, 1.0f).env (2, 0.15f, 0.5f, 0.6f, 0.5f).mod (srcEnv2, "f1Cutoff", 0.2f).chorus (0.4f).reverb (0.3f));
        add (B ("Evolving Vowel Pad", "Pad", { "Ambient", "Cinematic", "Electronic" }).tbl ("a", "Vowels", 0.5f).uni ("a", 6, 0.3f).lfo (1, SMOOTH, BARS2)
             .mod (srcLfo1, "aPos", 0.5f).lfo (2, SINE, BARS4).mod (srcLfo2, "aPan", 0.4f).amp (1.0f, 1.0f, 1.0f, 3.0f).reverb (0.5f, 6.0f));
        add (B ("Reggae Organ Bubble", "Organ", { "Reggae", "Dub" }).tbl ("a", "Organ", 0.35f).amp (0.002f, 0.15f, 0.0f, 0.05f).filt (1, LP12, 1800.0f, 0.1f)
             .chorus (0.25f, 0.3f, 5.0f).reverb (0.12f));

        //======================================================================
        // Strings, brass and winds (made by the synth: until the sample libraries arrive)
        add (B ("String Ensemble (synth)", "Strings", { "Orchestral", "Cinematic", "Pop", "Disco" }).tbl ("a", "Strings", 0.55f).uni ("a", 7, 0.25f, 0.8f)
             .filt (1, LP12, 4000.0f, 0.05f).amp (0.25f, 1.0f, 1.0f, 0.8f).lfo (1, SINE, FREE, 5.2f, 1).set ("l1Rise", 0.6f).mod (srcLfo1, "aFine", 0.05f)
             .mod (srcVelocity, "f1Cutoff", 0.15f).chorus (0.3f).reverb (0.4f, 3.5f).master (0.6f));
        add (B ("Violin Section (synth)", "Strings", { "Orchestral", "Cinematic", "Latin" }).tbl ("a", "Strings", 0.75f).uni ("a", 5, 0.2f).filt (1, BP12, 2200.0f, 0.1f)
             .set ("f1Mix", 0.5f).amp (0.18f, 1.0f, 1.0f, 0.6f).lfo (1, SINE, FREE, 5.6f, 1).set ("l1Rise", 0.5f).mod (srcLfo1, "aFine", 0.07f).reverb (0.4f, 3.5f));
        add (B ("Cello Section (synth)", "Strings", { "Orchestral", "Cinematic" }).tbl ("a", "Strings", 0.4f).uni ("a", 4, 0.18f).filt (1, LP12, 1800.0f, 0.1f)
             .amp (0.15f, 1.0f, 1.0f, 0.7f).lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.6f).mod (srcLfo1, "aFine", 0.06f).reverb (0.38f, 3.5f));
        add (B ("Pizzicato (synth)", "Strings", { "Orchestral", "Cinematic", "Pop" }).tbl ("a", "Strings", 0.5f).uni ("a", 3, 0.15f).filt (1, LP12, 2500.0f, 0.1f)
             .amp (0.001f, 0.25f, 0.0f, 0.2f).env (2, 0.0f, 0.06f, 0.0f, 0.05f).mod (srcEnv2, "f1Cutoff", 0.2f).reverb (0.35f, 2.5f));
        add (B ("Staccato Strings (synth)", "Strings", { "Cinematic", "Orchestral", "Trap" }).tbl ("a", "Strings", 0.7f).uni ("a", 5, 0.2f)
             .amp (0.008f, 0.18f, 0.0f, 0.12f).filt (1, LP12, 3500.0f, 0.1f).reverb (0.35f));
        add (B ("Orchestra Brass (synth)", "Brass", { "Orchestral", "Cinematic", "Trap" }).tbl ("a", "Brass", 0.3f).uni ("a", 4, 0.15f)
             .amp (0.06f, 1.0f, 1.0f, 0.3f).env (2, 0.08f, 0.6f, 0.5f, 0.3f).mod (srcEnv2, "aPos", 0.4f).mod (srcVelocity, "aPos", 0.3f)
             .filt (1, LP12, 3000.0f, 0.1f).reverb (0.4f, 3.5f));
        add (B ("French Horns (synth)", "Brass", { "Orchestral", "Cinematic" }).tbl ("a", "Brass", 0.15f).uni ("a", 3, 0.12f).filt (1, LP12, 1200.0f, 0.1f)
             .amp (0.12f, 1.0f, 1.0f, 0.5f).env (2, 0.15f, 0.8f, 0.4f, 0.3f).mod (srcEnv2, "f1Cutoff", 0.15f).reverb (0.45f, 4.0f));
        add (B ("Trumpet Lead (synth)", "Brass", { "Jazz", "Latin", "Salsa", "Ska" }).tbl ("a", "Brass", 0.55f).amp (0.03f, 1.0f, 1.0f, 0.15f)
             .env (2, 0.03f, 0.25f, 0.6f, 0.2f).mod (srcEnv2, "aPos", 0.3f).mod (srcVelocity, "aPos", 0.2f).lfo (1, SINE, FREE, 5.5f, 1).set ("l1Rise", 0.4f)
             .mod (srcLfo1, "aFine", 0.07f).filt (1, BP12, 1800.0f, 0.15f).set ("f1Mix", 0.5f).mono (0.04f, false).reverb (0.25f));
        add (B ("Horn Stabs (synth)", "Brass", { "Funk", "Soul", "Hip Hop", "Ska" }).tbl ("a", "Brass", 0.7f).uni ("a", 4, 0.18f).amp (0.015f, 0.35f, 0.3f, 0.12f)
             .env (2, 0.0f, 0.2f, 0.3f, 0.1f).mod (srcEnv2, "aPos", 0.3f).reverb (0.2f).squash (0.2f));
        add (B ("Synth Brass", "Brass", { "80s", "Synthwave", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 5, 0.3f).filt (1, LP24, 1200.0f, 0.15f)
             .amp (0.05f, 1.0f, 1.0f, 0.3f).env (2, 0.05f, 0.4f, 0.4f, 0.3f).mod (srcEnv2, "f1Cutoff", 0.3f).chorus (0.3f).reverb (0.25f));
        add (B ("Flute (synth)", "Winds", { "Orchestral", "Latin", "Drill", "Jazz" }).tbl ("a", "Basic Shapes", 0.05f).noise (4, 0.2f, 0.8f).amp (0.06f, 1.0f, 1.0f, 0.15f)
             .lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.5f).mod (srcLfo1, "aFine", 0.06f).mod (srcLfo1, "aLevel", 0.05f).reverb (0.3f, 3.0f));
        add (B ("Pan Flute (synth)", "Winds", { "World", "Latin", "Ambient" }).tbl ("a", "Hollow", 0.1f).noise (4, 0.3f, 0.7f).amp (0.08f, 0.3f, 0.8f, 0.3f)
             .env (2, 0.0f, 0.08f, 0.0f, 0.05f).mod (srcEnv2, "nzLevel", 0.4f).reverb (0.35f, 3.0f));
        add (B ("Clarinet (synth)", "Winds", { "Jazz", "Orchestral" }).tbl ("a", "Hollow", 0.5f).filt (1, LP12, 2200.0f, 0.1f).amp (0.04f, 1.0f, 1.0f, 0.12f)
             .mod (srcVelocity, "aPos", 0.3f).lfo (1, SINE, FREE, 4.8f, 1).set ("l1Rise", 0.6f).mod (srcLfo1, "aFine", 0.04f).reverb (0.25f));
        add (B ("Oboe (synth)", "Winds", { "Orchestral", "Cinematic" }).tbl ("a", "Vowels", 0.6f).filt (1, BP12, 1400.0f, 0.2f).set ("f1Mix", 0.6f)
             .amp (0.05f, 1.0f, 1.0f, 0.12f).lfo (1, SINE, FREE, 5.3f, 1).set ("l1Rise", 0.4f).mod (srcLfo1, "aFine", 0.05f).reverb (0.3f));
        add (B ("Sax-ish Lead (synth)", "Winds", { "Jazz", "Soul", "Funk", "R&B" }).tbl ("a", "Brass", 0.5f).on ("b").tbl ("b", "Vowels", 0.2f).set ("bLevel", 0.4f)
             .noise (4, 0.12f, 0.7f).amp (0.03f, 1.0f, 1.0f, 0.15f).mod (srcVelocity, "aPos", 0.35f).lfo (1, SINE, FREE, 5.0f, 1).set ("l1Rise", 0.5f)
             .mod (srcLfo1, "aFine", 0.08f).dist (TUBE, 0.2f, 0.5f).mono (0.05f, false).reverb (0.25f));
        add (B ("Timpani Roll (synth)", "Percussion", { "Orchestral", "Cinematic" }).tbl ("a", "Sub Thump", 0.4f).noise (1, 0.25f, 0.3f)
             .amp (0.002f, 1.2f, 0.0f, 0.6f).env (2, 0.0f, 0.08f, 0.0f, 0.05f).mod (srcEnv2, "aSemi", 0.08f).reverb (0.45f, 4.0f));
        add (B ("Celesta (synth)", "Mallets", { "Orchestral", "Cinematic", "Pop" }).tbl ("a", "Bell Partials", 0.7f).on ("b").tbl ("b", "Glass", 0.2f).set ("bOct", 1.0f)
             .set ("bLevel", 0.3f).amp (0.001f, 1.2f, 0.0f, 0.6f).reverb (0.4f, 3.5f));

        //======================================================================
        // Bells and mallets
        add (B ("Music Box", "Bells", { "Cinematic", "Lo-Fi", "Pop" }).tbl ("a", "Glass", 0.6f).set ("aOct", 1.0f).amp (0.001f, 1.6f, 0.0f, 0.8f).reverb (0.35f, 3.0f));
        add (B ("Church Bell", "Bells", { "Cinematic", "Trap", "Orchestral" }).tbl ("a", "FM Bells", 0.8f).on ("b").tbl ("b", "Bell Partials", 1.0f).set ("bOct", -1.0f)
             .amp (0.001f, 5.0f, 0.0f, 3.0f).reverb (0.45f, 6.0f));
        add (B ("Vibraphone (synth)", "Mallets", { "Jazz", "Lo-Fi", "Soul" }).tbl ("a", "Electric Piano", 0.0f).on ("b").tbl ("b", "Bell Partials", 0.5f).set ("bLevel", 0.35f)
             .amp (0.001f, 3.0f, 0.0f, 0.8f).lfo (1, SINE, FREE, 5.0f, 0).mod (srcLfo1, "aLevel", 0.15f).mod (srcLfo1, "bLevel", 0.15f).reverb (0.3f));
        add (B ("Glockenspiel (synth)", "Mallets", { "Orchestral", "Pop", "Cinematic" }).tbl ("a", "Glass", 0.9f).set ("aOct", 1.0f).amp (0.001f, 1.0f, 0.0f, 0.5f).reverb (0.3f));
        add (B ("Trap Bells Hard", "Bells", { "Trap", "Drill" }).tbl ("a", "FM Bells", 0.6f).amp (0.001f, 1.0f, 0.0f, 0.5f).dist (SOFT, 0.2f, 0.5f).reverb (0.35f).delay (0.2f, ED));
        add (B ("Digital Bell Keys", "Bells", { "Pop", "EDM", "Future Bass" }).tbl ("a", "FM Bells", 0.3f).uni ("a", 3, 0.12f).env (2, 0.0f, 0.4f, 0.0f, 0.3f)
             .mod (srcEnv2, "aPos", 0.4f).amp (0.001f, 1.2f, 0.1f, 0.4f).reverb (0.3f));
        add (B ("Xylophone (synth)", "Mallets", { "Orchestral", "Pop", "Latin" }).tbl ("a", "FM Bells", 0.1f).amp (0.001f, 0.25f, 0.0f, 0.2f).noise (0, 0.2f, 0.9f)
             .env (2, 0.0f, 0.008f, 0.0f, 0.01f).mod (srcEnv2, "nzLevel", 0.5f).set ("nzLevel", 0.0f).reverb (0.2f));

        //======================================================================
        // Chords and stabs
        add (B ("House Chord Stab", "Chords", { "House", "Deep House", "Garage" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 4, 0.2f).on ("b").tbl ("b", "Organ", 0.5f)
             .set ("bLevel", 0.4f).filt (1, LP24, 1600.0f, 0.25f).amp (0.001f, 0.35f, 0.0f, 0.2f).env (2, 0.0f, 0.2f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.25f)
             .reverb (0.3f).delay (0.15f, ED));
        add (B ("Rave Stab", "Chords", { "Rave", "Techno", "Breakbeat" }).tbl ("a", "Digital", 0.4f).uni ("a", 3, 0.2f).filt (1, LP12, 3000.0f, 0.3f)
             .amp (0.001f, 0.4f, 0.0f, 0.2f).reverb (0.3f).dist (SOFT, 0.2f));
        add (B ("Trance Gate Chords", "Chords", { "Trance", "EDM" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 7, 0.35f).lfo (1, SQUARE, S)
             .mod (srcLfo1, "aLevel", -0.5f).filt (1, LP24, 3500.0f, 0.15f).reverb (0.3f).delay (0.2f, ED).master (0.6f));
        add (B ("Neo Soul Chords", "Chords", { "Neo Soul", "R&B", "Jazz" }).tbl ("a", "Electric Piano", 0.25f).on ("b").tbl ("b", "Basic Shapes", 0.2f).set ("bLevel", 0.3f)
             .amp (0.004f, 2.0f, 0.3f, 0.6f).chorus (0.3f).reverb (0.25f));
        add (B ("Reggaeton Chord Stab", "Chords", { "Reggaeton", "Latin", "Pop" }).tbl ("a", "Saw Harmonics", 0.8f).uni ("a", 5, 0.25f).filt (1, LP24, 2500.0f, 0.2f)
             .amp (0.001f, 0.3f, 0.0f, 0.2f).reverb (0.25f).delay (0.15f, ED));
        add (B ("Dub Chord Echo", "Chords", { "Dub", "Reggae", "Dub Techno" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 3, 0.15f).filt (1, BP12, 900.0f, 0.4f)
             .amp (0.001f, 0.25f, 0.0f, 0.2f).delay (0.4f, QD, 0.6f).reverb (0.35f, 4.0f));
        add (B ("Future Bass Supersaw Chords", "Chords", { "Future Bass", "EDM", "Pop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 9, 0.45f).filt (1, LP24, 4000.0f, 0.15f)
             .lfo (1, RAMP, Q, 2.0f, 1).mod (srcLfo1, "aLevel", 0.4f).amp (0.02f, 1.0f, 1.0f, 0.4f).squash (0.4f).reverb (0.3f).master (0.55f));
        add (B ("Jazz Comp Keys", "Chords", { "Jazz", "Bossa Nova", "Lo-Fi" }).tbl ("a", "Electric Piano", 0.15f).amp (0.002f, 1.5f, 0.0f, 0.3f).filt (1, LP12, 2600.0f, 0.1f)
             .mod (srcVelocity, "f1Cutoff", 0.2f).reverb (0.2f));
        add (B ("Afrobeats Chord Pluck", "Chords", { "Afrobeats", "Amapiano", "Pop" }).tbl ("a", "Electric Piano", 0.5f).on ("b").tbl ("b", "Bell Partials", 0.6f).set ("bLevel", 0.3f)
             .amp (0.001f, 0.6f, 0.0f, 0.3f).reverb (0.25f).delay (0.15f, ED));
        add (B ("Rock Power Chord (synth)", "Guitar", { "Rock", "Metal", "Punk" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 3, 0.15f).on ("b").tbl ("b", "Basic Shapes", 1.0f)
             .set ("bSemi", 7.0f).set ("bLevel", 0.5f).dist (HARD, 0.75f).filt (1, LP24, 3000.0f, 0.2f).eq (-4.0f, 120.0f, -6.0f, 5500.0f)
             .amp (0.002f, 1.2f, 0.7f, 0.2f).reverb (0.15f).master (0.6f));
        add (B ("Grunge Chug (synth)", "Guitar", { "Rock", "Metal" }).tbl ("a", "Saw Harmonics", 1.0f).uni ("a", 2, 0.1f).dist (FOLD, 0.6f).filt (1, LP24, 2400.0f, 0.15f)
             .amp (0.001f, 0.25f, 0.0f, 0.08f).eq (-2.0f, 100.0f, -6.0f, 5000.0f).master (0.65f));

        //======================================================================
        // Arpeggios (hold a chord)
        add (B ("Trap Bell Arp", "Arp", { "Trap", "Drill" }).tbl ("a", "FM Bells", 0.45f).amp (0.001f, 0.4f, 0.0f, 0.3f).arp (UP, S, 2, 0.5f).reverb (0.3f).delay (0.2f, ED));
        add (B ("Synthwave Arp", "Arp", { "Synthwave", "80s" }).tbl ("a", "Basic Shapes", 0.67f).filt (1, LP24, 1500.0f, 0.3f).amp (0.001f, 0.25f, 0.2f, 0.1f)
             .env (2, 0.0f, 0.15f, 0.0f, 0.1f).mod (srcEnv2, "f1Cutoff", 0.3f).arp (UPDOWN, S, 2, 0.6f).delay (0.25f, ED).chorus (0.2f));
        add (B ("Trance Run Arp", "Arp", { "Trance", "EDM" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 5, 0.3f).filt (1, LP24, 2500.0f, 0.25f)
             .amp (0.001f, 0.2f, 0.0f, 0.1f).arp (UP, S, 3, 0.5f).delay (0.2f, ED).reverb (0.3f).master (0.6f));
        add (B ("Lo-Fi Dream Arp", "Arp", { "Lo-Fi", "Chill", "Ambient" }).tbl ("a", "Electric Piano", 0.3f).amp (0.002f, 0.6f, 0.0f, 0.4f).arp (UPDOWN, E, 2, 0.8f)
             .dist (CRUSH, 0.3f, 0.4f).chorus (0.3f).reverb (0.35f));
        add (B ("Reggaeton Arp", "Arp", { "Reggaeton", "Latin" }).tbl ("a", "Pulse Width", 0.3f).amp (0.001f, 0.2f, 0.0f, 0.1f).arp (PLAYED, S, 1, 0.5f)
             .filt (1, LP12, 3500.0f, 0.15f).delay (0.15f, ED));
        add (B ("Afro Marimba Arp", "Arp", { "Afrobeats", "Amapiano" }).tbl ("a", "FM Bells", 0.15f).amp (0.001f, 0.35f, 0.0f, 0.2f).arp (UPDOWN, E, 2, 0.6f).reverb (0.2f));
        add (B ("Chiptune Arp", "Arp", { "Chiptune", "Pop" }).tbl ("a", "Chip", 0.2f).amp (0.001f, 0.1f, 0.8f, 0.03f).arp (UP, T32, 1, 0.9f));
        add (B ("Techno Sequence", "Arp", { "Techno", "Minimal" }).tbl ("a", "Basic Shapes", 0.67f).filt (1, LADDER, 600.0f, 0.6f, 0.3f).amp (0.001f, 0.15f, 0.0f, 0.08f)
             .lfo (1, SINE, BARS2).mod (srcLfo1, "f1Cutoff", 0.25f).arp (RANDOM, S, 2, 0.5f).delay (0.2f, ED));
        add (B ("Ambient Pluck Arp", "Arp", { "Ambient", "Cinematic" }).tbl ("a", "Glass", 0.4f).amp (0.001f, 0.8f, 0.0f, 0.6f).arp (UP, E, 3, 0.4f)
             .delay (0.35f, QD, 0.5f).reverb (0.45f, 5.0f));
        add (B ("Harp Glissando Arp", "Arp", { "Orchestral", "Cinematic" }).noise (0, 0.9f, 0.5f).set ("aOn", 0.0f).filt (1, COMB, 261.6f, 0.92f).set ("f1Key", 1.0f)
             .amp (0.001f, 1.5f, 0.0f, 0.8f).env (2, 0.0f, 0.012f, 0.0f, 0.01f).mod (srcEnv2, "nzLevel", 1.0f).set ("nzLevel", 0.0f).arp (UP, T32, 3, 0.9f)
             .reverb (0.35f, 3.0f).master (0.8f));

        //======================================================================
        // FX
        add (B ("White Noise Riser", "FX", { "EDM", "House", "Trap" }).set ("aOn", 0.0f).noise (0, 0.8f, 0.6f).filt (1, BP12, 300.0f, 0.4f)
             .amp (6.0f, 1.0f, 1.0f, 1.0f).env (2, 8.0f, 1.0f, 1.0f, 1.0f).mod (srcEnv2, "f1Cutoff", 0.75f).reverb (0.4f, 4.0f));
        add (B ("Downlifter", "FX", { "EDM", "Trap", "Cinematic" }).set ("aOn", 0.0f).noise (0, 0.8f, 0.6f).filt (1, BP12, 8000.0f, 0.3f)
             .amp (0.01f, 4.0f, 0.0f, 1.0f).env (2, 0.0f, 4.0f, 0.0f, 1.0f).set ("f1Cutoff", 300.0f).mod (srcEnv2, "f1Cutoff", 0.7f).reverb (0.4f));
        add (B ("Laser Zap", "FX", { "Dancehall", "Reggae", "Dubstep" }).tbl ("a", "Basic Shapes", 0.9f).amp (0.001f, 0.4f, 0.0f, 0.1f).env (2, 0.0f, 0.35f, 0.0f, 0.1f)
             .mod (srcEnv2, "aSemi", 1.0f).set ("aOct", 2.0f).delay (0.3f, ED));
        add (B ("Dub Siren", "FX", { "Reggae", "Dub", "Dancehall" }).tbl ("a", "Basic Shapes", 1.0f).lfo (1, SAWUP, FREE, 1.5f, 0).mod (srcLfo1, "aSemi", 0.3f)
             .mod (srcModWheel, "l1Rate", 0.4f).delay (0.4f, ED, 0.6f).reverb (0.3f));
        add (B ("Impact Boom", "FX", { "Cinematic", "Trap", "EDM" }).tbl ("a", "Sub Thump", 0.3f).noise (1, 0.5f, 0.3f).amp (0.001f, 3.0f, 0.0f, 2.0f)
             .env (2, 0.0f, 0.4f, 0.0f, 0.2f).mod (srcEnv2, "aSemi", 0.5f).reverb (0.5f, 6.0f, 1.0f).dist (TUBE, 0.3f));
        add (B ("Air Sweep", "FX", { "Ambient", "Cinematic", "EDM" }).set ("aOn", 0.0f).noise (4, 0.7f, 0.8f).filt (1, BP24, 1000.0f, 0.5f).lfo (1, SINE, BARS2)
             .mod (srcLfo1, "f1Cutoff", 0.5f).amp (1.0f, 1.0f, 1.0f, 2.0f).reverb (0.5f, 5.0f));
        add (B ("Vinyl Crackle Bed", "FX", { "Lo-Fi", "Hip Hop", "Chill" }).set ("aOn", 0.0f).noise (2, 0.8f, 0.4f).amp (0.01f, 1.0f, 1.0f, 0.5f).eq (-6.0f, 150.0f, -4.0f, 6000.0f));
        add (B ("Sci-Fi Bubbles", "FX", { "Electronic", "Ambient" }).tbl ("a", "Harmonic Sweep", 0.5f).lfo (1, SH, S).mod (srcLfo1, "aPos", 0.6f).mod (srcLfo1, "aSemi", 0.2f)
             .amp (0.01f, 1.0f, 1.0f, 0.5f).delay (0.3f, ED).reverb (0.4f));
        add (B ("Tape Stop Drop", "FX", { "Trap", "Hip Hop" }).tbl ("a", "Basic Shapes", 0.67f).uni ("a", 3, 0.2f).env (2, 1.0f, 0.1f, 1.0f, 0.2f).mod (srcEnv2, "aSemi", -0.5f)
             .filt (1, LP24, 3000.0f, 0.1f).mod (srcEnv2, "f1Cutoff", -0.3f).amp (0.001f, 1.0f, 0.0f, 0.1f));

        //======================================================================
        // Drums and percussion (play C3 and around)
        add (B ("Kick Deep", "Drums", { "Hip Hop", "Trap", "House" }).tbl ("a", "Sub Thump", 0.2f).amp (0.0005f, 0.5f, 0.0f, 0.1f).env (2, 0.0f, 0.05f, 0.0f, 0.05f)
             .mod (srcEnv2, "aSemi", 1.0f).set ("aOct", -1.0f).dist (TUBE, 0.25f).master (0.8f));
        add (B ("Kick Punch", "Drums", { "House", "Techno", "EDM" }).tbl ("a", "Sub Thump", 0.6f).amp (0.0005f, 0.3f, 0.0f, 0.06f).env (2, 0.0f, 0.03f, 0.0f, 0.03f)
             .mod (srcEnv2, "aSemi", 1.0f).set ("aOct", -1.0f).noise (0, 0.4f, 0.9f).env (3, 0.0f, 0.006f, 0.0f, 0.01f).mod (srcEnv3, "nzLevel", 0.5f)
             .set ("nzLevel", 0.0f).dist (SOFT, 0.35f).master (0.8f));
        add (B ("Snare Synth", "Drums", { "Hip Hop", "Trap", "Pop" }).tbl ("a", "Basic Shapes", 0.1f).set ("aLevel", 0.5f).amp (0.0005f, 0.18f, 0.0f, 0.1f)
             .noise (0, 0.8f, 0.65f).env (2, 0.0f, 0.04f, 0.0f, 0.03f).mod (srcEnv2, "aSemi", 0.3f).filt (1, HP12, 180.0f, 0.1f).reverb (0.2f, 1.2f));
        add (B ("Clap", "Drums", { "Trap", "House", "Reggaeton" }).set ("aOn", 0.0f).noise (0, 0.9f, 0.6f).filt (1, BP12, 1200.0f, 0.35f).amp (0.0005f, 0.22f, 0.0f, 0.15f)
             .lfo (1, SAWDOWN, FREE, 40.0f, 1).mod (srcLfo1, "nzLevel", 0.25f).reverb (0.25f, 1.5f));
        add (B ("Hi-Hat Closed", "Drums", { "Trap", "Drill", "Hip Hop", "House" }).set ("aOn", 0.0f).noise (3, 0.9f, 0.9f).filt (1, HP24, 7000.0f, 0.15f)
             .amp (0.0005f, 0.05f, 0.0f, 0.04f).set ("velAmp", 0.9f));
        add (B ("Hi-Hat Open", "Drums", { "House", "Disco", "Trap" }).set ("aOn", 0.0f).noise (3, 0.9f, 0.9f).filt (1, HP24, 6500.0f, 0.15f)
             .amp (0.0005f, 0.45f, 0.0f, 0.3f).set ("velAmp", 0.9f));
        add (B ("Cowbell", "Drums", { "Latin", "Reggaeton", "Trap", "Disco" }).tbl ("a", "Basic Shapes", 1.0f).on ("b").tbl ("b", "Basic Shapes", 1.0f).set ("bSemi", 7.6f)
             .filt (1, BP12, 900.0f, 0.4f).set ("f1Key", 1.0f).amp (0.0005f, 0.25f, 0.0f, 0.1f).set ("aOct", 1.0f).set ("bOct", 1.0f));
        add (B ("Rimshot", "Drums", { "Reggaeton", "Dancehall", "Hip Hop" }).tbl ("a", "FM Bells", 0.3f).set ("aOct", 1.0f).noise (0, 0.5f, 0.8f).amp (0.0005f, 0.05f, 0.0f, 0.04f)
             .filt (1, BP12, 1800.0f, 0.3f));
        add (B ("Conga (synth)", "Percussion", { "Latin", "Afrobeats", "Salsa" }).tbl ("a", "Basic Shapes", 0.05f).amp (0.0005f, 0.3f, 0.0f, 0.15f)
             .env (2, 0.0f, 0.03f, 0.0f, 0.02f).mod (srcEnv2, "aSemi", 0.25f).noise (1, 0.3f, 0.5f).env (3, 0.0f, 0.01f, 0.0f, 0.01f).mod (srcEnv3, "nzLevel", 0.4f)
             .set ("nzLevel", 0.0f));
        add (B ("Shaker", "Percussion", { "Latin", "Pop", "Afrobeats" }).set ("aOn", 0.0f).noise (0, 0.7f, 0.85f).filt (1, HP12, 5000.0f, 0.2f)
             .amp (0.02f, 0.08f, 0.0f, 0.05f));
        add (B ("Tom Synth", "Drums", { "80s", "Rock", "Pop" }).tbl ("a", "Basic Shapes", 0.05f).amp (0.0005f, 0.6f, 0.0f, 0.2f).env (2, 0.0f, 0.15f, 0.0f, 0.1f)
             .mod (srcEnv2, "aSemi", 0.35f).noise (1, 0.15f, 0.4f).reverb (0.25f));
        add (B ("Perc Wood Block", "Percussion", { "Latin", "Afrobeats", "Reggaeton" }).tbl ("a", "FM Bells", 0.05f).set ("aOct", 1.0f).amp (0.0005f, 0.08f, 0.0f, 0.05f)
             .filt (1, BP12, 2000.0f, 0.3f).set ("f1Key", 1.0f));

        return v;
    }
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = []
    {
        auto presets = build();
        // Each at a like level (measured: see NemixLevels.inc)
        static const std::pair<const char*, float> trims[] {
           #include "NemixLevels.inc"
        };
        const auto trimIndex = indexOf ("trim");
        for (auto& p : presets)
            for (const auto& [name, db] : trims)
                if (p.name == name)
                {
                    p.sound.push_back ({ trimIndex, db });
                    break;
                }
        return presets;
    }();
    return list;
}

const juce::StringArray& presetCategories()
{
    static const juce::StringArray c { "Bass", "808", "Lead", "Pluck", "Keys", "Piano", "Organ", "Pad", "Strings", "Brass", "Winds", "Vocal",
                                       "Bells", "Mallets", "Guitar", "Chords", "Arp", "Drums", "Percussion", "FX", "Yours" };
    return c;
}

const juce::StringArray& presetGenres()
{
    static const juce::StringArray g { "Trap", "Drill", "Hip Hop", "Reggaeton", "Latin", "Reggae", "Dancehall", "Afrobeats", "Amapiano", "R&B", "Soul",
                                       "Gospel", "Jazz", "Funk", "Pop", "Rock", "Metal", "EDM", "House", "Techno", "Trance", "Dubstep",
                                       "Drum & Bass", "Synthwave", "Lo-Fi", "Ambient", "Cinematic", "Orchestral" };
    return g;
}

juce::File userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Audio Tech Pro").getChildFile ("Nemix Presets");
}

std::vector<Preset> userPresets()
{
    std::vector<Preset> list;
    const auto folder = userPresetFolder();
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.nemix"))
    {
        const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        Preset p;
        p.user = true;
        p.file = f;
        p.name = f.getFileNameWithoutExtension();
        p.category = "Yours";
        for (const auto& line : lines)
        {
            if (line.startsWith ("category=")) p.category = line.fromFirstOccurrenceOf ("=", false, false);
            else if (line.startsWith ("sound=")) p.sound = nemixstate::fromText (line.fromFirstOccurrenceOf ("=", false, false));
        }
        p.genres = { "Yours" };
        list.push_back (std::move (p));
    }
    std::sort (list.begin(), list.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return list;
}

bool saveUserPreset (const juce::String& name, const juce::String& category, const nemixstate::Sound& sound)
{
    const auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".nemix");
    return file.replaceWithText ("nemix=1\ncategory=" + category + "\nsound=" + nemixstate::toText (sound) + "\n");
}

} // namespace riff::nemix
