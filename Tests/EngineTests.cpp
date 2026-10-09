#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "model/Project.h"
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"
#include "engine/InstrumentRack.h"
#include "engine/BuiltInSynth.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("riff-engine-" + juce::Uuid().toString());
        TempDir()  { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    /** Writes a mono WAV filled with a constant value. */
    juce::File writeConstantWav (const juce::File& file, double sampleRate, int numSamples, float value)
    {
        juce::AudioBuffer<float> buffer (1, numSamples);
        buffer.clear();
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, value);

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate (sampleRate)
                                                    .withNumChannels (1)
                                                    .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        return file;
    }

    /** Renders `numSamples` through the engine in blocks, returning the left channel. */
    std::vector<float> renderLeft (AudioEngine& engine, int numSamples, int blockSize = 512)
    {
        std::vector<float> result;
        juce::AudioBuffer<float> out (2, blockSize);

        for (int done = 0; done < numSamples; done += blockSize)
        {
            const int n = juce::jmin (blockSize, numSamples - done);
            engine.render (nullptr, 0, out.getArrayOfWritePointers(), 2, n);
            result.insert (result.end(), out.getReadPointer (0), out.getReadPointer (0) + n);
        }

        return result;
    }

    struct Fixture
    {
        TempDir temp;
        AudioEngine engine;
        AudioFileCache cache { engine.getFormatManager() };
        InstrumentRack rack;
        ProjectPlayer player { engine, cache, &rack };
        juce::ValueTree project = model::createProject();

        Fixture() { engine.prepare (48000.0, 512); }
    };
}

TEST_CASE ("A region plays at exactly its position", "[engine]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);

    auto track = model::addTrack (f.project, "A", nullptr);
    model::addRegion (track, wav, 0.01, 0.5, nullptr); // starts at sample 480, 24000 samples long
    f.player.setProject (f.project);

    f.engine.play();
    const auto out = renderLeft (f.engine, 30000);

    const float expected = 0.5f * 0.8f; // file value * master gain

    CHECK (out[479] == 0.0f);
    CHECK_THAT (out[480], WithinAbs (expected, 1e-4));
    CHECK_THAT (out[10000], WithinAbs (expected, 1e-4));
    CHECK_THAT (out[24479], WithinAbs (expected, 1e-4));
    CHECK (out[24480] == 0.0f);

    CHECK_THAT (f.engine.getPlayheadSeconds(), WithinAbs (30000.0 / 48000.0, 1e-9));
}

TEST_CASE ("Stopped transport is silent and doesn't move", "[engine]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 4800, 0.5f);
    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.0, 0.1, nullptr);
    f.player.setProject (f.project);

    const auto out = renderLeft (f.engine, 2048);
    for (auto s : out)
        REQUIRE (s == 0.0f);

    CHECK (f.engine.getPlayheadSeconds() == 0.0);
}

TEST_CASE ("Source offset skips into the file", "[engine]")
{
    Fixture f;

    // First half 0.25, second half 0.75
    juce::AudioBuffer<float> b (1, 9600);
    for (int i = 0; i < 9600; ++i)
        b.setSample (0, i, i < 4800 ? 0.25f : 0.75f);

    const auto file = f.temp.dir.getChildFile ("steps.wav");
    {
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 9600);
    }

    auto region = model::addRegion (model::addTrack (f.project, "A", nullptr), file, 0.0, 0.1, nullptr);
    region.setProperty (ids::offset, 0.1, nullptr);       // start in the second half
    region.setProperty (ids::length, 0.05, nullptr);
    f.player.setProject (f.project);

    f.engine.play();
    const auto out = renderLeft (f.engine, 4096);
    CHECK_THAT (out[0], WithinAbs (0.75f * 0.8f, 1e-4));
    CHECK_THAT (out[2399], WithinAbs (0.75f * 0.8f, 1e-4));
    CHECK (out[2400] == 0.0f);
}

TEST_CASE ("Mute and solo", "[engine]")
{
    Fixture f;
    const auto loud  = writeConstantWav (f.temp.dir.getChildFile ("loud.wav"),  48000.0, 96000, 0.5f);
    const auto quiet = writeConstantWav (f.temp.dir.getChildFile ("quiet.wav"), 48000.0, 96000, 0.25f);

    auto a = model::addTrack (f.project, "A", nullptr);
    auto b = model::addTrack (f.project, "B", nullptr);
    model::addRegion (a, loud,  0.0, 2.0, nullptr);
    model::addRegion (b, quiet, 0.0, 2.0, nullptr);
    f.player.setProject (f.project);
    f.engine.play();

    SECTION ("both tracks sum")
    {
        const auto out = renderLeft (f.engine, 1024);
        CHECK_THAT (out[500], WithinAbs ((0.5f + 0.25f) * 0.8f, 1e-4));
    }

    SECTION ("muting fades the track out")
    {
        renderLeft (f.engine, 1024);
        a.setProperty (ids::mute, true, nullptr);
        f.player.rebuildNow();

        renderLeft (f.engine, 4800);             // let the 30 ms fade finish
        const auto out = renderLeft (f.engine, 1024);
        CHECK_THAT (out[500], WithinAbs (0.25f * 0.8f, 1e-4));
    }

    SECTION ("solo silences the others")
    {
        renderLeft (f.engine, 1024);
        b.setProperty (ids::solo, true, nullptr);
        f.player.rebuildNow();

        renderLeft (f.engine, 4800);
        const auto out = renderLeft (f.engine, 1024);
        CHECK_THAT (out[500], WithinAbs (0.25f * 0.8f, 1e-4));
    }

    SECTION ("volume and pan")
    {
        a.setProperty (ids::volume, 0.5, nullptr);
        a.setProperty (ids::pan, -1.0, nullptr);   // hard left: right side silent
        b.setProperty (ids::mute, true, nullptr);
        f.player.rebuildNow();
        renderLeft (f.engine, 4800);

        juce::AudioBuffer<float> out (2, 256);
        f.engine.render (nullptr, 0, out.getArrayOfWritePointers(), 2, 256);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (0.5f * 0.5f * 0.8f, 1e-4));
        CHECK_THAT (out.getSample (1, 100), WithinAbs (0.0f, 1e-4));
    }
}

TEST_CASE ("Files at another sample rate are resampled and stay aligned", "[engine]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("44k.wav"), 44100.0, 44100, 0.5f);

    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.1, 0.5, nullptr); // starts at sample 4800
    f.player.setProject (f.project);
    f.engine.play();

    const auto out = renderLeft (f.engine, 20000);
    const float expected = 0.5f * 0.8f;

    // Find where the audio crosses half level: should be right at the region start.
    int onset = -1;
    for (int i = 0; i < (int) out.size() && onset < 0; ++i)
        if (out[(size_t) i] > expected * 0.5f)
            onset = i;

    CHECK (std::abs (onset - 4800) <= 3);
    CHECK_THAT (out[10000], WithinAbs (expected, 0.01));
}

TEST_CASE ("Swapping graphs while playing keeps rendering", "[engine]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    auto track = model::addTrack (f.project, "A", nullptr);
    auto region = model::addRegion (track, wav, 0.0, 1.0, nullptr);
    f.player.setProject (f.project);
    f.engine.play();

    for (int i = 0; i < 50; ++i)
    {
        region.setProperty (ids::gain, 0.5 + 0.01 * i, nullptr);
        f.player.rebuildNow();
        const auto out = renderLeft (f.engine, 256);
        REQUIRE (out[100] > 0.0f);
    }
}

namespace
{
    /** Renders while feeding a constant value on every input channel. */
    void renderWithInput (AudioEngine& engine, int numSamples, float inputValue, int numInputs = 2,
                          std::vector<float>* leftOut = nullptr, int blockSize = 512)
    {
        juce::AudioBuffer<float> in (numInputs, blockSize), out (2, blockSize);
        for (int ch = 0; ch < numInputs; ++ch)
            juce::FloatVectorOperations::fill (in.getWritePointer (ch), inputValue + 0.1f * (float) ch, blockSize);

        for (int done = 0; done < numSamples; done += blockSize)
        {
            const int n = juce::jmin (blockSize, numSamples - done);
            engine.render (in.getArrayOfReadPointers(), numInputs, out.getArrayOfWritePointers(), 2, n);
            if (leftOut != nullptr)
                leftOut->insert (leftOut->end(), out.getReadPointer (0), out.getReadPointer (0) + n);
        }
    }

    std::unique_ptr<juce::AudioFormatReader> openReader (AudioEngine& engine, const juce::File& file)
    {
        return std::unique_ptr<juce::AudioFormatReader> (engine.getFormatManager().createReaderFor (file));
    }
}

TEST_CASE ("Recording writes the input to a WAV file", "[engine][record]")
{
    Fixture f;
    f.player.setProject (f.project);
    const auto file = f.temp.dir.getChildFile ("Audio 1#01.wav");

    juce::String error;
    REQUIRE (f.engine.startRecording ({ { "track-1", file, 1, 1 } }, error)); // input 2, mono
    CHECK (f.engine.isRecording());

    // Nothing is captured until the transport runs.
    renderWithInput (f.engine, 1024, 0.3f);
    f.engine.play();
    renderWithInput (f.engine, 48000, 0.3f);

    const auto live = f.engine.getLiveTakes();
    REQUIRE (live.size() == 1);
    CHECK_THAT (live[0].lengthSeconds, WithinAbs (1.0, 0.02));

    const auto takes = f.engine.stopRecording();
    CHECK_FALSE (f.engine.isRecording());
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].trackId == "track-1");
    CHECK (takes[0].startSample == 0);
    CHECK (takes[0].numSamples == 48000);

    auto reader = openReader (f.engine, file);
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 1);
    CHECK (reader->lengthInSamples == 48000);
    CHECK (reader->sampleRate == 48000.0);

    juce::AudioBuffer<float> data (1, 1000);
    reader->read (&data, 0, 1000, 20000, true, false);
    CHECK_THAT (data.getSample (0, 500), WithinAbs (0.4f, 1e-4)); // input 2 carries 0.3 + 0.1
}

TEST_CASE ("Punching in records from the current playhead", "[engine][record]")
{
    Fixture f;
    f.player.setProject (f.project);
    f.engine.play();
    renderWithInput (f.engine, 2048, 0.2f);   // playing, not recording yet

    juce::String error;
    REQUIRE (f.engine.startRecording ({ { "a", f.temp.dir.getChildFile ("a.wav"), 0, 2 } }, error));
    renderWithInput (f.engine, 4096, 0.2f);
    const auto takes = f.engine.stopRecording();

    REQUIRE (takes.size() == 1);
    CHECK (takes[0].startSample == 2048);
    CHECK (takes[0].numSamples == 4096);

    auto reader = openReader (f.engine, takes[0].file);
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
}

