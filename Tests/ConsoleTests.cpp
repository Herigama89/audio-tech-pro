#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "engine/ConsolePlugins.h"
#include "engine/TapeSaturation.h"
#include "engine/MasterBusCompressor.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::PluginDescription plugin (const juce::String& name, const juce::String& maker, const juce::String& format)
    {
        juce::PluginDescription d;
        d.name = name;
        d.manufacturerName = maker;
        d.pluginFormatName = format;
        return d;
    }
}

TEST_CASE ("Console mode recognises UA's G-series bus compressor", "[console-mode]")
{
    CHECK (console::isBusCompressor (plugin ("UADx SSL G Bus Compressor", "Universal Audio (UADx)", "AudioUnit")));
    CHECK (console::isBusCompressor (plugin ("UAD SSL G Bus Compressor", "Universal Audio (UAD)", "VST3")));
    CHECK (console::isBusCompressor (plugin ("SSL G-Master Buss Compressor", "Universal Audio", "VST3")));

    // Other makers' bus compressors too
    CHECK (console::isBusCompressor (plugin ("Comp 4K-Bus", "Antelope Audio", "AudioUnit")));
    CHECK (console::isBusCompressor (plugin ("SSL Native Bus Compressor 2", "Solid State Logic", "VST3")));
    CHECK (console::isBusCompressor (plugin ("The Glue", "Cytomic", "VST3")));

    // Not compressors for the bus
    CHECK_FALSE (console::isBusCompressor (plugin ("UADx SSL 4000 E Channel Strip", "Universal Audio (UADx)", "AudioUnit")));
    CHECK_FALSE (console::isBusCompressor (plugin ("UADx 1176 Classic Limiter", "Universal Audio (UADx)", "AudioUnit")));
    CHECK_FALSE (console::isBusCompressor (plugin ("Reverb Bus", "Someone Else", "VST3")));

    auto instrument = plugin ("UADx SSL G Bus Compressor", "Universal Audio", "AudioUnit");
    instrument.isInstrument = true;
    CHECK_FALSE (console::isBusCompressor (instrument));
}

TEST_CASE ("Console mode prefers the native Audio Unit version", "[console-mode]")
{
    const juce::Array<juce::PluginDescription> scanned {
        plugin ("Pro-Q 3", "FabFilter", "VST3"),
        plugin ("UAD SSL G Bus Compressor", "Universal Audio (UAD)", "VST3"),
        plugin ("UADx SSL G Bus Compressor", "Universal Audio (UADx)", "VST3"),
        plugin ("UADx SSL G Bus Compressor", "Universal Audio (UADx)", "AudioUnit"),
    };

    const auto found = console::findBusCompressors (scanned);
    REQUIRE (found.size() == 3);
    CHECK (found[0].name.startsWith ("UADx"));
    CHECK (found[0].pluginFormatName == "AudioUnit");
    CHECK (found[1].name.startsWith ("UADx"));
    CHECK (found[2].name.startsWith ("UAD "));

    CHECK (console::findBusCompressors ({ plugin ("Pro-Q 3", "FabFilter", "VST3") }).isEmpty());
}

