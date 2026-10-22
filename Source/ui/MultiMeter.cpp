#include "../engine/BandAnalysis.h"
#include "MultiMeter.h"
#include "Theme.h"

namespace riff
{

namespace
{
    juce::String dbText (double db, int decimals = 1)
    {
        return std::isfinite (db) && db > -99.0 ? juce::String (db, decimals) : juce::String::fromUTF8 ("-\xe2\x88\x9e");
    }

    // The display's colours, from the theme
    juce::Colour displayBack()  { return theme::background.darker (0.45f); }
    juce::Colour gridColour()   { return theme::outline.withAlpha (0.35f); }
    juce::Colour barColour()    { return theme::accent.withSaturation (juce::jmin (0.55f, theme::accent.getSaturation())).withBrightness (0.72f); }
    juce::Colour peakColour()   { return theme::meterAmber; }
    juce::Colour valueColour()  { return theme::meterGreen.brighter (0.35f); }

    /** The level meters' scale (Logic's): marks spaced evenly, so the top of the range has room. */
    const std::vector<float>& levelMarks()
    {
        static const std::vector<float> marks { 6, 3, 0, -3, -6, -9, -12, -16, -20, -24, -30, -40, -50, -60 };
        return marks;
    }

    float levelY (float db, juce::Rectangle<float> area)
    {
        const auto& m = levelMarks();
        const auto step = area.getHeight() / (float) (m.size() - 1);
        if (db >= m.front())
            return area.getY();
        for (size_t i = 1; i < m.size(); ++i)
            if (db >= m[i])
                return area.getY() + step * ((float) (i - 1) + (m[i - 1] - db) / (m[i - 1] - m[i]));
        return area.getBottom();
    }

    float returnRateFor (int index) { static const float rates[] { 3.0f, 6.0f, 11.8f, 20.0f, 40.0f }; return rates[juce::jlimit (0, 4, index)]; }
    float holdSecondsFor (int index) { static const float s[] { 2.0f, 4.0f, 6.0f, -1.0f }; return s[juce::jlimit (0, 3, index)]; }
}

MultiMeterPanel::MultiMeterPanel (AudioEngine& e) : engine (e)
{
    engine.setMasterTap (true);

    // The analyzer's window (Hann) and its power, to read a full-scale sine as 0 dB.
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        sum += (double) window[(size_t) i] * window[(size_t) i];
    }
    windowPower = (float) sum;

