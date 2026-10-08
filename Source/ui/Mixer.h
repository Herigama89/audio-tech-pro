#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "ConsoleLook.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"

namespace riff
{

class MixerPanel;

/**
    One console channel strip, top to bottom: the console section (four-band
    EQ, Dynamics, pan), insert slots, sends, the fader with its meter, mute
    and solo, and a name plate in the track's colour.
*/
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
    static constexpr int numSends = 2;
    static constexpr int stripWidth = 100;
    static constexpr int consoleHeight = 150;   // the EQ / Dynamics / pan section
    static constexpr int minimumHeight = 470;

private:
    bool isMaster() const { return ! track.isValid(); }
    bool hasSends() const;
    juce::ValueTree insertAt (int slot) const;
    juce::ValueTree sendAt (int index) const;
    void showSendMenu (int index);
    void setupConsoleKnob (juce::Slider& knob, const juce::Identifier& property, look::Knob cap,
                           double min, double max, const juce::String& name, const juce::String& suffix);

    MixerPanel& owner;
    juce::ValueTree project, track;

    // Console section
    juce::Slider hfKnob, hmfKnob, lmfKnob, lfKnob, dynKnob;
    juce::TextButton consoleInButton { "IN" };

    juce::OwnedArray<juce::TextButton> slots;
    juce::OwnedArray<juce::TextButton> sendButtons;
    juce::OwnedArray<juce::Slider> sendLevels;
    juce::Slider panKnob, fader;
    LevelMeter meter { "" };
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Label dbLabel;
};

/**
    The Master Bus Compressor's face: a gain-reduction VU meter and stepped
    controls, like a console's centre-section bus compressor.
*/
class MasterBusCompressorPanel final : public juce::Component
{
public:
    MasterBusCompressorPanel (juce::UndoManager& um, AudioEngine& engine);

    void setProject (juce::ValueTree newProject);
    void refresh();
    void updateMeter();

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int panelWidth = 252;

private:
    void setup (juce::Slider& knob, const juce::Identifier& property, look::Knob cap, double min, double max, double step,
                std::function<juce::String (double)> text, const juce::String& name);

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    juce::ValueTree project;

    GainReductionMeter vu;
    juce::Slider threshold, makeup, attack, release, ratio, mix;
    juce::TextButton inButton { "IN" };

    struct Caption { juce::Slider* knob; juce::String name; std::function<juce::String (double)> text; };
    std::vector<Caption> captions;
};

/**
    The mixer: a strip per track, then the Master Bus Compressor and the
    master strip on the right. Every control writes to the project
    (undoably), exactly like the track headers.
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

    static constexpr int headerHeight = 28;

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
    MasterBusCompressorPanel busComp;
    bool needsRebuild = false;
};

} // namespace riff
