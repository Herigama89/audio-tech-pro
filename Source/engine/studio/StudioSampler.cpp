#include "StudioInstruments.h"

namespace riff::studio
{

namespace
{
    std::vector<Param> samplerParams()
    {
        std::vector<Param> p;
        p.push_back ({ "root", "Root Key", 0, 127, 60 });
        p.push_back ({ "start", "Start", 0.0f, 1.0f, 0.0f });
        p.push_back ({ "end", "End", 0.0f, 1.0f, 1.0f });
        p.push_back ({ "loopStart", "Loop Start", 0.0f, 1.0f, 0.25f });
        p.push_back ({ "loopEnd", "Loop End", 0.0f, 1.0f, 0.75f });
        p.push_back ({ "loopMode", "Playback", 0, 3, 0, { "One shot", "Loop: continuous", "Loop: back and forth", "Loop until release" } });
        p.push_back ({ "xfade", "Loop Crossfade", 0.0f, 0.1f, 0.012f });
        p.push_back ({ "reverse", "Reverse", 0, 1, 0, { "Off", "On" } });
        p.push_back ({ "warp", "AudioWarp", 0, 2, 0, { "Off", "Keep time", "Keep time, synced" } });
        p.push_back ({ "warpBpm", "Sample Tempo", 40.0f, 240.0f, 120.0f });
        p.push_back ({ "pitch", "Pitch", -24.0f, 24.0f, 0.0f });
        p.push_back ({ "fine", "Fine", -100.0f, 100.0f, 0.0f });
        p.push_back ({ "keyFollow", "Key Follow", 0.0f, 1.0f, 1.0f });
        p.push_back ({ "bendRange", "Bend Range", 0, 24, 2 });
        p.push_back ({ "filterType", "Filter", 0, 3, 0, { "Low pass 24", "Low pass 12", "Band pass", "High pass" } });
        p.push_back ({ "cutoff", "Cutoff", 0.0f, 1.0f, 1.0f });
        p.push_back ({ "reso", "Reso", 0.0f, 1.0f, 0.1f });
        p.push_back ({ "drive", "Drive", 0.0f, 1.0f, 0.0f });
        p.push_back ({ "fEnv", "Filter Envelope", -1.0f, 1.0f, 0.0f });
        p.push_back ({ "fA", "Filter Attack", 0.0f, 4.0f, 0.005f });
        p.push_back ({ "fD", "Filter Decay", 0.0f, 4.0f, 0.4f });
        p.push_back ({ "fS", "Filter Sustain", 0.0f, 1.0f, 0.3f });
        p.push_back ({ "fR", "Filter Release", 0.0f, 4.0f, 0.3f });
        p.push_back ({ "attack", "Attack", 0.0f, 4.0f, 0.002f });
        p.push_back ({ "decay", "Decay", 0.0f, 4.0f, 0.5f });
        p.push_back ({ "sustain", "Sustain", 0.0f, 1.0f, 1.0f });
        p.push_back ({ "release", "Release", 0.0f, 6.0f, 0.15f });
        p.push_back ({ "level", "Level", -60.0f, 12.0f, 0.0f });
        p.push_back ({ "pEnv", "Pitch Envelope", -24.0f, 24.0f, 0.0f });
        p.push_back ({ "pDecay", "Pitch Decay", 0.0f, 2.0f, 0.2f });
        p.push_back ({ "lfoRate", "LFO Rate", 0.05f, 20.0f, 4.0f });
        p.push_back ({ "lfoShape", "LFO Shape", 0, 3, 0, { "Sine", "Triangle", "Saw", "Square" } });
        p.push_back ({ "lfoAmt", "LFO Amount", 0.0f, 1.0f, 0.0f });
        p.push_back ({ "lfoDest", "LFO To", 0, 2, 0, { "Pitch", "Cutoff", "Level" } });
        p.push_back ({ "glide", "Glide", 0.0f, 1.0f, 0.0f });
        p.push_back ({ "voiceMode", "Voices", 0, 2, 0, { "Poly", "Mono", "Legato" } });
        p.push_back ({ "polyphony", "Polyphony", 1, 16, 16 });
        p.push_back ({ "velAmp", "Velocity to Level", 0.0f, 1.0f, 0.7f });
        p.push_back ({ "velCutoff", "Velocity to Cutoff", 0.0f, 1.0f, 0.0f });
        p.push_back ({ "keyLow", "Lowest Key", 0, 127, 0 });
        p.push_back ({ "keyHigh", "Highest Key", 0, 127, 127 });
        return p;
    }
}

StudioSampler::StudioSampler() : StudioInstrument ("sampler", samplerParams(), { "Sampler" }) {}

void StudioSampler::setSampleFile (int, const juce::File& f)
{
    file = f;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f)); reader != nullptr)
    {
        const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
        buffer = std::make_shared<juce::AudioBuffer<float>> (2, juce::jmax (4, length));
        buffer->clear();
        reader->read (buffer.get(), 0, length, 0, true, true);
        sampleRate = reader->sampleRate;
    }
    std::atomic_store (&sample, buffer);
}