    auto setUpTab = [this] (juce::TextButton& b, bool gonio)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, gonio] { showGoniometer (gonio); };
        addChildComponent (b);   // (drawn by the panel, as Logic's big tabs; clicks land in mouseDown)
    };
    setUpTab (analyzerTab, false);
    setUpTab (goniometerTab, true);
    analyzerTab.setToggleState (true, juce::dontSendNotification);

    auto segment = [this] (juce::TextButton& b, const juce::String& text, int group, std::function<void()> onPick)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (group);
        b.setWantsKeyboardFocus (false);
        b.onClick = [&b, onPick] { if (b.getToggleState()) onPick(); };
        addAndMakeVisible (b);
    };
    const char* detectionNames[] { "Left", "Right", "LRmax", "Mono" };
    for (int i = 0; i < 4; ++i)
        segment (detectionButtons[i], detectionNames[i], 102, [this, i] { detection = (Detection) i; });
    detectionButtons[3].setToggleState (true, juce::dontSendNotification);
    const char* modeNames[] { "RMS Slow", "RMS Fast", "Peak" };
    for (int i = 0; i < 3; ++i)
        segment (modeButtons[i], modeNames[i], 103, [this, i] { mode = (Mode) i; });
    modeButtons[1].setToggleState (true, juce::dontSendNotification);   // RMS Fast: the balance of the mix, not its moment-to-moment swings

    auto combo = [this] (juce::ComboBox& box, const juce::StringArray& items, int selected, std::function<void (int)> onPick)
    {
        box.addItemList (items, 1);
        box.setSelectedItemIndex (selected, juce::dontSendNotification);
        box.onChange = [&box, onPick] { onPick (box.getSelectedItemIndex()); };
        box.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (box);
    };
    const juce::StringArray rates { "3 dB/s", "6 dB/s", "11.8 dB/s", "20 dB/s", "40 dB/s" };
    const juce::StringArray holds { "2 s", "4 s", "6 s", juce::String::fromUTF8 ("\xe2\x88\x9e") };
    combo (bandsBox, { "31", "63" }, 1, [this] (int i) { bands = i == 0 ? 31 : 63; bandDb.clear(); });
    combo (tiltBox, [] { auto names = bands::tiltNames(); for (auto& n : names) n = n.replace ("/oct", ""); return names; }(), bands::defaultTilt, [this] (int i) { tilt = bands::tiltSlope (i); repaint(); });
    tiltBox.setTooltip ("Tilt: lifts the bars by this much per octave above 1 kHz (and lowers them below). 4.5 dB/oct: a balanced "
                        "mix reads level across the range. 3 dB/oct: pink noise reads level. 0: as a plain FFT analyzer.");
    combo (analyzerReturnBox, rates, 2, [this] (int i) { analyzerReturn = returnRateFor (i); });
    combo (analyzerPeakBox, holds, 1, [this] (int i) { analyzerPeakSeconds = holdSecondsFor (i); });
    combo (levelBox, { "Peak", "RMS", "Peak & RMS" }, 2, [this] (int i) { levelMode = (LevelMode) i; });
    combo (levelPeakBox, holds, 0, [this] (int i) { levelPeakSeconds = holdSecondsFor (i); });
    combo (levelReturnBox, rates, 2, [this] (int i) { levelReturn = returnRateFor (i); });
    bandsBox.setTooltip ("Analyzer Bands: 31 (third octaves) or 63 (sixth octaves)");
    analyzerReturnBox.setTooltip ("Return Rate: how fast the bars fall back");
    analyzerPeakBox.setTooltip ("Peak: how long the peak marks stay");
    levelBox.setTooltip ("Level: the level meters show Peak, RMS, or both");

    for (auto* b : { &analyzerHoldButton, &levelHoldButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    analyzerHoldButton.setTooltip ("Hold: the analyzer's peak marks stay until Reset");
    levelHoldButton.setTooltip ("Hold: the level meters' peaks stay until Reset");
    analyzerHoldButton.onClick = [this] { analyzerHold = analyzerHoldButton.getToggleState(); };
    levelHoldButton.onClick = [this] { levelHold = levelHoldButton.getToggleState(); };

    analyzerResetButton.setWantsKeyboardFocus (false);
    analyzerResetButton.setTooltip ("Clears the analyzer's peak marks");
    analyzerResetButton.onClick = [this]
    {
        for (size_t i = 0; i < bandPeak.size(); ++i)
            bandPeak[i] = bandDb[i];
        repaint();
    };
    addAndMakeVisible (analyzerResetButton);
    levelResetButton.setWantsKeyboardFocus (false);
    levelResetButton.setTooltip ("Clears the held peaks, the Integrated loudness and the correlation's range");
    levelResetButton.onClick = [this]
    {
        heldPeak[0] = heldPeak[1] = -100.0f;
        loudness.reset();
        correlationLow = 1.0f;
        correlationHigh = -1.0f;
        repaint();
    };
    addAndMakeVisible (levelResetButton);

    showGoniometer (false);
    setSize (1000, 660);
    startTimerHz (30);
}

MultiMeterPanel::~MultiMeterPanel()
{
    engine.setMasterTap (false);
}

void MultiMeterPanel::showGoniometer (bool gonio)
{
    goniometer = gonio;
    analyzerTab.setToggleState (! gonio, juce::dontSendNotification);
    goniometerTab.setToggleState (gonio, juce::dontSendNotification);
    repaint();
}

void MultiMeterPanel::timerCallback()
{
    const auto rate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    if (std::abs (rate - preparedRate) > 0.5)
    {
        loudness.prepare (rate);
        preparedRate = rate;
    }
    const auto n = engine.readMaster (readL.data(), readR.data(), (int) readL.size());
    take (readL.data(), readR.data(), n);
}

void MultiMeterPanel::take (const float* l, const float* r, int n)
{
    if (preparedRate <= 0.0)
    {
        loudness.prepare (48000.0);
        preparedRate = 48000.0;
    }
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto dt = (float) juce::jlimit (0.005, 0.25, lastTime > 0.0 ? now - lastTime : 1.0 / 30.0);
    lastTime = now;

    if (n > 0)
    {
        loudness.process (l, r, n);

        // The analyzer's input: the latest samples
        for (int i = 0; i < n; ++i)
        {
            ringL[(size_t) ringPos] = l[i];
            ringR[(size_t) ringPos] = r[i];
            ringPos = (ringPos + 1) % fftSize;
        }

        // Level: peaks (falling at the return rate, held), RMS over about 300 ms
        const float* ch[2] { l, r };
        const auto blockSeconds = (double) n / preparedRate;
        const auto keep = std::exp (-blockSeconds / 0.3);
        for (int c = 0; c < 2; ++c)
        {
            float p = 0.0f;
            double ms = 0.0;
            for (int i = 0; i < n; ++i)
            {
                p = juce::jmax (p, std::abs (ch[c][i]));
                ms += (double) ch[c][i] * ch[c][i];
            }
            const auto pDb = juce::Decibels::gainToDecibels (p, -100.0f);
            peakDb[c] = juce::jmax (pDb, peakDb[c] - levelReturn * dt);
            rmsSquares[c] = rmsSquares[c] * keep + (1.0 - keep) * (ms / n);
            rmsDb[c] = (float) juce::Decibels::gainToDecibels (std::sqrt (rmsSquares[c]), -100.0);
            if (pDb >= heldPeak[c])
            {
                heldPeak[c] = pDb;
                heldFor[c] = 0.0f;
            }
            else if (! levelHold && levelPeakSeconds > 0.0f && (heldFor[c] += dt) > levelPeakSeconds)
                heldPeak[c] = juce::jmax (pDb, heldPeak[c] - levelReturn * dt);
        }

        // Correlation: +1 mono, 0 wide, -1 out of phase
        double sumLR = 0.0, sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sumLR += (double) l[i] * r[i];
            sumL += (double) l[i] * l[i];
            sumR += (double) r[i] * r[i];
        }
        if (sumL > 1.0e-9 && sumR > 1.0e-9)
        {
            correlation += ((float) (sumLR / std::sqrt (sumL * sumR)) - correlation) * 0.25f;
            correlationLow = juce::jmin (correlationLow, correlation);
            correlationHigh = juce::jmax (correlationHigh, correlation);
        }

        // The goniometer's dots: mid up, sides across (scaled to fill the display)
        float loudest = 0.0f;
        const auto step = juce::jmax (1, n / 1024);
        for (int i = 0; i < n; i += step)
        {
            scope[(size_t) scopePos] = { (r[i] - l[i]) * 0.7071f, (l[i] + r[i]) * 0.7071f };
            loudest = juce::jmax (loudest, std::abs (l[i]), std::abs (r[i]));
            scopePos = (scopePos + 1) % (int) scope.size();
        }
        const auto wanted = loudest > 1.0e-4f ? juce::jlimit (1.0f, 30.0f, 0.8f / loudest) : scopeGain;
        scopeGain += (wanted - scopeGain) * (wanted < scopeGain ? 0.5f : 0.05f);
    }
    else
        for (int c = 0; c < 2; ++c)
        {
            peakDb[c] = juce::jmax (-100.0f, peakDb[c] - levelReturn * dt);
            rmsSquares[c] *= 0.7;
            rmsDb[c] = (float) juce::Decibels::gainToDecibels (std::sqrt (rmsSquares[c]), -100.0);
        }

    analyse (dt);
    repaint();
}