TEST_CASE ("A take with no audio leaves no file behind", "[engine][record]")
{
    Fixture f;
    const auto file = f.temp.dir.getChildFile ("empty.wav");

    juce::String error;
    REQUIRE (f.engine.startRecording ({ { "a", file, 0, 1 } }, error));
    CHECK (f.engine.stopRecording().empty());
    CHECK_FALSE (file.exists());
}

TEST_CASE ("Input monitoring on an armed track", "[engine][record]")
{
    Fixture f;
    auto track = model::addTrack (f.project, "Vox", nullptr);
    f.player.setProject (f.project);

    std::vector<float> out;
    renderWithInput (f.engine, 1024, 0.5f, 2, &out);
    CHECK (out[500] == 0.0f); // not armed

    track.setProperty (ids::armed, true, nullptr);
    track.setProperty (ids::monitor, true, nullptr);
    f.player.rebuildNow();

    out.clear();
    renderWithInput (f.engine, 1024, 0.5f, 2, &out); // works while stopped, too
    CHECK_THAT (out[500], WithinAbs (0.5f * 0.8f, 1e-4));

    track.setProperty (ids::monitor, false, nullptr);
    f.player.rebuildNow();
    out.clear();
    renderWithInput (f.engine, 1024, 0.5f, 2, &out);
    CHECK (out[500] == 0.0f);
}

TEST_CASE ("Metronome clicks on the beat", "[engine]")
{
    Fixture f;
    f.project.setProperty (ids::metronome, true, nullptr);   // 120 BPM: a beat every 24000 samples
    f.player.setProject (f.project);
    f.engine.play();

    const auto out = renderLeft (f.engine, 50000);

    auto peakAround = [&out] (int centre)
    {
        float peak = 0.0f;
        for (int i = juce::jmax (0, centre); i < juce::jmin ((int) out.size(), centre + 400); ++i)
            peak = juce::jmax (peak, std::abs (out[(size_t) i]));
        return peak;
    };

    CHECK (peakAround (0) > 0.1f);       // beat 1 (downbeat)
    CHECK (peakAround (24000) > 0.1f);   // beat 2
    CHECK (peakAround (48000) > 0.1f);   // beat 3
    CHECK (peakAround (0) > peakAround (24000)); // the downbeat is accented
    CHECK (out[12000] == 0.0f);          // silence between beats
    CHECK (out[23990] == 0.0f);
}

#include "engine/LatencyMeasure.h"

TEST_CASE ("Playback wraps at the cycle end on the exact sample", "[engine][cycle]")
{
    Fixture f;
    // Audio: 0.25 for the first half second, 0.75 after.
    juce::AudioBuffer<float> b (1, 96000);
    for (int i = 0; i < 96000; ++i)
        b.setSample (0, i, i < 24000 ? 0.25f : 0.75f);
    const auto file = f.temp.dir.getChildFile ("steps.wav");
    {
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 96000);
    }

    model::addRegion (model::addTrack (f.project, "A", nullptr), file, 0.0, 2.0, nullptr);
    model::setCycle (f.project, 0.25, 0.75, true); // samples 12000 .. 36000
    f.player.setProject (f.project);

    f.engine.setPlayheadSeconds (0.25);
    f.engine.play();

    const auto out = renderLeft (f.engine, 30000, 512);
    // 24000 samples of cycle, then back to 12000.
    CHECK_THAT (out[11999], WithinAbs (0.25f * 0.8f, 1e-4));   // sample 23999
    CHECK_THAT (out[12000], WithinAbs (0.75f * 0.8f, 1e-4));   // sample 24000
    CHECK_THAT (out[23999], WithinAbs (0.75f * 0.8f, 1e-4));   // sample 35999, last in the cycle
    CHECK_THAT (out[24000], WithinAbs (0.25f * 0.8f, 1e-4));   // wrapped to 12000
    CHECK (f.engine.getPlayheadSeconds() == (12000.0 + 6000.0) / 48000.0);
}

TEST_CASE ("Playhead parked after the cycle plays on", "[engine][cycle]")
{
    Fixture f;
    model::setCycle (f.project, 0.0, 0.5, true);
    f.player.setProject (f.project);
    f.engine.setPlayheadSeconds (1.0);
    f.engine.play();
    renderLeft (f.engine, 4800);
    CHECK_THAT (f.engine.getPlayheadSeconds(), WithinAbs (1.1, 1e-9));
}

TEST_CASE ("Count-in clicks, then recording starts on the downbeat", "[engine][record]")
{
    Fixture f;
    f.player.setProject (f.project);  // metronome off
    const auto file = f.temp.dir.getChildFile ("ci.wav");

    juce::String error;
    REQUIRE (f.engine.startRecording ({ { "a", file, 0, 1 } }, error, (juce::int64) 0));
    f.engine.setPlayheadSecondsAllowingNegative (-2.0);  // one bar at 120 BPM
    f.engine.play();
    CHECK (f.engine.isCountingIn());

    std::vector<float> out;
    renderWithInput (f.engine, 96000 + 24000, 0.3f, 2, &out);
    CHECK_FALSE (f.engine.isCountingIn());

    // Clicks on each beat of the count-in...
    for (int beat = 0; beat < 4; ++beat)
    {
        float peak = 0.0f;
        for (int i = beat * 24000; i < beat * 24000 + 400; ++i)
            peak = juce::jmax (peak, std::abs (out[(size_t) i]));
        CHECK (peak > 0.1f);
    }

    // ...but not after it, because the metronome is off.
    float after = 0.0f;
    for (int i = 96000; i < 96000 + 400; ++i)
        after = juce::jmax (after, std::abs (out[(size_t) i]));
    CHECK (after == 0.0f);

    const auto takes = f.engine.stopRecording();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].startSample == 0);
    CHECK (takes[0].numSamples == 24000);
}

TEST_CASE ("Recording through cycle passes is one continuous file", "[engine][record][cycle]")
{
    Fixture f;
    model::setCycle (f.project, 0.0, 0.5, true);
    f.player.setProject (f.project);

    juce::String error;
    REQUIRE (f.engine.startRecording ({ { "a", f.temp.dir.getChildFile ("loop.wav"), 0, 1 } }, error, (juce::int64) 0));
    f.engine.play();
    renderWithInput (f.engine, 24000 * 3 + 5000, 0.2f);

    const auto takes = f.engine.stopRecording();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].numSamples == 77000);

    const auto pieces = model::splitRecording (0.0, 77000.0 / 48000.0, 0.0, true, 0.0, 0.5);
    REQUIRE (pieces.size() == 4);
    CHECK_THAT (pieces[3].length, WithinAbs (5000.0 / 48000.0, 1e-9));
}

TEST_CASE ("Latency estimator finds a loopback delay", "[latency]")
{
    const double rate = 48000.0;
    const auto length = latency::measurementLength (rate);
    const auto burst = latency::makeBurst (rate);
    juce::Random random (42);

    for (const int trueDelay : { 0, 37, 517, 2400 })
    {
        std::vector<float> recorded ((size_t) length);
        for (auto& s : recorded)
            s = 0.01f * (random.nextFloat() - 0.5f); // a little noise

        for (const auto pos : latency::burstPositions (rate))
            for (size_t i = 0; i < burst.size(); ++i)
                if (pos + trueDelay + (int) i < length)
                    recorded[(size_t) (pos + trueDelay) + i] += -0.3f * burst[i]; // quieter and inverted

        const auto result = latency::estimateDelay (recorded.data(), length, rate);
        INFO ("true delay " << trueDelay);
        CHECK (result.ok);
        CHECK (result.delaySamples == trueDelay);
    }

    SECTION ("silence is reported as a failure")
    {
        std::vector<float> silence ((size_t) length, 0.0f);
        const auto result = latency::estimateDelay (silence.data(), length, rate);
        CHECK_FALSE (result.ok);
        CHECK (result.error.isNotEmpty());
    }
}

TEST_CASE ("Engine latency measurement through a simulated cable", "[latency][engine]")
{
    Fixture f;
    f.player.setProject (f.project);

    juce::String error;
    REQUIRE (f.engine.startLatencyMeasurement (0, error));
    CHECK (f.engine.isMeasuringLatency());

    // Loop the output back into the input 300 samples later.
    constexpr int cableDelay = 300, block = 256;
    std::vector<float> line ((size_t) (48000 * 5), 0.0f);
    juce::AudioBuffer<float> in (1, block), out (2, block);
    std::optional<latency::Result> result;

    for (int t = 0; t + block < (int) line.size() && ! result.has_value(); t += block)
    {
        for (int i = 0; i < block; ++i)
            in.setSample (0, i, t + i >= cableDelay ? line[(size_t) (t + i - cableDelay)] : 0.0f);

        f.engine.render (in.getArrayOfReadPointers(), 1, out.getArrayOfWritePointers(), 2, block);

        for (int i = 0; i < block; ++i)
            line[(size_t) (t + i)] = out.getSample (0, i);

        result = f.engine.takeLatencyMeasurement();
    }

    REQUIRE (result.has_value());
    CHECK (result->ok);
    CHECK (result->delaySamples == cableDelay);
    CHECK_FALSE (f.engine.isMeasuringLatency());

    // Normal playback resumes afterwards.
    f.engine.play();
    renderLeft (f.engine, 512);
    CHECK (f.engine.getPlayheadSeconds() > 0.0);
}

namespace
{
    int firstNonSilent (const std::vector<float>& v, float threshold = 1e-6f)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (std::abs (v[i]) > threshold)
                return (int) i;
        return -1;
    }

    float peakIn (const std::vector<float>& v, int from, int to)
    {
        float p = 0.0f;
        for (int i = juce::jmax (0, from); i < juce::jmin ((int) v.size(), to); ++i)
            p = juce::jmax (p, std::abs (v[(size_t) i]));
        return p;
    }
}

TEST_CASE ("Built-in synth plays notes sample-accurately", "[synth]")
{
    BuiltInSynth synth;
    synth.prepare (48000.0, 4096);
    BuiltInSynth::Settings s;
    s.attack = 0.001f;
    s.release = 0.05f;
    synth.setSettings (s);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 100);
    midi.addEvent (juce::MidiMessage::noteOff (1, 69), 2000);

    std::vector<float> l (4096), r (4096);
    synth.render (l.data(), r.data(), 4096, midi);

    CHECK (firstNonSilent (l) == 101);           // nothing before the note-on (the first sample is the zero crossing)
    CHECK (peakIn (l, 500, 1900) > 0.01f);
    CHECK (l == r);                               // mono voice, stereo out
    CHECK (synth.getNumActiveVoices() == 1);      // releasing

    // After the release has finished the voice is free and output is silent.
    juce::MidiBuffer none;
    for (int i = 0; i < 4; ++i)
        synth.render (l.data(), r.data(), 4096, none);
    CHECK (synth.getNumActiveVoices() == 0);
    CHECK (peakIn (l, 0, 4096) == 0.0f);

    // All-notes-off releases everything.
    juce::MidiBuffer chord;
    for (int n : { 60, 64, 67 })
        chord.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 90), 0);
    synth.render (l.data(), r.data(), 512, chord);
    CHECK (synth.getNumActiveVoices() == 3);
    juce::MidiBuffer panic;
    panic.addEvent (juce::MidiMessage::allSoundOff (1), 0);
    synth.render (l.data(), r.data(), 512, panic);
    CHECK (synth.getNumActiveVoices() == 0);
}

