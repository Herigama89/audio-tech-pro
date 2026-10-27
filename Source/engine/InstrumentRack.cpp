#include "InstrumentRack.h"
#include "../model/Project.h"
#include "BuiltInInstruments.h"
#include "nemix/Nemix.h"
#include "../model/NemixState.h"
#if ! JUCE_WASM
 #include "ClapFormat.h"
 #include "PluginScanChild.h"
#endif

namespace riff
{

InstrumentRack::InstrumentRack()
{
   #if ! JUCE_WASM   // (the web version: built-in instruments and effects only)
    juce::addDefaultFormatsToManager (formatManager); // VST3, plus Audio Units on macOS
    formatManager.addFormat (std::make_unique<ClapPluginFormat>());
    knownPlugins.setCustomScanner (std::make_unique<pluginscan::OutOfProcessScanner>());   // (a crashing plugin can't take the app down)
   #endif
    sampleFormats.registerBasicFormats();
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

void InstrumentRack::applyBuiltInSettings (Instrument* instrument, const juce::ValueTree& track)
{
    const auto level = (float) (double) track.getProperty (ids::synthGain, 0.8);
    if (auto* kit = dynamic_cast<DrumKit*> (instrument))
        kit->setSettings ((float) (double) track.getProperty (ids::drumTune, 0.0), (float) (double) track.getProperty (ids::drumDecay, 1.0), level);
    else if (auto* sampler = dynamic_cast<Sampler*> (instrument))
        sampler->setSettings ((int) track.getProperty (ids::samplerRoot, 60), (float) (double) track.getProperty (ids::synthAttack, 0.002),
                              (float) (double) track.getProperty (ids::synthRelease, 0.2), level);
}

juce::String InstrumentRack::keyFor (const juce::ValueTree& track)
{
    if (! model::isInstrumentTrack (track))
        return {};

    if (track[ids::instrument].toString() == "plugin")
        return "plugin:" + track[ids::pluginDescription].toString();
    if (track[ids::instrument].toString() == "drums")
        return "drums";
    if (track[ids::instrument].toString() == "sampler")
        return "sampler:" + track[ids::samplerFile].toString();
    if (track[ids::instrument].toString() == "nemix")
        return "nemix";

    return "synth";
}

bool InstrumentRack::applyNemix (const juce::ValueTree& nemixTree, const juce::Identifier& property)
{
    const auto track = nemixTree.getParent();
    const auto it = entries.find (track[ids::id].toString());
    if (it == entries.end() || it->second.key != "nemix")
        return false;
    auto* synth = dynamic_cast<Nemix*> (it->second.instrument.get());
    if (synth == nullptr)
        return false;
    const auto index = nemix::indexOf (property.toString());
    if (index >= 0)
        synth->setParameter (index, nemixstate::valueOf (nemixTree, index));
    return true;
}

std::shared_ptr<Nemix> InstrumentRack::getNemix (const juce::String& trackId)
{
    const auto it = entries.find (trackId);
    if (it == entries.end())
        return nullptr;
    return std::dynamic_pointer_cast<Nemix> (it->second.instrument);
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

    // Nemix: its whole sound from the track (each time: a preset may have replaced it)
    const auto applyNemixSound = [] (Nemix& synth, const juce::ValueTree& t)
    {
        synth.resetToDefaults();
        for (const auto& [index, value] : nemixstate::soundOf (nemixstate::of (t)))
            synth.setParameter (index, value);
    };
    if (key == "nemix")
    {
        if (sameKind && rateOk)
        {
            if (auto* synth = dynamic_cast<Nemix*> (entry.instrument.get()))
                applyNemixSound (*synth, track);
            return entry.instrument;
        }
        if (! sameKind && onPluginReplaced && entry.instrument != nullptr)
            onPluginReplaced (id);
        auto synth = std::make_shared<Nemix>();
        applyNemixSound (*synth, track);
        synth->prepare (sampleRate, maxBlockSize);
        entry = { key, sampleRate, synth, false };
        return synth;
    }

    // Already set up (or loading) for this instrument at this rate.
    if (sameKind && rateOk)
    {
        if (auto* synth = dynamic_cast<BuiltInSynth*> (entry.instrument.get()))
            synth->setSettings (synthSettingsFrom (track));
        applyBuiltInSettings (entry.instrument.get(), track);

        return entry.instrument;
    }

    // The built-in Drum Kit and Sampler.
    if (key == "drums" || key.startsWith ("sampler:"))
    {
        if (! sameKind && onPluginReplaced && entry.instrument != nullptr)
            onPluginReplaced (id);

        std::shared_ptr<Instrument> instrument;
        if (key == "drums")
            instrument = std::make_shared<DrumKit>();
        else
        {
            auto sampler = std::make_shared<Sampler>();
            const juce::File file (track[ids::samplerFile].toString());
            if (std::unique_ptr<juce::AudioFormatReader> reader (sampleFormats.createReaderFor (file)); reader != nullptr)
            {
                const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
                auto buffer = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jlimit (1u, 2u, reader->numChannels), juce::jmax (1, length));
                reader->read (buffer.get(), 0, length, 0, true, buffer->getNumChannels() > 1);
                sampler->setSample (buffer, reader->sampleRate);
            }
            instrument = sampler;
        }
        instrument->prepare (sampleRate, maxBlockSize);
        applyBuiltInSettings (instrument.get(), track);
        entry = { key, sampleRate, instrument, false };
        return instrument;
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