void MultiMeterPanel::analyse (double seconds)
{
    const auto dt = (float) seconds;
    const auto rate = preparedRate > 0.0 ? preparedRate : 48000.0;
    if ((int) bandDb.size() != bands)
    {
        bandDb.assign ((size_t) bands, -100.0f);
        bandPower.assign ((size_t) bands, 0.0f);
        bandPeak.assign ((size_t) bands, -100.0f);
        bandHoldTime.assign ((size_t) bands, 0.0f);
    }

    // One spectrum (or two, for LRmax): the latest fftSize samples, windowed.
    std::vector<float> power ((size_t) bands, 0.0f);
    auto spectrum = [this, rate, &power] (int which)   // 0 left, 1 right, 2 mono
    {
        for (int i = 0; i < fftSize; ++i)
        {
            const auto k = (size_t) ((ringPos + i) % fftSize);
            const auto v = which == 0 ? ringL[k] : which == 1 ? ringR[k] : 0.5f * (ringL[k] + ringR[k]);
            fftData[(size_t) i] = v * window[(size_t) i];
        }
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // Bands log-spaced from 20 Hz to 20 kHz (equal octaves), each the spectrum integrated over exactly its range
        const auto binHz = rate / (double) fftSize;
        const auto norm = 4.0f / ((float) fftSize * windowPower);
        for (int k = 0; k < fftSize / 2; ++k)
            fftData[(size_t) k] = fftData[(size_t) k] * fftData[(size_t) k] * norm;
        std::vector<float> these;
        bands::integrate (fftData.data(), fftSize / 2, binHz, bands, these);
        for (size_t b = 0; b < (size_t) bands; ++b)
            power[b] = juce::jmax (power[b], these[b]);
    };
    switch (detection)
    {
        case Detection::left:  spectrum (0); break;
        case Detection::right: spectrum (1); break;
        case Detection::lrMax: spectrum (0); spectrum (1); break;
        case Detection::mono:  spectrum (2); break;
    }

    const auto tau = mode == Mode::rmsSlow ? 1.5f : 0.3f;
    const auto smooth = 1.0f - std::exp (-dt / tau);
    for (size_t b = 0; b < (size_t) bands; ++b)
    {
        float db;
        if (mode == Mode::peak)
            db = juce::jmax (10.0f * std::log10 (power[b] + 1.0e-12f), bandDb[b] - analyzerReturn * dt);
        else
        {
            bandPower[b] += (power[b] - bandPower[b]) * smooth;
            db = 10.0f * std::log10 (bandPower[b] + 1.0e-12f);
        }
        bandDb[b] = juce::jmax (-120.0f, db);

        // The peak marks: held, then falling at the return rate (unless Hold)
        if (bandDb[b] >= bandPeak[b])
        {
            bandPeak[b] = bandDb[b];
            bandHoldTime[b] = 0.0f;
        }
        else if (! analyzerHold && analyzerPeakSeconds > 0.0f && (bandHoldTime[b] += dt) > analyzerPeakSeconds)
            bandPeak[b] = juce::jmax (bandDb[b], bandPeak[b] - analyzerReturn * dt);
    }
}

