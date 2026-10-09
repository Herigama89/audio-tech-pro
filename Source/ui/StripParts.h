#pragma once

#include <JuceHeader.h>
#include "Theme.h"

/**
    The controls on a mixer strip, all drawn in code: neon-ring encoders,
    backlit keys, the pan display and the fader with its dark cap.
*/
namespace riff::strip
{
    /** The strip's top section is laid out in this space (then scaled to the strip's width). */
    constexpr float artWidth = 195.0f, artHeight = 887.0f;

    /** An encoder with a neon LED ring in the given colour (EQ gains light from the centre). */
    class PointerKnob final : public juce::Slider
    {
    public:
        explicit PointerKnob (juce::Colour pointerColour = juce::Colour (0xfff4f2ea));
        void paint (juce::Graphics&) override;

    private:
        juce::Colour pointer;
    };

    /** A small backlit key (EQ in, Dynamics in). Toggle state = lit. */
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

    /** A long-throw fader: a dark slot and a dark cap with a white line. */
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
