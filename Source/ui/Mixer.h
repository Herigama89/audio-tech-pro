#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "ConsoleLook.h"
#include "BusCompConsolePanel.h"
#include "StripParts.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"

namespace riff
{

class MixerPanel;

/**
    One console channel strip, built on the strip artwork, top to bottom: the
    EQ (HF, HMF, LMF, LF) with its IN lamp, mute and solo keys, Dynamics with
    its IN lamp, the Dynamics and pan displays, insert slots, sends, the fader
    with its meter and level display, and a name plate in the track's colour.
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
    static constexpr int stripWidth = 74;
    static constexpr float artScale = (float) stripWidth / strip::artWidth;
    static constexpr int topHeight = (int) (strip::artHeight * artScale);   // the artwork's part
    static constexpr int nameHeight = 26;
    static constexpr int minimumHeight = topHeight + 6 + numSlots * 17 + 6 + numSends * 18 + 100 + 20 + nameHeight;

private:
    bool isMaster() const { return ! track.isValid(); }
    bool hasSends() const;
    juce::ValueTree insertAt (int slot) const;
    juce::ValueTree sendAt (int index) const;
    void showSendMenu (int index);
    void setupConsoleKnob (juce::Slider& knob, const juce::Identifier& property,
                           double min, double max, const juce::String& name, const juce::String& suffix);
    juce::Rectangle<int> fromArt (float x, float y, float w, float h) const;
    juce::Rectangle<int> faderArea() const;

    MixerPanel& owner;
    juce::ValueTree project, track;

    // On the artwork: cream, blue, cream and red EQ knobs, the grey Dynamics knob
    strip::PointerKnob hfKnob { theme::ink }, hmfKnob, lmfKnob { theme::ink }, lfKnob, dynKnob;
    strip::LampKey consoleInButton { "EQ in", juce::Colour (0xffff3b2a) }, dynInButton { "Dynamics in", juce::Colour (0xffff3b2a) };
    strip::CreamKey muteButton { "M", theme::mute }, soloButton { "S", theme::solo };
    strip::DisplaySlider panKnob;
    juce::Label dynLabel;

    juce::OwnedArray<juce::TextButton> slots;
    juce::OwnedArray<juce::TextButton> sendButtons;
    juce::OwnedArray<juce::Slider> sendLevels;
    strip::Fader fader;
    LevelMeter meter { "" };
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
    MixerPanel (juce::UndoManager& um, AudioEngine& engine, ProjectPlayer& player, EffectRack& effects);
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

    /** Console mode's bus compressor face (shown instead of the built-in one when a UA plugin is available). */
    BusCompConsolePanel& getConsolePanel() noexcept { return consolePanel; }

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
    juce::Viewport outer;          // scrolls everything up and down when the pane is short
    juce::Component body;
    juce::Viewport viewport;       // scrolls the strips sideways
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    MasterBusCompressorPanel busComp;
    BusCompConsolePanel consolePanel;
    bool needsRebuild = false;
};

} // namespace riff
