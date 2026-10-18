#pragma once

#include <JuceHeader.h>

/**
    Scanning plugins safely: each new plugin is opened in a separate copy of the app (started with
    "--scan-plugin"), never in the app itself. If a plugin crashes or hangs while it's checked, only
    that copy goes down: the plugin is put on the "don't scan" list and the app carries on.
*/
namespace riff::pluginscan
{
    inline constexpr const char* argument = "--scan-plugin";

    /** True when this process was started to scan one plugin. */
    bool isScanChild (const juce::StringArray& commandLine);

    /** The child's whole job: opens the plugin, writes what it found to the given file, and returns
        the exit code (0: done). Arguments: --scan-plugin <format name> <file or identifier> <output file>. */
    int runScanChild (const juce::StringArray& commandLine);

    /** The KnownPluginList's scanner: each plugin checked in its own process. */
    class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override;

        static constexpr int timeoutMs = 120000;   // (time for a licence prompt; longer is taken as a hang)
    };
}
