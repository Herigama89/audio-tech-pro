#pragma once

#include <JuceHeader.h>

/**
    Where a track keeps its studio instrument's settings (Drums, Keys, Bass, Guitar, Beat Pad, Sampler): a STUDIO
    child holding each parameter by id (only those away from their defaults), the files it plays ("file0", and a
    beat pad's "file<pad>"), and the beat pad's pattern ("pattern"). The track's instrument is "studio:<kind>".
*/
namespace riff::studiostate
{
    inline const juce::Identifier STUDIO { "STUDIO" };
    inline const juce::String prefix { "studio:" };

    inline bool isStudio (const juce::ValueTree& track) { return track["instrument"].toString().startsWith (prefix); }
    inline juce::String kindOf (const juce::ValueTree& track) { return track["instrument"].toString().fromFirstOccurrenceOf (prefix, false, false); }
    inline juce::ValueTree of (const juce::ValueTree& track) { return track.getChildWithName (STUDIO); }

    inline juce::ValueTree getOrCreate (juce::ValueTree track, juce::UndoManager* um)
    {
        auto t = track.getChildWithName (STUDIO);
        if (! t.isValid())
        {
            t = juce::ValueTree (STUDIO);
            track.appendChild (t, um);
        }
        return t;
    }

    inline bool isFileProperty (const juce::Identifier& p) { return p.toString().startsWith ("file"); }
    inline int fileSlot (const juce::Identifier& p) { return p.toString().substring (4).getIntValue(); }
    inline juce::Identifier fileProperty (int slot) { return juce::Identifier ("file" + juce::String (slot)); }
    inline const juce::Identifier pattern { "pattern" };
}
