// Screen recording from the main window: the options, starting and stopping,
// and the running time on the Screen key. (Split from MainComponent.cpp.)

#include "MainComponent.h"
#include "model/Project.h"

namespace riff
{

void MainComponent::toggleScreenRecording()
{
    if (screenRecorder == nullptr || screenRecordStarting)
        return;

    if (screenRecorder->isRecording())
    {
        stopScreenRecording();
        return;
    }

    if (screenRecordWindow != nullptr)
    {
        screenRecordWindow->toFront (true);
        return;
    }

    auto* settings = appProperties.getUserSettings();
    auto initial = ScreenRecordSettings::fromString (settings->getValue ("screenRecording"));
    if (initial.folder == juce::File())
        initial.folder = ScreenRecordSettings::defaultFolder();

    auto* panel = new ScreenRecordPanel (initial, engine.getActiveInputChannelNames(),
                                         screenRecorder->isAvailable() ? juce::String() : screenRecorder->unavailableReason(),
                                         [this] (const ScreenRecordSettings& chosen)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), chosen]
        {
            if (safe == nullptr)
                return;
            safe->screenRecordWindow.reset();
            safe->startScreenRecording (chosen);
        });
    });

    screenRecordWindow = std::make_unique<EditorWindow> ("Record Screen", panel, false, [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->screenRecordWindow.reset();
        });
    });
}

void MainComponent::startScreenRecording (const ScreenRecordSettings& chosen)
{
    auto* settings = appProperties.getUserSettings();
    settings->setValue ("screenRecording", chosen.toString());
    appProperties.saveIfNeeded();

    auto folder = chosen.folder == juce::File() ? ScreenRecordSettings::defaultFolder() : chosen.folder;
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        showError ("Can't record the screen", "Couldn't create the folder " + folder.getFullPathName() + ".");
        return;
    }

    auto withFolder = chosen;
    withFolder.folder = folder;

    // The sound: the mix, plus the mic for the video only.
    screenTap.reset();
    screenTap.setMic (chosen.withMic ? chosen.micChannel : -1, chosen.micStereo,
                      juce::Decibels::decibelsToGain (chosen.micGainDb));
    engine.setScreenAudioTap (&screenTap);

    ScreenRecorder::Options options;
    options.wholeScreen = chosen.wholeScreen;
    options.maxHeight = chosen.maxHeight;
    options.frameRate = chosen.frameRate;
    options.file = withFolder.nextFile (project[ids::name].toString());
    options.sampleRate = engine.getSampleRate();
    options.window = this;

    screenRecordStarting = true;
    statusLabel.setText ("Starting the screen recording...", juce::dontSendNotification);

    screenRecorder->start (options, screenTap, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& error)
    {
        if (safe == nullptr)
            return;

        safe->screenRecordStarting = false;

        if (error.isNotEmpty())
        {
            safe->engine.setScreenAudioTap (nullptr);
            safe->statusLabel.setText (safe->deviceStatusText(), juce::dontSendNotification);

            if (error == "Recording cancelled.")
            {
                safe->statusLabel.setText ("Screen recording cancelled", juce::dontSendNotification);
            }
            else if (error.contains ("permission"))
            {
                safe->messageBox = juce::AlertWindow::showScopedAsync (
                    juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Allow screen recording",
                                                                  error, "Open Settings", "Later", safe.getComponent()),
                    [] (int result) { if (result == 1) ScreenRecorder::openPermissionSettings(); });
            }
            else
            {
                safe->showError ("Can't record the screen", error);
            }
            return;
        }

        safe->screenRecordStartedAt = juce::Time::getCurrentTime();
        const auto size = safe->screenRecorder->getVideoSize();
        safe->statusLabel.setText ("Recording the screen (" + juce::String (size.x) + " x " + juce::String (size.y)
                                   + "). Click Screen again to stop.", juce::dontSendNotification);
        safe->updateScreenRecording();
    });
}

void MainComponent::stopScreenRecording (const juce::String& failure)
{
    if (screenRecorder == nullptr || ! screenRecorder->isRecording())
        return;

    engine.setScreenAudioTap (nullptr);
    statusLabel.setText ("Finishing the video...", juce::dontSendNotification);

    screenRecorder->stop ([safe = juce::Component::SafePointer<MainComponent> (this), failure] (const juce::File& file, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        safe->updateScreenRecording();
        safe->statusLabel.setText (safe->deviceStatusText(), juce::dontSendNotification);

        if (error.isNotEmpty())
        {
            safe->showError ("Screen recording", (failure.isNotEmpty() ? failure + "\n\n" : juce::String()) + error);
            return;
        }

        const auto dropped = safe->screenTap.getNumDropped();
        auto text = (failure.isNotEmpty() ? "The recording stopped: " + failure + "\n\nWhat was recorded is saved as "
                                          : juce::String ("Saved as "))
                    + file.getFileName() + ".";
        if (dropped > 0)
            text << "\n\n(A little audio was dropped because the computer was busy.)";

        safe->messageBox = juce::AlertWindow::showScopedAsync (
            juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::InfoIcon, "Screen recording saved", text,
                                                          "Show in Finder", "OK", safe.getComponent()),
            [file] (int result) { if (result == 1) file.revealToUser(); });
    });
}

void MainComponent::updateScreenRecording()
{
    if (screenRecorder == nullptr)
        return;

    const bool recording = screenRecorder->isRecording();

    if (recording)
        if (const auto failure = screenRecorder->takeFailure(); failure.isNotEmpty())
        {
            stopScreenRecording (failure);
            return;
        }

    juce::String caption ("Screen");
    if (recording)
    {
        const auto seconds = (int) (juce::Time::getCurrentTime() - screenRecordStartedAt).inSeconds();
        caption = juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2);
    }

    if (caption != screenButton.getName() || recording != screenButton.getToggleState())
    {
        screenButton.setName (caption);
        screenButton.setToggleState (recording, juce::dontSendNotification);
        repaint (screenButton.getBounds().expanded (12, 0).withBottom (screenButton.getBottom() + 16));
    }
}

} // namespace riff
