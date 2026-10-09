#pragma once

#include <JuceHeader.h>
#include <map>
#include "BuiltInEffects.h"
#include "../model/Identifiers.h"

namespace riff
{

/** A hosted VST3 / Audio Unit effect, run in stereo. */
class PluginEffect final : public Effect
{
public:
    explicit PluginEffect (std::unique_ptr<juce::AudioPluginInstance> plugin);
    ~PluginEffect() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (float* left, float* right, int numSamples) noexcept override;
    juce::String getName() const override { return plugin->getName(); }
    int getLatencySamples() const override { return plugin->getLatencySamples(); }
    void setAutomatedParameter (int index, float value) noexcept override
    {
        const auto& params = plugin->getParameters();
        if (juce::isPositiveAndBelow (index, params.size()))
            params[index]->setValue (juce::jlimit (0.0f, 1.0f, value));
    }

    juce::AudioPluginInstance& getPlugin() noexcept { return *plugin; }

    /** Hears knob moves made in the plugin's own window: (parameter index, value 0..1, phase: 0 grab, 1 move, 2 let go).
        Called on the message thread. */
    void watchParameters (std::function<void (int, float, int)> callback);
    juce::String getStateBase64() const;
    void setStateBase64 (const juce::String& state);

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer noMidi;
    bool prepared = false;

    struct Watcher final : juce::AudioProcessorParameter::Listener
    {
        std::function<void (int, float, int)> callback;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        void post (int index, float value, int phase)
        {
            juce::MessageManager::callAsync ([weak = std::weak_ptr<bool> (alive), cb = callback, index, value, phase]
            {
                if (! weak.expired() && cb)
                    cb (index, value, phase);
            });
        }
        void parameterValueChanged (int index, float value) override { post (index, value, 1); }
        void parameterGestureChanged (int index, bool starting) override { post (index, -1.0f, starting ? 0 : 2); }
    };
    std::unique_ptr<Watcher> watcher;
};

/**
    Owns the effect behind every INSERT in the mixer, keyed by insert id.
    Like the InstrumentRack, effects live here so their state (delay lines,
    reverb tails, plugin settings) survives graph rebuilds. Message thread only.
*/
class EffectRack
{
public:
    explicit EffectRack (juce::AudioPluginFormatManager& formats) : formatManager (formats) {}

    /** The effect for an insert, created or updated to match it; nullptr while a plugin loads. */
    std::shared_ptr<Effect> effectFor (const juce::ValueTree& insert, double sampleRate, int maxBlockSize);

    void removeAllExcept (const juce::StringArray& insertIds);
    void storePluginStates (juce::ValueTree mixer);

    juce::AudioPluginInstance* getPlugin (const juce::String& insertId);

    /** Tests: makes a plugin insert use the given effect, as if it had loaded. */
    void injectForTesting (const juce::ValueTree& insert, std::shared_ptr<Effect> effect, double sampleRate)
    {
        entries[insert[ids::id].toString()] = { keyFor (insert), sampleRate, std::move (effect) };
    }
    BuiltInEffect* getBuiltIn (const juce::String& insertId);

    std::function<void()> onEffectReady;
    std::function<void (const juce::String& error)> onLoadError;
    std::function<void (const juce::String& insertId)> onEffectReplaced;   // close its window

    /** A hosted plugin's knob moved in its own window (for writing automation): insert id, parameter, value, phase. */
    std::function<void (const juce::String& insertId, int index, float value, int phase)> onPluginParameter;

private:
    struct Entry
    {
        juce::String key;
        double sampleRate = 0.0;
        std::shared_ptr<Effect> effect;
    };

    static juce::String keyFor (const juce::ValueTree& insert);
    void loadPlugin (const juce::ValueTree& insert, const juce::String& key, double sampleRate, int maxBlockSize,
                     const juce::String& state);

    juce::AudioPluginFormatManager& formatManager;
    std::map<juce::String, Entry> entries;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace riff
