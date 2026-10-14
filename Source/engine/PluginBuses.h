#pragma once

#include <JuceHeader.h>

namespace riff::pluginbuses
{
    /**
        Sets a hosted plugin's buses up for a stereo track: its main input and output
        stereo, and every other bus (sidechains, extra outputs) left switched on.

        Extra buses are not switched off: JUCE hands a switched-off VST3 bus null
        channel pointers, and some plugins (Groove Agent among them) write to every
        output they have anyway, which crashes. We give them real (unused) channels.
    */
    inline void setUp (juce::AudioPluginInstance& plugin)
    {
        auto layout = plugin.getBusesLayout();

        for (const bool isInput : { true, false })
        {
            auto& buses = isInput ? layout.inputBuses : layout.outputBuses;
            for (int i = 0; i < buses.size(); ++i)
            {
                if (i == 0)
                {
                    buses.getReference (i) = juce::AudioChannelSet::stereo();
                    continue;
                }
                if (buses[i].isDisabled())
                {
                    const auto* bus = plugin.getBus (isInput, i);
                    const auto last = bus != nullptr ? bus->getLastEnabledLayout() : juce::AudioChannelSet();
                    buses.getReference (i) = last.isDisabled() ? juce::AudioChannelSet::stereo() : last;
                }
            }
        }

        if (plugin.checkBusesLayoutSupported (layout) && plugin.setBusesLayout (layout))
            return;

        // Not that way: keep the plugin's own layout, but with every bus on.
        plugin.enableAllBuses();
    }

    /** Whether a buffer of `channels` channels still covers every channel the plugin has now (its layout can change while it runs). */
    inline bool fits (const juce::AudioPluginInstance& plugin, int channels) noexcept
    {
        return plugin.getTotalNumInputChannels() <= channels && plugin.getTotalNumOutputChannels() <= channels;
    }
}