//==============================================================================
void MultiMeterPanel::paintOverChildren (juce::Graphics&)
{
    if (std::exchange (entered, false))
        theme::leaveArea();
}

void MultiMeterPanel::paint (juce::Graphics& g)
{
    // In the MultiMeter's own theme, when it has one (Themes > Theme for: MultiMeter), until paintOverChildren
    theme::enterArea (theme::Area::meter);
    entered = true;
    g.fillAll (theme::panel);

    // The tabs (Logic's: the chosen one dark with its name lit)
    for (auto* tab : { &analyzerTab, &goniometerTab })
    {
        const auto on = tab->getToggleState();
        g.setColour (on ? theme::toolbarDark.darker (0.3f) : theme::toolbar.brighter (0.1f));
        g.fillRect (tab->getBounds());
        g.setColour (on ? valueColour() : theme::dimText);
        g.setFont (juce::FontOptions (28.0f));
        g.drawText (tab->getButtonText(), tab->getBounds(), juce::Justification::centred, false);
    }

    // The display, the meters and the correlation, on one dark face
    g.setColour (displayBack());
    g.fillRect (displayArea.getUnion (metersArea).getUnion (correlationArea));
    g.setColour (theme::outline.withAlpha (0.5f));
    g.drawVerticalLine (metersArea.getX() - 1, (float) displayArea.getY(), (float) correlationArea.getBottom());

    if (goniometer)
        paintGoniometer (g, displayArea);
    else
        paintAnalyzer (g, displayArea);
    paintMeters (g, metersArea);
    paintCorrelation (g, correlationArea);

    // The controls' captions
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (13.5f));
    auto caption = [&g] (const juce::String& text, const juce::Component& under)
    {
        g.drawText (text, under.getBounds().translated (0, -22).withHeight (20).expanded (30, 0), juce::Justification::centred, false);
    };
    {
        const auto all = detectionButtons[0].getBounds().getUnion (detectionButtons[3].getBounds());
        g.drawText ("Detection", all.translated (0, -22).withHeight (20), juce::Justification::centred, false);
        const auto modes = modeButtons[0].getBounds().getUnion (modeButtons[2].getBounds());
        g.drawText ("Mode", modes.translated (0, -22).withHeight (20), juce::Justification::centred, false);
    }
    caption ("Bands", bandsBox);
    caption ("Tilt", tiltBox);
    caption ("Return Rate", analyzerReturnBox);
    caption ("Peak", analyzerPeakBox);
    caption ("Level", levelBox);
    caption ("Peak", levelPeakBox);
    caption ("Return Rate", levelReturnBox);
    g.setColour (theme::outline.withAlpha (0.6f));
    g.drawVerticalLine (levelBox.getX() - 22, (float) controlsArea.getY() + 14.0f, (float) controlsArea.getBottom() - 14.0f);
}

