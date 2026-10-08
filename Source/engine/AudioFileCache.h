#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include <deque>
#include <memory>

namespace riff
{

/** Decoded audio held in memory, already at the engine's sample rate, always stereo. */
using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

/**
    Loads audio files into memory, converted to stereo at a target sample rate.

    Files are kept whole in RAM, which is simple and fine for songs made of a
    few dozen files.

    In the app, files decode on a background thread (converting a song's
    sample rate takes a while), so the screen never freezes: get() returns
    nullptr until the file is ready, and a change message goes out when it
    is. Tests use the synchronous mode, where get() decodes on the spot.

    Message thread only (the worker thread is internal).
*/
class AudioFileCache : public juce::ChangeBroadcaster,
                       private juce::AsyncUpdater
{
public:
    explicit AudioFileCache (juce::AudioFormatManager& formats, bool loadInBackground = false);
    ~AudioFileCache() override;

    /** The decoded audio, or nullptr if it's still loading (background mode) or can't be read. */
    SharedAudio get (const juce::File& file, double targetSampleRate);

    /** True while any file is waiting to be decoded or being decoded. */
    bool isLoading() const;

    /** True if this file is waiting to be decoded or being decoded. */
    bool isPending (const juce::File& file) const;

    /** The name of the file being decoded now (for the status line), or empty. */
    juce::String currentlyLoading() const;

    /** Blocks until every queued file is decoded, then makes them available. For tests and tools. */
    bool waitUntilIdle (int timeoutMs);

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

    struct Result { Key key; SharedAudio audio; double lengthSeconds = 0.0; };

    static Result decode (juce::AudioFormatManager& formats, const Key& key);
    void handleAsyncUpdate() override;
    void collectFinished();

    class Worker;

    juce::AudioFormatManager& formatManager;
    std::map<Key, SharedAudio> entries;
    std::set<Key> failed;
    std::map<juce::String, double> lengths;

    // Shared with the worker
    mutable juce::CriticalSection lock;
    std::deque<Key> queue;
    std::set<Key> pending;            // queued or being decoded
    std::vector<Result> finished;
    juce::String loadingName;
    std::unique_ptr<Worker> worker;
};

} // namespace riff
