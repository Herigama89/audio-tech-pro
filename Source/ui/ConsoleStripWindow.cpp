#include "ConsoleStripWindow.h"
#include "Eco.h"
#include "Theme.h"
#include "EffectDesigns.h"
#include "../model/Identifiers.h"
#if ! RIFF_PLUGIN
 #include "../model/Project.h"
#endif

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
    juce::String name;
    float fallback = 0.0f;
    std::function<juce::String (float)> text;

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
    juce::String name, led;
    bool defaultOn = false, pressed = false, round = false;
    std::function<void()> onClick;
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
    addKey (ids::consolePhantom, "48V (its lamp only: no microphone here needs power)", "phantom", false);
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
#if RIFF_PLUGIN
    // (as a plug-in: the host's side-chain input is the key - route a track to it in the host)
    addKey (ids::consoleExternalKey, "External key: the plug-in's side-chain input keys the dynamics (send a track to it in your DAW)", "ext", false);
#else
    addKey (ids::consoleExternalKey, "External key: another track's sound keys the dynamics (choose it under the panel)", "ext", false);
#endif
    addKey (ids::consoleLink, "Stereo link (off: each side compressed by its own level)", "link", true);
    // OUT, INSERT, meter
    addKnob (ids::consoleOutput, "Output", "strip_ptr_out", 2707, 84, 36, lin (-20.0f, 20.0f), 0.0f, db);
#if ! RIFF_PLUGIN
    // (as a plug-in, the DAW has the inserts)
    addKey (ids::consoleInsertIn, "Inserts in (the track's plug-ins)", "insIn", true);
    addKey (ids::consoleInsertsPost, "Inserts after the strip (off: before it)", "insOp", true);
#endif
    addKey (ids::consoleMeterInput, "Meter the input (off: the output)", "mtrInp", false);

    // The power: the whole strip in or out
    power = std::make_unique<Key>();
    power->property = ids::stripIn;
    power->name = "Power: the channel strip in or out";
    power->defaultOn = true;
    power->round = true;
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
#if ! RIFF_PLUGIN
    addAndMakeVisible (keyButton);
#endif

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
    auto* raw = k.get();
    k->onClick = [this, raw]
    {
        undoManager.beginNewTransaction ("Channel strip " + raw->name.upToFirstOccurrenceOf (":", false, false));
        const auto turningOn = ! on (raw->property, raw->defaultOn);
        track.setProperty (raw->property, turningOn, &undoManager);
#if ! RIFF_PLUGIN
        if (raw->property == ids::consoleExternalKey && turningOn && ! model::findById (track.getParent(), track[ids::consoleKey].toString()).isValid())
            showKeyMenu();   // (EXT with no key yet: choose one)
#endif
    };
    addAndMakeVisible (*k);
    keys.push_back (std::move (k));
}

void ConsoleStripWindow::showKeyMenu()
{
#if ! RIFF_PLUGIN
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
#endif
}

void ConsoleStripWindow::refresh()
{
    for (auto& k : knobs)
        k->showValue (track.hasProperty (k->property) ? (float) (double) track[k->property] : k->fallback);
#if ! RIFF_PLUGIN
    const auto key = model::findById (track.getParent(), track[ids::consoleKey].toString());
    keyButton.setButtonText ("Key: " + (key.isValid() ? key[ids::name].toString() : juce::String ("none")));
#endif
}

void ConsoleStripWindow::timerCallback()
{
    static int ecoFrame = 0;
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

void ConsoleStripWindow::resized()
{
    analyzer.setBounds (curveArea().reduced (2));
    auto row = optionsArea();
    keyButton.setBounds (row.removeFromRight (220).reduced (2, 0));
    const auto w = juce::jmin (130, row.getWidth() / 5);
    for (auto* box : { &modeBox, &fftBox, &rangeBox, &releaseBox, &holdBox })
        box->setBounds (row.removeFromLeft (w).reduced (2, 0));
    power->setBounds (onFace (powerPlace.x - 58, powerPlace.y - 58, 116, 116));
}

void ConsoleStripWindow::paint (juce::Graphics& g)
{
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

    // The screen under the panel: the EQ curve over the spectrum, 20 Hz to 20 kHz, +-18 dB
    const auto area = curveArea().toFloat();
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (area, 4.0f);
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
    g.setColour (settings.enabled ? theme::neon : theme::dimText);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    g.setFont (juce::FontOptions (9.0f));
    for (int d : { 12, 6, -6, -12 })
    {
        g.setColour (theme::neon.withAlpha (0.7f));
        g.drawText ((d > 0 ? "+" : "") + juce::String (d), juce::Rectangle<float> (area.getX() + 3.0f, yOf (d) - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    // Powered off: the panel goes dark
    if (! settings.enabled)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (0, 0, powerPlace.x / 2 - 34, faceHeight);   // (all but the power button)
    }
}

} // namespace riff
