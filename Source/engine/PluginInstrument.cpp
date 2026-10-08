#include "PluginInstrument.h"

namespace riff
{

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p)
    : plugin (std::move (p))
{
    jassert (plugin != nullptr);

    // Prefer one stereo output; keep the inputs as they are. If the plugin
    // refuses this layout it keeps its own default.
    auto layout = plugin->getBusesLayout();

    if (! layout.outputBuses.isEmpty())
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        for (int i = 1; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();

        if (plugin->checkBusesLayoutSupported (layout))
            plugin->setBusesLayout (layout);
    }
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

    const auto channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    buffer.setSize (channels, maxBlockSize);
    midiCopy.ensureSize (16384);

    prepared = true;
    preparedRate = sampleRate;
}

void PluginInstrument::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi) noexcept
{
    numSamples = juce::jmin (numSamples, buffer.getNumSamples());

    // A non-owning view of exactly numSamples, so the plugin sees the right block size.
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
    view.clear();

    midiCopy.clear();
    midiCopy.addEvents (midi, 0, numSamples, 0);

    if (plugin->isSuspended())
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
        return;
    }

    plugin->processBlock (view, midiCopy);

    const auto outs = juce::jmax (1, plugin->getTotalNumOutputChannels());
    juce::FloatVectorOperations::copy (left, view.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (right, view.getReadPointer (outs > 1 ? 1 : 0), numSamples);
}

juce::String PluginInstrument::getName() const
{
    return plugin->getName();
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
