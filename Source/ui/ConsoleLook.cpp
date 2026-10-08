#include "ConsoleLook.h"
#include "Theme.h"

namespace riff
{

namespace
{
    const juce::Identifier knobProperty { "atpKnob" }, ticksProperty { "atpTicks" },
                           lightProperty { "atpLight" }, keyProperty { "atpKey" }, faderProperty { "atpFader" };

    juce::Colour capColour (int knob)
    {
        switch (static_cast<look::Knob> (knob))
        {
            case look::Knob::cream: return theme::knobCream;
            case look::Knob::blue:  return theme::knobBlue;
            case look::Knob::red:   return theme::knobRed;
            case look::Knob::black: break;
        }
        return theme::knobBlack;
    }

    bool isLight (const juce::Component& c)
    {
        return (bool) c.getProperties().getWithDefault (lightProperty, false);
    }

    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre + juce::Point<float> (radius * std::sin (angle), -radius * std::cos (angle));
    }

    /** A brushed-metal fader cap. */
    void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical)
    {
        // Shadow
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (1.0f, 2.0f), 2.5f);

        juce::ColourGradient body (juce::Colour (0xfff6f6f4), cap.getX(), cap.getY(),
                                   juce::Colour (0xffcfcfcc), cap.getX(), cap.getBottom(), false);
        body.addColour (0.45, juce::Colour (0xffa9a9a6));
        body.addColour (0.55, juce::Colour (0xffe7e7e4));
        g.setGradientFill (body);
        g.fillRoundedRectangle (cap, 2.5f);

        g.setColour (juce::Colour (0xff4b4b4b));
        g.drawRoundedRectangle (cap.reduced (0.5f), 2.5f, 1.0f);

        g.setColour (juce::Colour (0xff1b1b1b));
        if (vertical)
        {
            g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            for (const auto f : { 0.2f, 0.32f, 0.68f, 0.8f })
                g.fillRect (cap.getX() + 3.0f, cap.getY() + cap.getHeight() * f, cap.getWidth() - 6.0f, 1.0f);
        }
        else
        {
            g.fillRect (cap.getCentreX() - 0.75f, cap.getY() + 2.0f, 1.5f, cap.getHeight() - 4.0f);
        }
    }
}

//==============================================================================
ConsoleLook::ConsoleLook()
{
    theme::apply (*this);
}

void ConsoleLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (1.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto outer = size * 0.5f;

    const bool ticks = slider.getProperties().getWithDefault (ticksProperty, size >= 28.0f);
    const auto cap = capColour (slider.getProperties().getWithDefault (knobProperty, (int) look::Knob::black));
    const auto alpha = slider.isEnabled() ? 1.0f : 0.45f;

    // Scale around the knob
    const auto skirt = ticks ? outer * 0.74f : outer * 0.96f;
    if (ticks)
    {
        const auto interval = slider.getInterval();
        const auto range = slider.getMaximum() - slider.getMinimum();
        const auto steps = (interval >= 1.0 && range / interval <= 12.0) ? (int) std::round (range / interval) + 1 : 11;

        g.setColour ((isLight (slider) ? theme::ink : theme::text).withAlpha (0.75f * alpha));
        for (int i = 0; i < steps; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / (float) (steps - 1);
            const bool major = steps <= 12 || i == 0 || i == steps - 1 || i == steps / 2;
            g.drawLine (juce::Line<float> (onCircle (centre, outer * (major ? 0.82f : 0.86f), a),
                                           onCircle (centre, outer * 0.98f, a)),
                        major ? 1.3f : 1.0f);
        }
    }

    // Drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre.translated (0.8f, 1.6f)));

    // Black skirt with a soft sheen
    {
        juce::ColourGradient grad (juce::Colour (0xff4a4a4e), centre.x - skirt * 0.6f, centre.y - skirt * 0.7f,
                                   juce::Colour (0xff111113), centre.x + skirt * 0.7f, centre.y + skirt * 0.8f, false);
        g.setGradientFill (grad);
        g.setOpacity (alpha);
        g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre));
    }

    // Coloured cap (black knobs get a slightly smaller, glossier cap)
    const auto capRadius = skirt * (cap == theme::knobBlack ? 0.72f : 0.8f);
    {
        juce::ColourGradient grad (cap.brighter (0.55f), centre.x - capRadius * 0.45f, centre.y - capRadius * 0.55f,
                                   cap.darker (0.45f), centre.x + capRadius * 0.9f, centre.y + capRadius * 0.9f, true);
        g.setGradientFill (grad);
        g.setOpacity (alpha);
        const auto capArea = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
        g.fillEllipse (capArea);
        g.setColour (juce::Colours::white.withAlpha (0.22f * alpha));
        g.drawEllipse (capArea.reduced (0.6f), 0.9f);
    }

    // Pointer
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto pointerColour = cap == theme::knobCream ? theme::ink : juce::Colours::white;
    g.setColour (pointerColour.withAlpha (alpha));
    g.drawLine (juce::Line<float> (onCircle (centre, skirt * 0.18f, angle), onCircle (centre, skirt * 0.96f, angle)),
                juce::jmax (1.4f, skirt * 0.12f));
}

