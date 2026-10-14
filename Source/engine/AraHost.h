#pragma once

#include <JuceHeader.h>

#if RIFF_HAS_ARA && JUCE_INTERNAL_HAS_ARA
 #define RIFF_ARA_ENABLED 1
#else
 #define RIFF_ARA_ENABLED 0
#endif

namespace riff
{
class PluginEffect;

/**
    ARA, as in Logic and Pro Tools: a plugin like Melodyne, VocAlign or RX
    Connect in a track's insert works right on that track's audio regions.
    It reads their audio, analyses it, shows them in its own window, and plays
    them back itself (the track's regions are not played by the engine while
    it does), with its edits.

    One AraDocument per ARA insert: the track's regions are its playback
    regions (each region's file is an audio source, with one audio
    modification per file on that track, so a split region keeps its edits).
    The plugin's edits are saved in the project (the insert's araArchive).
*/
class AraDocument
{
public:
    /** Made once the plugin's ARA factory is known (asynchronously). `archive`: its saved edits (base64). */
    static void create (PluginEffect& effect, juce::AudioFormatManager& formats, const juce::String& name,
                        const juce::String& archive, std::function<void (std::unique_ptr<AraDocument>)> done);

    ~AraDocument();

    /** Brings the plugin's regions in line with the track's: added, moved, trimmed, removed. Message thread. */
    void sync (const juce::ValueTree& project, const juce::ValueTree& track);

    /** The plugin's edits, to save with the project (base64), or empty. */
    juce::String storeArchive();

    /** How many regions the plugin has now (for tests and the status line). */
    int numRegions() const;

    /** The plugin asked the host to start, stop or move the playhead (e.g. Melodyne's own transport). */
    std::function<void (bool start)> onRequestPlayback;
    std::function<void (double seconds)> onRequestPosition;

    struct Impl;

private:
    explicit AraDocument (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};

/** Whether this build can host ARA plugins at all. */
constexpr bool araAvailable() { return RIFF_ARA_ENABLED != 0; }

} // namespace riff
