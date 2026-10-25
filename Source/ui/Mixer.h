#pragma once

#include <JuceHeader.h>
#include "ParameterNames.h"
#include <set>
#include "LevelMeter.h"
#include "ConsoleLook.h"
#include "BusCompConsolePanel.h"
#include "StripParts.h"
#include "../engine/ConsolePlugins.h"
#include "../engine/EffectRack.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPlayer.h"
#include "../engine/AutomationWriter.h"
#include "../engine/InterfaceControl.h"

namespace riff
{

class MixerPanel;

/**
    One console channel strip, built on the strip artwork, top to bottom: the
    EQ (HF, HMF, LMF, LF) with its IN lamp, mute and solo keys, Dynamics with
    its IN lamp, the Dynamics and pan displays, insert slots, sends, the fader
    with its meter and level display, and a name plate in the track's colour.
*/
class ChannelStrip final : public juce::Component,
                           public juce::DragAndDropTarget
{
public:
    /** `track` is invalid for the master strip. */
    ChannelStrip (MixerPanel& owner, juce::ValueTree project, juce::ValueTree track);

    void refresh();
    void pushLevels (float left, float right)
    {
        meter.push (left, right);
        if (! vuArea.isEmpty())
        {
            // The VU: 0 VU at -18 dBFS, with a needle's sluggish rise and fall
            const auto vu = juce::jlimit (-22.0f, 4.0f, juce::Decibels::gainToDecibels (juce::jmax (left, right), -100.0f) + 18.0f);
            const auto next = vuNeedle + (vu - vuNeedle) * (vu > vuNeedle ? 0.28f : 0.12f);
            if (std::abs (next - vuNeedle) > 0.1f)
            {
                vuNeedle = next;
                repaint (vuArea);
            }
        }
    }
    juce::Rectangle<int> vuArea;   // (empty when the strips have no VU)
    float vuNeedle = -22.0f;

    /** The channel's gain reduction (its dynamics and compressors), shown on an amber LED column by the meter. */
    void pushGainReduction (float db);
    juce::String getOwnerId() const;
    juce::ValueTree getTrack() const { return track; }
    juce::ValueTree getProject() const { return project; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void paintOverChildren (juce::Graphics& g) override;

    // Inserts dragged between slots and channels (Option: a copy), as in Logic
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    static constexpr const char* insertDragPrefix = "insert:";

    /** Shown highlighted: the track selected in the tracks area too. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Console mode: follow knobs turned in the plugin's own window. */
    void syncFromPlugin();

    /** The fader and pan show what the automation is doing at `time` (unless you're holding them). */
    void followAutomation (double time);

    /** Insert slots and sends: at least the defaults; when they fill up, more appear (as in Logic). */
    static constexpr int defaultInserts = 6, maxInserts = 15, defaultSends = 2, maxSends = 8;
    static constexpr int insertRowHeight = 17, sendRowHeight = 25;
    static constexpr int stripWidth = 74;
    static constexpr float artScale = (float) stripWidth / strip::artWidth;
    static constexpr int topHeight = (int) (strip::artHeight * artScale);   // the artwork's part
    static constexpr int nameHeight = 26;
    static constexpr int automationRow = 18;
    static constexpr int ioRow = 17;   // the Input slot (above the inserts) and the Output slot (below the sends)
    static constexpr int vuBand = 38;   // Themes > Strip VU meters: the amber VU at the top of each strip
    static constexpr int tapeRow = 44;  // under the Output slot: the analog preamp (PRE key, its Gain), then the tape machine (TAPE key, its Drive)
    static constexpr int preampRowHeight = 18;   // the interface's preamp (48V, gain), under the Input slot, when it's connected
    static inline bool preampRows = false;
    static int preampRow() { return preampRows ? preampRowHeight : 0; }
    static int minimumHeightFor (bool console, int inserts, int sends)
    {
        return (console ? topHeight : compactTopHeight) + 6 + 2 * ioRow + preampRow() + inserts * insertRowHeight + 6 + sends * sendRowHeight
             + ioRow + tapeRow + automationRow + 100 + 20 + nameHeight + (theme::stripVu() ? vuBand : 0);
    }

    /** Without the console section (EQ and Dynamics hidden, as in Logic): number, EQ/DYN tags, pan, mute and solo. */
    static constexpr int compactTopHeight = 92;

    /** The height of the top section in the mode the mixer is in. */
    int topSection() const;
    bool showsConsole() const;

private:
    friend class SnapshotTool;
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
    mutable ParameterNames parameterNames;   // (the strip plugin's, read once)
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
    // Record enable and input monitoring: the same switches as the track header's R and I (either one turns them on or off).
    strip::CreamKey armKey { "R", juce::Colour (0xffe2463f) }, monitorKey { "I", juce::Colour (0xffe8a33c) };
    juce::TextButton multiMeterKey { "MM" };   // the master: opens the MultiMeter
    bool canRecord() const;
    strip::DisplaySlider panKnob;
    juce::TextButton inputSlot, outputSlot;   // Logic's Input and Output slots
    juce::TextButton settingSlot { "Setting" };   // Logic's Setting button: copy, paste, reset, patches
    // Tape saturation (every channel, bus and the master, as on a console that runs everything through tape)
    juce::TextButton tapeKey { "TAPE" };
    strip::DisplaySlider tapeDrive;
    juce::ValueTree tapeTree() const { return isMaster() ? project : track; }
    juce::Identifier tapeId (const char* what) const { return juce::Identifier ((isMaster() ? juce::String ("masterTape") : juce::String ("tape")) + what); }
    void showTapeMenu();
    void updateTape();
    // The analog preamp (a class-A, transformer console preamp): its Gain turns the channel up and drives it
    juce::TextButton preKey { "PRE" };
    strip::DisplaySlider preGain;
    bool hasAnalogPre() const { return ! isMaster() && ! isVca(); }
    // The interface's preamp for this channel's input (Logic's Audio Device Controls)
    juce::TextButton phantomKey { "48V" };
    strip::DisplaySlider preampGain;
    juce::Array<int> preampChannels() const;   // the interface's input numbers (1-based) this channel records
    void updatePreamp();
    void showPreampMenu();
    bool preampClipped = false;
    float grShown = 0.0f;
    juce::Rectangle<int> grArea() const;
    /** The gain-reduction LEDs: a little opaque column of their own, so their updates redraw only themselves. */
    class GrColumn final : public juce::Component
    {
    public:
        GrColumn() { setOpaque (true); setInterceptsMouseClicks (false, false); }
        void setLit (int n) { if (n != lit) { lit = n; repaint(); } }
        int leds() const { return juce::jmax (4, (getHeight() - 2) / 4); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff141616));
            const auto r = getLocalBounds().reduced (1).toFloat();
            const int count = leds();
            for (int i = 0; i < count; ++i)
            {
                const auto y = r.getY() + (float) i * r.getHeight() / (float) count;
                g.setColour (i < lit ? juce::Colour (0xffffa62b) : juce::Colour (0xff3a2a14));
                g.fillRect (r.getX(), y, r.getWidth(), r.getHeight() / (float) count - 1.0f);
            }
        }
    private:
        int lit = 0;
    };
    GrColumn grColumn;
    // The strip's face as a picture, and what it showed when made
    juce::Image face;
    juce::String lastFaceKey;
    juce::String faceKey (float scale) const;
    void paintFace (juce::Graphics& g);
    void showInputMenu();
    void showOutputMenu();
    juce::TextButton consoleTag { "EQ" };   // compact mode: lit when the strip's EQ/Dynamics (or plugin) is working; opens it
    void updateConsoleTag();
    juce::Label dynLabel;

