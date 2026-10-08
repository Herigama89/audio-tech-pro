#include <JuceHeader.h>
#include "MainComponent.h"
#include "ui/ConsoleLook.h"
#include "ui/Theme.h"

class RiffApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
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
