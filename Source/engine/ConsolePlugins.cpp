#include "ConsolePlugins.h"

namespace riff::console
{

namespace
{
    bool isUniversalAudio (const juce::PluginDescription& d)
    {
        const auto maker = d.manufacturerName.toLowerCase();
        const auto name = d.name.toLowerCase();
        return maker.contains ("universal audio") || maker.startsWith ("uad") || maker == "ua"
            || name.startsWith ("uad ") || name.startsWith ("uadx ");
    }

    juce::String words (juce::String s)
    {
        // "Make-Up_Gain" -> " make up gain "
        s = s.toLowerCase();
        for (auto c : juce::String ("-_/().:"))
            s = s.replaceCharacter (c, ' ');
        return " " + s.trim() + " ";
    }
}

bool isBusCompressor (const juce::PluginDescription& d)
{
    if (d.isInstrument || ! isUniversalAudio (d))
        return false;

    const auto name = words (d.name);
    const bool gSeries = name.contains (" ssl g") || name.contains (" g bus") || name.contains (" gbus")
                      || name.contains (" g master") || name.contains (" g series");
    const bool busComp = name.contains (" bus") || name.contains (" buss") || name.contains (" comp");
    return gSeries && busComp;
}

juce::Array<juce::PluginDescription> findBusCompressors (const juce::Array<juce::PluginDescription>& scanned)
{
    juce::Array<juce::PluginDescription> found;
    for (const auto& d : scanned)
        if (isBusCompressor (d))
            found.add (d);

    auto score = [] (const juce::PluginDescription& d)
    {
        int s = 0;
        if (words (d.name).contains (" uadx ") || d.manufacturerName.containsIgnoreCase ("uadx")) s += 10;   // runs without UA hardware
        if (d.pluginFormatName == "AudioUnit") s += 2;
        if (d.pluginFormatName == "VST3") s += 1;
        return s;
    };

    std::stable_sort (found.begin(), found.end(), [&] (const auto& a, const auto& b) { return score (a) > score (b); });
    return found;
}

juce::String controlName (Control c)
{
    switch (c)
    {
        case Control::threshold: return "Threshold";
        case Control::mix:       return "Mix";
        case Control::attack:    return "Attack";
        case Control::release:   return "Release";
        case Control::filter:    return "S/C Filter";
        case Control::ratio:     return "Ratio";
        case Control::makeup:    return "Make-up Gain";
        case Control::in:        return "In";
        case Control::numControls: break;
    }
    return {};
}

int matchParameter (Control c, const juce::StringArray& names)
{
    auto find = [&names] (std::function<bool (const juce::String&)> test)
    {
        for (int i = 0; i < names.size(); ++i)
            if (test (words (names[i])))
                return i;
        return -1;
    };

    auto has = [] (std::initializer_list<const char*> keys)
    {
        return [keys] (const juce::String& w)
        {
            for (auto* k : keys)
                if (w.contains (k))
                    return true;
            return false;
        };
    };

    switch (c)
    {
        case Control::threshold: return find (has ({ " threshold", " thresh" }));
        case Control::mix:       return find (has ({ " mix ", " wet", " blend", " dry " }));
        case Control::attack:    return find (has ({ " attack" }));
        case Control::release:   return find (has ({ " release", " recovery" }));
        case Control::filter:    return find (has ({ " s c", " sidechain", " side chain", " hpf", " high pass", " highpass", " filter" }));
        case Control::ratio:     return find (has ({ " ratio" }));
        case Control::makeup:
        {
            const auto i = find (has ({ " makeup", " make up" }));
            return i >= 0 ? i : find (has ({ " output", " out gain", " gain " }));
        }
        case Control::in:
            return find ([] (const juce::String& w)
            {
                return w == " in " || w == " on " || w.contains (" comp in") || w.contains (" power")
                    || w.contains (" bypass") || w.contains (" enable");
            });
        case Control::numControls: break;
    }

    return -1;
}

bool isBypassParameter (const juce::String& name)
{
    return name.containsIgnoreCase ("bypass");
}

int findGainReductionMeter (const juce::StringArray& names)
{
    for (int i = 0; i < names.size(); ++i)
    {
        const auto w = words (names[i]);
        if (w.contains (" gain reduction") || w.contains (" gr ") || w.contains (" gr meter") || w.contains (" compression meter"))
            return i;
    }
    return -1;
}

float numberIn (const juce::String& text)
{
    const auto start = text.indexOfAnyOf ("-+0123456789.");
    return start < 0 ? 0.0f : text.substring (start).getFloatValue();
}

} // namespace riff::console