void MultiMeterPanel::paintAnalyzer (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat().reduced (14.0f, 10.0f);
    auto header = r.removeFromTop (24.0f);
    auto bottom = r.removeFromBottom (22.0f);
    auto left = r.removeFromLeft (30.0f);

    // Top and Range (drag them up or down)
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (theme::dimText);
    g.drawText ("dB", left.withY (header.getY()).withHeight (header.getHeight()), juce::Justification::centredLeft, false);
    auto h = header.withTrimmedLeft (left.getWidth() + 10.0f);
    const auto topText = (top > 0.0f ? "+" : "") + juce::String ((int) top) + " dB";
    g.drawText ("Top:", h.removeFromLeft (38.0f), juce::Justification::centredLeft, false);
    g.setColour (valueColour());
    topLabel = h.removeFromLeft (64.0f).toNearestInt();
    g.drawText (topText, topLabel, juce::Justification::centredLeft, false);
    g.setColour (theme::dimText);
    g.drawText ("Range:", h.removeFromLeft (54.0f), juce::Justification::centredLeft, false);
    g.setColour (valueColour());
    rangeLabel = h.removeFromLeft (64.0f).toNearestInt();
    g.drawText (juce::String ((int) range) + " dB", rangeLabel, juce::Justification::centredLeft, false);

    const auto yOf = [r, this] (float db) { return r.getY() + r.getHeight() * (top - db) / range; };
    // Every octave the same width: 20 - 200 Hz as wide as 2 - 20 kHz
    const auto xOf = [r] (float hz) { return r.getX() + r.getWidth() * (float) bands::position (hz); };

    // The grid: dB every 5, frequencies by octave
    g.setFont (juce::FontOptions (12.0f));
    const auto stepDb = range > 60.0f ? 10 : 5;
    for (int db = (int) std::floor (top / (float) stepDb) * stepDb; (float) db >= top - range - 0.1f; db -= stepDb)
    {
        const auto y = yOf ((float) db);
        g.setColour (db == 0 ? theme::dimText.withAlpha (0.55f) : gridColour());
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (theme::dimText);
        g.drawText (juce::String (db), left.withY (y - 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
    }
    for (const auto hz : { 20.0f, 40.0f, 80.0f, 160.0f, 315.0f, 630.0f, 1250.0f, 2500.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = xOf (hz);
        g.setColour (gridColour());
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (theme::dimText);
        const auto label = hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f && std::fmod (hz, 1000.0f) > 0.0f ? 1 : 0) + " k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 24.0f, bottom.getY() + 2.0f, 48.0f, 18.0f), juce::Justification::centred, false);
    }
    g.drawText ("Hz", bottom.withLeft (r.getRight() - 4.0f).withWidth (30.0f), juce::Justification::centredLeft, false);

    // The bars, and their peak marks
    if (bandDb.empty())
        return;
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (r.toNearestInt());
    for (int b = 0; b < bands; ++b)
    {
        const auto x0 = xOf ((float) bands::edge (b, bands));
        const auto x1 = xOf ((float) bands::edge (b + 1, bands));
        const auto lift = (float) bands::displayOffsetDb (bands::centre (b, bands), tilt);
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (x0 + 1.0f, juce::jmax (r.getY(), yOf (bandDb[(size_t) b] + lift)), x1 - 1.0f, r.getBottom());
        g.setColour (barColour());
        g.fillRect (bar);
        if (bandPeak[(size_t) b] > bandDb[(size_t) b] + 0.5f)
        {
            g.setColour (peakColour().withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x0 + 1.0f, yOf (bandPeak[(size_t) b] + lift) - 1.0f, x1 - x0 - 2.0f, 2.0f));
        }
    }
}

void MultiMeterPanel::paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat().reduced (20.0f);
    const auto size = juce::jmin (r.getWidth(), r.getHeight());
    const auto square = r.withSizeKeepingCentre (size, size);
    const auto c = square.getCentre();

    // The axes: L and R on the diagonals, M up, S across
    g.setColour (gridColour());
    g.drawLine (square.getX(), c.y, square.getRight(), c.y, 1.0f);
    g.drawLine (c.x, square.getY(), c.x, square.getBottom(), 1.0f);
    const auto half = size * 0.5f;
    g.setColour (gridColour().withAlpha (0.6f));
    g.drawLine (c.x - half * 0.7071f, c.y - half * 0.7071f, c.x + half * 0.7071f, c.y + half * 0.7071f, 1.0f);
    g.drawLine (c.x + half * 0.7071f, c.y - half * 0.7071f, c.x - half * 0.7071f, c.y + half * 0.7071f, 1.0f);
    g.drawEllipse (square.reduced (2.0f), 1.0f);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("L", juce::Rectangle<float> (24.0f, 18.0f).withCentre ({ c.x - half * 0.74f, c.y - half * 0.74f }), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (24.0f, 18.0f).withCentre ({ c.x + half * 0.74f, c.y - half * 0.74f }), juce::Justification::centred, false);
    g.drawText ("M", juce::Rectangle<float> (24.0f, 18.0f).withCentre ({ c.x, square.getY() + 10.0f }), juce::Justification::centred, false);
    g.drawText ("S", juce::Rectangle<float> (24.0f, 18.0f).withCentre ({ square.getRight() - 10.0f, c.y - 12.0f }), juce::Justification::centred, false);

    // The dots, the newest brightest
    const auto count = (int) scope.size();
    for (int i = 0; i < count; ++i)
    {
        const auto& p = scope[(size_t) ((scopePos + i) % count)];
        const auto age = (float) i / (float) count;
        const auto x = c.x + p.x * half * scopeGain, y = c.y - p.y * half * scopeGain;
        if (! square.expanded (2.0f).contains (x, y))
            continue;
        g.setColour (valueColour().withAlpha (0.08f + 0.6f * age));
        g.fillRect (x, y, 1.6f, 1.6f);
    }
}

