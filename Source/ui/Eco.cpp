#include "Eco.h"

#if JUCE_MAC
 #include <IOKit/ps/IOPowerSources.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

namespace riff::eco
{

bool runningOnBattery()
{
   #if JUCE_MAC
    // (unlimited: plugged in)
    return IOPSGetTimeRemainingEstimate() != kIOPSTimeRemainingUnlimited;
   #elif JUCE_WINDOWS
    SYSTEM_POWER_STATUS status {};
    return GetSystemPowerStatus (&status) && status.ACLineStatus == 0;
   #else
    return false;
   #endif
}

} // namespace riff::eco
