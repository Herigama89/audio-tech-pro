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

juce::File backupNow (const juce::ValueTree& project, const juce::File& projectFile, int keep)
{
    if (projectFile == juce::File())
        return {};
    auto folder = backupFolder (projectFile);
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    const auto backup = folder.getNonexistentChildFile (projectFile.getFileNameWithoutExtension() + " " + stamp, model::fileExtension, false);
    juce::String error;
    // (its audio paths are written relative to where it sits, so it opens from there as it is)
    if (! model::save (project, backup, error))
        return {};
    auto all = backupsOf (projectFile);
    for (int i = keep; i < all.size(); ++i)
        all.getReference (i).deleteFile();
    return backup;
}

//==============================================================================
namespace
{
    const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".caf", ".ogg", ".wave", ".bwf" };

    juce::String withoutExtension (const juce::String& name)
    {
        for (const auto& ext : audioExtensions)
            if (name.endsWithIgnoreCase (ext))
                return name.dropLastCharacters (ext.length());
        return name;
    }
}

juce::String MediaIndex::looseKey (const juce::String& name)
{
    // lower case, letters and digits only (accents, spaces, punctuation and how a system encodes them don't matter)
    juce::String key;
    for (auto c : name.toLowerCase())
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            key += juce::String::charToString (c);
    return key;
}

void MediaIndex::add (const juce::File& folder, int maxFiles)
{
    if (! folder.isDirectory())
        return;
    int seen = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        if (++seen > maxFiles)
            break;
        const auto f = entry.getFile();
        const auto name = f.getFileName();
        bool audio = false;
        for (const auto& ext : audioExtensions)
            audio = audio || name.endsWithIgnoreCase (ext);
        if (! audio)
            continue;
        // (the first found wins: the shallowest, usually the project's own)
        exact.emplace (name.toLowerCase(), f);
        stems.emplace (withoutExtension (name).toLowerCase(), f);
        loose.emplace (looseKey (withoutExtension (name)), f);
    }
}

juce::File MediaIndex::find (const juce::String& storedName) const
{
    const auto name = storedName.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
    if (name.isEmpty())
        return {};
    if (auto it = exact.find (name.toLowerCase()); it != exact.end())
        return it->second;
    const auto stem = withoutExtension (name);
    if (auto it = stems.find (stem.toLowerCase()); it != stems.end())
        return it->second;
    // Pro Tools' stereo files: "Gtr.L.wav" and "Gtr.R.wav" for "Gtr.wav" (or the other way round)
    for (const auto* side : { ".L", ".R", "_L", "_R", ".1", ".2" })
    {
        if (auto it = stems.find ((stem + side).toLowerCase()); it != stems.end())
            return it->second;
        if (stem.endsWithIgnoreCase (side))
            if (auto it = stems.find (stem.dropLastCharacters (2).toLowerCase()); it != stems.end())
                return it->second;
    }
    if (const auto key = looseKey (stem); key.isNotEmpty())
        if (auto it = loose.find (key); it != loose.end())
            return it->second;
    return {};
}

juce::Array<juce::File> missingFilesOf (const juce::ValueTree& project)
{
    juce::Array<juce::File> missing;
    for (const auto& f : audioFilesOf (project))
        if (! f.existsAsFile())
            missing.add (f);
    return missing;
}

int relinkMissing (juce::ValueTree project, const MediaIndex& index, juce::UndoManager* um)
{
    std::map<juce::String, juce::File> found;
    for (const auto& f : missingFilesOf (project))
        if (const auto match = index.find (f.getFileName()); match.existsAsFile())
            found[f.getFullPathName()] = match;
    if (found.empty())
        return 0;
    for (auto track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        for (auto region : track)
        {
            if (auto it = found.find (region[ids::file].toString()); it != found.end())
                region.setProperty (ids::file, it->second.getFullPathName(), um);
            for (auto take : region)
                if (take.hasType (ids::TAKE))
                    if (auto it = found.find (take[ids::file].toString()); it != found.end())
                        take.setProperty (ids::file, it->second.getFullPathName(), um);
        }
    }
    return (int) found.size();
}