void StudioSampler::prepareVoices (double, int)
{
    for (auto& v : voices) v = Voice();
}

float StudioSampler::readAt (const juce::AudioBuffer<float>& s, int channel, double pos) const noexcept
{
    const auto len = s.getNumSamples();
    const auto i0 = juce::jlimit (0, len - 2, (int) pos);
    const auto f = (float) (pos - i0);
    const auto* d = s.getReadPointer (juce::jmin (channel, s.getNumChannels() - 1));
    return d[i0] + (d[i0 + 1] - d[i0]) * f;
}

juce::Array<float> StudioSampler::findSlices (float sensitivity) const
{
    juce::Array<float> slices;
    auto s = getSample();
    if (s == nullptr) return slices;
    // hits: where the energy jumps over what came just before (in 10 ms windows)
    const auto window = juce::jmax (64, (int) (sampleRate * 0.01));
    const auto n = s->getNumSamples();
    float previous = 0.0f;
    int lastHit = -window * 8;
    for (int start = 0; start + window < n; start += window)
    {
        float e = 0.0f;
        for (int c = 0; c < s->getNumChannels(); ++c)
            e += s->getRMSLevel (c, start, window);
        const auto threshold = 1.4f + (1.0f - sensitivity) * 3.0f;
        if (e > 0.01f && e > previous * threshold && start - lastHit > window * 6)
        {
            slices.add ((float) start / (float) n);
            lastHit = start;
        }
        previous = previous * 0.6f + e * 0.4f;
    }
    if (slices.isEmpty() || slices[0] > 0.01f) slices.insert (0, 0.0f);
    return slices;
}

