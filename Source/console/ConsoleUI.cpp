#include "ConsoleUI.h"
#include <algorithm>

namespace atpconsole::ui
{

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

namespace
{
    constexpr int stripWidth = 104, stripGap = 3;
    const juce::Identifier faderProperty ("consoleFader"), bipolarProperty ("bipolar");

    juce::String dbText (double db)
    {
        if (db <= minusInfinityDb + 0.05)
            return juce::CharPointer_UTF8 ("-\xe2\x88\x9e");
        return (db > 0.05 ? "+" : "") + juce::String (db, 1);
    }

    juce::String outputPairName (const juce::StringArray& names, int first)
    {
        juce::String s = "Out " + juce::String (first + 1);
        if (first + 1 < names.size())
            s << "/" << (first + 2);
        if (names[first].isNotEmpty())
            s << "  " << names[first].upToFirstOccurrenceOf (" ", false, false);
        return s;
    }
}

//==============================================================================
ConsoleLook::ConsoleLook()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, orange);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
    setColour (juce::PopupMenu::highlightedTextColourId, background);
    setColour (juce::TextButton::buttonColourId, control);
    setColour (juce::TextButton::buttonOnColourId, orange);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, controlEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::Label::textColourId, juce::Colours::white);
    setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, controlEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, orange);
    setColour (juce::TextEditor::textColourId, juce::Colours::white);
    setColour (juce::TextEditor::highlightColourId, orange.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, orange);
    setColour (juce::ListBox::backgroundColourId, sidebar);
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, controlEdge);
    setColour (juce::Slider::rotarySliderFillColourId, orange);
    setColour (juce::Slider::thumbColourId, orange);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, edge);
    setColour (juce::TooltipWindow::textColourId, text);
}

void ConsoleLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                    float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.5f;
    const auto centre = bounds.getCentre();
    const float thickness = juce::jmax (2.5f, radius * 0.22f);
    const float arcRadius = radius - thickness * 0.5f;
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (colours::edge);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getProperties()[bipolarProperty];
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (slider.isEnabled() ? colour : colour.withAlpha (0.3f));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float knob = radius - thickness - 1.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a6a72), centre.x, centre.y - knob,
                                             juce::Colour (0xff36424a), centre.x, centre.y + knob, false));
    g.fillEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f);
    g.setColour (colours::edge);
    g.drawEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.0f, -knob + 1.5f, 2.0f, knob * 0.6f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

int ConsoleLook::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getProperties()[faderProperty])
        return 21;   // (half the cap's height: the cap travels the whole printed scale)
    return LookAndFeel_V4::getSliderThumbRadius (slider);
}

void ConsoleLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.getProperties()[faderProperty])
    {
        drawFader (g, juce::Rectangle<int> (x, y, width, height).toFloat(), sliderPos, slider);
        return;
    }

    if (style == juce::Slider::LinearBar)
    {
        // The interface's own preamp gain: a bar in a well, with its value written on it
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (colours::well);
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (colours::orange.withAlpha (slider.isEnabled() ? 0.55f : 0.2f));
        g.fillRoundedRectangle (area.withRight (juce::jlimit (area.getX(), area.getRight(), sliderPos)), 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (10.0f, true));
        g.drawText ("PRE " + juce::String (juce::roundToInt (slider.getValue())) + " dB", area.reduced (5.0f, 0.0f),
                    juce::Justification::centredLeft, false);
        return;
    }

    LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
}

void ConsoleLook::drawFader (juce::Graphics& g, juce::Rectangle<float> area, float thumbY, juce::Slider& slider)
{
    const float slotX = area.getRight() - 26.0f;      // (the slot's centre line)
    const float top = (float) slider.getPositionOfValue (slider.getMaximum());
    const float bottom = (float) slider.getPositionOfValue (slider.getMinimum());

    // The printed scale: dB marks on the left, ticks either side of the slot
    struct Mark { double db; const char* label; bool major; };
    static const Mark marks[] = { { 10, "+10", true }, { 5, "+5", true }, { 0, "0", true }, { -5, "5", true },
                                  { -10, "10", true }, { -20, "20", true }, { -30, "30", true }, { -40, "40", true },
                                  { -60, "60", false }, { minusInfinityDb, nullptr, true } };
    g.setFont (font (8.5f, false));
    for (const auto& m : marks)
    {
        const float my = (float) slider.getPositionOfValue (m.db);
        const bool unity = m.db == 0.0;
        g.setColour (unity ? juce::Colours::white : colours::faint);
        const float len = unity ? 10.0f : (m.major ? 7.0f : 4.0f);
        g.fillRect (slotX - 22.0f - len + 7.0f, my - (unity ? 0.75f : 0.5f), len, unity ? 1.5f : 1.0f);
        g.fillRect (slotX + 16.0f, my - (unity ? 0.75f : 0.5f), unity ? 8.0f : 5.0f, unity ? 1.5f : 1.0f);
        g.setColour (unity ? juce::Colours::white : colours::dim);
        g.setFont (font (8.5f, unity));
        const juce::String label = m.label != nullptr ? juce::String (m.label) : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
        g.drawText (label, juce::Rectangle<float> (area.getX(), my - 6.0f, slotX - 24.0f - area.getX(), 12.0f),
                    juce::Justification::centredRight, false);
    }

    // The slot cut into the panel
    const auto slot = juce::Rectangle<float> (slotX - 4.0f, top - 8.0f, 8.0f, bottom - top + 16.0f);
    g.setColour (juce::Colour (0xff06090b));
    g.fillRoundedRectangle (slot, 4.0f);
    g.setColour (colours::controlEdge.withAlpha (0.8f));
    g.drawRoundedRectangle (slot, 4.0f, 0.75f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (slot.reduced (3.0f, 4.0f), 1.0f);

    // The cap: black plastic, ridged for grip, with the strip's colour across its middle
    const auto cap = juce::Rectangle<float> (slotX - 14.0f, thumbY - 21.0f, 28.0f, 42.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (cap.translated (1.0f, 4.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), cap.getX(), cap.getY(),
                                             juce::Colour (0xff0e0e0e), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    juce::ColourGradient sides (juce::Colours::black.withAlpha (0.5f), cap.getX(), 0.0f,
                                juce::Colours::black.withAlpha (0.5f), cap.getRight(), 0.0f, false);
    sides.addColour (0.15, juce::Colours::transparentBlack);
    sides.addColour (0.85, juce::Colours::transparentBlack);
    g.setGradientFill (sides);
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (juce::Colour (0xff050505));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);
    g.setColour (juce::Colour (0xff8a8a8a).withAlpha (0.7f));
    g.fillRoundedRectangle (cap.getX() + 2.0f, cap.getY() + 1.0f, cap.getWidth() - 4.0f, 2.0f, 1.0f);
    for (float ry : { 7.0f, 11.0f, 15.0f, 27.0f, 31.0f, 35.0f })
    {
        g.setColour (juce::Colour (0xff5a5a5a));
        g.fillRect (cap.getX() + 3.0f, cap.getY() + ry, cap.getWidth() - 6.0f, 1.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (cap.getX() + 3.0f, cap.getY() + ry + 1.0f, cap.getWidth() - 6.0f, 1.0f);
    }
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillRect (cap.getX() + 1.5f, cap.getCentreY() - 1.5f, cap.getWidth() - 3.0f, 3.0f);
}

//==============================================================================
void ConsoleSlider::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
    {
        juce::Slider::mouseDown (e);
        return;
    }
    if (e.mods.isAltDown() && isDoubleClickReturnEnabled())
    {
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);   // (Option-click: back to the default)
        return;
    }
    dragging = true;
    proportion = valueToProportionOfLength (getValue());
    lastX = e.position.x;
    lastY = e.position.y;
    e.source.enableUnboundedMouseMovement (true, false);   // (hides the pointer, and lets it travel past the screen's edge)
    if (onDragStart != nullptr)
        onDragStart();
}

void ConsoleSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
    {
        juce::Slider::mouseDrag (e);
        return;
    }
    const auto style = getSliderStyle();
    const bool horizontal = style == LinearHorizontal || style == LinearBar;
    const float moved = horizontal ? e.position.x - lastX : lastY - e.position.y;   // (up / right is more)
    lastX = e.position.x;
    lastY = e.position.y;

    // A fader moves with the pointer (its whole travel); a knob takes 250 points for its whole turn
    const float travel = isRotary() ? 250.0f
                                    : juce::jmax (40.0f, (float) (horizontal ? getWidth() : getHeight())
                                                             - 2.0f * (float) getLookAndFeel().getSliderThumbRadius (*this));
    proportion = juce::jlimit (0.0, 1.0, proportion + (double) (moved / travel) * (e.mods.isShiftDown() ? 0.2 : 1.0));
    setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
}

