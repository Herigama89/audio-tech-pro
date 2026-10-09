#define DONT_SET_USING_JUCE_NAMESPACE 1   // (Core Audio has its own AudioBuffer)
#include "AggregateDevice.h"

namespace riff::aggregate
{

juce::String Setup::toString() const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    juce::Array<juce::var> list;
    for (const auto& uid : uids)
        list.add (uid);
    o->setProperty ("interfaces", list);
    o->setProperty ("clock", clockUid);
    o->setProperty ("drift", driftCorrection);
    return juce::JSON::toString (juce::var (o.get()), true);
}

Setup Setup::fromString (const juce::String& text)
{
    Setup s;
    const auto v = juce::JSON::parse (text);
    if (auto* list = v.getProperty ("interfaces", {}).getArray())
        for (const auto& uid : *list)
            if (uid.toString().isNotEmpty())
                s.uids.addIfNotAlreadyThere (uid.toString());
    s.clockUid = v.getProperty ("clock", {}).toString();
    s.driftCorrection = v.getProperty ("drift", true);
    if (! s.uids.contains (s.clockUid))
        s.clockUid = s.uids[0];
    return s;
}

juce::StringArray Setup::orderedUids() const
{
    juce::StringArray ordered;
    if (uids.contains (clockUid))
        ordered.add (clockUid);
    for (const auto& uid : uids)
        ordered.addIfNotAlreadyThere (uid);
    return ordered;
}

juce::StringArray channelLabels (const Setup& setup, const juce::Array<Interface>& interfaces, bool inputs)
{
    juce::StringArray labels;
    for (const auto& uid : setup.orderedUids())
        for (const auto& i : interfaces)
            if (i.uid == uid)
                for (int ch = 0; ch < (inputs ? i.inputs : i.outputs); ++ch)
                    labels.add (i.name + " " + juce::String (ch + 1));
    return labels;
}

juce::StringArray missingInterfaces (const Setup& setup, const juce::Array<Interface>& interfaces)
{
    juce::StringArray missing;
    for (const auto& uid : setup.uids)
    {
        bool found = false;
        for (const auto& i : interfaces)
            found = found || i.uid == uid;
        if (! found)
            missing.add (uid);
    }
    return missing;
}

} // namespace riff::aggregate

//==============================================================================
#if JUCE_MAC

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <vector>

namespace riff::aggregate
{

namespace
{
    AudioObjectPropertyAddress address (AudioObjectPropertySelector selector, AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        return { selector, scope, 0 /* the main element */ };
    }

    juce::String stringProperty (AudioObjectID id, AudioObjectPropertySelector selector)
    {
        auto a = address (selector);
        CFStringRef value = nullptr;
        UInt32 size = sizeof (value);
        if (AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, &value) != noErr || value == nullptr)
            return {};
        const auto result = juce::String::fromCFString (value);
        CFRelease (value);
        return result;
    }

    UInt32 uintProperty (AudioObjectID id, AudioObjectPropertySelector selector)
    {
        auto a = address (selector);
        UInt32 value = 0, size = sizeof (value);
        return AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, &value) == noErr ? value : 0;
    }

    int channelCount (AudioObjectID id, bool inputs)
    {
        auto a = address (kAudioDevicePropertyStreamConfiguration, inputs ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (id, &a, 0, nullptr, &size) != noErr || size == 0)
            return 0;

        juce::HeapBlock<char> data (size, true);
        auto* list = reinterpret_cast<AudioBufferList*> (data.get());
        if (AudioObjectGetPropertyData (id, &a, 0, nullptr, &size, list) != noErr)
            return 0;

        int channels = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
            channels += (int) list->mBuffers[i].mNumberChannels;
        return channels;
    }

    std::vector<AudioObjectID> allDevices()
    {
        auto a = address (kAudioHardwarePropertyDevices);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
            return {};
        std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));
        if (ids.empty() || AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr)
            return {};
        ids.resize (size / sizeof (AudioObjectID));
        return ids;
    }

    AudioObjectID findByUid (const juce::String& uid)
    {
        for (auto id : allDevices())
            if (stringProperty (id, kAudioDevicePropertyDeviceUID) == uid)
                return id;
        return kAudioObjectUnknown;
    }

    /** Holds a Core Foundation object and releases it. */
    struct CF
    {
        explicit CF (CFTypeRef r) : ref (r) {}
        ~CF() { if (ref != nullptr) CFRelease (ref); }
        CF (const CF&) = delete;
        CF& operator= (const CF&) = delete;
        CFTypeRef ref;
    };
}

