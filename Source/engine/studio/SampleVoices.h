#pragma once

#include "StudioDsp.h"
#include "../nemix/SampleLibrary.h"
#include <array>

/**
    Polyphonic playback of the recorded instruments (the Nemix Library's): a voice per note, each reading its
    recording at the note's pitch with an envelope, a tone filter and pan, into one of the instrument's parts.
*/
namespace riff::studio
{
    struct SampleVoice
    {
        const nemix::samples::Instrument* instrument = nullptr;
        const nemix::samples::Region* region = nullptr;
        double pos = 0.0, step = 1.0;
        float gainL = 1.0f, gainR = 1.0f;
        int note = -1, part = 0, group = 0;
        bool held = false, sustained = false, oneShot = false, releasing = false;
        float release = 0.2f, decay = 0.0f;   // seconds (decay 0: as recorded)
        float env = 1.0f, envStep = 0.0f, decayMul = 1.0f;
        float choke = 1.0f;
        bool choking = false;
        juce::uint64 age = 0;
        dsp::Svf tone;
        bool filtered = false;
        double glideTargetStep = 1.0;
        float glideRate = 0.0f;   // (a slide: step moves towards glideTargetStep)
        bool active() const noexcept { return region != nullptr; }
    };

    template <int maxVoices>
    class SampleVoices
    {
    public:
        struct Start
        {
            int note = 60, velocity = 100, part = 0;
            int playAs = -1;               // the key whose recording to use (-1: the note itself)
            float semitones = 0.0f;        // tuning on top
            float gainDb = 0.0f, pan = 0.0f;
            float release = 0.25f, decay = 0.0f;
            float toneHz = 0.0f, toneQ = 0.7f;   // a low pass (0: none)
            bool oneShot = false;
            int chokeGroup = 0;            // voices of this group stop (the open hi-hat by the closed)
            int group = 0;
            float startOffset = 0.0f;      // seconds into the recording
        };

        void prepare (double sampleRate) { rate = sampleRate; for (auto& v : voices) v = {}; }

        /** Starts a voice; returns it (or nullptr when the instrument isn't loaded or has nothing for that key). */
        SampleVoice* start (const nemix::samples::Instrument* ins, const Start& s) noexcept
        {
            if (ins == nullptr || ! ins->ready.load (std::memory_order_acquire))
                return nullptr;
            const auto key = s.playAs >= 0 ? s.playAs : s.note;
            const auto* region = ins->pick (key, juce::jlimit (1, 127, s.velocity), turns[(size_t) (key & 127)]++);
            if (region == nullptr || region->recording >= (int) ins->recordings.size())
            {
                // (an instrument's range ends: the nearest key that has one, pitched)
                for (int d = 1; d < 24 && region == nullptr; ++d)
                    for (const auto k : { key - d, key + d })
                        if (region == nullptr && k >= 0 && k < 128)
                            if (const auto* r = ins->pick (k, juce::jlimit (1, 127, s.velocity), 0); r != nullptr)
                                region = r;
                if (region == nullptr)
                    return nullptr;
            }
            if (s.chokeGroup > 0)
                for (auto& v : voices)
                    if (v.active() && v.group == s.chokeGroup)
                        v.choking = true;

            auto* v = freeVoice();
            const auto& rec = ins->recordings[(size_t) region->recording];
            v->instrument = ins;
            v->region = region;
            const auto semis = (float) (s.note - region->root) + region->tuneCents * 0.01f + s.semitones;
            const auto recRate = 44100.0;   // (the library's recordings are all at 44.1 kHz)
            v->step = std::pow (2.0, semis / 12.0) * recRate / rate;
            v->pos = juce::jlimit (0.0, (double) juce::jmax (0, rec.length - 2), (double) s.startOffset * recRate);
            const auto g = dsp::dbToGain (region->gainDb + s.gainDb);
            const auto pan = juce::jlimit (-1.0f, 1.0f, s.pan + region->pan);
            v->gainL = g * (pan <= 0.0f ? 1.0f : 1.0f - pan);
            v->gainR = g * (pan >= 0.0f ? 1.0f : 1.0f + pan);
            v->note = s.note;
            v->part = s.part;
            v->group = s.group;
            v->held = true;
            v->sustained = false;
            v->oneShot = s.oneShot || ins->oneShot;
            v->releasing = false;
            v->release = juce::jmax (0.01f, s.release);
            v->decay = s.decay;
            v->env = 1.0f;
            v->decayMul = s.decay > 0.0f ? std::exp (-1.0f / (s.decay * 0.25f * (float) rate)) : 1.0f;
            v->choke = 1.0f;
            v->choking = false;
            v->age = ++clock;
            v->filtered = s.toneHz > 0.0f && s.toneHz < 19000.0f;
            v->tone.reset();
            if (v->filtered)
                v->tone.set (s.toneHz, s.toneQ, rate);
            v->glideTargetStep = v->step;
            v->glideRate = 0.0f;
            return v;
        }