void ConsoleSlider::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
    {
        juce::Slider::mouseUp (e);
        return;
    }
    dragging = false;
    e.source.enableUnboundedMouseMovement (false);

    // The pointer comes back on the control: on a fader's cap, or a knob's centre
    juce::Point<float> at = getLocalBounds().getCentre().toFloat();
    if (isVertical())
        at.y = getPositionOfValue (getValue());
    else if (isHorizontal())
        at.x = getPositionOfValue (getValue());
    e.source.setScreenPosition (localPointToGlobal (at));
    if (onDragEnd != nullptr)
        onDragEnd();
}

//==============================================================================
void SlotButton::set (const juce::String& pluginName, bool isLoaded, bool isBypassed)
{
    if (name == pluginName && loaded == isLoaded && bypassed == isBypassed)
        return;
    name = pluginName;
    loaded = isLoaded;
    bypassed = isBypassed;
    setTooltip (loaded ? name + (bypassed ? " (bypassed)" : "") + "\nClick: open  ·  Right-click: options"
                       : "Click to load a plugin, or drag one here from the browser");
    repaint();
}

void SlotButton::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    const bool pre = tag.isNotEmpty();
    const auto accent = colours::orange;

    g.setColour (pre && loaded ? juce::Colour (0xff3d3122) : colours::well);
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (dragOver ? accent : (pre && loaded ? accent : (pre ? colours::control : colours::edge)));
    g.drawRoundedRectangle (area, 3.0f, dragOver ? 1.5f : 1.0f);

    auto inner = area.reduced (5.0f, 2.0f);
    if (pre)
    {
        g.setColour (accent);
        g.setFont (font (8.5f, true));
        g.drawText (tag, inner.removeFromTop (inner.getHeight() * 0.45f), juce::Justification::centredLeft, false);
        g.setColour (loaded ? (bypassed ? colours::faint : juce::Colours::white) : colours::faint);
        g.setFont (font (11.0f, loaded));
        g.drawText (loaded ? name : juce::String ("- empty -"), inner, juce::Justification::centredLeft, true);
        return;
    }

    const auto led = inner.removeFromLeft (7.0f).withSizeKeepingCentre (6.0f, 6.0f);
    g.setColour (loaded ? (bypassed ? colours::control : colours::green) : colours::control);
    g.fillEllipse (led);
    inner.removeFromLeft (4.0f);
    g.setColour (loaded ? (bypassed ? colours::faint : colours::text) : colours::faint);
    g.setFont (font (10.5f, false));
    g.drawText (loaded ? name : juce::String ("+ add plugin"), inner, juce::Justification::centredLeft, true);
}

void SlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick != nullptr)
        onClick (e);
}

bool SlotButton::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("plugin:");
}

void SlotButton::itemDropped (const SourceDetails& d)
{
    dragOver = false;
    repaint();
    if (onDrop != nullptr)
        onDrop (d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue());
}

//==============================================================================
void MeterView::update (Meter& m)
{
    source = &m;
    auto toDb = [] (float gain) { return juce::Decibels::gainToDecibels (gain, -100.0f); };
    const float l = toDb (m.left.exchange (0.0f)), r = toDb (m.right.exchange (0.0f));
    levelL = juce::jmax (l, levelL - 1.5f);   // (falls about 45 dB a second)
    levelR = juce::jmax (r, levelR - 1.5f);
    if (l >= holdL) { holdL = l; holdTicksL = 45; } else if (--holdTicksL <= 0) holdL = levelL;
    if (r >= holdR) { holdR = r; holdTicksR = 45; } else if (--holdTicksR <= 0) holdR = levelR;
    clip = m.clip.load();
    repaint();
}

