#include "BridgeServer.h"
#include "BridgeShared.h"
#include <algorithm>
#include <cstring>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace atpbridge
{

BridgeServer::~BridgeServer()
{
    close();
}

#if defined (_WIN32)

bool BridgeServer::open()
{
    if (shared != nullptr)
        return true;

    const auto size = (DWORD) sizeof (Shared);
    auto* handle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, mappingName);
    if (handle == nullptr)
        return false;
    const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;

    auto* view = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (Shared));
    if (view == nullptr)
    {
        CloseHandle (handle);
        return false;
    }

    mapping = handle;
    shared = static_cast<Shared*> (view);
    if (! existed)
        std::memset (shared, 0, sizeof (Shared));   // (a new mapping is zeroed anyway: this is for clarity)
    tick = CreateEventW (nullptr, FALSE, FALSE, tickEventName);

    shared->version = version;
    shared->magic = magic;
    return true;
}

void BridgeServer::close()
{
    if (shared != nullptr)
    {
        shared->consoleRunning.store (0);
        shared->configVersion.fetch_add (1);
        UnmapViewOfFile (shared);
        shared = nullptr;
    }
    if (tick != nullptr)   { SetEvent ((HANDLE) tick); CloseHandle ((HANDLE) tick); tick = nullptr; }
    if (mapping != nullptr) { CloseHandle ((HANDLE) mapping); mapping = nullptr; }
}

void BridgeServer::configure (double sampleRate, int blockSize, int inputLatency, int outputLatency,
                              const std::vector<std::string>& toDawNames, const std::vector<std::string>& fromDawNames)
{
    if (shared == nullptr)
        return;

    toDaw = std::min ((int) toDawNames.size(), maxToDaw);
    fromDaw = std::min ((int) fromDawNames.size(), maxFromDaw);
    block = blockSize;

    auto copyName = [] (char* dest, const std::string& name)
    {
        std::memset (dest, 0, nameLength);
        std::strncpy (dest, name.c_str(), nameLength - 1);
    };

    const bool changed = shared->sampleRate != sampleRate || shared->blockSize != blockSize
                      || shared->numToDaw != toDaw || shared->numFromDaw != fromDaw
                      || shared->inputLatency != inputLatency || shared->outputLatency != outputLatency;

    shared->sampleRate = sampleRate;
    shared->blockSize = blockSize;
    shared->numToDaw = toDaw;
    shared->numFromDaw = fromDaw;
    shared->inputLatency = inputLatency;
    shared->outputLatency = outputLatency;
    bool namesChanged = false;
    for (int i = 0; i < toDaw; ++i)
    {
        char name[nameLength];
        copyName (name, toDawNames[(size_t) i]);
        namesChanged = namesChanged || std::memcmp (name, shared->toDawNames[i], nameLength) != 0;
        std::memcpy (shared->toDawNames[i], name, nameLength);
    }
    for (int i = 0; i < fromDaw; ++i)
        copyName (shared->fromDawNames[i], fromDawNames[(size_t) i]);

    if (changed || namesChanged)
        shared->configVersion.fetch_add (1);   // (a running DAW is asked to reset)
    readFromDawPos = -1;
}

void BridgeServer::setRunning (bool running)
{
    if (shared != nullptr)
        shared->consoleRunning.store (running ? 1 : 0);
}

bool BridgeServer::isDawConnected() const noexcept
{
    return shared != nullptr && shared->dawRunning.load() != 0;
}

void BridgeServer::beginBlock (int n) noexcept
{
    if (shared == nullptr || n <= 0)
        return;
    const auto start = (int) (shared->toDawWritten.load (std::memory_order_relaxed) & (ringSize - 1));
    const int first = std::min (n, ringSize - start);
    for (int c = 0; c < toDaw; ++c)
    {
        std::memset (shared->toDaw[c] + start, 0, sizeof (float) * (size_t) first);
        if (n > first)
            std::memset (shared->toDaw[c], 0, sizeof (float) * (size_t) (n - first));
    }
}

void BridgeServer::write (int channel, const float* samples, int n) noexcept
{
    if (shared == nullptr || samples == nullptr || channel < 0 || channel >= toDaw || n <= 0)
        return;
    const auto start = (int) (shared->toDawWritten.load (std::memory_order_relaxed) & (ringSize - 1));
    const int first = std::min (n, ringSize - start);
    std::memcpy (shared->toDaw[channel] + start, samples, sizeof (float) * (size_t) first);
    if (n > first)
        std::memcpy (shared->toDaw[channel], samples + first, sizeof (float) * (size_t) (n - first));
}

bool BridgeServer::readFromDaw (float* const* dest, int numChannels, int n) noexcept
{
    auto silence = [&]
    {
        for (int c = 0; c < numChannels; ++c)
            if (dest[c] != nullptr)
                std::memset (dest[c], 0, sizeof (float) * (size_t) n);
        return false;
    };
    if (shared == nullptr || shared->dawRunning.load (std::memory_order_relaxed) == 0)
        return silence();

    const auto written = shared->fromDawWritten.load (std::memory_order_acquire);
    // Starting (or after falling far behind): just one block behind the DAW
    if (readFromDawPos < 0 || written - readFromDawPos > (long long) std::max (4 * std::max (block, n), 2048)
        || readFromDawPos > written)
        readFromDawPos = std::max (0LL, written - (long long) n);
    if (written - readFromDawPos < n)
        return silence();   // (the DAW hasn't delivered this block yet)

    const auto start = (int) (readFromDawPos & (ringSize - 1));
    const int first = std::min (n, ringSize - start);
    for (int c = 0; c < numChannels; ++c)
    {
        if (dest[c] == nullptr)
            continue;
        if (c >= fromDaw)
        {
            std::memset (dest[c], 0, sizeof (float) * (size_t) n);
            continue;
        }
        std::memcpy (dest[c], shared->fromDaw[c] + start, sizeof (float) * (size_t) first);
        if (n > first)
            std::memcpy (dest[c] + first, shared->fromDaw[c], sizeof (float) * (size_t) (n - first));
    }
    readFromDawPos += n;
    return true;
}

void BridgeServer::endBlock (int n) noexcept
{
    if (shared == nullptr || n <= 0)
        return;
    shared->toDawWritten.fetch_add (n, std::memory_order_release);
    shared->heartbeat.fetch_add (1, std::memory_order_relaxed);
    if (tick != nullptr)
        SetEvent ((HANDLE) tick);
}

#else   // (no bridge on this system yet)

bool BridgeServer::open()                    { return false; }
void BridgeServer::close()                   {}
void BridgeServer::configure (double, int, int, int, const std::vector<std::string>&, const std::vector<std::string>&) {}
void BridgeServer::setRunning (bool)         {}
bool BridgeServer::isDawConnected() const noexcept { return false; }
void BridgeServer::beginBlock (int) noexcept {}
void BridgeServer::write (int, const float*, int) noexcept {}
bool BridgeServer::readFromDaw (float* const* dest, int numChannels, int n) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        if (dest[c] != nullptr)
            std::memset (dest[c], 0, sizeof (float) * (size_t) n);
    return false;
}
void BridgeServer::endBlock (int) noexcept   {}

#endif

}
