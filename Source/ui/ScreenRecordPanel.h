#pragma once

#include <JuceHeader.h>

namespace riff
{

/** What to record, chosen before each screen recording. */
struct ScreenRecordSettings
{
    bool wholeScreen = false;
    int maxHeight = 2160;
    int frameRate = 60;
    bool withMic = false;
    int micChannel = 0;        // first input channel
    bool micStereo = false;
    float micGainDb = 0.0f;
    juce::File folder;

    juce::String toString() const;
    static ScreenRecordSettings fromString (const juce::String& text);

    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMoviesDirectory).getChildFile ("Audio Tech Pro");
    }

    /** A new file name in the folder, named after the project and the time. */
    juce::File nextFile (const juce::String& projectName) const;
};

/** The options window that opens before a screen recording. */
class ScreenRecordPanel final : public juce::Component
{
public:
    ScreenRecordPanel (const ScreenRecordSettings& initial, const juce::StringArray& inputNames,
                       const juce::String& unavailableReason, std::function<void (const ScreenRecordSettings&)> onStart);

    ScreenRecordSettings getSettings() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateEnablement();
    void chooseFolder();

    juce::Label captureCaption { {}, "Record" }, sizeCaption { {}, "Size" }, rateCaption { {}, "Frame rate" },
                audioCaption { {}, "Audio" }, micCaption { {}, "Mic input" }, levelCaption { {}, "Mic level" },
                folderCaption { {}, "Save to" }, folderLabel, help;
    juce::ComboBox captureBox, sizeBox, rateBox, audioBox, micBox;
    juce::Slider micLevel;
    juce::TextButton folderButton { "Choose..." }, startButton { "Start Recording" };
    juce::File folder;
    juce::Array<std::pair<int, bool>> micChoices;   // (first channel, stereo) per micBox item
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String unavailable;
};

} // namespace riff
