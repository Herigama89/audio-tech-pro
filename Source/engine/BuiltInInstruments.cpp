#include "BuiltInInstruments.h"

namespace riff
{

//==============================================================================
// Drum Kit
//==============================================================================
DrumKit::Piece DrumKit::pieceFor (int note)
{
    switch (note)
    {
        case 35: case 36: return Piece::kick;
        case 38: case 40: return Piece::snare;
        case 37:          return Piece::sideStick;
        case 39:          return Piece::clap;
        case 42:          return Piece::closedHat;
        case 44:          return Piece::pedalHat;
        case 46:          return Piece::openHat;
        case 41: case 43: case 45: case 47: case 48: case 50: return Piece::tom;
        case 49: case 52: case 55: case 57: return Piece::crash;
        case 51: case 59: return Piece::ride;
        case 53:          return Piece::bell;
        case 56:          return Piece::cowbell;
        default:          return note < 35 ? Piece::kick : note < 60 ? Piece::tom : Piece::none;
    }
}

juce::String DrumKit::pieceName (int note)
{
    switch (pieceFor (note))
    {
        case Piece::kick: return "Kick";
        case Piece::snare: return "Snare";
        case Piece::sideStick: return "Side Stick";
        case Piece::clap: return "Clap";
        case Piece::closedHat: return "Closed Hi-Hat";
        case Piece::pedalHat: return "Pedal Hi-Hat";
        case Piece::openHat: return "Open Hi-Hat";
        case Piece::tom: return "Tom";
        case Piece::crash: return "Crash";
        case Piece::ride: return "Ride";
        case Piece::bell: return "Ride Bell";
        case Piece::cowbell: return "Cowbell";
        case Piece::none: break;
    }
    return {};
}

void DrumKit::prepare (double rate, int)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    preparedRate = sampleRate;
    for (auto& v : voices)
        v.active = false;
}

float DrumKit::noise() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return (float) (seed >> 8) / (float) (1u << 23) - 1.0f;
}

