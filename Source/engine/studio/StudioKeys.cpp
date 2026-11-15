#include "StudioInstruments.h"
#include "../nemix/NemixPresets.h"
#include "../../model/NemixState.h"

namespace riff::studio
{

//==============================================================================
// The polysynth
void PolySynth::prepare (double sampleRate)
{
    rate = sampleRate;
    for (auto& v : voices) v = {};
    chorusA.prepare (sampleRate);
    chorusB.prepare (sampleRate);
    for (auto& f : hp) f.reset();
}

void PolySynth::noteOn (int note, float velocity) noexcept
{
    Voice* chosen = nullptr;
    for (auto& v : voices)
        if (v.note == note && v.active()) { chosen = &v; break; }
    if (chosen == nullptr)
        for (auto& v : voices)
            if (! v.active()) { chosen = &v; break; }
    if (chosen == nullptr)
    {
        juce::uint64 oldest = std::numeric_limits<juce::uint64>::max();
        for (auto& v : voices)
            if (v.age < oldest) { oldest = v.age; chosen = &v; }
    }
    auto& v = *chosen;
    v.note = note;
    v.held = true;
    v.sustained = false;
    v.velocity = velocity;
    v.age = ++clock;
    if (patch.gate) v.env.set (0.002f, 0.01f, 1.0f, 0.01f, rate);
    else v.env.set (patch.a, patch.d, patch.s, patch.r, rate);
    v.env.start();
}

void PolySynth::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.note == note && v.held)
        {
            v.held = false;
            if (sustain) v.sustained = true;
            else v.env.stop();
        }
}

void PolySynth::setSustain (bool down) noexcept
{
    sustain = down;
    if (! down)
        for (auto& v : voices)
            if (v.sustained) { v.sustained = false; v.env.stop(); }
}

void PolySynth::allOff() noexcept
{
    for (auto& v : voices) v.env.stop();
}

void PolySynth::render (float* l, float* r, int n) noexcept
{
    const auto& P = patch;
    const auto lfoInc = P.lfoRate / rate;
    float lfo = 0.0f;
    const auto hpCut = P.hpf < 0.5f ? 0.0f : P.hpf < 1.5f ? 120.0f : P.hpf < 2.5f ? 300.0f : 700.0f;
    for (int i = 0; i < n; ++i)
    {
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        lfo = (float) std::sin (lfoPhase * juce::MathConstants<double>::twoPi);
        float out = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active())
                continue;
            const auto env = v.env.next();
            const auto pitch = (float) v.note + bend + lfo * P.vibrato * 0.5f;
            const auto freq = 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);
            const auto inc = freq / rate;
            v.phase += inc;
            if (v.phase >= 1.0) v.phase -= 1.0;
            v.subPhase += inc * 0.5;
            if (v.subPhase >= 1.0) v.subPhase -= 1.0;
            const auto ph = (float) v.phase;
            // band-limited a little (PolyBLEP) saw and pulse
            const auto blep = [] (float t, float dt)
            {
                if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
                if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
                return 0.0f;
            };
            const auto dt = (float) inc;
            auto saw = 2.0f * ph - 1.0f - blep (ph, dt);
            const auto width = juce::jlimit (0.05f, 0.95f, P.pw + P.pwm * 0.45f * lfo);
            auto pulse = (ph < width ? 1.0f : -1.0f) + blep (ph, dt) - blep (std::fmod (ph + 1.0f - width, 1.0f), dt);
            const auto sub = v.subPhase < 0.5 ? 0.7f : -0.7f;
            float x = saw * P.saw + pulse * P.pulse + sub * P.sub + (noise.nextFloat() * 2.0f - 1.0f) * P.noise;
            // the filter: cutoff from the knob, its envelope, key follow and the LFO
            const auto cutNorm = juce::jlimit (0.0f, 1.0f, P.cutoff + P.envAmt * env * 0.6f + P.lfoAmt * lfo * 0.3f
                                                           + P.keyFollow * ((float) v.note - 60.0f) / 96.0f);
            const auto cutoff = 30.0f * std::pow (600.0f, cutNorm);
            const auto q = 0.55f + P.res * 9.0f;
            v.f1.set (cutoff, 0.6f, rate);
            v.f2.set (cutoff, q * 0.5f + 0.4f, rate);
            x = v.f2.low (v.f1.low (x));
            out += x * env * (0.4f + 0.6f * v.velocity);
        }
        out *= P.level * 0.25f;
        l[i] += out;
        r[i] += out;
    }
    if (hpCut > 0.0f)
    {
        hp[0].set (hpCut, 0.7f, rate);
        hp[1].set (hpCut, 0.7f, rate);
        for (int i = 0; i < n; ++i) { l[i] = hp[0].high (l[i]); r[i] = hp[1].high (r[i]); }
    }
    // the chorus: I slower, II faster, both together
    if (P.chorus == 1 || P.chorus == 3) chorusA.process (l, r, n, 0.5f, 2.2f, 0.8f);
    if (P.chorus == 2 || P.chorus == 3) chorusB.process (l, r, n, 0.83f, 3.0f, 0.8f);
}

