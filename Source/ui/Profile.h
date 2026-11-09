#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

/**
    Where the window's time goes, measured in the app itself (on the message thread): each marked part adds up
    the milliseconds it takes, and the Performance window shows the heaviest, per second. What "it lags" is, in
    numbers, on the computer it happens on.
*/
namespace riff::profile
{
    struct Entry { double ms = 0.0; int calls = 0; };

    inline std::map<juce::String, Entry>& table()
    {
        static std::map<juce::String, Entry> t;
        return t;
    }

    /** Times its own life, added to `name`'s total. */
    struct Scope
    {
        explicit Scope (const char* n) noexcept : name (n), start (juce::Time::getMillisecondCounterHiRes()) {}
        ~Scope()
        {
            if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
                return;
            auto& e = table()[name];
            e.ms += juce::Time::getMillisecondCounterHiRes() - start;
            ++e.calls;
        }
        const char* name;
        double start;
    };

    /** The totals since the last take (per second of `seconds`), heaviest first; the table starts again. */
    inline std::vector<std::pair<juce::String, Entry>> take (double seconds)
    {
        std::vector<std::pair<juce::String, Entry>> out;
        if (seconds <= 0.0)
            return out;
        for (auto& [name, e] : table())
            if (e.calls > 0)
                out.push_back ({ name, { e.ms / seconds, (int) std::lround (e.calls / seconds) } });
        table().clear();
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
        return out;
    }
}

#define RIFF_PROFILE(name) const riff::profile::Scope riffProfileScope_ (name)