void StudioSampler::noteOn (int note, float velocity) noexcept
{
    auto s = getSample();
    if (s == nullptr || note < choice (indexOf ("keyLow")) || note > choice (indexOf ("keyHigh")))
        return;
    const auto mode = choice (indexOf ("voiceMode"));
    Voice* chosen = nullptr;
    if (mode > 0)
    {
        chosen = &voices[0];
        if (std::find (monoStack.begin(), monoStack.end(), note) == monoStack.end()) monoStack.push_back (note);
        // legato: the voice keeps going, only its pitch moves (the glide)
        if (mode == 2 && chosen->active() && chosen->held)
        {
            chosen->note = note;
            return;
        }
    }
    else
    {
        const auto poly = juce::jlimit (1, 16, choice (indexOf ("polyphony")));
        for (int i = 0; i < poly && chosen == nullptr; ++i)
            if (! voices[(size_t) i].active()) chosen = &voices[(size_t) i];
        if (chosen == nullptr)
        {
            juce::uint64 oldest = std::numeric_limits<juce::uint64>::max();
            for (int i = 0; i < poly; ++i)
                if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; chosen = &voices[(size_t) i]; }
        }
    }
    auto& v = *chosen;
    const auto previousNote = v.note;
    v.note = note;
    v.held = true;
    v.velocity = velocity;
    v.age = ++clock;
    const auto len = (double) s->getNumSamples();
    const bool rev = choice (indexOf ("reverse")) == 1;
    v.dir = rev ? -1 : 1;
    v.pos = rev ? p (indexOf ("end")) * (len - 2) : p (indexOf ("start")) * (len - 2);
    v.grain = v.pos;
    v.grainPhase = 0.0;
    v.amp.set (p (indexOf ("attack")), p (indexOf ("decay")), p (indexOf ("sustain")), p (indexOf ("release")), rate);
    v.filt.set (p (indexOf ("fA")), p (indexOf ("fD")), p (indexOf ("fS")), p (indexOf ("fR")), rate);
    v.pitch.set (0.0005f, juce::jmax (0.002f, p (indexOf ("pDecay"))), 0.0f, 0.01f, rate);
    v.amp.start();
    v.filt.start();
    v.pitch.start();
    for (auto& f : v.f) f.reset();
    if (p (indexOf ("glide")) <= 0.0f || previousNote < 0)
        v.step = 0.0;   // (computed fresh on the first sample)
    lastNote.store (note);
    lastVelocity.store (juce::roundToInt (velocity * 127.0f));
}

void StudioSampler::noteOff (int note) noexcept
{
    const auto mode = choice (indexOf ("voiceMode"));
    if (mode > 0)
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        auto& v = voices[0];
        if (! monoStack.empty() && v.note == note)
        {
            v.note = monoStack.back();   // back to the key still held
            return;
        }
    }
    for (auto& v : voices)
        if (v.note == note && v.held)
        {
            v.held = false;
            v.amp.stop();
            v.filt.stop();
        }
}