void MeterView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto light = area.removeFromTop (6.0f).reduced (1.0f, 0.0f);
    g.setColour (clip ? colours::red : colours::edge);
    g.fillRoundedRectangle (light, 1.5f);
    area.removeFromTop (3.0f);

    auto proportion = [] (float db) { return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f)); };
    const float barWidth = (area.getWidth() - 2.0f) * 0.5f;
    juce::ColourGradient gradient (colours::red, 0.0f, area.getY(), juce::Colour (0xff6fbf3a), 0.0f, area.getBottom(), false);
    gradient.addColour (1.0 - proportion (0.0f), colours::yellow);
    gradient.addColour (1.0 - proportion (-12.0f), colours::green);

    const float levels[] = { levelL, levelR }, holds[] = { holdL, holdR };
    for (int i = 0; i < 2; ++i)
    {
        const auto bar = juce::Rectangle<float> (area.getX() + (float) i * (barWidth + 2.0f), area.getY(), barWidth, area.getHeight());
        g.setColour (juce::Colour (0xff0d1114));
        g.fillRect (bar);
        g.setGradientFill (gradient);
        g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * proportion (levels[i])));
        if (holds[i] > -59.0f)
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * proportion (holds[i])).withHeight (1.5f));
    }
}

void MeterView::mouseDown (const juce::MouseEvent&)
{
    if (source != nullptr)
        source->clip.store (false);
    clip = false;
    repaint();
}

//==============================================================================
StripView::StripView (MainView& o, Channel& c, int index, int cue)
    : channel (c), stripIndex (index), cueIndex (cue), owner (o)
{
    const bool isInput = c.kind == Kind::input;

    name.setFont (font (12.0f, true));
    name.setEditable (false, true, false);
    name.setJustificationType (juce::Justification::centredLeft);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { channel.name = name.getText(); };
    addAndMakeVisible (name);

    // The interface's own preamp, when its control service is running (PreSonus Universal Control)
    hwGain.setColour (juce::Slider::trackColourId, colours::orange);
    hwGain.setTooltip ("The interface's own mic preamp gain");
    hwGain.onValueChange = [this] { owner.engine.hardware.setGain (channel.input + 1, (float) hwGain.getValue()); };
    phantom.setClickingTogglesState (true);
    phantom.setColour (juce::TextButton::buttonOnColourId, colours::red);
    phantom.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    phantom.setTooltip ("48 V phantom power");
    phantom.onClick = [this] { owner.engine.hardware.setPhantom (channel.input + 1, phantom.getToggleState()); };
    addChildComponent (hwGain);
    addChildComponent (phantom);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        if (i == 0 && isInput)       s.tag = "PRE  ·  INPUT STAGE";
        else if (i == 0 && c.kind == Kind::fx) s.tag = "FX";
        s.onClick = [this, i, &s] (const juce::MouseEvent& e) { owner.slotClicked (channel, i, s, e); };
        s.onDrop = [this, i] (int known) { owner.dropPlugin (channel, i, known); };
        addAndMakeVisible (s);
    }

    const char* sendNames[] = { "Cue A send (before the fader)", "Cue B send (before the fader)", "FX send (after the fader)" };
    const juce::Colour sendColours[] = { colours::dim, colours::dim, colours::green };
    for (int i = 0; i < numSends; ++i)
    {
        auto& k = sends[(size_t) i];
        setupKnob (k, sendColours[i], minusInfinityDb, 6.0, minusInfinityDb, false);
        k.setSkewFactorFromMidPoint (-12.0);
        k.setTooltip (sendNames[i]);
        k.onValueChange = [this, i] { channel.sendDb[(size_t) i] = (float) sends[(size_t) i].getValue(); };
        if (isInput || (c.kind == Kind::daw && i != sendFx)) addAndMakeVisible (k);
    }

    setupKnob (fxReturn, colours::green, minusInfinityDb, 6.0, -6.0, false);
    fxReturn.setSkewFactorFromMidPoint (-12.0);
    fxReturn.setTooltip ("How much of the FX bus (reverb) this headphone mix gets");
    fxReturn.onValueChange = [this] { channel.fxReturnDb = (float) fxReturn.getValue(); };
    if (c.kind == Kind::cue) addAndMakeVisible (fxReturn);

    setupKnob (trim, colours::orange, -24.0, 24.0, 0.0, true);
    trim.setTooltip ("Input trim (dB), before the PRE slot: drives the preamp plugin harder or softer");
    trim.onValueChange = [this] { channel.trimDb = (float) trim.getValue(); repaint(); };
    setupKnob (pan, colours::green, -1.0, 1.0, 0.0, true);
    pan.setTooltip ("Pan");
    pan.onValueChange = [this] { channel.pan = (float) pan.getValue(); repaint(); };
    if (isInput)
    {
        addAndMakeVisible (trim);
        addAndMakeVisible (pan);
    }

    fader.getProperties().set (faderProperty, true);
    fader.setNormalisableRange (juce::NormalisableRange<double> (minusInfinityDb, 10.0,
        [] (double, double, double p) { return faderValue (p); },
        [] (double, double, double v) { return faderProportion (v); }));
    fader.setValue (0.0, juce::dontSendNotification);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setSliderSnapsToMousePosition (false);
    fader.setColour (juce::Slider::thumbColourId, isInput || c.kind == Kind::fx || c.kind == Kind::daw ? c.colour : juce::Colours::white);
    fader.setTooltip ("Double-click: 0 dB");
    fader.onValueChange = [this] { channel.faderDb = (float) fader.getValue(); repaint(); };
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);

    for (auto* b : { &mute, &solo, &polarity })
        b->setClickingTogglesState (true);
    solo.setColour (juce::TextButton::buttonOnColourId, colours::yellow);
    polarity.setColour (juce::TextButton::buttonOnColourId, colours::green);
    mute.setTooltip ("Mute");
    solo.setTooltip ("Solo (in the Main mix)");
    polarity.setTooltip ("Polarity (phase) invert");
    mute.onClick = [this] { channel.mute = mute.getToggleState(); };
    solo.onClick = [this] { channel.solo = solo.getToggleState(); };
    polarity.onClick = [this] { channel.polarity = polarity.getToggleState(); };
    addAndMakeVisible (mute);
    if (isInput)
    {
        addAndMakeVisible (solo);
        addAndMakeVisible (polarity);
    }

    output.setTooltip (isInput ? "Direct out: the processed signal (before the fader) to an output - to record it"
                               : "Where this mix plays");
    output.onChange = [this] { channel.output = output.getSelectedId() - 2; repaint(); };
    if (c.kind != Kind::fx && c.kind != Kind::daw) addAndMakeVisible (output);

    refresh();
}

