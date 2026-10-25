#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    A plugin's parameter names (and its gain-reduction meter, if it publishes one), read once per plugin.
    Asking a channel-strip plugin for its hundreds of names several times a second, on every strip,
    made the whole app crawl (some plugins answer slowly).
*/
class ParameterNames
{
public:
    const juce::StringArray& of (juce::AudioPluginInstance* plugin)
    {
        if (plugin == nullptr)
        {
            instance = nullptr;
            names.clearQuick();
            meter = -1;
            return names;
        }
        const auto& params = plugin->getParameters();
        if (plugin != instance || params.size() != names.size())
        {
            instance = plugin;
            names.clearQuick();
            meter = -1;
            for (int i = 0; i < params.size(); ++i)
            {
                names.add (params[i]->getName (64));
                if (meter < 0 && params[i]->getCategory() == juce::AudioProcessorParameter::compressorLimiterGainReductionMeter)
                    meter = i;
            }
        }
        return names;
    }

    /** The parameter its maker marked as the gain-reduction meter (-1: none); after of(). */
    int meterIndex() const noexcept { return meter; }

    juce::String nameOf (juce::AudioPluginInstance* plugin, int index)
    {
        const auto& n = of (plugin);
        return juce::isPositiveAndBelow (index, n.size()) ? n[index] : juce::String();
    }

private:
    juce::AudioPluginInstance* instance = nullptr;
    juce::StringArray names;
    int meter = -1;
};

} // namespace riff
