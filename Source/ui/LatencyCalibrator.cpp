#include "LatencyCalibrator.h"
#include "Theme.h"

namespace riff
{

LatencyCalibrator::LatencyCalibrator (AudioEngine& e, double currentOffsetMs, std::function<void (double)> onChange)
    : engine (e), onOffsetChanged (std::move (onChange))
{
    intro.setText ("Riff shifts every recording earlier by the latency your audio interface reports. "
                   "Some interfaces report it slightly wrong, which makes takes land a little early or late.\n\n"
                   "To measure the real latency, connect a cable from output 1 to the input below "
                   "(or point a mic at a speaker). Turn down anything plugged into your speakers if you use a cable, "
                   "then click Measure. You'll hear four short beeps.",
                   juce::dontSendNotification);
    intro.setJustificationType (juce::Justification::topLeft);
    intro.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (intro);

    inputCaption.setText ("Input:", juce::dontSendNotification);
    addAndMakeVisible (inputCaption);

    const auto names = engine.getActiveInputChannelNames();
    for (int i = 0; i < names.size(); ++i)
        inputBox.addItem (names[i], i + 1);
    inputBox.setSelectedId (1, juce::dontSendNotification);
    inputBox.setTextWhenNoChoicesAvailable ("No inputs. Check Audio settings.");
    addAndMakeVisible (inputBox);

    measureButton.onClick = [this] { startMeasuring(); };
    measureButton.setEnabled (names.size() > 0);
    addAndMakeVisible (measureButton);

    resultLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    resultLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (resultLabel);

    offsetCaption.setText ("Extra recording offset (ms). Positive moves takes earlier:", juce::dontSendNotification);
    addAndMakeVisible (offsetCaption);

    offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    offsetSlider.setRange (-50.0, 50.0, 0.01);
    offsetSlider.setValue (currentOffsetMs, juce::dontSendNotification);
    offsetSlider.onValueChange = [this] { if (onOffsetChanged) onOffsetChanged (offsetSlider.getValue()); };
    addAndMakeVisible (offsetSlider);

    resetButton.onClick = [this] { setOffset (0.0); };
    addAndMakeVisible (resetButton);

    const auto rate = engine.getSampleRate();
    resultLabel.setText ("Reported by the driver: " + juce::String (1000.0 * engine.getRoundTripLatencySamples() / rate, 2) + " ms",
                         juce::dontSendNotification);

    setSize (520, 380);
}

LatencyCalibrator::~LatencyCalibrator()
{
    stopTimer();

    // If the window closes mid-measurement, collect the result so the engine goes back to normal.
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (engine.isMeasuringLatency() && ! engine.takeLatencyMeasurement().has_value()
           && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (10);
}

void LatencyCalibrator::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void LatencyCalibrator::resized()
{
    auto area = getLocalBounds().reduced (16);

    intro.setBounds (area.removeFromTop (150));
    area.removeFromTop (8);

    auto row = area.removeFromTop (28);
    inputCaption.setBounds (row.removeFromLeft (50));
    measureButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    inputBox.setBounds (row);

    area.removeFromTop (10);
    resultLabel.setBounds (area.removeFromTop (60));

    offsetCaption.setBounds (area.removeFromTop (22));
    row = area.removeFromTop (28);
    resetButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    offsetSlider.setBounds (row);
}

void LatencyCalibrator::startMeasuring()
{
    juce::String error;

    if (! engine.startLatencyMeasurement (inputBox.getSelectedId() - 1, error))
    {
        resultLabel.setText (error, juce::dontSendNotification);
        return;
    }

    measureButton.setEnabled (false);
    resultLabel.setText ("Measuring...", juce::dontSendNotification);
    startTimerHz (20);
}

void LatencyCalibrator::timerCallback()
{
    const auto result = engine.takeLatencyMeasurement();
    if (! result.has_value())
        return;

    stopTimer();
    measureButton.setEnabled (true);

    const auto rate = engine.getSampleRate();
    const auto reported = engine.getRoundTripLatencySamples();
    auto toMs = [rate] (double samples) { return 1000.0 * samples / rate; };

    if (! result->ok)
    {
        resultLabel.setText (result->error, juce::dontSendNotification);
        return;
    }

    const auto extra = result->delaySamples - reported;

    resultLabel.setText ("Measured: " + juce::String (toMs (result->delaySamples), 2) + " ms ("
                             + juce::String (result->delaySamples) + " samples)\n"
                         + "Reported by the driver: " + juce::String (toMs (reported), 2) + " ms\n"
                         + "Extra offset set to " + juce::String (toMs (extra), 2) + " ms.",
                         juce::dontSendNotification);

    setOffset (toMs (extra));
}

void LatencyCalibrator::setOffset (double ms)
{
    offsetSlider.setValue (ms, juce::sendNotificationSync);
}

} // namespace riff
