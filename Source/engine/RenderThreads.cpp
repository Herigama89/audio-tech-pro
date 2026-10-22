#include "RenderThreads.h"
#include <thread>

#if JUCE_INTEL
 #include <immintrin.h>
#endif

namespace riff
{
namespace
{
    inline void pause() noexcept
    {
       #if JUCE_INTEL
        _mm_pause();
       #elif JUCE_ARM && (JUCE_MAC || JUCE_IOS)
        __asm__ __volatile__ ("yield");
       #else
        std::this_thread::yield();
       #endif
    }
}

class RenderThreads::Worker final : public juce::Thread
{
public:
    Worker (RenderThreads& o, int i) : juce::Thread ("Audio Tech Pro render " + juce::String (i)), owner (o), index (i) {}

    void wake() noexcept
    {
        if (sleeping.load())
            event.signal();
    }

    void run() override
    {
        juce::FloatVectorOperations::disableDenormalisedNumberSupport (true);   // (as the audio thread: no slow denormals)
        juce::WorkgroupToken token;
        juce::uint32 joinedVersion = 0, seen = owner.generation.load (std::memory_order_acquire);
        while (! threadShouldExit())
        {
            if (const auto v = owner.groupVersion.load (std::memory_order_acquire); v != joinedVersion)
            {
                joinedVersion = v;
                std::lock_guard<std::mutex> g (owner.groupLock);
                if (owner.group)
                    owner.group.join (token);
            }

            // The next block's jobs: spin a moment (a block is due soon), then sleep until woken
            bool got = false;
            const auto spinUntil = juce::Time::getHighResolutionTicks() + juce::Time::secondsToHighResolutionTicks (0.0003);
            while (! got && juce::Time::getHighResolutionTicks() < spinUntil)
            {
                got = owner.generation.load (std::memory_order_acquire) != seen;
                if (! got)
                    pause();
            }
            if (! got)
            {
                sleeping.store (true);
                while (! threadShouldExit() && owner.generation.load() == seen)
                    event.wait (20.0);
                sleeping.store (false);
            }
            seen = owner.generation.load (std::memory_order_acquire);
            owner.work (index);
        }
    }

private:
    RenderThreads& owner;
    const int index;
    juce::WaitableEvent event;
    std::atomic<bool> sleeping { false };
};

RenderThreads::RenderThreads() = default;
RenderThreads::~RenderThreads() { stop(); }

int RenderThreads::suggestedWorkers()
{
    return juce::jlimit (0, 7, juce::SystemStats::getNumPhysicalCpus() - 1);
}

void RenderThreads::start (int workers, double sampleRate, int blockSize)
{
    stop();
    for (int i = 0; i < workers; ++i)
    {
        threads.push_back (std::make_unique<Worker> (*this, i + 1));
        const auto options = juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime (juce::jmax (32, blockSize),
                                                                                                juce::jmax (8000.0, sampleRate));
        if (! threads.back()->startRealtimeThread (options))
            threads.back()->startThread (juce::Thread::Priority::highest);
    }
}

void RenderThreads::stop()
{
    for (auto& t : threads)
        t->signalThreadShouldExit();
    for (auto& t : threads)
    {
        t->wake();
        t->stopThread (2000);
    }
    threads.clear();
}

void RenderThreads::setWorkgroup (const juce::AudioWorkgroup& g)
{
    {
        std::lock_guard<std::mutex> lock (groupLock);
        group = g;
    }
    groupVersion.fetch_add (1, std::memory_order_acq_rel);
    for (auto& t : threads)
        t->wake();
}

void RenderThreads::work (int thread) noexcept
{
    for (;;)
    {
        const auto i = next.fetch_add (1, std::memory_order_acq_rel);
        if (i >= total.load (std::memory_order_acquire))
            return;
        job.load (std::memory_order_acquire) (context.load (std::memory_order_acquire), i, thread);
        done.fetch_add (1, std::memory_order_acq_rel);
    }
}

void RenderThreads::run (int count, Job j, void* ctx) noexcept
{
    if (count <= 0)
        return;
    if (threads.empty() || count == 1)
    {
        for (int i = 0; i < count; ++i)
            j (ctx, i, 0);
        return;
    }
    // (late workers from the last run find nothing to take while this is set up)
    next.store (1 << 30, std::memory_order_release);
    done.store (0, std::memory_order_release);
    total.store (count, std::memory_order_release);
    job.store (j, std::memory_order_release);
    context.store (ctx, std::memory_order_release);
    next.store (0, std::memory_order_release);
    generation.fetch_add (1, std::memory_order_acq_rel);
    for (auto& t : threads)
        t->wake();

    work (0);
    while (done.load (std::memory_order_acquire) < count)
        pause();
    next.store (1 << 30, std::memory_order_release);
}

} // namespace riff
