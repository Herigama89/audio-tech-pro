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

    // --- Channel strips --------------------------------------------------------

    /** True if a scanned plugin is UA's E-series (4000 E / 4K E) channel strip, UAD or UADx. */
    bool isChannelStrip (const juce::PluginDescription& d);

    /** The channel strips among the scanned plugins, best first (UADx, then Audio Unit). */
    juce::Array<juce::PluginDescription> findChannelStrips (const juce::Array<juce::PluginDescription>& scanned);

    /** The controls on a mixer strip that drive a channel strip plugin. */
    enum class StripControl { hf, hmf, lmf, lf, dynamics, eqIn, dynIn, numControls };

    juce::String stripControlName (StripControl c);

    /** Which parameter (by index) a strip control should drive, or -1 if none fits. */
    int matchStripParameter (StripControl c, const juce::StringArray& parameterNames);

    /** A plugin's gain-reduction meter output, if it publishes one, or -1. */
    int findGainReductionMeter (const juce::StringArray& parameterNames);

    /** The first number in a parameter's display text, e.g. "-12.5 dB" -> -12.5 (0 if none). */
    float numberIn (const juce::String& text);

    /**
        Sets a plugin parameter to what a person would type: a number as the
        plugin shows it ("-6" for -6 dB, "4" for 4:1) or one of its words ("Auto", "On").
        Searches the parameter's range using its own display text, so it works
        for any plugin. Returns the text the parameter shows afterwards, or
        empty if the value couldn't be matched.
    */
    /** The parameter a panel control drives on a loaded plugin: the one chosen by hand
        (the insert's "map<N>" property) or else the one matched by name. */
    juce::AudioProcessorParameter* parameterFor (juce::AudioPluginInstance* plugin, const juce::ValueTree& insert, Control c);
    juce::AudioProcessorParameter* parameterFor (juce::AudioPluginInstance* plugin, const juce::ValueTree& insert, StripControl c);

    juce::String setParameterFromText (juce::AudioProcessorParameter& parameter, const juce::String& wanted);
}
