#include "ConsoleLook.h"
#include "Theme.h"
#include "ToolIcons.h"
#include "Skin.h"
#include "PluginNames.h"

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
    const auto ring = capColour (slider.getProperties().getWithDefault (knobProperty, (int) look::Knob::black));
    const bool fromCentre = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    look::drawNeonKnob (g, bounds, sliderPos, startAngle, endAngle, ring, fromCentre, slider.isEnabled(), isLight (slider));
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
        g.setColour ((light ? theme::stripInk : theme::text).withAlpha (0.45f * alpha));
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
        look::drawFaderCap (g, juce::Rectangle<float> (capWidth, 30.0f).withCentre ({ cx, sliderPos }), true);
    }
    else
    {
        const auto cy = bounds.getCentreY();
        const auto slot = juce::Rectangle<float> (bounds.getX(), cy - 2.0f, bounds.getWidth(), 4.0f);
        g.setColour (juce::Colour (0xff1a1a1d).withAlpha (alpha));
        g.fillRoundedRectangle (slot, 2.0f);

        // A faint level fill, so it reads at a glance.
        g.setColour ((light ? theme::knobArc : theme::accent).withAlpha (0.55f * alpha));
        g.fillRoundedRectangle (slot.withRight (sliderPos).reduced (0.0f, 0.5f), 2.0f);

        look::drawFaderCap (g, juce::Rectangle<float> (12.0f, juce::jmin (18.0f, bounds.getHeight())).withCentre ({ sliderPos, cy }), false);
    }
}

void ConsoleLook::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool over, bool down)
{
    const auto style = static_cast<look::Key> ((int) button.getProperties().getWithDefault (keyProperty, (int) look::Key::dark));
    const auto on = button.getToggleState();
    const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);

    auto area = button.getLocalBounds().toFloat().reduced (0.5f);

    // Inserts and sends: the picture's slots.
    if (style == look::Key::slot)
        if (const auto kind = button.getProperties().getWithDefault ("atpSkin", {}).toString(); kind.isNotEmpty())
        {
            const auto image = kind == "send" ? skin::send() : skin::slot();
            g.setOpacity (backgroundColour == theme::stripSlot ? 0.92f : 1.0f);
            g.drawImage (image, area);
            if (backgroundColour == theme::stripSlotOn)
            {
                g.setColour (theme::knobArc.withAlpha (0.18f));
                g.fillRoundedRectangle (area.reduced (1.5f), 2.0f);
            }
            if (over)
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRoundedRectangle (area, 2.5f);
            }
            return;
        }

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