void MultiMeterPanel::paintMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat().reduced (10.0f, 12.0f);
    auto lufsColumn = r.removeFromLeft (r.getWidth() * 0.36f);
    r.removeFromLeft (8.0f);
    auto lrColumn = r;

    // Headers and readouts
    g.setFont (juce::FontOptions (15.0f));
    g.setColour (theme::text);
    g.drawText ("LUFS", lufsColumn.removeFromTop (20.0f), juce::Justification::centred, false);
    auto lrHeader = lrColumn.removeFromTop (20.0f);
    lrHeader.removeFromLeft (26.0f);
    g.drawText ("L", lrHeader.removeFromLeft (lrHeader.getWidth() * 0.5f), juce::Justification::centred, false);
    g.drawText ("R", lrHeader, juce::Justification::centred, false);

    g.setFont (juce::FontOptions (12.0f));
    auto readout = [&g] (juce::Rectangle<float> row, const juce::String& name, const juce::String& value)
    {
        g.setColour (theme::dimText);
        g.drawText (name, row, juce::Justification::centredLeft, false);
        g.setColour (peakColour());
        g.drawText (value, row, juce::Justification::centredRight, false);
    };
    readout (lufsColumn.removeFromTop (17.0f), "LU-I", dbText (loudness.integrated()));
    readout (lufsColumn.removeFromTop (17.0f), "LU-S", dbText (loudness.shortTerm()));
    {
        auto peakRow = lrColumn.removeFromTop (17.0f), rmsRow = lrColumn.removeFromTop (17.0f);
        for (auto [row, name, values] : { std::tuple { peakRow, "Peak", std::pair { heldPeak[0], heldPeak[1] } },
                                          std::tuple { rmsRow, "RMS", std::pair { rmsDb[0], rmsDb[1] } } })
        {
            auto rr = row;
            rr.removeFromLeft (26.0f);
            const auto w = rr.getWidth() / 3.0f;
            g.setColour (peakColour());
            g.drawText (dbText (values.first), rr.removeFromLeft (w), juce::Justification::centred, false);
            g.setColour (theme::dimText);
            g.drawText (name, rr.removeFromLeft (w), juce::Justification::centred, false);
            g.setColour (peakColour());
            g.drawText (dbText (values.second), rr, juce::Justification::centred, false);
        }
    }
    lufsColumn.removeFromTop (12.0f);
    lrColumn.removeFromTop (12.0f);

    // The scale, between the LUFS bar and L/R
    auto scaleArea = lrColumn.removeFromLeft (26.0f);
    const auto meterArea = lrColumn.withTop (scaleArea.getY());
    g.setFont (juce::FontOptions (11.5f));
    for (const auto m : levelMarks())
    {
        const auto y = levelY (m, meterArea);
        g.setColour (theme::dimText);
        g.drawText (juce::String ((int) m), scaleArea.withY (y - 7.0f).withHeight (14.0f), juce::Justification::centredRight, false);
        g.setColour (gridColour().withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, meterArea.getX(), meterArea.getRight());
        g.drawHorizontalLine ((int) y, lufsColumn.getX(), lufsColumn.getRight());
    }
    g.setColour (theme::dimText);
    g.drawText ("dB", scaleArea.withY (scaleArea.getY() - 18.0f).withHeight (14.0f), juce::Justification::centredRight, false);

    // LUFS: the short-term loudness (blue), the momentary above it (yellow); the Integrated marked
    {
        const auto bar = lufsColumn.withSizeKeepingCentre (juce::jmin (26.0f, lufsColumn.getWidth() * 0.45f), meterArea.getHeight()).withY (meterArea.getY());
        const auto shortTerm = (float) loudness.shortTerm(), momentary = (float) loudness.momentary(), integrated = (float) loudness.integrated();
        if (std::isfinite (shortTerm) && shortTerm > -60.0f)
        {
            g.setColour (barColour());
            g.fillRect (bar.withTop (levelY (shortTerm, meterArea)));
            if (std::isfinite (momentary) && momentary > shortTerm)
            {
                g.setColour (peakColour());
                g.fillRect (bar.withTop (levelY (momentary, meterArea)).withBottom (levelY (shortTerm, meterArea)));
            }
        }
        if (std::isfinite (integrated) && integrated > -60.0f)
        {
            const auto y = levelY (integrated, meterArea);
            g.setColour (peakColour());
            juce::Path arrow;
            arrow.addTriangle (bar.getX() - 9.0f, y - 4.0f, bar.getX() - 9.0f, y + 4.0f, bar.getX() - 3.0f, y);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
            g.setColour (peakColour().withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, bar.getX() - 3.0f, bar.getRight() + 4.0f);
        }
    }

    // L and R: Peak (yellow above the RMS), RMS (blue), the held peak marked
    const auto channelWidth = meterArea.getWidth() * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        const auto column = juce::Rectangle<float> (meterArea.getX() + channelWidth * (float) c, meterArea.getY(), channelWidth, meterArea.getHeight());
        const auto barW = juce::jmin (12.0f, channelWidth * 0.28f);
        const auto both = levelMode == LevelMode::peakAndRms;
        auto peakBar = column.withSizeKeepingCentre (both ? barW : barW * 1.6f, column.getHeight()).translated (both ? -barW * 0.6f : 0.0f, 0.0f);
        auto rmsBar = peakBar.translated (both ? barW * 1.2f : 0.0f, 0.0f);

        if (levelMode != LevelMode::rms)
        {
            const auto peakY = levelY (peakDb[c], meterArea), rmsY = levelY (rmsDb[c], meterArea);
            g.setColour (barColour());
            g.fillRect (peakBar.withTop (juce::jmax (peakY, rmsY)));
            g.setColour (peakColour());
            g.fillRect (peakBar.withTop (peakY).withBottom (juce::jmax (peakY, rmsY)));
            if (heldPeak[c] > -60.0f)
                g.fillRect (peakBar.withTop (levelY (heldPeak[c], meterArea) - 1.0f).withHeight (2.0f));
        }
        if (levelMode != LevelMode::peak)
        {
            g.setColour (barColour());
            g.fillRect (rmsBar.withTop (levelY (rmsDb[c], meterArea)));
        }
    }
}

