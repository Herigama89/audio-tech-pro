#include "Profile.h"
#include "ConsoleStripWindow.h"
#include "Eco.h"
#include "Theme.h"
#include "EffectDesigns.h"
#include "ConsoleLook.h"
#include "../model/Identifiers.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    juce::String hz (float v)  { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String db (float v)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String sec (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }

    constexpr int margin = 8, optionsHeight = 26, curveHeight = 190;

    // Options shared by every strip window this session
    SpectrumAnalyzer::Options& analyzerOptions() { static SpectrumAnalyzer::Options o; return o; }

    juce::Image part (const juce::String& name) { return effectdesign::skinImage ((name + "_png").toRawUTF8()); }

    // The lamps' places on the photo (twice the size shown), from Tools/effect_skins/strip_layout.json
    const std::pair<const char*, juce::Point<int>> lampPlaces[] {
        { "phantom", { 102, 144 } }, { "pad", { 158, 144 } }, { "phase", { 228, 268 } }, { "line", { 284, 268 } },
        { "deEssIn", { 357, 144 } }, { "de10", { 406, 81 } }, { "de6", { 406, 112 } }, { "de3", { 406, 144 } },
        { "knee", { 473, 144 } }, { "compFast", { 529, 144 } }, { "linRel", { 624, 268 } },
        { "gr3", { 746, 51 } }, { "gr6", { 776, 51 } }, { "gr10", { 806, 51 } }, { "gr14", { 836, 52 } }, { "gr20", { 865, 52 } },
        { "gg3", { 746, 104 } }, { "gg6", { 776, 104 } }, { "gg10", { 806, 104 } }, { "gg14", { 836, 104 } }, { "gg20", { 865, 104 } },
        { "dynIn", { 806, 268 } }, { "exp", { 988, 268 } }, { "gateFast", { 1111, 144 } },
        { "filtSc", { 1199, 144 } }, { "filtIn", { 1255, 144 } }, { "lpfOn", { 1297, 144 } }, { "hpfOn", { 1197, 268 } },
        { "lfBell", { 1482, 144 } }, { "black", { 1904, 144 } }, { "eqIn", { 1960, 144 } }, { "eqSc", { 2016, 144 } },
        { "hfBell", { 2435, 144 } }, { "dynPost", { 2537, 144 } }, { "deEssPost", { 2593, 144 } }, { "ext", { 2537, 268 } },
        { "link", { 2593, 268 } }, { "insIn", { 2652, 268 } }, { "insOp", { 2708, 268 } },
        { "o24", { 2785, 47 } }, { "o12", { 2785, 79 } }, { "o0", { 2785, 111 } }, { "om12", { 2785, 142 } }, { "om24", { 2785, 174 } },
        { "mtrInp", { 2785, 268 } } };
    constexpr int lampRadius = 17;
    const juce::Point<int> powerPlace { 2916, 160 };
    constexpr int powerRadius = 62;

    /** Liquid Glass: each knob's ring in its section's colour (the console's colour code). */
    juce::Colour sectionColour (const juce::Identifier& p)
    {
        if (p == ids::consoleMicGain) return juce::Colour (0xffff453a);
        if (p == ids::consoleDeEssThreshold) return juce::Colour (0xffffd60a);
        if (p == ids::consoleGateThreshold || p == ids::consoleGateRange || p == ids::consoleGateRelease) return juce::Colour (0xff30d158);
        if (p == ids::consoleLF || p == ids::consoleLFFreq) return juce::Colour (0xffc49a6c);
        if (p == ids::consoleLMF || p == ids::consoleLMFFreq || p == ids::consoleLMFQ) return juce::Colour (0xff0a84ff);
        if (p == ids::consoleHMF || p == ids::consoleHMFFreq || p == ids::consoleHMFQ) return juce::Colour (0xff30d158);
        if (p == ids::consoleHF || p == ids::consoleHFFreq) return juce::Colour (0xffff453a);
        return juce::Colour (0xffe6e8ec);
    }
    juce::String shortName (const juce::Identifier& p)
    {
        static const std::pair<juce::Identifier, const char*> names[] {
            { ids::consoleMicGain, "MIC" }, { ids::consoleInput, "TRIM" }, { ids::consoleDeEssThreshold, "AMOUNT" },
            { ids::consoleDynamics, "THRESH" }, { ids::consoleCompRatio, "RATIO" }, { ids::consoleCompRelease, "RELEASE" },
            { ids::consoleGateThreshold, "THRESH" }, { ids::consoleGateRange, "RANGE" }, { ids::consoleGateRelease, "RELEASE" },
            { ids::consoleHPF, "HPF" }, { ids::consoleLPF, "LPF" }, { ids::consoleLF, "GAIN" }, { ids::consoleLFFreq, "FREQ" },
            { ids::consoleLMF, "GAIN" }, { ids::consoleLMFFreq, "FREQ" }, { ids::consoleLMFQ, "Q" }, { ids::consoleHMF, "GAIN" },
            { ids::consoleHMFFreq, "FREQ" }, { ids::consoleHMFQ, "Q" }, { ids::consoleHF, "GAIN" }, { ids::consoleHFFreq, "FREQ" },
            { ids::consoleOutput, "OUT" } };
        for (const auto& [id, n] : names)
            if (id == p)
                return n;
        return {};
    }
    juce::String keyCaption (const juce::String& led)
    {
        static const std::pair<const char*, const char*> captions[] {
            { "phantom", "48V" }, { "pad", "PAD" }, { "phase", "\xc3\x98" }, { "line", "LINE" }, { "deEssIn", "IN" }, { "knee", "SOFT" },
            { "compFast", "FAST" }, { "linRel", "LIN" }, { "dynIn", "IN" }, { "exp", "EXP" }, { "gateFast", "FAST" }, { "filtSc", "S/C" },
            { "filtIn", "IN" }, { "lfBell", "BELL" }, { "black", "BLK" }, { "eqIn", "EQ" }, { "eqSc", "S/C" }, { "hfBell", "BELL" },
            { "dynPost", "DYN" }, { "deEssPost", "DE-S" }, { "ext", "EXT" }, { "link", "LINK" }, { "insIn", "INS" }, { "insOp", "POST" },
            { "mtrInp", "MTR" } };
        for (const auto& [l, c] : captions)
            if (led == l)
                return juce::String::fromUTF8 (c);
        return led.toUpperCase();
    }

    // The glass panel's sections (in the face's pixels: x from, x to, title)
    struct Section { float x0, x1; const char* title; juce::Colour dot; };
    const Section sections[] {
        { 3.0f, 159.0f, "INPUT", juce::Colour (0xffff453a) }, { 162.0f, 216.0f, "DE-ESS", juce::Colour (0xffffd60a) },
        { 219.0f, 364.0f, "COMP", juce::Colour (0xffe6e8ec) }, { 367.0f, 440.0f, "GR", juce::Colour (0xffff9f0a) },
        { 443.0f, 575.0f, "GATE", juce::Colour (0xff30d158) }, { 578.0f, 692.0f, "FILTERS", juce::Colour (0xffe6e8ec) },
        { 695.0f, 802.0f, "LF", juce::Colour (0xffc49a6c) }, { 805.0f, 948.0f, "LMF", juce::Colour (0xff0a84ff) },
        { 951.0f, 1012.0f, "EQ", juce::Colour (0xffe6e8ec) }, { 1015.0f, 1140.0f, "HMF", juce::Colour (0xff30d158) },
        { 1143.0f, 1250.0f, "HF", juce::Colour (0xffff453a) }, { 1253.0f, 1312.0f, "ROUTE", juce::Colour (0xffe6e8ec) },
        { 1315.0f, 1410.0f, "OUT", juce::Colour (0xffe6e8ec) }, { 1413.0f, 1492.0f, "", juce::Colour() } };

    juce::Rectangle<int> onFace (int x, int y, int w, int h)
    {
        return juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).transformedBy (juce::AffineTransform::scale (0.5f)).getSmallestIntegerContainer();
    }
}

