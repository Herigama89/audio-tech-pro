#pragma once

#include "Instrument.h"

namespace riff
{

/** A hosted VST3 / Audio Unit instrument plugin. */
class PluginInstrument final : public Instrument
{
public:
    explicit PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> plugin);
    ~PluginInstrument() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept override;
    juce::String getName() const override;

    juce::AudioPluginInstance& getPlugin() noexcept { return *plugin; }

    /** The plugin's own saved state, base64-encoded (message thread). */
    juce::String getStateBase64() const;
    void setStateBase64 (const juce::String& state);

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiCopy;
    bool prepared = false;
};

} // namespace riff
