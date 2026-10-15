#include "BusCompConsolePanel.h"
#include "Skin.h"
#include "Theme.h"
#include "ConsoleLook.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    // Where things are on the faceplate (drawn in a 636 x 1484 space, then scaled).
    struct KnobSpot { console::Control control; float x, y, radius; bool blue; };

    constexpr KnobSpot knobSpots[] = {
        { console::Control::threshold, 154.0f,  575.0f, 56.0f, true  },
        { console::Control::mix,       477.0f,  574.0f, 56.0f, false },
        { console::Control::attack,    155.0f,  784.0f, 56.0f, false },
        { console::Control::release,   477.0f,  785.0f, 56.0f, false },
        { console::Control::filter,    157.0f, 1003.0f, 56.0f, true  },
        { console::Control::ratio,     477.0f, 1000.0f, 56.0f, false },
        { console::Control::makeup,    473.0f, 1262.0f, 76.0f, false },
    };

    const juce::Rectangle<float> meterFace   { 96.0f, 64.0f, 445.0f, 196.0f };
    const juce::Point<float>     needlePivot { 315.0f, 326.0f };
    constexpr float              scaleRadius = 182.0f;
    constexpr float              sweepDegrees = 46.0f;
    const juce::Rectangle<float> inKey       { 91.0f, 1199.0f, 133.0f, 135.0f };
    const juce::Point<float>     inLamp      { 314.0f, 1266.0f };
    constexpr float              ledColumns[] = { 289.0f, 339.0f };
    constexpr float              ledTop = 627.0f, ledStep = 39.4f, ledRadius = 11.0f;
    constexpr int                numLeds = 9;
    constexpr float              reductionSteps[numLeds] = { 1, 2, 3, 4, 6, 8, 10, 14, 20 };          // bottom to top
    constexpr float              outputSteps[numLeds]    = { -30, -24, -18, -12, -9, -6, -3, -1, 0 };

    // Light lettering on the dark faceplate
    const juce::Colour lightInk { 0xffe6e8ec };
    const juce::Colour darkInk { 0xff1d1e21 };

    float angleFor (float reductionDb)
    {
        // 0 dB of reduction rests on the right; 20 dB is hard left.
        const auto pos = 1.0f - std::log10 (1.0f + juce::jlimit (0.0f, 20.0f, reductionDb)) / std::log10 (21.0f);
        return juce::degreesToRadians (-sweepDegrees + 2.0f * sweepDegrees * pos);
    }

    juce::Point<float> polar (juce::Point<float> c, float radius, float angle)
    {
        return c + juce::Point<float> (radius * std::sin (angle), -radius * std::cos (angle));
    }

    juce::Identifier mapProperty (console::Control c) { return juce::Identifier ("map" + juce::String ((int) c)); }

    /** "12.0 dB" -> "12"; keeps the scale numbers short like engraving. */
    juce::String shortText (const juce::String& text)
    {
        auto t = text.trim().upToFirstOccurrenceOf (" ", false, false);
        if (t.containsChar ('.') && t.length() > 4)
            t = juce::String (t.getFloatValue(), 1);
        return t.length() > 6 ? t.substring (0, 6) : t;
    }
}

//==============================================================================
/** A neon-ring encoder on the faceplate. */
class BusCompConsolePanel::Knob final : public juce::Slider
{
public:
    explicit Knob (bool largeKnob) : large (largeKnob)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        setRange (0.0, 1.0, 0.0);
        setVelocityBasedMode (false);
        setMouseDragSensitivity (220);
        setScrollWheelEnabled (false);
        setPopupDisplayEnabled (true, true, nullptr);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        // A neon-ring encoder, like the strips'
        const auto params = getRotaryParameters();
        look::drawNeonKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()),
                            params.startAngleRadians, params.endAngleRadians, large ? theme::knobCream : theme::neon, false, isEnabled());
    }

private:
    bool large;
};

