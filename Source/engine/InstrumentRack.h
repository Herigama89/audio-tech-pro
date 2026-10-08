#pragma once

#include <JuceHeader.h>
#include <map>
#include "BuiltInSynth.h"
#include "PluginInstrument.h"

namespace riff
{

/**
    Owns the instrument for every instrument track, keyed by track id.

    Instruments live here (not in the playback graph) because they hold state
    that must survive graph rebuilds: sounding voices, plugin settings. The
    graph only shares a pointer to them.

    Plugins load asynchronously; until one is ready its track is silent.
    Message thread only.
*/
class InstrumentRack
{
public:
    InstrumentRack();

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept           { return knownPlugins; }

    /**
        Returns the instrument for this track, creating or updating it to match
        the track's settings and the sample rate. May return nullptr while a
        plugin is still loading or if it failed.
    */
    std::shared_ptr<Instrument> instrumentFor (const juce::ValueTree& track, double sampleRate, int maxBlockSize);

    /** Forgets instruments whose tracks no longer exist. */
    void removeAllExcept (const juce::StringArray& trackIds);

    /** Writes every plugin's current settings into its track (call before saving). */
    void storePluginStates (juce::ValueTree project);

    /** The hosted plugin on a track, if it has one and it's loaded (for opening its window). */
    juce::AudioPluginInstance* getPlugin (const juce::String& trackId);

    /** Called when an async plugin load finishes, so the graph can be rebuilt. */
    std::function<void()> onInstrumentReady;
    std::function<void (const juce::String& trackName, const juce::String& error)> onLoadError;
    std::function<void (const juce::String& trackId)> onPluginReplaced; // close its editor window

    /** Settings for the built-in synth, read from the track. */
    static BuiltInSynth::Settings synthSettingsFrom (const juce::ValueTree& track);

private:
    struct Entry
    {
        juce::String key;                       // what the track asked for
        double sampleRate = 0.0;
        std::shared_ptr<Instrument> instrument;
        bool loading = false;
    };

    static juce::String keyFor (const juce::ValueTree& track);
    void loadPlugin (const juce::ValueTree& track, const juce::String& key, double sampleRate, int maxBlockSize,
                     const juce::String& state);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::map<juce::String, Entry> entries;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true); // guards async callbacks
};

} // namespace riff
