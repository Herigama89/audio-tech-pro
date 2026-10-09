#pragma once

#include <JuceHeader.h>

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
}
