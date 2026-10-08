#include "EffectRack.h"
#include "../model/Project.h"

namespace riff
{

//==============================================================================
// PluginEffect
//==============================================================================
PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> p)
    : plugin (std::move (p))
{
    // Stereo in, stereo out on the main buses; sidechains and extra outputs off.
    auto layout = plugin->getBusesLayout();

    for (int dir = 0; dir < 2; ++dir)
    {
        auto& buses = dir == 0 ? layout.inputBuses : layout.outputBuses;
        for (int i = 0; i < buses.size(); ++i)
            buses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    }

    if (plugin->checkBusesLayoutSupported (layout))
        plugin->setBusesLayout (layout);
}

PluginEffect::~PluginEffect()
{
    if (prepared)
        plugin->releaseResources();
}

void PluginEffect::prepare (double sampleRate, int maxBlockSize)
{
    if (prepared)
        plugin->releaseResources();

    plugin->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    plugin->prepareToPlay (sampleRate, maxBlockSize);

    const auto channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    buffer.setSize (channels, maxBlockSize);
    noMidi.ensureSize (1024);

    prepared = true;
    preparedRate = sampleRate;
}

void PluginEffect::process (float* left, float* right, int numSamples) noexcept
{
    numSamples = juce::jmin (numSamples, buffer.getNumSamples());

    if (plugin->isSuspended())
        return; // pass through

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
    view.clear();

    const auto ins = plugin->getTotalNumInputChannels();
    if (ins >= 1) juce::FloatVectorOperations::copy (view.getWritePointer (0), left, numSamples);
    if (ins >= 2) juce::FloatVectorOperations::copy (view.getWritePointer (1), right, numSamples);
    if (ins == 1) juce::FloatVectorOperations::addWithMultiply (view.getWritePointer (0), right, 1.0f, numSamples);

    noMidi.clear();
    plugin->processBlock (view, noMidi);

    const auto outs = juce::jmax (1, plugin->getTotalNumOutputChannels());
    juce::FloatVectorOperations::copy (left, view.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (right, view.getReadPointer (outs > 1 ? 1 : 0), numSamples);
}

juce::String PluginEffect::getStateBase64() const
{
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    return state.toBase64Encoding();
}

void PluginEffect::setStateBase64 (const juce::String& state)
{
    juce::MemoryBlock block;
    if (state.isNotEmpty() && block.fromBase64Encoding (state))
        plugin->setStateInformation (block.getData(), (int) block.getSize());
}

//==============================================================================
// EffectRack
//==============================================================================
juce::String EffectRack::keyFor (const juce::ValueTree& insert)
{
    if (insert[ids::kind].toString() == model::pluginKind)
        return "plugin:" + insert[ids::pluginDescription].toString();
    return "builtin:" + insert[ids::effect].toString();
}

std::shared_ptr<Effect> EffectRack::effectFor (const juce::ValueTree& insert, double sampleRate, int maxBlockSize)
{
    const auto id = insert[ids::id].toString();
    const auto key = keyFor (insert);
    auto& entry = entries[id];

    const bool sameKind = entry.key == key;
    const bool rateOk = std::abs (entry.sampleRate - sampleRate) < 0.5;

    if (sameKind && rateOk)
    {
        // Built-ins take their knob values from the project on every rebuild.
        if (auto* builtIn = dynamic_cast<BuiltInEffect*> (entry.effect.get()))
        {
            const auto& params = builtIn->getInfo().params;
            for (size_t p = 0; p < params.size(); ++p)
                builtIn->setParameter ((int) p, (float) (double) insert.getProperty (params[p].id, params[p].defaultValue));
        }

        return entry.effect;
    }

    if (! sameKind && entry.effect != nullptr && onEffectReplaced)
        onEffectReplaced (id);

    if (insert[ids::kind].toString() != model::pluginKind)
    {
        auto effect = std::shared_ptr<BuiltInEffect> (BuiltInEffect::create (insert[ids::effect].toString()).release());
        if (effect == nullptr)
            return nullptr;

        effect->prepare (sampleRate, maxBlockSize);
        const auto& params = effect->getInfo().params;
        for (size_t p = 0; p < params.size(); ++p)
            effect->setParameter ((int) p, (float) (double) insert.getProperty (params[p].id, params[p].defaultValue));

        entry = { key, sampleRate, effect };
        return effect;
    }

    juce::String state = insert[ids::pluginState].toString();
    if (sameKind)
        if (auto* existing = dynamic_cast<PluginEffect*> (entry.effect.get()))
            state = existing->getStateBase64();

    if (! sameKind)
        entry.effect = nullptr;

    entry.key = key;
    entry.sampleRate = sampleRate;
    loadPlugin (insert, key, sampleRate, maxBlockSize, state);
    return entry.effect; // the old one keeps running until the new one is ready
}

void EffectRack::loadPlugin (const juce::ValueTree& insert, const juce::String& key, double sampleRate,
                             int maxBlockSize, const juce::String& state)
{
    const auto id = insert[ids::id].toString();

    juce::PluginDescription description;
    auto xml = juce::parseXML (insert[ids::pluginDescription].toString());

    if (xml == nullptr || ! description.loadFromXml (*xml))
    {
        if (onLoadError)
            onLoadError ("The saved plugin description is damaged.");
        return;
    }

    std::weak_ptr<bool> weakAlive = alive;

    formatManager.createPluginInstanceAsync (description, sampleRate, maxBlockSize,
        [this, weakAlive, id, key, sampleRate, maxBlockSize, state, name = description.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
    {
        if (weakAlive.expired())
            return;

        auto it = entries.find (id);
        if (it == entries.end() || it->second.key != key)
            return;

        if (instance == nullptr)
        {
            if (onLoadError)
                onLoadError ("Couldn't load " + name + (error.isNotEmpty() ? ": " + error : juce::String()));
            return;
        }

        if (it->second.effect != nullptr && onEffectReplaced)
            onEffectReplaced (id);

        auto effect = std::make_shared<PluginEffect> (std::move (instance));
        effect->setStateBase64 (state);
        effect->prepare (sampleRate, maxBlockSize);
        it->second.effect = effect;

        if (onEffectReady)
            onEffectReady();
    });
}

void EffectRack::removeAllExcept (const juce::StringArray& insertIds)
{
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (insertIds.contains (it->first))
        {
            ++it;
            continue;
        }

        if (onEffectReplaced)
            onEffectReplaced (it->first);

        it = entries.erase (it);
    }
}

void EffectRack::storePluginStates (juce::ValueTree mixer)
{
    for (auto chain : mixer)
    {
        for (auto insert : chain)
        {
            auto it = entries.find (insert[ids::id].toString());
            if (it == entries.end())
                continue;

            if (auto* plugin = dynamic_cast<PluginEffect*> (it->second.effect.get()))
                insert.setProperty (ids::pluginState, plugin->getStateBase64(), nullptr);
        }
    }
}

juce::AudioPluginInstance* EffectRack::getPlugin (const juce::String& insertId)
{
    auto it = entries.find (insertId);
    if (it != entries.end())
        if (auto* plugin = dynamic_cast<PluginEffect*> (it->second.effect.get()))
            return &plugin->getPlugin();
    return nullptr;
}

BuiltInEffect* EffectRack::getBuiltIn (const juce::String& insertId)
{
    auto it = entries.find (insertId);
    return it != entries.end() ? dynamic_cast<BuiltInEffect*> (it->second.effect.get()) : nullptr;
}

} // namespace riff
