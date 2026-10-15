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
}

MultiMeterPanel::MultiMeterPanel (AudioEngine& e) : engine (e)
{
    engine.setMasterTap (true);

    analyzer = std::make_unique<SpectrumAnalyzer> ([this] (float* dest, int max)
    {
        const auto scope2 = spectrumFifo.read (juce::jmin (max, spectrumFifo.getNumReady()));
        if (scope2.blockSize1 > 0) std::copy_n (spectrumRing.data() + scope2.startIndex1, scope2.blockSize1, dest);
        if (scope2.blockSize2 > 0) std::copy_n (spectrumRing.data() + scope2.startIndex2, scope2.blockSize2, dest + scope2.blockSize1);
        return scope2.blockSize1 + scope2.blockSize2;
    }, [this] { return engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0; });
    addAndMakeVisible (*analyzer);

    resetButton.setTooltip ("Start measuring the Integrated loudness, range and clips again");
    resetButton.onClick = [this]
    {
        loudness.reset();
        clips = 0;
        std::fill (history.begin(), history.end(), -100.0f);
        peakHold[0] = peakHold[1] = -100.0f;
        repaint();
    };
    addAndMakeVisible (resetButton);

    targetBox.addItemList ({ "-14 LUFS (streaming)", "-16 LUFS (Apple)", "-23 LUFS (EBU R128)", "-24 LUFS (ATSC)", "-9 LUFS (loud master)" }, 1);
    targetBox.setSelectedId (1, juce::dontSendNotification);
    targetBox.setTooltip ("The loudness you're aiming for (a line in the history)");
    targetBox.onChange = [this]
    {
        static const float targets[] { -14.0f, -16.0f, -23.0f, -24.0f, -9.0f };
        target = targets[juce::jlimit (0, 4, targetBox.getSelectedItemIndex())];
        repaint();
    };
    addAndMakeVisible (targetBox);

    setSize (860, 520);
    startTimerHz (30);
}

MultiMeterPanel::~MultiMeterPanel()
{
    engine.setMasterTap (false);
}

void MultiMeterPanel::timerCallback()
{
    const auto rate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    if (std::abs (rate - preparedRate) > 0.5)
    {
        loudness.prepare (rate);
        preparedRate = rate;
    }
    const auto n = engine.readMaster (left.data(), right.data(), (int) left.size());
    take (left.data(), right.data(), n);
}

void MultiMeterPanel::take (const float* l, const float* r, int n)
{
    if (preparedRate <= 0.0)
    {
        loudness.prepare (48000.0);
        preparedRate = 48000.0;
    }
    if (n > 0)
    {
        loudness.process (l, r, n);

        // Peaks (held for two seconds) and RMS (smoothed)
        const float* ch[2] { l, r };
        double sumLR = 0.0, sumL = 0.0, sumR = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            float p = 0.0f;
            double ms = 0.0;
            for (int i = 0; i < n; ++i)
            {
                p = juce::jmax (p, std::abs (ch[c][i]));
                ms += (double) ch[c][i] * ch[c][i];
            }
            if (p >= 1.0f)
                ++clips;
            const auto pDb = juce::Decibels::gainToDecibels (p, -100.0f);
            peak[c] = juce::jmax (pDb, peak[c] - 1.2f);
            if (pDb >= peakHold[c] || ++holdFrames[c] > 60)
            {
                peakHold[c] = pDb;
                holdFrames[c] = 0;
            }
            const auto rDb = (float) juce::Decibels::gainToDecibels (std::sqrt (ms / n), -100.0);
            rms[c] = rms[c] + (rDb - rms[c]) * 0.25f;
        }
        for (int i = 0; i < n; ++i)
        {
            sumLR += (double) l[i] * r[i];
            sumL += (double) l[i] * l[i];
            sumR += (double) r[i] * r[i];
        }
        if (sumL > 1.0e-9 && sumR > 1.0e-9)
            correlation += ((float) (sumLR / std::sqrt (sumL * sumR)) - correlation) * 0.2f;

        // The goniometer's dots: mid up, sides across
        const auto step = juce::jmax (1, n / 512);
        for (int i = 0; i < n; i += step)
        {
            scope[(size_t) scopePos] = { (l[i] - r[i]) * 0.7071f, (l[i] + r[i]) * 0.7071f };
            scopePos = (scopePos + 1) % (int) scope.size();
        }

        // The analyzer's mono feed
        const auto w = spectrumFifo.write (juce::jmin (n, spectrumFifo.getFreeSpace()));
        int done = 0;
        for (auto [start, size] : { std::pair { w.startIndex1, w.blockSize1 }, std::pair { w.startIndex2, w.blockSize2 } })
        {
            for (int i = 0; i < size; ++i)
                spectrumRing[(size_t) (start + i)] = 0.5f * (l[done + i] + r[done + i]);
            done += size;
        }
    }
    else
        for (int c = 0; c < 2; ++c)
        {
            peak[c] = juce::jmax (-100.0f, peak[c] - 1.2f);
            rms[c] = juce::jmax (-100.0f, rms[c] - 1.0f);
        }

    history[(size_t) historyPos] = (float) juce::jmax (-100.0, loudness.shortTerm());
    historyPos = (historyPos + 1) % (int) history.size();
    repaint (levelArea.getUnion (loudnessArea).getUnion (scopeArea));
}

void MultiMeterPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel.darker (0.25f));
    paintLevels (g, levelArea);
    paintLoudness (g, loudnessArea);
    paintGoniometer (g, scopeArea);

    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("ANALYZER", analyzer->getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft, false);
}

void MultiMeterPanel::paintLevels (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("LEVEL", area.removeFromTop (16), juce::Justification::centredLeft, false);

    auto numbers = area.removeFromBottom (54);
    const auto yOf = [&area] (float db) { return (float) area.getBottom() - juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, (float) area.getHeight()); };

    // The scale
    g.setFont (juce::FontOptions (10.0f));
    for (int db = 0; db >= -60; db -= 6)
    {
        g.setColour (theme::dimText.withAlpha (0.8f));
        g.drawText (juce::String (db), area.getX(), (int) yOf ((float) db) - 6, 22, 12, juce::Justification::centredRight, false);
        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) yOf ((float) db), (float) area.getX() + 26.0f, (float) area.getRight());
    }

    auto bars = area.withTrimmedLeft (30);
    const auto barWidth = (bars.getWidth() - 8) / 2;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = bars.removeFromLeft (barWidth).toFloat();
        bars.removeFromLeft (8);
        g.setColour (juce::Colour (0xff111214));
        g.fillRect (bar);
        // Peak: green, then amber over -12, red over -3; RMS as a brighter inner bar
        const auto top = yOf (peak[c]);
        juce::ColourGradient grad (theme::meterRed, 0.0f, yOf (0.0f), theme::meterGreen, 0.0f, yOf (-60.0f), false);
        grad.addColour (juce::jmap (3.0 / 60.0, 0.0, 1.0), theme::meterAmber);
        grad.addColour (juce::jmap (12.0 / 60.0, 0.0, 1.0), theme::meterGreen);
        g.setGradientFill (grad);
        g.fillRect (bar.withTop (top).reduced (2.0f, 0.0f));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (bar.withTop (yOf (rms[c])).reduced (bar.getWidth() * 0.3f, 0.0f));
        g.setColour (juce::Colours::white);
        g.fillRect (bar.getX(), yOf (peakHold[c]) - 1.0f, bar.getWidth(), 2.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (c == 0 ? "L" : "R", bar.withY (bar.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centred, false);
    }

    numbers.removeFromTop (16);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (theme::text);
    g.drawText ("Peak  " + dbText (juce::jmax (peakHold[0], peakHold[1])) + " dB", numbers.removeFromTop (18), juce::Justification::centredLeft, false);
    g.setColour (clips > 0 ? theme::meterRed : theme::dimText);
    g.drawText ("Clips " + juce::String (clips), numbers, juce::Justification::centredLeft, false);
}