TEST_CASE ("Console mode matches the panel's knobs to the plugin's parameters", "[console-mode]")
{
    using C = console::Control;
    const juce::StringArray names { "Power", "Comp In", "Threshold", "Make-Up Gain", "Attack", "Release",
                                    "Ratio", "Rate S/C", "Mix", "Headroom", "Gain Reduction" };

    CHECK (console::matchParameter (C::threshold, names) == 2);
    CHECK (console::matchParameter (C::makeup, names) == 3);
    CHECK (console::matchParameter (C::attack, names) == 4);
    CHECK (console::matchParameter (C::release, names) == 5);
    CHECK (console::matchParameter (C::ratio, names) == 6);
    CHECK (console::matchParameter (C::filter, names) == 7);
    CHECK (console::matchParameter (C::mix, names) == 8);
    CHECK (console::matchParameter (C::in, names) == 0);   // the first switch that fits
    CHECK (console::findGainReductionMeter (names) == 10);

    // Other spellings
    CHECK (console::matchParameter (C::makeup, { "Thresh", "Output Gain" }) == 1);
    CHECK (console::matchParameter (C::mix, { "Thresh", "Dry/Wet" }) == 1);
    CHECK (console::matchParameter (C::filter, { "Sidechain HPF" }) == 0);
    CHECK (console::matchParameter (C::in, { "Bypass" }) == 0);
    CHECK (console::isBypassParameter ("Bypass"));
    CHECK_FALSE (console::isBypassParameter ("Comp In"));

    // Nothing fits: the knob stays unassigned
    CHECK (console::matchParameter (C::ratio, { "Threshold", "Mix" }) == -1);
    CHECK (console::findGainReductionMeter ({ "Threshold" }) == -1);
}

TEST_CASE ("Console mode reads numbers from parameter text", "[console-mode]")
{
    CHECK_THAT (console::numberIn ("-12.5 dB"), WithinAbs (-12.5, 1e-6));
    CHECK_THAT (console::numberIn ("+4 dB"), WithinAbs (4.0, 1e-6));
    CHECK_THAT (console::numberIn ("GR 3.2"), WithinAbs (3.2, 1e-6));
    CHECK_THAT (console::numberIn ("Auto"), WithinAbs (0.0, 1e-6));
}

TEST_CASE ("Console mode recognises UA's E-series channel strip", "[console-mode]")
{
    CHECK (console::isChannelStrip (plugin ("UADx SSL 4000 E Channel Strip", "Universal Audio (UADx)", "AudioUnit")));
    CHECK (console::isChannelStrip (plugin ("UAD SSL E Channel Strip", "Universal Audio (UAD)", "VST3")));
    CHECK (console::isChannelStrip (plugin ("UAD SSL 4000 E", "Universal Audio", "VST3")));

    // Anyone's channel strips
    CHECK (console::isChannelStrip (plugin ("UADx API Vision Channel Strip", "Universal Audio (UADx)", "AudioUnit")));
    CHECK (console::isChannelStrip (plugin ("bx_console SSL 4000 E", "Plugin Alliance", "VST3")));
    CHECK (console::isChannelStrip (plugin ("bx_console N", "Plugin Alliance", "AudioUnit")));
    CHECK (console::isChannelStrip (plugin ("SSL Native Channel Strip 2", "Solid State Logic", "VST3")));
    CHECK (console::isChannelStrip (plugin ("SSL E-Channel", "Waves", "VST3")));

    CHECK_FALSE (console::isChannelStrip (plugin ("UADx SSL G Bus Compressor", "Universal Audio (UADx)", "AudioUnit")));
    CHECK_FALSE (console::isChannelStrip (plugin ("Channel EQ", "Apple", "AudioUnit")));
    CHECK_FALSE (console::isChannelStrip (plugin ("Pro-Q 3", "FabFilter", "VST3")));

    const auto found = console::findChannelStrips ({ plugin ("UAD SSL E Channel Strip", "Universal Audio (UAD)", "VST3"),
                                                     plugin ("UADx SSL 4000 E Channel Strip", "Universal Audio (UADx)", "AudioUnit"),
                                                     plugin ("Pro-Q 3", "FabFilter", "VST3") });
    REQUIRE (found.size() == 2);
    CHECK (found[0].name.startsWith ("UADx"));
}

