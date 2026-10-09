#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    Draws controls in the Audio Tech Pro style: knobs with coloured caps and
    tick marks, metal fader caps, raised keys. One instance is set as the
    default LookAndFeel for the whole app.

    Per-control appearance is chosen with the helpers below, which store a
    property on the component.
*/
class ConsoleLook final : public juce::LookAndFeel_V4
{
public:
    ConsoleLook();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    int getSliderThumbRadius (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

namespace look
{
    enum class Knob { black, cream, blue, red };

    /** Turns a slider into a console knob with the given cap. `ticks` draws the scale around it. */
    void knob (juce::Slider&, Knob cap, bool ticks = true);

    /** Ticks and labels drawn on a light (cream or grey) background rather than a dark one. */
    void onLight (juce::Component&, bool light = true);

    enum class Key
    {
        dark,     // raised key on the navy bar
        cream,    // cream key (transport, track headers)
        console,  // small grey key on a mixer strip
        slot      // dark recessed slot (insert, send)
    };

    void key (juce::Button&, Key style);

    /** A vertical fader with a metal cap, or a horizontal one with a small cap. */
    void fader (juce::Slider&);

    /**
        An encoder with a neon LED ring, as on modern digital consoles: the ring lights
        from the start (or from the centre, for a knob that goes both ways) up to the value.
        `proportion` is 0..1; `fromCentre` lights from 0.5.
    */
    void drawNeonKnob (juce::Graphics&, juce::Rectangle<float> area, float proportion, float startAngle, float endAngle,
                       juce::Colour ring, bool fromCentre, bool enabled = true);

    /** A dark fader cap with grip ridges and a white line across its middle. */
    void drawFaderCap (juce::Graphics&, juce::Rectangle<float> cap, bool vertical);

    /** Draws a raised key; used by the look and by custom buttons. */
    void drawKey (juce::Graphics&, juce::Rectangle<float> area, Key style, bool on, juce::Colour onColour,
                  bool over, bool down);
}

/** The transport's cream keys with drawn icons. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { toStart, rewind, forward, stop, play, record, cycle, metronome, countIn, mixer, pianoRoll, keys, plus, generate, chat, screen };

    IconButton (const juce::String& name, Icon icon, juce::Colour onColour = juce::Colour (0xff3fa34d));

    void setIcon (Icon newIcon) { icon = newIcon; repaint(); }

    void paintButton (juce::Graphics&, bool over, bool down) override;

    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour colour);

private:
    Icon icon;
    juce::Colour onColour;
};

/**
    A moving-coil style meter showing the Master Bus Compressor's gain
    reduction. The needle rests at 0 on the right and swings left.
*/
class GainReductionMeter final : public juce::Component
{
public:
    /** Feed it the latest reduction (dB, positive) ~30 times a second. */
    void push (float reductionDb);
    float getNeedleDb() const noexcept { return needle; }

    void paint (juce::Graphics&) override;

    static constexpr float fullScaleDb = 20.0f;

private:
    float needle = 0.0f;
};

} // namespace riff