int ConsoleLook::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        return 15;
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal)
        return 7;
    return LookAndFeel_V4::getSliderThumbRadius (slider);
}

void ConsoleLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto light = isLight (slider);
    const auto alpha = slider.isEnabled() ? 1.0f : 0.5f;

    if (style == juce::Slider::LinearVertical)
    {
        const auto cx = bounds.getCentreX();

        // Scale marks either side of the slot
        g.setColour ((light ? theme::ink : theme::text).withAlpha (0.45f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const auto ty = bounds.getY() + bounds.getHeight() * (float) i / 10.0f;
            const auto len = (i % 5 == 0) ? 7.0f : 4.0f;
            g.fillRect (cx - 9.0f - len, ty - 0.5f, len, 1.0f);
            g.fillRect (cx + 9.0f, ty - 0.5f, len, 1.0f);
        }

        // Slot
        const auto slot = juce::Rectangle<float> (cx - 2.5f, bounds.getY() - 4.0f, 5.0f, bounds.getHeight() + 8.0f);
        g.setColour (juce::Colour (0xff141416).withAlpha (alpha));
        g.fillRoundedRectangle (slot, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (light ? 0.5f : 0.08f));
        g.drawLine (slot.getRight() + 0.5f, slot.getY() + 2.0f, slot.getRight() + 0.5f, slot.getBottom() - 2.0f, 1.0f);

        const auto capWidth = juce::jlimit (16.0f, 26.0f, bounds.getWidth() - 4.0f);
        drawFaderCap (g, juce::Rectangle<float> (capWidth, 30.0f).withCentre ({ cx, sliderPos }), true);
    }
    else
    {
        const auto cy = bounds.getCentreY();
        const auto slot = juce::Rectangle<float> (bounds.getX(), cy - 2.0f, bounds.getWidth(), 4.0f);
        g.setColour (juce::Colour (0xff1a1a1d).withAlpha (alpha));
        g.fillRoundedRectangle (slot, 2.0f);

        // A faint level fill, so it reads at a glance.
        g.setColour ((light ? theme::inkDim : theme::accent).withAlpha (0.55f * alpha));
        g.fillRoundedRectangle (slot.withRight (sliderPos).reduced (0.0f, 0.5f), 2.0f);

        drawFaderCap (g, juce::Rectangle<float> (12.0f, juce::jmin (18.0f, bounds.getHeight())).withCentre ({ sliderPos, cy }), false);
    }
}

void ConsoleLook::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool over, bool down)
{
    const auto style = static_cast<look::Key> ((int) button.getProperties().getWithDefault (keyProperty, (int) look::Key::dark));
    const auto on = button.getToggleState();
    const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);

    auto area = button.getLocalBounds().toFloat().reduced (0.5f);

    if (style == look::Key::slot)
    {
        // Recessed: the slot's own colour says whether it holds something.
        g.setColour (backgroundColour);
        g.fillRoundedRectangle (area, 2.5f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (area, 2.5f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (area.reduced (2.0f, 0.0f).removeFromTop (1.5f).translated (0.0f, 1.0f));
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (area, 2.5f);
        }
        return;
    }

    look::drawKey (g, area, style, on, onColour, over && button.isEnabled(), down);

    if (! button.isEnabled())
    {
        g.setColour ((style == look::Key::dark ? theme::toolbar : theme::console).withAlpha (0.5f));
        g.fillRoundedRectangle (area, 3.0f);
    }
}