void StudioSampler::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midi) noexcept
{
    auto s = getSample();
    int done = 0;
    const auto render = [&] (int until)
    {
        if (s == nullptr || until <= done) { done = juce::jmax (done, until); return; }
        const auto len = (double) s->getNumSamples() - 2.0;
        const auto start = p (indexOf ("start")) * len, end = juce::jmax (start + 4.0, (double) p (indexOf ("end")) * len);
        const auto loopA = juce::jlimit (start, end - 4.0, (double) p (indexOf ("loopStart")) * len);
        const auto loopB = juce::jlimit (loopA + 4.0, end, (double) p (indexOf ("loopEnd")) * len);
        const auto loopMode = choice (indexOf ("loopMode"));
        const auto warp = choice (indexOf ("warp"));
        const auto root = (float) choice (indexOf ("root"));
        const auto follow = p (indexOf ("keyFollow"));
        const auto tune = p (indexOf ("pitch")) + p (indexOf ("fine")) * 0.01f;
        const auto glide = p (indexOf ("glide"));
        const auto lfoAmt = p (indexOf ("lfoAmt"));
        const auto lfoDest = choice (indexOf ("lfoDest"));
        const auto lfoShape = choice (indexOf ("lfoShape"));
        const auto lfoInc = p (indexOf ("lfoRate")) / rate;
        const auto filterType = choice (indexOf ("filterType"));
        const auto cutoffKnob = p (indexOf ("cutoff"));
        const auto reso = p (indexOf ("reso"));
        const auto drive = p (indexOf ("drive"));
        const auto fEnv = p (indexOf ("fEnv"));
        const auto pEnv = p (indexOf ("pEnv"));
        const auto velAmp = p (indexOf ("velAmp")), velCut = p (indexOf ("velCutoff"));
        const auto level = dsp::dbToGain (p (indexOf ("level")));
        const auto baseRatio = sampleRate / rate;
        const auto xfade = juce::jmax (1.0, (double) p (indexOf ("xfade")) * sampleRate);
        // AudioWarp: the recording plays at its own speed (or the song's, synced), the pitch set by the grains
        const auto warpSpeed = warp == 2 ? songBpm / juce::jmax (1.0f, p (indexOf ("warpBpm"))) : 1.0;
        const auto grainLen = 0.04 * sampleRate;
        for (int i = done; i < until; ++i)
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const auto ph = (float) lfoPhase;
            const auto lfo = lfoShape == 0 ? std::sin (ph * dsp::twoPi) : lfoShape == 1 ? 1.0f - 4.0f * std::abs (ph - 0.5f) : lfoShape == 2 ? ph * 2.0f - 1.0f : (ph < 0.5f ? 1.0f : -1.0f);
            float outL = 0.0f, outR = 0.0f;
            for (size_t k = 0; k < voices.size(); ++k)
            {
                auto& v = voices[k];
                if (! v.active())
                {
                    playheads[k & 7].store (-1.0f, std::memory_order_relaxed);
                    continue;
                }
                const auto ampEnv = v.amp.next();
                const auto filtEnv = v.filt.next();
                const auto pitchEnv = v.pitch.next();
                const auto semis = ((float) v.note - root) * follow + tune + pEnv * pitchEnv + bend
                                   + (lfoDest == 0 ? lfo * lfoAmt * 2.0f : 0.0f);
                const auto target = std::pow (2.0, semis / 12.0) * baseRatio;
                if (v.step <= 0.0) v.step = target;
                v.step += (target - v.step) * (glide > 0.0f ? (double) (1.0f / (1.0f + glide * 0.3f * (float) rate)) : 1.0);

                float sl, sr;
                if (warp > 0)
                {
                    // two overlapping grains read at the pitch's speed from where the recording has got to
                    const auto speed = baseRatio * warpSpeed;
                    v.grain += speed * v.dir;
                    v.grainPhase += v.step / grainLen;
                    if (v.grainPhase >= 1.0) v.grainPhase -= 1.0;
                    float gl = 0.0f, gr = 0.0f;
                    for (int g = 0; g < 2; ++g)
                    {
                        auto gp = std::fmod (v.grainPhase + g * 0.5, 1.0);
                        const auto w = (float) std::sin (gp * juce::MathConstants<double>::pi);
                        const auto at = juce::jlimit (0.0, len, v.grain + gp * grainLen * v.dir * (v.step / baseRatio - warpSpeed));
                        gl += readAt (*s, 0, at) * w;
                        gr += readAt (*s, 1, at) * w;
                    }
                    sl = gl; sr = gr;
                    v.pos = v.grain;
                }
                else
                {
                    sl = readAt (*s, 0, v.pos);
                    sr = readAt (*s, 1, v.pos);
                    v.pos += v.step * v.dir;
                }
                // the loop (continuous, back and forth; or until release), else the end
                const bool looping = loopMode == 1 || loopMode == 2 || (loopMode == 3 && v.held);
                auto& at = warp > 0 ? v.grain : v.pos;
                if (looping)
                {
                    if (v.dir > 0 && at >= loopB)
                    {
                        if (loopMode == 2) v.dir = -1; else at -= (loopB - loopA);
                    }
                    else if (v.dir < 0 && at <= loopA)
                    {
                        if (loopMode == 2) v.dir = 1; else at += (loopB - loopA);
                    }
                    // the crossfade at the loop's end (continuous): the start mixed in
                    if (loopMode != 2 && v.dir > 0 && at > loopB - xfade && warp == 0)
                    {
                        const auto f = (float) ((at - (loopB - xfade)) / xfade);
                        sl = sl * (1.0f - f) + readAt (*s, 0, at - (loopB - loopA)) * f;
                        sr = sr * (1.0f - f) + readAt (*s, 1, at - (loopB - loopA)) * f;
                    }
                }
                if ((v.dir > 0 && at >= end) || (v.dir < 0 && at <= start))
                {
                    v.amp.kill();
                    continue;
                }
                // the filter (its envelope, velocity, the LFO), the drive
                auto cut = cutoffKnob + fEnv * filtEnv * 0.6f + velCut * (v.velocity - 0.5f) * 0.5f + (lfoDest == 1 ? lfo * lfoAmt * 0.3f : 0.0f);
                cut = juce::jlimit (0.0f, 1.0f, cut);
                if (cut < 0.995f || filterType != 0)
                {
                    const auto hz = 20.0f * std::pow (1000.0f, cut);
                    v.f[0].set (hz, 0.6f + reso * 9.0f, rate);
                    v.f[1].set (hz, 0.6f + reso * 9.0f, rate);
                    const auto run = [&] (dsp::Svf& f, float x)
                    {
                        const auto o = f.run (x);
                        return filterType <= 1 ? o.low : filterType == 2 ? o.band : o.high;
                    };
                    sl = run (v.f[0], sl);
                    sr = run (v.f[1], sr);
                }
                if (drive > 0.0f) { sl = dsp::drive (sl, drive); sr = dsp::drive (sr, drive); }
                const auto vel = 1.0f - velAmp + velAmp * v.velocity;
                const auto amp = ampEnv * vel * level * (lfoDest == 2 ? 1.0f - lfoAmt * 0.5f * (1.0f + lfo) : 1.0f);
                outL += sl * amp;
                outR += sr * amp;
                playheads[k & 7].store ((float) (at / len), std::memory_order_relaxed);
            }
            pl[0][i] += outL;
            pr[0][i] += outR;
        }
        done = until;
    };
    for (const auto m : midi)
    {
        render (juce::jlimit (0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) noteOn (msg.getNoteNumber(), (float) msg.getVelocity() / 127.0f);
        else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel()) bend = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f * (float) choice (indexOf ("bendRange"));
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) { v.amp.stop(); v.held = false; }
    }
    render (n);
}