//==============================================================================
BusCompConsolePanel::BusCompConsolePanel (juce::UndoManager& um, AudioEngine& e, EffectRack& fx)
    : undoManager (um), engine (e), effects (fx)
{

    for (size_t i = 0; i < numKnobs; ++i)
    {
        const auto spot = knobSpots[i];
        auto knob = std::make_unique<Knob> (spot.radius > 60.0f);
        auto* k = knob.get();

        k->onDragStart = [this, spot] { if (auto* p = parameterFor (spot.control)) p->beginChangeGesture(); };
        k->onDragEnd = [this, spot] { if (auto* p = parameterFor (spot.control)) p->endChangeGesture(); };
        k->onValueChange = [this, spot, k]
        {
            if (auto* p = parameterFor (spot.control))
                p->setValueNotifyingHost ((float) k->getValue());
        };
        k->textFromValueFunction = [this, spot] (double v)
        {
            if (auto* p = parameterFor (spot.control))
                return (p->getName (32) + "  " + p->getText ((float) v, 32) + " " + p->getLabel()).trim();
            return console::controlName (spot.control);
        };
        k->addMouseListener (this, false);

        addAndMakeVisible (*k);
        knobs[i] = std::move (knob);
    }

    setOpaque (true);
}

BusCompConsolePanel::~BusCompConsolePanel() = default;

void BusCompConsolePanel::setProject (juce::ValueTree newProject)
{
    project = newProject;
    refresh();
}

void BusCompConsolePanel::setCandidates (const juce::Array<juce::PluginDescription>& found)
{
    candidates = found;
    refresh();
}

bool BusCompConsolePanel::isAvailable() const
{
    // Only while a compressor plugin is chosen: with Built-in, the mixer shows the built-in compressor's own knobs.
    return showFaceForTesting || model::busCompPlugin (project).isValid();
}

int BusCompConsolePanel::widthForHeight (int height) const
{
    return juce::roundToInt ((float) height * (float) imageWidth / (float) imageHeight);
}

juce::AudioPluginInstance* BusCompConsolePanel::plugin() const
{
    const auto insert = model::busCompPlugin (project);
    return insert.isValid() ? effects.getPlugin (insert[ids::id].toString()) : nullptr;
}

int BusCompConsolePanel::parameterIndexFor (Control c) const
{
    auto* p = plugin();
    if (p == nullptr)
        return -1;

    const auto& params = p->getParameters();
    const auto insert = model::busCompPlugin (project);

    // Chosen by hand (right-click a knob), or matched by name.
    if (insert.hasProperty (mapProperty (c)))
    {
        const int index = insert[mapProperty (c)];
        return juce::isPositiveAndBelow (index, params.size()) ? index : -1;
    }

    juce::StringArray names;
    for (auto* param : params)
        names.add (param->getName (64));

    return console::matchParameter (c, names);
}

juce::AudioProcessorParameter* BusCompConsolePanel::parameterFor (Control c) const
{
    const auto index = parameterIndexFor (c);
    auto* p = plugin();
    return (p != nullptr && index >= 0) ? p->getParameters()[index] : nullptr;
}

void BusCompConsolePanel::assign (Control c, int parameterIndex)
{
    auto insert = model::busCompPlugin (project);
    if (! insert.isValid())
        return;

    undoManager.beginNewTransaction ("Assign knob");
    insert.setProperty (mapProperty (c), parameterIndex, &undoManager);
    refresh();
}

bool BusCompConsolePanel::isIn() const
{
    if (previewing)
        return previewIn;

    const auto insert = model::busCompPlugin (project);
    if (! insert.isValid() || (bool) insert[ids::bypass])
        return false;

    if (auto* p = parameterFor (Control::in))
    {
        const bool on = p->getValue() >= 0.5f;
        return console::isBypassParameter (p->getName (64)) ? ! on : on;
    }

    return plugin() != nullptr;
}

