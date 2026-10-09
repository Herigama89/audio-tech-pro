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
    constexpr float artWidth = 196.0f, artHeight = 861.0f;   // the strip art cut from the picture (Skin.h)

    /** An encoder with a neon LED ring in the given colour (EQ gains light from the centre). */
    class PointerKnob final : public juce::Slider
    {
    public:
        explicit PointerKnob (juce::Colour pointerColour = juce::Colour (0xfff4f2ea));
        void paint (juce::Graphics&) override;

        /** Draw the picture's knob (its scale is printed on the strip art underneath). */
        void useSkin (juce::Image body, juce::Colour pointerLine) { skinBody = body; skinPointer = pointerLine; repaint(); }

    private:
        juce::Colour pointer;
        juce::Image skinBody;
        juce::Colour skinPointer;
    };

    /** A small backlit key (EQ in, Dynamics in). Toggle state = lit. */
    class LampKey final : public juce::Button
    {
    public:
        LampKey (const juce::String& name, juce::Colour lampColour);
        void paintButton (juce::Graphics&, bool over, bool down) override;

        /** The key is printed on the strip art: only light it up. */
        bool onArt = false;

    private:
        juce::Colour lamp;
    };

    /** A cream push key with a letter on it (mute, solo). Lit in its colour when on. */
    class CreamKey final : public juce::Button
    {
    public:
        CreamKey (const juce::String& letter, juce::Colour litColour);
        void paintButton (juce::Graphics&, bool over, bool down) override;

        /** The key is printed on the strip art: draw only its letter, and its light when on. */
        bool onArt = false;

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
