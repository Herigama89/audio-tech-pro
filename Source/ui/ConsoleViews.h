#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "StripParts.h"

namespace riff
{
class AudioEngine;
class ProjectPlayer;
class MixerPanel;
class ChannelStrip;

/**
    What the console windows (Input FX, Console mode, the Analog Console) need from the app: the project's undo,
    the engine for levels and latency, and ways to open and choose plug-ins.
*/
struct ConsoleHost
{
    juce::UndoManager* undo = nullptr;
    AudioEngine* engine = nullptr;
    ProjectPlayer* player = nullptr;
    MixerPanel* mixer = nullptr;
    /** A built-in insert's live face (its EffectPanel, its meters moving); nullptr for a plug-in. */
    std::function<std::unique_ptr<juce::Component> (juce::ValueTree insert)> makeFace;
    std::function<void (juce::ValueTree insert)> openInsert;
    std::function<void (const juce::String& owner, int slot, juce::ValueTree existing, juce::Component& target)> chooseEffect;
    std::function<void()> openLatencyCalibration;
    /** Ultra Low: the interface's buffer down to the smallest it takes (true), or back to what it was. */
    std::function<void (bool ultra)> setUltraLowBuffer;
    /** The interface's own round trip (converters and buffers), ms. */
    std::function<double()> deviceRoundTripMs;
    /** An insert's latency, samples. */
    std::function<int (const juce::String& insertId)> latencyOf;
    std::function<double()> sampleRate;
    /** The master's level (the Analog Console's master meter). */
    std::function<std::pair<float, float>()> masterLevels;
    /** Bounce (the master's Bnce key): the song, or the cycle, to a file. */
    std::function<void()> bounce;
};

/** A component shown at a fraction of its size, to fit a width (a plug-in face in a console column). */
class ScaledFace final : public juce::Component
{
public:
    explicit ScaledFace (std::unique_ptr<juce::Component> face);
    int heightForWidth (int width) const;
    void resized() override;
    juce::Component* getFace() const noexcept { return face.get(); }
private:
    std::unique_ptr<juce::Component> face;
};

/** A plug-in that draws its own window (a VST3 or AU): its name and maker on a card that opens it. */
class PluginCard final : public juce::Component
{
public:
    PluginCard (const juce::String& name, const juce::String& maker, std::function<void()> open);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::String name, maker;
    std::function<void()> open;
};

//==============================================================================
/**
    Input FX: a track's input chain (as Unison's and LUNA's preamp slot) - plug-ins on the input, recorded through
    (Wet), or not (Dry), or both; Ultra Low leaves the slow ones out while tracking; one knob can turn the
    interface's own preamp. Shows the measured round trip.
*/
class InputFxPanel final : public juce::Component,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    InputFxPanel (ConsoleHost& host, juce::ValueTree project, juce::ValueTree track);
    ~InputFxPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Round trip through the chain as it is now, ms (the interface and the plug-ins not left out). */
    static double roundTripMs (ConsoleHost& host, const juce::ValueTree& project, const juce::ValueTree& track);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void timerCallback() override { repaint (topArea); }
    void rebuild();
    void setMode (int mode);
    void setRecord (const juce::String& what);
    juce::String chainOwner() const;

    ConsoleHost& host;
    juce::ValueTree project, track;
    juce::Rectangle<int> topArea, cards[3];
    juce::OwnedArray<juce::TextButton> modeKeys, recordKeys;
    juce::TextButton measureButton { "Measure..." }, addButton { "+ Add a plug-in to the input (built-in, VST3 or AU)" };
    juce::TextButton onButton { "Input FX On" };
    juce::ComboBox linkBox;
    juce::Label linkCaption;

    struct Row final : juce::Component
    {
        juce::ValueTree insert;
        std::unique_ptr<juce::Component> picture;
        juce::Label name, detail, latency;
        juce::TextButton up { "Up" }, down { "Down" }, on { "ON" }, edit { "Edit" }, remove { "Remove" };
        void resized() override;
        void paint (juce::Graphics&) override;
    };
    juce::OwnedArray<Row> rows;
    juce::Viewport rowsView;
    juce::Component rowsHolder;
    juce::Array<std::pair<juce::String, juce::String>> linkChoices;   // insertId, paramId
};

//==============================================================================
/** Console mode (as LUNA's mixer): every channel with its full strip - the live faces of its Input FX, its channel
    strip plug-in and its inserts - above its fader, meters and keys (the mixer's own strip). */
class ConsoleView final : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::AsyncUpdater
{
public:
    ConsoleView (ConsoleHost& host, juce::ValueTree project);
    ~ConsoleView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void handleAsyncUpdate() override { rebuild(); }
    void rebuild();
    void layout();

    ConsoleHost& host;
    juce::ValueTree project;
    juce::OwnedArray<juce::TextButton> fitKeys, showKeys;
    int fit = 5;
    bool showInput = true, showStrip = true, showInserts = true;

