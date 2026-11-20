/*
    The "Audio Tech Pro Console" ASIO driver (Windows, 64-bit).

    A DAW opens this driver as its audio interface. It has no hardware of its own: Audio Tech Pro
    Console owns the real interface and passes audio through shared memory (see BridgeShared.h):

        DAW inputs:   "IN 1 Wet" .. (each input after its plugins), "IN 1 Dry" .. (each raw input), "Main L/R"
        DAW outputs:  "DAW L/R": the DAW's playback, heard in the console's DAW strip

    The DAW runs on the console's clock: the console signals after every block, and this driver's
    thread then calls the DAW's bufferSwitch. The sample rate and buffer size are the console's.

    Built from Steinberg's ASIO SDK headers as bundled with JUCE (used here under GPL v3, as JUCE is).
*/

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <avrt.h>
#include <olectl.h>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include "../bridge/BridgeShared.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // {1F544FF7-0F4C-4038-A5DA-0E3747CEC171}
    const CLSID clsid = { 0x1f544ff7, 0x0f4c, 0x4038, { 0xa5, 0xda, 0x0e, 0x37, 0x47, 0xce, 0xc1, 0x71 } };
    const wchar_t* clsidString = L"{1F544FF7-0F4C-4038-A5DA-0E3747CEC171}";
    const wchar_t* driverKeyName = L"Audio Tech Pro Console";
    const char* driverName = "Audio Tech Pro Console";

    HMODULE moduleHandle = nullptr;
    std::atomic<long> objectCount { 0 }, lockCount { 0 };

    ASIOSamples toSamples (long long v)     { ASIOSamples s; s.hi = (unsigned long) (v >> 32); s.lo = (unsigned long) (v & 0xffffffff); return s; }
    ASIOTimeStamp toStamp (long long v)     { ASIOTimeStamp s; s.hi = (unsigned long) (v >> 32); s.lo = (unsigned long) (v & 0xffffffff); return s; }

    long long nowNanoseconds()
    {
        return (long long) timeGetTime() * 1000000LL;   // (the ASIO spec: derived from timeGetTime())
    }

    bool isInsideTheConsole()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW (nullptr, path, MAX_PATH);
        std::wstring p (path);
        std::transform (p.begin(), p.end(), p.begin(), ::towlower);
        return p.find (L"audio tech pro console") != std::wstring::npos;
    }
}

