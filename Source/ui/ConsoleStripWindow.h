#pragma once

#include <JuceHeader.h>
#include <array>
#include <optional>
#include "../engine/ConsoleChannel.h"
#include "SpectrumAnalyzer.h"

namespace riff
{

/**
    The built-in channel strip in full, in its own window: the photographed front panel of a classic
    British console channel (Tools/effect_skins/strip.py cut it: its knobs' pointer lines turn, its lamps
    light), and under it the EQ curve over the track's spectrum, as on the vintage EQs.

    Input (48V, PAD, MIC gain, LINE, polarity, TRIM), de-esser, compressor (threshold, ratio, release, soft
    knee, fast attack, linear release), gate/expander, filters (into the side chain, at the input), the
    four-band EQ (black-knob curves, into the side chain), routing (dynamics and de-esser before or after the
    EQ, an external key from another track, stereo link), the inserts (in, after the strip), the meter (input
    or output), and the power. The knobs on the mixer strip are the same controls.
*/
class ConsoleStripWindow final : public juce::Component,
                                 private juce::ValueTree::Listener,
                                 private juce::Timer
{
public:
    /** The meters: level in and out (linear peaks), and the compressor's, de-esser's and gate's gain reduction (dB),
        read ~30 times a second. */
    using Levels = std::function<std::array<float, 5>()>;

    ConsoleStripWindow (juce::ValueTree track, juce::UndoManager& um,
                        std::function<int (float*, int)> analyzerSource = {}, std::function<double()> sampleRate = {},
                        Levels levels = {});
    ~ConsoleStripWindow() override;

    /** The interface's 48 V for this track's input: its state (nothing when no interface preamp is reachable)
        and how to switch it. With them the 48V key turns the interface's phantom power on and off. */
    std::function<std::optional<bool>()> interfacePhantom;
    std::function<void (bool)> setInterfacePhantom;

    /** The track's full MultiMeter (analyzer, goniometer, loudness, levels, correlation) under the EQ: shown with the
        METER key, as tall as the window is made. The window owns it. */
    void setMeter (std::unique_ptr<juce::Component> multiMeter);
    /** How tall the window wants to be (the meter shown or not). */
    int preferredHeight() const;
    std::function<void()> onPreferredHeightChanged;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    /** One of the panel's knobs: where its value is printed round it (angle, value pairs). */
    struct Scale
    {
        std::vector<std::pair<float, float>> points;   // degrees clockwise from 12 o'clock -> value
        bool logarithmic = false;
        float offBelow = -1000.0f;                      // turned further left than this: off (offValue)
        float offValue = 0.0f;

        float valueAt (float angle) const noexcept;
        float angleOf (float value) const noexcept;
    };

private:
    class Knob;
    class Key;

    void addKnob (const juce::Identifier& property, const juce::String& name, const char* sprite, int cx, int cy, int r,
                  Scale scale, float fallback, std::function<juce::String (float)> text);
    void addKey (const juce::Identifier& property, const juce::String& name, const char* led, bool defaultOn);
    void refresh();
    void showKeyMenu();
    bool on (const juce::Identifier& property, bool fallback) const { return track.getProperty (property, fallback); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); repaint(); }
    void timerCallback() override;

    juce::ValueTree track;
    juce::UndoManager& undoManager;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Key>> keys;
    std::unique_ptr<Key> power;

    // The panel (its photo at twice the size it's shown) and where each lamp is on it
    static constexpr float photoScale = 0.5f;
    static constexpr int faceWidth = 1495, faceHeight = 156;
    std::map<juce::String, juce::Point<int>> leds;
    void lamp (juce::Graphics& g, const juce::String& name) const;
    void paintGlass (juce::Graphics& g);          // (the Liquid Glass panel)
    void paintCurveGround (juce::Graphics& g);

    // The analyzer behind the EQ curve, and its settings (as in Logic's MultiMeter); the side-chain key
    SpectrumAnalyzer analyzer;
    juce::ComboBox modeBox, fftBox, rangeBox, releaseBox, holdBox;
    juce::TextButton keyButton;
    juce::TextButton meterButton { "MultiMeter" };
    std::unique_ptr<juce::Component> meter;
    juce::Rectangle<int> meterArea() const;
    void applyAnalyzerOptions();
    juce::Rectangle<int> curveArea() const;
    juce::Rectangle<int> optionsArea() const;

    Levels levels;
    float inDb = -100.0f, outDb = -100.0f, grDb = 0.0f, deEssDb = 0.0f, gateDb = 0.0f;
    std::array<float, 5> lastShown {};
    int ecoFrame = 0;
};

} // namespace riff
