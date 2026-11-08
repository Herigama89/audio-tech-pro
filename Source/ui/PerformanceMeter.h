#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/** What the Performance meter shows (filled in by the main window from the engine). */
struct PerformanceReading
{
    float cpu = 0.0f;        // share of the time the audio took, averaged over the last moment (0..1+)
    float peakNow = 0.0f;    // the heaviest single block in that moment
    float peak = 0.0f;       // the highest since reset
    int overloads = 0;       // buffers that took longer than they play (you'd hear a click)
    float disk = 0.0f;       // share of time spent reading audio from disk
    juce::String device;     // "Quantum HD 2, 48 kHz, 128 samples (2.7 ms)"
    float uiFps = 0.0f;      // how often the window gets its turn (its updates, 30 a second when all's well)
    float uiWorstMs = 0.0f;  // the longest the window was held up in the last couple of seconds (a stutter)
    juce::String drawing;    // "graphics card" or "the system"
    struct TrackLoad { juce::String name; float load = 0.0f; };
    std::vector<TrackLoad> tracks;   // the heaviest first
};

/**
    Logic's CPU / HD meter, in the control bar: two small bars, the audio's
    processing load and the disk. They turn amber, then red, as they fill.
    Click for the Performance window.
*/
class PerformanceMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PerformanceMeter() { setTooltip ("Performance: how hard the computer works to play the song (click for details)"); }

    void setReading (float cpuNow, float diskNow, float peakNow = 0.0f)
    {
        // The average load, eased (a steady bar, as Logic's); the heaviest recent block as a thin mark that
        // holds a moment and falls back
        cpu += (cpuNow - cpu) * (cpuNow > cpu ? 0.5f : 0.15f);
        disk = juce::jmax (diskNow, disk * 0.9f);
        if (peakNow >= peakMark) { peakMark = peakNow; peakHold = 20; }
        else if (--peakHold < 0) peakMark = juce::jmax (cpu, peakMark * 0.92f);
        repaint();
    }

    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

    static juce::Colour colourFor (float load)
    {
        return load > 0.85f ? juce::Colour (0xffe2463f) : load > 0.6f ? juce::Colour (0xffe8b93a) : juce::Colour (0xff45c25a);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::lcd);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        auto rows = r.reduced (6.0f, 5.0f);
        const auto rowHeight = rows.getHeight() / 2.0f;
        for (auto [label, value] : { std::pair { "CPU", cpu }, std::pair { "HD", disk } })
        {
            auto row = rows.removeFromTop (rowHeight).reduced (0.0f, 2.0f);
            g.setColour (theme::lcdDim);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (label, row.removeFromLeft (24.0f), juce::Justification::centredLeft, false);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (row, 1.5f);
            // LED segments, lit up to the value
            const int segments = 12;
            const auto w = row.getWidth() / (float) segments;
            for (int i = 0; i < segments; ++i)
            {
                const auto level = (float) (i + 1) / (float) segments;
                if (value + 0.5f / (float) segments < level)
                    break;
                g.setColour (colourFor (level));
                g.fillRoundedRectangle (row.getX() + (float) i * w + 0.5f, row.getY(), w - 1.0f, row.getHeight(), 1.0f);
            }
            if (juce::String (label) == "CPU" && peakMark > value + 0.5f / (float) segments)
            {
                // the heaviest recent block
                const auto x = row.getX() + row.getWidth() * juce::jlimit (0.0f, 1.0f, peakMark) - 1.5f;
                g.setColour (colourFor (peakMark).withAlpha (0.9f));
                g.fillRect (x, row.getY(), 2.0f, row.getHeight());
            }
        }
    }

private:
    float cpu = 0.0f, disk = 0.0f, peakMark = 0.0f;
    int peakHold = 0;
};