void MultiMeterPanel::paintCorrelation (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat().reduced (12.0f, 6.0f);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("CORRELATION", r.removeFromTop (18.0f), juce::Justification::centred, false);
    const auto line = r.withSizeKeepingCentre (r.getWidth(), 3.0f);
    const auto xOf = [line] (float v) { return line.getX() + line.getWidth() * (juce::jlimit (-1.0f, 1.0f, v) + 1.0f) * 0.5f; };
    g.setColour (theme::meterRed.withAlpha (0.8f));
    g.fillRect (line.withRight (line.getCentreX()));
    g.setColour (theme::meterGreen.withAlpha (0.8f));
    g.fillRect (line.withLeft (line.getCentreX()));
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (theme::dimText);
    g.drawText ("-1", line.withY (line.getBottom() + 3.0f).withHeight (12.0f), juce::Justification::centredLeft, false);
    g.drawText ("0", line.withY (line.getBottom() + 3.0f).withHeight (12.0f), juce::Justification::centred, false);
    g.drawText ("+1", line.withY (line.getBottom() + 3.0f).withHeight (12.0f), juce::Justification::centredRight, false);
    if (correlationHigh >= correlationLow)
    {
        g.setColour (theme::meterRed);
        g.fillRect (juce::Rectangle<float> (xOf (correlationLow) - 1.0f, line.getY() - 9.0f, 2.0f, 9.0f));
        g.setColour (theme::meterGreen);
        g.fillRect (juce::Rectangle<float> (xOf (correlationHigh) - 1.0f, line.getY() - 9.0f, 2.0f, 9.0f));
    }
    g.setColour (theme::text);
    g.fillRect (juce::Rectangle<float> (xOf (correlation) - 1.5f, line.getY() - 12.0f, 3.0f, 15.0f));
}

