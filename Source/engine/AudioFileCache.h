#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include <deque>
#include <memory>

namespace riff
{

/**
    A file ready to play, at the file's own sample rate, always read as stereo.

    Most files are decoded into memory. Long WAV and AIFF files (recordings,
    stems, whole mixes) are streamed from disk instead: the file is mapped,
    the playback reads straight from it, and a read-ahead thread brings in
    what's about to play (see DiskReadAhead), so a long session doesn't fill
    the computer's memory and opens at once.
*/
struct DecodedAudio
{
    juce::AudioBuffer<float> buffer;   // empty when streamed
    double sampleRate = 44100.0;

    std::shared_ptr<juce::MemoryMappedAudioFormatReader> streamed;
    juce::int64 streamedFrames = 0;
    int streamedChannels = 2;

    bool isStreamed() const noexcept           { return streamed != nullptr; }
    juce::int64 numFrames() const noexcept     { return streamed != nullptr ? streamedFrames : (juce::int64) buffer.getNumSamples(); }

    /** Copies frames [start, start + num) into left and right; silence outside the file. Real-time safe. */
    void readStereo (juce::int64 start, int num, float* left, float* right) const noexcept;

    /** Brings frames into memory ahead of time (for streamed files; a no-op otherwise). */
    void touch (juce::int64 start, juce::int64 num) const noexcept;
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

    /** Audio made in memory for a file (the pitch editor's live render): it plays at once, and whoever made it may
        change its samples as it plays (same length), to be heard straight away. Tells the listeners. */
    void put (const juce::File& file, SharedAudio audio);

    /** Forgets a file (so it's read again next time), e.g. after it was re-recorded. */
    void forget (const juce::File& file);

    /** WAV and AIFF files bigger than this (as decoded stereo floats) stream from disk. */
    void setStreamingThreshold (juce::int64 bytes) noexcept { streamAbove.store (bytes); }
    static constexpr juce::int64 defaultStreamingThreshold = 96 * 1024 * 1024;   // about 4 minutes of stereo at 48 kHz

private:
    using Key = juce::String;   // the file's full path

    struct Result { Key key; SharedAudio audio; double lengthSeconds = 0.0; };

    static Result decode (juce::AudioFormatManager& formats, const Key& key, juce::int64 streamAboveBytes);
    std::atomic<juce::int64> streamAbove { defaultStreamingThreshold };
    void handleAsyncUpdate() override;
    void collectFinished();

    class Worker;

    juce::AudioFormatManager& formatManager;
    std::map<Key, SharedAudio> entries;
    std::set<Key> failed;
    std::set<Key> provided;   // made in memory (put): a slower read from disk doesn't replace them
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