void StripView::setupKnob (juce::Slider& k, juce::Colour colour, double min, double max, double def, bool bipolar)
{
    k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.setRange (min, max, 0.1);
    k.setValue (def, juce::dontSendNotification);
    k.setDoubleClickReturnValue (true, def);
    k.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k.getProperties().set (bipolarProperty, bipolar);
    k.setPopupDisplayEnabled (true, true, nullptr);
}

void StripView::refresh()
{
    name.setText (channel.name, juce::dontSendNotification);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = channel.slots[(size_t) i];
        slots[(size_t) i].set (slot.plugin != nullptr ? slot.plugin->getName() : juce::String(), slot.plugin != nullptr, slot.bypass.load());
    }
    for (int i = 0; i < numSends; ++i)
        sends[(size_t) i].setValue (channel.sendDb[(size_t) i].load(), juce::dontSendNotification);
    fxReturn.setValue (channel.fxReturnDb.load(), juce::dontSendNotification);
    trim.setValue (channel.trimDb.load(), juce::dontSendNotification);
    pan.setValue (channel.pan.load(), juce::dontSendNotification);
    fader.setValue (channel.faderDb.load(), juce::dontSendNotification);
    fader.setColour (juce::Slider::thumbColourId, channel.kind == Kind::input || channel.kind == Kind::fx || channel.kind == Kind::daw ? channel.colour : juce::Colours::white);
    mute.setToggleState (channel.mute.load(), juce::dontSendNotification);
    solo.setToggleState (channel.solo.load(), juce::dontSendNotification);
    polarity.setToggleState (channel.polarity.load(), juce::dontSendNotification);

    // Outputs: every output for a direct out (a stereo strip uses it and the next), pairs for the mixes
    output.clear (juce::dontSendNotification);
    const auto outs = owner.engine.getOutputNames();
    if (channel.kind == Kind::input)
    {
        output.addItem ("Direct out: off", 1);
        for (int i = 0; i < outs.size(); ++i)
            output.addItem ("Direct " + juce::String (i + 1) + (channel.stereo && i + 1 < outs.size() ? "/" + juce::String (i + 2) : juce::String())
                                + "  " + outs[i], i + 2);
    }
    else
    {
        output.addItem ("No output", 1);
        for (int i = 0; i + 1 < outs.size(); i += 2)
            output.addItem (outputPairName (outs, i), i + 2);
    }
    output.setSelectedId (channel.output.load() + 2, juce::dontSendNotification);

    // The interface's preamp, if its maker's control service tells us about this input
    const auto pre = channel.kind == Kind::input && owner.engine.hardware.isConnected()
                         ? owner.engine.hardware.preamp (channel.input + 1) : riff::InterfaceControl::Preamp {};
    hasHardware = pre.present;
    hwGain.setVisible (hasHardware);
    phantom.setVisible (hasHardware);
    if (hasHardware)
    {
        hwGain.setRange (pre.minDb, juce::jmax (pre.minDb + 1.0f, pre.maxDb), 1.0);
        if (! hwGain.isMouseButtonDown())
            hwGain.setValue (pre.gainDb, juce::dontSendNotification);
        phantom.setToggleState (pre.phantom, juce::dontSendNotification);
    }
    repaint();
}

void StripView::updateMeters()
{
    meter.update (channel.meter);
}

juce::String StripView::numberText() const
{
    switch (channel.kind)
    {
        case Kind::input:
            return "IN " + juce::String (channel.input + 1) + (channel.stereo ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (channel.input + 2) : juce::String());
        case Kind::daw:  return "DAW";
        case Kind::fx:   return "FX BUS";
        case Kind::cue:  return "CUE " + juce::String::charToString ((juce::juce_wchar) ('A' + cueIndex));
        case Kind::main:
        default:         return "MAIN";
    }
}

juce::String StripView::sourceText() const
{
    switch (channel.kind)
    {
        case Kind::input:  return channel.stereo ? channel.sourceName + " (stereo)" : channel.sourceName;
        case Kind::daw:    return owner.engine.isMacLink() ? "From your DAW (DAW L/R)"
                                  : (owner.engine.isDawConnected() ? "DAW connected" : "Waiting for your DAW");
        case Kind::fx:     return "Returns to all mixes";
        case Kind::cue:    return "Headphone mix";
        case Kind::main:
        default:           return "Monitors";
    }
}