    juce::OwnedArray<juce::TextButton> slots;
    int dropSlot = -1;   // where a dragged insert would land (drawn while dragging)
    int slotAt (juce::Point<int> position) const;
    juce::OwnedArray<juce::TextButton> sendButtons;
    juce::OwnedArray<strip::PointerKnob> sendLevels;
    std::map<juce::String, double> sendStart;   // the other selected tracks' send levels (dB) when a drag began
    std::map<juce::String, double> panStart;
    std::map<juce::String, double> consoleStart;   // the other selected tracks' console knob (or strip plugin) values when a drag began
    double consoleDragStart = 0.0;
public:
    /** The strip plugin's parameter for a control (console mode), for the other selected strips. */
    juce::AudioProcessorParameter* stripParameterFor (console::StripControl c) const { return stripParameter (c); }
private:
    double panDragStart = 0.0, sendDragStart = 0.0;
    int insertRows() const;
    int sendRows() const;

    /** Logic's channel format button: one circle for mono, two for stereo. */
    struct FormatButton final : juce::Button
    {
        FormatButton() : juce::Button ("Format") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool stereo = true;
    };
    FormatButton formatButton;
    bool isStereo() const;
    void toggleFormat();

    /** The selected tracks an edit here goes to (this one alone if it isn't selected). */
    juce::Array<juce::ValueTree> acting() const;
    void showZoneMenu();
    void startRename();
    std::unique_ptr<juce::TextEditor> nameEditor;
    void showControlMenu (juce::Slider& control, const juce::String& param, const juce::String& name,
                          std::function<void()> reset, const juce::String& learn = {});
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

