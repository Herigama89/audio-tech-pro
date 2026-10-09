#include "ConsoleStripWindow.h"
#include "ConsoleLook.h"
#include "Theme.h"
#include "Skin.h"
#include "../model/Identifiers.h"

namespace riff
{

namespace
{
    juce::String hz (double v)  { return v >= 1000.0 ? juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String db (double v)  { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String sec (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; }

    constexpr int cellWidth = 92, cellHeight = 84, headerHeight = 22, margin = 10, curveHeight = 200, optionsHeight = 26;

    // A classic console strip's colours: the panel, its print, and the knob caps.
    // (The panel and its print follow the theme: the studio skin's console grey, or the flat skin's dark panel.)
    juce::Colour panelColour() { return theme::imageSkin() ? theme::stripFace : theme::panel; }
    juce::Colour headingBand() { return theme::imageSkin() ? theme::stripFace.darker (0.9f) : theme::toolbarDark; }
    juce::Colour headingText() { return theme::imageSkin() ? juce::Colours::white : theme::accent.brighter (0.3f); }
    const juce::Colour capRed { 0xffc23b30 }, capGreen { 0xff3d8a4c }, capBlue { 0xff2f5ea3 }, capBrown { 0xff4a3426 },
                       capBlack { 0xff1e1f21 }, capGrey { 0xff9c9fa2 }, capWhite { 0xffe8e6df };

    // Options shared by every strip window this session
    SpectrumAnalyzer::Options& analyzerOptions() { static SpectrumAnalyzer::Options o; return o; }

    // The rows of each section, by control name ("" leaves a gap).
    struct RowSpec { const char* section; std::vector<const char*> cells; };
    const std::vector<RowSpec>& layout()
    {
        static const std::vector<RowSpec> rows {
            { "INPUT / OUTPUT", { "Input", "Output", "EQ in", "DYN in" } },
            { "FILTERS",        { "HPF", "LPF" } },
            { "EQ",             { "HF", "HF Freq", "HF Bell" } },
            { "",               { "HMF", "HMF Freq", "HMF Q" } },
            { "",               { "LMF", "LMF Freq", "LMF Q" } },
            { "",               { "LF", "LF Freq", "LF Bell" } },
            { "COMPRESSOR",     { "Comp", "Ratio", "Release", "Fast" } },
            { "GATE / EXPANDER",{ "Threshold", "Range", "Gate Rel", "Exp" } },
        };
        return rows;
    }
}

ConsoleStripWindow::ConsoleStripWindow (juce::ValueTree t, juce::UndoManager& um,
                                        std::function<int (float*, int)> analyzerSource, std::function<double()> sampleRate)
    : track (t), undoManager (um), analyzer (std::move (analyzerSource), std::move (sampleRate))
{
    // Cap colours as on a classic console: red, green, blue and brown for the bands; grey filters; black dynamics; green gate.
    const auto white = capWhite, neon = capBlue, amber = capBlack, green = capGreen;

    addKnob (ids::consoleInput, "Input", -20.0, 20.0, 0.0, 0.0, db, capRed);
    addKnob (ids::consoleOutput, "Output", -20.0, 20.0, 0.0, 0.0, db, capGrey);
    addSwitch (ids::consoleIn, "EQ in", true);
    addSwitch (ids::consoleDynIn, "DYN in", true);

    // Filters: all the way down (HPF) or up (LPF) is off.
    auto& hpf = addKnob (ids::consoleHPF, "HPF", 16.0, 350.0, 16.0, 60.0, [] (double v) { return v <= 16.5 ? juce::String ("Off") : hz (v); }, white);
    hpf.toProperty = [] (double v) { return v <= 16.5 ? 0.0 : v; };
    hpf.fromProperty = [] (double v) { return v <= 0.0 ? 16.0 : v; };
    auto& lpf = addKnob (ids::consoleLPF, "LPF", 3000.0, 22000.0, 22000.0, 9000.0, [] (double v) { return v >= 21900.0 ? juce::String ("Off") : hz (v); }, white);
    lpf.toProperty = [] (double v) { return v >= 21900.0 ? 0.0 : v; };
    lpf.fromProperty = [] (double v) { return v <= 0.0 ? 22000.0 : v; };

    const auto g = (double) ConsoleChannel::maxGainDb;
    juce::ignoreUnused (neon);
    addKnob (ids::consoleHF, "HF", -g, g, 0.0, 0.0, db, capRed);
    addKnob (ids::consoleHFFreq, "HF Freq", 1500.0, 16000.0, 8000.0, 5000.0, hz, capRed);
    addSwitch (ids::consoleHFBell, "HF Bell", false);
    addKnob (ids::consoleHMF, "HMF", -g, g, 0.0, 0.0, db, capGreen);
    addKnob (ids::consoleHMFFreq, "HMF Freq", 600.0, 7000.0, 2500.0, 2000.0, hz, capGreen);
    addKnob (ids::consoleHMFQ, "HMF Q", 0.1, 4.0, 1.0, 1.0, [] (double v) { return juce::String (v, 2); }, capGreen);
    addKnob (ids::consoleLMF, "LMF", -g, g, 0.0, 0.0, db, capBlue);
    addKnob (ids::consoleLMFFreq, "LMF Freq", 200.0, 2500.0, 400.0, 700.0, hz, capBlue);
    addKnob (ids::consoleLMFQ, "LMF Q", 0.1, 4.0, 1.0, 1.0, [] (double v) { return juce::String (v, 2); }, capBlue);
    addKnob (ids::consoleLF, "LF", -g, g, 0.0, 0.0, db, capBrown);
    addKnob (ids::consoleLFFreq, "LF Freq", 30.0, 450.0, 100.0, 120.0, hz, capBrown);
    addSwitch (ids::consoleLFBell, "LF Bell", false);

    addKnob (ids::consoleDynamics, "Comp", 0.0, ConsoleChannel::maxDynamics, 0.0, 0.0, [] (double v) { return juce::String (v, 1); }, amber);
    auto& ratio = addKnob (ids::consoleCompRatio, "Ratio", 1.0, 20.0, 1.0, 4.0, [] (double v) { return v < 1.05 ? juce::String ("Auto") : juce::String (v, 1) + ":1"; }, amber);
    ratio.toProperty = [] (double v) { return v < 1.05 ? 0.0 : v; };
    ratio.fromProperty = [] (double v) { return v < 1.0 ? 1.0 : v; };
    addKnob (ids::consoleCompRelease, "Release", 0.05, 4.0, 0.15, 0.6, sec, amber);
    addSwitch (ids::consoleCompFast, "Fast", false);

    addKnob (ids::consoleGateThreshold, "Threshold", -80.0, 0.0, -80.0, 0.0, [] (double v) { return v <= -79.9 ? juce::String ("Off") : db (v); }, green);
    addKnob (ids::consoleGateRange, "Range", 0.0, 40.0, 20.0, 0.0, [] (double v) { return juce::String (v, 0) + " dB"; }, green);
    addKnob (ids::consoleGateRelease, "Gate Rel", 0.05, 4.0, 0.25, 0.6, sec, green);
    addSwitch (ids::consoleGateExpander, "Exp", false);

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

    int height = curveHeight + optionsHeight + 2 * margin + margin;
    for (const auto& row : layout())
        height += (juce::String (row.section).isNotEmpty() ? headerHeight : 0) + cellHeight;
    setSize (margin * 2 + cellWidth * 4, height + margin);
}

ConsoleStripWindow::~ConsoleStripWindow()
{
    track.removeListener (this);
}

ConsoleStripWindow::Control& ConsoleStripWindow::addKnob (const juce::Identifier& property, const juce::String& name, double min, double max,
                                                          double fallback, double skewMid, std::function<juce::String (double)> text,
                                                          juce::Colour ring)
{
    auto c = std::make_unique<Control>();
    c->property = property;
    c->name = name;
    c->text = std::move (text);
    c->toProperty = [] (double v) { return v; };
    c->fromProperty = [] (double v) { return v; };
    c->knob = std::make_unique<strip::PointerKnob> (ring);
    // The console look: a knob with a coloured cap (silver for the filters)
    c->knob->useSkin (ring == capWhite ? skin::knobWhite() : skin::knobBlack(), juce::Colours::white);
    if (ring != capWhite)
        c->knob->setCap (ring);
    c->knob->setRange (min, max, 0.0);
    if (skewMid > min && skewMid < max)
        c->knob->setSkewFactorFromMidPoint (skewMid);
    c->knob->setDoubleClickReturnValue (true, fallback);
    c->knob->setPopupDisplayEnabled (true, true, this);
    auto* raw = c.get();
    c->knob->textFromValueFunction = [raw] (double v) { return raw->text (v); };
    c->knob->setTooltip (name);
    c->knob->onDragStart = [this, name] { undoManager.beginNewTransaction ("Channel strip " + name); };
    c->knob->onValueChange = [this, raw]
    {
        track.setProperty (raw->property, raw->toProperty (raw->knob->getValue()), &undoManager);
    };
    c->knob->getProperties().set ("fallback", fallback);
    addAndMakeVisible (*c->knob);
    controls.push_back (std::move (c));
    return *controls.back();
}

void ConsoleStripWindow::addSwitch (const juce::Identifier& property, const juce::String& name, bool defaultOn)
{
    auto s = std::make_unique<Switch>();
    s->property = property;
    s->defaultOn = defaultOn;
    s->name = name;
    s->key = std::make_unique<strip::LampKey> (name, name.endsWith ("in") ? juce::Colour (0xff4fd07a) : juce::Colour (0xffffb22e));
    s->key->setTooltip (name);
    auto* raw = s.get();
    s->key->onClick = [this, raw]
    {
        undoManager.beginNewTransaction ("Channel strip " + raw->name);
        track.setProperty (raw->property, ! (bool) track.getProperty (raw->property, raw->defaultOn), &undoManager);
    };
    addAndMakeVisible (*s->key);
    switches.push_back (std::move (s));
}

void ConsoleStripWindow::refresh()
{
    for (auto& c : controls)
    {
        const auto fallback = (double) c->knob->getProperties()["fallback"];
        const auto stored = track.hasProperty (c->property) ? (double) track[c->property] : c->toProperty (fallback);
        c->knob->setValue (c->fromProperty (stored), juce::dontSendNotification);
    }
    for (auto& s : switches)
        s->key->setToggleState ((bool) track.getProperty (s->property, s->defaultOn), juce::dontSendNotification);
}

juce::Rectangle<int> ConsoleStripWindow::curveArea() const
{
    return { margin, margin, getWidth() - margin * 2, curveHeight };
}

juce::Rectangle<int> ConsoleStripWindow::optionsArea() const
{
    return { margin, curveArea().getBottom() + 4, getWidth() - margin * 2, optionsHeight - 4 };
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
    sections.clear();
    analyzer.setBounds (curveArea().reduced (2));
    {
        auto row = optionsArea();
        const auto w = row.getWidth() / 5;
        for (auto* box : { &modeBox, &fftBox, &rangeBox, &releaseBox, &holdBox })
            box->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    auto y = optionsArea().getBottom() + margin;

    for (const auto& row : layout())
    {
        if (juce::String (row.section).isNotEmpty())
        {
            sections.push_back ({ row.section, { margin, y, getWidth() - margin * 2, headerHeight } });
            y += headerHeight;
        }

        for (size_t i = 0; i < row.cells.size(); ++i)
        {
            const juce::Rectangle<int> cell (margin + (int) i * cellWidth, y, cellWidth, cellHeight);
            const juce::String name (row.cells[i]);

            for (auto& c : controls)
                if (c->name == name)
                {
                    c->knob->setBounds (cell.withSizeKeepingCentre (knobSize, knobSize).withY (cell.getY() + 4));
                    c->labelArea = cell.withTrimmedTop (knobSize + 6);
                }
            for (auto& s : switches)
                if (s->name == name)
                    s->key->setBounds (cell.withSizeKeepingCentre (40, 30).withY (cell.getY() + 12));
        }
        y += cellHeight;
    }
}

void ConsoleStripWindow::paint (juce::Graphics& g)
{
    // A classic console strip's grey panel (or the flat skin's panel)
    const auto print = theme::imageSkin() ? theme::stripInk : theme::text;
    g.fillAll (panelColour());
    if (theme::imageSkin())
    {
        juce::Random grain (7);
        for (int yy = 0; yy < getHeight(); yy += 2)
        {
            g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + grain.nextFloat() * 0.02f));
            g.fillRect (0, yy, getWidth(), 1);
        }
    }

    // The EQ curve, from 20 Hz to 20 kHz, +-18 dB.
    const auto area = curveArea().toFloat();
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);