TEST_CASE ("Instrument tracks play MIDI regions on time", "[engine][midi]")
{
    Fixture f;
    auto track = model::addTrack (f.project, "Keys", nullptr, -1, model::instrumentKind);
    track.setProperty (ids::synthAttack, 0.001, nullptr);
    track.setProperty (ids::synthRelease, 0.02, nullptr);

    auto region = model::addMidiRegion (track, 0.5, 1.0, nullptr);       // from sample 24000
    model::addNote (region, 60, 0.25, 0.25, 100, nullptr);             // sample 36000 .. 48000
    f.player.setProject (f.project);
    f.engine.play();

    const auto out = renderLeft (f.engine, 72000);
    CHECK (firstNonSilent (out) == 36001);
    CHECK (peakIn (out, 37000, 47000) > 0.005f);
    CHECK (peakIn (out, 48000 + 2400, 72000) == 0.0f); // released and silent after 20 ms + margin
}

TEST_CASE ("Stopping playback releases sounding notes", "[engine][midi]")
{
    Fixture f;
    auto track = model::addTrack (f.project, "Pad", nullptr, -1, model::instrumentKind);
    track.setProperty (ids::synthRelease, 0.01, nullptr);
    model::addNote (model::addMidiRegion (track, 0.0, 10.0, nullptr), 60, 0.0, 9.0, 100, nullptr);
    f.player.setProject (f.project);

    f.engine.play();
    renderLeft (f.engine, 4800);
    f.engine.stop();
    renderLeft (f.engine, 4800);                       // release happens here
    const auto after = renderLeft (f.engine, 4800);
    CHECK (peakIn (after, 0, 4800) == 0.0f);
}

TEST_CASE ("Live MIDI plays only the live track", "[engine][midi]")
{
    Fixture f;
    auto a = model::addTrack (f.project, "A", nullptr, -1, model::instrumentKind);
    auto b = model::addTrack (f.project, "B", nullptr, -1, model::instrumentKind);
    b.setProperty (ids::mute, true, nullptr);   // so anything heard comes from A
    f.player.setProject (f.project);

    // No live track yet: a key press makes no sound.
    f.engine.sendLiveMidi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    CHECK (peakIn (renderLeft (f.engine, 2048), 0, 2048) == 0.0f);
    f.engine.sendLiveMidi (juce::MidiMessage::allNotesOff (1));
    renderLeft (f.engine, 512);

    f.player.setLiveMidiTrack (a[ids::id].toString());
    f.player.rebuildNow();
    f.engine.sendLiveMidi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    CHECK (peakIn (renderLeft (f.engine, 2048), 0, 2048) > 0.005f); // works while stopped
}

TEST_CASE ("Recording MIDI captures what you play, at the playhead", "[engine][midi][record]")
{
    Fixture f;
    auto track = model::addTrack (f.project, "Keys", nullptr, -1, model::instrumentKind);
    f.player.setProject (f.project);
    f.player.setLiveMidiTrack (track[ids::id].toString());
    f.player.rebuildNow();

    AudioEngine::RecordRequest request;
    request.trackId = track[ids::id].toString();
    request.midi = true;

    juce::String error;
    REQUIRE (f.engine.startRecording ({ request }, error, (juce::int64) 0));
    f.engine.play();

    renderLeft (f.engine, 4096);                                         // playhead 4096
    f.engine.sendLiveMidi (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90));
    renderLeft (f.engine, 4096);                                         // event lands at 4096
    f.engine.sendLiveMidi (juce::MidiMessage::noteOff (1, 62));
    renderLeft (f.engine, 4096);                                         // at 8192

    const auto takes = f.engine.stopRecording();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].isMidi);
    CHECK (takes[0].startSample == 0);
    REQUIRE (takes[0].midiEvents.size() == 2);
    CHECK (takes[0].midiEvents[0].sample == 4096);
    CHECK (takes[0].midiEvents[0].data1 == 62);
    CHECK (takes[0].midiEvents[1].sample == 8192);
}

#include "engine/BuiltInEffects.h"
#include "engine/EffectRack.h"

namespace
{
    std::unique_ptr<BuiltInEffect> makeEffect (const juce::String& type, double rate = 48000.0)
    {
        auto e = BuiltInEffect::create (type);
        REQUIRE (e != nullptr);
        e->prepare (rate, 4096);
        return e;
    }

    void setParam (BuiltInEffect& e, const juce::String& id, float v)
    {
        const auto& params = e.getInfo().params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id)
                e.setParameter ((int) i, v);
    }

    float sineLevelAfter (BuiltInEffect& e, double freq, double rate = 48000.0)
    {
        std::vector<float> l (16384), r (16384);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) i / rate);
        e.process (l.data(), r.data(), (int) l.size());
        return peakIn (l, 8000, 16384); // after it settles
    }
}

TEST_CASE ("Built-in effects do what they say", "[effects]")
{
    SECTION ("EQ boosts and cuts around its bands")
    {
        auto eq = makeEffect ("eq");
        CHECK_THAT (sineLevelAfter (*eq, 1000.0), WithinAbs (0.25, 0.005));   // flat by default

        setParam (*eq, "midGain", 12.0f);
        CHECK (sineLevelAfter (*eq, 1000.0) > 0.25f * 3.5f);                 // ~ +12 dB at the centre

        auto cut = makeEffect ("eq");
        setParam (*cut, "lowGain", -18.0f);
        CHECK (sineLevelAfter (*cut, 40.0) < 0.25f * 0.2f);
        CHECK_THAT (sineLevelAfter (*cut, 5000.0), WithinAbs (0.25, 0.01));   // highs untouched
    }

    SECTION ("compressor turns loud signals down")
    {
        auto comp = makeEffect ("compressor");
        setParam (*comp, "threshold", -24.0f);
        setParam (*comp, "ratio", 10.0f);
        const auto out = sineLevelAfter (*comp, 200.0);       // -12 dBFS in, 12 dB over the threshold
        const auto db = juce::Decibels::gainToDecibels (out);
        CHECK (db < -20.0f);
        CHECK (db > -24.5f);
    }

    SECTION ("delay repeats after its time")
    {
        auto delay = makeEffect ("delay");
        setParam (*delay, "time", 100.0f);    // 4800 samples
        setParam (*delay, "mix", 1.0f);
        setParam (*delay, "feedback", 0.0f);
        delay->prepare (48000.0, 4096);        // start at the new time without gliding

        std::vector<float> l (8192, 0.0f), r (8192, 0.0f);
        l[0] = r[0] = 1.0f;
        delay->process (l.data(), r.data(), 8192);
        CHECK (l[0] == 0.0f);                   // fully wet: no dry impulse
        CHECK_THAT (l[4800], WithinAbs (1.0f, 1e-3));
        CHECK (peakIn (l, 4802, 8192) < 1e-3f);
    }

    SECTION ("reverb leaves a tail")
    {
        auto reverb = makeEffect ("reverb");
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[0] = r[0] = 1.0f;
        reverb->process (l.data(), r.data(), 48000);
        CHECK (peakIn (l, 10000, 20000) > 1e-4f);
    }

    SECTION ("limiter never goes over its ceiling")
    {
        auto limiter = makeEffect ("limiter");
        setParam (*limiter, "ceiling", -6.0f);
        std::vector<float> l (4800), r (4800);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (i % 100 < 50 ? 1.0f : -1.0f);
        limiter->process (l.data(), r.data(), 4800);
        CHECK (peakIn (l, 0, 4800) <= juce::Decibels::decibelsToGain (-6.0f) + 1e-6f);
    }
}

TEST_CASE ("Insert chains run on tracks and on the master", "[engine][mixer]")
{
    Fixture f;
    EffectRack effects (f.rack.getFormatManager());
    ProjectPlayer player (f.engine, f.cache, &f.rack, &effects);

    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("loud.wav"), 48000.0, 96000, 0.9f);
    auto track = model::addTrack (f.project, "A", nullptr);
    model::addRegion (track, wav, 0.0, 2.0, nullptr);
    f.project.setProperty (ids::masterGain, 1.0, nullptr);

    auto limiter = model::addBuiltinInsert (model::chainFor (f.project, track[ids::id]), "limiter", 0, nullptr);
    limiter.setProperty ("ceiling", -12.0, nullptr);
    player.setProject (f.project);
    f.engine.play();

    auto out = renderLeft (f.engine, 9600);
    CHECK_THAT (out[9000], WithinAbs (juce::Decibels::decibelsToGain (-12.0f), 1e-3));

    // Bypassed: the signal passes untouched.
    limiter.setProperty (ids::bypass, true, nullptr);
    player.rebuildNow();
    out = renderLeft (f.engine, 9600);
    CHECK_THAT (out[9000], WithinAbs (0.9f, 1e-3));

    // On the master instead.
    auto masterLimiter = model::addBuiltinInsert (model::chainFor (f.project, model::masterOwner), "limiter", 0, nullptr);
    masterLimiter.setProperty ("ceiling", -10.0, nullptr);
    player.rebuildNow();
    out = renderLeft (f.engine, 9600);
    CHECK_THAT (out[9000], WithinAbs (juce::Decibels::decibelsToGain (-10.0f), 1e-3));

    // Track meters see the post-fader level.
    const auto [l, r] = f.engine.takeTrackLevel (player.slotOf (track[ids::id]));
    CHECK_THAT (l, WithinAbs (0.9f, 1e-3));
    CHECK_THAT (r, WithinAbs (0.9f, 1e-3));
}

