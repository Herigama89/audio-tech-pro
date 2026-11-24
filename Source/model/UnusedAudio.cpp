#include "UnusedAudio.h"
#include <map>
#include <set>

namespace riff::unusedaudio
{
namespace
{
    const juce::StringArray audioExtensions { ".wav", ".wave", ".bwf", ".aif", ".aiff", ".flac", ".caf", ".mp3", ".m4a", ".ogg" };

    bool isAudioName (const juce::String& s)
    {
        for (const auto& e : audioExtensions)
            if (s.endsWithIgnoreCase (e))
                return true;
        return false;
    }

    juce::String key (const juce::String& path)
    {
        return juce::File::isAbsolutePath (path) ? juce::File (path).getFullPathName().toLowerCase() : path.toLowerCase();
    }

    void collect (const juce::ValueTree& tree, juce::StringArray& into)
    {
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto& v = tree[tree.getPropertyName (i)];
            if (v.isString())
            {
                const auto s = v.toString();
                if (s.length() > 4 && s.length() < 2048 && isAudioName (s) && juce::File::isAbsolutePath (s))
                    into.addIfNotAlreadyThere (juce::File (s).getFullPathName());
            }
        }
        for (const auto& child : tree)
            collect (child, into);
    }

    /** A recording's name: "Vocals#03", "Vocals#03 (2)" */
    bool looksRecorded (const juce::File& f)
    {
        const auto stem = f.getFileNameWithoutExtension();
        const auto after = stem.fromLastOccurrenceOf ("#", false, false);
        return stem.containsChar ('#') && after.isNotEmpty() && juce::CharacterFunctions::isDigit (after[0]);
    }
}

juce::StringArray filesNamedBy (const juce::ValueTree& project)
{
    juce::StringArray all;
    collect (project, all);
    return all;
}

juce::File deletedFolder (const juce::File& audioFolder)
{
    return audioFolder.getChildFile ("Deleted Audio");
}

std::vector<Item> find (const juce::ValueTree& project, const juce::File& audioFolder,
                        const juce::Array<std::pair<juce::String, juce::ValueTree>>& alternatives,
                        juce::AudioFormatManager& formats)
{
    std::vector<Item> items;
    if (! audioFolder.isDirectory())
        return items;

    std::set<juce::String> used;
    for (const auto& f : filesNamedBy (project))
        used.insert (key (f));
    std::map<juce::String, juce::String> keptBy;
    for (const auto& [name, other] : alternatives)
        for (const auto& f : filesNamedBy (other))
            keptBy.emplace (key (f), name);

    const auto deleted = deletedFolder (audioFolder), stems = audioFolder.getChildFile ("Stems");
    for (const auto& entry : juce::RangedDirectoryIterator (audioFolder, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        const auto f = entry.getFile();
        if (! isAudioName (f.getFileName()) || f.isAChildOf (deleted) || f.isAChildOf (stems))
            continue;
        if (used.count (key (f.getFullPathName())) > 0)
            continue;

        Item item;
        item.file = f;
        item.bytes = entry.getFileSize();
        item.modified = entry.getModificationTime();
        const auto folder = f.getParentDirectory().getFileName();
        if (f.getParentDirectory() != audioFolder)
        {
            item.kind = Kind::render;
            item.reason = folder == "Freeze Files" ? "Freeze file - the track is no longer frozen"
                        : folder == "Stretched"     ? "Time stretch - the region has changed since"
                        : folder == "Edits"         ? "Audio edit - no region plays it now"
                        : folder == "Audio Process" ? "Processed audio - no region plays it now"
                        : "Render - no region plays it now";
        }
        else if (looksRecorded (f))
        {
            item.kind = Kind::recording;
            item.reason = "Recorded - not in any region or take";
        }
        else
        {
            item.kind = Kind::import;
            item.reason = "Imported - its regions were deleted";
        }
        if (auto it = keptBy.find (key (f.getFullPathName())); it != keptBy.end())
        {
            item.keptBy = it->second;
            item.reason = "Used by " + it->second;
        }
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) }; r != nullptr && r->sampleRate > 0)
            item.seconds = (double) r->lengthInSamples / r->sampleRate;
        items.push_back (std::move (item));
    }
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
    {
        if (a.keptBy.isEmpty() != b.keptBy.isEmpty())
            return a.keptBy.isEmpty();
        return a.modified > b.modified;
    });
    return items;
}

int remove (const juce::Array<juce::File>& files, bool toTrash, const juce::File& audioFolder, juce::String& error)
{
    int moved = 0;
    juce::StringArray failed;
    for (const auto& f : files)
    {
        bool ok = false;
        if (toTrash)
            ok = f.moveToTrash();
        else
        {
            const auto into = deletedFolder (audioFolder);
            into.createDirectory();
            ok = f.moveFileTo (into.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false));
        }
        if (ok)
            ++moved;
        else
            failed.add (f.getFileName());
    }
    if (! failed.isEmpty())
        error = "Couldn't move " + failed.joinIntoString (", ");
    return moved;
}

juce::String sizeText (juce::int64 bytes)
{
    const double mb = (double) bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return juce::String (mb / 1024.0, 2) + " GB";
    return juce::String (mb, mb < 10.0 ? 1 : 0) + " MB";
}
}