void MultiMeterPanel::paintLoudness (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("LOUDNESS (LUFS)", area.removeFromTop (16), juce::Justification::centredLeft, false);

    // The numbers
    auto boxes = area.removeFromTop (70);
    const auto w = boxes.getWidth() / 4;
    const std::pair<const char*, juce::String> values[] {
        { "Momentary", dbText (loudness.momentary()) }, { "Short-term", dbText (loudness.shortTerm()) },
        { "Integrated", dbText (loudness.integrated()) }, { "Range (LU)", juce::String (loudness.range(), 1) },
    };
    for (const auto& [name, value] : values)
    {
        auto box = boxes.removeFromLeft (w).reduced (3);
        g.setColour (juce::Colour (0xff141518));
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (name, box.removeFromTop (20), juce::Justification::centred, false);
        g.setColour (juce::String (name) == "Integrated" ? theme::accent : theme::text);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (value, box, juce::Justification::centred, false);
    }

    // The history: short-term loudness, with the target line
    area.removeFromTop (8);
    auto plot = area.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (plot, 4.0f);
    const auto yOf = [&plot] (float lufs) { return plot.getBottom() - juce::jmap (juce::jlimit (-40.0f, 0.0f, lufs), -40.0f, 0.0f, 0.0f, plot.getHeight()); };
    g.setFont (juce::FontOptions (10.0f));
    for (int l = 0; l >= -40; l -= 10)
    {
        g.setColour (theme::outline.withAlpha (0.4f));
        g.drawHorizontalLine ((int) yOf ((float) l), plot.getX(), plot.getRight());
        g.setColour (theme::dimText);
        g.drawText (juce::String (l), (int) plot.getX() + 3, (int) yOf ((float) l) - 12, 30, 12, juce::Justification::centredLeft, false);
    }
    g.setColour (theme::accent.withAlpha (0.8f));
    g.drawHorizontalLine ((int) yOf (target), plot.getX(), plot.getRight());

    juce::Path line;
    const auto count = (int) history.size();
    bool started = false;
    for (int i = 0; i < count; ++i)
    {
        const auto v = history[(size_t) ((historyPos + i) % count)];
        const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);
        if (v <= -99.0f)
        {
            started = false;
            continue;
        }
        if (! started) line.startNewSubPath (x, yOf (v)); else line.lineTo (x, yOf (v));
        started = true;
    }
    g.setColour (theme::meterGreen);
    g.strokePath (line, juce::PathStrokeType (1.6f));
}

void MultiMeterPanel::paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("GONIOMETER", area.removeFromTop (16), juce::Justification::centredLeft, false);

    auto corr = area.removeFromBottom (34);
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size).toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillEllipse (box);
    g.setColour (theme::outline.withAlpha (0.6f));
    g.drawEllipse (box, 1.0f);
    g.drawLine (box.getCentreX(), box.getY(), box.getCentreX(), box.getBottom(), 1.0f);
    g.drawLine (box.getX(), box.getCentreY(), box.getRight(), box.getCentreY(), 1.0f);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("M", box.withHeight (14.0f), juce::Justification::centredTop, false);
    g.drawText ("L", box.getX() + size * 0.12f, box.getY() + size * 0.12f, 12.0f, 12.0f, juce::Justification::centred, false);
    g.drawText ("R", box.getRight() - size * 0.12f - 12.0f, box.getY() + size * 0.12f, 12.0f, 12.0f, juce::Justification::centred, false);

    // The dots, rotated so the middle is up (scaled to the loudest)
    const auto radius = size * 0.5f;
    g.setColour (theme::meterGreen.withAlpha (0.55f));
    for (const auto& p : scope)
    {
        const auto x = box.getCentreX() + juce::jlimit (-1.0f, 1.0f, p.x) * radius;
        const auto y = box.getCentreY() - juce::jlimit (-1.0f, 1.0f, p.y) * radius;
        g.fillRect (x, y, 1.5f, 1.5f);
    }

    // Correlation: -1 (out of phase) .. +1 (mono)
    corr.removeFromTop (8);
    auto bar = corr.removeFromTop (12).toFloat().reduced (10.0f, 0.0f);
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (bar, 3.0f);
    const auto x = bar.getCentreX() + correlation * bar.getWidth() * 0.5f;
    g.setColour (correlation < 0.0f ? theme::meterRed : theme::meterGreen);
    g.fillRect (juce::jmin (x, bar.getCentreX()), bar.getY(), std::abs (x - bar.getCentreX()), bar.getHeight());
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("-1", bar.withWidth (14.0f).translated (-14.0f, 0.0f), juce::Justification::centred, false);
    g.drawText ("+1", bar.withX (bar.getRight()).withWidth (16.0f), juce::Justification::centred, false);
    g.drawText ("Correlation " + juce::String (correlation, 2), corr.toFloat(), juce::Justification::centred, false);
}

void MultiMeterPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (290);
    levelArea = top.removeFromLeft (150);
    top.removeFromLeft (14);
    scopeArea = top.removeFromRight (230);
    top.removeFromRight (14);
    loudnessArea = top;

    auto controls = r.removeFromTop (34).withTrimmedTop (6);
    resetButton.setBounds (controls.removeFromRight (80));
    controls.removeFromRight (8);
    targetBox.setBounds (controls.removeFromRight (190));

    r.removeFromTop (20);
    analyzer->setBounds (r);
}

} // namespace riff