TEST_CASE ("Exporting renders the song to a WAV file", "[engine][export]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.5, 1.0, nullptr);
    f.project.setProperty (ids::metronome, true, nullptr); // must not end up in the file
    f.player.setProject (f.project);

    f.engine.setPlayheadSeconds (3.0);
    const auto out = f.temp.dir.getChildFile ("Song.wav");
    double lastProgress = 0.0;
    const auto error = f.engine.bounce (out, 0.0, 2.0, 24, [&] (double p) { lastProgress = p; return true; });

    CHECK (error.isEmpty());
    CHECK (lastProgress == 1.0);
    CHECK (f.engine.getPlayheadSeconds() == 3.0);   // put back afterwards
    CHECK_FALSE (f.engine.isPlaying());

    auto reader = std::unique_ptr<juce::AudioFormatReader> (f.engine.getFormatManager().createReaderFor (out));
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples == 96000);
    CHECK (reader->numChannels == 2);

    juce::AudioBuffer<float> data (2, 96000);
    reader->read (&data, 0, 96000, 0, true, true);
    CHECK (data.getMagnitude (0, 0, 23900) == 0.0f);                      // silence before the region, no click
    CHECK_THAT (data.getSample (0, 30000), WithinAbs (0.5f * 0.8f, 1e-4));
    CHECK_THAT (data.getSample (1, 70000), WithinAbs (0.5f * 0.8f, 1e-4));
    CHECK (data.getMagnitude (0, 72100, 96000 - 72100) == 0.0f);

    // Cancelling removes the partial file.
    const auto cancelled = f.temp.dir.getChildFile ("Cancelled.wav");
    CHECK (f.engine.bounce (cancelled, 0.0, 2.0, 24, [] (double) { return false; }) == "cancelled");
    CHECK_FALSE (cancelled.exists());
}

namespace
{
    /** A plain delay that reports its latency, like a look-ahead plugin. */
    struct LatencyEffect final : public Effect
    {
        explicit LatencyEffect (int samples) : latency (samples) {}
        void prepare (double rate, int) override { preparedRate = rate; line.clear(); }
        void process (float* l, float* r, int n) noexcept override { line.process (l, r, n, latency); }
        juce::String getName() const override { return "Latency"; }
        int getLatencySamples() const override { return latency; }

        int latency;
        DelayLine line;
    };

    juce::File writeImpulseWav (const juce::File& file, int position)
    {
        juce::AudioBuffer<float> b (1, 48000);
        b.clear();
        b.setSample (0, position, 0.5f);
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 48000);
        return file;
    }

    struct RoutingFixture : Fixture
    {
        EffectRack effects { rack.getFormatManager() };
        ProjectPlayer router { engine, cache, &rack, &effects };
        juce::File loud = writeConstantWav (temp.dir.getChildFile ("half.wav"), 48000.0, 96000, 0.5f);

        juce::ValueTree audioTrack (const juce::String& name, const juce::File& file)
        {
            auto t = model::addTrack (project, name, nullptr);
            model::addRegion (t, file, 0.0, 2.0, nullptr);
            return t;
        }
    };
}

TEST_CASE ("Sends feed aux buses, before or after the fader", "[engine][routing]")
{
    RoutingFixture f;
    auto track = f.audioTrack ("A", f.loud);
    auto bus = model::addTrack (f.project, "Bus", nullptr, -1, model::busKind);
    auto send = model::addSend (model::chainFor (f.project, track[ids::id]), bus[ids::id], 0.5, nullptr);
    f.router.setProject (f.project);
    f.engine.play();

    // Direct 0.5 + bus 0.25, through the 0.8 master.
    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs ((0.5f + 0.25f) * 0.8f, 1e-4));

    // Fader down: a post-fader send goes quiet too...
    track.setProperty (ids::volume, 0.0, nullptr);
    f.router.rebuildNow();
    renderLeft (f.engine, 4800);
    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs (0.0f, 1e-4));

    // ...but a pre-fader send keeps going.
    send.setProperty (ids::pre, true, nullptr);
    f.router.rebuildNow();
    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs (0.25f * 0.8f, 1e-4));

    // Muting the track silences its sends, pre-fader or not.
    track.setProperty (ids::mute, true, nullptr);
    f.router.rebuildNow();
    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs (0.0f, 1e-4));
}

TEST_CASE ("Buses have their own effects and stay up when another track is soloed", "[engine][routing]")
{
    RoutingFixture f;
    auto track = f.audioTrack ("A", f.loud);
    auto other = f.audioTrack ("B", f.loud);
    auto bus = model::addTrack (f.project, "Bus", nullptr, -1, model::busKind);
    model::addSend (model::chainFor (f.project, track[ids::id]), bus[ids::id], 1.0, nullptr);
    track.setProperty (ids::volume, 0.0, nullptr);   // only the bus is heard from A
    other.setProperty (ids::mute, true, nullptr);

    auto limiter = model::addBuiltinInsert (model::chainFor (f.project, bus[ids::id]), "limiter", 0, nullptr);
    limiter.setProperty ("ceiling", -12.0, nullptr);
    f.project.setProperty (ids::masterGain, 1.0, nullptr);

    // A pre-fader send so the bus hears A even with its fader down.
    model::sendsOf (model::findChain (f.project, track[ids::id]))[0].setProperty (ids::pre, true, nullptr);

    track.setProperty (ids::solo, true, nullptr);
    f.router.setProject (f.project);
    f.engine.play();

    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs (juce::Decibels::decibelsToGain (-12.0f), 1e-3));
}

TEST_CASE ("Volume and effect automation play back", "[engine][automation]")
{
    RoutingFixture f;
    auto track = f.audioTrack ("A", f.loud);
    const auto id = track[ids::id].toString();
    f.project.setProperty (ids::masterGain, 1.0, nullptr);

    auto lane = model::laneFor (f.project, id, model::volumeParam, true);
    model::addPoint (lane, 0.0, 1.0, nullptr);
    model::addPoint (lane, 1.0, 0.0, nullptr);     // fade out over the first second
    f.router.setProject (f.project);
    renderLeft (f.engine, 4800);                    // let the master volume settle at 1.0
    f.engine.play();

    const auto out = renderLeft (f.engine, 48000);
    CHECK_THAT (out[100], WithinAbs (0.5f, 0.02));
    CHECK_THAT (out[24000], WithinAbs (0.25f, 0.02));   // halfway
    CHECK_THAT (out[47000], WithinAbs (0.0f, 0.03)); // read once per block, then smoothed: a few ms behind

    // Stopped: the fader's own value again.
    f.engine.stop();
    f.engine.setPlayheadSeconds (0.0);

    // An effect knob following its lane: the limiter's ceiling.
    lane.removeAllChildren (nullptr);
    auto limiter = model::addBuiltinInsert (model::chainFor (f.project, id), "limiter", 0, nullptr);
    auto ceiling = model::laneFor (f.project, id, model::insertParam (limiter[ids::id], "ceiling"), true);
    model::addPoint (ceiling, 0.0, -12.0, nullptr);
    f.router.rebuildNow();
    f.engine.play();

    CHECK_THAT (renderLeft (f.engine, 4800)[2000], WithinAbs (juce::Decibels::decibelsToGain (-12.0f), 1e-3));
}

TEST_CASE ("Delay compensation lines tracks up", "[engine][pdc]")
{
    RoutingFixture f;
    const auto impulse = writeImpulseWav (f.temp.dir.getChildFile ("click.wav"), 1000);
    auto slow = f.audioTrack ("Slow", impulse);
    auto fast = f.audioTrack ("Fast", impulse);
    f.project.setProperty (ids::masterGain, 1.0, nullptr);

    // A 100-sample plugin on the slow track.
    auto insert = model::addPluginInsert (model::chainFor (f.project, slow[ids::id]), "test-plugin", 0, nullptr);
    f.effects.injectForTesting (insert, std::make_shared<LatencyEffect> (100), 48000.0);
    f.router.setProject (f.project);
    renderLeft (f.engine, 4800);                    // let the master volume settle at 1.0
    f.engine.play();

    auto out = renderLeft (f.engine, 4800);
    CHECK (f.engine.getPlaybackLatencySamples() == 100);
    CHECK_THAT (out[1100], WithinAbs (1.0f, 1e-4));          // both clicks together, 100 samples late
    CHECK (peakIn (out, 0, 1100) == 0.0f);
    CHECK (peakIn (out, 1101, 4800) == 0.0f);

    // A bus with 50 samples of latency: everything waits for it.
    auto bus = model::addTrack (f.project, "Bus", nullptr, -1, model::busKind);
    auto busInsert = model::addPluginInsert (model::chainFor (f.project, bus[ids::id]), "test-plugin-2", 0, nullptr);
    f.effects.injectForTesting (busInsert, std::make_shared<LatencyEffect> (50), 48000.0);
    model::addSend (model::chainFor (f.project, fast[ids::id]), bus[ids::id], 1.0, nullptr);
    f.router.rebuildNow();

    f.engine.stop();
    f.engine.setPlayheadSeconds (0.0);
    f.engine.play();
    renderLeft (f.engine, 9600);       // flush the delay lines
    f.engine.setPlayheadSeconds (0.0);
    out = renderLeft (f.engine, 4800);

    CHECK (f.engine.getPlaybackLatencySamples() == 150);
    CHECK_THAT (out[1150], WithinAbs (1.5f, 1e-4));           // slow + fast + fast's send, all at once
    CHECK (peakIn (out, 0, 1150) == 0.0f);
    CHECK (peakIn (out, 1151, 4800) == 0.0f);
}

//==============================================================================
// Master Bus Compressor
//==============================================================================
#include "engine/MasterBusCompressor.h"

namespace
{
    /** Runs a sine of the given peak through the compressor; returns the output's peak over the last 0.2 s. */
    float compressSine (MasterBusCompressor& comp, float amplitude, double seconds = 1.0)
    {
        constexpr double rate = 48000.0;
        const auto total = (int) (seconds * rate);
        std::vector<float> l (512), r (512);
        float peak = 0.0f;

        for (int done = 0; done < total; done += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                l[(size_t) i] = r[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * (done + i) / rate);
            }

            comp.process (l.data(), r.data(), 512);

            if (done >= total - (int) (0.2 * rate))
                for (auto s : l)
                    peak = juce::jmax (peak, std::abs (s));
        }

        return peak;
    }
}