void StripView::paint (juce::Graphics& g)
{
    const bool bus = channel.kind != Kind::input;
    auto area = getLocalBounds().toFloat();
    g.setColour (bus ? colours::busStrip : colours::strip);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);

    // The colour tab
    g.setColour (channel.kind == Kind::input || channel.kind == Kind::fx || channel.kind == Kind::daw ? channel.colour : (channel.kind == Kind::main ? juce::Colours::white : colours::dim));
    g.fillRect (area.removeFromTop (5.0f).reduced (1.0f, 0.0f));

    g.setFont (font (9.5f, false));
    g.setColour (colours::dim);
    g.drawText (numberText(), juce::Rectangle<float> (7.0f, 7.0f, area.getWidth() - 14.0f, 12.0f), juce::Justification::centredLeft, false);
    g.drawText (sourceText(), juce::Rectangle<float> (7.0f, 37.0f, area.getWidth() - 14.0f, 12.0f), juce::Justification::centredLeft, true);

    // Knob labels
    g.setFont (font (8.5f, false));
    if (channel.kind == Kind::daw)
    {
        const char* labels[] = { "CUE A", "CUE B" };
        for (int i = 0; i < 2; ++i)
            g.drawText (labels[i], sends[(size_t) i].getBounds().translated (0, sends[(size_t) i].getHeight() - 2).withHeight (10),
                        juce::Justification::centred, false);
    }
    if (channel.kind == Kind::input)
    {
        const char* labels[] = { "CUE A", "CUE B", "FX" };
        for (int i = 0; i < numSends; ++i)
            g.drawText (labels[i], sends[(size_t) i].getBounds().translated (0, sends[(size_t) i].getHeight() - 2).withHeight (10),
                        juce::Justification::centred, false);
        g.drawText ("TRIM " + dbText (trim.getValue()), trim.getBounds().translated (0, trim.getHeight()).withHeight (10).expanded (6, 0),
                    juce::Justification::centred, false);
        const auto p = pan.getValue();
        g.drawText (std::abs (p) < 0.02 ? juce::String ("PAN C") : "PAN " + juce::String (p < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (p) * 100)),
                    pan.getBounds().translated (0, pan.getHeight()).withHeight (10).expanded (6, 0), juce::Justification::centred, false);
    }
    else if (channel.kind == Kind::cue)
    {
        g.drawText ("FX RETURN", fxReturn.getBounds().translated (0, fxReturn.getHeight() - 2).withHeight (10).expanded (10, 0),
                    juce::Justification::centred, false);
    }

    // The fader's panel: brushed metal, with the meter and the fader on it
    const auto panel = meter.getBounds().getUnion (fader.getBounds()).expanded (3, 3).toFloat();
    juce::ColourGradient metal (juce::Colour (0xff2a3338), panel.getX(), 0.0f, juce::Colour (0xff2a3338), panel.getRight(), 0.0f, false);
    metal.addColour (0.5, juce::Colour (0xff3a464c));
    g.setGradientFill (metal);
    g.fillRoundedRectangle (panel, 3.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (panel, 3.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (font (11.0f, true));
    g.drawText (dbText (fader.getValue()) + " dB", juce::Rectangle<int> (0, fader.getBottom() + 4, getWidth(), 14), juce::Justification::centred, false);
}

void StripView::resized()
{
    auto area = getLocalBounds().reduced (6, 0);
    area.removeFromTop (5 + 14);          // tab, number
    name.setBounds (area.removeFromTop (18).expanded (3, 0));
    area.removeFromTop (14);              // source
    area.removeFromTop (2);

    auto hw = area.removeFromTop (20);
    phantom.setBounds (hw.removeFromRight (30));
    hw.removeFromRight (3);
    hwGain.setBounds (hw);
    area.removeFromTop (4);

    slots[0].setBounds (area.removeFromTop (34));
    area.removeFromTop (3);
    for (int i = 1; i < numSlots; ++i)
    {
        slots[(size_t) i].setBounds (area.removeFromTop (21));
        area.removeFromTop (2);
    }
    area.removeFromTop (3);

    auto sendRow = area.removeFromTop (38);
    const int w = sendRow.getWidth() / numSends;
    for (int i = 0; i < numSends; ++i)
        sends[(size_t) i].setBounds (sendRow.removeFromLeft (w).removeFromTop (28).reduced (1, 0));
    fxReturn.setBounds (getLocalBounds().withY (sendRow.getY()).withHeight (28).withSizeKeepingCentre (30, 28));

    auto knobRow = area.removeFromTop (48);
    trim.setBounds (knobRow.removeFromLeft (knobRow.getWidth() / 2).removeFromTop (36).withSizeKeepingCentre (36, 36));
    pan.setBounds (knobRow.removeFromTop (36).withSizeKeepingCentre (36, 36));

    auto bottom = area.removeFromBottom (22);
    output.setBounds (bottom);
    area.removeFromBottom (4);
    auto buttons = area.removeFromBottom (24);
    const int bw = (buttons.getWidth() - 6) / 3;
    if (channel.kind == Kind::input)
    {
        mute.setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (3);
        solo.setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (3);
        polarity.setBounds (buttons);
    }
    else
    {
        mute.setBounds (buttons.withSizeKeepingCentre (bw + 10, 24));
    }
    area.removeFromBottom (22);           // (the dB read-out)

    area.reduce (3, 3);
    meter.setBounds (area.removeFromLeft (14));
    area.removeFromLeft (2);
    fader.setBounds (area);
}

void StripView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && e.y < 52)
        showStripMenu();
}

void StripView::showStripMenu()
{
    owner.stripMenuFor (*this);
}

//==============================================================================
PluginBrowser::PluginBrowser (Engine& e) : engine (e)
{
    search.setTextToShowWhenEmpty ("Search plugins...", colours::faint);
    search.setFont (font (12.0f));
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, colours::sidebar);
    addAndMakeVisible (list);

    scan.onClick = [this] { if (onScan != nullptr) onScan(); };
    addAndMakeVisible (scan);

    engine.knownPlugins.addChangeListener (this);
    rebuild();
}

PluginBrowser::~PluginBrowser()
{
    engine.knownPlugins.removeChangeListener (this);
}

void PluginBrowser::rebuild()
{
    types = engine.knownPlugins.getTypes();
    rows.clear();
    const auto filter = search.getText().trim();
    for (int i = 0; i < types.size(); ++i)
    {
        const auto& d = types.getReference (i);
        if (d.isInstrument)
            continue;
        if (filter.isNotEmpty() && ! d.name.containsIgnoreCase (filter) && ! d.manufacturerName.containsIgnoreCase (filter))
            continue;
        rows.add (i);
    }
    std::sort (rows.begin(), rows.end(), [this] (int a, int b)
    {
        return types.getReference (a).name.compareIgnoreCase (types.getReference (b).name) < 0;
    });
    list.updateContent();
    list.repaint();
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, rows.size()))
        return;
    const auto& d = types.getReference (rows[row]);
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1).toFloat();
    g.setColour (selected ? colours::control : colours::panel);
    g.fillRoundedRectangle (area, 3.0f);

    auto inner = area.reduced (8.0f, 0.0f);
    auto badge = inner.removeFromRight (34.0f).withSizeKeepingCentre (34.0f, 13.0f);
    g.setColour (colours::controlEdge);
    g.drawRoundedRectangle (badge, 2.0f, 1.0f);
    g.setColour (colours::dim);
    g.setFont (font (8.5f, true));
    g.drawText (d.pluginFormatName.toUpperCase().substring (0, 4), badge, juce::Justification::centred, false);

    g.setColour (colours::text);
    g.setFont (font (11.5f));
    g.drawText (d.name, inner.removeFromTop (inner.getHeight()), juce::Justification::centredLeft, true);
}

juce::var PluginBrowser::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty() || ! juce::isPositiveAndBelow (selected[0], rows.size()))
        return {};
    return "plugin:" + juce::String (rows[selected[0]]);
}

