#pragma once

#include <JuceHeader.h>

namespace riff
{

/**
    The logo while the app starts: shown for at least five seconds while it
    looks for plugins you've added since last time (VST3, AU, CLAP), then the
    main window opens. A plugin is checked one at a time, with its name shown;
    one that crashes the scan is noted and skipped next time. Click the logo to
    skip the rest of the scan.
*/
class StartupSplash final : public juce::Component,
                            private juce::Timer
{
public:
    StartupSplash (juce::Image logo, juce::AudioPluginFormatManager& formats, juce::KnownPluginList& list,
                   juce::File crashFile, std::function<void()> onFinished);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override { skip = true; }

    static constexpr double minimumSeconds = 5.0;

private:
    void timerCallback() override;

    juce::Image logo;
    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList& list;
    juce::File crashFile;
    std::function<void()> onFinished;

    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
    size_t current = 0;
    int formatIndex = 0;
    bool listed = false, skip = false, done = false;
    juce::String status { "Starting..." };
    float progress = 0.0f;
    int found = 0;
    double started = 0.0;
};

} // namespace riff