TEST_CASE ("The Master Bus Compressor", "[buscomp]")
{
    MasterBusCompressor comp;
    MasterBusCompressor::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 1;            // 4:1
    s.attack = 3;           // 3 ms
    s.release = 2;          // 0.6 s

    SECTION ("does nothing while it's switched off")
    {
        s.enabled = false;
        comp.setSettings (s);
        comp.prepare (48000.0);
        CHECK_THAT (compressSine (comp, 0.9f), WithinAbs (0.9, 1e-3));
        CHECK (comp.getGainReductionDb() == 0.0f);
    }

    SECTION ("leaves quiet signals alone")
    {
        s.enabled = true;
        comp.setSettings (s);
        comp.prepare (48000.0);
        CHECK_THAT (compressSine (comp, 0.05f), WithinAbs (0.05, 1e-3));   // -26 dB, under the threshold
    }

    SECTION ("reduces loud signals by the ratio")
    {
        s.enabled = true;
        comp.setSettings (s);
        comp.prepare (48000.0);

        // 0 dB peak, 20 dB over a -20 dB threshold at 4:1 -> about 15 dB of reduction.
        const auto outDb = juce::Decibels::gainToDecibels (compressSine (comp, 1.0f));
        CHECK (outDb < -12.0f);
        CHECK (outDb > -16.5f);

        const auto gr = comp.getGainReductionDb();
        CHECK (gr > 13.0f);
        CHECK (gr < 16.5f);

        comp.meterRead();
        CHECK (comp.getGainReductionDb() == 0.0f);
    }

    SECTION ("a higher ratio squeezes harder")
    {
        s.enabled = true;
        s.ratio = 0;        // 2:1
        comp.setSettings (s);
        comp.prepare (48000.0);
        const auto gentle = compressSine (comp, 1.0f);

        s.ratio = 2;        // 10:1
        comp.setSettings (s);
        comp.prepare (48000.0);
        const auto hard = compressSine (comp, 1.0f);

        CHECK (hard < gentle * 0.5f);
    }

    SECTION ("make-up gain and the mix")
    {
        s.enabled = true;
        comp.setSettings (s);
        comp.prepare (48000.0);
        const auto plain = compressSine (comp, 1.0f);

        s.makeupDb = 6.0f;
        comp.setSettings (s);
        comp.prepare (48000.0);
        CHECK_THAT (compressSine (comp, 1.0f), WithinAbs (plain * 1.9953f, 0.02));

        s.makeupDb = 0.0f;
        s.mix = 0.0f;       // fully dry
        comp.setSettings (s);
        comp.prepare (48000.0);
        CHECK_THAT (compressSine (comp, 1.0f), WithinAbs (1.0, 1e-3));
    }

    SECTION ("switching it on fades in rather than clicking")
    {
        s.enabled = false;
        comp.setSettings (s);
        comp.prepare (48000.0);

        s.enabled = true;
        comp.setSettings (s);

        std::vector<float> l (48, 1.0f), r (48, 1.0f);
        comp.process (l.data(), r.data(), 48);   // 1 ms: at most 5% of the way in
        CHECK (l[47] > 0.9f);
    }
}

TEST_CASE ("The Master Bus Compressor is set from the project and runs on the master", "[buscomp]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("loud.wav"), 48000.0, 48000, 0.9f);
    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.0, 1.0, nullptr);

    auto settings = MasterBusCompressor::fromProject (f.project);
    CHECK_FALSE (settings.enabled);                 // off in a new project
    CHECK (settings.release == MasterBusCompressor::autoRelease);

    juce::UndoManager um;
    MasterBusCompressor::setInProject (f.project, ids::busCompOn, true, &um);
    MasterBusCompressor::setInProject (f.project, ids::busCompThreshold, -20.0, &um);
    MasterBusCompressor::setInProject (f.project, ids::busCompRatio, 2, &um);
    MasterBusCompressor::setInProject (f.project, ids::busCompThreshold, -99.0, nullptr);   // clamped on reading
    settings = MasterBusCompressor::fromProject (f.project);
    CHECK (settings.enabled);
    CHECK (settings.ratio == 2);
    CHECK (settings.thresholdDb == MasterBusCompressor::minThresholdDb);
    MasterBusCompressor::setInProject (f.project, ids::busCompThreshold, -20.0, nullptr);

    f.player.setProject (f.project);
    f.player.rebuildNow();

    f.engine.play();
    const auto out = renderLeft (f.engine, 24000);

    // 0.9 * 0.8 master would be 0.72 without it; 10:1 from -20 dB pulls it far down.
    CHECK (std::abs (out[20000]) < 0.3f);
    CHECK (f.engine.takeBusCompGainReduction() > 6.0f);

    // Saved and loaded with the project.
    const auto file = f.temp.dir.getChildFile ("comp.atp");
    juce::String error;
    REQUIRE (model::save (f.project, file, error));
    const auto loaded = model::load (file, error);
    REQUIRE (error.isEmpty());
    CHECK (MasterBusCompressor::fromProject (loaded) == MasterBusCompressor::fromProject (f.project));
}

//==============================================================================
// Console channel (EQ and Dynamics on each strip)
//==============================================================================
#include "engine/ConsoleChannel.h"

namespace
{
    float sinePeakThrough (ConsoleChannel& c, double frequency, float amplitude = 0.25f)
    {
        constexpr double rate = 48000.0;
        std::vector<float> l (480), r (480);
        float peak = 0.0f;

        for (int block = 0; block < 100; ++block)
        {
            for (int i = 0; i < 480; ++i)
                l[(size_t) i] = r[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (block * 480 + i) / rate);
            c.process (l.data(), r.data(), 480);
            if (block >= 80)
                for (auto s : l)
                    peak = juce::jmax (peak, std::abs (s));
        }

        return peak;
    }
}

TEST_CASE ("The console EQ and Dynamics", "[console]")
{
    ConsoleChannel c;
    c.prepare (48000.0);
    ConsoleChannel::Settings s;

    SECTION ("flat settings leave the sound untouched")
    {
        CHECK_FALSE (s.isActive());
        c.setSettings (s);
        CHECK (sinePeakThrough (c, 1000.0) == 0.25f);
    }

    SECTION ("the full strip: filters, a moved band, trims and the gate")
    {
        s.hpf = 200.0f;
        c.setSettings (s);
        CHECK (s.isActive());
        CHECK (juce::Decibels::gainToDecibels (sinePeakThrough (c, 50.0) / 0.25f) < -20.0f);
        CHECK (std::abs (juce::Decibels::gainToDecibels (sinePeakThrough (c, 2000.0) / 0.25f)) < 1.0f);

        s.hpf = 0.0f;
        s.hmf = 10.0f;
        s.hmfFreq = 1000.0f;
        c.setSettings (s);
        c.reset();
        CHECK (juce::Decibels::gainToDecibels (sinePeakThrough (c, 1000.0) / 0.25f) > 9.0f);
        CHECK_THAT (ConsoleChannel::responseDb (s, 1000.0), WithinAbs (10.0, 0.2));

        s.hmf = 0.0f;
        s.outputDb = -6.0f;
        c.setSettings (s);
        c.reset();
        CHECK_THAT (juce::Decibels::gainToDecibels (sinePeakThrough (c, 1000.0) / 0.25f), WithinAbs (-6.0f, 0.1f));

        // A gate well above the signal closes by its range.
        s.outputDb = 0.0f;
        s.gateThreshold = -3.0f;
        s.gateRange = 30.0f;
        c.setSettings (s);
        c.reset();
        CHECK (juce::Decibels::gainToDecibels (sinePeakThrough (c, 1000.0) / 0.25f) < -25.0f);
    }

    SECTION ("each band boosts its own range")
    {
        s.lf = 12.0f;
        c.setSettings (s);
        const auto lowDb = juce::Decibels::gainToDecibels (sinePeakThrough (c, 40.0) / 0.25f);
        const auto highDb = juce::Decibels::gainToDecibels (sinePeakThrough (c, 6000.0) / 0.25f);
        CHECK (lowDb > 10.0f);
        CHECK (std::abs (highDb) < 1.0f);

        s.lf = 0.0f;
        s.hmf = -12.0f;
        c.setSettings (s);
        c.reset();
        CHECK (juce::Decibels::gainToDecibels (sinePeakThrough (c, ConsoleChannel::hmfFrequency) / 0.25f) < -10.0f);
        CHECK (std::abs (juce::Decibels::gainToDecibels (sinePeakThrough (c, 100.0) / 0.25f)) < 1.5f);
    }

    SECTION ("IN off bypasses everything")
    {
        s.hf = 15.0f;
        s.dynamics = 8.0f;
        s.enabled = false;
        CHECK_FALSE (s.isActive());
        c.setSettings (s);
        CHECK (sinePeakThrough (c, 1000.0) == 0.25f);   // 1 kHz: a sample lands on every peak
    }

    SECTION ("Dynamics compresses loud signals")
    {
        s.dynamics = 10.0f;
        c.setSettings (s);
        const auto loud = sinePeakThrough (c, 200.0, 1.0f);
        CHECK (c.getGainReductionDb() > 10.0f);
        CHECK (loud < 1.0f);
    }

    SECTION ("settings come from the track")
    {
        auto project = model::createProject();
        auto track = model::addTrack (project, "A", nullptr);
        CHECK (ConsoleChannel::fromTrack (track) == ConsoleChannel::Settings{});

        track.setProperty (ids::consoleHF, 40.0, nullptr);      // clamped
        track.setProperty (ids::consoleDynamics, 3.0, nullptr);
        track.setProperty (ids::consoleIn, false, nullptr);
        const auto read = ConsoleChannel::fromTrack (track);
        CHECK (read.hf == ConsoleChannel::maxGainDb);
        CHECK (read.dynamics == 3.0f);
        CHECK_FALSE (read.enabled);
    }
}

TEST_CASE ("The console section runs on a track in the engine", "[console]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("dc.wav"), 48000.0, 48000, 0.5f);
    auto track = model::addTrack (f.project, "A", nullptr);
    model::addRegion (track, wav, 0.0, 1.0, nullptr);

    // A constant (0 Hz) signal: the LF shelf lifts it by its full gain.
    track.setProperty (ids::consoleLF, 6.0, nullptr);
    f.player.setProject (f.project);
    f.player.rebuildNow();

    f.engine.play();
    const auto out = renderLeft (f.engine, 24000);
    CHECK_THAT (out[20000], WithinAbs (0.5 * 0.8 * juce::Decibels::decibelsToGain (6.0), 0.01));
}

TEST_CASE ("A bus compressor plugin on the master replaces the built-in one", "[console-mode]")
{
    Fixture f;
    EffectRack effects { f.rack.getFormatManager() };
    ProjectPlayer player (f.engine, f.cache, &f.rack, &effects);

    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("loud.wav"), 48000.0, 48000, 0.9f);
    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.0, 1.0, nullptr);

    // The built-in compressor is set up to squash hard...
    f.project.setProperty (ids::busCompOn, true, nullptr);
    f.project.setProperty (ids::busCompThreshold, -30.0, nullptr);
    f.project.setProperty (ids::busCompRatio, 2, nullptr);

    // ...but a "plugin" (a gain change standing in for UA's) sits in its place.
    juce::UndoManager um;
    auto insert = model::setBusCompPlugin (f.project, "<PLUGIN name=\"UADx SSL G Bus Compressor\"/>", &um);
    REQUIRE (model::busCompPlugin (f.project) == insert);

    auto standIn = BuiltInEffect::create ("eq");   // a flat EQ (every band at 0 dB) passes the sound unchanged
    for (int p = 0; p < (int) standIn->getInfo().params.size(); ++p)
        if (standIn->getInfo().params[(size_t) p].id.containsIgnoreCase ("gain"))
            standIn->setParameter (p, 0.0f);
    standIn->prepare (48000.0, AudioEngine::maxChunkSize);
    effects.injectForTesting (insert, std::move (standIn), 48000.0);

    player.setProject (f.project);
    player.rebuildNow();
    f.engine.play();
    const auto out = renderLeft (f.engine, 24000);

    // Only the plugin ran: the level is untouched (0.9 * 0.8), not compressed.
    CHECK_THAT (out[20000], WithinAbs (0.72, 0.01));
    const auto [in, outLevel] = f.engine.takeBusPluginLevels();
    CHECK (in > 0.5f);
    CHECK_THAT (outLevel, WithinAbs (in, 0.01));

    // Bypassing the plugin brings the built-in compressor back.
    insert.setProperty (ids::bypass, true, nullptr);
    player.rebuildNow();
    const auto squashed = renderLeft (f.engine, 24000);
    CHECK (std::abs (squashed[20000]) < 0.4f);

    // Swapping in another plugin keeps only one in the place.
    um.beginNewTransaction();
    model::setBusCompPlugin (f.project, "<PLUGIN name=\"UAD SSL G Bus Compressor\"/>", &um);
    CHECK (model::insertsOf (model::findChain (f.project, model::busCompOwner)).size() == 1);
    um.undo();
    CHECK (model::busCompPlugin (f.project)[ids::id] == insert[ids::id]);
}

