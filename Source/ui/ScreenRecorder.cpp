// Everything except macOS: screen recording isn't there yet.
#include "ScreenRecorder.h"

#if ! JUCE_MAC

namespace riff
{

namespace
{
    class UnavailableRecorder final : public ScreenRecorder
    {
    public:
        bool isAvailable() const override { return false; }
        juce::String unavailableReason() const override { return "Screen recording is on the Mac for now; Windows comes later."; }

        void start (const Options&, ScreenAudioTap&, std::function<void (const juce::String&)> started) override
        {
            if (started)
                started (unavailableReason());
        }

        void stop (std::function<void (const juce::File&, const juce::String&)> finished) override
        {
            if (finished)
                finished ({}, unavailableReason());
        }

        bool isRecording() const override { return false; }
    };
}

std::unique_ptr<ScreenRecorder> ScreenRecorder::create() { return std::make_unique<UnavailableRecorder>(); }
void ScreenRecorder::openPermissionSettings() {}

} // namespace riff

#endif
