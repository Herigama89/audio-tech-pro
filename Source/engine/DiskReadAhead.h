#pragma once

#include <JuceHeader.h>
#include <functional>
#include <mutex>
#include <vector>
#include "AudioFileCache.h"

namespace riff
{

/**
    For files streamed from disk: a low-priority thread that reads the next
    few seconds of every streamed region ahead of the playhead, so when the
    audio thread gets there the data is already in memory and playback never
    waits on the disk.
*/
class DiskReadAhead final : private juce::Thread
{
public:
    struct Item
    {
        SharedAudio audio;
        double start = 0.0, length = 0.0;   // on the timeline, seconds
        double sourceStart = 0.0;           // where the region starts in the file, in file frames
    };

    explicit DiskReadAhead (std::function<double()> playheadSeconds)
        : juce::Thread ("Disk read-ahead"), playhead (std::move (playheadSeconds)) {}

    ~DiskReadAhead() override { stopThread (2000); }

    /** The streamed regions now (message thread, after each rebuild). */
    void setItems (std::vector<Item> newItems)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            items = std::move (newItems);
        }
        if (! items.empty() && ! isThreadRunning())
            startThread (juce::Thread::Priority::normal);
    }

    /** Touches what plays in [from, from + seconds) now (also used by tests). */
    void readAhead (double from, double seconds)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& item : items)
        {
            const auto a = juce::jmax (from, item.start), b = juce::jmin (from + seconds, item.start + item.length);
            if (a >= b || item.audio == nullptr)
                continue;
            const auto rate = item.audio->sampleRate;
            item.audio->touch ((juce::int64) (item.sourceStart + (a - item.start) * rate), (juce::int64) ((b - a) * rate) + 1);
        }
    }

    static constexpr double secondsAhead = 4.0;

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            readAhead (playhead() - 0.25, secondsAhead);
            wait (25);
        }
    }

    std::function<double()> playhead;
    std::mutex mutex;
    std::vector<Item> items;
};

} // namespace riff
