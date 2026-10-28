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
    Logic Pro (.logicx, a folder). The arrangement is in Alternatives/NNN/ProjectData, a binary
    form Apple hasn't published; it's read here from the open (MIT-licensed) reverse-engineered
    description by Jon Kubis (github.com/jonkubis/LogicProFormatWriter, PROJECTDATA_FORMAT.md),
    made from Logic 11 and 12 projects:

      a 24-byte root header (magic 23 47 C0 AB, the body's length at +0x10), then records to the end:
      a reversed FourCC tag, its payload size at +0x1c, the payload after a 36-byte header.
      Positions are 960 ticks a quarter note: regions count from 34560 (bar 1), tempo, signature,
      marker and note events from 38400. Every record's +0x08 is its object index.

      karT (93 bytes, index 0x40000)  the arrange's track rows, in order; +0x2a its channel (an ivnE)
      ivnE                            a channel: its name, u16 length at +0xc2 and the text at +0xc4
      qSvE                            event lists: tempo (events 60 00 00 00, BPM x 10000 at +0x10),
                                      signature (30 00 00 00), markers (12 00 00 00), notes (9x 00 00 00),
                                      and the arrange's (index 0x40000) 80-byte region placements:
                                      +0x00 0x24 audio / 0x20 MIDI, +0x04 position, +0x10 the channel (>> 16),
                                      +0x14 the track's row (1-based), +0x20 a MIDI region's object (>> 16),
                                      +0x2c an audio region's number x 4
      gRuA / lFuA (index n x 0x40000) audio region n (+0x16 its length in samples) and its file
                                      (UTF-16 name at +0x0a, folder path and 'EVAW' format after it)
      qeSM                            a MIDI region: name at +0x34, its length 0x3c past the name's end;
                                      its notes in the qSvE of the same index

    When the ProjectData can't be read (an older or newer Logic, say), the project's audio files
    come in on their own tracks as before, at their recorded places, with the tempo and signature.
*/
namespace
{
    namespace lgx
    {
        inline juce::uint32 u32 (const juce::uint8* p) { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }
        inline int u16 (const juce::uint8* p) { return p[0] | (p[1] << 8); }

        struct Record { juce::String tag; const juce::uint8* data = nullptr; size_t size = 0; juce::uint32 index = 0;
                        const juce::uint8* body() const { return data + 0x24; }
                        size_t bodySize() const { return size > 0x24 ? juce::jmin ((size_t) u32 (data + 0x1c), size - 0x24) : 0; } };

        /** The records, or none when the file isn't one we can walk. */
        std::vector<Record> walk (const juce::MemoryBlock& file)
        {
            std::vector<Record> out;
            const auto* d = static_cast<const juce::uint8*> (file.getData());
            const auto n = file.getSize();
            static const juce::uint8 magic[] { 0x23, 0x47, 0xc0, 0xab };
            if (n < 24 || std::memcmp (d, magic, 4) != 0)
                return out;
            const size_t end = juce::jmin ((size_t) n, (size_t) 24 + u32 (d + 0x10));
            size_t pos = 24;
            while (pos + 0x24 <= end)
            {
                const bool frame = std::memcmp (d + pos, magic, 4) == 0;
                const size_t size = frame ? 24 + (size_t) u32 (d + pos + 0x10) : 0x24 + (size_t) u32 (d + pos + 0x1c);
                for (int i = 0; i < 4 && ! frame; ++i)
                    if (d[pos + i] < 0x20 || d[pos + i] >= 0x7f)
                        return {};                             // lost the thread: not a form we know
                if (size < 0x24 || pos + size > end)
                    return {};
                Record r;
                r.tag = frame ? juce::String ("#frame") : juce::String (reinterpret_cast<const char*> (d + pos), 4);
                r.data = d + pos;
                r.size = size;
                r.index = u32 (d + pos + 0x08);
                out.push_back (r);
                pos += size;
            }
            return pos == end ? out : std::vector<Record>();
        }

        /** A length-prefixed text: u16 count at `at`, the ASCII after it. */
        juce::String text (const Record& r, size_t at, size_t& endOut)
        {
            endOut = at;
            if (at + 2 > r.size) return {};
            const auto n = (size_t) u16 (r.data + at);
            if (n > 255 || at + 2 + n > r.size) return {};
            endOut = at + 2 + n + (n & 1);
            return juce::String::fromUTF8 (reinterpret_cast<const char*> (r.data + at + 2), (int) n);
        }

        /** Plain text from a marker's RTF. */
        juce::String rtfText (const juce::String& rtf)
        {
            auto t = rtf.fromLastOccurrenceOf ("\\cf2 ", false, false);
            if (t.isEmpty()) t = rtf.fromLastOccurrenceOf (" ", false, false);
            juce::String out;
            for (int i = 0; i < t.length(); ++i)
            {
                const auto c = t[i];
                if (c == '}' || c == '{') continue;
                if (c == '\\')
                {
                    if (i + 1 < t.length() && t[i + 1] == '\'' && i + 3 < t.length())
                    {
                        out << juce::String::charToString ((juce::juce_wchar) t.substring (i + 2, i + 4).getHexValue32());
                        i += 3;
                        continue;
                    }
                    while (i + 1 < t.length() && juce::CharacterFunctions::isLetterOrDigit (t[i + 1])) ++i;
                    if (i + 1 < t.length() && t[i + 1] == ' ') ++i;
                    continue;
                }
                out << juce::String::charToString (c);
            }
            return out.trim();
        }
    }

    /** The arrangement from a Logic ProjectData; false when it can't be read. */
    bool readLogicArrangement (const juce::File& bundle, const juce::File& projectData, Project& out)
    {
        juce::MemoryBlock file;
        if (! projectData.loadFileAsData (file))
            return false;
        const auto records = lgx::walk (file);
        if (records.empty())
            return false;
        using lgx::u32;

        constexpr double ppq = 960.0;
        constexpr juce::uint32 regionOrigin = 34560, eventOrigin = 38400, arrangeIndex = 0x40000, masterChannel = 0x500000;

        // Tempo and signature
        for (const auto& r : records)
        {
            if (r.tag != "qSvE") continue;
            const auto* b = r.body();
            const auto n = r.bodySize();
            if (n >= 0x20 && u32 (b) == 0x60 && out.tempoChanges.empty())
            {
                bool first = true;
                for (size_t o = 0; o + 0x20 <= n && u32 (b + o) == 0x60; o += 0x20)
                {
                    const auto tick = (double) ((juce::int64) u32 (b + o + 4) - (juce::int64) eventOrigin);
                    const auto bpm = u32 (b + o + 0x10) / 10000.0;
                    if (bpm < 5.0 || bpm > 1000.0) continue;
                    if (first || tick <= 0.0) { out.bpm = bpm; first = false; }
                    else out.tempoChanges.push_back ({ tick / ppq, bpm });
                }
            }
            if (n >= 96 && u32 (b) == 0x30)
            {
                out.numerator = juce::jlimit (1, 64, (int) b[0x0c]);
                out.denominator = 1 << juce::jlimit (0, 6, (int) b[0x0b]);
                out.signatureChanges.clear();
                for (size_t o = 80; o + 48 <= n - 16 && u32 (b + o) == 0x30; o += 48)
                {
                    const auto tick = (double) ((juce::int64) u32 (b + o + 4) - (juce::int64) eventOrigin);
                    if (tick > 0.0)
                        out.signatureChanges.push_back ({ tick / ppq, juce::jlimit (1, 64, (int) b[o + 0x0c]), 1 << juce::jlimit (0, 6, (int) b[o + 0x0b]) });
                }
            }
        }

        // Markers: events 12 00 00 00 (+0x10 the link to their name), names in RTF qSxT records
        {
            std::map<int, juce::String> names;
            for (const auto& r : records)
                if (r.tag == "qSxT" && r.size > 0x24)
                {
                    // (the RTF starts after binary fields with zeros in them: found by its bytes)
                    const auto* bb = r.body();
                    const auto bn = r.bodySize();
                    for (size_t k = 0; k + 6 <= bn; ++k)
                        if (std::memcmp (bb + k, "{\\rtf1", 6) == 0)
                        {
                            names[lgx::u16 (r.data + 0x0a)] = lgx::rtfText (juce::String::fromUTF8 (reinterpret_cast<const char*> (bb + k), (int) (bn - k)));
                            break;
                        }
                }
            for (const auto& r : records)
            {
                if (r.tag != "qSvE" || r.bodySize() < 48 || u32 (r.body()) != 0x12) continue;
                const auto* b = r.body();
                for (size_t o = 0; o + 48 <= r.bodySize() && u32 (b + o) == 0x12; o += 48)
                {
                    const auto tick = (double) ((juce::int64) u32 (b + o + 4) - (juce::int64) eventOrigin);
                    const auto link = (int) u32 (b + o + 0x10);
                    auto name = names.count (link) > 0 ? names[link] : juce::String();
                    if (name.isEmpty()) name = "Marker " + juce::String ((int) out.markers.size() + 1);
                    out.markers.push_back ({ Time::inBeats (juce::jmax (0.0, tick / ppq)), name });
                }
                break;
            }
        }

        // The arrange's track rows, in order, and their channels' names
        struct Row { juce::uint32 channel; juce::String name; int track = -1; };
        std::vector<Row> rows;
        std::map<juce::uint32, juce::String> channelNames;
        for (const auto& r : records)
            if (r.tag == "ivnE")
            {
                size_t e = 0;
                channelNames[r.index] = lgx::text (r, 0xc2, e);
            }
        for (const auto& r : records)
            if (r.tag == "karT" && r.size == 93 && r.index == arrangeIndex)
                rows.push_back ({ u32 (r.data + 0x2a), channelNames[u32 (r.data + 0x2a)] });

        // Audio regions (n -> gRuA, lFuA) and MIDI regions (object -> qeSM, notes qSvE)
        std::map<juce::uint32, const lgx::Record*> audioRegion, audioFile, midiRegion, midiNotes;
        for (const auto& r : records)
        {
            if ((r.tag == "gRuA" || r.tag == "lFuA") && r.index % 0x40000 == 0)
            {
                auto& m = r.tag == "gRuA" ? audioRegion : audioFile;
                const auto n = r.index / 0x40000;
                // (a region used in the arrange is marked 1 at its first byte; the pool's own copy 0)
                if (m.count (n) == 0 || r.tag == "lFuA" || r.body()[0] == 1)
                    m[n] = &r;
            }
            if (r.tag == "qeSM") midiRegion[r.index] = &r;
            if (r.tag == "qSvE" && r.index != arrangeIndex) midiNotes[r.index] = &r;
        }

        const auto trackFor = [&] (int row, juce::uint32 channel, bool midi) -> Track*
        {
            int i = row - 1;
            if (i < 0 || i >= (int) rows.size() || rows[(size_t) i].channel != channel)
                for (size_t k = 0; k < rows.size(); ++k)
                    if (rows[k].channel == channel) { i = (int) k; break; }
            if (i < 0 || i >= (int) rows.size() || rows[(size_t) i].channel == masterChannel)
                return nullptr;
            auto& row_ = rows[(size_t) i];
            if (row_.track < 0)
            {
                Track t;
                t.key = "logic:" + juce::String (i);
                t.name = row_.name.isNotEmpty() ? row_.name : (midi ? "Inst " : "Audio ") + juce::String (i + 1);
                t.kind = midi ? Track::Kind::instrument : Track::Kind::audio;
                row_.track = (int) out.tracks.size();
                out.tracks.push_back (std::move (t));
            }
            return &out.tracks[(size_t) row_.track];
        };

        // Make every row a track in order first (empty ones too), so the order is Logic's
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].channel != masterChannel && rows[i].track < 0)
            {
                Track t;
                t.key = "logic:" + juce::String ((int) i);
                t.name = rows[i].name.isNotEmpty() ? rows[i].name : "Track " + juce::String ((int) i + 1);
                t.kind = rows[i].name.startsWith ("Inst") ? Track::Kind::instrument : Track::Kind::audio;
                rows[i].track = (int) out.tracks.size();
                out.tracks.push_back (std::move (t));
            }

        int placed = 0;
        for (const auto& r : records)
        {
            if (r.tag != "qSvE" || r.index != arrangeIndex) continue;
            const auto* b = r.body();
            const auto n = r.bodySize();
            for (size_t o = 0; o + 0x50 <= n; o += 4)
            {
                const auto type = u32 (b + o);
                if ((type != 0x20 && type != 0x24) || b[o + 0x17] != 0x89 || u32 (b + o + 0x1c) != 0x3fffffff)
                    continue;
                const auto position = (juce::int64) u32 (b + o + 4) - (juce::int64) regionOrigin;
                const auto channel = u32 (b + o + 0x10) << 16;
                const int row = b[o + 0x14];
                const bool midi = type == 0x20;
                auto* track = trackFor (row, channel, midi);
                if (track == nullptr || position < 0) { o += 0x4c; continue; }

                Region region;
                region.midi = midi;
                region.start = Time::inBeats ((double) position / ppq);
                if (midi)
                {
                    const auto object = u32 (b + o + 0x20) << 16;
                    const auto q = midiRegion.find (object);
                    double lengthTicks = 3840.0;
                    if (q != midiRegion.end())
                    {
                        size_t nameEnd = 0;
                        region.name = lgx::text (*q->second, 0x34, nameEnd);
                        if (nameEnd + 0x40 <= q->second->size)
                            if (const auto l = u32 (q->second->data + nameEnd + 0x3c); l > 0 && l < 960u * 4 * 10000)
                                lengthTicks = l;
                    }
                    if (const auto nq = midiNotes.find (object); nq != midiNotes.end())
                    {
                        const auto* nb = nq->second->body();
                        const auto nn = nq->second->bodySize();
                        for (size_t e = 0; e + 0x20 <= nn; e += 4)
                        {
                            if ((nb[e] & 0xf0) != 0x90 || nb[e + 1] != 0 || nb[e + 2] != 0 || nb[e + 3] != 0 || nb[e + 0x17] != 0x89)
                                continue;
                            const auto at = (juce::int64) u32 (nb + e + 4) - (juce::int64) eventOrigin;
                            const int pitch = nb[e + 0x0c], velocity = nb[e + 0x0b];
                            const auto length = u32 (nb + e + 0x1c);
                            if (at < 0 || pitch > 127 || velocity < 1 || velocity > 127 || length == 0 || length > 960u * 4 * 10000)
                                continue;
                            region.notes.push_back ({ pitch, (double) at / ppq, (double) length / ppq, velocity });
                            lengthTicks = juce::jmax (lengthTicks, (double) (at + (juce::int64) length));
                            e += 0x1c;
                        }
                    }
                    region.length = Time::inBeats (lengthTicks / ppq);
                    track->kind = Track::Kind::instrument;
                }
                else
                {
                    const auto number = u32 (b + o + 0x2c) / 4;
                    const auto g = audioRegion.find (number);
                    const auto f = audioFile.find (number);
                    juce::String fileName, folder;
                    double rate = 0.0;
                    juce::uint32 fileFrames = 0;
                    if (f != audioFile.end())
                    {
                        const auto& lf = *f->second;
                        const auto* p = lf.body();
                        const auto count = (size_t) lgx::u16 (p + 0x08);
                        if (0x0a + count * 2 <= lf.bodySize())
                            fileName = juce::String (juce::CharPointer_UTF16 (reinterpret_cast<const juce::CharPointer_UTF16::CharType*> (p + 0x0a)), count);
                        // the 'EVAW' format block: frames, rate; the folder path before it
                        for (size_t k = 0x0a + count * 2; k + 0x20 <= lf.bodySize(); ++k)
                            if (std::memcmp (p + k, "EVAW", 4) == 0)
                            {
                                fileFrames = u32 (p + k + 0x0c);
                                rate = (double) u32 (p + k + 0x14);
                                if (k >= 0x13e)
                                    folder = juce::String::fromUTF8 (reinterpret_cast<const char*> (p + k - 0x13e));
                                break;
                            }
                    }
                    if (fileName.isNotEmpty())
                    {
                        auto found = folder.startsWithChar ('/') ? juce::File (folder).getChildFile (fileName) : juce::File();
                        if (! found.existsAsFile())
                            found = findMedia (fileName, bundle, { "Media/Audio Files", "Audio Files" });
                        if (! found.existsAsFile())
                            found = findMedia (fileName, bundle.getParentDirectory(), { "Audio Files", "Media/Audio Files" });
                        if (found.existsAsFile()) region.file = found; else region.missingFile = fileName;
                    }
                    if (rate <= 0.0 && region.file.existsAsFile())
                    {
                        juce::AudioFormatManager fm;
                        fm.registerBasicFormats();
                        if (std::unique_ptr<juce::AudioFormatReader> rd { fm.createReaderFor (region.file) }; rd != nullptr)
                        {
                            rate = rd->sampleRate;
                            fileFrames = (juce::uint32) rd->lengthInSamples;
                        }
                    }
                    if (rate <= 0.0) rate = 44100.0;
                    juce::uint32 frames = 0;
                    if (g != audioRegion.end())
                    {
                        const auto* gp = g->second->body();
                        size_t e = 0;
                        region.name = lgx::text (*g->second, 0x24 + 0x4a, e);
                        frames = u32 (gp + 0x16);
                        // where in the file it starts (kept only when it fits the file)
                        const auto offset = u32 (gp + 0x12);
                        if (offset > 0 && fileFrames > 0 && (juce::uint64) offset + frames <= fileFrames)
                            region.offsetSeconds = offset / rate;
                    }
                    if (frames == 0 && fileFrames > 0) frames = fileFrames;
                    if (frames == 0 && region.file.existsAsFile()) frames = (juce::uint32) (audioFileLength (region.file) * rate);
                    region.length = Time::inSeconds (frames / rate);
                    if (region.name.isEmpty()) region.name = fileName.upToLastOccurrenceOf (".", false, false);
                }
                track->regions.push_back (std::move (region));
                ++placed;
                o += 0x4c;
            }
        }

        // Tracks with nothing on them and no name of their own aren't worth bringing
        out.tracks.erase (std::remove_if (out.tracks.begin(), out.tracks.end(), [] (const Track& t) { return t.regions.empty() && t.name.startsWith ("Track "); }),
                          out.tracks.end());
        return placed > 0 || ! out.tracks.empty();
    }
}

