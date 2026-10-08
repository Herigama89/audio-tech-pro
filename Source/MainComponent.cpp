#include "MainComponent.h"

namespace riff
{

namespace colours
{
    const juce::Colour background { 0xff1c1d21 };
    const juce::Colour panel      { 0xff26282e };
    const juce::Colour outline    { 0xff3a3d45 };
    const juce::Colour text       { 0xffe6e7ea };
    const juce::Colour dimText    { 0xff8d919b };
    const juce::Colour accent     { 0xff4aa3ff };
    const juce::Colour waveform   { 0xff6fc3ff };
    const juce::Colour meterGreen { 0xff3ddc84 };
    const juce::Colour meterAmber { 0xffffc043 };
    const juce::Colour meterRed   { 0xffff5a52 };
}

//==============================================================================
// LevelMeter
//==============================================================================
void LevelMeter::push (float left, float right)
{
    // Instant attack, gentle release.
    levelL = juce::jmax (left,  levelL * 0.85f);
    levelR = juce::jmax (right, levelR * 0.85f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (label, area.removeFromBottom (16), juce::Justification::centred);

    auto drawBar = [&g] (juce::Rectangle<float> r, float linear)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 2.0f);

        const auto db = juce::Decibels::gainToDecibels (linear, -60.0f);
        const auto proportion = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
        auto filled = r.withTop (r.getBottom() - r.getHeight() * proportion);

        g.setColour (db > -0.5f ? colours::meterRed : db > -12.0f ? colours::meterAmber : colours::meterGreen);
        g.fillRoundedRectangle (filled, 2.0f);
    };

    auto bars = area.toFloat().reduced (2.0f, 0.0f);
    const auto barWidth = (bars.getWidth() - 3.0f) / 2.0f;
    drawBar (bars.removeFromLeft (barWidth), levelL);
    bars.removeFromLeft (3.0f);
    drawBar (bars.removeFromLeft (barWidth), levelR);
}

//==============================================================================
// MainComponent
//==============================================================================
MainComponent::MainComponent()
{
    auto& lf = getLookAndFeel();
    lf.setColour (juce::TextButton::buttonColourId,   colours::panel);
    lf.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    lf.setColour (juce::TextButton::textColourOffId,  colours::text);
    lf.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    lf.setColour (juce::ComboBox::outlineColourId,    colours::outline);
    lf.setColour (juce::Slider::trackColourId,        colours::accent);
    lf.setColour (juce::Slider::thumbColourId,        colours::text);
    lf.setColour (juce::Slider::backgroundColourId,   colours::panel);
    lf.setColour (juce::Label::textColourId,          colours::text);

    for (auto* b : { &openButton, &rewindButton, &playButton, &toneButton, &settingsButton })
        addAndMakeVisible (b);

    openButton.onClick     = [this] { openFile(); };
    rewindButton.onClick   = [this] { engine.rewind(); };
    playButton.onClick     = [this] { togglePlay(); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    toneButton.setClickingTogglesState (true);
    toneButton.onClick = [this] { engine.setToneEnabled (toneButton.getToggleState()); };

    toneFreqSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    toneFreqSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 20);
    toneFreqSlider.setRange (50.0, 2000.0, 1.0);
    toneFreqSlider.setSkewFactorFromMidPoint (440.0);
    toneFreqSlider.setValue (440.0, juce::dontSendNotification);
    toneFreqSlider.onValueChange = [this] { engine.setToneFrequency ((float) toneFreqSlider.getValue()); };
    addAndMakeVisible (toneFreqSlider);
    addAndMakeVisible (toneFreqLabel);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setRange (0.0, 1.0, 0.001);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.onValueChange = [this] { engine.setMasterGain ((float) volumeSlider.getValue()); };
    addAndMakeVisible (volumeSlider);
    addAndMakeVisible (volumeLabel);

    timeLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (timeLabel);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    thumbnail.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

    startupError = engine.initialise();
    statusLabel.setText (deviceStatusText(), juce::dontSendNotification);

    setWantsKeyboardFocus (true);
    setSize (1000, 560);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    thumbnail.removeChangeListener (this);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Control bar background
    g.setColour (colours::panel.darker (0.2f));
    g.fillRect (getLocalBounds().removeFromTop (56));

    // Waveform panel
    g.setColour (colours::panel);
    g.fillRoundedRectangle (waveformArea.toFloat(), 6.0f);

    auto inner = waveformArea.reduced (8);

    if (thumbnail.getNumChannels() == 0)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (startupError.isNotEmpty()
                              ? "Audio device error: " + startupError
                              : "Drop an audio file here, or click Open...\nSpace = play / stop",
                          inner, juce::Justification::centred, 2);
        return;
    }

    // Region header, the way a DAW draws a region
    auto header = inner.removeFromTop (20);
    g.setColour (colours::accent.withAlpha (0.35f));
    g.fillRect (header);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (engine.getLoadedFile().getFileName(), header.reduced (6, 0), juce::Justification::centredLeft);

    g.setColour (colours::accent.withAlpha (0.12f));
    g.fillRect (inner);

    g.setColour (colours::waveform);
    thumbnail.drawChannels (g, inner, 0.0, thumbnail.getTotalLength(), 1.0f);

    // Playhead
    const auto length = engine.getLengthSeconds();
    if (length > 0.0)
    {
        const auto x = (float) inner.getX() + (float) inner.getWidth() * (float) (engine.getPositionSeconds() / length);
        g.setColour (juce::Colours::white);
        g.drawLine (x, (float) waveformArea.getY() + 4.0f, x, (float) waveformArea.getBottom() - 4.0f, 1.5f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // --- Control bar ---
    auto bar = area.removeFromTop (56).reduced (10, 12);

    openButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (16);
    rewindButton.setBounds (bar.removeFromLeft (40));
    bar.removeFromLeft (4);
    playButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (12);
    timeLabel.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (12);

    settingsButton.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (16);
    volumeSlider.setBounds (bar.removeFromRight (110));
    volumeLabel.setBounds (bar.removeFromRight (56));
    bar.removeFromRight (16);

    toneButton.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (4);
    toneFreqLabel.setBounds (bar.removeFromRight (28));
    toneFreqSlider.setBounds (bar);

    // --- Status line ---
    statusLabel.setBounds (area.removeFromBottom (26).reduced (10, 0));

    // --- Main area: waveform + meters ---
    area.reduce (10, 10);
    auto meters = area.removeFromRight (84);
    inputMeter.setBounds (meters.removeFromLeft (40));
    meters.removeFromLeft (4);
    outputMeter.setBounds (meters);
    area.removeFromRight (10);
    waveformArea = area;
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)       { togglePlay();     return true; }
    if (key == juce::KeyPress::returnKey)      { engine.rewind();  return true; }
    return false;
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getPosition()))
        seekToX (e.x);
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getMouseDownPosition()))
        seekToX (e.x);
}