juce::File saveCopyWithMedia (const juce::ValueTree& project, const juce::File& parent, const juce::String& name,
                              const std::function<bool (double)>& progress, juce::String& error)
{
    const auto folder = parent.getChildFile (juce::File::createLegalFileName (name));
    if (folder.exists() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
    {
        error = "\"" + folder.getFullPathName() + "\" already exists and isn't empty. Choose another name.";
        return {};
    }
    if (! folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName();
        return {};
    }

    auto copy = project.createCopy();
    std::map<juce::String, juce::File> copied;   // original path -> its copy
    juce::StringArray missing;

    // Everything to copy: the audio, and the impulse responses effects load
    juce::Array<juce::File> files = audioFilesOf (copy);
    juce::Array<juce::ValueTree> irInserts;
    std::function<void (juce::ValueTree)> findIrs = [&] (juce::ValueTree node)
    {
        if (node.hasType (ids::INSERT) && node.getProperty ("irFile").toString().isNotEmpty())
        {
            irInserts.add (node);
            files.addIfNotAlreadyThere (juce::File (node.getProperty ("irFile").toString()));
        }
        for (auto child : node)
            findIrs (child);
    };
    findIrs (copy);

    juce::int64 total = 0, done = 0;
    for (const auto& f : files)
        total += juce::jmax ((juce::int64) 1, f.getSize());

    for (const auto& f : files)
    {
        if (! f.existsAsFile())
        {
            missing.add (f.getFileName());
            continue;
        }
        const bool ir = std::any_of (irInserts.begin(), irInserts.end(), [&f] (const juce::ValueTree& i) { return juce::File (i.getProperty ("irFile").toString()) == f; });
        const auto into = folder.getChildFile (ir ? "Impulse Responses" : "Audio Files");
        into.createDirectory();
        auto target = into.getChildFile (f.getFileName());
        if (target.exists())
            target = into.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false);
        if (! f.copyFileTo (target))
        {
            error = "Couldn't copy " + f.getFullPathName() + " (is the disk full?)";
            folder.deleteRecursively();
            return {};
        }
        copied[f.getFullPathName()] = target;
        done += juce::jmax ((juce::int64) 1, f.getSize());
        if (progress && ! progress (total > 0 ? 0.95 * (double) done / (double) total : 0.0))
        {
            folder.deleteRecursively();
            error = "cancelled";
            return {};
        }
    }

    // The copy points at its own files
    std::function<void (juce::ValueTree)> repoint = [&] (juce::ValueTree node)
    {
        if (node.hasType (ids::REGION) || node.hasType (ids::TAKE))
            if (auto it = copied.find (node[ids::file].toString()); it != copied.end())
                node.setProperty (ids::file, it->second.getFullPathName(), nullptr);
        if (node.hasType (ids::INSERT))
            if (auto it = copied.find (node.getProperty ("irFile").toString()); it != copied.end())
                node.setProperty ("irFile", it->second.getFullPathName(), nullptr);
        for (auto child : node)
            repoint (child);
    };
    repoint (copy);
    copy.setProperty (ids::name, name, nullptr);

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + model::fileExtension);
    if (! model::save (copy, file, error))
    {
        folder.deleteRecursively();
        return {};
    }
    if (! missing.isEmpty())
        folder.getChildFile ("Missing files.txt").replaceWithText ("These audio files were missing when the copy was made:\n" + missing.joinIntoString ("\n") + "\n");
    if (progress)
        progress (1.0);
    return file;
}

bool zipFolder (const juce::File& folder, const juce::File& zip, const std::function<bool (double)>& progress, juce::String& error)
{
    juce::ZipFile::Builder builder;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
    {
        const auto f = entry.getFile();
        // audio barely compresses: stored, so zipping is quick
        const bool audio = audioExtensions.contains (f.getFileExtension().toLowerCase());
        builder.addFile (f, audio ? 0 : 9, f.getRelativePathFrom (folder.getParentDirectory()).replaceCharacter ('\\', '/'));
    }
    zip.deleteFile();
    juce::FileOutputStream out (zip);
    if (! out.openedOk())
    {
        error = "Couldn't write " + zip.getFullPathName();
        return false;
    }
    double p = 0.0;
    if (! builder.writeToStream (out, &p))
    {
        error = "Couldn't make the zip file.";
        return false;
    }
    juce::ignoreUnused (progress);
    return true;
}

}