bool readLogic (const juce::File& file, Project& out, juce::String& error)
{
    if (! file.isDirectory())
    {
        error = "A Logic project is a .logicx package (a folder); this one couldn't be opened.";
        return false;
    }

    // The alternative last worked on
    juce::File alternative;
    for (const auto& a : juce::RangedDirectoryIterator (file.getChildFile ("Alternatives"), false, "*", juce::File::findDirectories))
        if (a.getFile().getChildFile ("ProjectData").existsAsFile()
            && (! alternative.exists() || a.getFile().getChildFile ("ProjectData").getLastModificationTime()
                                            > alternative.getChildFile ("ProjectData").getLastModificationTime()))
            alternative = a.getFile();
    if (! alternative.exists())
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

    // The arrangement itself: tracks, audio and MIDI regions, notes, tempo, signature, markers
    {
        Project arranged = out;
        if (readLogicArrangement (file, alternative.getChildFile ("ProjectData"), arranged))
        {
            out = std::move (arranged);
            int missing = 0;
            for (const auto& t : out.tracks)
                for (const auto& r : t.regions)
                    if (! r.midi && ! r.file.existsAsFile()) ++missing;
            out.note ("Logic: the tracks, audio and MIDI regions, notes, tempo, signature and markers came across. "
                      "Logic's plugins, instruments and mix settings are in a form Apple hasn't published: the instrument tracks "
                      "play the built-in synth (choose an instrument for each), and the faders start at 0 dB");
            if (missing > 0)
                out.note (juce::String (missing) + " audio region(s) refer to files that weren't found next to the project "
                          "(File > Find Missing Audio Files... looks for them)");
            return true;
        }
    }

    juce::Array<juce::File> files;
    if (auto* list = meta.getProperty ("AudioFiles", {}).getArray())
        for (const auto& entry : *list)
            if (const auto f = findMedia (entry.toString(), file, { "Media/Audio Files", "Audio Files" }); f.existsAsFile())
                files.addIfNotAlreadyThere (f);
    if (files.isEmpty())
        for (const auto* folder : { "Media/Audio Files", "Audio Files" })
            for (const auto& base : { file, file.getParentDirectory() })
                for (const auto& entry : juce::RangedDirectoryIterator (base.getChildFile (folder), false, "*", juce::File::findFiles))
                    if (isAudio (entry.getFile()))
                        files.addIfNotAlreadyThere (entry.getFile());
    files.sort();

    if (files.isEmpty())
    {
        error = "This Logic project's arrangement couldn't be read (it may be from a Logic version we don't know yet), and it has no audio files "
                "to bring over. In Logic: File > Export > All Tracks as Audio Files (from the start of the song), and "
                "File > Export > Selection as MIDI File for the MIDI regions - then open or drop those here.";
        return false;
    }

    addFilesAsTracks (out, files, timelineZero (files, 3600.0));
    out.note ("Logic: this project's arrangement couldn't be read (it may be from a Logic version we don't know yet), so its audio files came "
              "across, each on its own track (at its recorded place where Logic stamped one), with the tempo and signature. In Logic, "
              "File > Export > All Tracks as Audio Files, or Export > Selection as MIDI File, brings the rest over");
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
