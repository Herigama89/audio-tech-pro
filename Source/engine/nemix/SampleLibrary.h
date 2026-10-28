#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

/**
    Nemix's recorded instruments: pianos, basses, guitars, drum kits, the orchestra.

    The library is a folder ("Nemix Library") installed with the app: instruments.txt lists the instruments in
    a fixed order (a preset names one by its place), and each has a folder of Ogg Vorbis recordings with a
    key map (map.txt: which recording plays for which keys and velocities, round robins, choke groups).

    An instrument is decoded (to 16-bit, in memory) on a background thread the first time it's asked for, and
    kept for as long as the app runs, so the audio thread can read it without locks once `ready` is set.
*/
namespace riff::nemix::samples
{
    struct Recording
    {
        int channels = 1, length = 0;
        std::vector<int16_t> data;            // interleaved
        inline float at (int channel, int index) const noexcept
        {
            return (float) data[(size_t) index * (size_t) channels + (size_t) std::min (channel, channels - 1)] * (1.0f / 32768.0f);
        }
    };

    struct Region
    {
        int recording = 0, lokey = 0, hikey = 127, root = 60, lovel = 1, hivel = 127;
        float tuneCents = 0.0f, gainDb = 0.0f;
        int seq = 1, seqLength = 1;
        float release = 0.1f;
        int group = 0, offBy = 0;
        float pan = 0.0f;
    };

    struct Instrument
    {
        juce::String id, name, category, credit;
        bool oneShot = false;
        std::vector<Region> regions;
        std::vector<Recording> recordings;
        std::atomic<bool> ready { false }, failed { false };

        /** The region for a key and velocity (the round robin's turn `turn`), or nullptr. Audio thread, once ready. */
        const Region* pick (int key, int velocity, int turn) const noexcept;
    };

    struct Entry { juce::String id, name, category; };

    /** Where the library is (the first that exists: next to the app, then your Music folder); a non-existent file if none. */
    juce::File folder();
    /** The instruments the library holds, in their fixed order (empty without the library). */
    const std::vector<Entry>& catalogue();
    /** Starts loading an instrument (message thread); its pointer stays valid for the app's life. nullptr if there's none. */
    const Instrument* request (int index);
    /** The instrument if it has been requested (any thread), else nullptr. */
    const Instrument* find (int index) noexcept;
    /** Waits until an instrument has loaded (tests and offline rendering). */
    bool waitUntilLoaded (int index, int timeoutMs = 30000);
    /** The index of an instrument by id, or -1. */
    int indexOf (const juce::String& id);
}
