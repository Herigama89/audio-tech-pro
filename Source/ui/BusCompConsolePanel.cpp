#include "BusCompConsolePanel.h"
#include "Theme.h"
#include "../model/Project.h"
#include <BinaryData.h>

namespace riff
{

namespace
{
    // Where things are on the artwork (in its own pixels, 636 x 1484).
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

    const juce::Colour ink { 0xff2a2a28 };
    const juce::Colour engraved { 0xff3a3a37 };

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
/** A knob on the artwork: the body is in the picture; this draws the pointer. */
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
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto cap = bounds.getWidth() * 0.5f * 0.78f;
        const auto params = getRotaryParameters();
        const auto angle = params.startAngleRadians
                         + (float) valueToProportionOfLength (getValue()) * (params.endAngleRadians - params.startAngleRadians);

        const auto from = polar (centre, cap * 0.02f, angle);
        const auto to = polar (centre, cap * (large ? 0.86f : 0.9f), angle);
        const auto width = juce::jmax (1.6f, cap * (large ? 0.12f : 0.15f));

        // Engraved groove shadow, then the painted line
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawLine (juce::Line<float> (from.translated (0.7f, 0.9f), to.translated (0.7f, 0.9f)), width + 0.6f);
        g.setColour (juce::Colour (0xfff4f2ea).withAlpha (isEnabled() ? 1.0f : 0.75f));
        g.drawLine (juce::Line<float> (from, to), width);
    }

private:
    bool large;
};