//==============================================================================
float ConsoleStripWindow::Scale::valueAt (float angle) const noexcept
{
    if (angle < offBelow)
        return offValue;
    if (points.empty())
        return 0.0f;
    if (angle <= points.front().first)
        return points.front().second;
    for (size_t i = 1; i < points.size(); ++i)
        if (angle <= points[i].first)
        {
            const auto [a0, v0] = points[i - 1];
            const auto [a1, v1] = points[i];
            const auto t = (angle - a0) / (a1 - a0);
            return logarithmic ? v0 * std::pow (v1 / v0, t) : v0 + t * (v1 - v0);
        }
    return points.back().second;
}

float ConsoleStripWindow::Scale::angleOf (float value) const noexcept
{
    if (offBelow > -999.0f && std::abs (value - offValue) < 1.0e-4f)
        return -135.0f;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto [a0, v0] = points[i - 1];
        const auto [a1, v1] = points[i];
        const bool inside = (v0 <= v1) ? (value >= v0 && value <= v1) : (value <= v0 && value >= v1);
        if (inside)
        {
            const auto t = logarithmic ? std::log (value / v0) / std::log (v1 / v0) : (value - v0) / (v1 - v0);
            return a0 + (float) t * (a1 - a0);
        }
    }
    // (outside the printed scale: the nearer end)
    const bool rising = points.back().second >= points.front().second;
    return (rising ? value < points.front().second : value > points.front().second) ? points.front().first : points.back().first;
}

//==============================================================================
/** A knob on the panel: its pointer line, cut from the photo, turned to the value. */
class ConsoleStripWindow::Knob final : public juce::Slider
{
public:
    Knob (const char* spriteName, Scale s) : sprite (part (spriteName)), scale (std::move (s))
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        setRange (0.0, 1.0);
        setMouseDragSensitivity (180);
        setVelocityBasedMode (false);
    }

    void paint (juce::Graphics& g) override
    {
        if (theme::glassSkin())
        {
            // Liquid Glass: the LED-ring knob in its section's colour
            const auto area = getLocalBounds().toFloat().reduced (1.0f);
            look::drawGlassKnob (g, area, (float) getValue(), juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), ring, fromCentre, isEnabled());
            return;
        }
        if (! sprite.isValid())
            return;
        const auto angle = -135.0f + 270.0f * (float) getValue();
        const auto area = getLocalBounds().toFloat();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (sprite, juce::AffineTransform::scale (area.getWidth() / (float) sprite.getWidth(), area.getHeight() / (float) sprite.getHeight())
                                            .rotated (juce::degreesToRadians (angle), area.getCentreX(), area.getCentreY()));
    }

    float propertyValue() const { return scale.valueAt (-135.0f + 270.0f * (float) getValue()); }
    void showValue (float v) { setValue ((scale.angleOf (v) + 135.0f) / 270.0f, juce::dontSendNotification); }

    juce::Identifier property;
    juce::String name, label;
    float fallback = 0.0f;
    std::function<juce::String (float)> text;
    juce::Colour ring { 0xffd9d9d6 };
    bool fromCentre = false;

private:
    juce::Image sprite;
    Scale scale;
};