TEST_CASE ("Console mode matches strip knobs to the channel strip's parameters", "[console-mode]")
{
    using S = console::StripControl;
    const juce::StringArray names { "Power", "Input", "HP Filter", "LP Filter", "HF Gain", "HF Freq", "HF Bell",
                                    "HMF Gain", "HMF Freq", "HMF Q", "LMF Gain", "LMF Freq", "LMF Q",
                                    "LF Gain", "LF Freq", "LF Bell", "EQ In", "Comp Ratio", "Comp Thresh",
                                    "Comp Release", "Gate Thresh", "Dyn In", "Output" };

    CHECK (console::matchStripParameter (S::hf, names) == 4);
    CHECK (console::matchStripParameter (S::hmf, names) == 7);
    CHECK (console::matchStripParameter (S::lmf, names) == 10);
    CHECK (console::matchStripParameter (S::lf, names) == 13);
    CHECK (console::matchStripParameter (S::eqIn, names) == 16);
    CHECK (console::matchStripParameter (S::dynamics, names) == 18);   // the compressor's, not the gate's
    CHECK (console::matchStripParameter (S::dynIn, names) == 21);

    // Other spellings
    CHECK (console::matchStripParameter (S::lf, { "LF Freq", "LF" }) == 1);
    CHECK (console::matchStripParameter (S::dynamics, { "Gate Threshold", "Threshold" }) == 1);
    CHECK (console::matchStripParameter (S::eqIn, { "EQ" }) == 0);
    CHECK (console::matchStripParameter (S::hmf, { "HF Gain", "LMF Gain" }) == -1);
}

namespace
{
    /** The level of one frequency in a signal (Goertzel), as an amplitude. */
    double toneLevel (const std::vector<float>& s, double freq, double rate, size_t from)
    {
        const auto w = juce::MathConstants<double>::twoPi * freq / rate;
        const auto coeff = 2.0 * std::cos (w);
        double q1 = 0.0, q2 = 0.0;
        for (size_t i = from; i < s.size(); ++i)
        {
            const auto q0 = coeff * q1 - q2 + s[i];
            q2 = q1;
            q1 = q0;
        }
        const auto n = (double) (s.size() - from);
        return 2.0 * std::sqrt (std::max (0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2)) / n;
    }
    std::vector<float> testSine (double freq, float amp, int samples, double rate = 48000.0)
    {
        std::vector<float> s ((size_t) samples);
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) i / rate);
        return s;
    }
}

TEST_CASE ("Tape saturation: unity when quiet, rounds the peaks, harmonics, the head bump", "[console]")
{
    TapeSaturator tape;
    tape.prepare (48000.0);
    TapeSaturator::Settings s;
    s.enabled = true;
    s.drive = 1.0f;
    tape.setSettings (s);

    // Quiet: through at (about) unity
    auto l = testSine (1000.0, 0.01f, 48000), r = l;
    tape.process (l.data(), r.data(), (int) l.size());
    CHECK_THAT (toneLevel (l, 1000.0, 48000.0, 4800), Catch::Matchers::WithinRel (0.01, 0.12));

    // Loud: the peaks held down, with harmonics (3rd from the saturation, 2nd from its asymmetry)
    tape.reset();
    auto ll = testSine (1000.0, 0.9f, 48000), rr = ll;
    tape.process (ll.data(), rr.data(), (int) ll.size());
    const auto fundamental = toneLevel (ll, 1000.0, 48000.0, 4800);
    CHECK (fundamental < 0.6);
    CHECK (toneLevel (ll, 3000.0, 48000.0, 4800) > fundamental * 0.01);
    CHECK (toneLevel (ll, 2000.0, 48000.0, 4800) > fundamental * 0.001);
    for (auto x : ll) REQUIRE (std::isfinite (x));

    // Off: untouched
    s.enabled = false;
    tape.setSettings (s);
    auto ol = testSine (1000.0, 0.9f, 4800), orr = ol;
    const auto copy = ol;
    tape.process (ol.data(), orr.data(), (int) ol.size());
    CHECK (ol == copy);

    // The head bump: higher and lower at 15 ips than at 30
    TapeSaturator::Settings slow, fast;
    slow.enabled = fast.enabled = true;
    slow.speed = 0;
    fast.speed = 1;
    CHECK (TapeSaturator::responseDb (slow, 55.0) > 1.5);
    CHECK (TapeSaturator::responseDb (fast, 100.0) > 1.0);
    CHECK (TapeSaturator::responseDb (slow, 18000.0) < TapeSaturator::responseDb (fast, 18000.0));
}

