#pragma once

#include <JuceHeader.h>
#include <map>

/**
    Keeping work safe: autosaves, backups made on every save, and Consolidate
    (copying a project's audio into its own folder).
*/
namespace riff::model::files
{
    /** Where an autosave goes: next to a saved project ("Autosave" folder), or in the
        app's own folder for an untitled one (named by `sessionId`, so two runs don't clash). */
    juce::File autosaveFileFor (const juce::File& projectFile, const juce::String& projectName, const juce::String& sessionId);

    /** The app's folder for untitled projects' autosaves. */
    juce::File untitledAutosaveFolder();

    /** Untitled autosaves left behind (a crash, or the app quit without saving), newest first. */
    juce::Array<juce::File> leftoverUntitledAutosaves();

    /** A saved project's autosave, if there is one newer than the project itself. */
    juce::File newerAutosave (const juce::File& projectFile);

    /** The "Project Backups" folder next to a project. */
    juce::File backupFolder (const juce::File& projectFile);

    /** Copies the project file as it is now into its backups (before it's overwritten), keeping the newest `keep`.
        Returns the backup, or an invalid file if there was nothing to back up. */
    juce::File makeBackup (const juce::File& projectFile, int keep = 10);

    /** The project's backups, newest first. */
    juce::Array<juce::File> backupsOf (const juce::File& projectFile);

    /** The audio files a project uses (regions and their takes), each once. */
    juce::Array<juce::File> audioFilesOf (const juce::ValueTree& project);

    /**
        Copies every audio file the project uses that isn't already in its folder into
        "<project folder>/Audio Files", and points the regions at the copies (undoable).
        Returns how many files were copied; `error` says why if some couldn't be.
    */
    int consolidate (juce::ValueTree project, const juce::File& projectFile, juce::UndoManager* um, juce::String& error);

    /** A backup of the project as it is now (not as on disk): Pro Tools' Session File Backups, made every few
        minutes while you work. Kept with the backups made on saving (the newest `keep`). */
    juce::File backupNow (const juce::ValueTree& project, const juce::File& projectFile, int keep);

    /**
        Every audio file under some folders, by name, for finding files a project can't find where it says
        (moved, copied to another computer, a Pro Tools session's "Audio Files"). Names match ignoring case,
        accents and punctuation, and with another audio extension (.wav for .aif, a Pro Tools stereo pair's .L/.R).
    */
    class MediaIndex
    {
    public:
        void add (const juce::File& folder, int maxFiles = 60000);
        juce::File find (const juce::String& fileName) const;
        int size() const { return (int) exact.size(); }
        static juce::String looseKey (const juce::String& name);
    private:
        std::map<juce::String, juce::File> exact, stems, loose;
    };

    /** Audio files the project uses that aren't there (regions and takes), each once. */
    juce::Array<juce::File> missingFilesOf (const juce::ValueTree& project);

    /** Points regions and takes whose files are missing at files of the same name the index finds (undoable).
        Returns how many files were found. */
    int relinkMissing (juce::ValueTree project, const MediaIndex& index, juce::UndoManager* um);

    /**
        Save a Copy (Logic's "Save a Copy As" with its audio, Pro Tools' "Save Copy In"): a folder `name` inside
        `parent` holding the project and every audio file (and impulse response) it uses, the copy pointing at
        them - a project that opens on any computer. `progress` (0..1) returns false to stop. Returns the copy's
        project file, or an invalid file (`error` says why).
    */
    juce::File saveCopyWithMedia (const juce::ValueTree& project, const juce::File& parent, const juce::String& name,
                                  const std::function<bool (double)>& progress, juce::String& error);

    /** Zips a folder (the copy, to send) into `zip`. */
    bool zipFolder (const juce::File& folder, const juce::File& zip, const std::function<bool (double)>& progress, juce::String& error);
}
