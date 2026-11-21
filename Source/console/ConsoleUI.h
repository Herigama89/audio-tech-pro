#pragma once

#include <JuceHeader.h>
#include "ConsoleEngine.h"
#include <map>

namespace atpconsole::ui
{

namespace colours
{
    const juce::Colour background { 0xff1e2529 }, panel { 0xff2c363c }, sidebar { 0xff263035 }, strip { 0xff323d43 },
                       busStrip { 0xff2a3338 }, edge { 0xff141a1d }, well { 0xff1a2024 }, control { 0xff3a474e },
                       controlEdge { 0xff4f5e66 }, text { 0xffdce3e6 }, dim { 0xffa9b6bc }, faint { 0xff7f8d94 },
                       orange { 0xfff5a742 }, green { 0xff9bd34a }, yellow { 0xfff2d14b }, red { 0xffe8553d };
}

juce::Font font (float height, bool bold = false);

/** The console's look: flat dark panels, arc knobs and analog faders (black ridged caps on a printed dB scale). */
class ConsoleLook final : public juce::LookAndFeel_V4
{
public:
    ConsoleLook();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override       { return font (11.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.0f, true); }
    juce::Font getPopupMenuFont() override                      { return font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override             { return font (12.0f, true); }

private:
    void drawFader (juce::Graphics&, juce::Rectangle<float> area, float thumbY, juce::Slider&);
};

/** A plugin slot: the PRE (input stage) slot or an insert. Click it, or drop a plugin from the browser on it. */
class SlotButton final : public juce::Component,
                         public juce::DragAndDropTarget,
                         public juce::SettableTooltipClient
{
public:
    juce::String tag;                    // shown above the name (the PRE slot); empty for inserts
    std::function<void (const juce::MouseEvent&)> onClick;
    std::function<void (int knownPluginIndex)> onDrop;

    void set (const juce::String& pluginName, bool loaded, bool bypassed);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override  { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override   { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    juce::String name;
    bool loaded = false, bypassed = false, dragOver = false;
};

/** A stereo peak meter with a clip light (click the light to clear it). */
class MeterView final : public juce::Component
{
public:
    void update (Meter&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float levelL = 0.0f, levelR = 0.0f;   // dB
    float holdL = -100.0f, holdR = -100.0f;
    int holdTicksL = 0, holdTicksR = 0;
    bool clip = false;
    Meter* source = nullptr;
};

/** A fader or knob that drags like Logic's: the pointer hides while you drag (so you can keep going
    past the screen's edge), and comes back on the control when you let go. Shift: fine. Option-click: reset. */
class ConsoleSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool dragging = false;
    double proportion = 0.0;
    float lastX = 0.0f, lastY = 0.0f;
};

class MainView;

/** One channel strip (an input) or bus (FX, a cue, Main). */
class StripView final : public juce::Component
{
public:
    StripView (MainView&, Channel&, int stripIndex, int cueIndex);

    void refresh();          // everything from the model (names, plugins, outputs, hardware preamp)
    void updateMeters();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    Channel& channel;
    const int stripIndex;    // input strips: the engine's strip index; buses: -1
    const int cueIndex;      // cue buses: 0 or 1; otherwise -1

private:
    juce::String numberText() const;
    juce::String sourceText() const;
    void showStripMenu();
    void setupKnob (juce::Slider&, juce::Colour, double min, double max, double def, bool bipolar);

    MainView& owner;
    juce::Label name;
    juce::Slider hwGain { juce::Slider::LinearBar, juce::Slider::NoTextBox };
    juce::TextButton phantom { "48V" };
    std::array<SlotButton, numSlots> slots;
    std::array<ConsoleSlider, numSends> sends;
    ConsoleSlider fxReturn, trim, pan;
    ConsoleSlider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    MeterView meter;
    juce::TextButton mute { "M" }, solo { "S" }, polarity { juce::CharPointer_UTF8 ("\xc3\x98") };
    juce::ComboBox output;
    bool hasHardware = false;
};

/** The plugin list on the left: drag a plugin onto a slot. */
class PluginBrowser final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::ChangeListener
{
public:
    explicit PluginBrowser (Engine&);
    ~PluginBrowser() override;

    std::function<void()> onScan;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void rebuild();

    Engine& engine;
    juce::TextEditor search;
    juce::ListBox list { "Plugins", this };
    juce::TextButton scan { "Scan for plugins..." };
    juce::Array<int> rows;   // indexes into knownPlugins.getTypes()
    juce::Array<juce::PluginDescription> types;
};

/** A plugin's own window. */
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor&, std::function<void()> onClose);
    ~PluginWindow() override;
    void closeButtonPressed() override;

private:
    std::function<void()> closed;
};

/** The whole console: top bar, plugin browser, input strips, then FX, Cue A, Cue B and Main. */
class MainView final : public juce::Component,
                       public juce::DragAndDropContainer,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    MainView (Engine&, juce::ApplicationProperties&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // From the strips
    void slotClicked (Channel&, int slot, SlotButton&, const juce::MouseEvent&);
    void dropPlugin (Channel&, int slot, int knownIndex);
    void stripMenuFor (StripView&);
    Engine& engine;

    // Sessions
    void newSession();
    void openSession();
    void saveSession (bool saveAs);
    juce::File autosaveFile() const;
    void saveAutosave();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildStrips();
    void refreshAll();
    void layoutStrips();
    void loadPlugin (Channel&, int slot, const juce::PluginDescription&);
    void openEditor (juce::AudioPluginInstance*);
    juce::PopupMenu pluginMenu (std::function<void (const juce::PluginDescription&)> chosen);
    void showAudioSetup();
    void showPluginScanner();
    void showSessionMenu();
    void loadSessionFile (const juce::File&);
    void warn (const juce::String& title, const juce::String& message);

    juce::ApplicationProperties& properties;
    ConsoleLook look;
    PluginBrowser browser;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<StripView> inputViews, busViews;
    juce::TextButton audioButton { "Audio Setup" }, pluginsButton { "Plugins" }, sessionButton { "Session" },
                     talkback { "Talkback" };
    std::map<juce::AudioProcessor*, std::unique_ptr<PluginWindow>> windows;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File sessionFile;

    juce::String deviceText, statusText;
    double rate = 0.0, roundTrip = 0.0, cpu = 0.0;
    int block = 0, slowTicks = 0;
};

} // namespace atpconsole::ui