void DrumKit::trigger (int note, float velocity) noexcept
{
    const auto piece = pieceFor (note);
    if (piece == Piece::none)
        return;

    // The closed and pedal hats choke the open one.
    if (piece == Piece::closedHat || piece == Piece::pedalHat)
        for (auto& v : voices)
            if (v.active && v.piece == Piece::openHat)
                v.active = false;

    // A free voice, or the oldest.
    Voice* voice = &voices[0];
    for (auto& v : voices)
    {
        if (! v.active) { voice = &v; break; }
        if (v.time > voice->time) voice = &v;
    }

    const auto tuneRatio = std::pow (2.0, tune.load() / 12.0);
    Voice v;
    v.active = true;
    v.piece = piece;
    v.note = note;
    v.velocity = velocity;
    // Toms spread from low (41) to high (50); everything a little to its side, as on a kit.
    switch (piece)
    {
        case Piece::kick:      v.baseFreq = 52.0; v.pan = 0.0f; break;
        case Piece::snare:     v.baseFreq = 185.0; v.pan = 0.05f; break;
        case Piece::sideStick: v.baseFreq = 900.0; v.pan = 0.05f; break;
        case Piece::clap:      v.baseFreq = 1200.0; v.pan = -0.05f; break;
        case Piece::tom:       v.baseFreq = 70.0 + (note - 41) * 14.0; v.pan = (float) juce::jlimit (-0.5, 0.5, (note - 45) * 0.1); break;
        case Piece::cowbell:   v.baseFreq = 540.0; v.pan = 0.2f; break;
        case Piece::closedHat: case Piece::pedalHat: case Piece::openHat: v.baseFreq = 1.0; v.pan = 0.3f; break;
        case Piece::crash:     v.baseFreq = 1.0; v.pan = -0.35f; break;
        case Piece::ride:      case Piece::bell: v.baseFreq = 1.0; v.pan = 0.35f; break;
        case Piece::none: break;
    }
    v.baseFreq *= tuneRatio;
    // Metallic partials for cymbals and hats (inharmonic, as a real cymbal's).
    const double ratios[6] { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
    for (size_t i = 0; i < 6; ++i)
        v.metal[i] = 0.0 * ratios[i];
    *voice = v;
}

float DrumKit::next (Voice& v) noexcept
{
    const auto t = v.time;
    const auto d = (double) decay.load();
    const auto dt = 1.0 / sampleRate;
    v.time += dt;
    const auto twoPi = juce::MathConstants<double>::twoPi;
    float out = 0.0f;

    const auto env = [&] (double seconds) { return (float) std::exp (-t / (seconds * d)); };
    const auto metal = [&] (double baseHz) -> float
    {
        // Six square waves at inharmonic ratios, high-passed: the classic metal sound.
        const double ratios[6] { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
        float sum = 0.0f;
        for (size_t i = 0; i < 6; ++i)
        {
            v.metal[i] += baseHz * ratios[i] * dt;
            sum += (v.metal[i] - std::floor (v.metal[i])) < 0.5 ? 1.0f : -1.0f;
        }
        return sum / 6.0f;
    };
    const auto highPass = [&] (float x, float amount)
    {
        v.hp += amount * (x - v.hp);
        return x - v.hp;
    };
    const auto bandPass = [&] (float x, double freq, double q)
    {
        // State-variable filter, band-pass output.
        const auto f = (float) (2.0 * std::sin (juce::MathConstants<double>::pi * juce::jmin (freq, sampleRate * 0.2) / sampleRate));
        const auto low = v.bp2 + f * v.bp1;
        const auto high = x - low - (float) (1.0 / q) * v.bp1;
        v.bp1 = f * high + v.bp1;
        v.bp2 = low;
        return v.bp1;
    };

    switch (v.piece)
    {
        case Piece::kick:
        {
            const auto freq = v.baseFreq + 110.0 * std::exp (-t / 0.035);   // the pitch drops fast
            v.phase += freq * dt;
            out = (float) std::sin (twoPi * v.phase) * env (0.35) + noise() * 0.25f * (float) std::exp (-t / 0.004);
            if (t > 1.5 * d) v.active = false;
            break;
        }
        case Piece::snare:
        {
            v.phase += (v.baseFreq + 60.0 * std::exp (-t / 0.02)) * dt;
            v.phase2 += v.baseFreq * 1.6 * dt;
            const auto tone = (float) (std::sin (twoPi * v.phase) * 0.6 + std::sin (twoPi * v.phase2) * 0.3) * env (0.08);
            const auto rattle = highPass (noise(), 0.25f) * env (0.17);
            out = tone * 0.7f + rattle * 0.8f;
            if (t > 0.8 * d) v.active = false;
            break;
        }
        case Piece::sideStick:
            out = bandPass (noise(), v.baseFreq * 2.0, 3.0) * 2.5f * env (0.025) + (float) std::sin (twoPi * 400.0 * t) * env (0.02) * 0.4f;
            if (t > 0.2) v.active = false;
            break;
        case Piece::clap:
        {
            // Three quick hands, then the room.
            const auto burst = (t < 0.011 ? 1.0f : 0.0f) + (t >= 0.011 && t < 0.022 ? 0.8f : 0.0f) + (t >= 0.022 && t < 0.034 ? 0.7f : 0.0f);
            const auto tail = t >= 0.034 ? (float) std::exp (-(t - 0.034) / (0.12 * d)) : 0.0f;
            out = bandPass (noise(), v.baseFreq, 1.5) * 3.0f * (burst * (float) std::exp (-std::fmod (t, 0.011) / 0.004) + tail * 0.6f);
            if (t > 0.6 * d) v.active = false;
            break;
        }
        case Piece::closedHat:
        case Piece::pedalHat:
        case Piece::openHat:
        {
            const auto length = v.piece == Piece::closedHat ? 0.045 : v.piece == Piece::pedalHat ? 0.07 : 0.38;
            const auto sound = highPass (metal (400.0 * std::pow (2.0, tune.load() / 12.0)) * 0.6f + noise() * 0.4f, 0.65f);
            out = sound * env (length) * 0.7f;
            if (t > length * 6.0 * d) v.active = false;
            break;
        }
        case Piece::tom:
        {
            v.phase += (v.baseFreq * (1.0 + 0.6 * std::exp (-t / 0.05))) * dt;
            out = (float) std::sin (twoPi * v.phase) * env (0.32) + highPass (noise(), 0.3f) * env (0.01) * 0.2f;
            if (t > 1.6 * d) v.active = false;
            break;
        }
        case Piece::crash:
            out = highPass (metal (320.0) * 0.4f + noise() * 0.6f, 0.5f) * env (1.1) * 0.6f;
            if (t > 6.0 * d) v.active = false;
            break;
        case Piece::ride:
        case Piece::bell:
        {
            const auto ping = v.piece == Piece::bell ? 0.8f : 0.35f;
            out = highPass (metal (480.0) * (0.5f + ping) + noise() * 0.2f, 0.4f) * env (v.piece == Piece::bell ? 0.9 : 0.7) * 0.5f;
            if (t > 5.0 * d) v.active = false;
            break;
        }
        case Piece::cowbell:
        {
            v.phase += v.baseFreq * dt;
            v.phase2 += v.baseFreq * 1.48 * dt;
            const auto sq = [] (double p) { return (p - std::floor (p)) < 0.5 ? 1.0f : -1.0f; };
            out = bandPass ((sq (v.phase) + sq (v.phase2)) * 0.5f, 900.0, 2.0) * 1.5f * env (0.25);
            if (t > 1.2 * d) v.active = false;
            break;
        }
        case Piece::none:
            v.active = false;
            break;
    }
    return out * v.velocity;
}

void DrumKit::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);
    const auto level = gain.load();

    int position = 0;
    const auto renderUntil = [&] (int end)
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto l = (1.0f - juce::jmax (0.0f, v.pan)) * level, r = (1.0f + juce::jmin (0.0f, v.pan)) * level;
            for (int i = position; i < end && v.active; ++i)
            {
                const auto s = next (v);
                left[i] += s * l;
                right[i] += s * r;
            }
        }
        position = end;
    };

    for (const auto meta : midi)
    {
        renderUntil (juce::jlimit (0, numSamples, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            trigger (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices)
                if (m.isAllSoundOff()) v.active = false;
    }
    renderUntil (numSamples);
}

//==============================================================================
// Sampler
//==============================================================================
void Sampler::setSample (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double fileRate)
{
    std::shared_ptr<const juce::AudioBuffer<float>> old;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        old = std::move (sample);
        sample = std::move (buffer);
        sampleRateOfFile = fileRate > 0.0 ? fileRate : 48000.0;
        for (auto& v : voices)
            v.active = false;
    }
}

