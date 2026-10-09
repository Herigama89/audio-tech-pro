#include "ProjectFiles.h"
#include "Project.h"
#include "../AppInfo.h"

namespace riff::model::files
{

juce::File untitledAutosaveFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (app::name).getChildFile ("Autosave");
}

juce::File autosaveFileFor (const juce::File& projectFile, const juce::String& projectName, const juce::String& sessionId)
{
    if (projectFile != juce::File())
        return projectFile.getParentDirectory().getChildFile ("Autosave")
                   .getChildFile (projectFile.getFileNameWithoutExtension() + model::fileExtension);

    return untitledAutosaveFolder().getChildFile (juce::File::createLegalFileName (projectName.isNotEmpty() ? projectName : juce::String ("Untitled"))
                                                  + " " + sessionId + model::fileExtension);
}

juce::Array<juce::File> leftoverUntitledAutosaves()
{
    auto list = untitledAutosaveFolder().findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension);
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return list;
}

juce::File newerAutosave (const juce::File& projectFile)
{
    const auto autosave = autosaveFileFor (projectFile, {}, {});
    if (autosave.existsAsFile() && autosave.getLastModificationTime() > projectFile.getLastModificationTime())
        return autosave;
    return {};
}

juce::File backupFolder (const juce::File& projectFile)
{
    return projectFile.getParentDirectory().getChildFile ("Project Backups");
}

juce::Array<juce::File> backupsOf (const juce::File& projectFile)
{
    const auto prefix = projectFile.getFileNameWithoutExtension() + " ";
    juce::Array<juce::File> list;
    for (const auto& f : backupFolder (projectFile).findChildFiles (juce::File::findFiles, false, "*" + model::fileExtension))
        if (f.getFileName().startsWith (prefix))
            list.add (f);
    // Named by the time of the version they hold, so the names sort by age.
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return list;
}

juce::File makeBackup (const juce::File& projectFile, int keep)
{
    if (! projectFile.existsAsFile())
        return {};

    auto folder = backupFolder (projectFile);
    folder.createDirectory();

    const auto stamp = projectFile.getLastModificationTime().formatted ("%Y-%m-%d %H.%M.%S");
    const auto backup = folder.getNonexistentChildFile (projectFile.getFileNameWithoutExtension() + " " + stamp, model::fileExtension, false);
    if (! projectFile.copyFileTo (backup))
        return {};

    // Keep only the newest few.
    auto all = backupsOf (projectFile);
    for (int i = keep; i < all.size(); ++i)
        all.getReference (i).deleteFile();

    return backup;
}

juce::Array<juce::File> audioFilesOf (const juce::ValueTree& project)
{
    juce::Array<juce::File> list;
    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        for (const auto& region : track)
        {
            if (region.hasProperty (ids::file) && region[ids::file].toString().isNotEmpty())
                list.addIfNotAlreadyThere (juce::File (region[ids::file].toString()));
            for (const auto& take : region)
                if (take.hasType (ids::TAKE) && take[ids::file].toString().isNotEmpty())
                    list.addIfNotAlreadyThere (juce::File (take[ids::file].toString()));
        }
    }
    return list;
}

int consolidate (juce::ValueTree project, const juce::File& projectFile, juce::UndoManager* um, juce::String& error)
{
    if (projectFile == juce::File())
    {
        error = "Save the project first.";
        return 0;
    }

    const auto folder = projectFile.getParentDirectory();
    auto audioFolder = folder.getChildFile ("Audio Files");
    std::map<juce::String, juce::File> moved;   // old path -> copy
    juce::StringArray failed;

    for (const auto& file : audioFilesOf (project))
    {
        if (file.isAChildOf (folder))
            continue;   // already in the project
        if (! file.existsAsFile())
        {
            failed.add (file.getFileName() + " (missing)");
            continue;
        }

        audioFolder.createDirectory();
        auto target = audioFolder.getChildFile (file.getFileName());
        if (target.existsAsFile() && target.getSize() != file.getSize())
            target = audioFolder.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);

        if (target.existsAsFile() || file.copyFileTo (target))
            moved[file.getFullPathName()] = target;
        else
            failed.add (file.getFileName());
    }

    auto repoint = [&moved, um] (juce::ValueTree node)
    {
        if (auto it = moved.find (node[ids::file].toString()); it != moved.end())
            node.setProperty (ids::file, it->second.getFullPathName(), um);
    };

    for (auto track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        for (auto region : track)
        {
            repoint (region);
            for (auto take : region)
                if (take.hasType (ids::TAKE))
                    repoint (take);
        }
    }

    if (! failed.isEmpty())
        error = "Couldn't copy: " + failed.joinIntoString (", ");
    return (int) moved.size();
}

} // namespace riff::model::files
