#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"

namespace riff
{

class MixerPanel;

/** One channel strip: name, insert slots, pan, fader with meter, mute and solo. */
class ChannelStrip final : public juce::Component
{
public:
    /** `track` is invalid for the master strip. */
    ChannelStrip (MixerPanel& owner, juce::ValueTree project, juce::ValueTree track);

    void refresh();
    void pushLevels (float left, float right) { meter.push (left, right); }
    juce::String getOwnerId() const;
    juce::ValueTree getTrack() const { return track; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int numSlots = 6;
    static constexpr int stripWidth = 98;

private:
    bool isMaster() const { return ! track.isValid(); }
    juce::ValueTree insertAt (int slot) const;

    MixerPanel& owner;
    juce::ValueTree project, track;

    juce::Label nameLabel;
    juce::OwnedArray<juce::TextButton> slots;
    juce::Slider panKnob, fader;
    LevelMeter meter { "" };
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Label dbLabel;
};

/**
    The mixer: a strip per track plus the master strip, shown in the bottom pane.
    Every control writes to the project (undoably), exactly like the track headers.
*/
class MixerPanel final : public juce::Component,
                         private juce::ValueTree::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    MixerPanel (juce::UndoManager& um, AudioEngine& engine, ProjectPlayer& player);
    ~MixerPanel() override;

    void setProject (juce::ValueTree newProject);
    void setMasterLevels (float left, float right);

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Open the window of an insert. */
    std::function<void (juce::ValueTree insert)> onOpenInsert;

    /** Show the "choose an effect" menu for a slot (owner is a track id or "master"). */
    std::function<void (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)> onChooseEffect;

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override        { changed (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { changed (parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override         { changed (parent); }
    void changed (const juce::ValueTree& parent);

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void rebuild();

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    ProjectPlayer& player;
    juce::ValueTree project;

    juce::Label title;
    juce::TextButton closeButton { "Close" };
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    bool needsRebuild = false;
};

} // namespace riff
