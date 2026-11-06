#pragma once

#include <JuceHeader.h>
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

    // The analyzer behind the EQ curve, and its settings (as in Logic's MultiMeter); the side-chain key
    SpectrumAnalyzer analyzer;
    juce::ComboBox modeBox, fftBox, rangeBox, releaseBox, holdBox;
    juce::TextButton keyButton;
    void applyAnalyzerOptions();
    juce::Rectangle<int> curveArea() const;
    juce::Rectangle<int> optionsArea() const;

    Levels levels;
    float inDb = -100.0f, outDb = -100.0f, grDb = 0.0f, deEssDb = 0.0f, gateDb = 0.0f;
};

} // namespace riff
