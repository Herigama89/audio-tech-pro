#pragma once

#include <JuceHeader.h>
#include <map>
#include "AudioEngine.h"
#include "AudioFileCache.h"
#include "InstrumentRack.h"

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
    ProjectPlayer (AudioEngine& engine, AudioFileCache& cache, InstrumentRack* rack = nullptr);
    ~ProjectPlayer() override;

    /** Switches to a different project tree (after New / Open). */
    void setProject (juce::ValueTree newProject);

    /** Builds and installs a graph right now (also used by tests). */
    void rebuildNow();

    /** Schedules a rebuild (e.g. when a plugin finishes loading). */
    void requestRebuild() { triggerAsyncUpdate(); }

    /** Which track your MIDI keyboard plays. */
    void setLiveMidiTrack (const juce::String& trackId);
    juce::String getLiveMidiTrack() const { return liveMidiTrackId; }

    /** Pure function: model -> graph. Exposed for tests. */
    std::unique_ptr<PlaybackGraph> buildGraph (double sampleRate);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
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
    juce::String liveMidiTrackId;
    juce::ValueTree project;
    double builtForRate = 0.0;

    std::map<juce::String, int> slots;
    int nextSlot = 0;
};

} // namespace riff
