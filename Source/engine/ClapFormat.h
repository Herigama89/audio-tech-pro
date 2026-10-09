#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Hosts CLAP plugins (clap-plugin.org) the same way JUCE hosts VST3 and
    Audio Units, so they show up in the plugin scanner, the instrument and
    insert menus, and load, play, automate, save and open their windows like
    any other plugin.

    Each .clap file (a bundle on macOS) can hold several plugins; each one is
    a PluginDescription whose uniqueId is the hash of its CLAP id.

    Supported: main audio in and out (other ports are given silence), notes
    in (CLAP notes, or MIDI for plugins that want MIDI), parameters (with the
    plugin's own text, gestures and automation), state, latency, tail, the
    plugin's window (macOS and Windows; elsewhere the generic editor), timers,
    and the transport.
*/
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    ClapPluginFormat() = default;

    static juce::String formatName() { return "CLAP"; }

    juce::String getName() const override { return formatName(); }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& desc) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directoriesToSearch, bool recursive, bool allowPluginsWhichRequireAsynchronousInstantiation = false) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize, PluginCreationCallback) override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
};

} // namespace riff