//==============================================================================
// The programs
namespace
{
    PolySynth::Patch poly (std::initializer_list<std::pair<const char*, float>> values)
    {
        PolySynth::Patch p;
        for (const auto& [k, v] : values)
        {
            const juce::String key (k);
            if (key == "saw") p.saw = v; else if (key == "pulse") p.pulse = v; else if (key == "pw") p.pw = v;
            else if (key == "pwm") p.pwm = v; else if (key == "sub") p.sub = v; else if (key == "noise") p.noise = v;
            else if (key == "hpf") p.hpf = v; else if (key == "cutoff") p.cutoff = v; else if (key == "res") p.res = v;
            else if (key == "env") p.envAmt = v; else if (key == "lfo") p.lfoAmt = v; else if (key == "kf") p.keyFollow = v;
            else if (key == "a") p.a = v; else if (key == "d") p.d = v; else if (key == "s") p.s = v; else if (key == "r") p.r = v;
            else if (key == "rate") p.lfoRate = v; else if (key == "vib") p.vibrato = v; else if (key == "chorus") p.chorus = (int) v;
            else if (key == "gate") p.gate = v > 0.5f; else if (key == "level") p.level = v;
        }
        return p;
    }

    std::vector<StudioKeys::Program> makePrograms()
    {
        std::vector<StudioKeys::Program> list;
        const auto rec = [&] (const char* name, const char* cat, const char* id, float tone = 0, float release = 0, float room = 0.25f, float tune = 0)
        {
            StudioKeys::Program p;
            p.name = name; p.category = cat; p.type = 0; p.source = id; p.tone = tone; p.release = release; p.room = room; p.tune = tune;
            list.push_back (p);
        };
        const auto syn = [&] (const char* name, const char* cat, PolySynth::Patch patch, float room = 0.2f)
        {
            StudioKeys::Program p;
            p.name = name; p.category = cat; p.type = 1; p.patch = patch; p.room = room;
            list.push_back (p);
        };
        // Pianos
        rec ("Concert Grand", "Piano", "grand-piano", 0, 0, 0.25f);
        rec ("Bright Grand", "Piano", "grand-piano", 0.5f, 0, 0.2f);
        rec ("Ballad Grand", "Piano", "grand-piano", -0.35f, 0.4f, 0.45f);
        rec ("Pop Grand", "Piano", "grand-piano", 0.7f, -0.2f, 0.15f);
        rec ("Felt Grand", "Piano", "grand-piano", -0.8f, 0.3f, 0.3f);
        rec ("Lo-Fi Grand", "Piano", "grand-piano", -0.6f, 0, 0.1f, -0.15f);
        rec ("Concert Hall Grand", "Piano", "grand-piano", 0.1f, 0.3f, 0.8f);
        rec ("Upright", "Piano", "upright-piano", 0, 0, 0.2f);
        rec ("Honky Upright", "Piano", "upright-piano", 0.4f, -0.2f, 0.1f, 0.12f);
        rec ("Electric Grand", "Piano", "electric-grand", 0, 0, 0.2f);
        // Electric pianos
        rec ("Tine EP", "Electric Piano", "rhodes", 0, 0, 0.2f);
        rec ("Mellow EP", "Electric Piano", "rhodes", -0.5f, 0.2f, 0.25f);
        rec ("Bright EP", "Electric Piano", "rhodes", 0.6f, 0, 0.15f);
        rec ("Lo-Fi EP", "Electric Piano", "rhodes", -0.7f, 0, 0.1f, -0.1f);
        // Strings, brass, winds
        rec ("Violin Section", "Strings", "violins", 0, 0.4f, 0.5f);
        rec ("Viola Section", "Strings", "violas", 0, 0.4f, 0.5f);
        rec ("Cello Section", "Strings", "cellos", 0, 0.4f, 0.5f);
        rec ("Tremolo Strings", "Strings", "violin-tremolo", 0, 0.4f, 0.5f);
        rec ("Pizzicato Violins", "Strings", "violin-pizz", 0, 0, 0.4f);
        rec ("Pizzicato Cellos", "Strings", "cello-pizz", 0, 0, 0.4f);
        rec ("Harp", "Strings", "harp", 0, 0.3f, 0.45f);
        rec ("Trumpet", "Brass", "trumpet", 0, 0.1f, 0.35f);
        rec ("Trumpet Staccato", "Brass", "trumpet-stac", 0, 0, 0.35f);
        rec ("French Horn", "Brass", "horn", 0, 0.2f, 0.45f);
        rec ("Trombone", "Brass", "trombone", 0, 0.1f, 0.35f);
        rec ("Tuba", "Brass", "tuba", 0, 0.1f, 0.35f);
        rec ("Flute", "Winds", "flute", 0, 0.1f, 0.4f);
        rec ("Oboe", "Winds", "oboe", 0, 0.1f, 0.4f);
        rec ("Clarinet", "Winds", "clarinet", 0, 0.1f, 0.4f);
        rec ("Bassoon", "Winds", "bassoon", 0, 0.1f, 0.4f);
        rec ("Glockenspiel", "Mallets", "glockenspiel", 0, 0.3f, 0.35f);
        rec ("Marimba", "Mallets", "marimba", 0, 0.2f, 0.3f);
        rec ("Nylon Guitar", "Guitar", "nylon-guitar", 0, 0.1f, 0.25f);
        rec ("Steel Guitar", "Guitar", "steel-guitar", 0, 0.1f, 0.25f);
        rec ("Clean Electric Guitar", "Guitar", "clean-electric", 0, 0.1f, 0.25f);
        rec ("Upright Bass", "Bass", "upright-bass", 0, 0, 0.2f);
        rec ("Finger Bass", "Bass", "finger-bass", 0, 0, 0.1f);
        // The polysynth
        syn ("Poly Warm Pad", "Poly Synth", poly ({ { "saw", 1 }, { "pulse", 0.4f }, { "pwm", 0.5f }, { "sub", 0.3f }, { "cutoff", 0.45f }, { "env", 0.15f }, { "a", 0.6f }, { "d", 1.5f }, { "s", 0.8f }, { "r", 1.6f }, { "rate", 0.8f }, { "chorus", 1 } }), 0.4f);
        syn ("Poly Brass", "Poly Synth", poly ({ { "saw", 1 }, { "sub", 0.2f }, { "cutoff", 0.3f }, { "env", 0.55f }, { "a", 0.06f }, { "d", 0.6f }, { "s", 0.6f }, { "r", 0.3f }, { "chorus", 1 } }));
        syn ("Poly Strings", "Poly Synth", poly ({ { "saw", 1 }, { "pulse", 0.6f }, { "pw", 0.3f }, { "pwm", 0.7f }, { "cutoff", 0.55f }, { "hpf", 1 }, { "a", 0.35f }, { "s", 0.9f }, { "r", 0.9f }, { "rate", 4.0f }, { "chorus", 3 } }), 0.4f);
        syn ("Hoover Stab", "Poly Synth", poly ({ { "saw", 1 }, { "pulse", 1 }, { "pwm", 0.9f }, { "sub", 0.6f }, { "cutoff", 0.7f }, { "res", 0.2f }, { "env", 0.3f }, { "a", 0.01f }, { "d", 0.4f }, { "s", 0.7f }, { "r", 0.3f }, { "rate", 6.0f }, { "chorus", 3 } }));
        syn ("Analog Bass", "Poly Synth", poly ({ { "saw", 0.6f }, { "pulse", 0.8f }, { "pw", 0.4f }, { "sub", 1 }, { "cutoff", 0.25f }, { "res", 0.35f }, { "env", 0.45f }, { "a", 0.002f }, { "d", 0.35f }, { "s", 0.3f }, { "r", 0.12f }, { "chorus", 0 } }), 0.05f);
        syn ("Sub Bass", "Poly Synth", poly ({ { "saw", 0 }, { "pulse", 0.3f }, { "sub", 1 }, { "cutoff", 0.2f }, { "a", 0.002f }, { "s", 1 }, { "r", 0.1f }, { "chorus", 0 } }), 0.0f);
        syn ("Pluck Keys", "Poly Synth", poly ({ { "saw", 0.7f }, { "pulse", 0.6f }, { "cutoff", 0.25f }, { "res", 0.25f }, { "env", 0.6f }, { "a", 0.002f }, { "d", 0.35f }, { "s", 0.0f }, { "r", 0.3f }, { "chorus", 1 } }));
        syn ("Square Lead", "Poly Synth", poly ({ { "saw", 0 }, { "pulse", 1 }, { "pw", 0.5f }, { "cutoff", 0.6f }, { "res", 0.15f }, { "env", 0.15f }, { "a", 0.01f }, { "s", 0.9f }, { "r", 0.2f }, { "vib", 0.4f }, { "rate", 5.5f }, { "chorus", 0 } }));
        syn ("Saw Lead", "Poly Synth", poly ({ { "saw", 1 }, { "sub", 0.3f }, { "cutoff", 0.65f }, { "res", 0.25f }, { "env", 0.2f }, { "a", 0.01f }, { "s", 0.9f }, { "r", 0.25f }, { "chorus", 1 } }));
        syn ("Glass Bells", "Poly Synth", poly ({ { "saw", 0 }, { "pulse", 1 }, { "pw", 0.15f }, { "cutoff", 0.5f }, { "res", 0.55f }, { "env", 0.5f }, { "kf", 1 }, { "a", 0.002f }, { "d", 1.8f }, { "s", 0 }, { "r", 1.5f }, { "chorus", 2 } }), 0.5f);
        syn ("Dreamy Organ", "Poly Synth", poly ({ { "saw", 0 }, { "pulse", 1 }, { "pw", 0.5f }, { "sub", 0.6f }, { "cutoff", 0.55f }, { "gate", 1 }, { "chorus", 2 } }), 0.3f);
        syn ("Juno-Style Choir", "Poly Synth", poly ({ { "saw", 0.4f }, { "pulse", 0.8f }, { "pw", 0.2f }, { "pwm", 0.4f }, { "noise", 0.08f }, { "cutoff", 0.4f }, { "res", 0.3f }, { "a", 0.5f }, { "s", 0.85f }, { "r", 1.2f }, { "chorus", 3 } }), 0.5f);
        syn ("Dark Sweep", "Poly Synth", poly ({ { "saw", 1 }, { "pulse", 0.5f }, { "cutoff", 0.2f }, { "res", 0.45f }, { "lfo", 0.8f }, { "rate", 0.15f }, { "a", 0.8f }, { "s", 1 }, { "r", 2.0f }, { "chorus", 1 } }), 0.5f);
        syn ("Chord Stab", "Poly Synth", poly ({ { "saw", 1 }, { "pulse", 0.5f }, { "cutoff", 0.35f }, { "env", 0.5f }, { "a", 0.002f }, { "d", 0.25f }, { "s", 0.1f }, { "r", 0.2f }, { "chorus", 3 } }));
        syn ("80s Poly Keys", "Poly Synth", poly ({ { "saw", 0.5f }, { "pulse", 1 }, { "pw", 0.35f }, { "pwm", 0.3f }, { "cutoff", 0.45f }, { "env", 0.35f }, { "a", 0.005f }, { "d", 0.9f }, { "s", 0.4f }, { "r", 0.5f }, { "chorus", 1 } }), 0.3f);
        syn ("Noise Sweep FX", "Poly Synth", poly ({ { "saw", 0 }, { "noise", 1 }, { "cutoff", 0.15f }, { "res", 0.6f }, { "env", 0.7f }, { "a", 1.5f }, { "d", 2 }, { "s", 0.3f }, { "r", 2 }, { "chorus", 3 } }), 0.6f);
        // Nemix's synths (its sounds that don't need the recorded library)
        for (const auto& preset : nemix::factoryPresets())
            if (! preset.needsLibrary && preset.category != "Drums" && preset.category != "Percussion")
            {
                StudioKeys::Program p;
                p.name = preset.name;
                p.category = "Nemix " + preset.category;
                p.type = 2;
                p.source = preset.name;
                p.room = 0.0f;
                list.push_back (p);
            }
        return list;
    }

