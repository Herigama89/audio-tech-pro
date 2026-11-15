#include "PluginInstrument.h"
#include "PluginBuses.h"

namespace riff
{

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p)
    : plugin (std::move (p))
{
    jassert (plugin != nullptr);

    // A stereo main output; multi-output instruments (Groove Agent, Kontakt...) keep their other outputs on.
    pluginbuses::setUp (*plugin);
}

PluginInstrument::~PluginInstrument()
{
    if (prepared)
        plugin->releaseResources();
}

void PluginInstrument::prepare (double sampleRate, int maxBlockSize)
{
    if (prepared)
        plugin->releaseResources();

    plugin->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    plugin->prepareToPlay (sampleRate, maxBlockSize);

    // (with room to spare, in case the plugin turns more outputs on while it plays)
    const auto channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    buffer.setSize (juce::jmax (channels, 64), maxBlockSize);
    midiCopy.ensureSize (16384);

    prepared = true;
    preparedRate = sampleRate;
}

void PluginInstrument::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    numSamples = juce::jmin (numSamples, buffer.getNumSamples());

    // A non-owning view of exactly numSamples, so the plugin sees the right block size.
    if (plugin->isSuspended() || ! pluginbuses::fits (*plugin, buffer.getNumChannels()))
    {
        renderedOutputs.store (0, std::memory_order_relaxed);
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
        return;
    }

    const auto used = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), used, numSamples);
    view.clear();

    midiCopy.clear();
    midiCopy.addEvents (midi, 0, numSamples, 0);

    plugin->processBlock (view, midiCopy);

    const auto outs = juce::jmax (1, plugin->getTotalNumOutputChannels());
    renderedOutputs.store (outs, std::memory_order_relaxed);
    renderedSamples.store (numSamples, std::memory_order_relaxed);
    juce::FloatVectorOperations::copy (left, view.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (right, view.getReadPointer (outs > 1 ? 1 : 0), numSamples);
}

juce::String PluginInstrument::getName() const
{
    return plugin->getName();
}

int PluginInstrument::getNumExtraOutputs() const noexcept
{
    // (by channel pairs, as Logic lists a multi-output instrument's outputs: 3-4, 5-6...)
    return juce::jmax (0, (plugin->getTotalNumOutputChannels() + 1) / 2 - 1);
}

const float* PluginInstrument::getExtraOutput (int pair, int channel) const noexcept
{
    const auto outs = renderedOutputs.load (std::memory_order_relaxed);
    if (pair < 1 || outs <= 0)
        return nullptr;
    auto index = pair * 2 + juce::jlimit (0, 1, channel);
    if (index >= outs)
        index = pair * 2;            // (a last mono output: both sides)
    return index < outs && index < buffer.getNumChannels() ? buffer.getReadPointer (index) : nullptr;
}

juce::String PluginInstrument::getOutputName (int pair) const
{
    // The plugin's own name for the bus that output starts in ("Kick", "Snare", "Toms"...), with its channels
    const auto first = pair * 2;
    int channel = 0;
    for (int b = 0; b < plugin->getBusCount (false); ++b)
        if (const auto* bus = plugin->getBus (false, b); bus != nullptr && bus->isEnabled())
        {
            const auto count = bus->getNumberOfChannels();
            if (first >= channel && first < channel + count)
            {
                const auto numbers = juce::String (first + 1) + (first + 1 < plugin->getTotalNumOutputChannels() ? "-" + juce::String (first + 2) : juce::String());
                return bus->getName().isNotEmpty() && b > 0 ? bus->getName() + " (" + numbers + ")" : Instrument::getOutputName (pair);
            }
            channel += count;
        }
    return Instrument::getOutputName (pair);
}

juce::String PluginInstrument::getStateBase64() const
{
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    return state.toBase64Encoding();
}

void PluginInstrument::setStateBase64 (const juce::String& state)
{
    juce::MemoryBlock block;
    if (state.isNotEmpty() && block.fromBase64Encoding (state))
        plugin->setStateInformation (block.getData(), (int) block.getSize());
}

} // namespace riff
