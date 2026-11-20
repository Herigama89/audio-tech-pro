/*
    The "Audio Tech Pro Console" audio device (macOS): a Core Audio driver (an AudioServerPlugIn,
    installed in /Library/Audio/Plug-Ins/HAL) that your DAW opens as its audio interface.

    It has no hardware: it's a loopback. What any app plays into an output channel comes back on
    the same input channel. Audio Tech Pro Console opens it together with your real interface (as
    one combined device) and the two sides use different channels:

        channel  1-2    "DAW L/R"     the DAW plays here; the console hears it in its DAW strip
        channel  3-4    "Main L/R"    the console's Main mix, for the DAW to record
        channel  5-35   "IN n Wet"    each input after its plugins
        channel 36-66   "IN n Dry"    each raw input

    (DAW L/R come first so a DAW's default output - the first pair - is the right one.)

    Written for Audio Tech Pro; the structure follows Apple's AudioServerPlugIn samples and the
    BlackHole loopback driver (GPL v3).
*/

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // ---- the layout (keep in step with ConsoleEngine) ------------------------------------------
    constexpr UInt32 numStrips = 31;
    constexpr UInt32 numChannels = 4 + 2 * numStrips;   // 66
    constexpr UInt32 ringFrames = 16384;
    constexpr Float64 sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr UInt32 numSampleRates = sizeof (sampleRates) / sizeof (sampleRates[0]);

    // ---- objects -------------------------------------------------------------------------------
    enum : AudioObjectID
    {
        plugInID = kAudioObjectPlugInObject,
        deviceID = 2,
        inputStreamID = 3,
        outputStreamID = 4
    };

    const char* deviceName = "Audio Tech Pro Console";
    const char* manufacturer = "Audio Tech Pro";
    const char* deviceUID = "com.audiotechpro.console.device";
    const char* modelUID = "com.audiotechpro.console.model";

    // ---- state ---------------------------------------------------------------------------------
    pthread_mutex_t stateMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t ioMutex = PTHREAD_MUTEX_INITIALIZER;
    AudioServerPlugInHostRef host = nullptr;
    std::atomic<UInt32> refCount { 0 };

    Float64 sampleRate = 48000.0;
    UInt64 ioRunning = 0;
    Float64 hostTicksPerFrame = 0.0;
    UInt64 anchorHostTime = 0;
    UInt64 numberTimeStamps = 0;
    bool inputActive = true, outputActive = true;

    Float32* ring = nullptr;                // interleaved, numChannels per frame
    Float64 lastWrittenSampleTime = -1.0;   // (the end of the latest mixed output)
    bool ringClear = true;

    // ---- helpers -------------------------------------------------------------------------------
    CFStringRef cf (const char* text)  { return CFStringCreateWithCString (nullptr, text, kCFStringEncodingUTF8); }

    void computeTicks()
    {
        mach_timebase_info_data_t info;
        mach_timebase_info (&info);
        const Float64 hostClock = (Float64) info.denom / (Float64) info.numer * 1000000000.0;
        hostTicksPerFrame = hostClock / sampleRate;
    }

    bool isSupportedRate (Float64 rate)
    {
        for (auto r : sampleRates)
            if (std::fabs (r - rate) < 0.5)
                return true;
        return false;
    }

    /** Channel names (1-based, either direction). */
    void channelName (UInt32 channel, char* out, size_t size)
    {
        if (channel == 1)                       std::snprintf (out, size, "DAW L");
        else if (channel == 2)                  std::snprintf (out, size, "DAW R");
        else if (channel == 3)                  std::snprintf (out, size, "Main L");
        else if (channel == 4)                  std::snprintf (out, size, "Main R");
        else if (channel <= 4 + numStrips)      std::snprintf (out, size, "IN %u Wet", (unsigned) (channel - 4));
        else if (channel <= numChannels)        std::snprintf (out, size, "IN %u Dry", (unsigned) (channel - 4 - numStrips));
        else                                    std::snprintf (out, size, "Channel %u", (unsigned) channel);
    }

    AudioStreamBasicDescription format (Float64 rate)
    {
        AudioStreamBasicDescription f {};
        f.mSampleRate = rate;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
        f.mBytesPerPacket = 4 * numChannels;
        f.mFramesPerPacket = 1;
        f.mBytesPerFrame = 4 * numChannels;
        f.mChannelsPerFrame = numChannels;
        f.mBitsPerChannel = 32;
        return f;
    }

    template <typename T>
    OSStatus put (const T& value, UInt32 inDataSize, UInt32* outDataSize, void* outData)
    {
        if (inDataSize < sizeof (T))
            return kAudioHardwareBadPropertySizeError;
        std::memcpy (outData, &value, sizeof (T));
        *outDataSize = sizeof (T);
        return kAudioHardwareNoError;
    }

    OSStatus putString (const char* text, UInt32 inDataSize, UInt32* outDataSize, void* outData)
    {
        if (inDataSize < sizeof (CFStringRef))
            return kAudioHardwareBadPropertySizeError;
        *static_cast<CFStringRef*> (outData) = cf (text);   // (the caller releases it)
        *outDataSize = sizeof (CFStringRef);
        return kAudioHardwareNoError;
    }

    OSStatus putIDs (const AudioObjectID* ids, UInt32 count, UInt32 inDataSize, UInt32* outDataSize, void* outData)
    {
        const UInt32 fit = std::min<UInt32> (count, inDataSize / (UInt32) sizeof (AudioObjectID));
        if (fit > 0)
            std::memcpy (outData, ids, fit * sizeof (AudioObjectID));
        *outDataSize = fit * (UInt32) sizeof (AudioObjectID);
        return kAudioHardwareNoError;
    }

    /** The streams a device scope owns. */
    UInt32 streamsFor (AudioObjectPropertyScope scope, AudioObjectID* ids)
    {
        UInt32 n = 0;
        if (scope == kAudioObjectPropertyScopeGlobal || scope == kAudioObjectPropertyScopeInput)  ids[n++] = inputStreamID;
        if (scope == kAudioObjectPropertyScopeGlobal || scope == kAudioObjectPropertyScopeOutput) ids[n++] = outputStreamID;
        return n;
    }
}