/** A key on the panel (the photo's own, under an invisible button): its lamp shows it. */
class ConsoleStripWindow::Key final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        pressed = false;
        repaint();
        if (contains (e.getPosition()) && onClick)
            onClick();
    }
    void paint (juce::Graphics& g) override
    {
        if (theme::glassSkin() && isOn)
        {
            // Liquid Glass: a pill with its caption, lit when in (the power: a round glass key)
            const auto lit = isOn();
            const auto area = getLocalBounds().toFloat().reduced (1.0f);
            if (round)
            {
                const auto c = area.getCentre();
                const auto r = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
                const auto ball = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
                look::drawKey (g, ball, look::Key::dark, false, {}, false, pressed);
                const auto green = juce::Colour (0xff30d158);
                juce::Path icon;
                icon.addCentredArc (c.x, c.y, r * 0.42f, r * 0.42f, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
                icon.startNewSubPath (c.x, c.y - r * 0.55f);
                icon.lineTo (c.x, c.y - r * 0.12f);
                if (lit)
                {
                    g.setColour (green.withAlpha (0.35f));
                    g.strokePath (icon, juce::PathStrokeType (r * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (lit ? green : juce::Colour (0xff6b707a));
                g.strokePath (icon, juce::PathStrokeType (r * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const auto pill = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 15.0f));
            look::drawKey (g, pill, look::Key::dark, lit, colour, false, pressed);
            g.setColour (lit ? (colour.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff1a1205) : juce::Colours::white) : theme::stripInk);
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawFittedText (label, pill.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            return;
        }
        // (pressed: the key goes down a little)
        if (pressed)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f);
        }
    }
    bool hitTest (int x, int y) override
    {
        if (! round)
            return true;
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.47f;
    }

    juce::Identifier property;
    juce::String name, led, label;
    bool defaultOn = false, pressed = false, round = false;
    juce::Colour colour { 0xffff9f0a };
    std::function<void()> onClick;
    std::function<bool()> isOn;
};

//==============================================================================
ConsoleStripWindow::ConsoleStripWindow (juce::ValueTree t, juce::UndoManager& um,
                                        std::function<int (float*, int)> analyzerSource, std::function<double()> sampleRate,
                                        Levels levelSource)
    : track (t), undoManager (um), analyzer (std::move (analyzerSource), std::move (sampleRate)), levels (std::move (levelSource))
{
    for (const auto& [name, at] : lampPlaces)
        leds[name] = at;

    const auto lin = [] (float a, float b) { Scale s; s.points = { { -135.0f, a }, { 135.0f, b } }; return s; };
    const auto lg = [] (std::vector<std::pair<float, float>> p) { Scale s; s.points = std::move (p); s.logarithmic = true; return s; };
    const auto g = ConsoleChannel::maxGainDb;

    // INPUT
    addKnob (ids::consoleMicGain, "Mic gain", "strip_ptr_mic", 98, 213, 34, lin (20.0f, 70.0f), 20.0f,
             [this] (float v) { return "+" + juce::String (juce::roundToInt (v)) + " dB" + (on (ids::consoleLine, true) ? " (LINE is on: off for the mic)" : ""); });
    addKnob (ids::consoleInput, "Trim", "strip_ptr_trim", 227, 86, 37, lin (-20.0f, 20.0f), 0.0f, db);
    addKey (ids::consolePhantom, "48V: the interface's phantom power for this input (when the interface's control app is running)", "phantom", false);
    addKey (ids::consolePad, "PAD: 20 dB down at the input", "pad", false);
    addKey (ids::consolePhase, "Polarity", "phase", false);
    addKey (ids::consoleLine, "LINE: the line input (off: the mic preamp, its gain and colour)", "line", true);
    // DE-ESS
    addKnob (ids::consoleDeEssThreshold, "De-ess threshold", "strip_ptr_deEss", 355, 212, 35, lin (10.0f, -20.0f), 0.0f, db);
    addKey (ids::consoleDeEss, "De-esser in", "deEssIn", false);
    // COMP
    {
        Scale thresh = lin (0.0f, ConsoleChannel::maxDynamics);
        addKnob (ids::consoleDynamics, "Compressor threshold", "strip_ptr_thresh", 604, 84, 36, thresh, 0.0f,
                 [] (float v) { return v < 0.05f ? juce::String ("Off") : juce::String (-3.0f * v, 1) + " dB"; });
        Scale ratio = lg ({ { -135.0f, 1.0f }, { 135.0f, 20.0f } });
        ratio.offBelow = -128.0f;   // (all the way left: Auto, the ratio follows the threshold)
        addKnob (ids::consoleCompRatio, "Ratio", "strip_ptr_ratio", 505, 211, 36, ratio, 0.0f,
                 [] (float v) { return v < 1.0f ? juce::String ("Auto") : v >= 19.5f ? juce::String::fromUTF8 ("\xe2\x88\x9e:1 (limit)") : juce::String (v, 1) + ":1"; });
        addKnob (ids::consoleCompRelease, "Release", "strip_ptr_release", 700, 212, 36, lg ({ { -135.0f, 0.1f }, { 135.0f, 4.0f } }), 0.15f, sec);
        addKey (ids::consoleCompKnee, "Soft knee", "knee", false);
        addKey (ids::consoleCompFast, "Fast attack", "compFast", false);
        addKey (ids::consoleCompLinear, "Linear release", "linRel", false);
        addKey (ids::consoleDynIn, "Dynamics in", "dynIn", true);
    }
    // GATE
    {
        Scale gate;
        gate.points = { { -128.0f, -60.0f }, { 135.0f, 0.0f } };
        gate.offBelow = -128.0f;
        gate.offValue = -80.0f;
        addKnob (ids::consoleGateThreshold, "Gate threshold", "strip_ptr_gate", 975, 87, 34, gate, -80.0f,
                 [] (float v) { return v <= -79.9f ? juce::String ("Off") : db (v); });
        addKnob (ids::consoleGateRange, "Range", "strip_ptr_range", 880, 214, 34, lin (0.0f, 40.0f), 20.0f,
                 [] (float v) { return juce::String (juce::roundToInt (v)) + " dB"; });
        addKnob (ids::consoleGateRelease, "Gate release", "strip_ptr_gateRel", 1072, 214, 34, lg ({ { -135.0f, 0.1f }, { 135.0f, 4.0f } }), 0.25f, sec);
        addKey (ids::consoleGateExpander, "Expander (gentler than the gate)", "exp", false);
        addKey (ids::consoleGateFast, "Gate fast attack", "gateFast", false);
    }
    // FILTERS (all the way left: out)
    {
        auto hpf = lg ({ { -90.0f, 20.0f }, { -45.0f, 70.0f }, { 45.0f, 200.0f }, { 90.0f, 300.0f }, { 135.0f, 500.0f } });
        hpf.offBelow = -112.0f;
        addKnob (ids::consoleHPF, "High pass", "strip_ptr_hpf", 1250, 211, 36, hpf, 0.0f, [] (float v) { return v <= 0.0f ? juce::String ("Out") : hz (v); });
        auto lpf = lg ({ { -90.0f, 12000.0f }, { -45.0f, 8000.0f }, { 45.0f, 4000.0f }, { 90.0f, 3500.0f }, { 135.0f, 3000.0f } });
        lpf.offBelow = -112.0f;
        addKnob (ids::consoleLPF, "Low pass", "strip_ptr_lpf", 1360, 85, 36, lpf, 0.0f, [] (float v) { return v <= 0.0f ? juce::String ("Out") : hz (v); });
        addKey (ids::consoleFiltersToSidechain, "Filters into the dynamics' side chain only", "filtSc", false);
        addKey (ids::consoleFiltersAtInput, "Filters at the input (off: after the EQ)", "filtIn", true);
    }
    // EQ
    addKnob (ids::consoleLF, "LF gain", "strip_ptr_lf", 1585, 88, 37, lin (-g, g), 0.0f, db);
    addKnob (ids::consoleLFFreq, "LF frequency", "strip_ptr_lfFreq", 1483, 216, 37,
             lg ({ { -135.0f, 30.0f }, { -90.0f, 50.0f }, { -45.0f, 100.0f }, { 45.0f, 300.0f }, { 90.0f, 400.0f }, { 135.0f, 450.0f } }), 100.0f, hz);
    addKey (ids::consoleLFBell, "LF bell (off: shelf)", "lfBell", false);
    addKnob (ids::consoleLMF, "LMF gain", "strip_ptr_lmf", 1794, 85, 33, lin (-g, g), 0.0f, db);
    addKnob (ids::consoleLMFFreq, "LMF frequency", "strip_ptr_lmfFreq", 1697, 212, 33,
             lg ({ { -135.0f, 200.0f }, { -90.0f, 300.0f }, { -45.0f, 800.0f }, { 45.0f, 1500.0f }, { 90.0f, 2000.0f }, { 135.0f, 2500.0f } }), 400.0f, hz);
    addKnob (ids::consoleLMFQ, "LMF Q", "strip_ptr_lmfQ", 1890, 212, 33, lg ({ { -135.0f, 0.1f }, { 0.0f, 1.0f }, { 135.0f, 4.0f } }), 1.0f,
             [] (float v) { return "Q " + juce::String (v, 2); });
    addKnob (ids::consoleHMFQ, "HMF Q", "strip_ptr_hmfQ", 2051, 211, 35, lg ({ { -135.0f, 0.1f }, { 0.0f, 1.0f }, { 135.0f, 4.0f } }), 1.0f,
             [] (float v) { return "Q " + juce::String (v, 2); });
    addKnob (ids::consoleHMF, "HMF gain", "strip_ptr_hmf", 2148, 87, 34, lin (-g, g), 0.0f, db);
    addKnob (ids::consoleHMFFreq, "HMF frequency", "strip_ptr_hmfFreq", 2245, 214, 34,
             lg ({ { -135.0f, 600.0f }, { -90.0f, 800.0f }, { -45.0f, 1500.0f }, { 45.0f, 4500.0f }, { 90.0f, 6000.0f }, { 135.0f, 7000.0f } }), 2500.0f, hz);
    addKnob (ids::consoleHF, "HF gain", "strip_ptr_hf", 2356, 86, 33, lin (-g, g), 0.0f, db);
    addKnob (ids::consoleHFFreq, "HF frequency", "strip_ptr_hfFreq", 2457, 213, 33,
             lg ({ { -135.0f, 1500.0f }, { -90.0f, 2000.0f }, { -45.0f, 5000.0f }, { 45.0f, 10000.0f }, { 90.0f, 14000.0f }, { 135.0f, 16000.0f } }), 8000.0f, hz);
    addKey (ids::consoleHFBell, "HF bell (off: shelf)", "hfBell", false);
    addKey (ids::consoleBlackEq, "Black-knob EQ: the bells narrow as they boost or cut", "black", false);
    addKey (ids::consoleIn, "EQ in", "eqIn", true);
    addKey (ids::consoleEqToSidechain, "EQ into the dynamics' side chain only", "eqSc", false);
    // ROUTE
    addKey (ids::consoleDynPostEq, "Dynamics after the EQ (off: before it)", "dynPost", true);
    addKey (ids::consoleDeEssPostEq, "De-esser after the EQ (off: before it)", "deEssPost", true);
    addKey (ids::consoleExternalKey, "External key: another track's sound keys the dynamics (choose it under the panel)", "ext", false);
    addKey (ids::consoleLink, "Stereo link (off: each side compressed by its own level)", "link", true);
    // OUT, INSERT, meter
    addKnob (ids::consoleOutput, "Output", "strip_ptr_out", 2707, 84, 36, lin (-20.0f, 20.0f), 0.0f, db);
    addKey (ids::consoleInsertIn, "Inserts in (the track's plug-ins)", "insIn", true);
    addKey (ids::consoleInsertsPost, "Inserts after the strip (off: before it)", "insOp", true);
    addKey (ids::consoleMeterInput, "Meter the input (off: the output)", "mtrInp", false);

    // The power: the whole strip in or out
    power = std::make_unique<Key>();
    power->property = ids::stripIn;
    power->name = "Power: the channel strip in or out";
    power->defaultOn = true;
    power->round = true;
    power->isOn = [this] { return on (ids::stripIn, true); };
    power->setTooltip (power->name);
    power->onClick = [this]
    {
        undoManager.beginNewTransaction ("Channel strip power");
        track.setProperty (ids::stripIn, ! on (ids::stripIn, true), &undoManager);
    };
    addAndMakeVisible (*power);

    // External key: which track keys it (EXT turns it on)
    keyButton.setTooltip ("The track whose sound keys the strip's dynamics when EXT is on");
    keyButton.onClick = [this] { showKeyMenu(); };
    addAndMakeVisible (keyButton);

    // The track's MultiMeter under the EQ (when it's been given one)
    meterButton.setClickingTogglesState (true);
    meterButton.setToggleState ((bool) track.getProperty ("stripMeterShown", true), juce::dontSendNotification);
    meterButton.setTooltip ("The track's MultiMeter under the EQ: analyzer, goniometer, loudness (LUFS), peak and RMS levels, correlation. "
                            "Make the window taller for a bigger meter");
    meterButton.setWantsKeyboardFocus (false);
    meterButton.onClick = [this]
    {
        track.setProperty ("stripMeterShown", meterButton.getToggleState(), nullptr);
        if (meter != nullptr)
            meter->setVisible (meterButton.getToggleState());
        if (onPreferredHeightChanged)
            onPreferredHeightChanged();
        resized();
    };
    addChildComponent (meterButton);

    // The analyzer behind the curve, and its settings
    addAndMakeVisible (analyzer);
    auto setupBox = [this] (juce::ComboBox& box, const juce::StringArray& items, int selected, const juce::String& tip)
    {
        box.addItemList (items, 1);
        box.setSelectedItemIndex (selected, juce::dontSendNotification);
        box.setTooltip (tip);
        box.onChange = [this] { applyAnalyzerOptions(); };
        box.setWantsKeyboardFocus (false);
        addAndMakeVisible (box);
    };
    const auto& o = analyzerOptions();
    setupBox (modeBox, { "Bars", "Line", "Off" }, ! o.enabled ? 2 : (o.bars ? 0 : 1), "Analyzer: 1/3-octave bars, a line, or off");
    setupBox (fftBox, { "FFT 1k", "FFT 2k", "FFT 4k", "FFT 8k" }, juce::jlimit (0, 3, o.fftOrder - 10), "FFT size: bigger shows low notes more finely but moves slower");
    setupBox (rangeBox, { "60 dB", "80 dB", "100 dB" }, o.rangeDb <= 60.0f ? 0 : o.rangeDb <= 80.0f ? 1 : 2, "View range: how far down the analyzer shows");
    setupBox (releaseBox, { "Fast", "Med", "Slow" }, o.releaseDbPerSecond >= 100.0f ? 0 : o.releaseDbPerSecond >= 30.0f ? 1 : 2, "Release: how fast the bars fall back");
    setupBox (holdBox, { "Pk off", "Pk 1s", "Pk 3s", "Pk hold" }, o.peakHoldSeconds == 0.0f ? 0 : o.peakHoldSeconds < 0.0f ? 3 : (o.peakHoldSeconds <= 1.0f ? 1 : 2),
              "Peak hold: how long each band's highest level stays marked");
    applyAnalyzerOptions();

    track.addListener (this);
    refresh();

    setSize (faceWidth, faceHeight + margin + optionsHeight + curveHeight + margin);
    if (levels)
        startTimerHz (30);
}

ConsoleStripWindow::~ConsoleStripWindow()
{
    track.removeListener (this);
}

void ConsoleStripWindow::addKnob (const juce::Identifier& property, const juce::String& name, const char* sprite, int cx, int cy, int r,
                                  Scale scale, float fallback, std::function<juce::String (float)> text)
{
    auto k = std::make_unique<Knob> (sprite, std::move (scale));
    k->property = property;
    k->name = name;
    k->fallback = fallback;
    k->text = std::move (text);
    k->setBounds (onFace (cx - r, cy - r, r * 2, r * 2));
    k->label = shortName (property);
    k->ring = sectionColour (property);
    k->fromCentre = property == ids::consoleLF || property == ids::consoleLMF || property == ids::consoleHMF || property == ids::consoleHF
                 || property == ids::consoleInput || property == ids::consoleOutput;
    auto* raw = k.get();
    k->textFromValueFunction = [raw] (double) { return raw->name + ": " + raw->text (raw->propertyValue()); };
    k->setPopupDisplayEnabled (true, true, this);
    k->setTooltip (name);
    k->onDragStart = [this, name] { undoManager.beginNewTransaction ("Channel strip " + name); };
    k->onValueChange = [this, raw] { track.setProperty (raw->property, raw->propertyValue(), &undoManager); };
    k->setDoubleClickReturnValue (false, 0.0);
    addAndMakeVisible (*k);
    knobs.push_back (std::move (k));
}

void ConsoleStripWindow::addKey (const juce::Identifier& property, const juce::String& name, const char* led, bool defaultOn)
{
    auto k = std::make_unique<Key>();
    k->property = property;
    k->name = name;
    k->led = led;
    k->defaultOn = defaultOn;
    k->setTooltip (name);
    const auto at = leds[led];
    k->setBounds (onFace (at.x - 25, at.y - 46 - 22, 50, 44));   // (the key sits just above its lamp)
    k->label = keyCaption (led);
    k->colour = juce::String (led) == "line" || juce::String (led) == "dynIn" || juce::String (led) == "eqIn" || juce::String (led) == "filtIn"
                || juce::String (led) == "insIn" || juce::String (led) == "deEssIn" ? juce::Colour (0xff30d158) : juce::Colour (0xffff9f0a);
    auto* raw = k.get();
    k->isOn = [this, raw]
    {
        // (48V: the interface's own phantom power, when its preamp is reachable)
        if (raw->property == ids::consolePhantom && interfacePhantom)
            if (const auto state = interfacePhantom())
                return *state;
        return on (raw->property, raw->defaultOn);
    };
    k->onClick = [this, raw]
    {
        if (raw->property == ids::consolePhantom && interfacePhantom && setInterfacePhantom)
            if (const auto state = interfacePhantom())
            {
                setInterfacePhantom (! *state);
                track.setProperty (raw->property, ! *state, nullptr);
                repaint();
                return;
            }
        undoManager.beginNewTransaction ("Channel strip " + raw->name.upToFirstOccurrenceOf (":", false, false));
        const auto turningOn = ! on (raw->property, raw->defaultOn);
        track.setProperty (raw->property, turningOn, &undoManager);
        if (raw->property == ids::consoleExternalKey && turningOn && ! model::findById (track.getParent(), track[ids::consoleKey].toString()).isValid())
            showKeyMenu();   // (EXT with no key yet: choose one)
    };
    addAndMakeVisible (*k);
    keys.push_back (std::move (k));
}

void ConsoleStripWindow::showKeyMenu()
{
    juce::PopupMenu menu;
    const auto project = track.getParent();
    const auto current = track[ids::consoleKey].toString();
    auto set = [this] (const juce::String& id)
    {
        undoManager.beginNewTransaction ("Channel strip key");
        track.setProperty (ids::consoleKey, id, &undoManager);
        track.setProperty (ids::consoleExternalKey, id.isNotEmpty(), &undoManager);
    };
    menu.addSectionHeader ("Key the dynamics with");
    menu.addItem ("None (its own sound)", true, current.isEmpty(), [set] { set ({}); });
    menu.addSeparator();
    for (const auto& other : project)
    {
        if (! other.hasType (ids::TRACK) || model::isVcaTrack (other) || other == track)
            continue;
        const auto id = other[ids::id].toString();
        menu.addItem ((model::isBusTrack (other) ? "Bus: " : "") + other[ids::name].toString(), true, id == current, [set, id] { set (id); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton));
}

void ConsoleStripWindow::refresh()
{
    for (auto& k : knobs)
        k->showValue (track.hasProperty (k->property) ? (float) (double) track[k->property] : k->fallback);
    const auto key = model::findById (track.getParent(), track[ids::consoleKey].toString());
    keyButton.setButtonText ("Key: " + (key.isValid() ? key[ids::name].toString() : juce::String ("none")));
}

void ConsoleStripWindow::timerCallback()
{
    RIFF_PROFILE ("Channel strip window update");
    // (each window its own count: shared, two windows would starve each other in Eco mode)
    if (eco::skipFrame (ecoFrame))
        return;   // (Eco mode)
    const auto now = levels();
    const auto in = juce::Decibels::gainToDecibels (now[0], -100.0f), out = juce::Decibels::gainToDecibels (now[1], -100.0f);
    inDb = juce::jmax (in, inDb - 1.5f);    // (they fall back gently)
    outDb = juce::jmax (out, outDb - 1.5f);
    const auto ease = [] (float& shown, float v) { shown += (v - shown) * (v > shown ? 0.6f : 0.2f); };
    ease (grDb, std::abs (now[2]));
    ease (deEssDb, std::abs (now[3]));
    ease (gateDb, std::abs (now[4]));
    // (48V shows only when the interface's phantom power can be switched from here)
    for (auto& k : keys)
        if (k->property == ids::consolePhantom)
        {
            const bool reachable = interfacePhantom && interfacePhantom().has_value();
            if (k->isVisible() != reachable)
            {
                k->setVisible (reachable);
                repaint();
            }
        }
    // Only the meters move: repaint them alone (and only when they changed), not the whole face and its knobs
    const std::array<float, 5> shown { inDb, outDb, grDb, deEssDb, gateDb };
    if (shown == lastShown)
        return;
    lastShown = shown;
    if (theme::glassSkin())
    {
        repaint (368, 22, 70, 60);      // compressor and gate
        repaint (196, 22, 14, 56);      // de-esser
        repaint (1384, 18, 18, 76);     // level
    }
    else
        repaint (0, 0, faceWidth, faceHeight);
}

void ConsoleStripWindow::lamp (juce::Graphics& g, const juce::String& name) const
{
    const auto it = leds.find (name);
    if (it == leds.end())
        return;
    const auto image = part ("strip_led_" + name);
    g.drawImage (image, onFace (it->second.x - lampRadius, it->second.y - lampRadius, lampRadius * 2, lampRadius * 2).toFloat());
}

juce::Rectangle<int> ConsoleStripWindow::optionsArea() const
{
    return { margin, faceHeight + margin, getWidth() - margin * 2, optionsHeight - 4 };
}

juce::Rectangle<int> ConsoleStripWindow::curveArea() const
{
    return { margin, optionsArea().getBottom() + 4, getWidth() - margin * 2, curveHeight };
}

void ConsoleStripWindow::applyAnalyzerOptions()
{
    auto& o = analyzerOptions();
    o.enabled = modeBox.getSelectedItemIndex() != 2;
    o.bars = modeBox.getSelectedItemIndex() == 0;
    o.fftOrder = 10 + juce::jmax (0, fftBox.getSelectedItemIndex());
    o.rangeDb = std::array<float, 3> { 60.0f, 80.0f, 100.0f }[(size_t) juce::jlimit (0, 2, rangeBox.getSelectedItemIndex())];
    o.releaseDbPerSecond = std::array<float, 3> { 120.0f, 40.0f, 15.0f }[(size_t) juce::jlimit (0, 2, releaseBox.getSelectedItemIndex())];
    o.peakHoldSeconds = std::array<float, 4> { 0.0f, 1.0f, 3.0f, -1.0f }[(size_t) juce::jlimit (0, 3, holdBox.getSelectedItemIndex())];
    analyzer.setOptions (o);
}

void ConsoleStripWindow::setMeter (std::unique_ptr<juce::Component> multiMeter)
{
    meter = std::move (multiMeter);
    if (meter != nullptr)
    {
        addChildComponent (*meter);
        meter->setVisible (meterButton.getToggleState());
    }
    meterButton.setVisible (meter != nullptr);
    resized();
}

int ConsoleStripWindow::preferredHeight() const
{
    const auto base = faceHeight + margin + optionsHeight + curveHeight + margin;
    return meter != nullptr && meterButton.getToggleState() ? base + 560 : base;
}

juce::Rectangle<int> ConsoleStripWindow::meterArea() const
{
    const auto top = curveArea().getBottom() + margin;
    return { margin, top, getWidth() - margin * 2, juce::jmax (0, getHeight() - top - margin) };
}

void ConsoleStripWindow::resized()
{
    analyzer.setBounds (curveArea().reduced (2));
    if (meter != nullptr)
        meter->setBounds (meterArea());
    auto row = optionsArea();
    keyButton.setBounds (row.removeFromRight (220).reduced (2, 0));
    if (meterButton.isVisible())
        meterButton.setBounds (row.removeFromRight (120).reduced (2, 0));
    const auto w = juce::jmin (130, row.getWidth() / 5);
    for (auto* box : { &modeBox, &fftBox, &rangeBox, &releaseBox, &holdBox })
        box->setBounds (row.removeFromLeft (w).reduced (2, 0));
    power->setBounds (onFace (powerPlace.x - 58, powerPlace.y - 58, 116, 116));
}

void ConsoleStripWindow::paintGlass (juce::Graphics& g)
{
    // Liquid Glass: the window's ground, one glass card for the panel, a recessed glass section per part
    g.fillAll (theme::console);
    const auto face = juce::Rectangle<float> (0.0f, 0.0f, (float) faceWidth, (float) faceHeight).reduced (1.5f);
    look::drawGlassPanel (g, face, 14.0f);
    for (const auto& sec : sections)
    {
        const auto r = juce::Rectangle<float> (sec.x0, 6.0f, sec.x1 - sec.x0, (float) faceHeight - 12.0f);
        look::drawGlassPanel (g, r, 10.0f, true);
        if (sec.title[0] != 0)
        {
            g.setColour (sec.dot);
            g.fillEllipse (r.getX() + 7.0f, r.getY() + 7.0f, 5.0f, 5.0f);
            g.setColour (theme::stripInk.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (sec.title, juce::Rectangle<float> (r.getX() + 15.0f, r.getY() + 3.0f, 80.0f, 13.0f), juce::Justification::centredLeft, false);
        }
    }
    // the knobs' captions
    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
    g.setColour (theme::stripInkDim);
    for (const auto& k : knobs)
        g.drawText (k->label, k->getBounds().translated (0, k->getHeight() + 1).withHeight (9).expanded (10, 0), juce::Justification::centred, false);

    const bool powered = on (ids::stripIn, true);
    // The meters, smooth: gain reduction from the left (compressor, gate), the de-esser's, the level
    const auto bar = [&g] (juce::Rectangle<float> r, float fraction, bool vertical, bool reduction)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
        fraction = juce::jlimit (0.0f, 1.0f, fraction);
        if (fraction <= 0.0f)
            return;
        const auto lit = vertical ? r.withTrimmedTop (r.getHeight() * (1.0f - fraction)) : r.withWidth (r.getWidth() * fraction);
        juce::ColourGradient grad = reduction ? juce::ColourGradient (juce::Colour (0xffffd60a), r.getX(), r.getY(), juce::Colour (0xffff453a), vertical ? r.getX() : r.getRight(), vertical ? r.getBottom() : r.getY(), false)
                                              : juce::ColourGradient (juce::Colour (0xff30d158), r.getX(), r.getBottom(), juce::Colour (0xffff453a), r.getX(), r.getY(), false);
        if (! reduction)
            grad.addColour (0.75, juce::Colour (0xffffd60a));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (lit, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
    };
    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
    g.setColour (theme::stripInkDim);
    g.drawText ("COMP", juce::Rectangle<float> (373.0f, 28.0f, 60.0f, 9.0f), juce::Justification::centredLeft, false);
    g.drawText ("GATE", juce::Rectangle<float> (373.0f, 58.0f, 60.0f, 9.0f), juce::Justification::centredLeft, false);
    bar ({ 373.0f, 40.0f, 60.0f, 7.0f }, powered ? grDb / 20.0f : 0.0f, false, true);
    bar ({ 373.0f, 70.0f, 60.0f, 7.0f }, powered ? gateDb / 20.0f : 0.0f, false, true);
    bar ({ 200.0f, 26.0f, 6.0f, 48.0f }, powered ? deEssDb / 12.0f : 0.0f, true, true);
    const auto level = on (ids::consoleMeterInput, false) ? inDb : outDb;
    bar ({ 1389.0f, 22.0f, 7.0f, 68.0f }, powered ? juce::jmap (juce::jlimit (-48.0f, 0.0f, level), -48.0f, 0.0f, 0.0f, 1.0f) : 0.0f, true, false);
}

void ConsoleStripWindow::paint (juce::Graphics& g)
{
    RIFF_PROFILE ("Channel strip window");
    if (theme::glassSkin())
    {
        paintGlass (g);
        paintCurveGround (g);
        return;
    }
    g.fillAll (juce::Colour (0xff141518));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (effectdesign::skinImage ("strip_panel_jpg"), juce::Rectangle<float> (0.0f, 0.0f, (float) faceWidth, (float) faceHeight));

    const bool powered = on (ids::stripIn, true);
    if (powered)
    {
        g.drawImage (part ("strip_power_on"), onFace (powerPlace.x - powerRadius, powerPlace.y - powerRadius, powerRadius * 2, powerRadius * 2).toFloat());

        // The keys' lamps
        for (const auto& k : keys)
            if (on (k->property, k->defaultOn))
                lamp (g, k->led);
        if ((double) track.getProperty (ids::consoleHPF, 0.0) > 0.0) lamp (g, "hpfOn");
        if ((double) track.getProperty (ids::consoleLPF, 0.0) > 0.0) lamp (g, "lpfOn");

        // The meters: gain reduction (compressor on top, the gate under it), the de-esser's, the level
        const float steps[] { 3.0f, 6.0f, 10.0f, 14.0f, 20.0f };
        const char* comp[] { "gr3", "gr6", "gr10", "gr14", "gr20" };
        const char* gate[] { "gg3", "gg6", "gg10", "gg14", "gg20" };
        for (int i = 0; i < 5; ++i)
        {
            if (grDb >= steps[i]) lamp (g, comp[i]);
            if (gateDb >= steps[i]) lamp (g, gate[i]);
        }
        if (deEssDb >= 3.0f) lamp (g, "de3");
        if (deEssDb >= 6.0f) lamp (g, "de6");
        if (deEssDb >= 10.0f) lamp (g, "de10");
        const auto level = on (ids::consoleMeterInput, false) ? inDb : outDb;
        const std::pair<float, const char*> meter[] { { -42.0f, "om24" }, { -30.0f, "om12" }, { -18.0f, "o0" }, { -9.0f, "o12" }, { -0.5f, "o24" } };
        for (const auto& [at, name] : meter)
            if (level >= at)
                lamp (g, name);
    }

    paintCurveGround (g);
}

void ConsoleStripWindow::paintCurveGround (juce::Graphics& g)
{
    // The screen under the panel: the EQ curve over the spectrum, 20 Hz to 20 kHz, +-18 dB
    const auto area = curveArea().toFloat();
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (area, theme::glassSkin() ? 12.0f : 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    const auto xOf = [&area] (double f) { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
    g.setFont (juce::FontOptions (9.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (xOf (f), area.getY(), 1.0f, area.getHeight());
        g.setColour (theme::lcdDim);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (xOf (f) + 2.0f, area.getBottom() - 13.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);
}

void ConsoleStripWindow::paintOverChildren (juce::Graphics& g)
{
    // The EQ curve over the analyzer
    const auto area = curveArea().toFloat();
    const auto xOf = [&area] (double f) { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
    const auto yOf = [&area] (double dbValue) { return area.getCentreY() - (float) (juce::jlimit (-18.0, 18.0, dbValue) / 18.0) * (area.getHeight() * 0.5f - 6.0f); };
    const auto settings = ConsoleChannel::fromTrack (track);
    juce::Path curve;
    for (int i = 0; i <= 300; ++i)
    {
        const auto f = 20.0 * std::pow (1000.0, i / 300.0);
        const auto y = settings.enabled ? yOf (ConsoleChannel::responseDb (settings, f)) : yOf (0.0);
        if (i == 0) curve.startNewSubPath (xOf (f), y);
        else        curve.lineTo (xOf (f), y);
    }
    if (theme::glassSkin() && settings.enabled)
    {
        g.setColour (juce::Colour (0xff64d2ff).withAlpha (0.3f));
        g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff9fe6ff));
    }
    else
        g.setColour (settings.enabled ? theme::neon : theme::dimText);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    g.setFont (juce::FontOptions (9.0f));
    for (int d : { 12, 6, -6, -12 })
    {
        g.setColour (theme::neon.withAlpha (0.7f));
        g.drawText ((d > 0 ? "+" : "") + juce::String (d), juce::Rectangle<float> (area.getX() + 3.0f, yOf (d) - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    // Powered off: the panel goes dark
    if (! settings.enabled && ! theme::glassSkin())
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (0, 0, powerPlace.x / 2 - 34, faceHeight);   // (all but the power button)
    }
}

} // namespace riff
