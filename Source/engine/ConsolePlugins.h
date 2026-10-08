#pragma once

#include <JuceHeader.h>

/**
    Console mode: finding the Universal Audio plugins a user owns, and matching
    the panel's controls to the plugin's parameters by name.

    Nothing here loads or unlocks anything: it only recognises plugins that
    are installed and authorised on the user's own computer.
*/
namespace riff::console
{
    /** True if a scanned plugin is UA's G-series bus compressor (any format, UAD or UADx). */
    bool isBusCompressor (const juce::PluginDescription& d);

    /**
        The bus compressors among the scanned plugins, best first: native
        (UADx) before DSP versions, Audio Unit before VST3 on a Mac.
    */
    juce::Array<juce::PluginDescription> findBusCompressors (const juce::Array<juce::PluginDescription>& scanned);

    /** The panel's controls, in the order they appear. */
    enum class Control { threshold, mix, attack, release, filter, ratio, makeup, in, numControls };

    juce::String controlName (Control c);

    /** Which parameter (by index) a control should drive, or -1 if none fits. */
    int matchParameter (Control c, const juce::StringArray& parameterNames);

    /** For the IN key: true if the matched parameter is a "bypass" (on means out). */
    bool isBypassParameter (const juce::String& parameterName);

    /** A plugin's gain-reduction meter output, if it publishes one, or -1. */
    int findGainReductionMeter (const juce::StringArray& parameterNames);

    /** The first number in a parameter's display text, e.g. "-12.5 dB" -> -12.5 (0 if none). */
    float numberIn (const juce::String& text);
}