//==============================================================================
class ConsoleAsio final : public IASIO
{
public:
    ConsoleAsio()  { ++objectCount; }
    ~ConsoleAsio() { stop(); disposeBuffers(); closeBridge(); --objectCount; }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        // (ASIO hosts ask for the driver's own CLSID as the interface)
        if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, clsid))
        {
            *out = static_cast<IASIO*> (this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const long left = --refCount;
        if (left == 0)
            delete this;
        return (ULONG) left;
    }

    // IASIO
    ASIOBool init (void*) override
    {
        if (isInsideTheConsole())
        {
            setError ("This driver is for your DAW: in Audio Tech Pro Console, choose your real audio interface.");
            return ASIOFalse;
        }
        if (! openBridge())
        {
            setError ("Open Audio Tech Pro Console first: this driver plays and records through it.");
            return ASIOFalse;
        }
        snapshot();
        if (sampleRate <= 0.0 || blockSize <= 0)
        {
            setError ("Audio Tech Pro Console has no audio interface running: choose one in its Audio Setup.");
            return ASIOFalse;
        }
        return ASIOTrue;
    }

    void getDriverName (char* name) override        { strcpy_s (name, 32, driverName); }
    long getDriverVersion() override                { return 1; }
    void getErrorMessage (char* text) override      { strcpy_s (text, 124, error.c_str()); }

    ASIOError start() override
    {
        if (shared == nullptr || callbacks == nullptr || buffers.empty())
            return ASE_NotPresent;
        if (running.load())
            return ASE_OK;
        samplePosition = 0;
        readPosition = -1;
        timeInfoMode = callbacks->asioMessage != nullptr
                    && callbacks->asioMessage (kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
                    && callbacks->asioMessage (kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
        running = true;
        shared->dawRunning.store (1);
        worker = std::thread ([this] { run(); });
        return ASE_OK;
    }

    ASIOError stop() override
    {
        if (! running.exchange (false))
            return ASE_OK;
        if (tick != nullptr)
            SetEvent (tick);
        if (worker.joinable())
            worker.join();
        if (shared != nullptr)
            shared->dawRunning.store (0);
        return ASE_OK;
    }

    ASIOError getChannels (long* ins, long* outs) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        if (ins != nullptr)  *ins = numIn;
        if (outs != nullptr) *outs = numOut;
        return ASE_OK;
    }

    ASIOError getLatencies (long* in, long* out) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        // (the console's own latency, plus the block this bridge adds)
        if (in != nullptr)  *in = blockSize + consoleInLatency;
        if (out != nullptr) *out = blockSize + consoleOutLatency;
        return ASE_OK;
    }

    ASIOError getBufferSize (long* minSize, long* maxSize, long* preferred, long* granularity) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        *minSize = *maxSize = *preferred = blockSize;   // (set in the console's Audio Setup)
        *granularity = 0;
        return ASE_OK;
    }

    ASIOError canSampleRate (ASIOSampleRate rate) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        return std::abs (rate - sampleRate) < 1.0 ? ASE_OK : ASE_NoClock;
    }
    ASIOError getSampleRate (ASIOSampleRate* rate) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        *rate = sampleRate;
        return ASE_OK;
    }
    ASIOError setSampleRate (ASIOSampleRate rate) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        if (rate == 0.0)   // (asks for external sync: we always are, to the console)
            return ASE_OK;
        return std::abs (rate - sampleRate) < 1.0 ? ASE_OK : ASE_NoClock;
    }

    ASIOError getClockSources (ASIOClockSource* clocks, long* numSources) override
    {
        if (clocks == nullptr || numSources == nullptr || *numSources < 1)
            return ASE_InvalidParameter;
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = ASIOTrue;
        strcpy_s (clocks[0].name, 32, "Audio Tech Pro Console");
        *numSources = 1;
        return ASE_OK;
    }
    ASIOError setClockSource (long reference) override  { return reference == 0 ? ASE_OK : ASE_InvalidParameter; }

    ASIOError getSamplePosition (ASIOSamples* pos, ASIOTimeStamp* stamp) override
    {
        if (! running.load()) return ASE_NotPresent;
        *pos = toSamples (lastPosition.load());
        *stamp = toStamp (lastStamp.load());
        return ASE_OK;
    }

    ASIOError getChannelInfo (ASIOChannelInfo* info) override
    {
        if (shared == nullptr || info == nullptr) return ASE_NotPresent;
        const long count = info->isInput ? numIn : numOut;
        if (info->channel < 0 || info->channel >= count)
            return ASE_InvalidParameter;
        info->type = ASIOSTFloat32LSB;
        info->channelGroup = 0;
        info->isActive = ASIOFalse;
        for (auto& b : bufferInfos)
            if (b.isInput == info->isInput && b.channelNum == info->channel)
                info->isActive = ASIOTrue;
        strcpy_s (info->name, 32, info->isInput ? inNames[(size_t) info->channel].c_str() : outNames[(size_t) info->channel].c_str());
        return ASE_OK;
    }

    ASIOError createBuffers (ASIOBufferInfo* infos, long numChannels, long bufferSize, ASIOCallbacks* cb) override
    {
        if (shared == nullptr) return ASE_NotPresent;
        if (running.load()) return ASE_InvalidMode;
        if (bufferSize != blockSize)
        {
            setError ("Set the buffer size in Audio Tech Pro Console's Audio Setup.");
            return ASE_InvalidMode;
        }
        disposeBuffers();
        for (long i = 0; i < numChannels; ++i)
        {
            const auto& r = infos[i];
            if (r.channelNum < 0 || r.channelNum >= (r.isInput ? numIn : numOut))
            {
                disposeBuffers();
                return ASE_InvalidParameter;
            }
        }
        buffers.resize ((size_t) numChannels * 2);
        for (auto& b : buffers)
            b.assign ((size_t) blockSize, 0.0f);
        bufferInfos.assign (infos, infos + numChannels);
        for (long i = 0; i < numChannels; ++i)
        {
            infos[i].buffers[0] = buffers[(size_t) i * 2].data();
            infos[i].buffers[1] = buffers[(size_t) i * 2 + 1].data();
            bufferInfos[(size_t) i] = infos[i];
        }
        callbacks = cb;
        return ASE_OK;
    }

    ASIOError disposeBuffers() override
    {
        stop();
        buffers.clear();
        bufferInfos.clear();
        callbacks = nullptr;
        return ASE_OK;
    }

    ASIOError controlPanel() override
    {
        std::string text = "Audio Tech Pro Console ASIO driver\n\n";
        if (shared != nullptr)
            text += "Running at " + std::to_string ((int) sampleRate) + " Hz, " + std::to_string (blockSize) + " samples.\n"
                  + std::to_string (numIn) + " inputs (each input wet and dry, and the Main mix), "
                  + std::to_string (numOut) + " outputs (your DAW's playback).\n\n";
        text += "The sample rate, buffer size and audio interface are set in Audio Tech Pro Console > Audio Setup.";
        MessageBoxA (nullptr, text.c_str(), driverName, MB_OK | MB_ICONINFORMATION);
        return ASE_OK;
    }

    ASIOError future (long, void*) override
    {
        return ASE_InvalidParameter;   // (no optional features: input monitoring is the console's job)
    }

    ASIOError outputReady() override { return ASE_NotPresent; }

