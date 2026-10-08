#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "model/Project.h"
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"

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
        ProjectPlayer player { engine, cache };
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