void MultiMeterPanel::resized()
{
    const theme::ScopedArea scope (theme::Area::meter);   // (its controls take the MultiMeter's own theme's colours)
    auto area = getLocalBounds();
    auto tabs = area.removeFromTop (54);
    analyzerTab.setBounds (tabs.removeFromLeft ((int) (tabs.getWidth() * 0.5f)));
    goniometerTab.setBounds (tabs);
    for (auto* tab : { &analyzerTab, &goniometerTab })
    {
        tab->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        tab->setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        tab->setColour (juce::TextButton::textColourOnId, valueColour());
        tab->setColour (juce::TextButton::textColourOffId, theme::dimText);
    }

    controlsArea = area.removeFromBottom (190);
    auto face = area;
    metersArea = face.removeFromRight (juce::jmax (240, face.getWidth() / 4));
    correlationArea = metersArea.removeFromBottom (74);
    displayArea = face;

    // The controls
    auto c = controlsArea.reduced (30, 22);
    auto levelPart = c.removeFromRight ((int) (c.getWidth() * 0.34f));
    c.removeFromRight (20);
    const auto rowH = 32;
    auto segments = c.removeFromLeft ((int) (c.getWidth() * 0.5f));
    {
        auto row = segments.removeFromTop (rowH + 24).withTrimmedTop (24);
        const auto w = juce::jmin (100, row.getWidth() / 4);
        row = row.withSizeKeepingCentre (w * 4, rowH);
        for (auto& b : detectionButtons)
            b.setBounds (row.removeFromLeft (w));
        segments.removeFromTop (16);
        auto row2 = segments.removeFromTop (rowH + 24).withTrimmedTop (24);
        row2 = row2.withSizeKeepingCentre (w * 3, rowH);
        for (auto& b : modeButtons)
            b.setBounds (row2.removeFromLeft (w));
    }
    auto middle = c.removeFromLeft ((int) (c.getWidth() * 0.55f));
    {
        auto row = middle.removeFromTop (rowH + 24).withTrimmedTop (24);
        auto pair = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), 210), rowH);
        bandsBox.setBounds (pair.removeFromLeft (80));
        pair.removeFromLeft (10);
        tiltBox.setBounds (pair);
    }
    middle.removeFromTop (16);
    analyzerReturnBox.setBounds (middle.removeFromTop (rowH + 24).withTrimmedTop (24).withSizeKeepingCentre (150, rowH));
    analyzerPeakBox.setBounds (c.removeFromTop (rowH + 24).withTrimmedTop (24).withSizeKeepingCentre (100, rowH));
    c.removeFromTop (6);
    analyzerHoldButton.setBounds (c.removeFromTop (rowH).withSizeKeepingCentre (96, rowH - 2));
    c.removeFromTop (14);
    analyzerResetButton.setBounds (c.removeFromTop (rowH).withSizeKeepingCentre (96, rowH - 2));

    auto lv = levelPart.removeFromLeft ((int) (levelPart.getWidth() * 0.6f));
    levelBox.setBounds (lv.removeFromTop (rowH + 24).withTrimmedTop (24).withSizeKeepingCentre (160, rowH));
    lv.removeFromTop (16);
    levelReturnBox.setBounds (lv.removeFromTop (rowH + 24).withTrimmedTop (24).withSizeKeepingCentre (150, rowH));
    levelPeakBox.setBounds (levelPart.removeFromTop (rowH + 24).withTrimmedTop (24).withSizeKeepingCentre (90, rowH));
    levelPart.removeFromTop (6);
    levelHoldButton.setBounds (levelPart.removeFromTop (rowH).withSizeKeepingCentre (90, rowH - 2));
    levelPart.removeFromTop (14);
    levelResetButton.setBounds (levelPart.removeFromTop (rowH).withSizeKeepingCentre (90, rowH - 2));

    // Segmented keys and the readouts take the theme's colours
    for (auto* b : { &detectionButtons[0], &detectionButtons[1], &detectionButtons[2], &detectionButtons[3],
                     &modeButtons[0], &modeButtons[1], &modeButtons[2], &analyzerHoldButton, &levelHoldButton,
                     &analyzerResetButton, &levelResetButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::toolbar.brighter (0.08f));
        b->setColour (juce::TextButton::buttonOnColourId, displayBack());
        b->setColour (juce::TextButton::textColourOffId, theme::text.withAlpha (0.85f));
        b->setColour (juce::TextButton::textColourOnId, valueColour());
    }
    for (auto* box : { &bandsBox, &tiltBox, &analyzerReturnBox, &analyzerPeakBox, &levelBox, &levelPeakBox, &levelReturnBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        box->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        box->setColour (juce::ComboBox::textColourId, valueColour());
        box->setColour (juce::ComboBox::arrowColourId, valueColour());
    }
}

void MultiMeterPanel::mouseDown (const juce::MouseEvent& e)
{
    if (analyzerTab.getBounds().contains (e.getPosition()) || goniometerTab.getBounds().contains (e.getPosition()))
    {
        showGoniometer (goniometerTab.getBounds().contains (e.getPosition()));
        dragging = 0;
        return;
    }
    dragging = goniometer ? 0 : topLabel.contains (e.getPosition()) ? 1 : rangeLabel.contains (e.getPosition()) ? 2 : 0;
    dragStartValue = dragging == 1 ? top : range;
}

void MultiMeterPanel::mouseDrag (const juce::MouseEvent& e)
{
    // Drag Top or Range up and down (in 5 dB steps), as in Logic
    if (dragging == 0)
        return;
    const auto steps = (float) juce::roundToInt ((float) -e.getDistanceFromDragStartY() / 8.0f) * 5.0f;
    if (dragging == 1)
        top = juce::jlimit (-40.0f, 20.0f, dragStartValue + steps);
    else
        range = juce::jlimit (20.0f, 120.0f, dragStartValue + steps);
    repaint();
}

} // namespace riff
