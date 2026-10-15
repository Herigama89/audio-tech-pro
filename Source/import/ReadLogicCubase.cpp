#include "ProjectImport.h"
#include "PropertyList.h"

namespace riff::interchange
{
namespace
{
    /** A recording's place on the timeline, from its Broadcast Wave time stamp (seconds), or < 0. */
    double timeStampOf (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return -1.0;
        const auto ref = reader->metadataValues.getValue ("TimeReference", reader->metadataValues.getValue ("bwav time reference", {}));
        if (ref.isEmpty())
            return -1.0;
        return (double) ref.getLargeIntValue() / reader->sampleRate;
    }

    bool isAudio (const juce::File& f)
    {
        return f.hasFileExtension ("wav;aif;aiff;caf;flac;mp3;m4a;ogg");
    }

    /** Files in a folder as tracks, each placed by its time stamp when it has one (relative to `zero`). */
    void addFilesAsTracks (Project& out, const juce::Array<juce::File>& files, double zero)
    {
        for (const auto& f : files)
        {
            Track t;
            t.key = f.getFullPathName();
            t.name = f.getFileNameWithoutExtension();
            Region r;
            r.file = f;
            r.name = t.name;
            const auto length = audioFileLength (f);
            if (length <= 0.0)
                continue;
            const auto stamp = timeStampOf (f);
            r.start = Time::inSeconds (stamp >= zero ? stamp - zero : 0.0);
            r.length = Time::inSeconds (length);
            t.regions.push_back (std::move (r));
            out.tracks.push_back (std::move (t));
        }
    }

    /** Logic and Cubase stamp recordings with their place; Logic's bar 1 is 1:00:00:00 by default. */
    double timelineZero (const juce::Array<juce::File>& files, double usual)
    {
        double earliest = 1.0e12;
        for (const auto& f : files)
            if (const auto s = timeStampOf (f); s >= 0.0)
                earliest = juce::jmin (earliest, s);
        if (earliest >= 1.0e11)
            return 0.0;
        return earliest >= usual ? usual : 0.0;
    }
}

//==============================================================================
/*
    Logic Pro (.logicx, a folder): Alternatives/000/MetaData.plist has the tempo, key,
    signature, sample rate and the audio files the project uses; Media/Audio Files has them.
    The arrangement (ProjectData) is in a form Apple hasn't published, so each file comes in
    on its own track, at its recorded place when it was stamped with one (Logic does so for
    its recordings), else at the start.
*/
bool readLogic (const juce::File& file, Project& out, juce::String& error)
{
    if (! file.isDirectory())
    {
        error = "A Logic project is a .logicx package (a folder); this one couldn't be opened.";
        return false;
    }

    juce::File alternative;
    for (const auto& a : juce::RangedDirectoryIterator (file.getChildFile ("Alternatives"), false, "*", juce::File::findDirectories))
        if (! alternative.exists() || a.getFile().getFileName() < alternative.getFileName())
            alternative = a.getFile();

    const auto meta = plist::parse (alternative.getChildFile ("MetaData.plist"));
    if (auto* d = meta.getDynamicObject())
    {
        if (d->hasProperty ("BeatsPerMinute")) out.bpm = (double) d->getProperty ("BeatsPerMinute");
        if (d->hasProperty ("SongSignatureNumerator")) out.numerator = (int) d->getProperty ("SongSignatureNumerator");
        if (d->hasProperty ("SongSignatureDenominator")) out.denominator = (int) d->getProperty ("SongSignatureDenominator");
    }

    juce::Array<juce::File> files;
    if (auto* list = meta.getProperty ("AudioFiles", {}).getArray())
        for (const auto& entry : *list)
            if (const auto f = findMedia (entry.toString(), file, { "Media/Audio Files", "Audio Files" }); f.existsAsFile())
                files.addIfNotAlreadyThere (f);
    if (files.isEmpty())
        for (const auto* folder : { "Media/Audio Files", "Audio Files" })
            for (const auto& entry : juce::RangedDirectoryIterator (file.getChildFile (folder), false, "*", juce::File::findFiles))
                if (isAudio (entry.getFile()))
                    files.addIfNotAlreadyThere (entry.getFile());
    files.sort();

    if (files.isEmpty() && meta.isVoid())
    {
        error = "No project information or audio was found in this Logic project.";
        return false;
    }

    addFilesAsTracks (out, files, timelineZero (files, 3600.0));
    out.note ("Logic: its audio files came across (each on its own track, at its recorded place where Logic stamped one), "
              "with the tempo and signature. The arrangement, MIDI and plugins are in a form Apple hasn't published: "
              "in Logic, File > Export > All Tracks as Audio Files, or Export > Selection as MIDI File, brings the rest over");
    return true;
}

//==============================================================================
/*
    Cubase / Nuendo (.cpr / .npr): Steinberg's own binary form. The audio files it uses
    are named inside it (and sit in the project's Audio folder); the rest can't be read.
    Cubase 14 and newer: File > Export > DAWproject, and open that, for everything.
*/
bool readCubase (const juce::File& file, Project& out, juce::String& error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() < 12)
    {
        error = "The file couldn't be opened.";
        return false;
    }
    const auto* bytes = static_cast<const char*> (data.getData());
    if (std::memcmp (bytes, "RIFF", 4) != 0)
    {
        error = "This doesn't look like a Cubase project.";
        return false;
    }

    // Names of audio files inside it (plain text runs ending in an audio extension).
    juce::StringArray names;
    const auto size = (int) data.getSize();
    int runStart = -1;
    for (int i = 0; i <= size; ++i)
    {
        const bool printable = i < size && (juce::uint8) bytes[i] >= 32 && (juce::uint8) bytes[i] < 127;
        if (printable && runStart < 0)
            runStart = i;
        else if (! printable && runStart >= 0)
        {
            const juce::String run (bytes + runStart, (size_t) (i - runStart));
            for (const auto* ext : { ".wav", ".aif", ".aiff", ".mp3", ".flac" })
                if (run.endsWithIgnoreCase (ext) && run.length() > 4)
                    names.addIfNotAlreadyThere (run.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false));
            runStart = -1;
        }
    }

    juce::Array<juce::File> files;
    for (const auto& n : names)
        if (const auto f = findMedia (n, file.getParentDirectory(), { "Audio", "Edits" }); f.existsAsFile())
            files.addIfNotAlreadyThere (f);
    if (files.isEmpty())
        for (const auto& entry : juce::RangedDirectoryIterator (file.getSiblingFile ("Audio"), false, "*", juce::File::findFiles))
            if (isAudio (entry.getFile()))
                files.add (entry.getFile());
    files.sort();

    if (files.isEmpty())
    {
        error = "No audio files of this Cubase project were found. In Cubase 14 or newer, File > Export > DAWproject, and open that.";
        return false;
    }
    addFilesAsTracks (out, files, timelineZero (files, 0.0));
    out.note ("Cubase: its audio files came across, each on its own track (at its recorded place where it has one). "
              "For the whole project (regions, MIDI, mix and plugins): in Cubase 14 or newer, File > Export > DAWproject, and open that");
    return true;
}

} // namespace riff::interchange
