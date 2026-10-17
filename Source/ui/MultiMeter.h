#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "../engine/Loudness.h"

namespace riff
{

/**
    The MultiMeter on the master, laid out as Logic's:
      - the main display: the Analyzer (a bar per 1/3 or 1/6 octave, Top and Range set by
        dragging their readouts) or the Goniometer (the stereo image), chosen by the tabs
      - at the right: the LUFS meter (short-term, with Integrated marked; LU-I and LU-S read out),
        the L and R level meters (Peak and RMS, read out), and the Correlation meter under them
      - along the bottom, the Analyzer's settings (Detection, Mode, Bands, Return Rate, Peak hold,
        Hold, Reset) and the level meters' (Level, Peak hold, Hold, Return Rate, Reset)
    Its colours follow the theme. Open it with the MM key on the master strip, or Mix > MultiMeter.
*/
class MultiMeterPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit MultiMeterPanel (AudioEngine& engine);
    ~MultiMeterPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    enum class Detection { left, right, lrMax, mono };
    enum class Mode { rmsSlow, rmsFast, peak };
    enum class LevelMode { peak, rms, peakAndRms };

private:
    friend class SnapshotTool;
    void timerCallback() override;
    void take (const float* left, const float* right, int n);   // (also used by the snapshot tool)
    void analyse (double seconds);
    void showGoniometer (bool gonio);

    void paintAnalyzer (juce::Graphics& g, juce::Rectangle<int> area);
    void paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMeters (juce::Graphics& g, juce::Rectangle<int> area);
    void paintCorrelation (juce::Graphics& g, juce::Rectangle<int> area);

    AudioEngine& engine;
    LoudnessMeter loudness;
    double preparedRate = 0.0;
    std::vector<float> readL = std::vector<float> (65536), readR = std::vector<float> (65536);

    // --- Analyzer -------------------------------------------------------------
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window = std::vector<float> (fftSize), fftData = std::vector<float> (fftSize * 2);
    std::vector<float> ringL = std::vector<float> (fftSize), ringR = std::vector<float> (fftSize);
    int ringPos = 0;
    float windowPower = 1.0f;

    int bands = 63;                        // 31 (1/3 octave) or 63 (1/6 octave)
    std::vector<float> bandDb, bandPower, bandPeak, bandHoldTime;
    Detection detection = Detection::mono;
    Mode mode = Mode::peak;
    float analyzerReturn = 11.8f;          // dB a second the bars fall back
    float analyzerPeakSeconds = 4.0f;      // how long a peak mark stays (<= 0: for ever)
    bool analyzerHold = false;
    float top = 5.0f, range = 60.0f;        // the display's top (dB) and how much it shows

    // --- Level, loudness, correlation ------------------------------------------
    LevelMode levelMode = LevelMode::peakAndRms;
    float levelReturn = 11.8f, levelPeakSeconds = 2.0f;
    bool levelHold = false;
    float peakDb[2] { -100.0f, -100.0f }, rmsDb[2] { -100.0f, -100.0f };
    float heldPeak[2] { -100.0f, -100.0f }, heldFor[2] {};
    double rmsSquares[2] {};
    float correlation = 0.0f, correlationLow = 1.0f, correlationHigh = -1.0f;

    // --- Goniometer -------------------------------------------------------------
    std::vector<juce::Point<float>> scope = std::vector<juce::Point<float>> (4096);
    int scopePos = 0;
    float scopeGain = 1.0f;

    // --- Controls ----------------------------------------------------------------
    juce::TextButton analyzerTab { "ANALYZER" }, goniometerTab { "GONIOMETER" };
    bool goniometer = false;
    juce::TextButton detectionButtons[4], modeButtons[3];
    juce::ComboBox bandsBox, analyzerReturnBox, analyzerPeakBox, levelBox, levelPeakBox, levelReturnBox;
    juce::TextButton analyzerHoldButton { "Hold" }, analyzerResetButton { "Reset" }, levelHoldButton { "Hold" }, levelResetButton { "Reset" };

    juce::Rectangle<int> displayArea, metersArea, correlationArea, controlsArea, topLabel, rangeLabel;
    int dragging = 0;   // 1 Top, 2 Range
    float dragStartValue = 0.0f;
    double lastTime = 0.0;
};

} // namespace riff
