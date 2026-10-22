#pragma once

#include <JuceHeader.h>

namespace riff::presets
{
    /**
        Presets for inserts, as Pro Tools' and Cubase's plug-in headers keep them: per plug-in, in the user's
        Presets folder (Audio Tech Pro/Presets/<plug-in>/<name>.atppreset). A built-in effect's preset is its
        knob values; another maker's plug-in's is its own saved state.

        One preset per plug-in can be its Default ("Set as Default"): every new instance starts from it.
    */

    /** The Presets folder (tests can point it elsewhere). */
    juce::File root();
    void setRootForTests (const juce::File& folder);

    /** Which plug-in an insert is, as a folder name: the built-in effect's type, or the plug-in's maker, name and format. */
    juce::String keyOf (const juce::ValueTree& insert);
    /** A plug-in's name for menus. */
    juce::String displayNameOf (const juce::ValueTree& insert);

    /** The saved presets' names, sorted (not the Default). */
    juce::StringArray list (const juce::ValueTree& insert);

    /** Saves the insert's settings as a preset (pluginState: the plug-in's current state; built-ins don't need it). */
    bool save (const juce::ValueTree& insert, const juce::String& name, const juce::String& pluginState = {});

    /** Loads a preset into the insert (undoable): the knobs, or the plug-in's state (also given back, to apply to the
        running plug-in). False if there's no such preset. */
    bool load (juce::ValueTree insert, const juce::String& name, juce::UndoManager* um, juce::String* pluginState = nullptr);

    bool remove (const juce::ValueTree& insert, const juce::String& name);

    /** The Default: saved, there, cleared, and put on a new insert (before it's in the project: no undo). */
    bool saveDefault (const juce::ValueTree& insert, const juce::String& pluginState = {});
    bool hasDefault (const juce::ValueTree& insert);
    bool clearDefault (const juce::ValueTree& insert);
    bool applyDefault (juce::ValueTree insert, juce::UndoManager* um, juce::String* pluginState = nullptr);

    /** Built-ins: back to the factory settings (every knob to its default, undoable). */
    void resetToFactory (juce::ValueTree insert, juce::UndoManager* um);
}
