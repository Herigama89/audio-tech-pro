#include "SpectrumAnalyzer.h"
#include "Eco.h"
#include "Theme.h"

namespace riff
{

namespace
{
    constexpr int lineColumns = 160;   // the line view's resolution
    constexpr double lowHz = 20.0, highHz = 20000.0;

    double columnFrequency (int column, int columns)
    {
        return lowHz * std::pow (highHz / lowHz, (column + 0.5) / columns);
    }
}

const std::vector<double>& SpectrumAnalyzer::thirdOctaveCentres()
{
    static const std::vector<double> centres { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                               1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    return centres;
}

SpectrumAnalyzer::SpectrumAnalyzer (std::function<int (float*, int)> s, std::function<double()> rate)
    : source (std::move (s)), sampleRate (std::move (rate))
{
    setInterceptsMouseClicks (false, false);
    setOptions (options);
    startTimerHz (30);
}

void SpectrumAnalyzer::setOptions (const Options& o)
{
    const bool sizeChanged = fft == nullptr || o.fftOrder != options.fftOrder;
    const bool modeChanged = o.bars != options.bars;
    options = o;
    if (sizeChanged)
    {
        const auto size = 1 << options.fftOrder;
        fft = std::make_unique<juce::dsp::FFT> (options.fftOrder);
        window = std::make_unique<juce::dsp::WindowingFunction<float>> ((size_t) size, juce::dsp::WindowingFunction<float>::hann, true);
        ring.assign ((size_t) size, 0.0f);
        fftData.assign ((size_t) size * 2, 0.0f);
        scratch.assign (8192, 0.0f);
        spectrumDb.assign ((size_t) size / 2, -120.0f);
        ringPos = 0;
    }
    if (sizeChanged || modeChanged || bandDb.empty())
    {
        const auto count = options.bars ? thirdOctaveCentres().size() : (size_t) lineColumns;
        bandDb.assign (count, -120.0f);
        bandShown.assign (count, -120.0f);
        peakDb.assign (count, -120.0f);
        peakAge.assign (count, 0.0f);
    }
    repaint();
}

void SpectrumAnalyzer::computeBands (double rate)
{
    // The newest window of audio -> magnitudes in dB.
    const auto size = (int) ring.size();
    for (int i = 0; i < size; ++i)
        fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % size)];
    std::fill (fftData.begin() + size, fftData.end(), 0.0f);
    window->multiplyWithWindowingTable (fftData.data(), (size_t) size);
    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);
    const auto scale = 4.0f / (float) size;   // a full-scale sine reads about 0 dB
    for (int b = 0; b < size / 2; ++b)
        spectrumDb[(size_t) b] = juce::Decibels::gainToDecibels (fftData[(size_t) b] * scale, -120.0f);

    const auto binHz = rate / size;
    auto levelBetween = [&] (double f0, double f1)
    {
        // Power sum of the bins in the band (at least the nearest one).
        auto b0 = juce::jlimit (1, size / 2 - 1, (int) std::floor (f0 / binHz));
        auto b1 = juce::jlimit (b0, size / 2 - 1, (int) std::ceil (f1 / binHz));
        double power = 0.0;
        for (int b = b0; b <= b1; ++b)
            power += std::pow (10.0, spectrumDb[(size_t) b] / 10.0);
        return (float) (10.0 * std::log10 (juce::jmax (1.0e-12, power)));
    };

    if (options.bars)
    {
        const auto& centres = thirdOctaveCentres();
        const auto edge = std::pow (2.0, 1.0 / 6.0);
        for (size_t i = 0; i < centres.size(); ++i)
            bandDb[i] = levelBetween (centres[i] / edge, centres[i] * edge);
    }
    else
    {
        for (int c = 0; c < lineColumns; ++c)
        {
            const auto f0 = columnFrequency (c, lineColumns) / std::pow (highHz / lowHz, 0.5 / lineColumns);
            const auto f1 = columnFrequency (c, lineColumns) * std::pow (highHz / lowHz, 0.5 / lineColumns);
            // The line follows the loudest bin (not a power sum), so narrow columns don't read high.
            auto b0 = juce::jlimit (1, size / 2 - 1, (int) std::floor (f0 / binHz));
            auto b1 = juce::jlimit (b0, size / 2 - 1, (int) std::ceil (f1 / binHz));
            float m = -120.0f;
            for (int b = b0; b <= b1; ++b)
                m = juce::jmax (m, spectrumDb[(size_t) b]);
            bandDb[(size_t) c] = m;
        }
    }
}

std::vector<float> SpectrumAnalyzer::analyse (const float* samples, int numSamples, double rate)
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) ringPos] = samples[i];
        ringPos = (ringPos + 1) % (int) ring.size();
    }
    computeBands (rate);
    return bandDb;
}

