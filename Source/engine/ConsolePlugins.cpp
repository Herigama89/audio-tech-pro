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
    if (d.isInstrument)
        return false;

    const auto name = words (d.name);
    const bool gSeries = name.contains (" ssl g") || name.contains (" g bus") || name.contains (" gbus")
                      || name.contains (" g master") || name.contains (" g series");
    const bool bus = name.contains (" bus") || name.contains (" buss") || name.contains ("bus ");
    const bool comp = name.contains (" comp") || name.contains ("comp ");
    if (isUniversalAudio (d) && gSeries && (bus || comp))
        return true;
    // Anyone's: SSL Native Bus Compressor, Comp 4K-Bus, bx_console bus comps, The Glue, G-Master, API 2500...
    return (bus && comp) || name.contains (" glue ") || name.contains (" g master") || name.contains (" gmaster")
        || name.contains (" g bus") || name.contains (" gbus") || name.contains (" 2500 ") || name.contains (" mix bus comp");
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

//==============================================================================
bool isChannelStrip (const juce::PluginDescription& d)
{
    if (d.isInstrument)
        return false;

    const auto name = words (d.name);
    if (name.contains (" bus") || name.contains (" buss") || name.contains (" silence"))
        return false;

    const bool eSeries = name.contains (" 4000 e") || name.contains (" 4000e") || name.contains (" 4k e")
                      || name.contains (" e channel") || name.contains (" e series") || name.contains (" ssl e ");
    if (eSeries)
        return true;
    // Anyone's console strips: bx_console (SSL, N, Focusrite, AMEK, API...), SSL Native Channel Strip, API Vision,
    // Waves E/G-Channel, Scheps Omni Channel, Console 1, Harrison 32C, MixHub...
    for (const auto* key : { " channel strip", " channelstrip", " bx console", " g channel", " echannel", " gchannel",
                             " vision channel", " omni channel", " console 1 ", " 32c ", " mixhub", " 9000 j", " 4000 g", " 4k g",
                             " channel module", " strip " })
        if (name.contains (key))
            return true;
    return false;
}

juce::Array<juce::PluginDescription> findChannelStrips (const juce::Array<juce::PluginDescription>& scanned)
{
    juce::Array<juce::PluginDescription> found;
    for (const auto& d : scanned)
        if (isChannelStrip (d))
            found.add (d);

    auto score = [] (const juce::PluginDescription& d)
    {
        int s = 0;
        if (words (d.name).contains (" uadx ") || d.manufacturerName.containsIgnoreCase ("uadx")) s += 10;
        if (d.pluginFormatName == "AudioUnit") s += 2;
        if (d.pluginFormatName == "VST3") s += 1;
        return s;
    };

    std::stable_sort (found.begin(), found.end(), [&] (const auto& a, const auto& b) { return score (a) > score (b); });
    return found;
}

juce::String stripControlName (StripControl c)
{
    switch (c)
    {
        case StripControl::hf:       return "HF Gain";
        case StripControl::hmf:      return "HMF Gain";
        case StripControl::lmf:      return "LMF Gain";
        case StripControl::lf:       return "LF Gain";
        case StripControl::dynamics: return "Compressor Threshold";
        case StripControl::eqIn:     return "EQ In";
        case StripControl::dynIn:    return "Dynamics In";
        case StripControl::numControls: break;
    }
    return {};
}

