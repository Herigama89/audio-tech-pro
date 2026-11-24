// The New Project window (the Dashboard), the project's audio settings, and Delete Unused Audio.

#include "MainComponent.h"
#include "ui/ProjectDashboard.h"
#include "ui/UnusedAudioPanel.h"
#include "model/StarterTemplates.h"
#include "model/UnusedAudio.h"

namespace riff
{
namespace
{
    juce::DialogWindow* openWindow (juce::Component* content, const juce::String& title, juce::Component* near)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = title;
        options.dialogBackgroundColour = theme::background;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        options.componentToCentreAround = near;
        return options.launchAsync();
    }
}

//==============================================================================
bool MainComponent::dashboardAtStartup()
{
    return appProperties.getUserSettings()->getBoolValue ("showDashboardAtStartup", true) && projectFile == juce::File();
}

void MainComponent::newProject()
{
    showDashboard (false);
}

void MainComponent::showDashboard (bool recent)
{
    if (dashboardWindow != nullptr)
    {
        if (auto* d = dynamic_cast<ProjectDashboard*> (dashboardWindow->getContentComponent()))
            d->showRecent (recent);
        dashboardWindow->toFront (true);
        return;
    }
    auto* props = appProperties.getUserSettings();

    ProjectDashboard::Settings defaults;
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    defaults.sampleRate = props->getDoubleValue ("newProjectRate", device != nullptr ? device->getCurrentSampleRate() : 48000.0);
    defaults.bitDepth = props->getIntValue ("newProjectBits", props->getIntValue ("recordBitDepth", 24));
    defaults.fileType = props->getValue ("newProjectFileType", "wav");
    defaults.folder = juce::File (props->getValue ("newProjectFolder"));
    defaults.tempo = props->getDoubleValue ("newProjectTempo", 120.0);
    defaults.starter = props->getValue ("newProjectStarter", "empty");
    int n = 1;
    while (n < 1000 && defaults.folder.getChildFile ("Untitled " + juce::String (n)).exists())
        ++n;
    defaults.name = "Untitled " + juce::String (n);

    juce::StringArray devices;
    if (auto* type = engine.getDeviceManager().getCurrentDeviceTypeObject())
        devices = type->getDeviceNames (false);

    juce::Array<juce::File> recentFiles;
    for (int i = 0; i < recentProjects.getNumFiles() && recentFiles.size() < 12; ++i)
        if (const auto f = recentProjects.getFile (i); f.existsAsFile())
            recentFiles.add (f);

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    auto close = [safe]
    {
        if (safe != nullptr && safe->dashboardWindow != nullptr)
            safe->dashboardWindow->exitModalState (0);   // (deleted when dismissed)
    };
    ProjectDashboard::Callbacks callbacks;
    callbacks.cancel = close;
    callbacks.setShowAtStartup = [safe] (bool on)
    {
        if (safe != nullptr)
        {
            safe->appProperties.getUserSettings()->setValue ("showDashboardAtStartup", on);
            safe->appProperties.saveIfNeeded();
        }
    };
    callbacks.openOther = [safe, close]
    {
        close();
        if (safe != nullptr)
            safe->openProject();
    };
    callbacks.openRecent = [safe, close] (const juce::File& file)
    {
        close();
        if (safe != nullptr)
            safe->confirmDiscardChanges ([safe, file] { if (safe != nullptr) safe->openProjectFile (file); });
    };
    callbacks.create = [safe, close] (const ProjectDashboard::Settings& s)
    {
        if (safe == nullptr)
            return;
        auto* p = safe->appProperties.getUserSettings();
        p->setValue ("newProjectRate", s.sampleRate);
        p->setValue ("newProjectBits", s.bitDepth);
        p->setValue ("newProjectFileType", s.fileType);
        p->setValue ("newProjectFolder", s.folder.getFullPathName());
        p->setValue ("newProjectTempo", s.tempo);
        p->setValue ("newProjectStarter", s.starter);
        safe->appProperties.saveIfNeeded();
        close();
        safe->confirmDiscardChanges ([safe, s]
        {
            if (safe == nullptr)
                return;
            juce::ValueTree project;
            if (s.templateFile.existsAsFile())
            {
                juce::String error;
                project = model::load (s.templateFile, error);
                if (! project.isValid())
                {
                    safe->showError ("Couldn't open the template", error);
                    return;
                }
            }
            else
                project = starters::build (s.starter, s.name);
            project.setProperty (ids::name, s.name, nullptr);
            project.setProperty (ids::projectSampleRate, s.sampleRate, nullptr);
            project.setProperty (ids::projectBitDepth, s.bitDepth, nullptr);
            project.setProperty (ids::projectFileType, s.fileType, nullptr);
            project.setProperty (ids::tempo, s.tempo, nullptr);
            project.setProperty (ids::keyRoot, s.keyRoot, nullptr);
            project.setProperty (ids::keyMinor, s.keyMinor, nullptr);
            project.setProperty (ids::timeSigNumerator, s.timeSig, nullptr);
            project.setProperty (ids::countIn, s.countInBars > 0, nullptr);
            project.setProperty (ids::countInBars, juce::jmax (1, s.countInBars), nullptr);

            if (s.device.isNotEmpty())
                if (const auto error = safe->useAudioDevice (s.device, 0); error.isEmpty())
                    safe->saveDeviceState();

            // (as Pro Tools: the project's folder made now, its recordings in its Audio Files)
            auto folder = s.folder.getChildFile (juce::File::createLegalFileName (s.name));
            if (folder.exists())
                folder = folder.getNonexistentSibling (false);
            juce::File file;
            if (folder.createDirectory())
            {
                file = folder.getChildFile (juce::File::createLegalFileName (s.name) + model::fileExtension);
                juce::String error;
                if (! model::save (project, file, error))
                    file = juce::File();
            }
            safe->setProject (project, file);
        });
    };

    dashboardWindow = openWindow (new ProjectDashboard (defaults, devices, recentFiles, templatesFolder(),
                                                        props->getBoolValue ("showDashboardAtStartup", true), callbacks),
                                  "New Project", this);
    if (recent)
        if (auto* d = dashboardWindow != nullptr ? dynamic_cast<ProjectDashboard*> (dashboardWindow->getContentComponent()) : nullptr)
            d->showRecent (true);
}

