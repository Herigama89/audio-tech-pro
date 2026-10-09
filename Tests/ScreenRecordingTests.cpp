#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "engine/ScreenAudioTap.h"
#include "engine/AudioEngine.h"
#include "ui/ScreenRecorder.h"
#include "ui/ScreenRecordPanel.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Screen audio tap: the mix in order, with the mic mixed in for the video", "[screen]")
{
    ScreenAudioTap tap (1000);
    std::vector<float> l (300), r (300), mic1 (300, 0.25f), mic2 (300, -0.5f);
    for (int i = 0; i < 300; ++i)
    {
        l[(size_t) i] = (float) i;
        r[(size_t) i] = (float) -i;
    }
    const float* inputs[] { mic1.data(), mic2.data() };

    // No mic: just the mix.
    tap.push (l.data(), r.data(), 100, inputs, 2, 0);
    // A mono mic on input 2, at half level, from sample 100 of the inputs.
    tap.setMic (1, false, 0.5f);
    tap.push (l.data() + 100, r.data() + 100, 100, inputs, 2, 100);
    // A stereo pair 1+2.
    tap.setMic (0, true, 1.0f);
    tap.push (l.data() + 200, r.data() + 200, 100, inputs, 2, 200);

    REQUIRE (tap.getNumReady() == 300);
    std::vector<float> outL (300), outR (300);
    CHECK (tap.pull (outL.data(), outR.data(), 300) == 300);

    CHECK (outL[50] == 50.0f);
    CHECK (outR[50] == -50.0f);
    CHECK_THAT (outL[150], WithinAbs (150.0 - 0.25, 1e-6));    // mic 2 (-0.5) at half level, both sides
    CHECK_THAT (outR[150], WithinAbs (-150.0 - 0.25, 1e-6));
    CHECK_THAT (outL[250], WithinAbs (250.0 + 0.25, 1e-6));    // stereo: input 1 left, input 2 right
    CHECK_THAT (outR[250], WithinAbs (-250.0 - 0.5, 1e-6));

    // A missing input is ignored rather than read.
    tap.setMic (5, false, 1.0f);
    tap.push (l.data(), r.data(), 10, inputs, 2, 0);
    CHECK (tap.pull (outL.data(), outR.data(), 10) == 10);
    CHECK (outL[3] == 3.0f);

    // When the recorder falls behind, the newest audio is dropped and counted.
    tap.setMic (-1, false, 1.0f);
    for (int i = 0; i < 4; ++i)
        tap.push (l.data(), r.data(), 300, nullptr, 0, 0);
    CHECK (tap.getNumReady() == 999);
    CHECK (tap.getNumDropped() == 1200 - 999);
    tap.reset();
    CHECK (tap.getNumReady() == 0);
    CHECK (tap.getNumDropped() == 0);
}

TEST_CASE ("The engine feeds the screen tap with exactly what goes out", "[screen][engine]")
{
    AudioEngine engine;
    engine.prepare (48000.0, 512);
    ScreenAudioTap tap;
    engine.setScreenAudioTap (&tap);

    juce::AudioBuffer<float> in (2, 512), out (2, 512);
    in.clear();
    for (int i = 0; i < 512; ++i)
        in.setSample (0, i, 0.1f);

    engine.render (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 512);
    CHECK (tap.getNumReady() == 512);

    tap.setMic (0, false, 1.0f);
    engine.render (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 512);
    std::vector<float> l (1024), r (1024);
    CHECK (tap.pull (l.data(), r.data(), 1024) == 1024);
    CHECK_THAT (l[600], WithinAbs (out.getSample (0, 88) + 0.1f, 1e-6));   // the (silent) mix plus the mic

    engine.setScreenAudioTap (nullptr);
    engine.render (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 512);
    CHECK (tap.getNumReady() == 0);
}

TEST_CASE ("Screen recording sizes and settings", "[screen]")
{
    // A 5K Retina display, limited to 4K: same shape, even numbers.
    CHECK (ScreenRecorder::fitSize (5120, 2880, 2160) == juce::Point<int> (3840, 2160));
    CHECK (ScreenRecorder::fitSize (2880, 1864, 1080) == juce::Point<int> (1668, 1080));
    CHECK (ScreenRecorder::fitSize (1920, 1080, 2160) == juce::Point<int> (1920, 1080));   // never bigger than the screen
    CHECK (ScreenRecorder::fitSize (1920, 1080, 0) == juce::Point<int> (1920, 1080));
    const auto ultrawide = ScreenRecorder::fitSize (6880, 2880, 0);
    CHECK (ultrawide.x <= 4096);
    CHECK (ultrawide.x % 2 == 0);
    CHECK (ultrawide.y % 2 == 0);
    CHECK (ScreenRecorder::bitrateFor ({ 3840, 2160 }, 60) <= 80000000);
    CHECK (ScreenRecorder::bitrateFor ({ 1280, 720 }, 30) >= 4000000);

    ScreenRecordSettings s;
    s.wholeScreen = true;
    s.maxHeight = 1440;
    s.frameRate = 30;
    s.withMic = true;
    s.micChannel = 2;
    s.micStereo = true;
    s.micGainDb = -6.0f;
    s.folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atp-screen-" + juce::Uuid().toString());
    const auto back = ScreenRecordSettings::fromString (s.toString());
    CHECK (back.wholeScreen);
    CHECK (back.maxHeight == 1440);
    CHECK (back.frameRate == 30);
    CHECK (back.withMic);
    CHECK (back.micChannel == 2);
    CHECK (back.micStereo);
    CHECK (back.micGainDb == -6.0f);
    CHECK (back.folder == s.folder);

    CHECK (ScreenRecordSettings::fromString ("garbage").folder == ScreenRecordSettings::defaultFolder());

    s.folder.createDirectory();
    const auto file = s.nextFile ("My Song: Final");
    CHECK (file.hasFileExtension ("mp4"));
    CHECK (file.getParentDirectory() == s.folder);
    CHECK (file.getFileName().startsWith ("My Song"));
    CHECK_FALSE (file.getFileName().contains (":"));
    s.folder.deleteRecursively();

    // The options window offers each input and each stereo pair as the mic.
    ScreenRecordPanel panel (s, { "Mic", "Guitar", "Line L", "Line R" }, {}, {});
    const auto chosen = panel.getSettings();
    CHECK (chosen.micChannel == 2);
    CHECK (chosen.micStereo);
    CHECK (chosen.withMic);
}
