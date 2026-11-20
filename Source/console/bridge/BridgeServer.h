#pragma once

#include <string>
#include <vector>

namespace atpbridge
{
    struct Shared;

    /**
        The console's side of the bridge to the virtual ASIO driver (Windows; does nothing elsewhere).

        Message thread: open(), configure(), close().
        Audio thread, once per block: beginBlock(), write() for each channel, readFromDaw(), endBlock().
    */
    class BridgeServer
    {
    public:
        BridgeServer() = default;
        ~BridgeServer();

        /** Makes the shared memory the driver opens. False where there's no bridge (not Windows). */
        bool open();
        void close();
        bool isOpen() const noexcept { return shared != nullptr; }

        /** The format the DAW will see. Changing it makes a running DAW reset its audio. */
        void configure (double sampleRate, int blockSize, int inputLatency, int outputLatency,
                        const std::vector<std::string>& toDawNames, const std::vector<std::string>& fromDawNames);

        void setRunning (bool running);

        /** True while a DAW has the driver running. */
        bool isDawConnected() const noexcept;

        // ---- audio thread ----
        void beginBlock (int numSamples) noexcept;                       // (clears this block in every channel)
        void write (int channel, const float* samples, int numSamples) noexcept;
        /** The DAW's playback for this block; false (and silence) if it hasn't any ready. */
        bool readFromDaw (float* const* dest, int numChannels, int numSamples) noexcept;
        void endBlock (int numSamples) noexcept;                         // (and wakes the driver)

        int numToDaw() const noexcept   { return toDaw; }
        int numFromDaw() const noexcept { return fromDaw; }

    private:
        Shared* shared = nullptr;
        [[maybe_unused]] void* mapping = nullptr;
        [[maybe_unused]] void* tick = nullptr;
        int toDaw = 0, fromDaw = 0;
        [[maybe_unused]] int block = 0;
        [[maybe_unused]] long long readFromDawPos = -1;
    };
}