//==============================================================================
void MainComponent::applyProjectAudio()
{
    auto* props = appProperties.getUserSettings();
    engine.setRecordBitDepth ((int) project.getProperty (ids::projectBitDepth, props->getIntValue ("recordBitDepth", 24)));
    engine.setRecordFileType (project.getProperty (ids::projectFileType, props->getValue ("recordFileType", "wav")).toString());

    if (! project.hasProperty (ids::projectSampleRate))
        return;
    const double want = project[ids::projectSampleRate];
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr || std::abs (device->getCurrentSampleRate() - want) < 1.0)
        return;
    bool available = false;
    for (auto r : device->getAvailableSampleRates())
        available = available || std::abs (r - want) < 1.0;
    if (! available)
    {
        juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<MainComponent> (this), want, now = device->getCurrentSampleRate()]
        {
            if (safe != nullptr)
                safe->showError ("Sample rate", "This project is " + juce::String (want / 1000.0, 1) + " kHz, but your audio interface can't run at it, "
                                 "so it plays at " + juce::String (now / 1000.0, 1) + " kHz (its audio is converted as it plays).");
        });
        return;
    }
    auto setup = dm.getAudioDeviceSetup();
    setup.sampleRate = want;
    if (dm.setAudioDeviceSetup (setup, true).isEmpty())
        saveDeviceState();
}

//==============================================================================
void MainComponent::showDeleteUnusedAudio()
{
    if (unusedAudioWindow != nullptr)
        unusedAudioWindow->exitModalState (0);
    const auto audioFolder = recordingFolder();

    // Kept: whatever the project's alternatives and its saved file use
    juce::Array<std::pair<juce::String, juce::ValueTree>> others;
    if (projectFile.existsAsFile())
    {
        const auto base = projectFile.getFileNameWithoutExtension().upToFirstOccurrenceOf (" - ", false, false);
        for (const auto& f : projectFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension))
        {
            if (! f.getFileNameWithoutExtension().startsWith (base))
                continue;
            juce::String error;
            if (auto other = model::load (f, error); other.isValid())
                others.add ({ f == projectFile ? juce::String ("the saved project (save to free it)")
                                               : "the alternative \"" + f.getFileNameWithoutExtension() + "\"", other });
        }
    }
    auto items = unusedaudio::find (project, audioFolder, others, engine.getFormatManager());

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    UnusedAudioPanel::Callbacks callbacks;
    callbacks.preview = [safe] (const juce::File& f) { if (safe != nullptr) safe->engine.previewFile (f); };
    callbacks.stopPreview = [safe] { if (safe != nullptr) safe->engine.stopPreview(); };
    callbacks.close = [safe]
    {
        if (safe != nullptr && safe->unusedAudioWindow != nullptr)
            safe->unusedAudioWindow->exitModalState (0);
    };
    callbacks.emptyDeleted = [safe, audioFolder]
    {
        const auto folder = unusedaudio::deletedFolder (audioFolder);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Empty Deleted Audio?",
                                                                     "The files in Deleted Audio are deleted for good (not to the Trash).",
                                                                     "Delete", "Cancel");
        juce::AlertWindow::showAsync (options, [safe, folder] (int result)
        {
            if (result != 1)
                return;
            folder.deleteRecursively();
            if (safe != nullptr)
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->showDeleteUnusedAudio(); });
        });
    };
    callbacks.remove = [safe, audioFolder] (const juce::Array<juce::File>& files, bool toTrash)
    {
        if (safe == nullptr)
            return;
        safe->appProperties.getUserSettings()->setValue ("unusedAudioToTrash", toTrash);
        juce::String error;
        const auto moved = unusedaudio::remove (files, toTrash, audioFolder, error);
        // (undo could bring back a region whose file has gone: the history starts again, as Pro Tools' Clean Up)
        safe->undoManager.clearUndoHistory();
        if (error.isNotEmpty())
            safe->showError ("Delete Unused Audio", error);
        juce::ignoreUnused (moved);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->showDeleteUnusedAudio(); });
    };

    unusedAudioWindow = openWindow (new UnusedAudioPanel (std::move (items), project[ids::name].toString(), audioFolder, engine.getFormatManager(),
                                                          appProperties.getUserSettings()->getBoolValue ("unusedAudioToTrash", true), callbacks),
                                    "Delete Unused Audio", this);
}
}