juce::StringArray StudioSampler::screenLines() const
{
    auto s = getSample();
    const auto seconds = s != nullptr ? (double) s->getNumSamples() / sampleRate : 0.0;
    const auto loopNames = params()[(size_t) indexOf ("loopMode")].choices;
    return { file.existsAsFile() ? file.getFileName() : juce::String ("Drop audio here"),
             loopNames[choice (indexOf ("loopMode"))].toUpperCase() + " · WARP: " + params()[(size_t) indexOf ("warp")].choices[choice (indexOf ("warp"))].toUpperCase()
                 + " · " + juce::String (seconds, 2) + " s",
             juce::String (p (indexOf ("start")) * seconds, 3),
             juce::String (p (indexOf ("loopStart")) * seconds, 2) + "-" + juce::String (p (indexOf ("loopEnd")) * seconds, 2),
             (p (indexOf ("pitch")) >= 0 ? "+" : "") + juce::String (juce::roundToInt (p (indexOf ("pitch")))) + " st",
             params()[(size_t) indexOf ("filterType")].choices[choice (indexOf ("filterType"))],
             "ROOT " + juce::MidiMessage::getMidiNoteName (choice (indexOf ("root")), true, true, 3) + " · " + juce::String (sampleRate / 1000.0, 1) + "k" };
}

//==============================================================================
std::unique_ptr<StudioInstrument> create (const juce::String& kind)
{
    if (kind == "drums")   return std::make_unique<StudioDrums>();
    if (kind == "keys")    return std::make_unique<StudioKeys>();
    if (kind == "bass")    return std::make_unique<StudioBass>();
    if (kind == "guitar")  return std::make_unique<StudioGuitar>();
    if (kind == "beatpad") return std::make_unique<BeatPad>();
    if (kind == "sampler") return std::make_unique<StudioSampler>();
    return nullptr;
}

} // namespace riff::studio
