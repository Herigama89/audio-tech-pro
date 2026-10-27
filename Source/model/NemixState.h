#pragma once

#include <JuceHeader.h>
#include "../engine/nemix/NemixParams.h"
#include <set>

/**
    Where a track keeps its Nemix sound: a NEMIX child of the track holding each parameter that isn't at its
    default (by id), and the preset's name. Changing one property changes that knob straight away (the player
    passes it to the synth without rebuilding anything); undo works on each like any other edit.
*/
namespace riff::nemixstate
{
    inline const juce::Identifier NEMIX { "NEMIX" };
    inline const juce::Identifier preset { "nmxPreset" };

    inline juce::ValueTree of (const juce::ValueTree& track) { return track.getChildWithName (NEMIX); }

    inline juce::ValueTree getOrCreate (juce::ValueTree track, juce::UndoManager* um)
    {
        auto t = track.getChildWithName (NEMIX);
        if (! t.isValid())
        {
            t = juce::ValueTree (NEMIX);
            track.appendChild (t, um);
        }
        return t;
    }

    /** A parameter's value as the tree has it (its default if it isn't there). */
    inline float valueOf (const juce::ValueTree& state, int index)
    {
        const auto& p = nemix::params()[(size_t) index];
        return state.isValid() ? (float) (double) state.getProperty (juce::Identifier (p.id), p.defaultValue) : p.defaultValue;
    }

    /** Sets one (a default value is removed, so the tree stays small). */
    inline void set (juce::ValueTree state, int index, float value, juce::UndoManager* um)
    {
        const auto& p = nemix::params()[(size_t) index];
        const juce::Identifier id (p.id);
        value = juce::jlimit (p.min, p.max, value);
        if (std::abs (value - p.defaultValue) < 1.0e-7f * std::max (1.0f, std::abs (p.max - p.min)))
            state.removeProperty (id, um);
        else
            state.setProperty (id, value, um);
    }

    /** Every parameter of a sound: (index, value) pairs, defaults left out. */
    using Sound = std::vector<std::pair<int, float>>;

    inline Sound soundOf (const juce::ValueTree& state)
    {
        Sound s;
        if (! state.isValid())
            return s;
        for (int i = 0; i < state.getNumProperties(); ++i)
        {
            const auto name = state.getPropertyName (i);
            const auto index = nemix::indexOf (name.toString());
            if (index >= 0)
                s.push_back ({ index, (float) (double) state.getProperty (name) });
        }
        return s;
    }

    /** Replaces the whole sound (one undo step if um is given and a transaction was begun). */
    inline void load (juce::ValueTree state, const Sound& sound, const juce::String& name, juce::UndoManager* um)
    {
        // Clear what isn't in the new sound, then set what is
        std::vector<juce::Identifier> remove;
        for (int i = 0; i < state.getNumProperties(); ++i)
            remove.push_back (state.getPropertyName (i));
        std::set<int> setNow;
        for (const auto& [index, value] : sound)
            setNow.insert (index);
        for (const auto& id : remove)
        {
            const auto index = nemix::indexOf (id.toString());
            if (index >= 0 && setNow.count (index) == 0)
                state.removeProperty (id, um);
        }
        for (const auto& [index, value] : sound)
            set (state, index, value, um);
        state.setProperty (preset, name, um);
    }

    /** A sound as text ("id=value;id=value"), for saving your own presets. */
    inline juce::String toText (const Sound& sound)
    {
        juce::StringArray parts;
        for (const auto& [index, value] : sound)
            parts.add (nemix::params()[(size_t) index].id + "=" + juce::String (value, 6));
        return parts.joinIntoString (";");
    }

    inline Sound fromText (const juce::String& text)
    {
        Sound s;
        for (const auto& part : juce::StringArray::fromTokens (text, ";", ""))
        {
            const auto index = nemix::indexOf (part.upToFirstOccurrenceOf ("=", false, false).trim());
            if (index >= 0)
                s.push_back ({ index, part.fromFirstOccurrenceOf ("=", false, false).getFloatValue() });
        }
        return s;
    }
}
