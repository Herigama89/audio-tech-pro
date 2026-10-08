#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include <deque>
#include <memory>

namespace riff
{

/** A decoded file in memory, at the file's own sample rate, always stereo. */
struct DecodedAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};

using SharedAudio = std::shared_ptr<const DecodedAudio>;

/**
    Loads audio files into memory as they are: no sample-rate conversion here.
    A file at another rate than the session is converted while it plays (see
    Resampler.h), as other DAWs do, so importing is as quick as reading the
    file, and changing the interface's sample rate reloads nothing.

    In the app, files are read on a background thread so the screen never
    waits: get() returns nullptr until the file is ready, and a change message
    goes out when it is. Tests use the synchronous mode, where get() reads on
    the spot.

    Message thread only (the worker thread is internal).
*/
class AudioFileCache : public juce::ChangeBroadcaster,
                       private juce::AsyncUpdater
{
public:
    explicit AudioFileCache (juce::AudioFormatManager& formats, bool loadInBackground = false);
    ~AudioFileCache() override;

    /** The decoded audio, or nullptr if it's still loading (background mode) or can't be read. */
    SharedAudio get (const juce::File& file);

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

    /** Forgets a file (so it's read again next time), e.g. after it was re-recorded. */
    void forget (const juce::File& file);

private:
    using Key = juce::String;   // the file's full path

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
