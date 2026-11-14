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
/** A knob of the Analog Console: the uploaded red-capped knob, painted in its section's colour. */
class AnalogKnob final : public juce::Slider
{
public:
    AnalogKnob (const juce::String& colour, const juce::String& caption);
    void paint (juce::Graphics&) override;
    juce::String caption;
private:
    juce::Image strip;
};

/**
    The Analog Console: the whole mixer as a large-format desk - every channel's input, filters, dynamics, four-band
    EQ, inserts, sends, pan and keys as knobs (the built-in channel strip's), and its fader in the track's colour;
    the master's VU, bus compressor and monitor section.
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

    static constexpr int columnWidth = 108, masterWidth = 300;

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
    juce::OwnedArray<juce::TextButton> filterKeys;
    int filter = 0;   // all, audio, buses

    /** A control bound to a property of the track (or the project): it follows it and writes it, undoably. */
    struct Binding { juce::Component* control; juce::ValueTree tree; juce::Identifier property; double fallback; bool toggle; };
    struct Strip final : juce::Component
    {
        juce::ValueTree track;
        juce::OwnedArray<juce::Component> parts;
        std::vector<Binding> bindings;
        strip::Fader fader;
        LevelMeter meter { "" };
        juce::Label level;
        juce::Rectangle<int> faderArea;
        juce::String number, name;
        juce::Colour colour;
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> sectionTitles;
        float gr = 0.0f;
        juce::Rectangle<int> grArea;
        bool isMaster = false;
        const float* vuLevel = nullptr;   // (the master's VU)
        void paint (juce::Graphics&) override;
    };
    juce::OwnedArray<Strip> strips;
    std::unique_ptr<Strip> master;
    void buildStrip (Strip& s);
    void buildMaster (Strip& s);
    void refreshBindings (Strip& s);
    juce::Viewport view;
    juce::Component holder;
    float vuValue = 0.0f;   // the master VU's needle (amplitude, with a VU's swing)
};

} // namespace riff