void BusCompConsolePanel::toggleIn()
{
    auto insert = model::busCompPlugin (project);

    if (! insert.isValid())
    {
        // First press: load the plugin onto the master.
        if (! candidates.isEmpty() && onLoadPlugin)
            onLoadPlugin (candidates.getFirst());
        return;
    }

    // The plugin's own IN switch if it has one; otherwise take it out of the signal path.
    if (auto* p = parameterFor (Control::in); p != nullptr && ! (bool) insert[ids::bypass])
    {
        const auto target = p->getValue() >= 0.5f ? 0.0f : 1.0f;
        p->beginChangeGesture();
        p->setValueNotifyingHost (target);
        p->endChangeGesture();
    }
    else
    {
        undoManager.beginNewTransaction ("Master Bus Compressor in");
        insert.setProperty (ids::bypass, ! (bool) insert[ids::bypass], &undoManager);
    }

    repaint();
}

void BusCompConsolePanel::refresh()
{
    auto* p = plugin();

    for (size_t i = 0; i < numKnobs; ++i)
    {
        auto* param = parameterFor (knobSpots[i].control);
        knobs[i]->setEnabled (param != nullptr || previewing);
        if (param != nullptr)
        {
            knobs[i]->setDoubleClickReturnValue (true, param->getDefaultValue());
            knobs[i]->setValue (param->getValue(), juce::dontSendNotification);
            knobs[i]->setTooltip (param->getName (64) + "  (right-click to choose another parameter)");
        }
        else if (! previewing)
        {
            knobs[i]->setValue (0.5, juce::dontSendNotification);
            knobs[i]->setTooltip (p != nullptr ? "Not assigned: right-click to choose a parameter" : juce::String());
        }
    }

    repaint();
}

float BusCompConsolePanel::gainReduction()
{
    auto* p = plugin();
    if (p == nullptr)
        return 0.0f;

    // The plugin's own meter, when it publishes one.
    const auto& params = p->getParameters();
    juce::StringArray names;
    for (auto* param : params)
    {
        if (param->getCategory() == juce::AudioProcessorParameter::compressorLimiterGainReductionMeter)
            return std::abs (console::numberIn (param->getCurrentValueAsText()));
        names.add (param->getName (64));
    }

    if (const auto meter = console::findGainReductionMeter (names); meter >= 0)
        return std::abs (console::numberIn (params[meter]->getCurrentValueAsText()));

    // Otherwise, from the levels in and out, allowing for the make-up gain.
    const auto [in, out] = engine.takeBusPluginLevels();
    if (in < 1.0e-4f)
        return 0.0f;

    float makeup = 0.0f;
    if (auto* m = parameterFor (Control::makeup))
        makeup = console::numberIn (m->getCurrentValueAsText());

    return juce::jlimit (0.0f, 20.0f, juce::Decibels::gainToDecibels (in) - juce::Decibels::gainToDecibels (out, -100.0f) + makeup);
}

void BusCompConsolePanel::update()
{
    if (previewing)
        return;

    // Follow knobs turned in the plugin's own window.
    for (size_t i = 0; i < numKnobs; ++i)
        if (auto* param = parameterFor (knobSpots[i].control))
            if (! knobs[i]->isMouseButtonDown() && std::abs (knobs[i]->getValue() - param->getValue()) > 1.0e-4)
                knobs[i]->setValue (param->getValue(), juce::dontSendNotification);

    // Meters: a moving-coil needle, and the LED ladders.
    const auto target = isIn() ? gainReduction() : 0.0f;
    reductionDb = target;
    needleDb += (target - needleDb) * (target > needleDb ? 0.45f : 0.15f);

    auto dirty = fromImage (meterFace).getUnion (fromImage ({ 260.0f, 600.0f, 110.0f, 370.0f }))
                                      .getUnion (fromImage (juce::Rectangle<float> (60.0f, 60.0f).withCentre (inLamp)))
                                      .getUnion (fromImage (inKey));
    repaint (dirty);
}