/** The Performance window: the load, its peak and overloads, the disk, the device, and which tracks cost the most. */
class PerformancePanel final : public juce::Component, private juce::Timer
{
public:
    PerformancePanel (std::function<PerformanceReading()> read, std::function<void()> resetPeak)
        : reader (std::move (read)), reset (std::move (resetPeak))
    {
        resetButton.onClick = [this] { if (reset) reset(); };
        addAndMakeVisible (resetButton);
        setSize (440, 420);
        startTimerHz (10);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        auto r = getLocalBounds().reduced (14);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (reading.device, r.removeFromTop (18), juce::Justification::centredLeft, true);
        // The screen: how smoothly the window runs (what "laggy" is, in numbers)
        g.setColour (reading.uiWorstMs > 80.0f ? theme::danger : theme::dimText);
        g.drawText ("Screen: " + juce::String (juce::roundToInt (reading.uiFps)) + " updates/s, longest hold-up "
                        + juce::String (juce::roundToInt (reading.uiWorstMs)) + " ms (drawn by " + reading.drawing + ")",
                    r.removeFromTop (18), juce::Justification::centredLeft, true);
        r.removeFromTop (8);

        auto bar = [&] (const juce::String& name, float value, float peakValue, juce::Rectangle<int> row)
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (name, row.removeFromLeft (70), juce::Justification::centredLeft, false);
            g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + "%", row.removeFromRight (52), juce::Justification::centredRight, false);
            auto b = row.reduced (6, 4).toFloat();
            g.setColour (theme::lcd);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (PerformanceMeter::colourFor (value));
            g.fillRoundedRectangle (b.withWidth (b.getWidth() * juce::jlimit (0.0f, 1.0f, value)), 3.0f);
            if (peakValue > 0.0f)
            {
                g.setColour (juce::Colours::white);
                g.fillRect (b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, peakValue) - 1.0f, b.getY(), 2.0f, b.getHeight());
            }
        };
        bar ("Audio", smoothedCpu, reading.peak, r.removeFromTop (26));
        bar ("Disk", smoothedDisk, 0.0f, r.removeFromTop (26));
        r.removeFromTop (4);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (reading.overloads > 0 ? theme::danger : theme::dimText);
        auto info = r.removeFromTop (22);
        g.drawText ("Peak " + juce::String (juce::roundToInt (reading.peak * 100.0f)) + "%   Overloads: " + juce::String (reading.overloads),
                    info.withTrimmedRight (70), juce::Justification::centredLeft, true);
        if (reading.overloads > 0)
            g.drawText ("Clicks? Try Large for Process buffer range (Settings), a larger I/O buffer, or freeze the heaviest tracks.",
                        r.removeFromTop (18), juce::Justification::centredLeft, true);
        r.removeFromTop (8);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Heaviest tracks", r.removeFromTop (20), juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (12.0f));
        if (reading.tracks.empty())
        {
            g.setColour (theme::dimText);
            g.drawText ("Play the song to see what each track costs.", r.removeFromTop (20), juce::Justification::centredLeft, false);
        }
        for (const auto& t : reading.tracks)
        {
            if (r.getHeight() < 18)
                break;
            auto row = r.removeFromTop (20);
            g.setColour (theme::text);
            g.drawText (t.name, row.removeFromLeft (150), juce::Justification::centredLeft, true);
            g.drawText (juce::String (t.load * 100.0f, 1) + "%", row.removeFromRight (52), juce::Justification::centredRight, false);
            auto b = row.reduced (6, 5).toFloat();
            g.setColour (theme::lcd);
            g.fillRoundedRectangle (b, 2.0f);
            g.setColour (PerformanceMeter::colourFor (t.load * 2.0f));   // (one track using half is a lot)
            g.fillRoundedRectangle (b.withWidth (b.getWidth() * juce::jlimit (0.0f, 1.0f, t.load)), 2.0f);
        }
    }

    void resized() override
    {
        resetButton.setBounds (getWidth() - 14 - 64, 14 + 18 + 8 + 52 + 4, 64, 22);
    }

private:
    void timerCallback() override
    {
        if (reader)
            reading = reader();
        smoothedCpu += (reading.cpu - smoothedCpu) * (reading.cpu > smoothedCpu ? 0.5f : 0.2f);   // (steady, as Logic's)
        smoothedDisk = juce::jmax (reading.disk, smoothedDisk * 0.8f);
        repaint();
    }

    std::function<PerformanceReading()> reader;
    std::function<void()> reset;
    PerformanceReading reading;
    float smoothedCpu = 0.0f, smoothedDisk = 0.0f;
    juce::TextButton resetButton { "Reset" };
};

} // namespace riff