int matchStripParameter (StripControl c, const juce::StringArray& names)
{
    auto find = [&names] (std::function<bool (const juce::String&)> test)
    {
        for (int i = 0; i < names.size(); ++i)
            if (test (words (names[i])))
                return i;
        return -1;
    };

    // A band's gain: names the band, says gain/boost/cut, not its frequency, Q or bell switch.
    auto bandGain = [&find] (const char* band)
    {
        const juce::String b (band);
        const auto i = find ([b] (const juce::String& w)
        {
            return w.contains (b) && (w.contains (" gain") || w.contains (" boost") || w.contains (" cut") || w.contains (" level"));
        });
        if (i >= 0)
            return i;
        // Some plugins name the gain just by the band ("HF").
        return find ([b] (const juce::String& w) { return w == b + " "; });
    };

    auto anyOf = [] (const juce::String& w, std::initializer_list<const char*> keys)
    {
        for (auto* k : keys)
            if (w.contains (k))
                return true;
        return false;
    };

    switch (c)
    {
        case StripControl::hf:  return bandGain (" hf");
        case StripControl::hmf: return bandGain (" hmf");
        case StripControl::lmf: return bandGain (" lmf");
        case StripControl::lf:  return bandGain (" lf");

        case StripControl::dynamics:
        {
            const auto i = find ([&] (const juce::String& w) { return anyOf (w, { " comp", " cmp" }) && anyOf (w, { " thresh" }); });
            return i >= 0 ? i : find ([&] (const juce::String& w) { return w.contains (" thresh") && ! anyOf (w, { " gate", " exp" }); });
        }

        case StripControl::eqIn:
            return find ([&] (const juce::String& w) { return w == " eq " || anyOf (w, { " eq in", " eq on", " eq bypass", " eq enable" }); });

        case StripControl::dynIn:
            return find ([&] (const juce::String& w)
            {
                return w == " dyn " || w == " dynamics " || anyOf (w, { " dyn in", " dynamics in", " dyn on", " comp in", " dyn bypass" });
            });

        case StripControl::numControls: break;
    }

    return -1;
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

namespace
{
    template <typename Match>
    juce::AudioProcessorParameter* mappedParameter (juce::AudioPluginInstance* plugin, const juce::ValueTree& insert, int control, Match match)
    {
        if (plugin == nullptr)
            return nullptr;

        const auto& params = plugin->getParameters();
        const juce::Identifier key ("map" + juce::String (control));
        int index = -1;

        if (insert.hasProperty (key))
        {
            index = insert[key];
        }
        else
        {
            juce::StringArray names;
            for (auto* p : params)
                names.add (p->getName (64));
            index = match (names);
        }

        return juce::isPositiveAndBelow (index, params.size()) ? params[index] : nullptr;
    }
}

juce::AudioProcessorParameter* parameterFor (juce::AudioPluginInstance* plugin, const juce::ValueTree& insert, Control c)
{
    return mappedParameter (plugin, insert, (int) c, [c] (const juce::StringArray& names) { return matchParameter (c, names); });
}

juce::AudioProcessorParameter* parameterFor (juce::AudioPluginInstance* plugin, const juce::ValueTree& insert, StripControl c)
{
    return mappedParameter (plugin, insert, (int) c, [c] (const juce::StringArray& names) { return matchStripParameter (c, names); });
}

juce::String setParameterFromText (juce::AudioProcessorParameter& parameter, const juce::String& wanted)
{
    const auto target = wanted.trim();
    if (target.isEmpty())
        return {};

    const auto steps = parameter.getNumSteps();
    const int samples = (steps > 1 && steps <= 400) ? steps : 400;
    auto valueAt = [samples] (int i) { return (float) i / (float) (samples - 1); };

    auto apply = [&parameter] (float v)
    {
        const bool attached = parameter.getParameterIndex() >= 0;   // (gestures need a processor)
        if (attached) parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        if (attached) parameter.endChangeGesture();
        return parameter.getCurrentValueAsText();
    };

    const bool isNumber = target.containsAnyOf ("0123456789")
                          && target.retainCharacters ("-+0123456789.").length() >= target.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").length();

    if (! isNumber)
    {
        // A word: the first position whose text matches.
        for (int i = 0; i < samples; ++i)
        {
            const auto text = parameter.getText (valueAt (i), 64).trim();
            if (text.equalsIgnoreCase (target) || text.startsWithIgnoreCase (target))
                return apply (valueAt (i));
        }
        return {};
    }

    const auto number = target.retainCharacters ("-+0123456789.").getFloatValue();
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < samples; ++i)
    {
        const auto text = parameter.getText (valueAt (i), 64);
        if (! text.containsAnyOf ("0123456789"))
            continue;
        const auto distance = std::abs (numberIn (text) - number);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    if (best < 0)
        return {};

    // Continuous parameters: refine between the neighbours by bisection.
    auto v = valueAt (best);
    if (steps <= 1 || steps > 400)
    {
        auto lo = valueAt (juce::jmax (0, best - 1)), hi = valueAt (juce::jmin (samples - 1, best + 1));
        const bool rising = numberIn (parameter.getText (hi, 64)) >= numberIn (parameter.getText (lo, 64));
        for (int i = 0; i < 24; ++i)
        {
            const auto mid = 0.5f * (lo + hi);
            const auto n = numberIn (parameter.getText (mid, 64));
            if ((n < number) == rising)
                lo = mid;
            else
                hi = mid;
        }
        const auto candidate = 0.5f * (lo + hi);
        if (std::abs (numberIn (parameter.getText (candidate, 64)) - number) <= bestDistance)
            v = candidate;
    }

    return apply (v);
}

} // namespace riff::console
