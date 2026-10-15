#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"
#include "../engine/Loudness.h"
#include "SpectrumAnalyzer.h"

namespace riff
{

/**
    The MultiMeter on the master (as Logic's): what's heard, measured every way.
      Level      peak (held) and RMS for left and right, in dBFS, with the clip count
      Loudness   Momentary, Short-term and Integrated LUFS, the loudness range (LU),
                 and a history of the short-term loudness against a target (-14 / -23 LUFS...)
      Goniometer the stereo image (mid up, sides across) and the phase correlation (-1 .. +1)
      Analyzer   the spectrum, in 1/3-octave bars
    Open it with the MM key on the master strip, or Mix > MultiMeter.
*/
class MultiMeterPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit MultiMeterPanel (AudioEngine& engine);
    ~MultiMeterPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    friend class SnapshotTool;
    void timerCallback() override;
    void take (const float* left, const float* right, int n);   // (also used by the snapshot tool)

    void paintLevels (juce::Graphics& g, juce::Rectangle<int> area);
    void paintLoudness (juce::Graphics& g, juce::Rectangle<int> area);
    void paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area);

    AudioEngine& engine;
    LoudnessMeter loudness;
    double preparedRate = 0.0;

    std::vector<float> left = std::vector<float> (65536), right = std::vector<float> (65536);

    // Level
    float peak[2] {}, peakHold[2] { -100.0f, -100.0f }, rms[2] {};
    int holdFrames[2] {}, clips = 0;

    // Stereo
    std::vector<juce::Point<float>> scope = std::vector<juce::Point<float>> (2048);
    int scopePos = 0;
    float correlation = 0.0f;

    // Loudness history (short-term, a value every frame, about 2 minutes)
    std::vector<float> history = std::vector<float> (3600, -100.0f);
    int historyPos = 0;
    float target = -14.0f;

    // The spectrum's own feed (mono)
    juce::AbstractFifo spectrumFifo { 32768 };
    std::vector<float> spectrumRing = std::vector<float> (32768);
    std::unique_ptr<SpectrumAnalyzer> analyzer;

    juce::TextButton resetButton { "Reset" };
    juce::ComboBox targetBox;
    juce::Rectangle<int> levelArea, loudnessArea, scopeArea;
};

} // namespace riff
