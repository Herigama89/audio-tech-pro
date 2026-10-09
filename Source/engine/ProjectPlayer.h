#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include "AudioEngine.h"
#include "AudioFileCache.h"
#include "InstrumentRack.h"
#include "EffectRack.h"
#include "../model/Identifiers.h"

namespace riff
{

/**
    Keeps the engine in step with the project model.

    Listens to the project ValueTree; whenever it changes, it (asynchronously,
    so a burst of edits becomes one rebuild) builds a new PlaybackGraph and
    hands it to the engine. Also rebuilds when the device sample rate changes.
*/
class ProjectPlayer final : private juce::ValueTree::Listener,
                            private juce::AsyncUpdater,
                            private juce::ChangeListener
{
public:
    ProjectPlayer (AudioEngine& engine, AudioFileCache& cache, InstrumentRack* rack = nullptr,
                   EffectRack* effects = nullptr);

    /** The slot a track was given in the graph (for its meter), or -1. */
    int slotOf (const juce::String& trackId) const;
    ~ProjectPlayer() override;

    /** Switches to a different project tree (after New / Open). */
    void setProject (juce::ValueTree newProject);

    /** Builds and installs a graph right now (also used by tests). */
    void rebuildNow();

    /** Schedules a rebuild (e.g. when a plugin finishes loading). */
    void requestRebuild() { triggerAsyncUpdate(); }

    /** While a plugin knob is being written as automation, its lane's curve is left out (the knob plays). */
    void setLaneSuppressed (const juce::String& owner, const juce::String& param, bool suppressed)
    {
        if (suppressed) suppressedLanes.insert (owner + "|" + param);
        else            suppressedLanes.erase (owner + "|" + param);
    }

    /** While exporting stems every track plays as if nothing were soloed (mutes still count). */
    void setIgnoreSolo (bool shouldIgnore)  { ignoreSolo = shouldIgnore; }

    /** Which track your MIDI keyboard plays. */
    void setLiveMidiTrack (const juce::String& trackId);
    juce::String getLiveMidiTrack() const { return liveMidiTrackId; }

    /** Pure function: model -> graph. Exposed for tests. */
    std::unique_ptr<PlaybackGraph> buildGraph (double sampleRate);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override
    {
        if (p != ids::height && p != ids::hidden && p != ids::showAutomation && p != ids::automationParam && p != ids::collapsed && p != ids::icon && p != ids::takesOpen && p != ids::smartControls)   // view settings: nothing to hear
            triggerAsyncUpdate();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override                               { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override { rebuildNow(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    int slotFor (const juce::String& trackId);

    AudioEngine& engine;
    AudioFileCache& cache;
    InstrumentRack* rack;
    EffectRack* effects;

    std::vector<std::shared_ptr<Effect>> buildChain (const juce::String& owner, double sampleRate, juce::StringArray& insertIds,
                                                     std::map<juce::String, std::shared_ptr<Effect>>& effectsById);
    PlaybackGraph::Curve curveFor (const juce::String& owner, const juce::String& param, double sampleRate, bool decibelsToGain = false);
    void addParamAutomation (PlaybackGraph::Track& t, const juce::String& owner, double sampleRate,
                             const std::map<juce::String, std::shared_ptr<Effect>>& effectsById);
    juce::String liveMidiTrackId;
    bool ignoreSolo = false;
    std::set<juce::String> suppressedLanes;
    juce::ValueTree project;
    double builtForRate = 0.0;

    std::map<juce::String, int> slots;
    int nextSlot = 0;
};

} // namespace riff
