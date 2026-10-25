#pragma once

#include <JuceHeader.h>
#include <map>
#include "BuiltInEffects.h"
#include "AraHost.h"
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
    void setSidechain (const float* l, const float* r) noexcept override { sideL = l; sideR = r; }
    bool acceptsSidechain() const override { return plugin->getBusCount (true) > 1; }

    void setPosition (juce::int64 samplePosition, bool playing, double bpm) noexcept override
    {
        playHead.samples.store (samplePosition, std::memory_order_relaxed);
        playHead.playing.store (playing, std::memory_order_relaxed);
        playHead.bpm.store (bpm, std::memory_order_relaxed);
    }

    /** Runs `change` with the plugin stopped (not processing, its resources released), then starts it
        again: ARA needs this to add or remove regions. The audio thread hears silence from it meanwhile. */
    void withPluginStopped (const std::function<void()>& change);

    /** Set for an ARA plugin playing its track's regions: it makes the sound itself (silence when it can't). */
    void setRendersRegions (bool shouldRender) noexcept { rendersRegions.store (shouldRender); }

    /** Extensions (Cubase style): the plugin plays only its regions, added to the track's other audio (it hears silence). */
    void setExtensionMode (bool on) noexcept { extensionMode.store (on); }
    juce::String getName() const override { return plugin->getName(); }
    int getLatencySamples() const override { return plugin->getLatencySamples(); }
    void setAutomatedParameter (int index, float value) noexcept override
    {
        const auto& params = plugin->getParameters();
        if (juce::isPositiveAndBelow (index, params.size()))
            params[index]->setValue (juce::jlimit (0.0f, 1.0f, value));
    }

    /** Plugins that publish their gain reduction as a meter parameter ("Gain Reduction", "GR"...) show it on the strip. */
    float takeGainReductionDb() noexcept override;

    juce::AudioPluginInstance& getPlugin() noexcept { return *plugin; }

    /** Hears knob moves made in the plugin's own window: (parameter index, value 0..1, phase: 0 grab, 1 move, 2 let go).
        Called on the message thread. */
    void watchParameters (std::function<void (int, float, int)> callback);
    juce::String getStateBase64() const;
    void setStateBase64 (const juce::String& state);

private:
    /** The song's position, given to the plugin as its host's playhead. */
    struct PlayHead final : juce::AudioPlayHead
    {
        std::atomic<juce::int64> samples { 0 };
        std::atomic<bool> playing { false };
        std::atomic<double> bpm { 120.0 };
        double rate = 48000.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            const auto s = samples.load (std::memory_order_relaxed);
            const auto tempo = bpm.load (std::memory_order_relaxed);
            info.setTimeInSamples (s);
            info.setTimeInSeconds ((double) s / rate);
            info.setIsPlaying (playing.load (std::memory_order_relaxed));
            info.setBpm (tempo);
            info.setPpqPosition ((double) s / rate * tempo / 60.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return info;
        }
    } playHead;
    juce::SpinLock processLock;
    std::atomic<bool> rendersRegions { false }, extensionMode { false };
    int preparedBlock = 0;

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
    juce::AudioProcessorParameter* grParameter = nullptr;
    float grLastValue = -1.0f, grLastDb = -1.0f;
    int grAsked = 0;
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    bool grSearched = false;
};

/**
    Owns the effect behind every INSERT in the mixer, keyed by insert id.
    Like the InstrumentRack, effects live here so their state (delay lines,
    reverb tails, plugin settings) survives graph rebuilds. Message thread only.
*/
class EffectRack
{
public:
    /** A built-in's knob turned (no insert added, removed or switched): its values go straight to the running
        effect, without a new graph. False when that isn't enough (not running yet, a plug-in, its latency moved). */
    bool applyParameters (const juce::ValueTree& insert);

    explicit EffectRack (juce::AudioPluginFormatManager& formats) : formatManager (formats) {}

    /** The effect for an insert, created or updated to match it; nullptr while a plugin loads. */
    std::shared_ptr<Effect> effectFor (const juce::ValueTree& insert, double sampleRate, int maxBlockSize);

    void removeAllExcept (const juce::StringArray& insertIds);
    void storePluginStates (juce::ValueTree mixer);

    juce::AudioPluginInstance* getPlugin (const juce::String& insertId);

    /** A running plug-in's state now (base64), for a preset; empty if it isn't a plug-in or isn't loaded. */
    juce::String captureState (const juce::String& insertId);
    /** Puts a state (a preset's) into the running plug-in. */
    void applyState (const juce::String& insertId, const juce::String& base64);

    /** Tests: makes a plugin insert use the given effect, as if it had loaded. */
    void injectForTesting (const juce::ValueTree& insert, std::shared_ptr<Effect> effect, double sampleRate)
    {
        entries[insert[ids::id].toString()] = { keyFor (insert), sampleRate, std::move (effect) };
    }
    BuiltInEffect* getBuiltIn (const juce::String& insertId);

    /** ARA: plugins like Melodyne play their track's regions themselves. The rack reads audio files with these. */
    void setAudioFormats (juce::AudioFormatManager* formats) noexcept { audioFormats = formats; }

    /** True when this insert is an ARA plugin bound to its track's regions (the engine then leaves them to it). */
    bool isAraInsert (const juce::String& insertId) const { return araDocs.count (insertId) > 0; }

    /** Brings every ARA plugin's regions in line with its track (after the project changed). */
    void syncAra (const juce::ValueTree& project);

    /** An ARA plugin is ready (its regions follow), or asked to start / stop / move the playhead. */
    std::function<void()> onAraReady;
    std::function<void (bool start)> onAraPlaybackRequest;
    std::function<void (double seconds)> onAraPositionRequest;

    /** An insert's gain reduction since the last call (dB), for the strip's meter. */
    float takeGainReduction (const juce::String& insertId);

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
        juce::uint64 applied = 0;   // a built-in's knob values as last given it (a signature), so unchanged ones are left alone
    };
    static juce::uint64 signatureOf (const juce::ValueTree& insert);
    void applyBuiltIn (BuiltInEffect& effect, const juce::ValueTree& insert);

    static juce::String keyFor (const juce::ValueTree& insert);
    void loadPlugin (const juce::ValueTree& insert, const juce::String& key, double sampleRate, int maxBlockSize,
                     const juce::String& state);

    juce::AudioPluginFormatManager& formatManager;
    std::map<juce::String, Entry> entries;
    juce::AudioFormatManager* audioFormats = nullptr;
    std::map<juce::String, std::unique_ptr<AraDocument>> araDocs;   // (declared after entries: they go first)
    void dropAra (const juce::String& insertId) { araDocs.erase (insertId); }
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace riff
