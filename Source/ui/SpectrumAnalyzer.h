#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    A spectrum analyzer like Logic's MultiMeter: the track's sound as
    1/3-octave bars (or a smooth line), with a peak-hold mark over each band.
    You choose the FFT size, the view range, how fast it falls back (release)
    and how long peaks are held. Draws behind the EQ curve.
*/
class SpectrumAnalyzer final : public juce::Component,
                               private juce::Timer
{
public:
    struct Options
    {
        int fftOrder = 12;          // 2^12 = 4096 points
        float rangeDb = 80.0f;      // from 0 dB down to -range
        float releaseDbPerSecond = 40.0f;
        float peakHoldSeconds = 2.0f;   // 0: off, < 0: forever
        bool bars = true;           // 1/3-octave bars, or a line
        bool enabled = true;
    };

    /** `source` hands over new audio (mono) and returns how many samples it wrote. */
    SpectrumAnalyzer (std::function<int (float*, int)> source, std::function<double()> sampleRate);

    void setOptions (const Options& o);
    const Options& getOptions() const noexcept { return options; }

    void paint (juce::Graphics& g) override;

    /** The 31 third-octave band centres, 20 Hz to 20 kHz. */
    static const std::vector<double>& thirdOctaveCentres();

    /** Feeds audio directly (for tests): the bands' levels in dB after one analysis. */
    std::vector<float> analyse (const float* samples, int numSamples, double rate);

private:
    void timerCallback() override;
    void computeBands (double rate);

    std::function<int (float*, int)> source;
    std::function<double()> sampleRate;
    Options options;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    std::vector<float> ring, fftData, scratch;
    int ringPos = 0;

    std::vector<float> spectrumDb;              // per FFT bin, after windowing
    std::vector<float> bandDb, bandShown, peakDb, peakAge;   // per band (31) or per column (line mode)
    juce::uint32 lastTick = 0;
};

} // namespace riff
