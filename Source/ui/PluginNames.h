#pragma once

#include <JuceHeader.h>

/**
    Short plugin names for insert slots, the way Logic shortens them:
    makers' prefixes and bracketed extras go first, then vowels inside
    words (from the end), then spaces, and only then is the name cut.
*/
namespace riff::pluginnames
{
    inline juce::String clean (juce::String name)
    {
        // "(Mono)", "[VST3]" and the like
        for (;;)
        {
            const auto open = name.indexOfAnyOf ("([");
            if (open < 0)
                break;
            const auto close = name.indexOfAnyOf (")]", open);
            name = (name.substring (0, open) + (close > open ? name.substring (close + 1) : juce::String())).trim();
        }
        for (const auto* prefix : { "FabFilter ", "Waves ", "UADx ", "UAD ", "Soundtoys ", "iZotope ", "Slate Digital ", "Plugin Alliance ",
                                    "Brainworx ", "Arturia ", "Native Instruments ", "Universal Audio " })
            if (name.startsWithIgnoreCase (prefix) && name.length() > (int) std::strlen (prefix) + 2)
                name = name.substring ((int) std::strlen (prefix));
        for (const auto* suffix : { " Stereo", " Mono", " Mono/Stereo", " Stereo/Mono" })
            if (name.endsWithIgnoreCase (suffix))
                name = name.dropLastCharacters ((int) std::strlen (suffix));
        return name.trim();
    }

    /** The name shortened until `fits` says it fits (a measure of the text's width against the slot). */
    inline juce::String abbreviate (const juce::String& full, const std::function<bool (const juce::String&)>& fits)
    {
        if (fits (full))
            return full;
        auto name = clean (full);
        if (fits (name))
            return name;

        // Vowels inside words, from the end
        auto isVowel = [] (juce::juce_wchar c) { return juce::String ("aeiou").containsChar (c); };
        for (int i = name.length() - 1; i > 0 && ! fits (name); --i)
            if (isVowel (name[i]) && name[i - 1] != ' ')
                name = name.substring (0, i) + name.substring (i + 1);
        if (fits (name))
            return name;

        name = name.removeCharacters (" ");
        while (name.length() > 1 && ! fits (name))
            name = name.dropLastCharacters (1);
        return name;
    }

    inline juce::String abbreviate (const juce::String& full, const juce::Font& font, float width)
    {
        return abbreviate (full, [&] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (font, s) <= width; });
    }
}
