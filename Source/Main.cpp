#include <JuceHeader.h>
#include "MainComponent.h"
#include "ui/ConsoleLook.h"
#include "ui/Theme.h"
#include "ui/StartupSplash.h"
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

        // The main window is built (hidden) while the logo shows for at least five seconds and new
        // plugins are scanned; then it opens.
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        auto* main = dynamic_cast<riff::MainComponent*> (mainWindow->getContentComponent());

        auto logo = juce::ImageCache::getFromMemory (BinaryData::Logo_png, BinaryData::Logo_pngSize);
        if (logo.isValid())
            logo = logo.rescaled (720, 720 * logo.getHeight() / logo.getWidth(), juce::Graphics::highResamplingQuality);

        if (main == nullptr)
        {
            mainWindow->setVisible (true);
            return;
        }
        splash = std::make_unique<riff::StartupSplash> (logo, main->getPluginFormats(), main->getKnownPlugins(), main->pluginScanCrashFile(), [this]
        {
            juce::MessageManager::callAsync ([this]
            {
                if (auto* m = mainWindow != nullptr ? dynamic_cast<riff::MainComponent*> (mainWindow->getContentComponent()) : nullptr)
                    m->saveKnownPlugins();
                splash.reset();
                if (mainWindow != nullptr)
                {
                    mainWindow->setVisible (true);
                    mainWindow->toFront (true);
                }
            });
        });
        splash->addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        splash->centreWithSize (splash->getWidth(), splash->getHeight());
        splash->setVisible (true);
        splash->toFront (true);
    }

    void shutdown() override
    {
        splash = nullptr;
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
            // (shown when the start-up logo and plugin scan are done)
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
    std::unique_ptr<riff::StartupSplash> splash;
};

START_JUCE_APPLICATION (RiffApplication)
