#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "engine/ConsolePlugins.h"

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
