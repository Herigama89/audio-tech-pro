#pragma once

#include <JuceHeader.h>
#include "Theme.h"

/**
    The moving parts of a mixer strip that sit on the strip artwork (see
    Tools/make_strip_art.py). The artwork shows the knob bodies, keys and
    displays; these draw only what changes: pointers, lamps, lettering,
    values, and the fader cap.
*/
namespace riff::strip
{
    /** The artwork, decoded once. */
    const juce::Image& topArt();
    const juce::Image& plateArt();
    const juce::Image& faderCapArt();

    /** StripTop.png is this many pixels wide and tall. */
    constexpr float artWidth = 195.0f, artHeight = 887.0f;

    /** A knob whose body is in the artwork: draws its pointer only. */
    class PointerKnob final : public juce::Slider
    {
    public:
        explicit PointerKnob (juce::Colour pointerColour = juce::Colour (0xfff4f2ea));
        void paint (juce::Graphics&) override;

    private:
        juce::Colour pointer;
    };

    /** A small square key with a lamp (the red keys on the artwork). Toggle state = lit. */
    class LampKey final : public juce::Button
    {
    public:
        LampKey (const juce::String& name, juce::Colour lampColour);
        void paintButton (juce::Graphics&, bool over, bool down) override;

    private:
        juce::Colour lamp;
    };

    /** A cream push key with a letter on it (mute, solo). Lit in its colour when on. */
    class CreamKey final : public juce::Button
    {
    public:
        CreamKey (const juce::String& letter, juce::Colour litColour);
        void paintButton (juce::Graphics&, bool over, bool down) override;

    private:
        juce::Colour lit;
    };

    /** A value shown in one of the dark display boxes; drag sideways to change it (pan). */
    class DisplaySlider final : public juce::Slider
    {
    public:
        DisplaySlider();
        void paint (juce::Graphics&) override;
    };

    /** A long-throw fader: a dark slot and the metal cap from the artwork. */
    class Fader final : public juce::Slider
    {
    public:
        Fader();
        void paint (juce::Graphics&) override;

        /** Where the cap's centre sits for a value, in this component. */
        float yForValue (double value);
        float capHeight() const;
    };
}