//==============================================================================
// The interface
static HRESULT QueryInterface (void*, REFIID, LPVOID*);
static ULONG AddRef (void*);
static ULONG Release (void*);
static OSStatus Initialize (AudioServerPlugInDriverRef, AudioServerPlugInHostRef);
static OSStatus CreateDevice (AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*);
static OSStatus DestroyDevice (AudioServerPlugInDriverRef, AudioObjectID);
static OSStatus AddDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus RemoveDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus PerformDeviceConfigurationChange (AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static OSStatus AbortDeviceConfigurationChange (AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static Boolean HasProperty (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*);
static OSStatus IsPropertySettable (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, Boolean*);
static OSStatus GetPropertyDataSize (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
static OSStatus GetPropertyData (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, UInt32*, void*);
static OSStatus SetPropertyData (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, const void*);
static OSStatus StartIO (AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus StopIO (AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus GetZeroTimeStamp (AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64*, UInt64*, UInt64*);
static OSStatus WillDoIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean*, Boolean*);
static OSStatus BeginIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);
static OSStatus DoIOOperation (AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*, void*, void*);
static OSStatus EndIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);

static AudioServerPlugInDriverInterface driverInterface = {
    nullptr,
    QueryInterface, AddRef, Release,
    Initialize, CreateDevice, DestroyDevice, AddDeviceClient, RemoveDeviceClient,
    PerformDeviceConfigurationChange, AbortDeviceConfigurationChange,
    HasProperty, IsPropertySettable, GetPropertyDataSize, GetPropertyData, SetPropertyData,
    StartIO, StopIO, GetZeroTimeStamp, WillDoIOOperation, BeginIOOperation, DoIOOperation, EndIOOperation
};
static AudioServerPlugInDriverInterface* driverInterfacePtr = &driverInterface;
static AudioServerPlugInDriverRef driverRef = &driverInterfacePtr;

/** The factory named in Info.plist. */
extern "C" __attribute__ ((visibility ("default"))) void* AudioTechProConsole_Create (CFAllocatorRef, CFUUIDRef requestedType)
{
    if (CFEqual (requestedType, kAudioServerPlugInTypeUUID))
        return driverRef;
    return nullptr;
}

//==============================================================================
static HRESULT QueryInterface (void* driver, REFIID uuid, LPVOID* out)
{
    if (driver != driverRef || out == nullptr)
        return kAudioHardwareIllegalOperationError;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes (nullptr, uuid);
    HRESULT result = E_NOINTERFACE;
    if (CFEqual (requested, IUnknownUUID) || CFEqual (requested, kAudioServerPlugInDriverInterfaceUUID))
    {
        ++refCount;
        *out = driverRef;
        result = S_OK;
    }
    CFRelease (requested);
    return result;
}

static ULONG AddRef (void* driver)
{
    if (driver != driverRef) return 0;
    return ++refCount;
}

static ULONG Release (void* driver)
{
    if (driver != driverRef) return 0;
    const UInt32 now = refCount.load();
    if (now > 0) return --refCount;
    return 0;
}

static OSStatus Initialize (AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef inHost)
{
    if (driver != driverRef) return kAudioHardwareBadObjectError;
    host = inHost;
    computeTicks();
    return kAudioHardwareNoError;
}

static OSStatus CreateDevice (AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*) { return kAudioHardwareUnsupportedOperationError; }
static OSStatus DestroyDevice (AudioServerPlugInDriverRef, AudioObjectID)                                                       { return kAudioHardwareUnsupportedOperationError; }
static OSStatus AddDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*)                 { return kAudioHardwareNoError; }
static OSStatus RemoveDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*)              { return kAudioHardwareNoError; }