void Sampler::prepare (double rate, int)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    preparedRate = sampleRate;
    for (auto& v : voices)
    {
        v.active = false;
        v.envelope.setSampleRate (sampleRate);
    }
}

void Sampler::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    const juce::SpinLock::ScopedTryLockType lock (sampleLock);
    if (! lock.isLocked() || sample == nullptr || sample->getNumSamples() < 2)
        return;

    const auto& buffer = *sample;
    const auto length = buffer.getNumSamples();
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
    const auto level = gain.load();

    int position = 0;
    const auto renderUntil = [&] (int end)
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            for (int i = position; i < end; ++i)
            {
                const auto index = (int) v.position;
                if (index + 1 >= length || ! v.envelope.isActive())
                {
                    v.active = false;
                    break;
                }
                const auto frac = (float) (v.position - index);
                const auto e = v.envelope.getNextSample() * v.velocity * level;
                left[i] += (l[index] + (l[index + 1] - l[index]) * frac) * e;
                right[i] += (r[index] + (r[index + 1] - r[index]) * frac) * e;
                v.position += v.step;
            }
        }
        position = end;
    };

    for (const auto meta : midi)
    {
        renderUntil (juce::jlimit (0, numSamples, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            Voice* voice = &voices[0];
            for (auto& v : voices)
                if (! v.active) { voice = &v; break; }
            voice->active = true;
            voice->note = m.getNoteNumber();
            voice->velocity = m.getFloatVelocity();
            voice->position = 0.0;
            voice->step = std::pow (2.0, (voice->note - root.load()) / 12.0) * sampleRateOfFile / sampleRate;
            voice->envelope.setSampleRate (sampleRate);
            voice->envelope.setParameters ({ attackSeconds.load(), 0.0f, 1.0f, releaseSeconds.load() });
            voice->envelope.noteOn();
        }
        else if (m.isNoteOff())
        {
            for (auto& v : voices)
                if (v.active && v.note == m.getNoteNumber())
                    v.envelope.noteOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
                v.envelope.noteOff();
        }
    }
    renderUntil (numSamples);
}

} // namespace riff
