#include "BuiltInSynth.h"

namespace riff
{

namespace
{
    // Band-limiting correction for the saw and square edges (PolyBLEP).
    inline double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)       { t /= dt;             return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return t * t + t + t + 1.0; }
        return 0.0;
    }

    struct FilterCoefficients { float a1, a2, a3, k; };

    // Zero-delay-feedback state-variable filter (Simper / Cytomic).
    FilterCoefficients makeFilter (float cutoff, float resonance, double sampleRate) noexcept
    {
        const auto fc = juce::jlimit (20.0, sampleRate * 0.45, (double) cutoff);
        const auto g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
        const auto k = 2.0f - 1.95f * juce::jlimit (0.0f, 1.0f, resonance);
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        return { a1, g * a1, g * g * a1, k };
    }
}

BuiltInSynth::BuiltInSynth()
{
    prepare (44100.0, 512);
}

void BuiltInSynth::setSettings (const Settings& s) noexcept
{
    wave.store (juce::jlimit (0, 3, s.wave));
    cutoff.store (s.cutoff);
    resonance.store (s.resonance);
    attack.store (juce::jmax (0.001f, s.attack));
    decay.store (juce::jmax (0.001f, s.decay));
    sustain.store (juce::jlimit (0.0f, 1.0f, s.sustain));
    release.store (juce::jmax (0.001f, s.release));
    gain.store (s.gain);
}

void BuiltInSynth::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    preparedRate = newSampleRate;

    for (auto& v : voices)
    {
        v = Voice();
        v.envelope.setSampleRate (newSampleRate);
    }

    sustainPedal = false;
    heldBySustain.fill (false);
}

int BuiltInSynth::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

//==============================================================================
void BuiltInSynth::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    const juce::ADSR::Parameters envelope { attack.load(), decay.load(), sustain.load(), release.load() };
    for (auto& v : voices)
        v.envelope.setParameters (envelope);

    // Render up to each event, then apply it: sample-accurate timing.
    int position = 0;

    for (const auto meta : midi)
    {
        const auto eventPos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderVoices (left, right, position, eventPos);
        handleEvent (meta.data, meta.numBytes);
        position = eventPos;
    }

    renderVoices (left, right, position, numSamples);
}

void BuiltInSynth::handleEvent (const juce::uint8* data, int size) noexcept
{
    if (size < 1)
        return;

    const auto type = data[0] & 0xf0;
    const int d1 = size > 1 ? data[1] & 0x7f : 0;
    const int d2 = size > 2 ? data[2] & 0x7f : 0;

    if (type == 0x90 && d2 > 0)                        noteOn (d1, (float) d2 / 127.0f);
    else if (type == 0x80 || (type == 0x90 && d2 == 0)) noteOff (d1);
    else if (type == 0xb0)
    {
        if (d1 == 64)                                   // sustain pedal
        {
            sustainPedal = d2 >= 64;
            if (! sustainPedal)
                for (int n = 0; n < 128; ++n)
                    if (std::exchange (heldBySustain[(size_t) n], false))
                        noteOff (n);
        }
        else if (d1 == 120) allNotesOff (true);         // all sound off
        else if (d1 == 123) allNotesOff (false);        // all notes off
    }
}

void BuiltInSynth::noteOn (int note, float velocity) noexcept
{
    heldBySustain[(size_t) note] = false;

    // A free voice, or steal the oldest one.
    Voice* chosen = nullptr;
    for (auto& v : voices)
        if (! v.active) { chosen = &v; break; }

    if (chosen == nullptr)
    {
        chosen = &voices[0];
        for (auto& v : voices)
            if (v.startedAt < chosen->startedAt)
                chosen = &v;
    }

    auto& v = *chosen;
    v.active = true;
    v.note = note;
    v.velocity = velocity;
    v.phase = 0.0;
    v.phaseInc = juce::MidiMessage::getMidiNoteInHertz (note) / sampleRate;
    v.ic1 = v.ic2 = 0.0f;
    v.startedAt = ++noteCounter;
    v.envelope.reset();
    v.envelope.noteOn();
}

void BuiltInSynth::noteOff (int note) noexcept
{
    if (sustainPedal)
    {
        heldBySustain[(size_t) note] = true;
        return;
    }

    for (auto& v : voices)
        if (v.active && v.note == note)
            v.envelope.noteOff();
}

void BuiltInSynth::allNotesOff (bool immediately) noexcept
{
    sustainPedal = false;
    heldBySustain.fill (false);

    for (auto& v : voices)
    {
        if (immediately)
        {
            v.active = false;
            v.envelope.reset();
        }
        else if (v.active)
        {
            v.envelope.noteOff();
        }
    }
}

void BuiltInSynth::renderVoices (float* left, float* right, int start, int end) noexcept
{
    if (end <= start)
        return;

    const auto f = makeFilter (cutoff.load(), resonance.load(), sampleRate);
    const auto shape = wave.load();
    const auto level = gain.load() * 0.3f; // headroom for chords

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = start; i < end; ++i)
        {
            // Oscillator
            const auto t = v.phase, dt = v.phaseInc;
            double osc = 0.0;

            switch (shape)
            {
                case 0:  osc = 2.0 * t - 1.0 - polyBlep (t, dt); break;
                case 1:  osc = (t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (std::fmod (t + 0.5, 1.0), dt); break;
                case 2:  osc = 1.0 - 4.0 * std::abs (t - 0.5); break;
                default: osc = std::sin (juce::MathConstants<double>::twoPi * t); break;
            }

            v.phase += dt;
            if (v.phase >= 1.0)
                v.phase -= 1.0;

            // Filter
            const auto x = (float) osc;
            const auto v3 = x - v.ic2;
            const auto v1 = f.a1 * v.ic1 + f.a2 * v3;
            const auto v2 = v.ic2 + f.a2 * v.ic1 + f.a3 * v3;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;

            const auto s = v2 * v.velocity * level * v.envelope.getNextSample();
            left[i]  += s;
            right[i] += s;
        }

        if (! v.envelope.isActive())
            v.active = false;
    }
}

} // namespace riff
