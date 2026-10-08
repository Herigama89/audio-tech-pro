#include "StripParts.h"
#include <BinaryData.h>

namespace riff::strip
{

const juce::Image& topArt()
{
    static const auto image = juce::ImageCache::getFromMemory (BinaryData::StripTop_png, BinaryData::StripTop_pngSize);
    return image;
}

const juce::Image& plateArt()
{
    static const auto image = juce::ImageCache::getFromMemory (BinaryData::StripPlate_png, BinaryData::StripPlate_pngSize);
    return image;
}

const juce::Image& faderCapArt()
{
    static const auto image = juce::ImageCache::getFromMemory (BinaryData::FaderCap_png, BinaryData::FaderCap_pngSize);
    return image;
}

namespace
{
    juce::Point<float> polar (juce::Point<float> c, float radius, float angle)
    {
        return c + juce::Point<float> (radius * std::sin (angle), -radius * std::cos (angle));
    }
}

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
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto cap = bounds.getWidth() * 0.5f * 0.78f;
    const auto params = getRotaryParameters();
    const auto angle = params.startAngleRadians
                     + (float) valueToProportionOfLength (getValue()) * (params.endAngleRadians - params.startAngleRadians);

    const auto from = polar (centre, cap * 0.05f, angle);
    const auto to = polar (centre, cap * 0.92f, angle);
    const auto width = juce::jmax (1.5f, cap * 0.17f);

    g.setColour (juce::Colours::black.withAlpha (pointer.getPerceivedBrightness() > 0.5f ? 0.45f : 0.2f));
    g.drawLine (juce::Line<float> (from.translated (0.6f, 0.8f), to.translated (0.6f, 0.8f)), width + 0.5f);
    g.setColour (pointer.withAlpha (isEnabled() ? 1.0f : 0.6f));
    g.drawLine (juce::Line<float> (from, to), width);
}

//==============================================================================
LampKey::LampKey (const juce::String& name, juce::Colour lampColour) : juce::Button (name), lamp (lampColour)
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
}

void LampKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    // The key itself is in the artwork; light it, or dim it.
    const auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.18f);
    if (getToggleState())
    {
        g.setColour (lamp.withAlpha (0.35f));
        g.fillRoundedRectangle (r.expanded (r.getWidth() * 0.35f), r.getWidth() * 0.3f);
        g.setGradientFill (juce::ColourGradient (lamp.brighter (0.9f), r.getCentreX(), r.getY(), lamp, r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 1.5f);
    }
    else
    {
        g.setColour (juce::Colour (0xff3a0f0c).withAlpha (0.65f));
        g.fillRoundedRectangle (r, 1.5f);
    }

    if (over || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.25f : 0.12f));
        g.fillRoundedRectangle (r, 1.5f);
    }
}

//==============================================================================
CreamKey::CreamKey (const juce::String& letter, juce::Colour litColour) : juce::Button (letter), lit (litColour)
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
}

void CreamKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto face = getLocalBounds().toFloat().reduced (getWidth() * 0.09f, getHeight() * 0.17f);

    if (getToggleState())
    {
        // A lit key: its colour glows through the cream.
        g.setGradientFill (juce::ColourGradient (lit.brighter (0.5f), face.getCentreX(), face.getY(),
                                                 lit, face.getCentreX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 2.0f);
        g.setColour (lit.withAlpha (0.3f));
        g.drawRoundedRectangle (face.expanded (1.5f), 3.0f, 2.0f);
    }

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (face, 2.0f);
    }
    else if (over)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (face, 2.0f);
    }

    g.setColour (getToggleState() && lit.getPerceivedBrightness() < 0.55f ? juce::Colours::white : theme::ink);
    g.setFont (juce::FontOptions (face.getHeight() * 0.78f, juce::Font::bold));
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
    g.setColour (juce::Colour (0xffe9e6dc));
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
    const auto& cap = faderCapArt();
    return (float) getWidth() * 0.62f * (float) cap.getHeight() / (float) juce::jmax (1, cap.getWidth());
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
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawLine (slot.getRight() + 0.5f, slot.getY() + 2.0f, slot.getRight() + 0.5f, slot.getBottom() - 2.0f, 1.0f);

    // The cap, with its shadow
    const auto& art = faderCapArt();
    const auto capWidth = bounds.getWidth() * 0.62f;
    const auto cap = juce::Rectangle<float> (capWidth, capHeight()).withCentre ({ cx, yForValue (getValue()) });

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cap.translated (1.5f, 3.0f), 2.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f);   // images take the current colour's opacity
    g.drawImage (art, cap, juce::RectanglePlacement::stretchToFit);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (cap, 1.5f, 0.8f);
}

} // namespace riff::strip