juce::Font ConsoleLook::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto style = static_cast<look::Key> ((int) button.getProperties().getWithDefault (keyProperty, (int) look::Key::dark));
    const auto size = juce::jmin (style == look::Key::slot ? 11.5f : 13.0f, (float) buttonHeight * 0.62f,
                                  juce::jmax (9.0f, (float) button.getWidth() * 0.55f));
    return juce::FontOptions (size, style == look::Key::console || style == look::Key::cream ? juce::Font::bold : juce::Font::plain);
}

void ConsoleLook::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (theme::outline.brighter (over || down ? 0.45f : 0.25f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

void ConsoleLook::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawRect (0, 0, width, height);
}

void ConsoleLook::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (theme::toolbarDark);
    g.setColour (theme::outline);
    g.drawRect (0, 0, width, height);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centred);
    s.append (text, juce::FontOptions (13.0f), theme::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, (float) width - 8.0f);
    layout.draw (g, { 4.0f, 0.0f, (float) width - 8.0f, (float) height });
}

//==============================================================================
namespace look
{
    void knob (juce::Slider& s, Knob cap, bool ticks)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
        s.getProperties().set (knobProperty, (int) cap);
        s.getProperties().set (ticksProperty, ticks);
    }

    void onLight (juce::Component& c, bool light)
    {
        c.getProperties().set (lightProperty, light);
    }

    void key (juce::Button& b, Key style)
    {
        b.getProperties().set (keyProperty, (int) style);

        switch (style)
        {
            case Key::cream:
            case Key::console:
                b.setColour (juce::TextButton::textColourOffId, theme::ink);
                b.setColour (juce::TextButton::textColourOnId, theme::ink);
                break;
            case Key::slot:
                b.setColour (juce::TextButton::buttonColourId, theme::slotDark);
                b.setColour (juce::TextButton::textColourOffId, theme::text);
                break;
            case Key::dark:
                b.setColour (juce::TextButton::textColourOffId, theme::text);
                b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                break;
        }
    }

    void fader (juce::Slider& s)
    {
        s.getProperties().set (faderProperty, true);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    void drawKey (juce::Graphics& g, juce::Rectangle<float> area, Key style, bool on, juce::Colour onColour, bool over, bool down)
    {
        juce::Colour top, bottom, edge;
        auto corner = 3.5f;

        switch (style)
        {
            case Key::cream:
                top = theme::cream.brighter (0.15f); bottom = theme::creamShade; edge = theme::creamDeep.darker (0.45f);
                break;
            case Key::console:
                top = juce::Colour (0xffd9d4c7); bottom = juce::Colour (0xffb5ae9e); edge = theme::consoleDark.darker (0.3f);
                corner = 2.5f;
                break;
            case Key::dark:
            case Key::slot:
                top = theme::toolbar.brighter (0.32f); bottom = theme::toolbar.brighter (0.08f); edge = theme::toolbarDark.darker (0.3f);
                break;
        }

        if (on)
        {
            top = onColour.brighter (0.25f);
            bottom = onColour.darker (0.1f);
            edge = onColour.darker (0.6f);
        }

        if (down)
        {
            std::swap (top, bottom);
            top = top.darker (0.08f);
        }
        else if (over)
        {
            top = top.brighter (0.07f);
            bottom = bottom.brighter (0.07f);
        }

        // Shadow under the key
        g.setColour (juce::Colours::black.withAlpha (down ? 0.15f : 0.35f));
        g.fillRoundedRectangle (area.translated (0.0f, 1.2f), corner);

        g.setGradientFill (juce::ColourGradient (top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, corner);

        g.setColour (juce::Colours::white.withAlpha (on ? 0.25f : 0.35f));
        g.drawLine (area.getX() + corner, area.getY() + 1.0f, area.getRight() - corner, area.getY() + 1.0f, 1.0f);

        g.setColour (edge);
        g.drawRoundedRectangle (area, corner, 1.0f);
    }
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i, juce::Colour on)
    : juce::Button (name), icon (i), onColour (on)
{
    setWantsKeyboardFocus (false);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (0.5f, 0.5f).withTrimmedBottom (1.0f);
    const bool on = getToggleState();
    look::drawKey (g, area, look::Key::cream, on, onColour, over && isEnabled(), down);

    auto colour = on ? (onColour.getPerceivedBrightness() > 0.6f ? theme::ink : juce::Colours::white) : theme::ink;
    if (icon == Icon::record && ! on)
        colour = theme::danger;
    if (! isEnabled())
        colour = colour.withAlpha (0.35f);

    const auto iconArea = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()) * 0.5f,
                                                      juce::jmin (area.getWidth(), area.getHeight()) * 0.5f);
    drawIcon (g, icon, iconArea, colour);
}