bool isSupported() { return true; }

juce::Array<Interface> listInterfaces()
{
    juce::Array<Interface> result;
    for (auto id : allDevices())
    {
        const auto transport = uintProperty (id, kAudioDevicePropertyTransportType);
        if (transport == kAudioDeviceTransportTypeAggregate || transport == kAudioDeviceTransportTypeAutoAggregate)
            continue;

        Interface i;
        i.uid = stringProperty (id, kAudioDevicePropertyDeviceUID);
        i.name = stringProperty (id, kAudioObjectPropertyName);
        i.inputs = channelCount (id, true);
        i.outputs = channelCount (id, false);
        i.isUsb = transport == kAudioDeviceTransportTypeUSB;

        if (i.uid.isNotEmpty() && i.uid != deviceUid && (i.inputs > 0 || i.outputs > 0))
            result.add (i);
    }
    return result;
}

void destroy()
{
    const auto existing = findByUid (deviceUid);
    if (existing != kAudioObjectUnknown)
        AudioHardwareDestroyAggregateDevice (existing);
}

juce::String create (const Setup& setup)
{
    if (! setup.isActive())
        return "Choose at least two interfaces.";

    const auto missing = missingInterfaces (setup, listInterfaces());
    if (! missing.isEmpty())
        return "Not connected: " + missing.joinIntoString (", ") + ".";

    destroy();

    const int one = 1, zero = 0;
    CF yes (CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &one));
    CF no (CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &zero));
    CF name (deviceName.toCFString());
    CF uid (deviceUid.toCFString());

    CF description (CFDictionaryCreateMutable (kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    auto* desc = (CFMutableDictionaryRef) description.ref;
    CFDictionarySetValue (desc, CFSTR (kAudioAggregateDeviceNameKey), name.ref);
    CFDictionarySetValue (desc, CFSTR (kAudioAggregateDeviceUIDKey), uid.ref);
    CFDictionarySetValue (desc, CFSTR (kAudioAggregateDeviceIsPrivateKey), yes.ref);   // only Audio Tech Pro sees it

    CF subDevices (CFArrayCreateMutable (kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
    const auto ordered = setup.orderedUids();
    for (const auto& subUid : ordered)
    {
        CF sub (CFDictionaryCreateMutable (kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CF subUidString (subUid.toCFString());
        CFDictionarySetValue ((CFMutableDictionaryRef) sub.ref, CFSTR (kAudioSubDeviceUIDKey), subUidString.ref);
        const bool drift = setup.driftCorrection && subUid != ordered[0];
        CFDictionarySetValue ((CFMutableDictionaryRef) sub.ref, CFSTR (kAudioSubDeviceDriftCompensationKey), drift ? yes.ref : no.ref);
        CFArrayAppendValue ((CFMutableArrayRef) subDevices.ref, sub.ref);
    }
    CFDictionarySetValue (desc, CFSTR (kAudioAggregateDeviceSubDeviceListKey), subDevices.ref);

    // The clock: "master" is the key's value in every SDK (renamed "main" in newer headers).
    CF clock (ordered[0].toCFString());
    CFDictionarySetValue (desc, CFSTR ("master"), clock.ref);

    AudioObjectID device = kAudioObjectUnknown;
    const auto status = AudioHardwareCreateAggregateDevice (desc, &device);
    if (status != noErr || device == kAudioObjectUnknown)
        return "macOS couldn't combine the interfaces (error " + juce::String ((int) status) + ").";

    // Give Core Audio a moment to publish the new device.
    for (int i = 0; i < 20 && findByUid (deviceUid) == kAudioObjectUnknown; ++i)
        juce::Thread::sleep (25);

    return {};
}

} // namespace riff::aggregate

#else

namespace riff::aggregate
{
    bool isSupported() { return false; }
    juce::Array<Interface> listInterfaces() { return {}; }
    juce::String create (const Setup&) { return "Combining interfaces is on the Mac for now. On Windows, use your interface's ASIO driver."; }
    void destroy() {}
}

#endif
