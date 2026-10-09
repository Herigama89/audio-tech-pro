#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "ConsoleLook.h"
#include "BusCompConsolePanel.h"
#include "StripParts.h"
#include "../engine/ConsolePlugins.h"
#include "../engine/EffectRack.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"
#include "../engine/AutomationWriter.h"

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

    /** The channel's gain reduction (its dynamics and compressors), shown on an amber LED column by the meter. */
    void pushGainReduction (float db);
    juce::String getOwnerId() const;
    juce::ValueTree getTrack() const { return track; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    /** Shown highlighted: the track selected in the tracks area too. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Console mode: follow knobs turned in the plugin's own window. */
    void syncFromPlugin();

    /** The fader and pan show what the automation is doing at `time` (unless you're holding them). */
    void followAutomation (double time);

    static constexpr int numSlots = 6;
    static constexpr int numSends = 2;
    static constexpr int stripWidth = 74;
    static constexpr float artScale = (float) stripWidth / strip::artWidth;
    static constexpr int topHeight = (int) (strip::artHeight * artScale);   // the artwork's part
    static constexpr int nameHeight = 26;
    static constexpr int automationRow = 18;
    static constexpr int ioRow = 17;   // the Input slot (above the inserts) and the Output slot (below the sends)
    static constexpr int minimumHeight = topHeight + 6 + 2 * ioRow + numSlots * 17 + 6 + numSends * 18 + ioRow + automationRow + 100 + 20 + nameHeight;

    /** Without the console section (EQ and Dynamics hidden, as in Logic): number, EQ/DYN tags, pan, mute and solo. */
    static constexpr int compactTopHeight = 92;
    static int minimumHeightFor (bool console) { return minimumHeight - (console ? 0 : topHeight - compactTopHeight); }

    /** The height of the top section in the mode the mixer is in. */
    int topSection() const;
    bool showsConsole() const;

private:
    bool isMaster() const { return ! track.isValid(); }
    bool isVca() const { return track.isValid() && track[ids::kind].toString() == "vca"; }

    /** Selects the track on any click on the strip (its knobs and fader included). */
    struct SelectOnClick final : juce::MouseListener
    {
        explicit SelectOnClick (ChannelStrip& s) : strip (s) {}
        void mouseDown (const juce::MouseEvent& e) override;
        ChannelStrip& strip;
    };
    SelectOnClick selectOnClick { *this };
    bool selected = false;
    std::map<juce::String, double> groupStart;   // other selected tracks' fader positions (dB) when a drag began
    double dragStartDb = 0.0;
    bool hasSends() const;
    juce::ValueTree insertAt (int slot) const;
    juce::ValueTree sendAt (int index) const;
    void showSendMenu (int index);
    void setupConsoleKnob (juce::Slider& knob, const juce::Identifier& property, console::StripControl control,
                           double min, double max, const juce::String& name, const juce::String& suffix);

    // Console mode: this track's channel strip plugin
    juce::ValueTree stripInsert() const;
    juce::AudioPluginInstance* stripInstance() const;
    juce::AudioProcessorParameter* stripParameter (console::StripControl c) const;
    int stripParameterIndex (console::StripControl c) const;
    bool usesPlugin() const { return stripInsert().isValid(); }
    void applyMode();
    void toggleStripSwitch (console::StripControl c, const juce::Identifier& builtInProperty, const juce::String& undoName);
    void showConsoleMenu();
    juce::Slider* knobFor (console::StripControl c);
    juce::Rectangle<int> fromArt (float x, float y, float w, float h) const;
    juce::Rectangle<int> faderArea() const;

    MixerPanel& owner;
    juce::ValueTree project, track;

    // On the artwork: cream, blue, cream and red EQ knobs, the grey Dynamics knob
    // Neon-ring encoders: blue for the EQ, white-blue for the Dynamics
    strip::PointerKnob hfKnob { theme::neon }, hmfKnob { theme::neon }, lmfKnob { theme::neon }, lfKnob { theme::neon }, dynKnob { theme::knobCream };
    strip::LampKey consoleInButton { "EQ in", theme::knobArc }, dynInButton { "Dynamics in", theme::knobArc };
    strip::CreamKey muteButton { "M", theme::mute }, soloButton { "S", theme::solo }, stripInButton { "IN", theme::knobArc };
    strip::DisplaySlider panKnob;
    juce::TextButton inputSlot, outputSlot;   // Logic's Input and Output slots
    juce::TextButton settingSlot { "Setting" };   // Logic's Setting button: copy, paste, reset, patches
    float grShown = 0.0f;
    juce::Rectangle<int> grArea() const;
    void showInputMenu();
    void showOutputMenu();
    juce::TextButton consoleTag { "EQ" };   // compact mode: lit when the strip's EQ/Dynamics (or plugin) is working; opens it
    void updateConsoleTag();
    juce::Label dynLabel;

    juce::OwnedArray<juce::TextButton> slots;
    juce::OwnedArray<juce::TextButton> sendButtons;
    juce::OwnedArray<juce::Slider> sendLevels;
    strip::Fader fader;
    LevelMeter meter { "" };
    juce::Label dbLabel;
    juce::TextButton automationButton;   // Off / Read / Touch / Latch / Write
    void showAutomationModeMenu();
    void updateAutomationButton();
    bool pluginMode = false;
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

    /** Open a track's channel strip in full, in its own window (the built-in strip; a plugin opens its own). */
    std::function<void (juce::ValueTree track)> onOpenStripWindow;

    /** A strip was clicked: select its track everywhere. */
    std::function<void (const juce::String& trackId, juce::ModifierKeys mods)> onSelectTrack;

    /** Strips shown elsewhere (the Inspector's): they get meter levels and plugin knob updates too. */
    void addExtraStrip (ChannelStrip* s)    { extraStrips.addIfNotAlreadyThere (s); }
    std::function<void()> onLayoutChanged;   // the strips' layout changed (EQ/Dyn shown or hidden)

    /** Lays every strip out again (after the skin changed). */
    void relayoutStrips();

    /** For the strips' Input slots: the interface's inputs, and an instrument track's instrument chooser. */
    std::function<juce::StringArray()> inputNames;
    std::function<void (juce::ValueTree track, juce::Component& target)> onInstrumentMenu;

    /** The strip's Setting button (copy / paste / reset the channel strip, save and load patches). */
    std::function<void (juce::ValueTree track, juce::Component& target)> onSettingMenu;
    void removeExtraStrip (ChannelStrip* s) { extraStrips.removeFirstMatchingValue (s); }

    const juce::StringArray& getSelectedTrackIds() const noexcept { return selectedTrackIds; }

    /** Highlights the selected tracks' strips and scrolls the main one into view. */
    void setSelectedTracks (const juce::StringArray& trackIds, const juce::String& mainTrackId);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Console mode's bus compressor face (shown instead of the built-in one when a UA plugin is available). */
    BusCompConsolePanel& getConsolePanel() noexcept { return consolePanel; }

    /** Console mode for the strips: the channel strip plugins found on this computer, best first. */
    void setStripCandidates (const juce::Array<juce::PluginDescription>& found);
    const juce::Array<juce::PluginDescription>& getStripCandidates() const noexcept { return stripCandidates; }

    /** The mixer's console was chosen in the header: a plugin, or none for the built-in strip. */
    std::function<void (const juce::PluginDescription* plugin)> onChooseConsole;

    /** A plugin finished loading: strips switch to it. */
    void pluginsChanged() { triggerAsyncUpdate(); }

    EffectRack& getEffects() noexcept { return effects; }
    juce::ValueTree getProject() const { return project; }

    static constexpr int headerHeight = 28;

    /** Whether the strips show their console section, and the bus compressor its panel (both off by default). */
    bool showsConsole() const { return (bool) project.getProperty ("mixerConsole", false); }
    bool showsBusComp() const { return (bool) project.getProperty ("mixerBusComp", false); }

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
    EffectRack& effects;
    juce::ValueTree project;

    juce::Label title, consoleCaption { {}, "Console" };
    juce::ComboBox consoleBox, filterBox;   // filterBox: which kinds of strip to show
    friend class SnapshotTool;
    juce::TextButton narrowButton { "Narrow" };   // smaller strips, to see more at once
    juce::TextButton consoleButton { "EQ/Dyn" };  // show the console section (EQ and Dynamics) on the strips
    juce::TextButton busCompButton { "Bus Comp" }; // show the Master Bus Compressor beside the master
    float stripScale() const;
    juce::Array<juce::PluginDescription> stripCandidates;
    int syncCounter = 0;
    void refreshConsoleBox();
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
    juce::String selectedTrackId;
    juce::StringArray selectedTrackIds;
    juce::Array<ChannelStrip*> extraStrips;
};

} // namespace riff
