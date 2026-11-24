#include <catch2/catch_test_macros.hpp>
#include "../Source/engine/BuiltInEffects.h"
#include "../Source/engine/PitchCorrect.h"

/*
    Clicks and crackle in Pitch Correction (the plug-in) and the pitch editor: a sung-like voice (vibrato, drift,
    legato notes, breaths and silences) made with nothing above 5 kHz, so anything the engines leave above 8 kHz
    is a click of their own.
*/
using namespace riff;

namespace
{
    constexpr double rate = 48000.0;

    /** Notes (MIDI, seconds; 0: a breath, -1: silence), sung with vibrato and a little drift and jitter. */
    std::vector<float> sing (const std::vector<std::pair<double, double>>& notes, float amp = 0.25f)
    {
        juce::Random random (7);
        std::vector<float> out;
        double phase = 0.0, current = notes.front().first > 0 ? notes.front().first : 60.0, t = 0.0;
        juce::IIRFilter breathLp1, breathLp2;
        breathLp1.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, 3500.0));
        breathLp2.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, 3500.0));
        float env = 0.0f, breath = 0.0f;
        for (const auto& [note, seconds] : notes)
        {
            const auto count = (int) (seconds * rate);
            for (int i = 0; i < count; ++i, t += 1.0 / rate)
            {
                float s = 0.0f;
                if (note > 0)
                {
                    current += (note - current) * 0.004;   // (a legato glide, ~5 ms)
                    env += (1.0f - env) * 0.002f;
                }
                else
                    env += (0.0f - env) * 0.002f;
                const auto pitch = current + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t)
                                 + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 0.7 * t);
                const auto f0 = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0) * (1.0 + 0.002 * (random.nextFloat() - 0.5f));
                phase += f0 / rate;
                phase -= std::floor (phase);
                if (env > 1.0e-4f)
                    for (int h = 1; f0 * h < 4800.0; ++h)
                    {
                        const auto f = f0 * h;
                        // three formants
                        const auto formants = 1.0 / (1.0 + std::pow ((f - 700.0) / 130.0, 2.0))
                                            + 0.6 / (1.0 + std::pow ((f - 1200.0) / 150.0, 2.0))
                                            + 0.25 / (1.0 + std::pow ((f - 2600.0) / 200.0, 2.0)) + 0.02;
                        s += (float) (formants / h * std::sin (juce::MathConstants<double>::twoPi * phase * h));
                    }
                s *= env;
                // breath: noise below 3.5 kHz, faded in and out
                breath += ((note == 0 ? 1.0f : 0.0f) - breath) * 0.002f;
                s += breathLp2.processSingleSampleRaw (breathLp1.processSingleSampleRaw (0.05f * breath * (random.nextFloat() * 2.0f - 1.0f)));
                out.push_back (s * amp);
            }
        }
        return out;
    }

    struct Clicks
    {
        double worstDb = -200.0;   // the loudest 2 ms above 8 kHz, against the voice's level
        int count = 0;             // 2 ms windows above -45 dB
        juce::String where;        // (their times, in seconds)
    };

    void dump (const std::vector<float>& s, const juce::String& name)
    {
        const auto folder = juce::SystemStats::getEnvironmentVariable ("RIFF_PITCH_DUMP", {});
        if (folder.isEmpty())
            return;
        juce::AudioBuffer<float> b (1, (int) s.size());
        std::copy (s.begin(), s.end(), b.getWritePointer (0));
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (juce::File (folder).getChildFile (name + ".wav"));
        juce::WavAudioFormat wav;
        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (32)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    Clicks clicksIn (const std::vector<float>& s, size_t from = 4800)
    {
        std::vector<float> hp (s.begin() + (long) from, s.end());
        double total = 0.0;
        for (auto v : hp)
            total += (double) v * v;
        const auto level = std::sqrt (total / (double) juce::jmax ((size_t) 1, hp.size()));
        for (int stage = 0; stage < 4; ++stage)
        {
            juce::IIRFilter f;
            f.setCoefficients (juce::IIRCoefficients::makeHighPass (rate, 8000.0));
            f.processSamples (hp.data(), (int) hp.size());
        }
        Clicks c;
        constexpr size_t window = 96;
        for (size_t at = 0; at + window <= hp.size(); at += window)
        {
            double e = 0.0;
            for (size_t i = 0; i < window; ++i)
                e += (double) hp[at + i] * hp[at + i];
            const auto db = juce::Decibels::gainToDecibels (std::sqrt (e / window) / juce::jmax (1.0e-9, level), -200.0);
            c.worstDb = std::max (c.worstDb, db);
            if (db > -45.0)
            {
                ++c.count;
                c.where << juce::String ((double) (at + from) / rate, 3) << "s (" << juce::String (db, 1) << ") ";
            }
        }
        return c;
    }

    void setParam (BuiltInEffect& e, const juce::String& id, float value)
    {
        const auto& params = e.getInfo().params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id)
                e.setParameter ((int) i, value);
    }

    std::vector<float> throughCorrection (const std::vector<float>& in, int block, bool live, float response = 20.0f)
    {
        auto e = BuiltInEffect::create ("pitchCorrection");
        e->prepare (rate, block);
        setParam (*e, "scale", 0.0f);
        setParam (*e, "response", response);
        setParam (*e, "live", live ? 1.0f : 0.0f);
        auto l = in, r = in;
        // (block sizes as a host gives them: not always the same)
        juce::Random random (3);
        for (size_t at = 0; at < l.size();)
        {
            const auto n = (int) std::min (l.size() - at, (size_t) juce::jmax (1, block - random.nextInt (block / 2 + 1)));
            e->process (l.data() + at, r.data() + at, n);
            at += (size_t) n;
        }
        return l;
    }

    const std::vector<std::pair<double, double>>& melody()
    {
        // off-key notes (quarter tones) with breaths, silences and leaps
        static const std::vector<std::pair<double, double>> m {
            { -1, 0.2 }, { 57.4, 0.6 }, { 59.3, 0.4 }, { 61.6, 0.5 }, { 0, 0.25 }, { 64.45, 0.7 }, { 62.3, 0.3 },
            { -1, 0.3 }, { 52.2, 0.6 }, { 69.4, 0.5 }, { 66.6, 0.4 }, { 0, 0.2 }, { 60.25, 0.8 }, { -1, 0.3 } };
        return m;
    }
}