//==============================================================================
BusCompConsolePanel::BusCompConsolePanel (juce::UndoManager& um, AudioEngine& e, EffectRack& fx)
    : undoManager (um), engine (e), effects (fx)
{
    artwork = juce::ImageCache::getFromMemory (BinaryData::MasterBusCompressorPanel_png, BinaryData::MasterBusCompressorPanel_pngSize);

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
    return ! candidates.isEmpty() || model::busCompPlugin (project).isValid();
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

void BusCompConsolePanel::showPreview (const std::array<float, 7>& knobValues, float reduction, float outputLevel, bool in)
{
    previewing = true;
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
    g.fillAll (juce::Colour (0xffc9c4b8));

    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (scale).translated (origin));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageAt (artwork, 0, 0);

    const auto loaded = plugin() != nullptr || previewing;
    const auto in = isIn();

    // --- Title, where the mockup's brand name was ------------------------------
    g.setColour (ink);
    g.setFont (juce::FontOptions (40.0f));
    g.drawText ("Master Bus", juce::Rectangle<float> (60.0f, 386.0f, 516.0f, 44.0f), juce::Justification::centred, false);
    g.drawText ("Compressor", juce::Rectangle<float> (60.0f, 427.0f, 516.0f, 44.0f), juce::Justification::centred, false);

    // --- Knob scales and captions ---------------------------------------------
    for (size_t i = 0; i < numKnobs; ++i)
    {
        const auto& s = knobSpots[i];
        const juce::Point<float> c { s.x, s.y };
        const auto dotRadius = s.radius + 20.0f;

        g.setColour (ink.withAlpha (0.85f));
        for (int d = 0; d <= 10; ++d)
        {
            const auto a = juce::degreesToRadians (-135.0f + 27.0f * (float) d);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (polar (c, dotRadius, a)));
        }

        auto* param = parameterFor (s.control);
        const auto caption = param != nullptr ? param->getName (20) : console::controlName (s.control);

        g.setColour (ink);
        g.setFont (juce::FontOptions (s.radius > 60.0f ? 34.0f : 32.0f));
        g.drawText (caption, juce::Rectangle<float> (s.x - 140.0f, s.y + s.radius + 22.0f, 280.0f, 40.0f),
                    juce::Justification::centred, false);

        if (param != nullptr)
        {
            g.setFont (juce::FontOptions (22.0f));
            const auto low  = polar (c, dotRadius + 26.0f, juce::degreesToRadians (-135.0f));
            const auto high = polar (c, dotRadius + 26.0f, juce::degreesToRadians (135.0f));
            const auto mid  = polar (c, dotRadius + 22.0f, 0.0f);
            g.drawText (shortText (param->getText (0.0f, 16)), juce::Rectangle<float> (70.0f, 24.0f).withCentre (low), juce::Justification::centred, false);
            g.drawText (shortText (param->getText (1.0f, 16)), juce::Rectangle<float> (70.0f, 24.0f).withCentre (high), juce::Justification::centred, false);
            g.drawText (shortText (param->getText (0.5f, 16)), juce::Rectangle<float> (70.0f, 24.0f).withCentre (mid.translated (0.0f, -2.0f)), juce::Justification::centred, false);
        }
    }

    g.setFont (juce::FontOptions (32.0f));
    g.drawText ("Bypass/In", juce::Rectangle<float> (inKey.getCentreX() - 120.0f, inKey.getBottom() + 22.0f, 240.0f, 40.0f),
                juce::Justification::centred, false);

    // --- The meter -------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState faceState (g);
        g.reduceClipRegion (meterFace.toNearestInt());

        // Heavy-compression zone, then the scale
        juce::Path zone;
        zone.addCentredArc (needlePivot.x, needlePivot.y, scaleRadius + 6.0f, scaleRadius + 6.0f, 0.0f, angleFor (20.0f), angleFor (10.0f), true);
        g.setColour (juce::Colour (0xffc63a2c));
        g.strokePath (zone, juce::PathStrokeType (9.0f));

        juce::Path arc;
        arc.addCentredArc (needlePivot.x, needlePivot.y, scaleRadius, scaleRadius, 0.0f, angleFor (20.0f), angleFor (0.0f), true);
        g.setColour (ink);
        g.strokePath (arc, juce::PathStrokeType (2.6f));

        g.setFont (juce::FontOptions (24.0f));
        for (const auto db : { 20.0f, 10.0f, 7.0f, 5.0f, 3.0f, 2.0f, 1.0f, 0.0f })
        {
            const auto a = angleFor (db);
            g.setColour (db >= 10.0f ? juce::Colour (0xffb3281f) : ink);
            g.drawLine (juce::Line<float> (polar (needlePivot, scaleRadius - 2.0f, a), polar (needlePivot, scaleRadius + 16.0f, a)), 3.0f);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (40.0f, 26.0f).withCentre (polar (needlePivot, scaleRadius + 36.0f, a)),
                        juce::Justification::centred, false);
        }

        for (int minor = 0; minor <= 20; ++minor)
        {
            const auto a = angleFor ((float) minor);
            g.setColour (ink.withAlpha (0.8f));
            g.drawLine (juce::Line<float> (polar (needlePivot, scaleRadius, a), polar (needlePivot, scaleRadius + 8.0f, a)), 1.6f);
        }

        g.setColour (ink.withAlpha (0.9f));
        g.setFont (juce::FontOptions (54.0f));
        g.drawText ("VU", juce::Rectangle<float> (needlePivot.x - 70.0f, 168.0f, 140.0f, 60.0f), juce::Justification::centred, false);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("dB GAIN REDUCTION", juce::Rectangle<float> (needlePivot.x - 120.0f, 222.0f, 240.0f, 22.0f), juce::Justification::centred, false);

        if (! loaded)
        {
            g.setColour (ink.withAlpha (0.75f));
            g.setFont (juce::FontOptions (20.0f));
            g.drawText (candidates.isEmpty() ? juce::String ("Plugin not found") : "Press IN to load " + candidates.getFirst().name,
                        meterFace.withTrimmedTop (meterFace.getHeight() - 30.0f), juce::Justification::centred, true);
        }

        // Needle, with its shadow on the face
        const auto a = angleFor (needleDb);
        const auto root = polar (needlePivot, 70.0f, a), tip = polar (needlePivot, scaleRadius + 22.0f, a);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawLine (juce::Line<float> (root.translated (6.0f, 5.0f), tip.translated (6.0f, 5.0f)), 3.5f);
        g.setColour (juce::Colour (0xff141414));
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
        const auto grColour = i >= 7 ? juce::Colour (0xfff2c230) : juce::Colour (0xff53d14b);
        const auto outColour = i >= 7 ? juce::Colour (0xffe8382c) : (i >= 6 ? juce::Colour (0xfff2c230) : juce::Colour (0xff53d14b));
        led ({ ledColumns[0], y }, grColour, in && reductionDb >= reductionSteps[i]);
        led ({ ledColumns[1], y }, outColour, outputDb >= outputSteps[i]);

        g.setColour (ink.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (12.0f, 3.0f).withCentre ({ (ledColumns[0] + ledColumns[1]) * 0.5f, y }));
    }

    // --- IN key and its lamp ---------------------------------------------------
    if (! in)
    {
        // The artwork shows the key lit; unlit, it's a dull grey.
        g.setColour (juce::Colour (0xff4a4844).withAlpha (0.62f));
        g.fillRoundedRectangle (inKey.reduced (12.0f), 6.0f);
        g.setColour (juce::Colour (0xffd9d6cc).withAlpha (0.9f));
        g.setFont (juce::FontOptions (38.0f, juce::Font::bold));
        g.drawText ("IN", inKey, juce::Justification::centred, false);
    }

    const auto lamp = juce::Rectangle<float> (34.0f, 34.0f).withCentre (inLamp);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (lamp.expanded (4.0f), 5.0f);
    if (in)
    {
        g.setColour (juce::Colour (0xfff9dc48).withAlpha (0.35f));
        g.fillRoundedRectangle (lamp.expanded (12.0f), 12.0f);
    }
    g.setGradientFill (juce::ColourGradient (in ? juce::Colour (0xfffff3a0) : juce::Colour (0xff6d6436), lamp.getCentreX(), lamp.getY(),
                                             in ? juce::Colour (0xffe8b81c) : juce::Colour (0xff3e3820), lamp.getCentreX(), lamp.getBottom(), false));
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