TEST_CASE ("The console Dynamics has its own IN switch", "[console]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    track.setProperty (ids::consoleDynamics, 10.0, nullptr);
    CHECK (ConsoleChannel::fromTrack (track).isActive());

    track.setProperty (ids::consoleDynIn, false, nullptr);
    const auto s = ConsoleChannel::fromTrack (track);
    CHECK_FALSE (s.dynEnabled);
    CHECK_FALSE (s.isActive());          // nothing else is turned, so the section does nothing

    ConsoleChannel c;
    c.prepare (48000.0);
    c.setSettings (s);
    CHECK (sinePeakThrough (c, 1000.0, 1.0f) == 1.0f);
}

TEST_CASE ("Audio files load in the background without blocking", "[cache]")
{
    TempDir temp;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto wav = writeConstantWav (temp.dir.getChildFile ("song.wav"), 44100.0, 44100 * 4, 0.5f);

    AudioFileCache cache (formats, true);

    // The first request only queues the file.
    CHECK (cache.get (wav) == nullptr);
    CHECK (cache.isLoading());
    CHECK (cache.isPending (wav));

    REQUIRE (cache.waitUntilIdle (30000));
    CHECK_FALSE (cache.isLoading());

    // Now it's there, as it is in the file: 44.1 kHz, both sides.
    const auto audio = cache.get (wav);
    REQUIRE (audio != nullptr);
    CHECK (audio->sampleRate == 44100.0);
    CHECK (audio->buffer.getNumSamples() == 44100 * 4);
    CHECK_THAT (audio->buffer.getSample (0, 1000), WithinAbs (0.5, 1e-4));
    CHECK_THAT (audio->buffer.getSample (1, 1000), WithinAbs (0.5, 1e-4));

    // A file that can't be read doesn't keep retrying.
    const auto junk = temp.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    CHECK (cache.get (junk) == nullptr);
    REQUIRE (cache.waitUntilIdle (30000));
    CHECK (cache.get (junk) == nullptr);
    CHECK_FALSE (cache.isLoading());
}

TEST_CASE ("A file at another sample rate plays at the right pitch and level", "[engine]")
{
    // A 1 kHz tone at 44.1 kHz, played in a 48 kHz session.
    Fixture f;
    juce::AudioBuffer<float> tone (1, 44100 * 2);
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 44100.0));

    const auto file = f.temp.dir.getChildFile ("tone.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    }

    model::addRegion (model::addTrack (f.project, "A", nullptr), file, 0.0, 2.0, nullptr);
    f.player.setProject (f.project);
    f.engine.play();
    const auto out = renderLeft (f.engine, 48000);

    // Compare with a perfect 1 kHz tone at 48 kHz (same level after the 0.8 master).
    double error = 0.0, energy = 0.0;
    for (int i = 4800; i < 43200; ++i)
    {
        const auto ideal = 0.5 * 0.8 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0);
        error += (out[(size_t) i] - ideal) * (out[(size_t) i] - ideal);
        energy += ideal * ideal;
    }

    const auto snrDb = 10.0 * std::log10 (energy / juce::jmax (error, 1e-20));
    INFO ("signal to error: " << snrDb << " dB");
    CHECK (snrDb > 70.0);
}

TEST_CASE ("Changing the session's sample rate doesn't reload audio", "[engine]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    model::addRegion (model::addTrack (f.project, "A", nullptr), wav, 0.0, 1.0, nullptr);
    f.player.setProject (f.project);

    const auto first = f.cache.get (wav);
    f.engine.prepare (44100.0, 512);
    f.player.rebuildNow();
    CHECK (f.cache.get (wav) == first);

    // ...and it still plays at the right level, converted.
    f.engine.play();
    const auto out = renderLeft (f.engine, 20000);
    CHECK_THAT (out[10000], WithinAbs (0.5 * 0.8, 0.005));
}

TEST_CASE ("Console mode puts a channel strip plugin on every track", "[console-mode]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", &um);
    auto b = model::addTrack (project, "B", &um);
    b.setProperty (ids::builtInStrip, true, nullptr);   // this one keeps the built-in strip
    const juce::String uad = "<PLUGIN name=\"UADx SSL 4000 E Channel Strip\"/>";

    um.beginNewTransaction();
    model::setConsolePlugin (project, uad, &um);
    CHECK (model::stripPlugin (project, a[ids::id].toString()).isValid());
    CHECK_FALSE (model::stripPlugin (project, b[ids::id].toString()).isValid());

    // New tracks get it too, in the same undo step as the track.
    um.beginNewTransaction();
    auto c = model::addTrack (project, "C", &um);
    CHECK (model::stripPlugin (project, c[ids::id].toString())[ids::pluginDescription].toString() == uad);
    um.undo();
    CHECK_FALSE (model::stripPlugin (project, c[ids::id].toString()).isValid());

    // Back to the built-in strip everywhere.
    um.beginNewTransaction();
    model::setConsolePlugin (project, {}, &um);
    CHECK_FALSE (model::stripPlugin (project, a[ids::id].toString()).isValid());
    CHECK (model::consolePlugin (project).isEmpty());
    um.undo();
    CHECK (model::stripPlugin (project, a[ids::id].toString()).isValid());

    // Saved and loaded with the project.
    TempDir temp;
    const auto file = temp.dir.getChildFile ("console.atp");
    juce::String error;
    REQUIRE (model::save (project, file, error));
    const auto loaded = model::load (file, error);
    CHECK (model::consolePlugin (loaded) == uad);
    CHECK (model::stripPlugin (loaded, a[ids::id].toString()).isValid());
}

TEST_CASE ("A track's channel strip plugin replaces the built-in strip", "[console-mode]")
{
    Fixture f;
    EffectRack effects { f.rack.getFormatManager() };
    ProjectPlayer player (f.engine, f.cache, &f.rack, &effects);

    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("dc.wav"), 48000.0, 48000, 0.5f);
    auto track = model::addTrack (f.project, "A", nullptr);
    model::addRegion (track, wav, 0.0, 1.0, nullptr);
    track.setProperty (ids::consoleLF, 12.0, nullptr);   // the built-in strip would lift a constant signal a lot

    auto insert = model::setStripPlugin (f.project, track[ids::id].toString(), "<PLUGIN name=\"UADx SSL 4000 E Channel Strip\"/>", nullptr);
    auto standIn = BuiltInEffect::create ("eq");          // flat: passes the sound unchanged
    standIn->prepare (48000.0, AudioEngine::maxChunkSize);
    effects.injectForTesting (insert, std::move (standIn), 48000.0);

    player.setProject (f.project);
    f.engine.play();
    auto out = renderLeft (f.engine, 24000);
    CHECK_THAT (out[20000], WithinAbs (0.5 * 0.8, 0.005));   // the plugin, not the built-in +12 dB

    // Taking the plugin off brings the built-in strip back.
    model::setStripPlugin (f.project, track[ids::id].toString(), {}, nullptr);
    player.rebuildNow();
    out = renderLeft (f.engine, 24000);
    CHECK (out[20000] > 0.5f * 0.8f * 2.0f);
}

TEST_CASE ("Exporting stems writes each track to its own file, ignoring solo", "[engine][export]")
{
    Fixture f;
    const auto wavA = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    const auto wavB = writeConstantWav (f.temp.dir.getChildFile ("b.wav"), 48000.0, 48000, 0.25f);
    auto a = model::addTrack (f.project, "A", nullptr);
    auto b = model::addTrack (f.project, "B", nullptr);
    model::addRegion (a, wavA, 0.0, 1.0, nullptr);
    model::addRegion (b, wavB, 1.0, 1.0, nullptr);
    a.setProperty (ids::solo, true, nullptr);
    f.player.setIgnoreSolo (true);
    f.player.setProject (f.project);

    const auto fileA = f.temp.dir.getChildFile ("Stems/A.wav");
    const auto fileB = f.temp.dir.getChildFile ("Stems/B.wav");
    f.engine.beginOfflineRender();
    const auto error = f.engine.renderStems ({ { f.player.slotOf (a[ids::id]), fileA }, { f.player.slotOf (b[ids::id]), fileB } },
                                            0.0, 2.0, 24, nullptr);
    f.engine.endOfflineRender();
    REQUIRE (error.isEmpty());

    const auto read = [&f] (const juce::File& file)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader> (f.engine.getFormatManager().createReaderFor (file));
        juce::AudioBuffer<float> data (2, 96000);
        data.clear();
        if (reader != nullptr)
            reader->read (&data, 0, (int) reader->lengthInSamples, 0, true, true);
        return data;
    };

    const auto dataA = read (fileA);
    const auto dataB = read (fileB);
    CHECK_THAT (dataA.getSample (0, 24000), WithinAbs (0.5f, 1e-4));   // before the master fader
    CHECK (dataA.getMagnitude (0, 49000, 47000) == 0.0f);
    CHECK (dataB.getMagnitude (0, 0, 47000) == 0.0f);
    CHECK_THAT (dataB.getSample (1, 72000), WithinAbs (0.25f, 1e-4));   // B plays although A is soloed
}