static OSStatus PerformDeviceConfigurationChange (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt64 action, void*)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    const Float64 rate = (Float64) action;   // (the change is a new sample rate, passed as the action)
    if (! isSupportedRate (rate)) return kAudioHardwareBadObjectError;
    pthread_mutex_lock (&stateMutex);
    sampleRate = rate;
    computeTicks();
    pthread_mutex_unlock (&stateMutex);
    return kAudioHardwareNoError;
}

static OSStatus AbortDeviceConfigurationChange (AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*) { return kAudioHardwareNoError; }

//==============================================================================
// Properties

static Boolean HasProperty (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t, const AudioObjectPropertyAddress* a)
{
    if (driver != driverRef || a == nullptr) return false;
    const auto s = a->mSelector;

    switch (object)
    {
        case plugInID:
            return s == kAudioObjectPropertyBaseClass || s == kAudioObjectPropertyClass || s == kAudioObjectPropertyOwner
                || s == kAudioObjectPropertyManufacturer || s == kAudioObjectPropertyOwnedObjects
                || s == kAudioPlugInPropertyDeviceList || s == kAudioPlugInPropertyTranslateUIDToDevice
                || s == kAudioPlugInPropertyResourceBundle;

        case deviceID:
            switch (s)
            {
                case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass: case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyName: case kAudioObjectPropertyManufacturer: case kAudioObjectPropertyOwnedObjects:
                case kAudioDevicePropertyDeviceUID: case kAudioDevicePropertyModelUID: case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyRelatedDevices: case kAudioDevicePropertyClockDomain: case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning: case kAudioObjectPropertyControlList:
                case kAudioDevicePropertyNominalSampleRate: case kAudioDevicePropertyAvailableNominalSampleRates:
                case kAudioDevicePropertyIsHidden: case kAudioDevicePropertyZeroTimeStampPeriod: case kAudioDevicePropertyStreams:
                    return true;
                case kAudioDevicePropertyDeviceCanBeDefaultDevice: case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                case kAudioDevicePropertyLatency: case kAudioDevicePropertySafetyOffset:
                case kAudioDevicePropertyPreferredChannelsForStereo:
                    return a->mScope == kAudioObjectPropertyScopeInput || a->mScope == kAudioObjectPropertyScopeOutput;
                case kAudioObjectPropertyElementName:
                    return (a->mScope == kAudioObjectPropertyScopeInput || a->mScope == kAudioObjectPropertyScopeOutput)
                        && a->mElement >= 1 && a->mElement <= numChannels;
                default:
                    return false;
            }

        case inputStreamID:
        case outputStreamID:
            switch (s)
            {
                case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass: case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyOwnedObjects: case kAudioStreamPropertyIsActive: case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType: case kAudioStreamPropertyStartingChannel: case kAudioStreamPropertyLatency:
                case kAudioStreamPropertyVirtualFormat: case kAudioStreamPropertyPhysicalFormat:
                case kAudioStreamPropertyAvailableVirtualFormats: case kAudioStreamPropertyAvailablePhysicalFormats:
                    return true;
                default:
                    return false;
            }

        default:
            return false;
    }
}