    /** The compressor: built-in or a plugin (as the channel strips choose). */
    std::function<void (juce::Component& target)> onChoose;
    std::function<void()> onOpenPlugin;

private:
    juce::TextButton chooseButton, openButton { "Open" };
    void setup (juce::Slider& knob, const juce::Identifier& property, look::Knob cap, double min, double max, double step,
                std::function<juce::String (double)> text, const juce::String& name);

    juce::UndoManager& undoManager;
    AudioEngine& engine;
    juce::ValueTree project;

    GainReductionMeter vu;
    juce::Slider threshold, makeup, attack, release, ratio, mix, grit;
    juce::TextButton inButton { "IN" };
    juce::TextButton colorButton { "COLOR" };   // the console's colour and grit: its VCA and transformers driven

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

    /** Option-click on a plugin (as in Pro Tools and Cubase): a copy, with its settings, in the same slot of every selected channel. */
    std::function<void (juce::ValueTree insert)> onCopyInsertToSelected;

    /** An insert dragged to a slot (of this channel or another): moved, or copied with Option. */
    std::function<void (juce::ValueTree insert, const juce::String& toOwner, int slot, bool copy)> onMoveInsert;

    /** The header's Automix key: a balanced mix for a genre. */
    std::function<void()> onAutomix;

    /** The master strip's MM key: the MultiMeter. */
    std::function<void()> onOpenMultiMeter;

    std::function<void()> onClose;

    /** Open a track's channel strip in full, in its own window (the built-in strip; a plugin opens its own). */
    std::function<void (juce::ValueTree track)> onOpenStripWindow;

    /** A strip was clicked: select its track everywhere. */
    std::function<void (const juce::String& trackId, juce::ModifierKeys mods)> onSelectTrack;

    /** Strips shown elsewhere (the Inspector's): they get meter levels and plugin knob updates too. */
    void addExtraStrip (ChannelStrip* s)    { extraStrips.addIfNotAlreadyThere (s); }

    /** A track's strip (nullptr if it has none here). */
    ChannelStrip* findStrip (const juce::String& trackId) const
    {
        for (auto* s : strips)
            if (s->getOwnerId() == trackId)
                return s;
        return nullptr;
    }

    /** Captures the plugins' settings into the project (before a strip plugin is set aside, say). */
    std::function<void()> onStorePluginStates;
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

    /** How many insert slots and sends every strip shows: the defaults, or one more than the fullest strip uses. */
    int insertRows() const noexcept { return rows.first; }
    int sendRows() const noexcept { return rows.second; }
    static std::pair<int, int> rowsFor (const juce::ValueTree& project);

    /** The strips' height (unscaled) with the rows they show now. */
    int stripMinimumHeight() const { return ChannelStrip::minimumHeightFor (showsConsole(), rows.first, rows.second); }

    /** Highlights the selected tracks' strips and scrolls the main one into view. */
    void setSelectedTracks (const juce::StringArray& trackIds, const juce::String& mainTrackId);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** The interface's preamps, when its control service is there: the strips show them under their Input slots. */
    void setInterfaceControl (InterfaceControl* control);
    InterfaceControl* getInterfaceControl() const noexcept { return interfaceControl; }

    /** Console mode's bus compressor face (shown instead of the built-in one when a UA plugin is available). */
    BusCompConsolePanel& getConsolePanel() noexcept { return consolePanel; }
    MasterBusCompressorPanel& getBusCompPanel() noexcept { return busComp; }