private:
    bool openBridge()
    {
        if (shared != nullptr)
            return true;
        mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, atpbridge::mappingName);
        if (mapping == nullptr)
            return false;
        shared = static_cast<atpbridge::Shared*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (atpbridge::Shared)));
        if (shared == nullptr || shared->magic != atpbridge::magic || shared->version != atpbridge::version)
        {
            closeBridge();
            return false;
        }
        tick = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, atpbridge::tickEventName);
        return true;   // (without the event it polls)
    }

    void closeBridge()
    {
        if (shared != nullptr) { UnmapViewOfFile (shared); shared = nullptr; }
        if (mapping != nullptr) { CloseHandle (mapping); mapping = nullptr; }
        if (tick != nullptr)    { CloseHandle (tick); tick = nullptr; }
    }

    void snapshot()
    {
        configVersion = shared->configVersion.load();
        sampleRate = shared->sampleRate;
        blockSize = shared->blockSize;
        numIn = std::clamp<long> (shared->numToDaw, 0, atpbridge::maxToDaw);
        numOut = std::clamp<long> (shared->numFromDaw, 0, atpbridge::maxFromDaw);
        consoleInLatency = shared->inputLatency;
        consoleOutLatency = shared->outputLatency;
        inNames.clear();
        outNames.clear();
        for (long i = 0; i < numIn; ++i)
            inNames.emplace_back (std::string (shared->toDawNames[i], strnlen (shared->toDawNames[i], atpbridge::nameLength)));
        for (long i = 0; i < numOut; ++i)
            outNames.emplace_back (std::string (shared->fromDawNames[i], strnlen (shared->fromDawNames[i], atpbridge::nameLength)));
    }

    void setError (const char* text) { error = text; }

    void run()
    {
        DWORD taskIndex = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
        if (task == nullptr)
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        int index = 0;
        bool resetAsked = false;
        while (running.load())
        {
            if (tick != nullptr)
                WaitForSingleObject (tick, 50);
            else
                Sleep (1);
            if (! running.load())
                break;

            // The console changed its format (or quit): ask the DAW to open the driver again
            if (shared->configVersion.load() != configVersion || shared->consoleRunning.load() == 0)
            {
                if (! resetAsked && shared->configVersion.load() != configVersion && callbacks->asioMessage != nullptr)
                {
                    resetAsked = true;
                    callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr);
                }
                continue;
            }

            const long long written = shared->toDawWritten.load (std::memory_order_acquire);
            if (readPosition < 0 || written - readPosition > atpbridge::ringSize / 2 || readPosition > written)
                readPosition = std::max (0LL, written - (long long) blockSize);   // (start, or catch up: one block behind)

            while (running.load() && written - readPosition >= blockSize)
            {
                process (index, readPosition);
                index ^= 1;
                readPosition += blockSize;
            }
        }

        if (task != nullptr)
            AvRevertMmThreadCharacteristics (task);
    }

    void process (int index, long long position)
    {
        const int start = (int) (position & (atpbridge::ringSize - 1));
        const int first = std::min (blockSize, atpbridge::ringSize - start);

        // The console's channels into the DAW's input buffers
        for (size_t i = 0; i < bufferInfos.size(); ++i)
        {
            const auto& b = bufferInfos[i];
            if (b.isInput != ASIOTrue)
                continue;
            auto* dest = static_cast<float*> (b.buffers[index]);
            const float* src = shared->toDaw[b.channelNum];
            std::memcpy (dest, src + start, sizeof (float) * (size_t) first);
            if (blockSize > first)
                std::memcpy (dest + first, src, sizeof (float) * (size_t) (blockSize - first));
        }

        lastPosition = samplePosition;
        lastStamp = nowNanoseconds();

        if (timeInfoMode && callbacks->bufferSwitchTimeInfo != nullptr)
        {
            ASIOTime time {};
            time.timeInfo.speed = 1.0;
            time.timeInfo.systemTime = toStamp (lastStamp.load());
            time.timeInfo.samplePosition = toSamples (samplePosition);
            time.timeInfo.sampleRate = sampleRate;
            time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
            callbacks->bufferSwitchTimeInfo (&time, index, ASIOTrue);
        }
        else
        {
            callbacks->bufferSwitch (index, ASIOTrue);
        }
        samplePosition += blockSize;

        // The DAW's output buffers into the console's DAW strip
        const long long out = shared->fromDawWritten.load (std::memory_order_relaxed);
        const int outStart = (int) (out & (atpbridge::ringSize - 1));
        const int outFirst = std::min (blockSize, atpbridge::ringSize - outStart);
        for (long c = 0; c < numOut; ++c)
        {
            const float* src = nullptr;
            for (const auto& b : bufferInfos)
                if (b.isInput != ASIOTrue && b.channelNum == c)
                    src = static_cast<const float*> (b.buffers[index]);
            float* dest = shared->fromDaw[c];
            if (src == nullptr)
            {
                std::memset (dest + outStart, 0, sizeof (float) * (size_t) outFirst);
                if (blockSize > outFirst)
                    std::memset (dest, 0, sizeof (float) * (size_t) (blockSize - outFirst));
                continue;
            }
            std::memcpy (dest + outStart, src, sizeof (float) * (size_t) outFirst);
            if (blockSize > outFirst)
                std::memcpy (dest, src + outFirst, sizeof (float) * (size_t) (blockSize - outFirst));
        }
        shared->fromDawWritten.store (out + blockSize, std::memory_order_release);
    }

    std::atomic<long> refCount { 1 };
    std::string error = "Open Audio Tech Pro Console first.";

    HANDLE mapping = nullptr, tick = nullptr;
    atpbridge::Shared* shared = nullptr;
    std::int32_t configVersion = 0;
    double sampleRate = 0.0;
    int blockSize = 0;
    long numIn = 0, numOut = 0;
    int consoleInLatency = 0, consoleOutLatency = 0;
    std::vector<std::string> inNames, outNames;

    std::vector<std::vector<float>> buffers;
    std::vector<ASIOBufferInfo> bufferInfos;
    ASIOCallbacks* callbacks = nullptr;
    bool timeInfoMode = false;

    std::atomic<bool> running { false };
    std::thread worker;
    long long readPosition = -1, samplePosition = 0;
    std::atomic<long long> lastPosition { 0 }, lastStamp { 0 };
};