void SpectrumAnalyzer::timerCallback()
{
    static int ecoFrame = 0;
    if (eco::skipFrame (ecoFrame))
        return;   // (Eco mode: 15 a second; nothing while the app isn't in front)
    const auto now = juce::Time::getMillisecondCounter();
    const auto dt = lastTick == 0 ? 1.0f / 30.0f : juce::jlimit (0.001f, 0.5f, (float) (now - lastTick) / 1000.0f);
    lastTick = now;

    if (! options.enabled || ! source)
        return;

    // Take what arrived; with nothing new, the display falls back.
    int got = 0;
    for (int reads = 0; reads < 16; ++reads)   // (a source that never runs dry can't hold the screen up)
    {
        const auto n = source (scratch.data(), (int) scratch.size());
        if (n <= 0)
            break;
        got += n;
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) ringPos] = scratch[(size_t) i];
            ringPos = (ringPos + 1) % (int) ring.size();
        }
    }
    if (got > 0)
        computeBands (sampleRate ? sampleRate() : 48000.0);
    else
        std::fill (bandDb.begin(), bandDb.end(), -120.0f);

    // Instant rise, falls at the release rate; peaks hold, then fall too.
    for (size_t i = 0; i < bandDb.size(); ++i)
    {
        bandShown[i] = juce::jmax (bandDb[i], bandShown[i] - options.releaseDbPerSecond * dt);
        if (bandShown[i] >= peakDb[i])
        {
            peakDb[i] = bandShown[i];
            peakAge[i] = 0.0f;
        }
        else if (options.peakHoldSeconds >= 0.0f)
        {
            peakAge[i] += dt;
            if (peakAge[i] > options.peakHoldSeconds)
                peakDb[i] = juce::jmax (bandShown[i], peakDb[i] - options.releaseDbPerSecond * dt);
        }
    }
    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    if (! options.enabled)
        return;
    const auto area = getLocalBounds().toFloat();
    const auto yOf = [&] (float dbValue) { return area.getY() + juce::jlimit (0.0f, 1.0f, -dbValue / options.rangeDb) * area.getHeight(); };
    const auto xOf = [&] (double f) { return area.getX() + (float) (std::log (f / lowHz) / std::log (highHz / lowHz)) * area.getWidth(); };

    const auto fill = juce::Colour (0xff3fa7d6), peak = juce::Colour (0xffffd166);

    if (options.bars)
    {
        const auto& centres = thirdOctaveCentres();
        const auto width = area.getWidth() / (float) centres.size();
        for (size_t i = 0; i < centres.size(); ++i)
        {
            const auto x = area.getX() + (float) i * width;
            const auto top = yOf (bandShown[i]);
            g.setGradientFill (juce::ColourGradient (fill.withAlpha (0.75f), 0.0f, area.getY(), fill.withAlpha (0.25f), 0.0f, area.getBottom(), false));
            g.fillRect (x + 1.0f, top, width - 2.0f, area.getBottom() - top);
            if (options.peakHoldSeconds != 0.0f && peakDb[i] > -options.rangeDb)
            {
                g.setColour (peak);
                g.fillRect (x + 1.0f, yOf (peakDb[i]) - 1.0f, width - 2.0f, 2.0f);
            }
        }
    }
    else
    {
        juce::Path line, area2;
        for (int c = 0; c < (int) bandShown.size(); ++c)
        {
            const auto x = xOf (columnFrequency (c, (int) bandShown.size()));
            const auto y = yOf (bandShown[(size_t) c]);
            if (c == 0) { line.startNewSubPath (x, y); area2.startNewSubPath (x, area.getBottom()); }
            else        line.lineTo (x, y);
            area2.lineTo (x, y);
        }
        area2.lineTo (area.getRight(), area.getBottom());
        area2.closeSubPath();
        g.setColour (fill.withAlpha (0.22f));
        g.fillPath (area2);
        g.setColour (fill.withAlpha (0.9f));
        g.strokePath (line, juce::PathStrokeType (1.4f));
        if (options.peakHoldSeconds != 0.0f)
        {
            juce::Path peaks;
            for (int c = 0; c < (int) peakDb.size(); ++c)
            {
                const auto x = xOf (columnFrequency (c, (int) peakDb.size()));
                const auto y = yOf (peakDb[(size_t) c]);
                if (c == 0) peaks.startNewSubPath (x, y); else peaks.lineTo (x, y);
            }
            g.setColour (peak.withAlpha (0.7f));
            g.strokePath (peaks, juce::PathStrokeType (1.0f));
        }
    }

    // The level scale on the right
    g.setFont (juce::FontOptions (9.0f));
    for (float d = 0.0f; d >= -options.rangeDb + 1.0f; d -= options.rangeDb > 80.0f ? 20.0f : 10.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (area.getX(), yOf (d), area.getWidth(), 1.0f);
        g.setColour (theme::lcdDim);
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (area.getRight() - 28.0f, yOf (d), 26.0f, 11.0f), juce::Justification::centredRight, false);
    }
}

} // namespace riff
