#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "engine/studio/StudioInstruments.h"

using namespace riff;

namespace
{
    /** Renders `blocks` blocks of 512 with `midi` at the start; the loudest sample (main output). */
    float peakOf (studio::StudioInstrument& inst, const juce::MidiBuffer& midi, int blocks = 40)
    {
        inst.prepare (48000.0, 512);
        std::vector<float> l (512), r (512);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            inst.render (l.data(), r.data(), 512, b == 0 ? midi : juce::MidiBuffer());
            for (int i = 0; i < 512; ++i)
                peak = juce::jmax (peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
        }
        return peak;
    }

    juce::MidiBuffer note (int n, int velocity = 110)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) velocity), 0);
        return m;
    }
}

TEST_CASE ("Studio instruments: every kind is made, with its parts and parameters", "[studio]")
{
    for (const auto& kind : studio::kinds())
    {
        auto inst = studio::create (kind);
        REQUIRE (inst != nullptr);
        CHECK (inst->numParts() >= 1);
        CHECK (inst->getNumExtraOutputs() == inst->numParts());
        CHECK (inst->partParam (0, "gain") >= 0);
        CHECK (inst->indexOf ("mainGain") >= 0);
        // every parameter starts at its default, inside its range
        for (int i = 0; i < (int) inst->params().size(); ++i)
        {
            const auto& p = inst->params()[(size_t) i];
            CHECK (inst->getParam (i) >= p.min);
            CHECK (inst->getParam (i) <= p.max);
        }
    }
}

TEST_CASE ("Beat Pad: its synthesised kits sound, and a pad can go out on its own channel", "[studio]")
{
    studio::BeatPad pad;
    for (int kit = 0; kit < 7; ++kit)   // (the synthesised kits; the acoustic one needs the library)
    {
        pad.setParam (pad.indexOf ("kit"), (float) kit);
        CHECK (peakOf (pad, note (36)) > 0.01f);
    }
    // pad 1 into output group 1 (pads 1), and that group out on its own channel: the main output is silent, its own pair not
    pad.setParam (pad.indexOf ("kit"), 1.0f);
    pad.setParam (pad.indexOf ("pad0Out"), 0.0f);
    pad.setParam (pad.partParam (0, "out"), 1.0f);
    CHECK (peakOf (pad, note (36), 4) < 1.0e-6f);
    pad.prepare (48000.0, 512);
    std::vector<float> l (512), r (512);
    pad.render (l.data(), r.data(), 512, note (36));
    const auto* own = pad.getExtraOutput (1, 0);
    REQUIRE (own != nullptr);
    float peak = 0.0f;
    for (int i = 0; i < 512; ++i) peak = juce::jmax (peak, std::abs (own[i]));
    CHECK (peak > 0.01f);
}

TEST_CASE ("Beat Pad: the pattern plays with the song, and round-trips as text", "[studio]")
{
    studio::BeatPad pad;
    pad.setPatternText ("0:0=100,2:4=90");
    CHECK (pad.getStep (0, 0) == 100);
    CHECK (pad.getStep (2, 4) == 90);
    CHECK (pad.getStep (6, 2) == 0);
    CHECK (pad.patternText() == "0:0=100,2:4=90");
    pad.prepare (48000.0, 512);
    pad.setPosition (0, true, 120.0);
    std::vector<float> l (512), r (512);
    pad.render (l.data(), r.data(), 512, {});
    float peak = 0.0f;
    for (auto v : l) peak = juce::jmax (peak, std::abs (v));
    CHECK (peak > 0.01f);   // (step 1's kick, without a note played)
}

TEST_CASE ("Studio Keys: the polysynth's programs play", "[studio]")
{
    studio::StudioKeys keys;
    const auto& programs = studio::StudioKeys::programs();
    int poly = -1;
    for (int i = 0; i < (int) programs.size(); ++i)
        if (programs[(size_t) i].type == 1) { poly = i; break; }
    REQUIRE (poly >= 0);
    keys.setParam (keys.indexOf ("program"), (float) poly);
    CHECK (peakOf (keys, note (60)) > 0.01f);
    CHECK (studio::StudioKeys::categories().contains ("Piano"));
    CHECK (programs.size() > 60);
}

TEST_CASE ("Sampler Track: a recording plays across the keys, pitched", "[studio]")
{
    // a second of a 220 Hz sine, as a file
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-sampler-test.wav");
    {
        juce::AudioBuffer<float> b (1, 44100);
        for (int i = 0; i < 44100; ++i) b.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 44100.0f));
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, 44100);
    }
    studio::StudioSampler sampler;
    sampler.setSampleFile (0, file);
    REQUIRE (sampler.getSample() != nullptr);
    CHECK (peakOf (sampler, note (60)) > 0.2f);
    CHECK (peakOf (sampler, note (72)) > 0.2f);
    CHECK (! sampler.findSlices (0.5f).isEmpty());
    file.deleteFile();
}

TEST_CASE ("Studio Guitar: a held chord is named and voiced on the strings", "[studio]")
{
    studio::StudioGuitar guitar;
    guitar.prepare (48000.0, 512);
    juce::MidiBuffer chord;
    for (const auto n : { 48, 52, 55 })   // C E G
        chord.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
    std::vector<float> l (512), r (512);
    guitar.render (l.data(), r.data(), 512, chord);
    CHECK (guitar.chordName() == "C");
    int played = 0;
    for (const auto f : guitar.chordFrets()) played += f >= 0 ? 1 : 0;
    CHECK (played >= 4);
}

