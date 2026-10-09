#include "StripParts.h"
#include "ConsoleLook.h"

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
    return juce::jlimit (20.0f, 34.0f, (float) getWidth() * 0.78f);
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

    // The cap
    const auto capWidth = bounds.getWidth() * 0.62f;
    look::drawFaderCap (g, juce::Rectangle<float> (capWidth, capHeight()).withCentre ({ cx, yForValue (getValue()) }), true);
}

} // namespace riff::strip