TEST_CASE ("Pitch Correction and the pitch editor: no clicks or crackle in a sung voice", "[pitchquality]")
{
    const auto voice = sing (melody());
    const auto dry = clicksIn (voice);
    dump (voice, "dry");

    for (const bool live : { false, true })
        for (const int block : { 32, 128, 512, 1024 })
        {
            const auto out = throughCorrection (voice, block, live);
            const auto c = clicksIn (out, 4800 + (size_t) (live ? 0 : 2880));
            dump (out, juce::String (live ? "live" : "studio") + juce::String (block));
            INFO ((live ? "live" : "studio") << " block " << block << ": worst " << c.worstDb << " dB, " << c.count);
            CHECK (c.count == 0);
        }

    // The editor: every note snapped and moved
    juce::AudioBuffer<float> audio (1, (int) voice.size());
    std::copy (voice.begin(), voice.end(), audio.getWritePointer (0));
    const auto analysis = pitchcorrect::analyse (audio, rate);
    for (const double shift : { 0.0, 2.0, -3.0 })
    {
        std::vector<pitchcorrect::NoteEdit> edits (analysis.notes.size());
        for (auto& e : edits)
        {
            e.snap = 1.0;
            e.shift = shift;
        }
        const auto rendered = pitchcorrect::render (audio, analysis, edits);
        std::vector<float> out (rendered.getReadPointer (0), rendered.getReadPointer (0) + rendered.getNumSamples());
        const auto c = clicksIn (out);
        dump (out, "editor" + juce::String (shift));
        INFO ("editor shift " << shift << ": worst " << c.worstDb << " dB, " << c.count);
        CHECK (c.count == 0);
    }
}