    std::vector<Param> keysParams()
    {
        juce::StringArray names;
        for (const auto& p : StudioKeys::programs())
            names.add (p.name);
        juce::StringArray layerNames { "None" };
        layerNames.addArray (names);
        std::vector<Param> p;
        p.push_back ({ "program", "Program", 0.0f, (float) names.size() - 1.0f, 0.0f, names });
        p.push_back ({ "layer", "Layer", 0.0f, (float) names.size(), 0.0f, layerNames });
        p.push_back ({ "layerLevel", "Layer Level", -40.0f, 6.0f, -6.0f });
        p.push_back ({ "layerMode", "Layer Mode", 0, 1, 0, { "Layer", "Split" } });
        p.push_back ({ "split", "Split Point", 24, 96, 60 });
        p.push_back ({ "layerOct", "Layer Octave", -2, 2, 0 });
        p.push_back ({ "octave", "Octave", -3, 3, 0 });
        p.push_back ({ "lid", "Lid", 0.0f, 1.0f, 0.7f });
        p.push_back ({ "tune", "Tune", -1.0f, 1.0f, 0.0f });
        p.push_back ({ "hammer", "Hammer", 0.0f, 1.0f, 0.45f });
        p.push_back ({ "release", "Release", 0.0f, 1.0f, 0.6f });
        p.push_back ({ "tone", "Tone", -1.0f, 1.0f, 0.0f });
        p.push_back ({ "resonance", "Resonance", 0.0f, 1.0f, 0.4f });
        p.push_back ({ "ambience", "Ambience", 0.0f, 1.0f, 0.28f });
        p.push_back ({ "reverb", "Reverb", 0.0f, 1.0f, 0.38f });
        p.push_back ({ "master", "Master", -60.0f, 6.0f, -3.0f });
        p.push_back ({ "velocity", "Velocity", 0, 3, 0, { "Linear", "Soft", "Hard", "Fixed" } });
        p.push_back ({ "sostenuto", "Sostenuto", 0, 1, 0, { "Off", "On" } });
        return p;
    }
}