void BusCompConsolePanel::setOutputLevel (float peak)
{
    if (! previewing)
        outputDb = juce::jmax (juce::Decibels::gainToDecibels (peak, -100.0f), outputDb - 1.5f);   // LEDs fall back gently
}

void BusCompConsolePanel::showPreview (const std::array<float, 7>& knobValues, float reduction, float outputLevel, bool in,
                                       const std::array<juce::StringArray, 7>& texts)
{
    previewing = true;
    previewTexts = texts;
    previewIn = in;
    for (size_t i = 0; i < numKnobs; ++i)
    {
        knobs[i]->setEnabled (true);
        knobs[i]->setValue (knobValues[i], juce::dontSendNotification);
    }
    reductionDb = needleDb = reduction;
    outputDb = juce::Decibels::gainToDecibels (outputLevel, -100.0f);
    repaint();
}

//==============================================================================
juce::Point<float> BusCompConsolePanel::toImage (juce::Point<float> p) const
{
    return (p - origin) / scale;
}

juce::Rectangle<int> BusCompConsolePanel::fromImage (juce::Rectangle<float> r) const
{
    return (r * scale + origin).getSmallestIntegerContainer();
}

void BusCompConsolePanel::resized()
{
    scale = juce::jmin ((float) getWidth() / (float) imageWidth, (float) getHeight() / (float) imageHeight);
    origin = { ((float) getWidth() - imageWidth * scale) * 0.5f, 0.0f };

    for (size_t i = 0; i < numKnobs; ++i)
    {
        const auto& s = knobSpots[i];
        knobs[i]->setBounds (fromImage (juce::Rectangle<float> (s.radius * 2.0f, s.radius * 2.0f).withCentre ({ s.x, s.y })));
    }
}

void BusCompConsolePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::consoleDark);
    const auto plateTop = theme::stripFace.brighter (0.05f), plateBottom = theme::stripFaceLow.darker (0.12f);
    const auto lightPlate = plateTop.interpolatedWith (plateBottom, 0.5f).getPerceivedBrightness() > 0.55f;
    const auto ink = lightPlate ? darkInk : lightInk;
    const auto engraved = ink.interpolatedWith (plateBottom, 0.35f);

    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (scale).translated (origin));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    // --- The faceplate, drawn: dark charcoal, a bezelled meter window, four screws
    {
        const juce::Rectangle<float> plate { 6.0f, 6.0f, 624.0f, 1472.0f };
        // The faceplate in the skin's own strip colours (Studio White's light, Xenon's glass, a REAPER theme's...)
        g.setGradientFill (juce::ColourGradient (plateTop, 0.0f, plate.getY(), plateBottom, 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 18.0f);
        if (theme::xenonSkin())
        {
            // Xenon: the picture's dark panel texture on the faceplate.
            juce::Graphics::ScopedSaveState s (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (plate, 18.0f);
            g.reduceClipRegion (rounded);
            const auto tex = skin::xenonPanel();
            const auto area = plate.getSmallestIntegerContainer();
            skin::drawCached (g, "consolePlate" + juce::String ((juce::int64) tex.getPixelData().get()), area, [tex, area] (juce::Graphics& ig)
            {
                for (float y = 0.0f; y < (float) area.getHeight(); y += (float) tex.getHeight() * 2.0f)
                    for (float x = 0.0f; x < (float) area.getWidth(); x += (float) tex.getWidth() * 2.0f)
                        ig.drawImage (tex, juce::Rectangle<float> (x, y, (float) tex.getWidth() * 2.0f, (float) tex.getHeight() * 2.0f));
            });
        }
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (plate.reduced (2.0f), 16.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (plate, 18.0f, 3.0f);

        for (const auto p : { juce::Point<float> (34.0f, 34.0f), juce::Point<float> (602.0f, 34.0f),
                              juce::Point<float> (34.0f, 1450.0f), juce::Point<float> (602.0f, 1450.0f) })
        {
            const auto screw = juce::Rectangle<float> (22.0f, 22.0f).withCentre (p);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6c70), screw.getX(), screw.getY(), juce::Colour (0xff2a2b2e), screw.getRight(), screw.getBottom(), false));
            g.fillEllipse (screw);
            g.setColour (juce::Colour (0xff141516));
            g.drawLine (screw.getX() + 4.0f, screw.getCentreY(), screw.getRight() - 4.0f, screw.getCentreY(), 3.0f);
        }

        // Meter window: bezel, then the face lit blue from below
        const auto bezel = meterFace.expanded (22.0f, 22.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c40), 0.0f, bezel.getY(), juce::Colour (0xff0c0d0e), 0.0f, bezel.getBottom(), false));
        g.fillRoundedRectangle (bezel, 14.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (bezel, 14.0f, 2.0f);
        g.setGradientFill (juce::ColourGradient (theme::lcd.darker (0.5f), 0.0f, meterFace.getY(), theme::lcd.brighter (0.25f), 0.0f, meterFace.getBottom(), false));
        g.fillRoundedRectangle (meterFace, 6.0f);
        g.setColour (theme::neon.withAlpha (0.18f));
        g.fillRoundedRectangle (meterFace.withTrimmedTop (meterFace.getHeight() * 0.6f), 6.0f);

        // LED ladders' housings
        for (const auto x : ledColumns)
        {
            const auto housing = juce::Rectangle<float> (x - 20.0f, ledTop - 26.0f, 40.0f, ledStep * (float) (numLeds - 1) + 52.0f);
            g.setColour (juce::Colour (0xff0c0d0e));
            g.fillRoundedRectangle (housing, 8.0f);
        }
    }

    const auto loaded = plugin() != nullptr || previewing;
    const auto in = isIn();

    // --- Title, where the mockup's brand name was ------------------------------
    g.setColour (ink);
    g.setFont (juce::FontOptions (40.0f));
    g.drawText ("Master Bus", juce::Rectangle<float> (60.0f, 386.0f, 516.0f, 44.0f), juce::Justification::centred, false);
    g.drawText ("Compressor", juce::Rectangle<float> (60.0f, 427.0f, 516.0f, 44.0f), juce::Justification::centred, false);

    // --- The control box, its title, and the line from Threshold to the meters
    {
        const juce::Rectangle<float> box { 45.0f, 498.0f, 545.0f, 636.0f };
        const juce::Rectangle<float> titleGap { 190.0f, 480.0f, 256.0f, 40.0f };

        juce::Path frame;
        frame.addRoundedRectangle (box, 22.0f);
        juce::Graphics::ScopedSaveState frameState (g);
        g.excludeClipRegion (titleGap.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.strokePath (frame, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (1.5f, 2.0f));
        g.setColour (juce::Colour (0xff3c3b37));
        g.strokePath (frame, juce::PathStrokeType (3.5f));
    }

    g.setColour (ink);
    g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
    g.drawText ("Gain Reduction", juce::Rectangle<float> (190.0f, 480.0f, 256.0f, 38.0f), juce::Justification::centred, false);

    {
        juce::Path link;
        link.startNewSubPath (226.0f, 569.0f);
        link.lineTo (314.0f, 569.0f);
        link.lineTo (314.0f, 606.0f);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.strokePath (link, juce::PathStrokeType (2.0f), juce::AffineTransform::translation (1.0f, 1.5f));
        g.setColour (juce::Colour (0xff6c6a63));
        g.strokePath (link, juce::PathStrokeType (2.5f));
    }

    // --- Knob scales and captions ---------------------------------------------
    for (size_t i = 0; i < numKnobs; ++i)
    {
        const auto& s = knobSpots[i];
        const juce::Point<float> c { s.x, s.y };
        const auto dotRadius = s.radius + 20.0f;


        auto* param = parameterFor (s.control);
        auto caption = param != nullptr ? param->getName (20) : console::controlName (s.control);
        juce::String lowText, highText;
        if (param != nullptr)
        {
            lowText = shortText (param->getText (0.0f, 16));
            highText = shortText (param->getText (1.0f, 16));
        }
        else if (previewing && previewTexts[i].size() == 3)
        {
            caption = previewTexts[i][0];
            lowText = previewTexts[i][1];
            highText = previewTexts[i][2];
        }

        // Caption under the knob, clear of its dots
        g.setColour (ink);
        g.setFont (juce::FontOptions (s.radius > 60.0f ? 32.0f : 30.0f));
        g.drawText (caption, juce::Rectangle<float> (s.x - 110.0f, s.y + s.radius + 34.0f, 220.0f, 34.0f),
                    juce::Justification::centred, true);

        // The two ends of the scale, beside the first and last dots
        if (lowText.isNotEmpty() || highText.isNotEmpty())
        {
            g.setColour (ink.withAlpha (0.85f));
            g.setFont (juce::FontOptions (21.0f));
            const auto low  = polar (c, dotRadius + 8.0f, juce::degreesToRadians (-135.0f)).translated (-14.0f, 18.0f);
            const auto high = polar (c, dotRadius + 8.0f, juce::degreesToRadians (135.0f)).translated (14.0f, 18.0f);
            g.drawText (lowText, juce::Rectangle<float> (72.0f, 24.0f).withCentre (low), juce::Justification::centred, false);
            g.drawText (highText, juce::Rectangle<float> (72.0f, 24.0f).withCentre (high), juce::Justification::centred, false);
        }
    }

    g.setFont (juce::FontOptions (30.0f));
    g.drawText ("Bypass/In", juce::Rectangle<float> (inKey.getCentreX() - 120.0f, inKey.getBottom() + 22.0f, 240.0f, 36.0f),
                juce::Justification::centred, false);

    // --- The meter -------------------------------------------------------------
    {
        const auto faceInk = theme::lcdText;   // the scale, lit on the meter's own face
        juce::Graphics::ScopedSaveState faceState (g);
        g.reduceClipRegion (meterFace.toNearestInt());

        // Heavy-compression zone, then the scale
        juce::Path zone;
        zone.addCentredArc (needlePivot.x, needlePivot.y, scaleRadius + 6.0f, scaleRadius + 6.0f, 0.0f, angleFor (20.0f), angleFor (10.0f), true);
        g.setColour (juce::Colour (0xffff5a4a));
        g.strokePath (zone, juce::PathStrokeType (9.0f));

        juce::Path arc;
        arc.addCentredArc (needlePivot.x, needlePivot.y, scaleRadius, scaleRadius, 0.0f, angleFor (20.0f), angleFor (0.0f), true);
        g.setColour (faceInk);
        g.strokePath (arc, juce::PathStrokeType (2.6f));

        g.setFont (juce::FontOptions (24.0f));
        for (const auto db : { 20.0f, 10.0f, 7.0f, 5.0f, 3.0f, 2.0f, 1.0f, 0.0f })
        {
            const auto a = angleFor (db);
            g.setColour (db >= 10.0f ? juce::Colour (0xffff6a5a) : faceInk);
            g.drawLine (juce::Line<float> (polar (needlePivot, scaleRadius - 2.0f, a), polar (needlePivot, scaleRadius + 16.0f, a)), 3.0f);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (40.0f, 26.0f).withCentre (polar (needlePivot, scaleRadius + 36.0f, a)),
                        juce::Justification::centred, false);
        }

        for (int minor = 0; minor <= 20; ++minor)
        {
            const auto a = angleFor ((float) minor);
            g.setColour (faceInk.withAlpha (0.8f));
            g.drawLine (juce::Line<float> (polar (needlePivot, scaleRadius, a), polar (needlePivot, scaleRadius + 8.0f, a)), 1.6f);
        }

        g.setColour (faceInk.withAlpha (0.9f));
        g.setFont (juce::FontOptions (54.0f));
        g.drawText ("VU", juce::Rectangle<float> (needlePivot.x - 70.0f, 168.0f, 140.0f, 60.0f), juce::Justification::centred, false);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("dB GAIN REDUCTION", juce::Rectangle<float> (needlePivot.x - 120.0f, 222.0f, 240.0f, 22.0f), juce::Justification::centred, false);

        if (! loaded)
        {
            g.setColour (faceInk.withAlpha (0.75f));
            g.setFont (juce::FontOptions (20.0f));
            g.drawText (candidates.isEmpty() ? juce::String ("Plugin not found") : "Press IN to load " + candidates.getFirst().name,
                        meterFace.withTrimmedTop (meterFace.getHeight() - 30.0f), juce::Justification::centred, true);
        }

        // Needle, with its shadow on the face
        const auto a = angleFor (needleDb);
        const auto root = polar (needlePivot, 70.0f, a), tip = polar (needlePivot, scaleRadius + 22.0f, a);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawLine (juce::Line<float> (root.translated (6.0f, 5.0f), tip.translated (6.0f, 5.0f)), 3.5f);
        g.setColour (juce::Colour (0xffff8a3c).withAlpha (0.35f));
        g.drawLine (juce::Line<float> (root, tip), 8.0f);
        g.setColour (juce::Colour (0xffffb070));
        g.drawLine (juce::Line<float> (root, tip), 3.0f);
    }

    // --- LED ladders: gain reduction (left) and output level (right) ----------
    auto led = [&g] (juce::Point<float> c, juce::Colour colour, bool on)
    {
        const auto r = juce::Rectangle<float> (ledRadius * 2.0f, ledRadius * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (r.expanded (2.5f).translated (0.5f, 1.0f));
        if (on)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (r.expanded (6.0f));
        }
        g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.7f) : colour.darker (2.2f), r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.3f,
                                                 on ? colour : colour.darker (3.0f), r.getRight(), r.getBottom(), true));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.15f));
        g.fillEllipse (r.reduced (r.getWidth() * 0.3f).translated (-r.getWidth() * 0.12f, -r.getHeight() * 0.14f));
    };

    for (int i = 0; i < numLeds; ++i)
    {
        const auto y = ledTop + ledStep * (float) (numLeds - 1 - i);
        // Blue LEDs: deep to ice blue, red at the top of the output
        const auto grColour = i >= 7 ? juce::Colour (0xffa8e6ff) : juce::Colour (0xff2f8cff);
        const auto outColour = i >= 8 ? juce::Colour (0xffff4a3a) : (i >= 6 ? juce::Colour (0xffa8e6ff) : juce::Colour (0xff2f8cff));
        led ({ ledColumns[0], y }, grColour, in && reductionDb >= reductionSteps[i]);
        led ({ ledColumns[1], y }, outColour, outputDb >= outputSteps[i]);

        g.setColour (ink.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (12.0f, 3.0f).withCentre ({ (ledColumns[0] + ledColumns[1]) * 0.5f, y }));
    }

    // --- IN key and its lamp ---------------------------------------------------
    look::drawKey (g, inKey.reduced (10.0f), look::Key::console, in, theme::neon, false, false);
    g.setColour (in ? juce::Colours::white : ink.withAlpha (0.85f));
    g.setFont (juce::FontOptions (38.0f, juce::Font::bold));
    g.drawText ("IN", inKey, juce::Justification::centred, false);

    const auto lamp = juce::Rectangle<float> (34.0f, 34.0f).withCentre (inLamp);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (lamp.expanded (4.0f), 5.0f);
    if (in)
    {
        g.setColour (theme::neon.withAlpha (0.35f));
        g.fillRoundedRectangle (lamp.expanded (12.0f), 12.0f);
    }
    g.setGradientFill (juce::ColourGradient (in ? juce::Colour (0xffbfe0ff) : juce::Colour (0xff1c2a3d), lamp.getCentreX(), lamp.getY(),
                                             in ? theme::neon : juce::Colour (0xff111a26), lamp.getCentreX(), lamp.getBottom(), false));
    g.fillRoundedRectangle (lamp, 4.0f);

    // --- Which plugin this face is running ------------------------------------
    g.setColour (engraved.withAlpha (0.8f));
    g.setFont (juce::FontOptions (21.0f));
    const auto insert = model::busCompPlugin (project);
    juce::String footer = previewing ? juce::String ("Console mode") : juce::String();
    if (insert.isValid())
    {
        juce::PluginDescription d;
        if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()); xml != nullptr && d.loadFromXml (*xml))
            footer = d.name + (plugin() == nullptr ? "  (loading...)" : "");
    }
    else if (! candidates.isEmpty())
    {
        footer = candidates.getFirst().name;
    }
    g.drawText (footer, juce::Rectangle<float> (20.0f, 1418.0f, 596.0f, 30.0f), juce::Justification::centred, true);
}

void BusCompConsolePanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (e.eventComponent == this && inKey.contains (toImage (e.position)))
        toggleIn();
}

void BusCompConsolePanel::showMenu()
{
    juce::PopupMenu menu;
    const auto insert = model::busCompPlugin (project);

    juce::PluginDescription current;
    if (insert.isValid())
        if (auto xml = juce::parseXML (insert[ids::pluginDescription].toString()))
            current.loadFromXml (*xml);

    menu.addItem ("Built-in compressor", true, ! insert.isValid(), [this] { if (onRemovePlugin) onRemovePlugin(); });
    menu.addItem ("Choose any plugin...", [this] { if (onChooseAny) onChooseAny (*this); });
    menu.addSeparator();
    if (insert.isValid())
        menu.addItem ("Open plugin window", [this, insert] { if (onOpenPluginWindow) onOpenPluginWindow (insert); });

    if (! candidates.isEmpty())
    {
        juce::PopupMenu choose;
        for (const auto& d : candidates)
            choose.addItem (d.name + "  (" + d.pluginFormatName + ")", true,
                            insert.isValid() && d.createIdentifierString() == current.createIdentifierString(),
                            [this, d] { if (onLoadPlugin) onLoadPlugin (d); });
        menu.addSubMenu ("Use plugin", choose);
    }

    if (auto* p = plugin())
    {
        juce::PopupMenu knobsMenu;
        const auto& params = p->getParameters();

        for (int c = 0; c < (int) Control::numControls; ++c)
        {
            const auto control = (Control) c;
            const auto chosen = parameterIndexFor (control);
            juce::PopupMenu list;
            list.addItem ("Nothing", true, chosen < 0, [this, control] { assign (control, -1); });
            for (int i = 0; i < params.size(); ++i)
                list.addItem (params[i]->getName (64), true, i == chosen, [this, control, i] { assign (control, i); });
            knobsMenu.addSubMenu (console::controlName (control), list);
        }

        knobsMenu.addSeparator();
        knobsMenu.addItem ("Match by name again", [this, target = insert]() mutable
        {
            undoManager.beginNewTransaction ("Reset knob assignments");
            for (int c = 0; c < (int) Control::numControls; ++c)
                target.removeProperty (mapProperty ((Control) c), &undoManager);
            refresh();
        });
        menu.addSubMenu ("Knob assignments", knobsMenu);
    }

    if (insert.isValid())
    {
        menu.addSeparator();
        menu.addItem ("Remove plugin (use the built-in compressor)", [this] { if (onRemovePlugin) onRemovePlugin(); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

} // namespace riff
