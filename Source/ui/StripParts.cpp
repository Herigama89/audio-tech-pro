#include "StripParts.h"
#include "ConsoleLook.h"
#include "Skin.h"
#include "ThemePack.h"
#include "Looks.h"

namespace riff::strip
{

//==============================================================================
bool DragSlider::dragsVertically() const
{
    const auto style = getSliderStyle();
    return style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar && style != juce::Slider::RotaryHorizontalDrag;
}

float DragSlider::dragTravel() const
{
    if (isRotary())
        return 180.0f;
    return (float) juce::jmax (40, (dragsVertically() ? getHeight() : getWidth()) - 14);
}

juce::Point<float> DragSlider::handlePosition() const
{
    if (isRotary())
        return getLocalBounds().toFloat().getCentre();
    const auto p = (float) const_cast<DragSlider*> (this)->valueToProportionOfLength (getValue());
    const auto r = getLocalBounds().toFloat().reduced (7.0f);
    return dragsVertically() ? juce::Point<float> (r.getCentreX(), r.getBottom() - p * r.getHeight())
                             : juce::Point<float> (r.getX() + p * r.getWidth(), r.getCentreY());
}

void DragSlider::mouseDown (const juce::MouseEvent& e)
{
    menuClick = false;
    if (e.mods.isPopupMenu() && onRightClick)
    {
        menuClick = true;
        onRightClick (e);
        return;
    }
    if (e.mods.isPopupMenu() || ! isEnabled())
    {
        Slider::mouseDown (e);
        return;
    }

    // Option-click: back to the default (0 dB on a fader, centre on a pan knob).
    if (e.mods.isAltDown() && isDoubleClickReturnEnabled())
    {
        if (onDragStart) onDragStart();
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
        if (onDragEnd) onDragEnd();
        return;
    }

    dragging = true;
    lastDelta = 0.0f;
    proportion = valueToProportionOfLength (getValue());
    if (onDragStart)
        onDragStart();
    e.source.enableUnboundedMouseMovement (true, false);   // the pointer hides while you drag
}

void DragSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (menuClick)
        return;
    if (! dragging)
    {
        Slider::mouseDrag (e);
        return;
    }
    const auto delta = dragsVertically() ? (float) -e.getDistanceFromDragStartY() : (float) e.getDistanceFromDragStartX();
    const auto step = delta - lastDelta;
    lastDelta = delta;
    const auto fine = e.mods.isShiftDown() ? 0.12 : 1.0;   // Shift: fine adjustment
    proportion = juce::jlimit (0.0, 1.0, proportion + (double) step * fine / (double) juce::jmax (1.0f, dragTravel()));
    setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
}

void DragSlider::mouseUp (const juce::MouseEvent& e)
{
    if (menuClick)
    {
        menuClick = false;
        return;
    }
    if (! dragging)
    {
        Slider::mouseUp (e);
        return;
    }
    dragging = false;
    e.source.enableUnboundedMouseMovement (false);
    // The pointer comes back on the fader cap (or the knob), where the control now is.
    juce::Desktop::setMousePosition (localPointToGlobal (handlePosition()).roundToInt());
    if (onDragEnd)
        onDragEnd();
}

void DragSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isDoubleClickReturnEnabled() && ! e.mods.isPopupMenu())
    {
        if (onDragStart) onDragStart();
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
        if (onDragEnd) onDragEnd();
    }
}

//==============================================================================
PointerKnob::PointerKnob (juce::Colour pointerColour) : pointer (pointerColour)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::degreesToRadians (220.0f), juce::degreesToRadians (500.0f), true);   // (-140 to +140 degrees)
    setMouseDragSensitivity (200);
    setScrollWheelEnabled (false);
    setWantsKeyboardFocus (false);
}

