// Projects as files: Save a Copy (with its audio), Close Project, finding missing audio, timed backups.
#include "MainComponent.h"
#include "model/Project.h"
#include "model/ProjectFiles.h"
#include "AppInfo.h"

namespace riff
{

namespace
{
    /** A long copy (gigabytes of audio) with a progress bar; `done` on the message thread. */
    class CopyJob final : public juce::ThreadWithProgressWindow
    {
    public:
        using Work = std::function<juce::String (std::function<bool (double)>)>;
        CopyJob (const juce::String& title, Work w, std::function<void (const juce::String&)> d)
            : ThreadWithProgressWindow (title, true, true), work (std::move (w)), done (std::move (d)) {}
        void run() override
        {
            error = work ([this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }
        void threadComplete (bool cancelled) override
        {
            done (cancelled ? juce::String ("cancelled") : error);
            delete this;
        }
    private:
        Work work;
        std::function<void (const juce::String&)> done;
        juce::String error;
    };
}

void MainComponent::closeProject()
{
    if (engine.isRecording())
        return;
    confirmDiscardChanges ([this]
    {
        engine.stop();
        setProject (model::createProject(), {});
        statusLabel.setText ("Project closed", juce::dontSendNotification);
    });
}

void MainComponent::saveCopy()
{
    if (engine.isRecording())
        return;
    const auto name = project[ids::name].toString().isNotEmpty() ? project[ids::name].toString() : juce::String ("Untitled");
    const auto suggested = (projectFile != juce::File() ? projectFile.getParentDirectory().getParentDirectory()
                                                        : juce::File::getSpecialLocation (juce::File::userDesktopDirectory))
                               .getChildFile (juce::File::createLegalFileName (name + " Copy"));

    fileChooser = std::make_unique<juce::FileChooser> ("Save a copy of the project, with all its audio (a new folder of this name)", suggested, "*");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();
        if (chosen == juce::File())
            return;
        const auto copyName = chosen.getFileNameWithoutExtension();
        const auto parent = chosen.getParentDirectory();

        // Plugins keep their own settings: into the project first, as when saving
        rack.storePluginStates (project);
        effectRack.storePluginStates (model::mixerOf (project));
        const auto snapshot = project.createCopy();
        auto result = std::make_shared<juce::File>();

        auto* job = new CopyJob ("Saving a copy with its audio", [snapshot, parent, copyName, result] (std::function<bool (double)> progress)
        {
            juce::String error;
            *result = model::files::saveCopyWithMedia (snapshot, parent, copyName, progress, error);
            return error;
        },
        [safe = juce::Component::SafePointer<MainComponent> (this), result] (const juce::String& error)
        {
            if (safe == nullptr)
                return;
            auto& self = *safe;
            if (error.isNotEmpty())
            {
                if (error != "cancelled")
                    self.showError ("Couldn't save the copy", error);
                return;
            }
            const auto folder = result->getParentDirectory();
            const auto missing = folder.getChildFile ("Missing files.txt").existsAsFile();
            self.statusLabel.setText ("Copy saved: " + folder.getFullPathName(), juce::dontSendNotification);
            self.messageBox = juce::AlertWindow::showScopedAsync (
                juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                    .withTitle ("Copy saved")
                    .withMessage ("\"" + folder.getFileName() + "\" holds the project and every audio file it uses: it opens on any computer "
                                  "with " + app::name + " (plug-ins from other makers have to be installed there too)."
                                  + (missing ? "\n\nSome audio files were missing and couldn't be copied: see \"Missing files.txt\" in the folder." : juce::String())
                                  + "\n\nTo send it, make a .zip of the folder.")
                    .withButton ("Make a .zip to Send")
                    .withButton ("Show in Folder")
                    .withButton ("Done")
                    .withAssociatedComponent (safe.getComponent()),
                [safe, folder] (int choice)
                {
                    if (safe == nullptr)
                        return;
                    if (choice == 2)
                        folder.revealToUser();
                    if (choice != 1)
                        return;
                    const auto zip = folder.getSiblingFile (folder.getFileName() + ".zip");
                    auto* zipJob = new CopyJob ("Zipping the copy", [folder, zip] (std::function<bool (double)> progress)
                    {
                        juce::String zipError;
                        model::files::zipFolder (folder, zip, progress, zipError);
                        return zipError;
                    },
                    [safe, zip] (const juce::String& zipError)
                    {
                        if (safe == nullptr)
                            return;
                        if (zipError.isNotEmpty())
                        {
                            if (zipError != "cancelled")
                                safe->showError ("Couldn't make the zip", zipError);
                            return;
                        }
                        safe->statusLabel.setText ("Ready to send: " + zip.getFullPathName(), juce::dontSendNotification);
                        zip.revealToUser();
                    });
                    zipJob->launchThread();
                });
        });
        job->launchThread();
    });
}

int MainComponent::relinkFrom (juce::ValueTree target, const juce::Array<juce::File>& folders, juce::UndoManager* um)
{
    model::files::MediaIndex index;
    for (const auto& folder : folders)
        index.add (folder, folders.indexOf (folder) == 0 ? 100000 : 20000);
    return model::files::relinkMissing (target, index, um);
}

void MainComponent::findMissingFiles (bool onlyIfMissing)
{
    const auto missing = model::files::missingFilesOf (project);
    if (missing.isEmpty())
    {
        if (! onlyIfMissing)
            showError ("Find Missing Audio Files", "Every audio file the project uses is there.");
        return;
    }

    juce::String list;
    for (int i = 0; i < juce::jmin (8, missing.size()); ++i)
        list << juce::String::fromUTF8 ("\xe2\x80\xa2 ") << missing[i].getFileName() << "\n";
    if (missing.size() > 8)
        list << "...and " << (missing.size() - 8) << " more\n";

    messageBox = juce::AlertWindow::showScopedAsync (
        juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle (juce::String (missing.size()) + (missing.size() == 1 ? " audio file is missing" : " audio files are missing"))
            .withMessage ("The project can't find:\n" + list + "\nChoose a folder to look in (it and every folder inside it are searched, "
                          "by name - the case, accents or a different audio extension don't matter).")
            .withButton ("Choose Folder...")
            .withButton ("Not Now")
            .withAssociatedComponent (this),
        [safe = juce::Component::SafePointer<MainComponent> (this)] (int choice)
        {
            if (safe == nullptr || choice != 1)
                return;
            auto& self = *safe;
            self.fileChooser = std::make_unique<juce::FileChooser> ("Where are the missing audio files?",
                                                                    self.projectFile != juce::File() ? self.projectFile.getParentDirectory()
                                                                                                     : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            self.fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                           [safe] (const juce::FileChooser& chooser)
            {
                const auto folder = chooser.getResult();
                if (safe == nullptr || ! folder.isDirectory())
                    return;
                auto& me = *safe;
                juce::MouseCursor::showWaitCursor();
                me.undoManager.beginNewTransaction ("Find Missing Audio Files");
                const auto found = relinkFrom (me.project, { folder }, &me.undoManager);
                juce::MouseCursor::hideWaitCursor();
                const auto left = model::files::missingFilesOf (me.project).size();
                if (found > 0)
                {
                    me.markDirty();
                    me.player.rebuildNow();
                    me.timeline.changed();
                }
                me.statusLabel.setText ("Found " + juce::String (found) + (found == 1 ? " file" : " files")
                                            + (left > 0 ? ", " + juce::String (left) + " still missing" : juce::String (": all there now")),
                                        juce::dontSendNotification);
                if (left > 0)
                    me.findMissingFiles (true);
            });
        });
}

void MainComponent::backupIfDue()
{
    // Pro Tools' Session File Backups: every few minutes while there are changes, into "Project Backups"
    const auto minutes = appProperties.getUserSettings()->getIntValue ("backupMinutes", 5);
    if (minutes <= 0 || projectFile == juce::File() || ! changedSinceBackup || engine.isRecording() || engine.isRenderingOffline())
        return;
    const auto now = juce::Time::getMillisecondCounter();
    if (lastBackupMs == 0)
        lastBackupMs = now;
    if (now - lastBackupMs < (juce::uint32) minutes * 60000u)
        return;
    lastBackupMs = now;
    rack.storePluginStates (project);
    effectRack.storePluginStates (model::mixerOf (project));
    if (model::files::backupNow (project, projectFile, appProperties.getUserSettings()->getIntValue ("backupsToKeep", 10)).existsAsFile())
        changedSinceBackup = false;
}

} // namespace riff