const std::vector<StudioKeys::Program>& StudioKeys::programs()
{
    static const auto list = makePrograms();
    return list;
}

juce::StringArray StudioKeys::categories()
{
    juce::StringArray c;
    for (const auto& p : programs())
        c.addIfNotAlreadyThere (p.category);
    return c;
}

StudioKeys::StudioKeys() : StudioInstrument ("keys", keysParams(), { "Close", "Wide", "Room" })
{
    for (auto& layer : layers)
    {
        layer.nemix = std::make_shared<Nemix>();
    }
    setDefault (partParam (1, "gain"), -8.0f);
    setDefault (partParam (2, "gain"), -6.0f);
    applyProgram (layers[0], 0);
}

StudioKeys::~StudioKeys() = default;

void StudioKeys::applyProgram (Layer& layer, int program)
{
    const auto& list = programs();
    if (! juce::isPositiveAndBelow (program, (int) list.size()))
    {
        layer.program = -1;
        layer.wanted.store (-1);
        return;
    }
    const auto& prog = list[(size_t) program];
    if (prog.type == 0)
    {
        layer.libraryIndex = nemix::samples::indexOf (prog.source);
        if (layer.libraryIndex >= 0)
            nemix::samples::request (layer.libraryIndex);
    }
    else if (prog.type == 2 && layer.nemix != nullptr)
    {
        for (const auto& preset : nemix::factoryPresets())
            if (preset.name == prog.source)
            {
                layer.nemix->resetToDefaults();
                for (const auto& [index, value] : preset.sound)
                    layer.nemix->setParameter (index, value);
                break;
            }
    }
    layer.wanted.store (program);
}

