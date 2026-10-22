// The space bar in plugin windows (see PluginWindowKeys.h). Compiled with ARC.
#define DONT_SET_USING_JUCE_NAMESPACE 1
#include "PluginWindowKeys.h"

#import <AppKit/AppKit.h>

namespace
{
    id monitor = nil;
    std::function<bool (void*, bool)> onSpace;
}

void riff::watchPluginWindowKeys (std::function<bool (void* nsView, bool typing)> handler)
{
    stopWatchingPluginWindowKeys();
    onSpace = std::move (handler);
    monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskKeyDown
                                                    handler: ^NSEvent* (NSEvent* event)
    {
        const auto mods = [event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption);
        if ([event keyCode] != 49 || mods != 0 || [event isARepeat] || ! onSpace)   // 49: the space bar
            return event;
        NSWindow* window = [event window];
        if (window == nil)
            return event;
        const bool typing = [[window firstResponder] isKindOfClass: [NSText class]];   // (a field editor: someone's typing)
        return onSpace ((__bridge void*) [window contentView], typing) ? nil : event;
    }];
}

void riff::stopWatchingPluginWindowKeys()
{
    if (monitor != nil)
        [NSEvent removeMonitor: monitor];
    monitor = nil;
    onSpace = nullptr;
}