void PointerKnob::paint (juce::Graphics& g)
{
    // Themes > Knobs: a photo knob, or the classic drawn one, whatever the skin.
    if (theme::knobStyle() >= 0)
    {
        const auto params = getRotaryParameters();
        const auto proportion = (float) valueToProportionOfLength (getValue());
        const bool fromCentre = getMinimum() < 0.0 && getMaximum() > 0.0;
        if (! looks::drawKnob (g, getLocalBounds().toFloat(), proportion, params.startAngleRadians, params.endAngleRadians, fromCentre, isEnabled()))
            look::drawNeonKnob (g, getLocalBounds().toFloat(), proportion, params.startAngleRadians, params.endAngleRadians,
                                theme::knobBlack, fromCentre, isEnabled(), true);
        return;
    }
    if (theme::reaperSkin())
    {
        const auto params = getRotaryParameters();
        juce::ignoreUnused (params);
        if (themepack::drawKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()),
                                 getMinimum() < 0.0 && getMaximum() > 0.0, isEnabled()))
            return;
    }
    // Studio White: the photo's knob, its LED ring lit to the value (from the centre for EQ gains and pan).
    if (theme::xenonSkin())
    {
        const auto params = getRotaryParameters();
        skin::drawXenonKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()),
                             params.startAngleRadians, params.endAngleRadians, isEnabled());
        return;
    }
    if (theme::whiteSkin())
    {
        const auto params = getRotaryParameters();
        skin::drawPhotoKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()),
                             params.startAngleRadians, params.endAngleRadians, getMinimum() < 0.0 && getMaximum() > 0.0, isEnabled());
        return;
    }

    if (skinBody.isValid() && theme::imageSkin())
    {
        const auto params = getRotaryParameters();
        const auto angle = params.startAngleRadians + (float) valueToProportionOfLength (getValue()) * (params.endAngleRadians - params.startAngleRadians);
        const auto area = getLocalBounds().toFloat();
        if (cap.isTransparent())
        {
            skin::drawKnob (g, skinBody, area, angle, skinPointer, isEnabled() ? 1.0f : 0.5f);
            return;
        }
        // The knob, then its coloured cap with the pointer line across it.
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto r = area.withSizeKeepingCentre (size, size);
        g.setOpacity (isEnabled() ? 1.0f : 0.5f);
        g.drawImage (skinBody, r);
        const auto capArea = r.reduced (size * 0.2f);
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.45f), capArea.getCentreX(), capArea.getY(),
                                                 cap.darker (0.35f), capArea.getCentreX(), capArea.getBottom(), false));
        g.fillEllipse (capArea);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (capArea, 1.0f);
        const auto c = r.getCentre();
        auto at = [&] (float f) { return c + juce::Point<float> (size * 0.5f * f * std::sin (angle), -size * 0.5f * f * std::cos (angle)); };
        g.setColour (cap.getPerceivedBrightness() > 0.7f ? juce::Colour (0xff151515) : juce::Colours::white);
        g.drawLine (juce::Line<float> (at (0.0f), at (0.6f)), juce::jmax (1.4f, size * 0.07f));
        return;
    }

    // A neon-ring encoder (EQ gains light from the centre, both ways).
    const auto params = getRotaryParameters();
    look::drawNeonKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()),
                        params.startAngleRadians, params.endAngleRadians, pointer,
                        getMinimum() < 0.0 && getMaximum() > 0.0, isEnabled(), look::isOnLight (*this));
}

//==============================================================================
LampKey::LampKey (const juce::String& name, juce::Colour lampColour) : juce::Button (name), lamp (lampColour)
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
}

void LampKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (onArt)
    {
        // The printed lamp lights up.
        const auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.12f);
        if (getToggleState())
        {
            g.setColour (lamp.withAlpha (0.35f));
            g.fillRoundedRectangle (r.expanded (2.0f), 3.0f);
            g.setColour (lamp.brighter (0.5f));
            g.fillRoundedRectangle (r, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.25f), 1.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (over ? 0.1f : 0.3f));
            g.fillRoundedRectangle (r, 1.5f);
        }
        juce::ignoreUnused (down);
        return;
    }

    // A small backlit key: dark when off, glowing in its colour when on.
    const auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.12f);
    if (looks::drawButton (g, r, getToggleState(), lamp, over, down))
        return;
    look::drawKey (g, r, look::Key::console, getToggleState(), lamp, over, down);
}

//==============================================================================
CreamKey::CreamKey (const juce::String& letter, juce::Colour litColour) : juce::Button (letter), lit (litColour)
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
}

void CreamKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    // Themes > Buttons: the photo's key, lit in its colour, its letter on it.
    {
        const auto face = getLocalBounds().toFloat().reduced (getWidth() * 0.07f, getHeight() * 0.1f);
        if (looks::drawButton (g, face, getToggleState(), lit, over, down))
        {
            g.setColour (looks::buttonTextColour (getToggleState(), lit));
            g.setFont (juce::FontOptions (face.getHeight() * 0.58f, juce::Font::bold));
            g.drawText (getName(), face, juce::Justification::centred, false);
            return;
        }
    }
    // A REAPER theme's own mute / solo / record / monitor button
    if (theme::reaperSkin())
    {
        const auto letter = getName();
        const auto kind = letter == "M" ? "mute" : letter == "S" ? "solo" : letter == "R" ? "recarm" : letter == "I" ? "monitor" : "";
        if (juce::String (kind).isNotEmpty() && themepack::drawButton (g, kind, getToggleState(), over, down, getLocalBounds().toFloat()))
            return;
    }
    if (onArt)
    {
        // The key is on the art; light it in its colour when on, and print its letter.
        const auto face = getLocalBounds().toFloat().reduced (getWidth() * 0.1f, getHeight() * 0.14f);
        if (getToggleState())
        {
            g.setColour (lit.withAlpha (0.85f));
            g.fillRoundedRectangle (face, 2.0f);
        }
        else if (over || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.05f : 0.08f));
            g.fillRoundedRectangle (face, 2.0f);
        }
        g.setColour (getToggleState() && lit.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff111214) : juce::Colour (0xffe6e9ea));
        g.setFont (juce::FontOptions (face.getHeight() * 0.6f, juce::Font::bold));
        g.drawText (getName(), face, juce::Justification::centred, false);
        return;
    }

    // A backlit rubber key (mute, solo), lit in its colour when on.
    const auto face = getLocalBounds().toFloat().reduced (getWidth() * 0.07f, getHeight() * 0.12f);
    look::drawKey (g, face, look::Key::console, getToggleState(), lit, over, down);

    g.setColour (getToggleState() ? (lit.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff111214) : juce::Colours::white)
                                  : theme::ink.withAlpha (0.85f));
    g.setFont (juce::FontOptions (face.getHeight() * 0.62f, juce::Font::bold));
    g.drawText (getName(), face, juce::Justification::centred, false);
}

//==============================================================================
DisplaySlider::DisplaySlider()
{
    setSliderStyle (juce::Slider::RotaryHorizontalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (120);
    setScrollWheelEnabled (false);
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void DisplaySlider::paint (juce::Graphics& g)
{
    if (asKnob)
    {
        const auto proportion = (float) valueToProportionOfLength (getValue());
        if (theme::knobStyle() >= 0)
        {
            if (! looks::drawKnob (g, getLocalBounds().toFloat(), proportion, juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true, isEnabled()))
                look::drawNeonKnob (g, getLocalBounds().toFloat(), proportion, juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f),
                                    theme::knobBlack, true, isEnabled(), true);
            return;
        }
        if (theme::reaperSkin() && themepack::drawKnob (g, getLocalBounds().toFloat(), proportion, true, isEnabled()))
            return;
        if (theme::xenonSkin())
        {
            skin::drawXenonKnob (g, getLocalBounds().toFloat(), proportion, juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f),
                                 isEnabled());
            return;
        }
        if (theme::whiteSkin())
        {
            skin::drawPhotoKnob (g, getLocalBounds().toFloat(), proportion, juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f),
                                 true, isEnabled());
            return;
        }
        look::drawNeonKnob (g, getLocalBounds().toFloat(), proportion, juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f),
                            theme::knobBlack, true, isEnabled(), true);
        return;
    }

    const auto r = getLocalBounds().toFloat();
    g.setColour ((look::isOnLight (*this) ? theme::stripInk : juce::Colour (0xffe9e6dc)).withMultipliedAlpha (dimmed));
    g.setFont (juce::FontOptions (r.getHeight() * 0.56f));
    g.drawText (getTextFromValue (getValue()), r, juce::Justification::centred, false);
}

//==============================================================================
Fader::Fader()
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setSliderSnapsToMousePosition (false);   // grab the cap and move it; clicks don't jump
    setScrollWheelEnabled (false);
    setWantsKeyboardFocus (false);
}

float Fader::capHeight() const
{
    return juce::jlimit (24.0f, 40.0f, (float) getWidth() * 1.1f);
}

float Fader::yForValue (double value)
{
    const auto travel = (float) getHeight() - capHeight();
    return capHeight() * 0.5f + (1.0f - (float) valueToProportionOfLength (value)) * travel;
}