    const auto xOf = [&area] (double f) { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
    const auto yOf = [&area] (double dbValue) { return area.getCentreY() - (float) (juce::jlimit (-18.0, 18.0, dbValue) / 18.0) * (area.getHeight() * 0.5f - 6.0f); };

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
    g.fillRect (area.getX(), yOf (0.0), area.getWidth(), 1.0f);

    // Section headings: white print on a dark band, as on the console
    for (const auto& s : sections)
    {
        g.setColour (headingBand());
        g.fillRect (s.area.withTrimmedBottom (3));
        g.setColour (headingText());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.name, s.area.withTrimmedBottom (3).withTrimmedLeft (6), juce::Justification::centredLeft, false);
    }

    // Each knob's printed scale, and its name and value
    for (const auto& c : controls)
    {
        const auto kb = c->knob->getBounds().toFloat();
        const auto centre = kb.getCentre();
        const auto radius = kb.getWidth() * 0.5f + 3.0f;
        g.setColour (print.withAlpha (theme::imageSkin() ? 0.75f : 0.0f));   // (the flat skin's knobs have their own ring)
        for (int i = 0; i <= 10 && theme::imageSkin(); ++i)
        {
            const auto a = juce::degreesToRadians (-140.0f + 28.0f * (float) i);
            const auto len = (i == 0 || i == 5 || i == 10) ? 5.0f : 3.0f;
            g.drawLine (centre.x + radius * std::sin (a), centre.y - radius * std::cos (a),
                        centre.x + (radius + len) * std::sin (a), centre.y - (radius + len) * std::cos (a), 1.0f);
        }
        g.setColour (print);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (c->name, c->labelArea.withHeight (14), juce::Justification::centred, false);
        g.setColour (print.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (c->text (c->knob->getValue()), c->labelArea.withHeight (14).translated (0, 14), juce::Justification::centred, false);
    }
    for (const auto& s : switches)
    {
        g.setColour (print);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s->name, s->key->getBounds().translated (0, 34).withHeight (14).expanded (20, 0), juce::Justification::centred, false);
    }

}

void ConsoleStripWindow::paintOverChildren (juce::Graphics& g)
{
    // The EQ curve over the analyzer
    const auto area = curveArea().toFloat();
    const auto xOf = [&area] (double f) { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
    const auto yOf = [&area] (double dbValue) { return area.getCentreY() - (float) (juce::jlimit (-18.0, 18.0, dbValue) / 18.0) * (area.getHeight() * 0.5f - 6.0f); };
    const auto settings = ConsoleChannel::fromTrack (track);
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const auto f = 20.0 * std::pow (1000.0, i / 200.0);
        const auto y = settings.enabled ? yOf (ConsoleChannel::responseDb (settings, f)) : yOf (0.0);
        if (i == 0) curve.startNewSubPath (xOf (f), y);
        else        curve.lineTo (xOf (f), y);
    }
    g.setColour (settings.enabled ? theme::neon : theme::dimText);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // The EQ's range on the left of the curve
    g.setFont (juce::FontOptions (9.0f));
    for (int d : { 12, 6, -6, -12 })
    {
        g.setColour (theme::neon.withAlpha (0.7f));
        g.drawText ((d > 0 ? "+" : "") + juce::String (d), juce::Rectangle<float> (area.getX() + 3.0f, yOf (d) - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft, false);
    }
}

} // namespace riff