    /** Console mode for the strips: the channel strip plugins found on this computer, best first. */
    void setStripCandidates (const juce::Array<juce::PluginDescription>& found);
    const juce::Array<juce::PluginDescription>& getStripCandidates() const noexcept { return stripCandidates; }

    /** The mixer's console was chosen in the header: a plugin, or none for the built-in strip. */
    std::function<void (const juce::PluginDescription* plugin)> onChooseConsole;
    std::function<juce::Array<juce::PluginDescription>()> allPluginTypes;   // every effect scanned (to use any of them as the strip)

    /** A plugin finished loading: strips switch to it. */
    void pluginsChanged() { triggerAsyncUpdate(); }

    EffectRack& getEffects() noexcept { return effects; }
    ProjectPlayer& getPlayer() noexcept { return player; }
    juce::ValueTree getProject() const { return project; }

    static constexpr int headerHeight = 28;

    /** Whether the strips show their console section, and the bus compressor its panel (both off by default). */
    bool showsConsole() const { return (bool) project.getProperty ("mixerConsole", false); }
    bool showsBusComp() const { return (bool) project.getProperty ("mixerBusComp", false); }

    /** Mixer zones (as Cubase's): "left" or "right" - pinned there, outside the scrolling strips - or empty (with the tracks).
        A channel's own choice (its mixerZone) first; buses and summing stacks follow the mixer's mixerBusZone. */
    static juce::String zoneOf (const juce::ValueTree& project, const juce::ValueTree& track);
    /** Pins tracks to a zone ("left", "right") or back with the tracks ("none"), undoably. */
    void setZone (const juce::Array<juce::ValueTree>& tracks, const juce::String& zone);

private:
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& p) override { touched (tree, p, false); }
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override        { changed (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { changed (parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override         { changed (parent); }
    void changed (const juce::ValueTree& parent);

    /** Only the strips a change is about are refreshed (moving one fader with 64 tracks
        doesn't redo all 64); edits to regions and notes don't touch the mixer at all. */
    void touched (const juce::ValueTree& tree, const juce::Identifier& property, bool structural);
    std::set<juce::String> dirtyOwners;   // track ids (or "master") whose strips need refreshing
    bool refreshAll = true, rowsMayChange = true;

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
    juce::TextButton busCompButton { "Bus Comp" };
    juce::TextButton automixButton { "Automix" }; // show the Master Bus Compressor beside the master
    juce::TextButton commentsButton { "Comments" };   // the notes boxes under the strips, on or off
    juce::TextButton zonesButton { "Zones" };   // where the buses (and any channel you pin) sit: left, right or with the tracks
    void showZonesMenu();
    float stripScale() const;
    juce::Array<juce::PluginDescription> stripCandidates;
    int syncCounter = 0;
    void refreshConsoleBox();
    juce::TextButton closeButton { "Close" };
    juce::Viewport outer;          // scrolls everything up and down when the pane is short
    juce::Component body;
    juce::Viewport viewport;       // scrolls the strips sideways
    juce::Component stripHolder;

public:
    /** View > Show Comments in Mixer: a notes box under each channel (as in Pro Tools). */
    void setShowComments (bool show) { showComments = show; commentsButton.setToggleState (show, juce::dontSendNotification); resized(); }
    std::function<void (bool shown)> onCommentsToggled;   // (the main window keeps the choice)
    bool getShowComments() const noexcept { return showComments; }
private:
    bool showComments = false;
    static constexpr int commentsHeight = 46;
    static constexpr int zoneGap = 6;
    std::pair<int, int> zoneEdges {};
    bool zonesChanged = false;
    InterfaceControl* interfaceControl = nullptr;   // where the left zone ends and the right one begins (0: none)
    juce::OwnedArray<juce::TextEditor> commentBoxes;
    void layOutComments (int stripW, int top);
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    MasterBusCompressorPanel busComp;
    BusCompConsolePanel consolePanel;
    bool needsRebuild = false;
    std::pair<int, int> rows { ChannelStrip::defaultInserts, ChannelStrip::defaultSends };
    bool updateRows();
    juce::String selectedTrackId;
    juce::StringArray selectedTrackIds;
    juce::Array<ChannelStrip*> extraStrips;
};

} // namespace riff
