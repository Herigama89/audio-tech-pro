#pragma once

#include <JuceHeader.h>
#include "Theme.h"

/**
    The controls on a mixer strip, all drawn in code: neon-ring encoders,
    backlit keys, the pan display and the fader with its dark cap.
*/
namespace riff::strip
{
    /** A channel's inserts bypass (as Cubase's): a small round power key, lit amber while all that channel's
        plug-ins are bypassed (they stay loaded). Its track's "consoleInsertIn" off = bypassed. */
    class InsertBypassKey final : public juce::Button, private juce::ValueTree::Listener
    {
    public:
        InsertBypassKey() : juce::Button ("Bypass inserts")
        {
            setTooltip ("Bypass: this channel's plug-ins all out of the signal (still loaded) - again to bring them back");
            setWantsKeyboardFocus (false);
        }
        ~InsertBypassKey() override { tree.removeListener (this); }
        void setTrack (juce::ValueTree t) { tree.removeListener (this); tree = t; tree.addListener (this); repaint(); }
        bool bypassed() const { return tree.isValid() && ! (bool) tree.getProperty ("consoleInsertIn", true); }
        std::function<void()> onToggle;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto d = juce::jmin (r.getWidth(), r.getHeight());
            const auto c = r.withSizeKeepingCentre (d, d);
            const bool on = bypassed();
            g.setColour (on ? juce::Colour (0xffe8a028) : juce::Colour (0xff2c2f35).brighter (over ? 0.25f : 0.0f).darker (down ? 0.3f : 0.0f));
            g.fillEllipse (c);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawEllipse (c, 0.8f);
            // the power symbol
            const auto ink = on ? juce::Colour (0xff1a1206) : juce::Colours::white.withAlpha (0.8f);
            const auto rr = d * 0.27f;
            juce::Path arc;
            arc.addCentredArc (c.getCentreX(), c.getCentreY(), rr, rr, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
            g.setColour (ink);
            g.strokePath (arc, juce::PathStrokeType (juce::jmax (1.0f, d * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (c.getCentreX(), c.getCentreY() - rr * 1.25f, c.getCentreX(), c.getCentreY() - rr * 0.1f, juce::jmax (1.0f, d * 0.09f));
        }
        void clicked() override { if (onToggle) onToggle(); }
    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override { if (p.toString() == "consoleInsertIn") repaint(); }
        juce::ValueTree tree;
    };

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

        /** Right-click (or Control-click): the control's menu (see ControlMenu.h), when set. */
        std::function<void (const juce::MouseEvent&)> onRightClick;

    protected:
        /** How many pixels of mouse travel take it from one end to the other. */
        virtual float dragTravel() const;
        /** Where the pointer comes back when you let go (this component's coordinates). */
        virtual juce::Point<float> handlePosition() const;
        bool dragsVertically() const;

    private:
        bool dragging = false, menuClick = false;
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
        void paintUncached (juce::Graphics&);

        /** A coloured cap on the knob (as on a classic console's EQ: red, green, blue, black). */
        void setCap (juce::Colour c) { cap = c; repaint(); }

    private:
        juce::Colour pointer;
        juce::Image skinBody;
        juce::Colour skinPointer, cap { juce::Colours::transparentBlack };
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
        void paintUncached (juce::Graphics&, bool over, bool down);

        /** The key is printed on the strip art: draw only its letter, and its light when on. */
        bool onArt = false;

    private:
        juce::Colour lit;
    };

    /** A small power key (the compact strip's channel strip IN): a power sign, lit green when on. */
    class PowerKey final : public juce::Button
    {
    public:
        PowerKey() : juce::Button ("IN") { setClickingTogglesState (false); setWantsKeyboardFocus (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    /** A stack's disclosure triangle (its channels shown, or folded away): pointing down when open. */
    class FoldKey final : public juce::Button
    {
    public:
        FoldKey() : juce::Button ("Fold") { setClickingTogglesState (false); setWantsKeyboardFocus (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool open = true;
    };

    /** A value shown in one of the dark display boxes; drag sideways to change it (pan). */
    class DisplaySlider final : public DragSlider
    {
    public:
        DisplaySlider();
        void paint (juce::Graphics&) override;
        void paintUncached (juce::Graphics&);

        /** Drawn as a small knob (the compact strip's pan) instead of a value. */
        bool asKnob = false;

        /** Drawn as a knob with its value beside it (the strip's PRE and TAPE). */
        bool knobAndValue = false;

        /** Its cap's colour with Console caps knobs (none: grey). */
        juce::Colour capColour { juce::Colours::transparentBlack };

        /** Shown faded (its section switched out). Drawn faded here rather than with setAlpha: a see-through
            component is drawn through an extra layer, and dozens of them made the mixer slow. */
        void setDimmed (float opacity) { if (! juce::approximatelyEqual (opacity, dimmed)) { dimmed = opacity; repaint(); } }
    private:
        float dimmed = 1.0f;
    public:
    };

    /** A long-throw fader: a dark slot and a dark cap with a white line. */
    class Fader final : public DragSlider
    {
    public:
        Fader();
        void paint (juce::Graphics&) override;
        /** The cap painted in a colour (the track's), its light and shade kept; transparent: as the theme draws it. */
        void setTint (juce::Colour c) { if (c != tint) { tint = c; repaint(); } }

    protected:
        float dragTravel() const override { return (float) getHeight() - capHeight(); }
        juce::Point<float> handlePosition() const override { return { (float) getWidth() * 0.5f, const_cast<Fader*> (this)->yForValue (getValue()) }; }

    public:

        /** Where the cap's centre sits for a value, in this component. */
        float yForValue (double value);
        float capHeight() const;

    private:
        void paintFader (juce::Graphics&);
        juce::Colour tint;
    };
}
