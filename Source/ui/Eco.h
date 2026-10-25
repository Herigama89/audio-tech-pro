#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
    Eco mode (Settings): for slower computers, or a laptop on battery. The screen is drawn with less work - meters
    and the playhead 15 times a second instead of 30, nothing moving while the app isn't in front, strips scrolled
    out of view not updated - while the sound is untouched (the engine, plugins and recording run as always).
*/
namespace riff::eco
{
    enum class Mode { off = 0, on = 1, onBattery = 2 };

    inline std::atomic<int> mode { (int) Mode::off };
    inline std::atomic<bool> batteryNow { false };

    /** True on battery power (laptops; checked now and then by the main window). */
    bool runningOnBattery();

    inline bool active() noexcept
    {
        const auto m = (Mode) mode.load (std::memory_order_relaxed);
        return m == Mode::on || (m == Mode::onBattery && batteryNow.load (std::memory_order_relaxed));
    }

    /** For a 30-a-second display timer: true on the frames Eco mode leaves out (every other one), and on every frame
        while the app isn't the one in front. */
    inline bool skipFrame (int& counter) noexcept
    {
        if (! active())
            return false;
        if (! juce::Process::isForegroundProcess())
            return true;
        return (++counter & 1) != 0;
    }
}
