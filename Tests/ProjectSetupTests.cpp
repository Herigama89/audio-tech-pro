#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "engine/Wav64.h"
#include "model/Project.h"
#include "model/StarterTemplates.h"
#include "model/UnusedAudio.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-setup-" + juce::Uuid().toString());
        TempDir()  { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    juce::File write (const juce::File& f, const juce::String& type, int bits, double rate = 48000.0, int frames = 4800)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
        auto w = createAudioWriter (type, bits, rate, 2, stream);
        REQUIRE (w != nullptr);
        juce::AudioBuffer<float> b (2, frames);
        for (int i = 0; i < frames; ++i)
        {
            b.setSample (0, i, 0.5f * std::sin (0.01f * (float) i));
            b.setSample (1, i, -0.25f);
        }
        REQUIRE (w->writeFromAudioSampleBuffer (b, 0, frames));
        return f;
    }
}

TEST_CASE ("Recordings in every bit depth and file type read back", "[setup]")
{
    TempDir t;
    juce::AudioFormatManager formats;
    registerAudioFormats (formats);
    struct Case { const char* type; int bits; float tolerance; };
    for (const auto& c : { Case { "wav", 16, 1.0e-4f }, Case { "wav", 24, 1.0e-6f }, Case { "wav", 32, 1.0e-7f }, Case { "wav", 64, 1.0e-7f },
                           Case { "aiff", 24, 1.0e-6f }, Case { "flac", 24, 1.0e-6f }, Case { "flac", 16, 1.0e-4f } })
    {
        INFO (c.type << " " << c.bits);
        const auto f = write (t.dir.getChildFile (juce::String ("take ") + c.type + juce::String (c.bits) + extensionForFileType (c.type)), c.type, c.bits, 96000.0);
        std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) };
        REQUIRE (r != nullptr);
        CHECK (r->lengthInSamples == 4800);
        CHECK (r->numChannels == 2);
        CHECK_THAT (r->sampleRate, WithinAbs (96000.0, 0.5));
        juce::AudioBuffer<float> b (2, 4800);
        r->read (&b, 0, 4800, 0, true, true);
        float worst = 0.0f;
        for (int i = 0; i < 4800; ++i)
            worst = std::max ({ worst, std::abs (b.getSample (0, i) - 0.5f * std::sin (0.01f * (float) i)), std::abs (b.getSample (1, i) + 0.25f) });
        CHECK (worst < c.tolerance);
    }
}

TEST_CASE ("Every starter makes a project, its buses after its tracks", "[setup]")
{
    for (const auto& s : starters::all())
    {
        INFO (s.id);
        const auto p = starters::build (s.id, "Song");
        CHECK (p[ids::name].toString() == "Song");
        int tracks = 0;
        bool busSeen = false, trackAfterBus = false;
        for (const auto& t : p)
            if (t.hasType (ids::TRACK))
            {
                ++tracks;
                const bool bus = t[ids::kind].toString() == model::busKind;
                trackAfterBus = trackAfterBus || (busSeen && ! bus);
                busSeen = busSeen || bus;
            }
        CHECK (tracks >= (int) (s.id == "empty" ? 0 : 1));
        CHECK (! trackAfterBus);
    }
    CHECK (starters::categories().size() >= 4);
}

TEST_CASE ("Delete Unused Audio finds only what nothing uses, and keeps what an alternative uses", "[setup]")
{
    TempDir t;
    juce::AudioFormatManager formats;
    registerAudioFormats (formats);
    auto audio = t.dir.getChildFile ("Audio Files");
    audio.getChildFile ("Freeze Files").createDirectory();
    audio.getChildFile ("Stems").createDirectory();
    const auto used = write (audio.getChildFile ("Vox#01.wav"), "wav", 24);
    const auto take = write (audio.getChildFile ("Vox#02.wav"), "wav", 24);
    const auto oldTake = write (audio.getChildFile ("Vox#03.wav"), "wav", 24);
    const auto freeze = write (audio.getChildFile ("Freeze Files").getChildFile ("Pad Freeze.wav"), "wav", 24);
    const auto alt = write (audio.getChildFile ("Room.wav"), "wav", 24);
    write (audio.getChildFile ("Stems").getChildFile ("Drums.wav"), "wav", 24);   // (exports: never listed)
    const auto sample = write (audio.getChildFile ("Kick.wav"), "wav", 24);

    auto project = model::createProject();
    auto track = model::addTrack (project, "Vox", nullptr);
    auto region = model::addRegion (track, used, 0.0, 0.1, nullptr);
    juce::ValueTree takeTree ("TAKE");
    takeTree.setProperty (ids::file, take.getFullPathName(), nullptr);
    region.appendChild (takeTree, nullptr);
    juce::ValueTree studio ("STUDIO");
    studio.setProperty ("file0", sample.getFullPathName(), nullptr);   // (an instrument's sample is used too)
    model::addTrack (project, "Drums", nullptr).appendChild (studio, nullptr);
    auto other = model::createProject();
    model::addRegion (model::addTrack (other, "Room", nullptr), alt, 0.0, 0.1, nullptr);

    const auto items = unusedaudio::find (project, audio, { { "Mix B", other } }, formats);
    juce::StringArray names;
    for (const auto& i : items)
        names.add (i.file.getFileName() + (i.keptBy.isNotEmpty() ? "*" : ""));
    INFO (names.joinIntoString (", "));
    REQUIRE (items.size() == 3);
    CHECK (names.contains ("Vox#03.wav"));
    CHECK (names.contains ("Pad Freeze.wav"));
    CHECK (names.contains ("Room.wav*"));
    for (const auto& i : items)
    {
        if (i.file == oldTake) CHECK (i.kind == unusedaudio::Kind::recording);
        if (i.file == freeze)  CHECK (i.kind == unusedaudio::Kind::render);
        CHECK_THAT (i.seconds, WithinAbs (0.1, 0.001));
    }

    juce::String error;
    CHECK (unusedaudio::remove ({ oldTake }, false, audio, error) == 1);
    CHECK (! oldTake.existsAsFile());
    CHECK (unusedaudio::deletedFolder (audio).getChildFile ("Vox#03.wav").existsAsFile());
    CHECK (unusedaudio::find (project, audio, { { "Mix B", other } }, formats).size() == 2);   // (Deleted Audio isn't listed)
}
