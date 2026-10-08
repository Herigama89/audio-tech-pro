#include "InstrumentRack.h"
#include "../model/Project.h"

namespace riff
{

InstrumentRack::InstrumentRack()
{
    juce::addDefaultFormatsToManager (formatManager); // VST3, plus Audio Units on macOS
}

BuiltInSynth::Settings InstrumentRack::synthSettingsFrom (const juce::ValueTree& track)
{
    BuiltInSynth::Settings s;
    s.wave      = track.getProperty (ids::synthWave, 0);
    s.cutoff    = (float) (double) track.getProperty (ids::synthCutoff, 3000.0);
    s.resonance = (float) (double) track.getProperty (ids::synthResonance, 0.2);
    s.attack    = (float) (double) track.getProperty (ids::synthAttack, 0.005);
    s.decay     = (float) (double) track.getProperty (ids::synthDecay, 0.3);
    s.sustain   = (float) (double) track.getProperty (ids::synthSustain, 0.6);
    s.release   = (float) (double) track.getProperty (ids::synthRelease, 0.3);
    s.gain      = (float) (double) track.getProperty (ids::synthGain, 0.5);
    return s;
}

juce::String InstrumentRack::keyFor (const juce::ValueTree& track)
{
    if (! model::isInstrumentTrack (track))
        return {};

    if (track[ids::instrument].toString() == "plugin")
        return "plugin:" + track[ids::pluginDescription].toString();

    return "synth";
}

std::shared_ptr<Instrument> InstrumentRack::instrumentFor (const juce::ValueTree& track, double sampleRate, int maxBlockSize)
{
    const auto id = track[ids::id].toString();
    const auto key = keyFor (track);

    if (key.isEmpty())
        return nullptr;

    auto& entry = entries[id];
    const bool sameKind = entry.key == key;
    const bool rateOk = std::abs (entry.sampleRate - sampleRate) < 0.5;

    // Already set up (or loading) for this instrument at this rate.
    if (sameKind && rateOk)
    {
        if (auto* synth = dynamic_cast<BuiltInSynth*> (entry.instrument.get()))
            synth->setSettings (synthSettingsFrom (track));

        return entry.instrument;
    }

    if (key == "synth")
    {
        if (! sameKind)
            if (onPluginReplaced && entry.instrument != nullptr)
                onPluginReplaced (id);

        auto synth = std::make_shared<BuiltInSynth>();
        synth->prepare (sampleRate, maxBlockSize);
        synth->setSettings (synthSettingsFrom (track));
        entry = { key, sampleRate, synth, false };
        return synth;
    }

    // A plugin. Carry its settings over if only the sample rate changed.
    juce::String state = track[ids::pluginState].toString();

    if (sameKind)
    {
        if (auto* existing = dynamic_cast<PluginInstrument*> (entry.instrument.get()))
            state = existing->getStateBase64();
    }
    else
    {
        if (onPluginReplaced && entry.instrument != nullptr)
            onPluginReplaced (id);
        entry.instrument = nullptr;
    }

    entry.key = key;
    entry.sampleRate = sampleRate;
    entry.loading = true;
    loadPlugin (track, key, sampleRate, maxBlockSize, state);

    return entry.instrument; // the old one keeps playing until the new one is ready
}

void InstrumentRack::loadPlugin (const juce::ValueTree& track, const juce::String& key, double sampleRate,
                                 int maxBlockSize, const juce::String& state)
{
    const auto id = track[ids::id].toString();
    const auto trackName = track[ids::name].toString();

    juce::PluginDescription description;
    auto xml = juce::parseXML (track[ids::pluginDescription].toString());

    if (xml == nullptr || ! description.loadFromXml (*xml))
    {
        entries[id].loading = false;
        if (onLoadError)
            onLoadError (trackName, "The saved plugin description is damaged.");
        return;
    }

    std::weak_ptr<bool> weakAlive = alive;

    formatManager.createPluginInstanceAsync (description, sampleRate, maxBlockSize,
        [this, weakAlive, id, key, sampleRate, maxBlockSize, state, trackName, name = description.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
    {
        if (weakAlive.expired())
            return;

        auto it = entries.find (id);
        if (it == entries.end() || it->second.key != key)
            return; // the track changed instrument while this was loading

        it->second.loading = false;

        if (instance == nullptr)
        {
            if (onLoadError)
                onLoadError (trackName, "Couldn't load " + name + (error.isNotEmpty() ? ": " + error : juce::String()));
            return;
        }

        if (it->second.instrument != nullptr && onPluginReplaced)
            onPluginReplaced (id); // its window shows the old instance

        auto plugin = std::make_shared<PluginInstrument> (std::move (instance));
        plugin->setStateBase64 (state);
        plugin->prepare (sampleRate, maxBlockSize);
        it->second.instrument = plugin;

        if (onInstrumentReady)
            onInstrumentReady();
    });
}

void InstrumentRack::removeAllExcept (const juce::StringArray& trackIds)
{
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (trackIds.contains (it->first))
        {
            ++it;
            continue;
        }

        if (onPluginReplaced && dynamic_cast<PluginInstrument*> (it->second.instrument.get()) != nullptr)
            onPluginReplaced (it->first);

        it = entries.erase (it);
    }
}

void InstrumentRack::storePluginStates (juce::ValueTree project)
{
    for (auto track : project)
    {
        auto it = entries.find (track[ids::id].toString());
        if (it == entries.end())
            continue;

        if (auto* plugin = dynamic_cast<PluginInstrument*> (it->second.instrument.get()))
            track.setProperty (ids::pluginState, plugin->getStateBase64(), nullptr);
    }
}

juce::AudioPluginInstance* InstrumentRack::getPlugin (const juce::String& trackId)
{
    auto it = entries.find (trackId);
    if (it == entries.end())
        return nullptr;

    if (auto* plugin = dynamic_cast<PluginInstrument*> (it->second.instrument.get()))
        return &plugin->getPlugin();

    return nullptr;
}

} // namespace riff