TEST_CASE ("Master Bus Compressor's Color: harmonics and grit, level kept", "[console]")
{
    const auto runWith = [] (bool color, float grit, float amp)
    {
        MasterBusCompressor c;
        MasterBusCompressor::Settings s;
        s.enabled = true;
        s.thresholdDb = 0.0f;   // (no compression: the colour alone)
        s.color = color;
        s.grit = grit;
        c.setSettings (s);
        c.prepare (48000.0);
        auto l = testSine (500.0, amp, 48000), r = l;
        c.process (l.data(), r.data(), (int) l.size());
        return l;
    };
    const auto clean = runWith (false, 0.0f, 0.5f);
    const auto warm = runWith (true, 0.0f, 0.5f);
    const auto gritty = runWith (true, 1.0f, 0.5f);
    CHECK (toneLevel (clean, 1000.0, 48000.0, 4800) < 1.0e-4);
    CHECK (toneLevel (warm, 1000.0, 48000.0, 4800) > 0.002);                                       // even harmonics
    CHECK (toneLevel (gritty, 1500.0, 48000.0, 4800) > toneLevel (warm, 1500.0, 48000.0, 4800) * 2.0);   // grit: more odd ones
    // the level stays about the same at low levels
    const auto quiet = runWith (true, 0.3f, 0.02f);
    CHECK_THAT (toneLevel (quiet, 500.0, 48000.0, 4800), Catch::Matchers::WithinRel (0.02, 0.15));
}

#include "engine/PreampSaturation.h"

TEST_CASE ("Preamp: its gain turns the channel up, and the more gain the more harmonics", "[console]")
{
    using namespace riff;
    auto thd = [] (float gainDb, float level, float& outLevel)
    {
        PreampSaturator pre;
        pre.prepare (48000.0);
        pre.setSettings ({ true, gainDb });
        const int n = 48000;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i)
            l[(size_t) i] = r[(size_t) i] = level * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f);
        pre.process (l.data(), r.data(), n);
        // the fundamental and the 2nd and 3rd harmonics, over the last half second (a whole number of cycles)
        auto amp = [&] (double hz)
        {
            double s = 0.0, c = 0.0;
            for (int i = n / 2; i < n; ++i)
            {
                s += l[(size_t) i] * std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0);
                c += l[(size_t) i] * std::cos (juce::MathConstants<double>::twoPi * hz * i / 48000.0);
            }
            return 2.0 * std::sqrt (s * s + c * c) / (n / 2);
        };
        const auto f = amp (1000.0);
        outLevel = (float) f;
        return std::sqrt (amp (2000.0) * amp (2000.0) + amp (3000.0) * amp (3000.0)) / f;
    };
    float out = 0.0f;
    const auto clean = thd (12.0f, 0.01f, out);
    CHECK (std::abs (out / (0.01 * std::pow (10.0, 12.0 / 20.0)) - 1.0) < 0.05);   // quiet: just louder, by the gain
    CHECK (clean < 0.01);
    const auto some = thd (12.0f, 0.25f, out);    // -12 dB in, +12 dB of gain
    const auto lots = thd (30.0f, 0.25f, out);
    CHECK (some > 0.005);
    CHECK (lots > some * 3.0);
    CHECK (out < 2.6f);   // (capped about 6 dB above full scale)

    PreampSaturator off;
    off.prepare (48000.0);
    std::vector<float> l (64, 0.3f), r (64, 0.3f);
    off.process (l.data(), r.data(), 64);
    CHECK (l[10] == 0.3f);   // out: untouched
}