TEST_CASE ("Importing tracks from another project brings their regions, plugins, sends and automation", "[model]")
{
    auto source = model::createProject ("Other");
    auto bus = model::addTrack (source, "Verb", nullptr, -1, model::busKind);
    auto vocal = model::addTrack (source, "Vocal", nullptr);
    auto guitar = model::addTrack (source, "Guitar", nullptr);
    model::addRegion (vocal, juce::File ("/tmp/v.wav"), 1.0, 2.0, nullptr);
    auto chain = model::chainFor (source, vocal[ids::id]);
    auto limiter = model::addBuiltinInsert (chain, "limiter", 0, nullptr);
    model::addSend (chain, bus[ids::id], 0.5, nullptr);
    model::addPoint (model::laneFor (source, vocal[ids::id], model::insertParam (limiter[ids::id], "ceiling"), true), 0.0, -3.0, nullptr);
    model::addPoint (model::laneFor (source, vocal[ids::id], model::volumeParam, true), 0.0, 0.5, nullptr);
    juce::ignoreUnused (guitar);

    auto project = model::createProject ("Mine");
    model::addTrack (project, "Drums", nullptr);
    juce::UndoManager um;

    // Only the vocal: its send to the bus that stayed behind is dropped.
    auto added = model::importTracks (project, source, { vocal[ids::id].toString() }, &um);
    REQUIRE (added.size() == 1);
    CHECK (model::numTracks (project) == 2);
    const auto copy = added[0];
    CHECK (copy[ids::name].toString() == "Vocal");
    CHECK (copy[ids::id].toString() != vocal[ids::id].toString());
    CHECK (copy.getNumChildren() == 1);
    CHECK (copy.getChild (0)[ids::id].toString() != vocal.getChild (0)[ids::id].toString());

    const auto newChain = model::findChain (project, copy[ids::id]);
    REQUIRE (newChain.isValid());
    const auto inserts = model::insertsOf (newChain);
    REQUIRE (inserts.size() == 1);
    CHECK (inserts[0][ids::id].toString() != limiter[ids::id].toString());
    CHECK (model::sendsOf (newChain).isEmpty());
    CHECK (model::laneFor (project, copy[ids::id], model::volumeParam, false).isValid());
    CHECK (model::laneFor (project, copy[ids::id], model::insertParam (inserts[0][ids::id], "ceiling"), false).isValid());

    um.undo();
    CHECK (model::numTracks (project) == 1);

    // With the bus too, the send comes along, pointing at the new bus.
    added = model::importTracks (project, source, { bus[ids::id].toString(), vocal[ids::id].toString() }, nullptr);
    REQUIRE (added.size() == 2);
    const auto sends = model::sendsOf (model::findChain (project, added[1][ids::id]));
    REQUIRE (sends.size() == 1);
    CHECK (sends[0][ids::target].toString() == added[0][ids::id].toString());
}

TEST_CASE ("Fades shape a region, and a looped region repeats", "[engine][fades]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    auto track = model::addTrack (f.project, "A", nullptr);
    auto region = model::addRegion (track, wav, 0.0, 0.5, nullptr);   // 24000 samples
    model::setFade (region, true, 0.1, nullptr);    // 4800 samples, straight
    model::setFade (region, false, 0.1, nullptr);
    f.player.setProject (f.project);
    f.engine.setPlayheadSeconds (0.0);
    f.engine.play();

    auto out = renderLeft (f.engine, 48000);
    CHECK_THAT (out[0], WithinAbs (0.0f, 1e-4));
    CHECK_THAT (out[2400], WithinAbs (0.5f * 0.8f * 0.5f, 2e-3));      // halfway up
    CHECK_THAT (out[12000], WithinAbs (0.5f * 0.8f, 1e-4));            // full in the middle
    CHECK_THAT (out[24000 - 2400], WithinAbs (0.5f * 0.8f * 0.5f, 2e-3));
    CHECK (std::abs (out[30000]) < 1e-6f);

    // Looping (no fades): it plays again from 0.5 s, up to the loop end at 0.9 s.
    region.setProperty (ids::fadeIn, 0.0, nullptr);
    region.setProperty (ids::fadeOut, 0.0, nullptr);
    region.setProperty (ids::loop, true, nullptr);
    region.setProperty (ids::loopEnd, 0.9, nullptr);
    f.player.rebuildNow();
    f.engine.setPlayheadSeconds (0.0);
    out = renderLeft (f.engine, 48000);
    CHECK_THAT (out[30000], WithinAbs (0.5f * 0.8f, 1e-4));
    CHECK_THAT (out[43000], WithinAbs (0.5f * 0.8f, 1e-4));
    CHECK (std::abs (out[43300]) < 1e-6f);
    CHECK_THAT (model::projectEnd (f.project), WithinAbs (0.9, 1e-9));
}

TEST_CASE ("Crossfades, shuffle and loops in the model", "[model][fades]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "A", nullptr);
    auto a = model::addRegion (track, juce::File ("/tmp/a.wav"), 0.0, 2.0, nullptr);
    auto b = model::addRegion (track, juce::File ("/tmp/b.wav"), 1.5, 2.0, nullptr);

    model::createCrossfades (b, nullptr);
    CHECK_THAT ((double) a[ids::fadeOut], WithinAbs (0.5, 1e-9));
    CHECK_THAT ((double) b[ids::fadeIn], WithinAbs (0.5, 1e-9));
    CHECK_THAT ((double) b[ids::fadeInCurve], WithinAbs (0.5, 1e-9));
    // Equal power: the two halves add up to the same power all through the crossfade.
    for (double x : { 0.1, 0.3, 0.5, 0.9 })
        CHECK_THAT (std::pow (model::fadeGain (x, 0.5), 2.0) + std::pow (model::fadeGain (1.0 - x, 0.5), 2.0), WithinAbs (1.0, 1e-9));

    // Trimming keeps the fades inside the region.
    model::trimRegionEnd (a, 0.3, 10.0, nullptr);
    CHECK ((double) a[ids::fadeOut] <= 0.3 + 1e-9);

    // Shuffle left closes the gaps.
    auto c = model::addRegion (track, juce::File ("/tmp/c.wav"), 8.0, 1.0, nullptr);
    model::shuffleTrack (track, true, nullptr);
    CHECK_THAT ((double) b[ids::start], WithinAbs (0.3, 1e-9));
    CHECK_THAT ((double) c[ids::start], WithinAbs (2.3, 1e-9));

    // Looping goes up to the next region.
    model::setLoop (b, true, nullptr);
    CHECK_THAT (model::loopEndOf (b), WithinAbs (2.3, 1e-9));
    c.setProperty (ids::start, 20.0, nullptr);
    CHECK_THAT (model::loopEndOf (b), WithinAbs (2.3, 1e-9));   // the loop end set when it started looping
    model::setLoop (b, false, nullptr);
    CHECK_THAT (model::loopEndOf (b), WithinAbs (2.3, 1e-9));
}

TEST_CASE ("Controller events reach the instrument at their time", "[engine][midi]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (track, 1.0, 2.0, nullptr);
    region.setProperty (ids::offset, 0.5, nullptr);
    model::addControllerEvent (region, 1, 0.75, 1.0, nullptr);            // at 1.25 s in the song
    model::addControllerEvent (region, model::pitchBend, 0.25, 0.5, nullptr);   // before the window: left out

    Fixture f;
    f.player.setProject (project);
    auto graph = f.player.buildGraph (48000.0);
    REQUIRE (graph->tracks.size() == 1);
    REQUIRE (graph->tracks[0].regions.size() == 1);
    const auto& events = graph->tracks[0].regions[0].events;
    REQUIRE (events.size() == 1);
    CHECK (events[0].time == 60000);
    CHECK (events[0].status == 0xb0);
    CHECK (events[0].data1 == 1);
    CHECK (events[0].data2 == 127);
}

TEST_CASE ("VCAs, outputs into a bus, stacks and freezing", "[engine][mixer]")
{
    Fixture f;
    const auto wav = writeConstantWav (f.temp.dir.getChildFile ("a.wav"), 48000.0, 48000, 0.5f);
    auto a = model::addTrack (f.project, "A", nullptr);
    model::addRegion (a, wav, 0.0, 1.0, nullptr);
    f.project.setProperty (ids::masterGain, 1.0, nullptr);

    // A VCA at -6 dB turns A down; muting it mutes A.
    auto vca = model::addTrack (f.project, "VCA", nullptr, -1, model::vcaKind);
    vca.setProperty (ids::volume, 0.5, nullptr);
    a.setProperty (ids::vca, vca[ids::id].toString(), nullptr);
    f.player.setProject (f.project);
    f.engine.setPlayheadSeconds (0.0);
    f.engine.play();
    auto out = renderLeft (f.engine, 4800);
    CHECK_THAT (out[4000], WithinAbs (0.25f, 1e-3));

    vca.setProperty (ids::mute, true, nullptr);
    f.player.rebuildNow();
    out = renderLeft (f.engine, 9600);
    CHECK (std::abs (out[9000]) < 1e-4f);
    vca.setProperty (ids::mute, false, nullptr);
    a.removeProperty (ids::vca, nullptr);

    // Output into a bus: the bus's fader applies.
    auto bus = model::addTrack (f.project, "Bus", nullptr, -1, model::busKind);
    bus.setProperty (ids::volume, 0.5, nullptr);
    a.setProperty (ids::output, bus[ids::id].toString(), nullptr);
    f.player.rebuildNow();
    out = renderLeft (f.engine, 9600);
    CHECK_THAT (out[9000], WithinAbs (0.25f, 1e-3));
    a.removeProperty (ids::output, nullptr);

    // A summing stack is a bus the tracks play into, with them under it.
    auto stack = model::createTrackStack (f.project, { a }, true, nullptr);
    CHECK (model::isBusTrack (stack));
    CHECK (a[ids::output].toString() == stack[ids::id].toString());
    CHECK (f.project.indexOf (a) == f.project.indexOf (stack) + 1);
    stack.setProperty (ids::collapsed, true, nullptr);
    CHECK (model::isInCollapsedStack (f.project, a));
    model::flattenStack (f.project, stack, nullptr);
    CHECK (a[ids::output].toString().isEmpty());

    // Freezing renders what comes out of the plugins, before the fader.
    a.setProperty (ids::volume, 0.5, nullptr);
    f.player.rebuildNow();
    const auto frozen = f.temp.dir.getChildFile ("Freeze.wav");
    f.engine.beginOfflineRender();
    REQUIRE (f.engine.renderStems ({ { f.player.slotOf (a[ids::id]), frozen } }, 0.0, 1.0, 24, nullptr, true).isEmpty());
    f.engine.endOfflineRender();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (f.engine.getFormatManager().createReaderFor (frozen));
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> data (2, 48000);
    reader->read (&data, 0, 48000, 0, true, true);
    CHECK_THAT (data.getSample (0, 24000), WithinAbs (0.5f, 1e-4));   // the fader isn't in it

    a.setProperty (ids::frozenFile, frozen.getFullPathName(), nullptr);
    a.setProperty (ids::frozen, true, nullptr);
    f.player.rebuildNow();
    f.engine.setPlayheadSeconds (0.0);
    out = renderLeft (f.engine, 4800);
    CHECK_THAT (out[4000], WithinAbs (0.25f, 1e-3));   // the frozen file, through the fader
}