void StudioKeys::paramChanged (int index) noexcept
{
    const auto& id = params()[(size_t) index].id;
    if (id == "program")
        applyProgram (layers[0], choice (index));
    else if (id == "layer")
        applyProgram (layers[1], choice (index) - 1);
}

void StudioKeys::prepareVoices (double sampleRate, int maxBlockSize)
{
    voices.prepare (sampleRate);
    for (auto& layer : layers)
    {
        layer.poly.prepare (sampleRate);
        if (layer.nemix != nullptr)
            layer.nemix->prepare (sampleRate, maxBlockSize);
    }
    work.setSize (4, maxBlockSize);
    room.prepare (sampleRate);
    hall.prepare (sampleRate);
    for (auto& f : hammerF) f.reset();
    layerMidi[0].ensureSize (2048);
    layerMidi[1].ensureSize (2048);
}

bool StudioKeys::inRange (int layerIndex, int note) const noexcept
{
    if (layers[(size_t) layerIndex].program < 0)
        return false;
    if (choice (indexOf ("layerMode")) == 0)
        return true;
    const auto split = choice (indexOf ("split"));
    return layerIndex == 0 ? note >= split : note < split;   // (split: the layer below the point, the main program above)
}

void StudioKeys::startNote (Layer& layer, int note, int velocity, float gainDb) noexcept
{
    const auto& prog = programs()[(size_t) layer.program];
    const auto vel = dsp::velocityCurve ((float) velocity / 127.0f, choice (indexOf ("velocity"))) * (softDown ? 0.7f : 1.0f);
    if (prog.type == 0)
    {
        const auto hammer = p (indexOf ("hammer"));
        SampleVoices<64>::Start s;
        s.note = note;
        s.velocity = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f));
        s.part = 0;
        s.semitones = p (indexOf ("tune")) + prog.tune;
        s.gainDb = gainDb;
        s.release = 0.08f + 2.4f * std::pow (juce::jlimit (0.0f, 1.0f, p (indexOf ("release")) + prog.release * 0.5f), 2.0f);
        // the hammer: harder makes the loud notes brighter; the soft pedal, duller
        const auto bright = juce::jlimit (0.0f, 1.0f, 0.35f + vel * (0.4f + hammer * 0.6f) - (softDown ? 0.2f : 0.0f) + prog.tone * 0.25f);
        s.toneHz = bright >= 0.98f ? 0.0f : 600.0f * std::pow (32.0f, bright);
        voices.start (nemix::samples::find (layer.libraryIndex), s);
    }
    else if (prog.type == 1)
        layer.poly.noteOn (note, vel * dsp::dbToGain (gainDb));
    else
        layerMidi[(size_t) (&layer - layers.data())].addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.0f, 1.0f, vel * dsp::dbToGain (gainDb))), 0);
}