        /** Note off: voices of that note release (unless the pedal holds them, or they're one-shots). */
        void noteOff (int note, bool pedalDown) noexcept
        {
            for (auto& v : voices)
                if (v.active() && v.note == note && v.held)
                {
                    v.held = false;
                    if (pedalDown) v.sustained = true;
                    else if (! v.oneShot) v.releasing = true;
                }
        }
        /** The sustain pedal lifted: the notes it held release. */
        void pedalUp() noexcept
        {
            for (auto& v : voices)
                if (v.active() && v.sustained && ! v.held)
                {
                    v.sustained = false;
                    if (! v.oneShot) v.releasing = true;
                }
        }
        void allOff() noexcept { for (auto& v : voices) if (v.active()) v.releasing = true; }
        void kill() noexcept { for (auto& v : voices) v = {}; }

        /** Renders every voice into its part's buffers. */
        void render (float* const* partL, float* const* partR, int numParts, int n, std::array<std::atomic<float>, 128>* levels = nullptr) noexcept
        {
            for (auto& v : voices)
            {
                if (! v.active())
                    continue;
                const auto& rec = v.instrument->recordings[(size_t) v.region->recording];
                const auto part = juce::jlimit (0, numParts - 1, v.part);
                auto* l = partL[part];
                auto* r = partR[part];
                const auto relMul = std::exp (-1.0f / (v.release * 0.25f * (float) rate));
                const auto chokeMul = std::exp (-1.0f / (0.012f * 0.25f * (float) rate));
                float peak = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const auto i0 = (int) v.pos;
                    if (i0 + 1 >= rec.length)
                    {
                        v.region = nullptr;
                        break;
                    }
                    const auto f = (float) (v.pos - (double) i0);
                    auto sl = rec.at (0, i0) + (rec.at (0, i0 + 1) - rec.at (0, i0)) * f;
                    auto sr = rec.channels > 1 ? rec.at (1, i0) + (rec.at (1, i0 + 1) - rec.at (1, i0)) * f : sl;
                    if (v.filtered)
                    {
                        // (one filter on the sum's difference kept: mono tone, stereo image kept)
                        const auto mid = (sl + sr) * 0.5f, side = (sl - sr) * 0.5f;
                        const auto m2 = v.tone.low (mid);
                        sl = m2 + side * 0.7f;
                        sr = m2 - side * 0.7f;
                    }
                    if (v.releasing) v.env *= relMul;
                    else if (v.decay > 0.0f) v.env *= v.decayMul;
                    if (v.choking) v.choke *= chokeMul;
                    const auto a = v.env * v.choke;
                    l[i] += sl * a * v.gainL;
                    r[i] += sr * a * v.gainR;
                    peak = juce::jmax (peak, a);
                    v.pos += v.step;
                    if (v.glideRate != 0.0f)
                    {
                        v.step *= 1.0 + (double) v.glideRate;
                        if ((v.glideRate > 0.0f && v.step >= v.glideTargetStep) || (v.glideRate < 0.0f && v.step <= v.glideTargetStep))
                        {
                            v.step = v.glideTargetStep;
                            v.glideRate = 0.0f;
                        }
                    }
                    if (a < 1.0e-4f && (v.releasing || v.choking || v.decay > 0.0f))
                    {
                        v.region = nullptr;
                        break;
                    }
                }
                if (levels != nullptr && v.note >= 0 && v.note < 128)
                    (*levels)[(size_t) v.note].store (juce::jmax ((*levels)[(size_t) v.note].load (std::memory_order_relaxed), peak), std::memory_order_relaxed);
            }
        }

        /** A slide (bass, sampler glide): the voices of `fromNote` move to `toNote` over `seconds`. */
        bool slide (int fromNote, int toNote, float seconds) noexcept
        {
            for (auto& v : voices)
                if (v.active() && v.note == fromNote && ! v.releasing)
                {
                    const auto ratio = std::pow (2.0, (double) (toNote - fromNote) / 12.0);
                    v.glideTargetStep = v.step * ratio;
                    const auto steps = juce::jmax (1.0, (double) seconds * rate);
                    v.glideRate = (float) (std::pow (ratio, 1.0 / steps) - 1.0);
                    v.note = toNote;
                    v.held = true;
                    return true;
                }
            return false;
        }

        std::array<SampleVoice, maxVoices>& all() noexcept { return voices; }

    private:
        std::array<SampleVoice, maxVoices> voices;
        std::array<int, 128> turns {};
        juce::uint64 clock = 0;
        double rate = 48000.0;

        SampleVoice* freeVoice() noexcept
        {
            SampleVoice* chosen = nullptr;
            for (auto& v : voices)
                if (! v.active()) { chosen = &v; break; }
            if (chosen == nullptr)
            {
                // the oldest releasing one, else the oldest
                juce::uint64 oldest = std::numeric_limits<juce::uint64>::max();
                for (int pass = 0; pass < 2 && chosen == nullptr; ++pass)
                    for (auto& v : voices)
                        if ((pass == 1 || v.releasing) && v.age < oldest) { oldest = v.age; chosen = &v; }
            }
            *chosen = SampleVoice();
            return chosen;
        }
    };
}
