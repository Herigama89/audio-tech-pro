#pragma once

#include <JuceHeader.h>
#include "../engine/AudioEngine.h"

namespace riff
{

/**
    "Calibrate recording latency" window.

    Measures the real round trip with a loopback (a cable from an output to an
    input, or a mic in front of a speaker), and stores the difference from what
    the driver reports as an extra recording offset. The offset can also be
    adjusted by hand.
*/
class LatencyCalibrator final : public juce::Component,
                                private juce::Timer
{
public:
    LatencyCalibrator (AudioEngine& engine, double currentOffsetMs, std::function<void (double)> onOffsetChanged);
    ~LatencyCalibrator() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void startMeasuring();
    void setOffset (double ms);

    AudioEngine& engine;
    std::function<void (double)> onOffsetChanged;

    juce::Label intro, inputCaption, resultLabel, offsetCaption;
    juce::ComboBox inputBox;
    juce::TextButton measureButton { "Measure" }, resetButton { "Reset to 0" };
    juce::Slider offsetSlider;
};

} // namespace riff
