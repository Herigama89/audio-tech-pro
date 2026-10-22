#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace riff
{

/**
    Helpers for the audio thread, so a song's tracks are processed on several cores at once (as Logic's and
    Pro Tools' engines do): at small buffer sizes one core can't get through every track's plug-ins in time, but
    the machine's other cores can share them.

    Realtime threads (joined to the audio device's workgroup on macOS, so the system schedules them with the
    audio thread); run() hands out jobs, does some itself and returns when all are done. Nothing allocates or
    locks on the way. Workers spin briefly for the next block, then sleep.
*/
class RenderThreads
{
public:
    using Job = void (*) (void* context, int index, int thread) noexcept;

    RenderThreads();
    ~RenderThreads();

    /** (Re)starts with `workers` helper threads (0: everything on the calling thread). */
    void start (int workers, double sampleRate, int blockSize);
    void stop();
    int workers() const noexcept { return (int) threads.size(); }

    /** The audio device's workgroup, for the workers to join (macOS; ignored elsewhere). */
    void setWorkgroup (const juce::AudioWorkgroup& group);

    /** Runs job(context, i, thread) for every i in [0, count): thread 0 is the caller, 1.. the workers.
        Returns once every job has finished. Real-time safe. */
    void run (int count, Job job, void* context) noexcept;

    /** How many workers to use on this computer: its cores but one (the audio thread's), at most 7. */
    static int suggestedWorkers();

private:
    class Worker;
    void work (int thread) noexcept;

    std::vector<std::unique_ptr<Worker>> threads;
    std::atomic<int> next { 1 << 30 }, total { 0 }, done { 0 };
    std::atomic<juce::uint32> generation { 0 };
    std::atomic<Job> job { nullptr };
    std::atomic<void*> context { nullptr };

    std::mutex groupLock;
    juce::AudioWorkgroup group;
    std::atomic<juce::uint32> groupVersion { 0 };
};

} // namespace riff
