#pragma once
#include "EffectFace.h"
#include "EffectDesigns.h"

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/** A window's content under a slim bar with a Pin key (pinned: the window stays in front of the main window),
    for windows without a plugin header of their own (the MultiMeter, the channel strip). */
class PinFrame final : public juce::Component
{
public:
    static constexpr int barHeight = 24;

    PinFrame (juce::Component* contentToOwn, bool pinnedAtStart) : content (contentToOwn)
    {
        addAndMakeVisible (*content);
        pin.setClickingTogglesState (true);
        pin.setToggleState (pinnedAtStart, juce::dontSendNotification);
        pin.setTooltip ("Pin: keep this window in front of the main window (while the app is in front)");
        pin.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        pin.setWantsKeyboardFocus (false);
        pin.onClick = [this] { if (onPinChanged) onPinChanged (pin.getToggleState()); };
        addAndMakeVisible (pin);
        setSize (content->getWidth(), content->getHeight() + barHeight);
    }

    std::function<void (bool pinned)> onPinChanged;
    juce::Component& getContent() noexcept { return *content; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::toolbarDark);
        g.fillRect (getLocalBounds().removeFromTop (barHeight));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, barHeight - 1, getWidth(), 1);
    }

    void resized() override
    {
        const juce::ScopedValueSetter<bool> busy (laying, true);
        pin.setBounds (getWidth() - 58, 3, 52, barHeight - 6);
        content->setBounds (getLocalBounds().withTrimmedTop (barHeight));
    }

    void childBoundsChanged (juce::Component* child) override
    {
        // The content sizing itself (a window that grows): the frame and its window follow
        if (child == content.get() && ! laying)
            setSize (content->getWidth(), content->getHeight() + barHeight);
    }

private:
    std::unique_ptr<juce::Component> content;
    juce::TextButton pin { "Pin" };
    bool laying = false;
};

/** A slider with a right-click menu (Logic's: automation, MIDI Learn, default) instead of a drag. */
class MenuSlider : public juce::Slider
{
public:
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        menuClick = e.mods.isPopupMenu() && onMenu != nullptr;
        if (menuClick) onMenu(); else juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! menuClick) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! std::exchange (menuClick, false)) juce::Slider::mouseUp (e); }
private:
    bool menuClick = false;
};

/**
    A floating window for a plugin's editor or the synth controls.
    Pinned (plugin windows, as in Logic): it floats above the app's main window, so clicking
    the tracks or the mixer doesn't send it behind; it stops floating while you're in another
    app. Several stay open at once, each until you close it.
*/
class EditorWindow final : public juce::DocumentWindow,
                           private juce::Timer
{
public:
    EditorWindow (const juce::String& title, juce::Component* contentToOwn, bool canResize,
                  std::function<void()> onCloseRequested)
        : DocumentWindow (title, juce::Colour (0xff1c1d21), DocumentWindow::closeButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        if (appKeys != nullptr)
            addKeyListener (appKeys);   // the app's keys (space: play) work here too
        setContentOwned (contentToOwn, true);
        setResizable (canResize, false);
        centreAroundComponent (nullptr, getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose(); // the owner deletes this window
    }

    /** The app's key commands, passed on by every window (set by the main window). */
    inline static juce::KeyListener* appKeys = nullptr;

    /** Plugin windows open pinned or not as the last one was left (the pin key in their header). */
    inline static bool pinByDefault = true;

    void setPinned (bool shouldPin)
    {
        pinned = shouldPin;
        if (pinned)
            startTimerHz (4);
        else
            stopTimer();
        timerCallback();
    }
    bool isPinned() const noexcept { return pinned; }

    /** Stacks a new window a little below and right of the last one, so they don't open on top of each other. */
    void cascadeFrom (const juce::Component* previous)
    {
        if (previous != nullptr)
            setTopLeftPosition (previous->getX() + 28, previous->getY() + 28);
    }

private:
    void timerCallback() override
    {
        // Above the main window while the app is in front; an ordinary window when you switch away.
        const auto shouldFloat = pinned && juce::Process::isForegroundProcess();
        if (isAlwaysOnTop() != shouldFloat)
            setAlwaysOnTop (shouldFloat);
    }

    std::function<void()> onClose;
    bool pinned = false;
};

/** Controls for the built-in instruments (Tech Synth, Drum Kit, Sampler), bound (undoably) to the track's properties. */
class SynthPanel final : public juce::Component,
                         private juce::ValueTree::Listener
{
public:
    SynthPanel (juce::ValueTree track, juce::UndoManager& um, const juce::String& kind = "synth");
    ~SynthPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Control
    {
        juce::Identifier id;
        juce::Slider slider;
        juce::Label label;
    };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();
    void addControl (const juce::Identifier& id, const juce::String& name, double min, double max,
                     double mid, const juce::String& suffix);

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    juce::ComboBox waveBox;
    juce::Label waveLabel;
    juce::TextButton soundButton { "Choose Sound..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String kind;
    juce::OwnedArray<Control> controls;
};

/** Knobs for one of Riff's built-in effects, bound (undoably) to its INSERT node. */
class EffectPanel final : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::Timer
{
public:
    EffectPanel (juce::ValueTree insert, juce::UndoManager& um);
    ~EffectPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Where a design's meter reads from (gain reduction, pitch correction): the running effect. */
    void setLiveValue (std::function<float()> source);

    /** Where its meters read from (the running effect's readMeters). */
    void setMeterSource (std::function<int (float* dest, int capacity)> source);

    /** Where the analyzer reads the output from: fills the latest samples and gives the sample rate (0: none). */
    void setAnalyzerSource (std::function<double (float* dest, int count)> source);

private:
    struct Knob
    {
        juce::String id;
        MenuSlider slider;
        juce::Label label;
    };
    Knob* addKnob (const ParamInfo& p);
    void buildDesign (const EffectInfo& info);
    void timerCallback() override;

    // A hand-made face (Logic-style) for some effects
    struct Placed
    {
        effectdesign::Control control;
        juce::Component* component = nullptr;
        juce::Label* label = nullptr;
    };
    const effectdesign::Design* design = nullptr;
    std::unique_ptr<effectdesign::DesignLook> designLook;
    std::vector<Placed> placedControls;
    juce::OwnedArray<juce::Component> extras;
    juce::OwnedArray<juce::Label> extraLabels;
    std::vector<std::function<void()>> refreshers;
    std::function<float()> liveValue;
    float liveShown = 0.0f;
    std::function<double (float*, int)> analyzerSource;
    std::function<int (float*, int)> meterSource;
    std::vector<float> meterValues;
    int meterCount = 0;
    juce::OwnedArray<juce::Component> graphEditors;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<int> graphPages;
    effectdesign::Spectrum spectrum;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData, window;
    void updateSpectrum();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void refresh();

    juce::ValueTree insert;
    juce::UndoManager& undoManager;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::OwnedArray<Knob> knobs;
    /** Places the display, sections and knobs (or, not applying, works out the height they need). */
    int layoutFace (bool apply);

    struct SectionBox { juce::String title; juce::Rectangle<int> area; };
    std::vector<SectionBox> sectionBoxes;
    juce::Rectangle<int> displayArea;
    std::unique_ptr<effectface::FaceLook> look;
    int faceWidth = 760;
    static constexpr int headerHeight = 42, displayHeight = 170, cellWidth = 76, cellHeight = 96,
                         sectionTitleHeight = 20, sectionPad = 6, sectionGap = 8;
};

} // namespace riff