void PluginBrowser::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);
    g.setColour (colours::edge);
    g.fillRect (getLocalBounds().removeFromRight (1));
    g.setColour (colours::dim);
    g.setFont (font (11.0f, true));
    g.drawText ("PLUGIN BROWSER", 12, 10, getWidth() - 24, 16, juce::Justification::centredLeft, false);

    g.setFont (font (10.5f));
    g.setColour (colours::dim);
    const auto hint = rows.isEmpty() ? juce::String ("No plugins yet: scan for your VST3 plugins.")
                                     : juce::String ("Drag a plugin onto a PRE slot to load it as the input stage of that channel.");
    g.drawFittedText (hint, juce::Rectangle<int> (12, getHeight() - 78, getWidth() - 24, 38), juce::Justification::topLeft, 3);
}

void PluginBrowser::resized()
{
    auto area = getLocalBounds().reduced (10, 0);
    area.removeFromTop (32);
    search.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);
    scan.setBounds (area.removeFromBottom (30).withTrimmedBottom (6));
    area.removeFromBottom (48);
    list.setBounds (area);
}

//==============================================================================
PluginWindow::PluginWindow (juce::AudioProcessor& p, std::function<void()> onClose)
    : juce::DocumentWindow (p.getName(), colours::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      closed (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (p);
    setResizable (editor->isResizable(), false);
    setContentOwned (editor, true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();   // (the editor goes before its plugin)
}

void PluginWindow::closeButtonPressed()
{
    if (closed != nullptr)
        closed();
}

//==============================================================================
MainView::MainView (Engine& e, juce::ApplicationProperties& props)
    : engine (e), properties (props), browser (e)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    setLookAndFeel (&look);

    browser.onScan = [this] { showPluginScanner(); };
    addAndMakeVisible (browser);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    audioButton.onClick = [this] { showAudioSetup(); };
    pluginsButton.onClick = [this] { showPluginScanner(); };
    sessionButton.onClick = [this] { showSessionMenu(); };
    talkback.setColour (juce::TextButton::buttonColourId, colours::orange);
    talkback.setColour (juce::TextButton::textColourOffId, colours::background);
    talkback.setTooltip ("Hold to talk to the headphone mixes (choose the mic in Session > Talkback mic)");
    talkback.onStateChange = [this] { engine.talkback = talkback.isDown(); };
    for (auto* b : { &audioButton, &pluginsButton, &sessionButton, &talkback })
        addAndMakeVisible (b);

    engine.onLayoutChanged = [this] { rebuildStrips(); };
    engine.onPluginWillBeDeleted = [this] (juce::AudioProcessor* p) { windows.erase (p); };
    engine.hardware.onChange = [this] { refreshAll(); };
    engine.knownPlugins.addChangeListener (this);

    rebuildStrips();
    setSize (1440, 900);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    engine.knownPlugins.removeChangeListener (this);
    engine.onLayoutChanged = nullptr;
    engine.onPluginWillBeDeleted = nullptr;
    engine.hardware.onChange = nullptr;
    windows.clear();
    inputViews.clear();
    busViews.clear();
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // (the plugin list changed: kept for next time)
    if (auto xml = engine.knownPlugins.createXml())
    {
        properties.getUserSettings()->setValue ("knownPlugins", xml.get());
        properties.saveIfNeeded();
    }
}

void MainView::rebuildStrips()
{
    inputViews.clear();
    busViews.clear();
    for (int i = 0; i < engine.numStrips(); ++i)
        if (engine.strip (i).visible)
            stripHolder.addAndMakeVisible (inputViews.add (new StripView (*this, engine.strip (i), i, -1)));

    if (engine.hasDawBridge())
        addAndMakeVisible (busViews.add (new StripView (*this, engine.dawStrip(), -1, -1)));
    addAndMakeVisible (busViews.add (new StripView (*this, engine.bus (Kind::fx), -1, -1)));
    addAndMakeVisible (busViews.add (new StripView (*this, engine.cue (0), -1, 0)));
    addAndMakeVisible (busViews.add (new StripView (*this, engine.cue (1), -1, 1)));
    addAndMakeVisible (busViews.add (new StripView (*this, engine.bus (Kind::main), -1, -1)));
    resized();
    repaint();
}

void MainView::refreshAll()
{
    for (auto* v : inputViews) v->refresh();
    for (auto* v : busViews) v->refresh();
}

void MainView::timerCallback()
{
    for (auto* v : inputViews) v->updateMeters();
    for (auto* v : busViews) v->updateMeters();

    if (++slowTicks % (30 * 30) == 0)
        saveAutosave();   // (every 30 seconds too, so even a crash or a forced quit keeps your setup)

    if (slowTicks % 15 == 0)   // (twice a second: the read-outs)
    {
        rate = engine.getSampleRate();
        block = engine.getBlockSize();
        roundTrip = engine.getRoundTripMs();
        cpu = engine.deviceManager.getCpuUsage();
        deviceText = engine.getDeviceName();
        const int xruns = engine.getXRuns();
        const double pluginMs = rate > 0 ? engine.maxPluginLatencySamples() * 1000.0 / rate : 0.0;
        statusText = juce::String (engine.deviceManager.getCurrentAudioDevice() != nullptr ? "Engine running" : "No audio interface open")
                   + "     " + juce::String (engine.countPlugins()) + " plugins loaded"
                   + "     Plugin latency (worst strip): " + juce::String (pluginMs, 1) + " ms"
                   + "     Monitor path: Input > Trim > PRE > Inserts > Cue / Main"
                   + "     Hardware control: " + (engine.hardware.isConnected() ? "connected" : "not found")
                   + (engine.isMacLink() ? "     DAW device: Audio Tech Pro Console (with " + engine.getMacInterface() + ")"
                      : engine.hasDawBridge() ? juce::String ("     DAW (ASIO driver): ") + (engine.isDawConnected() ? "connected" : "not connected")
                                              : juce::String())
                   + "     Dropouts: " + (xruns >= 0 ? juce::String (xruns) : juce::String ("n/a"));
        for (auto* v : busViews)
            if (v->channel.kind == Kind::daw)
                v->repaint();
        repaint (0, 0, getWidth(), 58);
        repaint (0, getHeight() - 28, getWidth(), 28);
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Top bar
    auto bar = getLocalBounds().removeFromTop (58);
    g.setColour (colours::panel);
    g.fillRect (bar);
    g.setColour (colours::edge);
    g.fillRect (bar.removeFromBottom (1));

    // The logo: three faders
    const auto logo = juce::Rectangle<float> (16.0f, 15.0f, 28.0f, 28.0f);
    g.setColour (colours::edge);
    g.fillRoundedRectangle (logo, 6.0f);
    g.setColour (colours::orange);
    g.drawRoundedRectangle (logo.reduced (1.0f), 6.0f, 2.0f);
    g.fillRoundedRectangle (logo.getX() + 6.0f, logo.getY() + 7.0f, 3.0f, 13.0f, 1.5f);
    g.fillRoundedRectangle (logo.getX() + 17.0f, logo.getY() + 10.0f, 3.0f, 10.0f, 1.5f);
    g.setColour (colours::green);
    g.fillRoundedRectangle (logo.getX() + 11.5f, logo.getY() + 4.0f, 3.0f, 16.0f, 1.5f);

    g.setColour (juce::Colours::white);
    g.setFont (font (14.0f, true));
    g.drawText ("Audio Tech Pro Console", 54, 12, 260, 18, juce::Justification::centredLeft, false);
    g.setColour (colours::dim);
    g.setFont (font (11.0f));
    g.drawText (deviceText, 54, 30, 300, 16, juce::Justification::centredLeft, true);

    // Read-outs
    struct Readout { const char* label; juce::String value; juce::Colour colour; };
    const Readout readouts[] = { { "RATE", rate > 0 ? juce::String (rate / 1000.0, rate == 44100.0 ? 1 : 0) + " kHz" : "-", colours::green },
                                 { "BUFFER", juce::String (block) + " smp", colours::green },
                                 { "ROUND TRIP", juce::String (roundTrip, 1) + " ms", colours::orange },
                                 { "CPU", juce::String (juce::roundToInt (cpu * 100.0)) + "%", cpu > 0.8 ? colours::red : colours::green } };
    int x = 370;
    for (const auto& r : readouts)
    {
        const auto box = juce::Rectangle<int> (x, 10, 86, 38);
        g.setColour (colours::edge);
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (colours::dim);
        g.setFont (font (9.5f));
        g.drawText (r.label, box.reduced (9, 4).removeFromTop (12), juce::Justification::centredLeft, false);
        g.setColour (r.colour);
        g.setFont (font (13.0f, true));
        g.drawText (r.value, box.reduced (9, 4).removeFromBottom (16), juce::Justification::centredLeft, false);
        x += 92;
    }
    // (the CPU bar)
    const auto cpuBar = juce::Rectangle<float> ((float) x - 92.0f + 50.0f, 18.0f, 28.0f, 5.0f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (cpuBar, 2.5f);
    g.setColour (cpu > 0.8 ? colours::red : colours::green);
    g.fillRoundedRectangle (cpuBar.withWidth (cpuBar.getWidth() * (float) juce::jlimit (0.0, 1.0, cpu)), 2.5f);

    // Status bar
    auto status = getLocalBounds().removeFromBottom (28);
    g.setColour (colours::panel);
    g.fillRect (status);
    g.setColour (colours::edge);
    g.fillRect (status.removeFromTop (1));
    g.setColour (engine.deviceManager.getCurrentAudioDevice() != nullptr ? colours::green : colours::red);
    g.fillEllipse (16.0f, (float) status.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (colours::dim);
    g.setFont (font (11.0f));
    g.drawText (statusText, status.withTrimmedLeft (30).withTrimmedRight (16), juce::Justification::centredLeft, true);
}

void MainView::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (58).reduced (12, 13);
    talkback.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (6);
    sessionButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    pluginsButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    audioButton.setBounds (bar.removeFromRight (100));

    area.removeFromBottom (28);
    browser.setBounds (area.removeFromLeft (230));

    auto buses = area.removeFromRight (busViews.size() * (stripWidth + stripGap) + 10).reduced (5, 10);
    for (auto* v : busViews)
    {
        v->setBounds (buses.removeFromLeft (stripWidth));
        buses.removeFromLeft (stripGap);
    }

    viewport.setBounds (area);
    layoutStrips();
}

void MainView::layoutStrips()
{
    const int h = juce::jmax (620, viewport.getHeight() - viewport.getScrollBarThickness());
    stripHolder.setSize (juce::jmax (viewport.getWidth(), inputViews.size() * (stripWidth + stripGap) + 20), h);
    int x = 10;
    for (auto* v : inputViews)
    {
        v->setBounds (x, 10, stripWidth, h - 20);
        x += stripWidth + stripGap;
    }
}

//==============================================================================
juce::PopupMenu MainView::pluginMenu (std::function<void (const juce::PluginDescription&)> chosen)
{
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> byMaker;
    auto types = engine.knownPlugins.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (const auto& d : types)
    {
        if (d.isInstrument)
            continue;
        byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")]
            .addItem (d.name + "  (" + d.pluginFormatName + ")", [chosen, d] { chosen (d); });
    }
    for (auto& [maker, sub] : byMaker)
        menu.addSubMenu (maker, sub);
    if (byMaker.empty())
        menu.addItem ("No plugins found: scan for plugins...", [this] { showPluginScanner(); });
    return menu;
}

void MainView::slotClicked (Channel& c, int slot, SlotButton& button, const juce::MouseEvent& e)
{
    auto* plugin = c.slots[(size_t) slot].plugin.get();
    if (plugin != nullptr && ! e.mods.isPopupMenu())
    {
        openEditor (plugin);
        return;
    }

    auto load = [this, &c, slot] (const juce::PluginDescription& d) { loadPlugin (c, slot, d); };
    juce::PopupMenu menu;
    if (plugin == nullptr)
    {
        menu = pluginMenu (load);
    }
    else
    {
        menu.addSectionHeader (plugin->getName());
        menu.addItem ("Open editor", [this, plugin] { openEditor (plugin); });
        menu.addItem ("Bypass", true, c.slots[(size_t) slot].bypass.load(), [this, &c, slot]
        {
            c.slots[(size_t) slot].bypass = ! c.slots[(size_t) slot].bypass.load();
            refreshAll();
        });
        menu.addSubMenu ("Replace with", pluginMenu (load));
        menu.addSeparator();
        menu.addItem ("Remove", [this, &c, slot] { engine.setPlugin (c, slot, nullptr); refreshAll(); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button));
}

void MainView::dropPlugin (Channel& c, int slot, int knownIndex)
{
    const auto types = engine.knownPlugins.getTypes();
    if (juce::isPositiveAndBelow (knownIndex, types.size()))
        loadPlugin (c, slot, types.getReference (knownIndex));
}

void MainView::loadPlugin (Channel& c, int slot, const juce::PluginDescription& d)
{
    juce::String error;
    auto plugin = engine.createPlugin (d, error);
    if (plugin == nullptr)
    {
        warn ("Couldn't load " + d.name, error.isNotEmpty() ? error : juce::String ("The plugin didn't open."));
        return;
    }
    auto* raw = plugin.get();
    engine.setPlugin (c, slot, std::move (plugin));
    refreshAll();
    openEditor (raw);
}

void MainView::openEditor (juce::AudioPluginInstance* p)
{
    if (p == nullptr)
        return;
    if (auto it = windows.find (p); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }
    windows[p] = std::make_unique<PluginWindow> (*p, [safe = juce::Component::SafePointer<MainView> (this), p]
    {
        juce::MessageManager::callAsync ([safe, p] { if (safe != nullptr) safe->windows.erase (p); });
    });
}

void MainView::stripMenuFor (StripView& view)
{
    auto& c = view.channel;
    juce::PopupMenu menu;
    menu.addSectionHeader (c.name);
    menu.addItem ("Rename...", [this, &c]
    {
        auto* w = new juce::AlertWindow ("Rename", "", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", c.name);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, &c] (int result)
        {
            if (result == 1)
            {
                c.name = w->getTextEditorContents ("name");
                refreshAll();
            }
        }), true);
    });

    if (c.kind == Kind::input)
    {
        const int index = view.stripIndex;
        const bool canPair = index + 1 < engine.numStrips();
        menu.addItem ("Stereo (link with the next input)", canPair, c.stereo, [this, index, &c] { engine.setStereo (index, ! c.stereo); });
        menu.addItem ("Use as the talkback mic", true, engine.talkbackInput.load() == c.input, [this, &c] { engine.talkbackInput = c.input; });

        juce::PopupMenu colourMenu;
        const std::pair<const char*, juce::uint32> choices[] = { { "Orange", 0xfff5a742 }, { "Pink", 0xffe77fb3 }, { "Blue", 0xff5ab4e8 },
                                                                 { "Purple", 0xffa88bf0 }, { "Green", 0xff9bd34a }, { "Yellow", 0xfff2d14b },
                                                                 { "Grey", 0xff8c9aa1 } };
        for (const auto& [label, argb] : choices)
            colourMenu.addItem (label, true, c.colour == juce::Colour (argb), [this, &c, value = argb] { c.colour = juce::Colour (value); refreshAll(); });
        menu.addSubMenu ("Colour", colourMenu);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&view));
}

//==============================================================================
void MainView::showAudioSetup()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 256, 0, 256, false, false, false, false);
    selector->setSize (560, 480);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio Setup";
    options.dialogBackgroundColour = colours::panel;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainView::showPluginScanner()
{
    auto deadMansPedal = properties.getUserSettings()->getFile().getSiblingFile ("Console plugin scan.crash");
    auto* list = new juce::PluginListComponent (engine.formats, engine.knownPlugins, deadMansPedal,
                                                properties.getUserSettings(), true);
    list->setSize (760, 520);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (list);
    options.dialogTitle = "Plugins (Options > Scan for new or updated plugins)";
    options.dialogBackgroundColour = colours::panel;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainView::showSessionMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("New session", [this] { newSession(); });
    menu.addItem ("Open session...", [this] { openSession(); });
    menu.addItem ("Save session", [this] { saveSession (false); });
    menu.addItem ("Save session as...", [this] { saveSession (true); });
    menu.addSeparator();

    juce::PopupMenu tb;
    tb.addItem ("None", true, engine.talkbackInput.load() < 0, [this] { engine.talkbackInput = -1; });
    for (int i = 0; i < engine.numStrips(); ++i)
    {
        const int input = engine.strip (i).input;
        tb.addItem ("IN " + juce::String (input + 1) + "  " + engine.strip (i).sourceName, true,
                    engine.talkbackInput.load() == input, [this, input] { engine.talkbackInput = input; });
    }
    menu.addSubMenu ("Talkback mic", tb);
    menu.addSeparator();
    menu.addItem ("Audio setup...", [this] { showAudioSetup(); });
    menu.addItem ("Plugins...", [this] { showPluginScanner(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sessionButton));
}

void MainView::newSession()
{
    windows.clear();
    engine.resetToDefaults();
    sessionFile = juce::File();
    refreshAll();
}

void MainView::openSession()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a console session",
                                                   sessionFile.existsAsFile() ? sessionFile : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.atpconsole");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (const auto file = fc.getResult(); file.existsAsFile())
            loadSessionFile (file);
    });
}

