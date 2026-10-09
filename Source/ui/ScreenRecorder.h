#pragma once

#include <JuceHeader.h>
#include "engine/ScreenAudioTap.h"

namespace riff
{

/**
    Records the screen (the app's windows, or the whole display) to an MP4
    with the DAW's own audio. macOS only for now (ScreenCaptureKit); elsewhere
    isAvailable() is false.
*/
class ScreenRecorder
{
public:
    struct Options
    {
        bool wholeScreen = false;      // false: only Audio Tech Pro's windows
        int maxHeight = 2160;          // output height limit in pixels (0: the display's own)
        int frameRate = 60;
        juce::File file;               // .mp4
        double sampleRate = 48000.0;
        juce::Component* window = nullptr;   // which display: the one showing this window
    };

    virtual ~ScreenRecorder() = default;

    static std::unique_ptr<ScreenRecorder> create();

    virtual bool isAvailable() const = 0;
    virtual juce::String unavailableReason() const { return {}; }

    /** Starts; `started` is called on the message thread with an error message, or empty once it's recording. */
    virtual void start (const Options& options, ScreenAudioTap& tap, std::function<void (const juce::String& error)> started) = 0;

    /** Stops and finishes the file; `finished` is called on the message thread (error empty on success). */
    virtual void stop (std::function<void (const juce::File& file, const juce::String& error)> finished) = 0;

    virtual bool isRecording() const = 0;

    /** If the recording stopped or failed by itself (display unplugged, disk full...), why; once. Poll it while recording. */
    virtual juce::String takeFailure() { return {}; }

    /** The size being recorded, once started. */
    virtual juce::Point<int> getVideoSize() const { return {}; }

    /** Opens the system's privacy settings for screen recording (macOS). */
    static void openPermissionSettings();

    /** Output sizes the options offer: label and maximum height (0 = full resolution). */
    static juce::Array<std::pair<juce::String, int>> sizeChoices()
    {
        juce::Array<std::pair<juce::String, int>> choices;
        choices.add ({ "4K (2160p)", 2160 });
        choices.add ({ "1440p", 1440 });
        choices.add ({ "1080p", 1080 });
        choices.add ({ "720p", 720 });
        choices.add ({ "Full resolution", 0 });
        return choices;
    }

    /** The video size for a display of `width` x `height` pixels limited to `maxHeight` (even numbers, as encoders need). */
    static juce::Point<int> fitSize (int width, int height, int maxHeight)
    {
        if (width <= 0 || height <= 0)
            return {};
        auto h = (maxHeight > 0) ? juce::jmin (height, maxHeight) : height;
        auto w = (int) std::lround ((double) width * h / height);
        // Very wide screens: keep within what the H.264 encoder takes (4096 wide).
        if (w > 4096)
        {
            h = (int) std::lround ((double) h * 4096.0 / w);
            w = 4096;
        }
        return { w & ~1, h & ~1 };
    }

    /** About this bitrate for H.264 at the size and frame rate (bits per second). */
    static int bitrateFor (juce::Point<int> size, int fps)
    {
        const auto bits = (double) size.x * size.y * fps * 0.12;
        return (int) juce::jlimit (4.0e6, 80.0e6, bits);
    }
};

} // namespace riff