void IconButton::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour);
    juce::Path p;

    auto triangle = [&p] (juce::Rectangle<float> box, bool right)
    {
        if (right) p.addTriangle (box.getX(), box.getY(), box.getRight(), box.getCentreY(), box.getX(), box.getBottom());
        else       p.addTriangle (box.getRight(), box.getY(), box.getX(), box.getCentreY(), box.getRight(), box.getBottom());
    };

    switch (icon)
    {
        case Icon::play:
            triangle (r.reduced (r.getWidth() * 0.08f, 0.0f).translated (r.getWidth() * 0.06f, 0.0f), true);
            break;

        case Icon::stop:
            p.addRectangle (r.reduced (r.getWidth() * 0.1f));
            break;

        case Icon::record:
            p.addEllipse (r.reduced (r.getWidth() * 0.04f));
            break;

        case Icon::rewind:
        case Icon::forward:
        {
            const bool right = icon == Icon::forward;
            auto box = r.reduced (0.0f, r.getHeight() * 0.12f);
            triangle (box.removeFromLeft (box.getWidth() * 0.5f), right);
            triangle (box, right);
            break;
        }

        case Icon::toStart:
        {
            auto box = r.reduced (0.0f, r.getHeight() * 0.12f);
            p.addRectangle (box.removeFromLeft (box.getWidth() * 0.16f));
            triangle (box.removeFromLeft (box.getWidth() * 0.5f), false);
            triangle (box, false);
            break;
        }

        case Icon::cycle:
        {
            const auto c = r.getCentre();
            const auto rad = r.getWidth() * 0.42f;
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rad, rad * 0.72f, 0.0f, juce::degreesToRadians (-60.0f), juce::degreesToRadians (250.0f), true);
            g.strokePath (arc, juce::PathStrokeType (r.getWidth() * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = arc.getCurrentPosition();
            p.addTriangle (tip.x - rad * 0.38f, tip.y - rad * 0.35f, tip.x + rad * 0.12f, tip.y + rad * 0.3f, tip.x + rad * 0.38f, tip.y - rad * 0.42f);
            break;
        }

        case Icon::metronome:
        {
            const auto b = r.reduced (r.getWidth() * 0.12f, 0.0f);
            juce::Path body;
            body.addTriangle (b.getCentreX(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getBottom());
            g.strokePath (body, juce::PathStrokeType (r.getWidth() * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (b.getCentreX(), b.getBottom() - b.getHeight() * 0.25f, b.getRight() + b.getWidth() * 0.1f, b.getY() + b.getHeight() * 0.1f,
                        r.getWidth() * 0.1f);
            break;
        }

        case Icon::countIn:
            g.setFont (juce::FontOptions (r.getHeight() * 0.75f, juce::Font::bold));
            g.drawText ("1234", r.expanded (r.getWidth() * 0.5f, 0.0f), juce::Justification::centred, false);
            return;

        case Icon::mixer:
        {
            const auto w = r.getWidth();
            for (int i = 0; i < 3; ++i)
            {
                const auto x = r.getX() + w * (0.17f + 0.33f * (float) i);
                g.fillRect (x - w * 0.03f, r.getY(), w * 0.06f, r.getHeight());
                const auto capY = r.getY() + r.getHeight() * (i == 1 ? 0.25f : (i == 0 ? 0.6f : 0.45f));
                p.addRoundedRectangle (x - w * 0.11f, capY, w * 0.22f, r.getHeight() * 0.22f, 1.0f);
            }
            break;
        }

        case Icon::pianoRoll:
        {
            const auto b = r.reduced (0.0f, r.getHeight() * 0.08f);
            g.drawRect (b, juce::jmax (1.0f, r.getWidth() * 0.07f));
            const auto keyWidth = b.getWidth() / 4.0f;
            for (int i = 1; i < 4; ++i)
                g.fillRect (b.getX() + keyWidth * (float) i - 0.5f, b.getY(), 1.0f, b.getHeight());
            for (const auto f : { 1.0f, 2.0f, 3.0f })
                p.addRectangle (b.getX() + keyWidth * f - keyWidth * 0.28f, b.getY(), keyWidth * 0.56f, b.getHeight() * 0.58f);
            break;
        }

        case Icon::keys:
        {
            const auto b = r.reduced (r.getWidth() * 0.02f);
            g.drawRoundedRectangle (b, 2.0f, juce::jmax (1.0f, r.getWidth() * 0.08f));
            g.setFont (juce::FontOptions (b.getHeight() * 0.62f, juce::Font::bold));
            g.drawText ("K", b, juce::Justification::centred, false);
            return;
        }

        case Icon::plus:
        {
            const auto t = r.getWidth() * 0.16f;
            p.addRectangle (r.getCentreX() - t * 0.5f, r.getY(), t, r.getHeight());
            p.addRectangle (r.getX(), r.getCentreY() - t * 0.5f, r.getWidth(), t);
            break;
        }
    }

    g.fillPath (p);
}

//==============================================================================
void GainReductionMeter::push (float reductionDb)
{
    const auto target = juce::jlimit (0.0f, fullScaleDb, reductionDb);
    // Moving-coil ballistics: quick to swing over, slower to fall back.
    needle += (target - needle) * (target > needle ? 0.45f : 0.16f);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Bezel
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2f), bounds.getX(), bounds.getY(),
                                             juce::Colour (0xff0c0c0d), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto face = bounds.reduced (9.0f, 8.0f).withTrimmedBottom (10.0f);
    juce::Path faceShape;
    faceShape.addRoundedRectangle (face, 3.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8ecc4), face.getCentreX(), face.getY(),
                                             juce::Colour (0xffe6c983), face.getCentreX(), face.getBottom(), false));
    g.fillPath (faceShape);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (faceShape);

        // Scale: 0 dB of reduction on the right, 20 on the left.
        const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.42f);
        const auto radius = face.getHeight() * 1.08f;
        constexpr float sweep = 0.82f;   // radians either side of straight up

        auto angleFor = [] (float db)
        {
            const auto pos = 1.0f - std::log10 (1.0f + db) / std::log10 (1.0f + fullScaleDb);
            return -sweep + 2.0f * sweep * pos;
        };

        // Heavy compression zone
        {
            juce::Path zone;
            zone.addCentredArc (pivot.x, pivot.y, radius * 0.9f, radius * 0.9f, 0.0f, angleFor (fullScaleDb), angleFor (10.0f), true);
            g.setColour (juce::Colour (0xffc8322b));
            g.strokePath (zone, juce::PathStrokeType (4.0f));
        }

        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, -sweep, sweep, true);
        g.setColour (theme::ink);
        g.strokePath (arc, juce::PathStrokeType (1.2f));

        g.setFont (juce::FontOptions (juce::jmax (8.0f, face.getHeight() * 0.13f)));
        for (const auto db : { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 20.0f })
        {
            const auto a = angleFor (db);
            g.setColour (db >= 10.0f ? juce::Colour (0xffb32a24) : theme::ink);
            g.drawLine (juce::Line<float> (onCircle (pivot, radius * 0.86f, a), onCircle (pivot, radius * 0.94f, a)), 1.4f);
            const auto label = onCircle (pivot, radius * 1.02f, a);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (22.0f, 12.0f).withCentre (label), juce::Justification::centred, false);
        }

        g.setColour (theme::ink.withAlpha (0.85f));
        g.setFont (juce::FontOptions (face.getHeight() * 0.22f, juce::Font::bold));
        g.drawText ("GR", face.withTrimmedTop (face.getHeight() * 0.45f).withTrimmedBottom (face.getHeight() * 0.12f),
                    juce::Justification::centred, false);

        // Needle
        const auto a = angleFor (needle);
        g.setColour (juce::Colour (0xff1a1a1a));
        g.drawLine (juce::Line<float> (onCircle (pivot, radius * 0.45f, a), onCircle (pivot, radius * 0.97f, a)), 1.6f);

        // Glass
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), face.getX(), face.getY(),
                                                 juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
        g.fillRect (face);
    }

    // Screw under the window
    const auto screw = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() - 8.5f });
    g.setColour (juce::Colour (0xff6a6a6e));
    g.fillEllipse (screw);
    g.setColour (juce::Colour (0xff202022));
    g.drawLine (screw.getX() + 1.5f, screw.getCentreY(), screw.getRight() - 1.5f, screw.getCentreY(), 1.0f);
}

} // namespace riff