void MainComponent::seekToX (int x)
{
    const auto inner = waveformArea.reduced (8);
    const auto length = engine.getLengthSeconds();

    if (length <= 0.0 || inner.getWidth() <= 0)
        return;

    const auto proportion = juce::jlimit (0.0, 1.0, (double) (x - inner.getX()) / inner.getWidth());
    engine.setPositionSeconds (proportion * length);
    repaint (waveformArea);
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (engine.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadFile (juce::File (files[0]));
}

//==============================================================================
void MainComponent::openFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open an audio file",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       engine.getFormatManager().getWildcardForAllFormats());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      loadFile (file);
                              });
}

void MainComponent::loadFile (const juce::File& file)
{
    if (! engine.loadFile (file))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Can't open file",
                                                "Riff couldn't read " + file.getFileName() + ".");
        return;
    }

    thumbnail.setSource (new juce::FileInputSource (file));
    updateTransportButtons();
    repaint();
}

void MainComponent::togglePlay()
{
    if (engine.isPlaying())
        engine.stop();
    else
        engine.play();

    updateTransportButtons();
}

void MainComponent::updateTransportButtons()
{
    playButton.setButtonText (engine.isPlaying() ? "Stop" : "Play");
    playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(),
        0, 2,     // min / max input channels
        2, 2,     // min / max output channels
        true,     // show MIDI inputs
        false,    // show MIDI output
        true,     // stereo pairs
        false);   // hide advanced options
    selector->setSize (520, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = colours::background;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
        repaint (waveformArea);
    else
        statusLabel.setText (deviceStatusText(), juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    const auto levels = engine.takeLevels();
    inputMeter.push (levels.inL, levels.inR);
    outputMeter.push (levels.outL, levels.outR);

    timeLabel.setText (formatTime (engine.getPositionSeconds()), juce::dontSendNotification);

    // The transport stops by itself at the end of the file.
    if (playButton.getToggleState() != engine.isPlaying())
        updateTransportButtons();

    if (thumbnail.getNumChannels() > 0)
        repaint (waveformArea);
}

juce::String MainComponent::deviceStatusText() const
{
    auto* device = const_cast<AudioEngine&> (engine).getDeviceManager().getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device open";

    const auto sr = device->getCurrentSampleRate();
    const auto bs = device->getCurrentBufferSizeSamples();
    const auto latencyMs = 1000.0 * (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + 2 * bs) / sr;

    return device->getTypeName() + "  |  " + device->getName()
         + "  |  " + juce::String (sr / 1000.0, 1) + " kHz"
         + "  |  buffer " + juce::String (bs)
         + "  |  ~" + juce::String (latencyMs, 1) + " ms round trip";
}

juce::String MainComponent::formatTime (double seconds)
{
    const auto totalMs = (int) std::round (seconds * 1000.0);
    return juce::String::formatted ("%02d:%02d.%03d", totalMs / 60000, (totalMs / 1000) % 60, totalMs % 1000);
}

} // namespace riff