void Fader::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto cx = bounds.getCentreX();

    const auto style = theme::faderStyle();

    // A REAPER theme's fader cap on a thin slot
    if (style == 3)
    {
        const auto& cap = themepack::piece ({ "mcp_volthumb", "gen_volthumb_vert" });
        if (cap.isValid())
        {
            const auto slotArea = juce::Rectangle<float> (cx - 2.0f, capHeight() * 0.5f - 6.0f, 4.0f, bounds.getHeight() - capHeight() + 12.0f);
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.fillRoundedRectangle (slotArea, 2.0f);
            const auto h = juce::jmin (capHeight() * 1.25f, cap.logicalHeight());
            const auto w = juce::jmin (bounds.getWidth(), h * cap.logicalWidth() / juce::jmax (1.0f, cap.logicalHeight()));
            themepack::draw (g, cap, 0, juce::Rectangle<float> (w, h).withCentre ({ cx, yForValue (getValue()) }), false, isEnabled() ? 1.0f : 0.6f);
            return;
        }
    }

    // A photo cap (Themes > Faders) on a dark slot
    if (style >= 4)
    {
        const auto slotArea = juce::Rectangle<float> (cx - 2.5f, capHeight() * 0.5f - 6.0f, 5.0f, bounds.getHeight() - capHeight() + 12.0f);
        g.setColour (juce::Colour (0xff060708));
        g.fillRoundedRectangle (slotArea, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawLine (slotArea.getRight() + 0.5f, slotArea.getY() + 2.0f, slotArea.getRight() + 0.5f, slotArea.getBottom() - 2.0f, 1.0f);
        // As long as the classic cap, no wider than it by much (a wide cap would hide the scale).
        auto h = capHeight() * 1.3f;
        if (const auto maxWidth = capHeight() * 0.72f; looks::capWidthForHeight (h) > maxWidth)
            h *= maxWidth / looks::capWidthForHeight (h);
        if (looks::drawFaderCap (g, juce::Rectangle<float> (looks::capWidthForHeight (h), h).withCentre ({ cx, yForValue (getValue()) }), true,
                                 isEnabled() ? 1.0f : 0.6f))
            return;
    }

    // Studio White: the photo's rail and its cap (in a white shade), as photographed.
    if (style == 1)
    {
        const auto rail = skin::whiteRail();
        const auto railWidth = juce::jlimit (5.0f, 9.0f, bounds.getWidth() * 0.22f);
        skin::tileDown (g, rail, juce::Rectangle<float> (cx - railWidth * 0.5f, capHeight() * 0.5f - 6.0f, railWidth,
                                                         bounds.getHeight() - capHeight() + 12.0f));
        const auto cap = skin::whiteCap();
        const auto h = capHeight() * 1.15f;
        const auto w = h * (float) cap.getWidth() / (float) cap.getHeight();
        g.setOpacity (isEnabled() ? 1.0f : 0.6f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (cap, juce::Rectangle<float> (w, h).withCentre ({ cx, yForValue (getValue()) }));
        return;
    }

    // Xenon: a dark slot with a faint teal line, and the picture's cap.
    if (style == 2)
    {
        const auto slotArea = juce::Rectangle<float> (cx - 2.0f, capHeight() * 0.5f - 6.0f, 4.0f, bounds.getHeight() - capHeight() + 12.0f);
        g.setColour (juce::Colour (0xff05080a));
        g.fillRoundedRectangle (slotArea, 2.0f);
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRect (slotArea.withSizeKeepingCentre (1.0f, slotArea.getHeight() - 4.0f));
        const auto cap = skin::xenonCap();
        const auto h = capHeight() * 1.2f;
        const auto w = h * (float) cap.getWidth() / (float) cap.getHeight();
        g.setOpacity (isEnabled() ? 1.0f : 0.6f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (cap, juce::Rectangle<float> (w, h).withCentre ({ cx, yForValue (getValue()) }));
        return;
    }

    // The slot the cap runs in
    const auto slot = juce::Rectangle<float> (cx - 2.5f, capHeight() * 0.5f - 6.0f, 5.0f, bounds.getHeight() - capHeight() + 12.0f);
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (slot.getRight() + 0.5f, slot.getY() + 2.0f, slot.getRight() + 0.5f, slot.getBottom() - 2.0f, 1.0f);

    // The cap (the picture's cap keeps its shape: 20 x 34)
    const auto capWidth = capHeight() * 21.0f / 36.0f;
    look::drawFaderCap (g, juce::Rectangle<float> (capWidth, capHeight()).withCentre ({ cx, yForValue (getValue()) }), true);
}

} // namespace riff::strip