static OSStatus IsPropertySettable (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* a, Boolean* settable)
{
    if (driver != driverRef || settable == nullptr) return kAudioHardwareBadObjectError;
    if (! HasProperty (driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    *settable = (object == deviceID && a->mSelector == kAudioDevicePropertyNominalSampleRate)
             || ((object == inputStreamID || object == outputStreamID)
                 && (a->mSelector == kAudioStreamPropertyIsActive || a->mSelector == kAudioStreamPropertyVirtualFormat
                     || a->mSelector == kAudioStreamPropertyPhysicalFormat));
    return kAudioHardwareNoError;
}

static OSStatus GetPropertyDataSize (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* a,
                                     UInt32, const void*, UInt32* size)
{
    if (driver != driverRef || size == nullptr) return kAudioHardwareBadObjectError;
    if (! HasProperty (driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    const auto s = a->mSelector;

    switch (s)
    {
        case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass:     *size = sizeof (AudioClassID); return 0;
        case kAudioObjectPropertyOwner:                                         *size = sizeof (AudioObjectID); return 0;
        case kAudioObjectPropertyName: case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID: case kAudioDevicePropertyModelUID:
        case kAudioPlugInPropertyResourceBundle: case kAudioObjectPropertyElementName:
                                                                                *size = sizeof (CFStringRef); return 0;
        case kAudioPlugInPropertyTranslateUIDToDevice:                          *size = sizeof (AudioObjectID); return 0;
        case kAudioPlugInPropertyDeviceList:                                    *size = sizeof (AudioObjectID); return 0;
        case kAudioDevicePropertyRelatedDevices:                                *size = sizeof (AudioObjectID); return 0;
        case kAudioObjectPropertyControlList:                                   *size = 0; return 0;
        case kAudioDevicePropertyNominalSampleRate:                             *size = sizeof (Float64); return 0;
        case kAudioDevicePropertyAvailableNominalSampleRates:                   *size = numSampleRates * sizeof (AudioValueRange); return 0;
        case kAudioDevicePropertyPreferredChannelsForStereo:                    *size = 2 * sizeof (UInt32); return 0;
        case kAudioStreamPropertyVirtualFormat: case kAudioStreamPropertyPhysicalFormat:
                                                                                *size = sizeof (AudioStreamBasicDescription); return 0;
        case kAudioStreamPropertyAvailableVirtualFormats: case kAudioStreamPropertyAvailablePhysicalFormats:
                                                                                *size = numSampleRates * sizeof (AudioStreamRangedDescription); return 0;
        case kAudioObjectPropertyOwnedObjects:
        {
            if (object == plugInID)      { *size = sizeof (AudioObjectID); return 0; }
            if (object == deviceID)      { AudioObjectID ids[2]; *size = streamsFor (a->mScope, ids) * sizeof (AudioObjectID); return 0; }
            *size = 0;
            return 0;
        }
        case kAudioDevicePropertyStreams:
        {
            AudioObjectID ids[2];
            *size = streamsFor (a->mScope, ids) * sizeof (AudioObjectID);
            return 0;
        }
        default:
            *size = sizeof (UInt32);   // (every other property here is a UInt32)
            return 0;
    }
}

static OSStatus GetPropertyData (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* a,
                                 UInt32 qualifierSize, const void* qualifier, UInt32 inSize, UInt32* outSize, void* out)
{
    if (driver != driverRef || outSize == nullptr || out == nullptr) return kAudioHardwareBadObjectError;
    if (! HasProperty (driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;
    const auto s = a->mSelector;

    // ---- the plug-in ----
    if (object == plugInID)
    {
        switch (s)
        {
            case kAudioObjectPropertyBaseClass:   return put<AudioClassID> (kAudioObjectClassID, inSize, outSize, out);
            case kAudioObjectPropertyClass:       return put<AudioClassID> (kAudioPlugInClassID, inSize, outSize, out);
            case kAudioObjectPropertyOwner:       return put<AudioObjectID> (kAudioObjectUnknown, inSize, outSize, out);
            case kAudioObjectPropertyManufacturer: return putString (manufacturer, inSize, outSize, out);
            case kAudioPlugInPropertyResourceBundle: return putString ("", inSize, outSize, out);
            case kAudioObjectPropertyOwnedObjects:
            case kAudioPlugInPropertyDeviceList:
            {
                const AudioObjectID ids[] = { deviceID };
                return putIDs (ids, 1, inSize, outSize, out);
            }
            case kAudioPlugInPropertyTranslateUIDToDevice:
            {
                if (qualifierSize != sizeof (CFStringRef) || qualifier == nullptr) return kAudioHardwareBadPropertySizeError;
                CFStringRef uid = *static_cast<const CFStringRef*> (qualifier);
                CFStringRef ours = cf (deviceUID);
                const AudioObjectID result = (uid != nullptr && CFStringCompare (uid, ours, 0) == kCFCompareEqualTo) ? deviceID : kAudioObjectUnknown;
                CFRelease (ours);
                return put<AudioObjectID> (result, inSize, outSize, out);
            }
            default: return kAudioHardwareUnknownPropertyError;
        }
    }

    // ---- the device ----
    if (object == deviceID)
    {
        switch (s)
        {
            case kAudioObjectPropertyBaseClass:   return put<AudioClassID> (kAudioObjectClassID, inSize, outSize, out);
            case kAudioObjectPropertyClass:       return put<AudioClassID> (kAudioDeviceClassID, inSize, outSize, out);
            case kAudioObjectPropertyOwner:       return put<AudioObjectID> (plugInID, inSize, outSize, out);
            case kAudioObjectPropertyName:        return putString (deviceName, inSize, outSize, out);
            case kAudioObjectPropertyManufacturer: return putString (manufacturer, inSize, outSize, out);
            case kAudioDevicePropertyDeviceUID:   return putString (deviceUID, inSize, outSize, out);
            case kAudioDevicePropertyModelUID:    return putString (modelUID, inSize, outSize, out);
            case kAudioDevicePropertyTransportType: return put<UInt32> (kAudioDeviceTransportTypeVirtual, inSize, outSize, out);
            case kAudioDevicePropertyClockDomain: return put<UInt32> (0, inSize, outSize, out);
            case kAudioDevicePropertyDeviceIsAlive: return put<UInt32> (1, inSize, outSize, out);
            case kAudioDevicePropertyDeviceIsRunning:
            {
                pthread_mutex_lock (&stateMutex);
                const UInt32 running = ioRunning > 0 ? 1 : 0;
                pthread_mutex_unlock (&stateMutex);
                return put<UInt32> (running, inSize, outSize, out);
            }
            case kAudioDevicePropertyDeviceCanBeDefaultDevice:       return put<UInt32> (0, inSize, outSize, out);   // (not for system sounds)
            case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: return put<UInt32> (0, inSize, outSize, out);
            case kAudioDevicePropertyLatency:     return put<UInt32> (0, inSize, outSize, out);
            case kAudioDevicePropertySafetyOffset: return put<UInt32> (0, inSize, outSize, out);
            case kAudioDevicePropertyIsHidden:    return put<UInt32> (0, inSize, outSize, out);
            case kAudioDevicePropertyZeroTimeStampPeriod: return put<UInt32> (ringFrames, inSize, outSize, out);
            case kAudioObjectPropertyControlList: *outSize = 0; return kAudioHardwareNoError;
            case kAudioDevicePropertyRelatedDevices:
            {
                const AudioObjectID ids[] = { deviceID };
                return putIDs (ids, 1, inSize, outSize, out);
            }
            case kAudioObjectPropertyOwnedObjects:
            case kAudioDevicePropertyStreams:
            {
                AudioObjectID ids[2];
                const UInt32 n = streamsFor (a->mScope, ids);
                return putIDs (ids, n, inSize, outSize, out);
            }
            case kAudioDevicePropertyNominalSampleRate:
            {
                pthread_mutex_lock (&stateMutex);
                const Float64 rate = sampleRate;
                pthread_mutex_unlock (&stateMutex);
                return put<Float64> (rate, inSize, outSize, out);
            }
            case kAudioDevicePropertyAvailableNominalSampleRates:
            {
                const UInt32 fit = std::min<UInt32> (numSampleRates, inSize / (UInt32) sizeof (AudioValueRange));
                auto* ranges = static_cast<AudioValueRange*> (out);
                for (UInt32 i = 0; i < fit; ++i)
                    ranges[i] = { sampleRates[i], sampleRates[i] };
                *outSize = fit * (UInt32) sizeof (AudioValueRange);
                return kAudioHardwareNoError;
            }
            case kAudioDevicePropertyPreferredChannelsForStereo:
            {
                if (inSize < 2 * sizeof (UInt32)) return kAudioHardwareBadPropertySizeError;
                auto* channels = static_cast<UInt32*> (out);
                channels[0] = 1;   // (DAW L/R)
                channels[1] = 2;
                *outSize = 2 * sizeof (UInt32);
                return kAudioHardwareNoError;
            }
            case kAudioObjectPropertyElementName:
            {
                char name[32];
                channelName (a->mElement, name, sizeof (name));
                return putString (name, inSize, outSize, out);
            }
            default: return kAudioHardwareUnknownPropertyError;
        }
    }

    // ---- the streams ----
    const bool isInput = object == inputStreamID;
    switch (s)
    {
        case kAudioObjectPropertyBaseClass:   return put<AudioClassID> (kAudioObjectClassID, inSize, outSize, out);
        case kAudioObjectPropertyClass:       return put<AudioClassID> (kAudioStreamClassID, inSize, outSize, out);
        case kAudioObjectPropertyOwner:       return put<AudioObjectID> (deviceID, inSize, outSize, out);
        case kAudioObjectPropertyOwnedObjects: *outSize = 0; return kAudioHardwareNoError;
        case kAudioStreamPropertyIsActive:
        {
            pthread_mutex_lock (&stateMutex);
            const UInt32 active = (isInput ? inputActive : outputActive) ? 1 : 0;
            pthread_mutex_unlock (&stateMutex);
            return put<UInt32> (active, inSize, outSize, out);
        }
        case kAudioStreamPropertyDirection:     return put<UInt32> (isInput ? 1 : 0, inSize, outSize, out);
        case kAudioStreamPropertyTerminalType:  return put<UInt32> (isInput ? kAudioStreamTerminalTypeLine : kAudioStreamTerminalTypeLine, inSize, outSize, out);
        case kAudioStreamPropertyStartingChannel: return put<UInt32> (1, inSize, outSize, out);
        case kAudioStreamPropertyLatency:       return put<UInt32> (0, inSize, outSize, out);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        {
            pthread_mutex_lock (&stateMutex);
            const auto f = format (sampleRate);
            pthread_mutex_unlock (&stateMutex);
            return put<AudioStreamBasicDescription> (f, inSize, outSize, out);
        }
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
        {
            const UInt32 fit = std::min<UInt32> (numSampleRates, inSize / (UInt32) sizeof (AudioStreamRangedDescription));
            auto* list = static_cast<AudioStreamRangedDescription*> (out);
            for (UInt32 i = 0; i < fit; ++i)
            {
                list[i].mFormat = format (sampleRates[i]);
                list[i].mSampleRateRange = { sampleRates[i], sampleRates[i] };
            }
            *outSize = fit * (UInt32) sizeof (AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        }
        default: return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus SetPropertyData (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* a,
                                 UInt32, const void*, UInt32 inSize, const void* data)
{
    if (driver != driverRef || data == nullptr) return kAudioHardwareBadObjectError;
    if (! HasProperty (driver, object, pid, a)) return kAudioHardwareUnknownPropertyError;

    auto requestRate = [] (Float64 rate) -> OSStatus
    {
        if (! isSupportedRate (rate)) return kAudioHardwareIllegalOperationError;
        pthread_mutex_lock (&stateMutex);
        const bool same = std::fabs (rate - sampleRate) < 0.5;
        pthread_mutex_unlock (&stateMutex);
        if (! same && host != nullptr)
            host->RequestDeviceConfigurationChange (host, deviceID, (UInt64) rate, nullptr);
        return kAudioHardwareNoError;
    };

    if (object == deviceID && a->mSelector == kAudioDevicePropertyNominalSampleRate)
    {
        if (inSize != sizeof (Float64)) return kAudioHardwareBadPropertySizeError;
        return requestRate (*static_cast<const Float64*> (data));
    }

    if (object == inputStreamID || object == outputStreamID)
    {
        if (a->mSelector == kAudioStreamPropertyIsActive)
        {
            if (inSize != sizeof (UInt32)) return kAudioHardwareBadPropertySizeError;
            const bool active = *static_cast<const UInt32*> (data) != 0;
            pthread_mutex_lock (&stateMutex);
            (object == inputStreamID ? inputActive : outputActive) = active;
            pthread_mutex_unlock (&stateMutex);
            if (host != nullptr)
                host->PropertiesChanged (host, object, 1, a);
            return kAudioHardwareNoError;
        }
        if (a->mSelector == kAudioStreamPropertyVirtualFormat || a->mSelector == kAudioStreamPropertyPhysicalFormat)
        {
            if (inSize != sizeof (AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
            const auto* f = static_cast<const AudioStreamBasicDescription*> (data);
            if (f->mFormatID != kAudioFormatLinearPCM || f->mChannelsPerFrame != numChannels || f->mBitsPerChannel != 32)
                return kAudioDeviceUnsupportedFormatError;
            return requestRate (f->mSampleRate);
        }
    }
    return kAudioHardwareUnknownPropertyError;
}

//==============================================================================
// IO

static OSStatus StartIO (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    pthread_mutex_lock (&stateMutex);
    if (ioRunning == 0)
    {
        if (ring == nullptr)
            ring = static_cast<Float32*> (std::calloc ((size_t) ringFrames * numChannels, sizeof (Float32)));
        if (ring == nullptr)
        {
            pthread_mutex_unlock (&stateMutex);
            return kAudioHardwareUnspecifiedError;
        }
        pthread_mutex_lock (&ioMutex);
        numberTimeStamps = 0;
        anchorHostTime = mach_absolute_time();
        lastWrittenSampleTime = -1.0;
        ringClear = false;
        pthread_mutex_unlock (&ioMutex);
    }
    ++ioRunning;
    pthread_mutex_unlock (&stateMutex);
    return kAudioHardwareNoError;
}

static OSStatus StopIO (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    pthread_mutex_lock (&stateMutex);
    if (ioRunning > 0)
        --ioRunning;
    pthread_mutex_unlock (&stateMutex);
    return kAudioHardwareNoError;
}

static OSStatus GetZeroTimeStamp (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, Float64* sampleTime, UInt64* hostTime, UInt64* seed)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    pthread_mutex_lock (&ioMutex);
    // (a steady clock from the host's: one time stamp per trip round the ring)
    const Float64 ticksPerRing = hostTicksPerFrame * (Float64) ringFrames;
    const UInt64 now = mach_absolute_time();
    const UInt64 next = anchorHostTime + (UInt64) ((Float64) (numberTimeStamps + 1) * ticksPerRing);
    if (next <= now)
        ++numberTimeStamps;
    *sampleTime = (Float64) (numberTimeStamps * ringFrames);
    *hostTime = anchorHostTime + (UInt64) ((Float64) numberTimeStamps * ticksPerRing);
    *seed = 1;
    pthread_mutex_unlock (&ioMutex);
    return kAudioHardwareNoError;
}

static OSStatus WillDoIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32 operation, Boolean* willDo, Boolean* inPlace)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    const bool ours = operation == kAudioServerPlugInIOOperationReadInput || operation == kAudioServerPlugInIOOperationWriteMix;
    if (willDo != nullptr)  *willDo = ours;
    if (inPlace != nullptr) *inPlace = true;
    return kAudioHardwareNoError;
}

static OSStatus BeginIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*)
{
    return (driver != driverRef || device != deviceID) ? kAudioHardwareBadObjectError : kAudioHardwareNoError;
}

static OSStatus EndIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*)
{
    return (driver != driverRef || device != deviceID) ? kAudioHardwareBadObjectError : kAudioHardwareNoError;
}

static OSStatus DoIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID device, AudioObjectID stream, UInt32, UInt32 operation,
                               UInt32 frames, const AudioServerPlugInIOCycleInfo* cycle, void* buffer, void*)
{
    if (driver != driverRef || device != deviceID) return kAudioHardwareBadObjectError;
    if (stream != inputStreamID && stream != outputStreamID) return kAudioHardwareBadObjectError;
    if (ring == nullptr || buffer == nullptr || cycle == nullptr) return kAudioHardwareNoError;

    const bool reading = operation == kAudioServerPlugInIOOperationReadInput;
    const Float64 time = reading ? cycle->mInputTime.mSampleTime : cycle->mOutputTime.mSampleTime;
    const UInt64 start = (UInt64) time % ringFrames;
    const UInt32 first = (UInt32) std::min<UInt64> (frames, ringFrames - start);
    const UInt32 second = frames - first;
    auto* io = static_cast<Float32*> (buffer);
    const size_t frameBytes = numChannels * sizeof (Float32);

    if (reading)
    {
        // Nothing written for this time yet (no one is playing into the device): silence
        if (lastWrittenSampleTime < 0.0 || lastWrittenSampleTime - frames < time)
        {
            std::memset (io, 0, frames * frameBytes);
            if (! ringClear)
            {
                std::memset (ring, 0, (size_t) ringFrames * frameBytes);
                ringClear = true;
            }
            return kAudioHardwareNoError;
        }
        std::memcpy (io, ring + start * numChannels, first * frameBytes);
        if (second > 0)
            std::memcpy (io + (size_t) first * numChannels, ring, second * frameBytes);
        return kAudioHardwareNoError;
    }

    if (operation == kAudioServerPlugInIOOperationWriteMix)
    {
        std::memcpy (ring + start * numChannels, io, first * frameBytes);
        if (second > 0)
            std::memcpy (ring, io + (size_t) first * numChannels, second * frameBytes);
        lastWrittenSampleTime = time + frames;
        ringClear = false;
    }
    return kAudioHardwareNoError;
}
