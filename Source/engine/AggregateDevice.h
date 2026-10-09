#pragma once

#include <JuceHeader.h>

/**
    Several audio interfaces used as one: a combined ("aggregate") device made
    from the interfaces you pick, with one of them as the clock and drift
    correction on the others. macOS only (Core Audio); elsewhere isSupported()
    is false. The combined device is private to Audio Tech Pro and goes away
    when the app quits; it's made again from the saved setup at the next start.
*/
namespace riff::aggregate
{
    inline const juce::String deviceName { "Audio Tech Pro Combined" };
    inline const juce::String deviceUid { "com.audiotechpro.combined" };

    struct Interface
    {
        juce::String uid, name;
        int inputs = 0, outputs = 0;
        bool isUsb = false;
    };

    struct Setup
    {
        juce::StringArray uids;        // in channel order
        juce::String clockUid;         // the master clock (first in the list)
        bool driftCorrection = true;   // on every interface but the clock

        bool isActive() const { return uids.size() >= 2; }

        juce::String toString() const;
        static Setup fromString (const juce::String& text);

        /** The interfaces in channel order: the clock first, the others as listed. */
        juce::StringArray orderedUids() const;
    };

    bool isSupported();

    /** The interfaces connected now (not the combined device itself, nor other aggregates). */
    juce::Array<Interface> listInterfaces();

    /** Makes the combined device (replacing an old one). Returns an error message, or empty. */
    juce::String create (const Setup& setup);

    /** Removes the combined device, if there is one. */
    void destroy();

    /** Names for the combined device's channels, e.g. "Quantum HD 2 3" or "SSL 2 MKII 1", from the interfaces it's made of. */
    juce::StringArray channelLabels (const Setup& setup, const juce::Array<Interface>& interfaces, bool inputs);

    /** Which of the setup's interfaces aren't connected now. */
    juce::StringArray missingInterfaces (const Setup& setup, const juce::Array<Interface>& interfaces);
}
