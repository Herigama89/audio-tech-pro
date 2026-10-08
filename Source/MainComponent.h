#pragma once

#include <JuceHeader.h>
#include "engine/AudioEngine.h"

namespace riff
{

/** A simple stereo peak meter with decay, fed from the engine's peak values. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

    void push (float left, float right);
    void paint (juce::Graphics& g) override;

private:
    juce::String label;
    float levelL = 0, levelR = 0;
};

/**
    Phase 0 main window: transport bar, test tone, a waveform view of one
    audio file, input/output meters and an audio-settings dialog.
*/
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void openFile();
    void loadFile (const juce::File& file);
    void togglePlay();
    void showAudioSettings();
    void updateTransportButtons();
    void seekToX (int x);
    juce::String deviceStatusText() const;
    static juce::String formatTime (double seconds);

    AudioEngine engine;

    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail thumbnail { 512, engine.getFormatManager(), thumbnailCache };

    juce::TextButton openButton     { "Open..." };
    juce::TextButton rewindButton   { "|<" };
    juce::TextButton playButton     { "Play" };
    juce::TextButton toneButton     { "Test tone" };
    juce::TextButton settingsButton { "Audio settings" };

    juce::Slider toneFreqSlider;
    juce::Slider volumeSlider;
    juce::Label  toneFreqLabel  { {}, "Hz" };
    juce::Label  volumeLabel    { {}, "Master" };
    juce::Label  timeLabel;
    juce::Label  statusLabel;

    LevelMeter inputMeter  { "IN" };
    LevelMeter outputMeter { "OUT" };

    juce::Rectangle<int> waveformArea;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String startupError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace riff