void MainView::loadSessionFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("ATPCONSOLE"))
    {
        warn ("Couldn't open the session", file.getFileName() + " isn't a console session.");
        return;
    }
    windows.clear();
    juce::StringArray problems;
    engine.loadSession (*xml, problems);
    sessionFile = file;
    refreshAll();
    if (! problems.isEmpty())
        warn ("Some plugins didn't load", problems.joinIntoString ("\n"));
}

void MainView::saveSession (bool saveAs)
{
    auto write = [this] (const juce::File& file)
    {
        if (auto xml = engine.saveSession(); xml != nullptr && xml->writeTo (file))
            sessionFile = file;
        else
            warn ("Couldn't save the session", file.getFullPathName());
    };
    if (! saveAs && sessionFile != juce::File())
    {
        write (sessionFile);
        return;
    }
    chooser = std::make_unique<juce::FileChooser> ("Save the console session",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Console Session.atpconsole"),
                                                   "*.atpconsole");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write] (const juce::FileChooser& fc)
    {
        if (auto file = fc.getResult(); file != juce::File())
            write (file.withFileExtension ("atpconsole"));
    });
}

juce::File MainView::autosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Audio Tech Pro Console").getChildFile ("Last Session.atpconsole");
}

void MainView::saveAutosave()
{
    const auto file = autosaveFile();
    file.getParentDirectory().createDirectory();
    if (auto xml = engine.saveSession())
        xml->writeTo (file);
}

void MainView::warn (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK"),
                                  [] (int) {});
}

} // namespace atpconsole::ui
