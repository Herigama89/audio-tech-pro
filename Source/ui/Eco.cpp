// Kept apart from JUCE's headers on purpose: the system power headers bring in old Mac types (Point, Component...)
// that would clash with JUCE's once its namespace is in use.
#if defined (__APPLE__)
 #include <TargetConditionals.h>
 #include <CoreFoundation/CoreFoundation.h>
 #include <IOKit/ps/IOPowerSources.h>
#elif defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace riff::eco
{

bool runningOnBattery()
{
   #if defined (__APPLE__) && TARGET_OS_OSX
    // (unlimited: plugged in)
    return IOPSGetTimeRemainingEstimate() != kIOPSTimeRemainingUnlimited;
   #elif defined (_WIN32)
    SYSTEM_POWER_STATUS status {};
    return GetSystemPowerStatus (&status) && status.ACLineStatus == 0;
   #else
    return false;
   #endif
}

} // namespace riff::eco
