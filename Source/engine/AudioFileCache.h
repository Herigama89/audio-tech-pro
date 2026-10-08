#pragma once

#include <JuceHeader.h>
#include <map>
#include <memory>

namespace riff
{

/** Decoded audio held in memory, already at the engine's sample rate, always stereo. */
using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

/**
    Loads audio files into memory, converted to stereo at a target sample rate.

    Phase 0 keeps whole files in RAM, which is simple and fine for songs made of
    a few dozen files. Disk streaming arrives with recording in Phase 1.

    Message thread only: loading blocks while it decodes.
*/
class AudioFileCache
{
public:
    explicit AudioFileCache (juce::AudioFormatManager& formats) : formatManager (formats) {}

    /** Returns the decoded audio, loading it if needed; nullptr if the file can't be read. */
    SharedAudio get (const juce::File& file, double targetSampleRate);

    /** Length of the file in seconds (reads only the header); 0 if unreadable. */
    double getLengthSeconds (const juce::File& file);

    /** Drops entries decoded at other sample rates. */
    void purge (double keepSampleRate);

private:
    struct Key
    {
        juce::String path;
        double sampleRate;
        bool operator< (const Key& other) const
        {
            return path != other.path ? path < other.path : sampleRate < other.sampleRate;
        }
    };

    SharedAudio decode (const juce::File& file, double targetSampleRate);

    juce::AudioFormatManager& formatManager;
    std::map<Key, SharedAudio> entries;
    std::map<juce::String, double> lengths;
};

} // namespace riff
