#pragma once

#include <JuceHeader.h>

/**
    Delete Unused Audio (as LUNA's and Pro Tools' Clean Up): the audio files in a project's audio folder that nothing
    in the project uses any more - old takes, bounces a newer one replaced, renders of regions since reset, freeze
    files of tracks unfrozen, imports whose regions were deleted.

    "Used" is any file a property anywhere in the project names (regions, takes, a stretched region's original,
    freeze files, the studio instruments' samples, impulse responses...). Files the project's alternatives or its
    saved file still use are listed but kept. Files go to the Trash or to the project's "Deleted Audio" folder.
*/
namespace riff::unusedaudio
{
    enum class Kind { recording, render, import };

    struct Item
    {
        juce::File file;
        Kind kind = Kind::import;
        juce::String reason;      // why it's unused (or, kept: what still uses it)
        juce::String keptBy;      // not empty: it can't be deleted (an alternative or the saved project uses it)
        juce::int64 bytes = 0;
        double seconds = 0.0;
        juce::Time modified;
    };

    /** Every audio file a project names, anywhere in its tree (full paths). */
    juce::StringArray filesNamedBy (const juce::ValueTree& project);

    /** The project's unused audio in `audioFolder` (and its folders, except Stems and Deleted Audio), newest first.
        `alternatives`: other projects whose files are kept (the alternatives, the saved file). */
    std::vector<Item> find (const juce::ValueTree& project, const juce::File& audioFolder,
                            const juce::Array<std::pair<juce::String, juce::ValueTree>>& alternatives,
                            juce::AudioFormatManager& formats);

    /** The folder files go to with "Move to Deleted Audio" (inside the audio folder). */
    juce::File deletedFolder (const juce::File& audioFolder);

    /** Moves the files to the Trash (or the Deleted Audio folder); returns how many moved. */
    int remove (const juce::Array<juce::File>& files, bool toTrash, const juce::File& audioFolder, juce::String& error);

    /** "3.4 MB", "1.2 GB" */
    juce::String sizeText (juce::int64 bytes);
}
