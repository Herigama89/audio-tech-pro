#include <JuceHeader.h>
#include "ConsoleEngine.h"
#include "ConsoleUI.h"
#include "../engine/PluginScanChild.h"

/**
    Audio Tech Pro Console: the app. Opens the audio interface, restores the plugin list and the last
    session (strips, plugins and their settings), and saves both when it quits.
*/
class AtpConsoleApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return riff::pluginscan::isScanChild (getCommandLineParameterArray()); }

    void initialise (const juce::String&) override
    {
        // Started by the console itself to check one plugin (a plugin that crashes takes down only that copy)
        if (const auto args = getCommandLineParameterArray(); riff::pluginscan::isScanChild (args))
        {
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            setApplicationReturnValue (riff::pluginscan::runScanChild (args));
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "Audio Tech Pro Console";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        engine = std::make_unique<atpconsole::Engine>();
        if (auto known = properties.getUserSettings()->getXmlValue ("knownPlugins"))
            engine->knownPlugins.recreateFromXml (*known);

        // The last session: its audio interface first, then the strips and their plugins
        const auto last = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("Audio Tech Pro Console").getChildFile ("Last Session.atpconsole");
        std::unique_ptr<juce::XmlElement> session;
        if (last.existsAsFile())
            session = juce::XmlDocument::parse (last);
        if (session != nullptr && ! session->hasTagName ("ATPCONSOLE"))
            session.reset();

        const juce::XmlElement* device = nullptr;
        if (session != nullptr)
            if (auto* d = session->getChildByName ("DEVICE"))
                device = d->getFirstChildElement();
        if (session != nullptr)
            engine->setPreferredMacInterface (session->getStringAttribute ("macInterface"));
        engine->start (device);

        window = std::make_unique<MainWindow> (*engine, properties);

        if (session != nullptr)
        {
            juce::StringArray problems;
            engine->loadSession (*session, problems, false);
        }
    }

    void shutdown() override
    {
        if (window != nullptr)
            window->view->saveAutosave();
        if (engine != nullptr)
            if (auto xml = engine->knownPlugins.createXml())
                properties.getUserSettings()->setValue ("knownPlugins", xml.get());
        properties.saveIfNeeded();

        window.reset();      // (closes the plugin windows)
        if (engine != nullptr)
            engine->shutdown();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (atpconsole::Engine& consoleEngine, juce::ApplicationProperties& props)
            : juce::DocumentWindow ("Audio Tech Pro Console", atpconsole::ui::colours::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            view = new atpconsole::ui::MainView (consoleEngine, props);
            setContentOwned (view, true);
            setResizable (true, false);
            setResizeLimits (900, 680, 10000, 10000);
            centreWithSize (juce::jmin (1440, getParentMonitorArea().getWidth() - 40),
                            juce::jmin (900, getParentMonitorArea().getHeight() - 60));
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        atpconsole::ui::MainView* view = nullptr;   // (owned as the content)
    };

    juce::ApplicationProperties properties;
    std::unique_ptr<atpconsole::Engine> engine;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (AtpConsoleApp)
