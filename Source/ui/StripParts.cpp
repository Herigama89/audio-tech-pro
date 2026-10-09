#include "StripParts.h"
#include "ConsoleLook.h"
#include "Skin.h"

namespace riff::strip
{

//==============================================================================
PointerKnob::PointerKnob (juce::Colour pointerColour) : pointer (pointerColour)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
    setMouseDragSensitivity (200);
    setScrollWheelEnabled (false);
    setWantsKeyboardFocus (false);
}

void PointerKnob::paint (juce::Graphics& g)
{
    if (skinBody.isValid())
    {
        const auto params = getRotaryParameters();
        const auto angle = params.startAngleRadians + (float) valueToProportionOfLength (getValue()) * (params.endAngleRadians - params.startAngleRadians);
        skin::drawKnob (g, skinBody, getLocalBounds().toFloat(), angle, skinPointer, isEnabled() ? 1.0f : 0.5f);
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
    const auto r = getLocalBounds().toFloat();
    g.setColour (look::isOnLight (*this) ? theme::stripInk : juce::Colour (0xffe9e6dc));
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