    struct Column final : juce::Component
    {
        juce::ValueTree track;
        struct Section { juce::String title, owner; juce::OwnedArray<juce::Component> faces; juce::OwnedArray<juce::TextButton> titles; juce::Rectangle<int> titleArea;
                         std::unique_ptr<juce::TextButton> add; };
        juce::OwnedArray<Section> sections;
        std::unique_ptr<ChannelStrip> strip;
        /** The faces, scrolling on their own above the strip (its fader always in sight). */
        struct Faces final : juce::Component { Column* column = nullptr; void paint (juce::Graphics&) override; };
        Faces faces;
        juce::Viewport facesView;
        Column() { faces.column = this; facesView.setViewedComponent (&faces, false); facesView.setScrollBarsShown (true, false); addAndMakeVisible (facesView); }
        int facesHeight (int width) const;
        void paint (juce::Graphics&) override;
        juce::Colour colour;
        juce::String number, name;
    };
    juce::OwnedArray<Column> columns;
    juce::Viewport view;
    juce::Component holder;
};

//==============================================================================
/** A knob of the Analog Console: a Console caps knob (its section's colour, or the pan knob) with its scale
    printed round it, numbers at the values they mark; while it's turned, its caption shows the value. */
class AnalogKnob final : public juce::Slider
{
public:
    AnalogKnob (juce::Colour cap, const juce::String& caption, bool pan = false);
    void paint (juce::Graphics&) override;
    void valueChanged() override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Rectangle<float> knobArea() const;
    /** The caption under it: its name, or (while turned) its value. */
    juce::String captionNow() const;

    juce::String caption;
    std::vector<std::pair<double, juce::String>> scale;   // value, its number (blank: a tick only)
    int knobSize = 24;
private:
    juce::Colour cap;
    bool pan = false;
};

/** A key of the Analog Console: square-cornered, lit in its colour (as the console it's after). */
class ConsoleKey final : public juce::TextButton
{
public:
    ConsoleKey (const juce::String& text, juce::Colour lit) : juce::TextButton (text), lit (lit) { setWantsKeyboardFocus (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour lit;
};

/** An insert slot (Input FX, Inserts): its plug-in's name and lamp, or "+" to add one. */
class InsertSlot final : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertSlot (const juce::String& name, int kind, bool on, std::function<void()> click)
        : name (name), kind (kind), on (on), click (std::move (click)) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (click && getLocalBounds().contains (e.getPosition())) click(); }
    juce::String name;
    int kind = 0;   // 0 empty, 1 built-in, 2 plug-in, 3 input FX built-in
    bool on = true;
    std::function<void()> click;
};

/** The console's MultiMeter: the master's spectrum (a curve or bars), its stereo image and loudness. */
class ConsoleMeter final : public juce::Component, private juce::Timer
{
public:
    ConsoleMeter (AudioEngine& engine, juce::ValueTree project);
    ~ConsoleMeter() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int bandCount = 31;
private:
    void timerCallback() override;
    AudioEngine& engine;
    juce::ValueTree project;
    ConsoleKey spectrumKey { "Spectrum", juce::Colours::white }, stereoKey { "Stereo", juce::Colours::white }, loudnessKey { "Loudness", juce::Colours::white };
    ConsoleKey curveKey { "Curve", juce::Colours::white }, barsKey { "Bars", juce::Colours::white };
    juce::Rectangle<int> screen, readouts;
    std::vector<float> readL, readR;
    std::unique_ptr<struct ConsoleMeterState> state;
    int tab = 0;
    bool bars = false;
    void setTab (int t);
};

/**
    The Analog Console: the whole mixer as a large-format desk - every channel's input, Input FX, dynamics, filters,
    four-band EQ, inserts, sends, pan and keys, its fader in the track's colour with a printed scale and an LED meter;
    the master's compression meter, bus compressor, MultiMeter, inserts and Bounce.
*/
class AnalogConsole final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater,
                            private juce::Timer
{
public:
    AnalogConsole (ConsoleHost& host, juce::ValueTree project);
    ~AnalogConsole() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int columnWidth = 116, masterWidth = 212;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { rebuildWanted = true; triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void rebuild();

    ConsoleHost& host;
    juce::ValueTree project;
    bool rebuildWanted = true;
    juce::OwnedArray<ConsoleKey> filterKeys;
    int filter = 0;   // all, audio, buses

    /** A control bound to a property of the track (or the project): it follows it and writes it, undoably. */
    struct Binding { juce::Component* control; juce::ValueTree tree; juce::Identifier property; double fallback; bool toggle; };
    struct Strip final : juce::Component
    {
        juce::ValueTree track;
        juce::OwnedArray<juce::Component> parts;
        std::vector<Binding> bindings;
        strip::Fader fader;
        juce::Label level;
        juce::Rectangle<int> faderArea, meterArea, compMeter;
        juce::String number, name;
        juce::Colour colour;
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionTitles;
        juce::String inputFxTime;   // (the Input FX title's round trip)
        float gr = 0.0f, peakL = 0.0f, peakR = 0.0f, holdL = 0.0f, holdR = 0.0f;
        int holdTicksL = 0, holdTicksR = 0;
        juce::Rectangle<int> grArea;
        bool isMaster = false;
        void paint (juce::Graphics&) override;
        void paintMeter (juce::Graphics&, juce::Rectangle<float> area, float peak, float hold) const;
    };
    juce::OwnedArray<Strip> strips;
    std::unique_ptr<Strip> master;
    std::unique_ptr<ConsoleMeter> meter;
    void buildStrip (Strip& s);
    void buildMaster (Strip& s);
    void refreshBindings (Strip& s);
    juce::Viewport view;
    juce::Component holder;
    float busGr = 0.0f;
};

} // namespace riff