void StudioKeys::stopNote (Layer& layer, int note) noexcept
{
    if (layer.program < 0) return;
    const auto& prog = programs()[(size_t) layer.program];
    if (prog.type == 1) layer.poly.noteOff (note);
    else if (prog.type == 2) layerMidi[(size_t) (&layer - layers.data())].addEvent (juce::MidiMessage::noteOff (1, note), 0);
}

void StudioKeys::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midi) noexcept
{
    // programs chosen since the last block
    for (auto& layer : layers)
    {
        const auto wanted = layer.wanted.load (std::memory_order_relaxed);
        if (wanted != layer.program)
        {
            if (layer.program >= 0)
            {
                layer.poly.allOff();
                voices.allOff();
            }
            layer.program = wanted;
            if (wanted >= 0 && programs()[(size_t) wanted].type == 1)
                layer.poly.setPatch (programs()[(size_t) wanted].patch);
        }
    }

    auto* dl = work.getWritePointer (0);
    auto* dr = work.getWritePointer (1);
    juce::FloatVectorOperations::clear (dl, n);
    juce::FloatVectorOperations::clear (dr, n);
    layerMidi[0].clear();
    layerMidi[1].clear();
    const auto octave = choice (indexOf ("octave")) * 12;
    const auto layerOct = choice (indexOf ("layerOct")) * 12;
    const auto layerGain = p (indexOf ("layerLevel"));

    int done = 0;
    float* l1[1] { dl }, *r1[1] { dr };
    auto renderTo = [&] (int until)
    {
        if (until <= done) return;
        l1[0] = dl + done; r1[0] = dr + done;
        voices.render (l1, r1, 1, until - done, &noteLevel);
        for (auto& layer : layers)
            if (layer.program >= 0 && programs()[(size_t) layer.program].type == 1)
                layer.poly.render (dl + done, dr + done, until - done);
        done = until;
    };
    for (const auto m : midi)
    {
        renderTo (juce::jlimit (0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            const auto key = msg.getNoteNumber();
            down[(size_t) key] = true;
            for (int i = 0; i < 2; ++i)
                if (inRange (i, key))
                {
                    const auto note = juce::jlimit (0, 127, key + octave + (i == 1 ? layerOct : 0));
                    startNote (layers[(size_t) i], note, msg.getVelocity(), i == 1 ? layerGain : 0.0f);
                }
            lastNote.store (key);
            lastVelocity.store (msg.getVelocity());
            noteLevel[(size_t) key].store (1.0f);
        }
        else if (msg.isNoteOff())
        {
            const auto key = msg.getNoteNumber();
            down[(size_t) key] = false;
            const bool hold = sustainDown || (sostenutoDown && sostenutoHeld[(size_t) key]);
            for (int i = 0; i < 2; ++i)
            {
                const auto note = juce::jlimit (0, 127, key + octave + (i == 1 ? layerOct : 0));
                voices.noteOff (note, hold);
                if (! hold) stopNote (layers[(size_t) i], note);
            }
        }
        else if (msg.isController())
        {
            const auto cc = msg.getControllerNumber();
            const auto value = (float) msg.getControllerValue() / 127.0f;
            if (cc == 64)
            {
                // sustain (half pedal: the dampers part way - the notes ring a while, then stop)
                pedal = value;
                const auto wasDown = sustainDown;
                sustainDown = value >= 0.5f;
                for (auto& layer : layers) layer.poly.setSustain (sustainDown);
                if (wasDown && ! sustainDown)
                {
                    voices.pedalUp();
                    for (int i = 0; i < 2; ++i)
                        for (int k = 0; k < 128; ++k)
                            if (! down[(size_t) k]) stopNote (layers[(size_t) i], k);
                }
                for (int i = 0; i < 2; ++i)
                    layerMidi[(size_t) i].addEvent (msg, 0);
            }
            else if (cc == 66 && choice (indexOf ("sostenuto")) >= 0)
            {
                // sostenuto: only the notes held as it goes down keep sounding
                const auto wasDown = sostenutoDown;
                sostenutoDown = value >= 0.5f;
                if (sostenutoDown && ! wasDown)
                    sostenutoHeld = down;
                if (! sostenutoDown && wasDown)
                {
                    for (int k = 0; k < 128; ++k)
                        if (sostenutoHeld[(size_t) k] && ! down[(size_t) k] && ! sustainDown)
                        {
                            voices.noteOff (k, false);
                            for (int i = 0; i < 2; ++i) stopNote (layers[(size_t) i], k);
                        }
                    sostenutoHeld.fill (false);
                }
            }
            else if (cc == 67)
                softDown = value >= 0.5f;
            else if (cc == 123 || cc == 120)
            {
                voices.allOff();
                for (auto& layer : layers) layer.poly.allOff();
                for (int i = 0; i < 2; ++i) layerMidi[(size_t) i].addEvent (msg, 0);
            }
            else
                for (int i = 0; i < 2; ++i) layerMidi[(size_t) i].addEvent (msg, 0);
        }
        else if (msg.isPitchWheel())
        {
            const auto semis = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f * 2.0f;
            for (auto& layer : layers) layer.poly.pitchBend (semis);
            for (int i = 0; i < 2; ++i) layerMidi[(size_t) i].addEvent (msg, 0);
        }
    }
    renderTo (n);
    // Nemix's sounds (their notes this block, at its start: a few ms early at most)
    auto* nl = work.getWritePointer (2);
    auto* nr = work.getWritePointer (3);
    for (int i = 0; i < 2; ++i)
    {
        auto& layer = layers[(size_t) i];
        if (layer.program >= 0 && programs()[(size_t) layer.program].type == 2 && layer.nemix != nullptr)
        {
            layer.nemix->setPosition (songPosition, songPlaying, songBpm);
            layer.nemix->render (nl, nr, n, layerMidi[(size_t) i]);
            juce::FloatVectorOperations::add (dl, nl, n);
            juce::FloatVectorOperations::add (dr, nr, n);
        }
    }
    for (auto& level : noteLevel)
        level.store (level.load (std::memory_order_relaxed) * 0.9f, std::memory_order_relaxed);

    // The piano's sound: the lid (open: brighter and wider), its tone; the close mics, the wide pair, the room
    const auto lidOpen = p (indexOf ("lid"));
    const auto tone = p (indexOf ("tone"));
    for (int c = 0; c < 2; ++c)
        lid[c].set (900.0f, -1.5f * lidOpen, (lidOpen - 0.5f) * 8.0f + tone * 6.0f, rate);
    for (int i = 0; i < n; ++i)
    {
        dl[i] = lid[0].run (dl[i]);
        dr[i] = lid[1].run (dr[i]);
    }
    const auto master = dsp::dbToGain (p (indexOf ("master")));
    const auto width = 0.6f + 0.9f * lidOpen;
    for (int i = 0; i < n; ++i)
    {
        pl[0][i] += dl[i] * master;
        pr[0][i] += dr[i] * master;
        const auto mid = (dl[i] + dr[i]) * 0.5f, side = (dl[i] - dr[i]) * 0.5f * width * 1.6f;
        pl[1][i] += (mid + side) * master;
        pr[1][i] += (mid - side) * master;
    }
    // the room, the hall (reverb), and the strings ringing along while the pedal is down (resonance)
    const auto progRoom = layers[0].program >= 0 ? programs()[(size_t) layers[0].program].room : 0.25f;
    room.set (0.2f + 0.5f * p (indexOf ("ambience")), 0.35f, 6.0f);
    room.process (dl, dr, pl[2], pr[2], n, (0.4f + p (indexOf ("ambience"))) * master * (0.6f + progRoom));
    hall.set (0.85f, 0.3f, 25.0f);
    hall.process (dl, dr, pl[2], pr[2], n, (p (indexOf ("reverb")) * 0.8f + (sustainDown ? p (indexOf ("resonance")) * 0.25f : 0.0f)) * master);
}

juce::StringArray StudioKeys::screenLines() const
{
    const auto& list = programs();
    const auto prog = juce::jlimit (0, (int) list.size() - 1, choice (indexOf ("program")));
    const auto layer = choice (indexOf ("layer")) - 1;
    return { list[(size_t) prog].name,
             list[(size_t) prog].category.toUpperCase() + (layer >= 0 ? " + " + list[(size_t) layer].name.toUpperCase() : juce::String()),
             juce::String (juce::roundToInt (p (indexOf ("lid")) * 100.0f)) + "%",
             (p (indexOf ("tune")) >= 0 ? "+" : "") + juce::String (p (indexOf ("tune")), 1),
             juce::String (juce::roundToInt (p (indexOf ("hammer")) * 100.0f)) + "%",
             juce::String (juce::roundToInt (p (indexOf ("release")) * 100.0f)) + "%",
             "PROG A" + juce::String (prog / 100) + juce::String ((prog / 10) % 10) + juce::String (prog % 10) };
}

} // namespace riff::studio
