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

    /**
        A control you drag the way Logic's faders and knobs work: grab it anywhere
        (it doesn't jump to the mouse), the mouse pointer disappears while you
        drag and comes back on the control when you let go. Shift drags finely;
        Option-click (or double-click) puts it back to its default.
    */
    class DragSlider : public juce::Slider
    {
    public:
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    protected:
        /** How many pixels of mouse travel take it from one end to the other. */
        virtual float dragTravel() const;
        /** Where the pointer comes back when you let go (this component's coordinates). */
        virtual juce::Point<float> handlePosition() const;
        bool dragsVertically() const;

    private:
        bool dragging = false;
        float lastDelta = 0.0f;
        double proportion = 0.0;
    };

    /** An encoder with a neon LED ring in the given colour (EQ gains light from the centre). */
    class PointerKnob final : public DragSlider
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
    class DisplaySlider final : public DragSlider
    {
    public:
        DisplaySlider();
        void paint (juce::Graphics&) override;

        /** Drawn as a small knob (the compact strip's pan) instead of a value. */
        bool asKnob = false;
    };

    /** A long-throw fader: a dark slot and a dark cap with a white line. */
    class Fader final : public DragSlider
    {
    public:
        Fader();
        void paint (juce::Graphics&) override;

    protected:
        float dragTravel() const override { return (float) getHeight() - capHeight(); }
        juce::Point<float> handlePosition() const override { return { (float) getWidth() * 0.5f, const_cast<Fader*> (this)->yForValue (getValue()) }; }

    public:

        /** Where the cap's centre sits for a value, in this component. */
        float yForValue (double value);
        float capHeight() const;
    };
}
