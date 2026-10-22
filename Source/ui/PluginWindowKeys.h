#pragma once

#include <functional>

namespace riff
{
    /**
        The space bar in a plugin's window starts and stops the song, as in Logic and Pro Tools - even when the
        plugin's own view has the keyboard (a plugin's native view takes keys before the app sees them).

        On macOS a watch on the app's key presses: a space in one of our floating windows that isn't typing
        into a text field goes to `handler` (view: the window's content NSView, typing: a text field has the keyboard);
        when it returns true the plugin never sees it. Elsewhere nothing is needed: the windows pass the
        app's keys on themselves.
    */
    void watchPluginWindowKeys (std::function<bool (void* nsView, bool typing)> handler);
    void stopWatchingPluginWindowKeys();
}