void ConsoleLook::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool over, bool down)
{
    if (button.getProperties().getWithDefault ("atpSkin", {}).toString().isEmpty())
    {
        LookAndFeel_V4::drawButtonText (g, button, over, down);
        return;
    }
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (button.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    const auto area = button.getLocalBounds().reduced (3, 0);
    g.drawText (pluginnames::abbreviate (button.getButtonText(), font, (float) area.getWidth()), area, juce::Justification::centred, false);
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

void ConsoleLook::drawComboBox (juce::Graphics& g, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox& box)
{
    LookAndFeel_V4::drawComboBox (g, width, height, down, bx, by, bw, bh, box);
    if ((bool) box.getProperties().getWithDefault ("atpToolIcons", false) && box.getSelectedId() > 0)
        toolicons::draw (g, (toolicons::Tool) (box.getSelectedId() - 1),
                         juce::Rectangle<float> (5.0f, 0.0f, (float) height - 8.0f, (float) height).withSizeKeepingCentre ((float) height - 10.0f, (float) height - 10.0f),
                         box.findColour (juce::ComboBox::textColourId));
}

void ConsoleLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    LookAndFeel_V4::positionComboBoxText (box, label);
    if ((bool) box.getProperties().getWithDefault ("atpToolIcons", false))
        label.setBounds (label.getBounds().withTrimmedLeft (box.getHeight() - 8));
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
        s.setRotaryParameters (juce::degreesToRadians (220.0f), juce::degreesToRadians (500.0f), true);   // (-140 to +140 degrees)
        s.getProperties().set (knobProperty, (int) cap);
        s.getProperties().set (ticksProperty, ticks);
    }

    void onLight (juce::Component& c, bool light)
    {
        c.getProperties().set (lightProperty, light);
    }

    bool isOnLight (const juce::Component& c)
    {
        return isLight (c);
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

    void drawNeonKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, float startAngle, float endAngle,
                       juce::Colour ringColour, bool fromCentre, bool enabled, bool light)
    {
        // A studio console knob: a scale of ticks printed round it, a thin value ring,
        // and a knurled black knob (or a silver one) with a pointer line.
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto centre = area.getCentre();
        const auto outer = size * 0.5f;
        const auto alpha = enabled ? 1.0f : 0.45f;
        proportion = juce::jlimit (0.0f, 1.0f, proportion);
        const bool silver = ringColour.getPerceivedBrightness() > 0.85f;
        const auto print = (light ? juce::Colour (0xff15171a) : juce::Colour (0xffc9ccd0)).withAlpha (0.8f * alpha);

        // Printed scale: ticks round the knob, longer at the ends and the middle.
        const auto ticks = outer > 22.0f ? 11 : 9;
        for (int i = 0; i < ticks; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / (float) (ticks - 1);
            const bool major = i == 0 || i == ticks - 1 || i == ticks / 2;
            g.setColour (print);
            g.drawLine (juce::Line<float> (onCircle (centre, outer * (major ? 0.80f : 0.85f), a), onCircle (centre, outer * 0.97f, a)),
                        juce::jmax (0.8f, outer * (major ? 0.07f : 0.05f)));
        }

        // The knob itself: the picture's knob (black, or white), with its pointer.
        const auto to = startAngle + proportion * (endAngle - startAngle);
        juce::ignoreUnused (fromCentre);
        // On the grey face: black knobs (silver for the white ones); on a black panel: the panel's white knobs.
        const bool white = silver || ! light;
        if (const auto body = silver ? skin::knobWhite() : (light ? skin::knobBlack() : skin::knobBus()); body.isValid())
        {
            const auto knobArea = area.withSizeKeepingCentre (outer * 1.5f, outer * 1.5f);
            g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
            g.fillEllipse (knobArea.translated (outer * 0.03f, outer * 0.08f).reduced (outer * 0.06f));
            skin::drawKnob (g, body, knobArea, to, white ? juce::Colour (0xff141516) : juce::Colours::white, alpha);
            return;
        }

        const auto from = startAngle + (fromCentre ? 0.5f : 0.0f) * (endAngle - startAngle);
        if (std::abs (to - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, outer * 0.75f, outer * 0.75f, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
            g.setColour (theme::knobArc.withAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (juce::jmax (1.2f, outer * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Shadow on the panel.
        const auto skirtRadius = outer * 0.68f;
        const auto skirt = juce::Rectangle<float> (skirtRadius * 2.0f, skirtRadius * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha ((light ? 0.35f : 0.6f) * alpha));
        g.fillEllipse (skirt.translated (outer * 0.03f, outer * 0.08f).expanded (outer * 0.03f));

        // The skirt: a dark knurled ring.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3f43), centre.x, skirt.getY(), juce::Colour (0xff070809), centre.x, skirt.getBottom(), false));
        g.setOpacity (alpha);
        g.fillEllipse (skirt);
        if (skirtRadius > 9.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f * alpha));
            const auto grooves = 36;
            for (int i = 0; i < grooves; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) grooves;
                g.drawLine (juce::Line<float> (onCircle (centre, skirtRadius * 0.8f, a), onCircle (centre, skirtRadius * 0.98f, a)), 0.8f);
            }
        }

        // The top: black with a soft light from above, or brushed silver.
        const auto capRadius = skirtRadius * (silver ? 0.86f : 0.78f);
        const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
        if (silver)
        {
            juce::ColourGradient metal (juce::Colour (0xfff7f7f5), centre.x, cap.getY(), juce::Colour (0xffb3b6b9), centre.x, cap.getBottom(), false);
            metal.addColour (0.55, juce::Colour (0xffdcdedf));
            g.setGradientFill (metal);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d4044), centre.x, cap.getY(), juce::Colour (0xff111214), centre.x, cap.getBottom(), false));
        }
        g.setOpacity (alpha);
        g.fillEllipse (cap);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha ((silver ? 0.5f : 0.14f) * alpha), centre.x, cap.getY(),
                                                 juce::Colours::transparentWhite, centre.x, centre.y, false));
        g.fillEllipse (cap.reduced (capRadius * 0.1f));
        g.setColour (juce::Colours::black.withAlpha (0.75f * alpha));
        g.drawEllipse (cap, 0.8f);

        // The pointer: white on black, black on silver.
        g.setColour ((silver ? juce::Colour (0xff151618) : juce::Colours::white).withAlpha (0.95f * alpha));
        g.drawLine (juce::Line<float> (onCircle (centre, capRadius * (silver ? 0.15f : 0.25f), to), onCircle (centre, skirtRadius * 0.96f, to)),
                    juce::jmax (1.3f, capRadius * 0.17f));
    }

    void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical)
    {
        // The picture's fader cap.
        if (const auto image = skin::faderCap(); image.isValid())
        {
            g.setOpacity (1.0f);
            g.drawImage (image, cap.withSizeKeepingCentre (juce::jmin (cap.getWidth(), cap.getHeight() * 21.0f / 36.0f), cap.getHeight()));
            return;
        }

        // A light grey plastic cap with a dark line across its middle.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (1.0f, 2.5f), 2.5f);

        juce::ColourGradient body (juce::Colour (0xfff2f3f3), cap.getX(), cap.getY(), juce::Colour (0xff8e9195), cap.getX(), cap.getBottom(), false);
        body.addColour (0.44, juce::Colour (0xffc8cacd));
        body.addColour (0.5, juce::Colour (0xff7d8084));
        body.addColour (0.56, juce::Colour (0xffd9dbdd));
        g.setGradientFill (body);
        g.fillRoundedRectangle (cap, 2.5f);

        // Grip ridges
        if (vertical)
        {
            for (const auto f : { 0.14f, 0.24f, 0.76f, 0.86f })
            {
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.fillRect (cap.getX() + 2.0f, cap.getY() + cap.getHeight() * f, cap.getWidth() - 4.0f, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.fillRect (cap.getX() + 2.0f, cap.getY() + cap.getHeight() * f + 1.0f, cap.getWidth() - 4.0f, 0.8f);
            }
        }

        // The line
        g.setColour (juce::Colour (0xff1a1c1e));
        if (vertical)
            g.fillRect (cap.getX() + 1.5f, cap.getCentreY() - 1.0f, cap.getWidth() - 3.0f, 2.0f);
        else
            g.fillRect (cap.getCentreX() - 1.0f, cap.getY() + 1.5f, 2.0f, cap.getHeight() - 3.0f);

        g.setColour (juce::Colour (0xff3a3c3f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 2.5f, 1.0f);
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
                top = juce::Colour (0xff53575c); bottom = juce::Colour (0xff33363a); edge = juce::Colour (0xff15171a);
                corner = 2.5f;
                break;
            case Key::dark:
            case Key::slot:
                top = theme::toolbar.brighter (0.32f); bottom = theme::toolbar.brighter (0.08f); edge = theme::toolbarDark.darker (0.3f);
                break;
        }

        if (on)
        {
            // Backlit: the key glows in its colour.
            g.setColour (onColour.withAlpha (0.28f));
            g.fillRoundedRectangle (area.expanded (2.0f), corner + 2.0f);
            top = onColour.brighter (0.35f);
            bottom = onColour.darker (0.05f);
            edge = onColour.darker (0.7f);
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

        case Icon::punch:
        {
            // Two locators with the recorded stretch between them.
            const auto h = r.getHeight();
            g.fillRect (r.getX() + r.getWidth() * 0.12f, r.getY(), r.getWidth() * 0.09f, h);
            g.fillRect (r.getRight() - r.getWidth() * 0.21f, r.getY(), r.getWidth() * 0.09f, h);
            g.fillRoundedRectangle (r.getX() + r.getWidth() * 0.3f, r.getCentreY() - h * 0.17f, r.getWidth() * 0.4f, h * 0.34f, 1.5f);
            return;
        }

        case Icon::replace:
            g.setFont (juce::FontOptions (r.getHeight() * 0.75f, juce::Font::bold));
            g.drawText ("REPL", r.expanded (r.getWidth() * 0.5f, 0.0f), juce::Justification::centred, false);
            return;

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

        case Icon::screen:
        {
            // A monitor with a recording dot.
            const auto b = r.withTrimmedBottom (r.getHeight() * 0.24f);
            g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, juce::jmax (1.2f, r.getWidth() * 0.09f));
            g.fillRect (r.getCentreX() - r.getWidth() * 0.06f, b.getBottom(), r.getWidth() * 0.12f, r.getHeight() * 0.12f);
            g.fillRect (r.getCentreX() - r.getWidth() * 0.26f, r.getBottom() - r.getHeight() * 0.08f, r.getWidth() * 0.52f, r.getHeight() * 0.08f);
            const auto d = b.getHeight() * 0.42f;
            g.setColour (juce::Colour (0xffd8352d));
            g.fillEllipse (b.getCentreX() - d * 0.5f, b.getCentreY() - d * 0.5f, d, d);
            return;
        }

        case Icon::chat:
        {
            // A speech bubble with three dots.
            const auto b = r.withTrimmedBottom (r.getHeight() * 0.22f);
            juce::Path bubble;
            bubble.addRoundedRectangle (b, b.getHeight() * 0.3f);
            bubble.startNewSubPath (b.getX() + b.getWidth() * 0.22f, b.getBottom() - 1.0f);
            bubble.lineTo (b.getX() + b.getWidth() * 0.16f, r.getBottom());
            bubble.lineTo (b.getX() + b.getWidth() * 0.44f, b.getBottom() - 1.0f);
            bubble.closeSubPath();
            g.fillPath (bubble);

            g.setColour (juce::Colour (0xfff1e4c1));
            const auto d = b.getHeight() * 0.18f;
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (b.getCentreX() + (float) i * d * 1.9f - d * 0.5f, b.getCentreY() - d * 0.5f, d, d);
            return;
        }

        case Icon::generate:
        {
            // A four-pointed spark and a small one: "make something".
            auto spark = [&p] (juce::Point<float> c, float radius)
            {
                juce::Path s;
                for (int i = 0; i < 8; ++i)
                {
                    const auto angle = juce::MathConstants<float>::pi * 0.25f * (float) i;
                    const auto rr = i % 2 == 0 ? radius : radius * 0.28f;
                    const juce::Point<float> pt (c.x + rr * std::sin (angle), c.y - rr * std::cos (angle));
                    if (i == 0) s.startNewSubPath (pt); else s.lineTo (pt);
                }
                s.closeSubPath();
                p.addPath (s);
            };
            spark ({ r.getX() + r.getWidth() * 0.42f, r.getY() + r.getHeight() * 0.56f }, r.getHeight() * 0.44f);
            spark ({ r.getRight() - r.getWidth() * 0.14f, r.getY() + r.getHeight() * 0.16f }, r.getHeight() * 0.2f);
            break;
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

    // The picture's VU face (its needle taken off); the needle is drawn here.
    g.setOpacity (1.0f);
    g.drawImage (skin::vuFace(), face);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (face.toNearestInt());

        // 0 dB of reduction rests on the right (the face's 0), 20 swings to the left end.
        const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.55f);
        const auto radius = face.getHeight() * 1.35f;
        constexpr float left = -0.72f, rest = 0.38f;
        const auto pos = 1.0f - std::log10 (1.0f + needle) / std::log10 (1.0f + fullScaleDb);
        const auto a = left + (rest - left) * pos;

        const juce::Point<float> shadow (1.5f, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.drawLine (juce::Line<float> (onCircle (pivot, radius * 0.55f, a) + shadow, onCircle (pivot, radius * 0.97f, a) + shadow), 1.6f);
        g.setColour (juce::Colour (0xff141414));
        g.drawLine (juce::Line<float> (onCircle (pivot, radius * 0.55f, a), onCircle (pivot, radius * 0.97f, a)), 1.4f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (face, 2.0f, 1.5f);

    // Screw under the window
    const auto screw = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() - 8.5f });
    g.setColour (juce::Colour (0xff6a6a6e));
    g.fillEllipse (screw);
    g.setColour (juce::Colour (0xff202022));
    g.drawLine (screw.getX() + 1.5f, screw.getCentreY(), screw.getRight() - 1.5f, screw.getCentreY(), 1.0f);
}

} // namespace riff