//==============================================================================
class Factory final : public IClassFactory
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override
    {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IClassFactory))
        {
            *out = static_cast<IClassFactory*> (this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateInstance (IUnknown* outer, REFIID riid, void** out) override
    {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        auto* driver = new (std::nothrow) ConsoleAsio();
        if (driver == nullptr) return E_OUTOFMEMORY;
        const auto result = driver->QueryInterface (riid, out);
        driver->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer (BOOL lock) override
    {
        if (lock) ++lockCount; else --lockCount;
        return S_OK;
    }
};

static Factory factory;

//==============================================================================
namespace
{
    bool setKey (HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW (root, path.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        const auto ok = RegSetValueExW (key, name, 0, REG_SZ, reinterpret_cast<const BYTE*> (value.c_str()),
                                        (DWORD) ((value.size() + 1) * sizeof (wchar_t))) == ERROR_SUCCESS;
        RegCloseKey (key);
        return ok;
    }
}

extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        moduleHandle = instance;
        DisableThreadLibraryCalls (instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject (REFCLSID requested, REFIID riid, LPVOID* out)
{
    if (! IsEqualCLSID (requested, clsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory.QueryInterface (riid, out);
}

STDAPI DllCanUnloadNow()
{
    return (objectCount.load() == 0 && lockCount.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW (moduleHandle, path, MAX_PATH) == 0)
        return SELFREG_E_CLASS;

    const std::wstring classKey = std::wstring (L"CLSID\\") + clsidString;
    bool ok = setKey (HKEY_CLASSES_ROOT, classKey, nullptr, L"Audio Tech Pro Console ASIO Driver")
           && setKey (HKEY_CLASSES_ROOT, classKey + L"\\InprocServer32", nullptr, path)
           && setKey (HKEY_CLASSES_ROOT, classKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    const std::wstring asioKey = std::wstring (L"SOFTWARE\\ASIO\\") + driverKeyName;
    ok = ok && setKey (HKEY_LOCAL_MACHINE, asioKey, L"CLSID", clsidString)
            && setKey (HKEY_LOCAL_MACHINE, asioKey, L"Description", driverKeyName);
    return ok ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer()
{
    const std::wstring classKey = std::wstring (L"CLSID\\") + clsidString;
    RegDeleteTreeW (HKEY_CLASSES_ROOT, classKey.c_str());
    HKEY asio = nullptr;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &asio) == ERROR_SUCCESS)
    {
        RegDeleteTreeW (asio, driverKeyName);
        RegCloseKey (asio);
    }
    return S_OK;
}
