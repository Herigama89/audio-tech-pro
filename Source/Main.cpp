#include <JuceHeader.h>
#include "MainComponent.h"
#include "ui/ConsoleLook.h"
#include "ui/Theme.h"
#include "AppInfo.h"
#include <BinaryData.h>

class RiffApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // The skin and colours chosen in View > Theme, before anything is built in them.
        {
            juce::PropertiesFile::Options options;
            options.applicationName = riff::app::name;
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
            juce::PropertiesFile settings (options);
            riff::theme::set (riff::theme::Settings::fromString (settings.getValue ("theme")));
            riff::theme::apply (look);
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&look);

        // The logo while the app starts (click it to close it sooner).
        {
            const auto logo = juce::ImageCache::getFromMemory (BinaryData::Logo_png, BinaryData::Logo_pngSize);
            if (logo.isValid())
            {
                const auto width = 720;
                auto* splash = new juce::SplashScreen (getApplicationName(),
                                                       logo.rescaled (width, width * logo.getHeight() / logo.getWidth(),
                                                                      juce::Graphics::highResamplingQuality),
                                                       true);
                splash->deleteAfterDelay (juce::RelativeTime::seconds (2.5), true);
            }
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // Give the user a chance to save first.
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<riff::MainComponent*> (mainWindow->getContentComponent()))
            {
                main->confirmDiscardChanges ([] { juce::JUCEApplication::quit(); });
                return;
            }

        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, riff::theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new riff::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1240, 480, 10000, 10000);

            // Fit the screen we open on.
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto room = display->userBounds.toNearestInt().reduced (20);
                setSize (juce::jmin (getWidth(), room.getWidth()), juce::jmin (getHeight(), room.getHeight()));
            }

            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    riff::ConsoleLook look;   // the whole app's look; outlives every window
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RiffApplication)
