#pragma once

/**
    The bridge between Audio Tech Pro Console and its virtual ASIO driver (Windows).

    The console owns the real audio interface. Your DAW opens the driver "Audio Tech Pro Console"
    instead, and the two exchange audio through this block of shared memory:

        console -> DAW:  each input's processed (wet) and raw (dry) signal, and the Main mix
        DAW -> console:  the DAW's playback (stereo), which plays in the console's DAW strip

    Each direction is a ring of samples per channel with a running sample count. After every block
    the console signals `tickEventName`; the driver wakes, hands the DAW whole blocks, and writes the
    DAW's output back. So the DAW runs on the console's clock, one block behind it.

    Plain C++ only: this header is shared by the console (JUCE) and the driver (no JUCE).
*/

#include <atomic>
#include <cstdint>

namespace atpbridge
{
    constexpr std::uint32_t magic = 0x42505441;   // "ATPB"
    constexpr std::uint32_t version = 1;

    constexpr int maxToDaw = 64;                  // the DAW's inputs
    constexpr int maxFromDaw = 2;                 // the DAW's outputs (its playback, stereo)
    constexpr int ringSize = 16384;               // samples per channel (a power of two)
    constexpr int nameLength = 32;

    constexpr const wchar_t* mappingName = L"Local\\AudioTechProConsoleBridge";
    constexpr const wchar_t* tickEventName = L"Local\\AudioTechProConsoleBridgeTick";

    struct Shared
    {
        std::uint32_t magic;
        std::uint32_t version;

        // The format, set by the console (configVersion goes up whenever any of it changes)
        std::atomic<std::int32_t> configVersion;
        double sampleRate;
        std::int32_t blockSize;
        std::int32_t numToDaw, numFromDaw;
        std::int32_t inputLatency, outputLatency;   // the console's own (interface + its buffer), in samples
        char toDawNames[maxToDaw][nameLength];
        char fromDawNames[maxFromDaw][nameLength];

        // Running sample counts (only ever go up)
        std::atomic<std::int64_t> toDawWritten;     // by the console
        std::atomic<std::int64_t> fromDawWritten;   // by the driver
        std::atomic<std::int32_t> consoleRunning;   // 1 while the console's audio runs
        std::atomic<std::int32_t> dawRunning;       // 1 while a DAW has the driver started
        std::atomic<std::int64_t> heartbeat;        // the console's blocks so far

        float toDaw[maxToDaw][ringSize];
        float fromDaw[maxFromDaw][ringSize];
    };

    static_assert ((ringSize & (ringSize - 1)) == 0, "ringSize must be a power of two");
    static_assert (std::atomic<std::int64_t>::is_always_lock_free, "the counters must be lock-free across processes");
}