TEST_CASE ("Groups link tracks", "[model][mixer]")
{
    auto project = model::createProject();
    auto a = model::addTrack (project, "A", nullptr);
    auto b = model::addTrack (project, "B", nullptr);
    auto c = model::addTrack (project, "C", nullptr);
    CHECK (model::groupMembers (project, a).size() == 1);
    a.setProperty (ids::group, 2, nullptr);
    c.setProperty (ids::group, 2, nullptr);
    const auto members = model::groupMembers (project, a);
    CHECK (members.size() == 2);
    CHECK (members.contains (c));
    CHECK_FALSE (members.contains (b));
}

TEST_CASE ("The metronome follows the tempo track", "[engine][tempo]")
{
    Fixture f;
    f.project.setProperty (ids::tempo, 120.0, nullptr);
    f.project.setProperty (ids::metronome, true, nullptr);
    model::addTempoChange (f.project, 2.0, 60.0, nullptr);   // beats at 0, 0.5, then 1.0, 2.0 s
    f.player.setProject (f.project);
    f.engine.setPlayheadSeconds (0.0);
    f.engine.play();
    const auto out = renderLeft (f.engine, 48000 * 3);

    // Clicks start where the beats are: 0, 0.5, 1.0 and 2.0 s, and not at 1.5 s.
    const auto loudNear = [&out] (int sample) { float m = 0.0f; for (int i = sample; i < sample + 200; ++i) m = juce::jmax (m, std::abs (out[(size_t) i])); return m; };
    CHECK (loudNear (24000) > 0.05f);
    CHECK (loudNear (48000) > 0.05f);
    CHECK (loudNear (72000) < 0.001f);
    CHECK (loudNear (96000) > 0.05f);
}

TEST_CASE ("Region and track transpose and velocity apply as MIDI plays", "[engine][midi]")
{
    auto project = model::createProject();
    auto track = model::addTrack (project, "Keys", nullptr, -1, model::instrumentKind);
    auto region = model::addMidiRegion (track, 0.0, 2.0, nullptr);
    model::addNote (region, 60, 0.0, 0.5, 100, nullptr);
    region.setProperty (ids::transpose, 12, nullptr);
    track.setProperty (ids::transpose, -2, nullptr);
    region.setProperty (ids::velocityOffset, 40, nullptr);

    Fixture f;
    f.player.setProject (project);
    auto graph = f.player.buildGraph (48000.0);
    REQUIRE (graph->tracks.size() == 1);
    REQUIRE (graph->tracks[0].regions.size() == 1);
    const auto& note = graph->tracks[0].regions[0].notes.at (0);
    CHECK (note.pitch == 70);
    CHECK (note.velocity == 127);   // capped
}

TEST_CASE ("Comping: swiped stretches of takes play, and flatten into regions", "[engine][comp]")
{
    Fixture f;
    // One file with two passes: 0.25 for the first second, 0.75 for the next.
    const auto file = f.temp.dir.getChildFile ("takes.wav");
    {
        juce::AudioBuffer<float> b (2, 96000);
        for (int i = 0; i < 96000; ++i)
            b.setSample (0, i, i < 48000 ? 0.25f : 0.75f), b.setSample (1, i, i < 48000 ? 0.25f : 0.75f);
        std::unique_ptr<juce::OutputStream> out (new juce::FileOutputStream (file));
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 96000);
    }

    auto track = model::addTrack (f.project, "Vox", nullptr);
    auto folder = model::addTakeFolder (track, file, 0.0, 1.0, { 0.0, 1.0 }, 0, nullptr);
    model::compSwipe (folder, 1, 0.5, 0.8, nullptr);   // take 2 from 0.5 to 0.8 s
    const auto pieces = model::compPieces (folder);
    REQUIRE (pieces.size() == 3);
    CHECK (pieces[1].take == 1);

    f.project.setProperty (ids::masterGain, 1.0, nullptr);
    f.player.setProject (f.project);
    f.engine.setPlayheadSeconds (0.0);
    f.engine.play();
    const auto out = renderLeft (f.engine, 48000);
    CHECK_THAT (out[12000], WithinAbs (0.25f, 1e-3));
    CHECK_THAT (out[31000], WithinAbs (0.75f, 1e-3));   // the swiped stretch
    CHECK_THAT (out[44000], WithinAbs (0.25f, 1e-3));

    // Swiping over part of a stretch shortens it.
    model::compSwipe (folder, 0, 0.7, 1.0, nullptr);
    CHECK_THAT (model::compPieces (folder)[1].to, WithinAbs (0.7, 1e-9));

    const auto regions = model::flattenComp (folder, nullptr);
    CHECK (regions.size() == 3);
    CHECK (track.getNumChildren() == 3);
    CHECK_THAT ((double) regions[1][ids::offset], WithinAbs (1.5, 1e-9));
}

#include "../Source/engine/BuiltInInstruments.h"

TEST_CASE ("The built-in Drum Kit and Sampler make sound", "[engine][instruments]")
{
    DrumKit kit;
    kit.prepare (48000.0, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 0);
    midi.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 100), 100);
    std::vector<float> l (4800), r (4800);
    kit.render (l.data(), r.data(), 4800, midi);
    float peak = 0.0f;
    for (auto v : l) peak = juce::jmax (peak, std::abs (v));
    CHECK (peak > 0.1f);
    CHECK (DrumKit::pieceName (38) == "Snare");

    Sampler sampler;
    sampler.prepare (48000.0, 512);
    auto sound = std::make_shared<juce::AudioBuffer<float>> (1, 48000);
    for (int i = 0; i < 48000; ++i)
        sound->setSample (0, i, 0.5f * (float) std::sin (i * 0.05));
    sampler.setSample (sound, 48000.0);
    sampler.setSettings (60, 0.001f, 0.1f, 1.0f);
    juce::MidiBuffer notes;
    notes.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 127), 0);   // an octave up: twice as fast
    std::vector<float> sl (2000), sr (2000);
    sampler.render (sl.data(), sr.data(), 2000, notes);
    CHECK_THAT (sl[1000], WithinAbs (0.5f * (float) std::sin (2000 * 0.05), 0.02f));
}

TEST_CASE ("Live Loops cells loop in place of the track's timeline, from their bar until stopped", "[engine][liveloops]")
{
    Fixture f;
    const auto loopWav = writeConstantWav (f.temp.dir.getChildFile ("loop.wav"), 48000.0, 4800, 0.5f);
    const auto otherWav = writeConstantWav (f.temp.dir.getChildFile ("other.wav"), 48000.0, 48000, 0.25f);

    auto track = model::addTrack (f.project, "A", nullptr);
    auto region = model::addRegion (track, loopWav, 0.0, 0.05, nullptr);   // 2400 samples of sound
    model::addRegion (track, otherWav, 0.5, 0.5, nullptr);
    const auto cell = model::setLiveCell (f.project, track[ids::id].toString(), 0, region, nullptr);
    REQUIRE (cell.isValid());
    model::captureScene (f.project, 3, 0.4, 1.2, nullptr);
    f.player.setProject (f.project);

    // Starts at 0.1 s and stops at 0.3 s; the timeline's region at 0.5 s stays quiet meanwhile.
    f.player.setLiveCell (track[ids::id].toString(), { cell[ids::id].toString(), 0.1, 0.0 });
    f.player.stopLiveCell (track[ids::id].toString(), 0.3);
    f.engine.play();
    const auto out = renderLeft (f.engine, 36000);
    const float expected = 0.5f * 0.8f;

    CHECK (out[4799] == 0.0f);
    CHECK_THAT (out[4800], WithinAbs (expected, 1e-4));
    CHECK_THAT (out[4800 + 2400 + 100], WithinAbs (expected, 1e-4));   // second pass of the loop
    CHECK_THAT (out[14399], WithinAbs (expected, 1e-4));
    CHECK (out[14400] == 0.0f);
    CHECK (out[30000] == 0.0f);   // not the timeline

    // When it's cleared, the timeline plays again.
    f.player.clearLiveCells();
    f.engine.setPlayheadSeconds (0.6);
    const auto after = renderLeft (f.engine, 1000);
    CHECK_THAT (after[500], WithinAbs (0.25f * 0.8f, 1e-4));

    // Cycle to scene took the region that starts in 0.4..1.2, trimmed to the stretch.
    const auto captured = model::liveCell (f.project, track[ids::id].toString(), 3);
    REQUIRE (captured.isValid());
    CHECK_THAT ((double) captured.getChild (0)[ids::length], WithinAbs (0.5, 1e-9));
    CHECK ((double) captured.getChild (0)[ids::start] == 0.0);

    // Replacing and clearing a cell.
    model::setLiveCell (f.project, track[ids::id].toString(), 0, {}, nullptr);
    CHECK (! model::liveCell (f.project, track[ids::id].toString(), 0).isValid());
}

TEST_CASE ("Long files stream from disk and play the same as from memory", "[engine][streaming]")
{
    for (const double fileRate : { 48000.0, 44100.0 })
    {
        Fixture f;
        f.cache.setStreamingThreshold (1024);   // stream everything bigger than 1 KB
        const auto wav = writeConstantWav (f.temp.dir.getChildFile ("long.wav"), fileRate, 96000, 0.5f);

        const auto audio = f.cache.get (wav);
        REQUIRE (audio != nullptr);
        CHECK (audio->isStreamed());
        CHECK (audio->numFrames() == 96000);
        CHECK (audio->buffer.getNumSamples() == 0);   // nothing decoded into memory

        // Reads past either end are silent.
        std::vector<float> l (16), r (16);
        audio->readStereo (-8, 16, l.data(), r.data());
        CHECK (l[7] == 0.0f);
        CHECK_THAT (l[8], WithinAbs (0.5, 1e-4));
        CHECK_THAT (r[8], WithinAbs (0.5, 1e-4));   // mono plays in both sides
        audio->readStereo (96000 - 8, 16, l.data(), r.data());
        CHECK_THAT (l[7], WithinAbs (0.5, 1e-4));
        CHECK (l[8] == 0.0f);

        auto track = model::addTrack (f.project, "A", nullptr);
        model::addRegion (track, wav, 0.01, 1.0, nullptr);
        f.player.setProject (f.project);
        f.player.getReadAhead().readAhead (0.0, 2.0);

        f.engine.play();
        const auto out = renderLeft (f.engine, 60000);
        const float expected = 0.5f * 0.8f;
        CHECK (out[479] == 0.0f);
        CHECK_THAT (out[2000], WithinAbs (expected, 2e-3));
        CHECK_THAT (out[40000], WithinAbs (expected, 2e-3));
        CHECK_THAT (out[48400], WithinAbs (expected, 2e-3));
        CHECK (std::abs (out[49000]) < 1e-3f);   // ends after 1 s
    }
}
