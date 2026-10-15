#include "ProjectImport.h"
#include "../model/Project.h"
#include "../model/TempoMap.h"

namespace riff
{
namespace interchange
{
//==============================================================================
juce::File findMedia (const juce::String& storedPath, const juce::File& projectFolder, const juce::StringArray& extraFolders)
{
    auto path = storedPath.trim();
    if (path.isEmpty())
        return {};
    if (path.startsWithIgnoreCase ("file://"))
        path = juce::URL::removeEscapeChars (path.fromFirstOccurrenceOf ("file://", false, true));
    if (path.startsWith ("/") && path.length() > 3 && path[2] == ':')   // file:///C:/...
        path = path.substring (1);

    // Where it was (this computer)
    if (juce::File::isAbsolutePath (path))
        if (const juce::File f (path); f.existsAsFile())
            return f;

    // Relative to the project
    const auto normalised = path.replaceCharacter ('\\', '/');
    if (! juce::File::isAbsolutePath (normalised) && ! normalised.startsWithChar ('/'))
        if (const auto f = projectFolder.getChildFile (normalised); f.existsAsFile())
            return f;

    // By name: in the project's usual media folders, then anywhere under it (not too deep)
    const auto name = normalised.fromLastOccurrenceOf ("/", false, false);
    if (name.isEmpty())
        return {};

    juce::StringArray folders { "", "Samples", "Samples/Recorded", "Samples/Imported", "Samples/Processed", "Audio",
                                "Audio Files", "Media", "Media/Audio Files", "Audio Files", "Media/Audio", "Recorded",
                                "Resources/Audio Files", "Data", "Edits" };
    folders.addArray (extraFolders);
    for (const auto& folder : folders)
        if (const auto f = (folder.isEmpty() ? projectFolder : projectFolder.getChildFile (folder)).getChildFile (name); f.existsAsFile())
            return f;

    for (const auto& root : { projectFolder, projectFolder.getParentDirectory() })
    {
        int checked = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, name, juce::File::findFiles))
        {
            if (++checked > 2000)
                break;
            return entry.getFile();
        }
    }
    return {};
}

double audioFileLength (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (! file.existsAsFile())
        return 0.0;
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) }; reader != nullptr && reader->sampleRate > 0.0)
        return (double) reader->lengthInSamples / reader->sampleRate;
    return 0.0;
}

juce::MemoryBlock fromHex (const juce::String& hex)
{
    juce::MemoryBlock block;
    block.ensureSize ((size_t) hex.length() / 2);
    int high = -1;
    size_t n = 0;
    for (auto p = hex.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto v = juce::CharacterFunctions::getHexDigitValue (*p);
        if (v < 0)
            continue;
        if (high < 0)
            high = v;
        else
        {
            block.setSize (n + 1);
            static_cast<juce::uint8*> (block.getData())[n++] = (juce::uint8) ((high << 4) | v);
            high = -1;
        }
    }
    return block;
}

bool readVstPreset (const juce::MemoryBlock& data, juce::String& classId, juce::MemoryBlock& component, juce::MemoryBlock& controller)
{
    // "VST3", version (int32), class id (32 ASCII hex), chunk list offset (int64); the list: "List", count, then id/offset/size.
    const auto* bytes = static_cast<const char*> (data.getData());
    const auto size = (juce::int64) data.getSize();
    if (size < 48 || std::memcmp (bytes, "VST3", 4) != 0)
        return false;
    classId = juce::String (bytes + 8, 32);
    const auto listAt = (juce::int64) juce::ByteOrder::littleEndianInt64 (bytes + 40);
    if (listAt < 48 || listAt + 8 > size || std::memcmp (bytes + listAt, "List", 4) != 0)
        return false;
    const auto count = (int) juce::ByteOrder::littleEndianInt (bytes + listAt + 4);
    for (int i = 0; i < count; ++i)
    {
        const auto entry = listAt + 8 + i * 20;
        if (entry + 20 > size)
            break;
        const auto offset = (juce::int64) juce::ByteOrder::littleEndianInt64 (bytes + entry + 4);
        const auto length = (juce::int64) juce::ByteOrder::littleEndianInt64 (bytes + entry + 12);
        if (offset < 0 || length < 0 || offset + length > size)
            continue;
        if (std::memcmp (bytes + entry, "Comp", 4) == 0)
            component = juce::MemoryBlock (bytes + offset, (size_t) length);
        else if (std::memcmp (bytes + entry, "Cont", 4) == 0)
            controller = juce::MemoryBlock (bytes + offset, (size_t) length);
    }
    return component.getSize() > 0;
}

juce::MemoryBlock juceVst3State (const juce::MemoryBlock& component, const juce::MemoryBlock& controller)
{
    juce::XmlElement state ("VST3PluginState");
    if (component.getSize() > 0)
        state.createNewChildElement ("IComponent")->addTextElement (component.toBase64Encoding());
    if (controller.getSize() > 0)
        state.createNewChildElement ("IEditController")->addTextElement (controller.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (state, out);
    return out;
}
} // namespace interchange

//==============================================================================
namespace projectimport
{
using namespace interchange;

namespace
{
    struct Kind { const char* extensions; const char* name; bool (*reader) (const juce::File&, Project&, juce::String&); };

    const Kind kinds[] {
        { "als",                 "Ableton Live",   readAbleton },
        { "dawproject",          "DAWproject",     readDawProject },
        { "flp",                 "FL Studio",      readFlStudio },
        { "song",                "Studio One",     readStudioOne },
        { "ptx;ptf;pts",         "Pro Tools",      readProTools },
        { "logicx",              "Logic Pro",      readLogic },
        { "cpr;npr",             "Cubase",         readCubase },
        { "mid;midi;smf;kar",    "MIDI file",      readMidiFile },
    };

    const Kind* kindOf (const juce::File& file)
    {
        const auto ext = file.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase();
        for (const auto& k : kinds)
            if (juce::StringArray::fromTokens (k.extensions, ";", {}).contains (ext))
                return &k;
        return nullptr;
    }

    juce::String simplified (juce::String name)
    {
        name = name.toLowerCase();
        for (const auto* tail : { "(x64)", "(64 bit)", "(stereo)", "(mono)", "(m/s)", "(s)", "(m)", " x64", " stereo", " mono", " vst3", " vst", " au" })
            if (name.endsWith (tail))
                name = name.dropLastCharacters ((int) std::strlen (tail));
        juce::String out;
        for (auto c : name)
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                out << c;
        return out;
    }

    int formatRank (const juce::String& format)
    {
        return format == "VST3" ? 0 : format == "AudioUnit" ? 1 : format == "CLAP" ? 2 : 3;
    }
}

juce::String wildcard()
{
    juce::StringArray all;
    for (const auto& k : kinds)
        for (const auto& e : juce::StringArray::fromTokens (k.extensions, ";", {}))
            all.add ("*." + e);
    return all.joinIntoString (";");
}

bool canImport (const juce::File& file)
{
    const auto* k = kindOf (file);
    if (k == nullptr)
        return false;
    return file.existsAsFile() || (file.isDirectory() && file.hasFileExtension ("logicx"));
}

juce::String formatName (const juce::File& file)
{
    const auto* k = kindOf (file);
    return k != nullptr ? juce::String (k->name) : juce::String();
}

bool read (const juce::File& file, Project& out, juce::String& error)
{
    const auto* k = kindOf (file);
    if (k == nullptr)
    {
        error = "This kind of file can't be opened.";
        return false;
    }
    out = {};
    out.name = file.getFileNameWithoutExtension();
    out.folder = file.getParentDirectory();
    out.sourceFormat = k->name;
    try
    {
        return k->reader (file, out, error);
    }
    catch (const std::exception& e)
    {
        error = "The " + juce::String (k->name) + " project couldn't be read (" + e.what() + ").";
        return false;
    }
}

std::optional<juce::PluginDescription> matchPlugin (const juce::KnownPluginList& known, const Plugin& plugin)
{
    const auto types = known.getTypes();
    const auto wanted = simplified (plugin.name);
    if (wanted.isEmpty())
        return {};

    const juce::PluginDescription* best = nullptr;
    int bestScore = 1 << 30;
    for (const auto& t : types)
    {
        int score = 0;
        const auto name = simplified (t.name);
        if (name == wanted)
            score = 0;
        else if (name.isNotEmpty() && (name.startsWith (wanted) || wanted.startsWith (name)) && juce::jmin (name.length(), wanted.length()) >= 4)
            score = 100;
        else
            continue;

        if (plugin.vendor.isNotEmpty() && t.manufacturerName.isNotEmpty()
            && ! simplified (t.manufacturerName).contains (simplified (plugin.vendor).substring (0, 5))
            && ! simplified (plugin.vendor).contains (simplified (t.manufacturerName).substring (0, 5)))
            score += 30;
        if (t.isInstrument != plugin.instrument)
            score += 20;
        // The same format first (its settings come across as they are), then VST3, AU, CLAP.
        score += t.pluginFormatName == plugin.format ? 0 : 5 + formatRank (t.pluginFormatName);

        if (score < bestScore)
        {
            bestScore = score;
            best = &t;
        }
    }
    if (best == nullptr)
        return {};
    return *best;
}

//==============================================================================
namespace
{
    struct Builder
    {
        Project& in;
        const PluginMatcher& matcher;
        juce::ValueTree project;
        TempoMap map;

        double seconds (Time t) const { return t.beats ? map.secondsAt (t.value) : t.value; }
        double beats (Time t) const   { return t.beats ? t.value : map.beatAt (t.value); }

        /** A plugin's description and state for us, or false (noted) when it isn't installed. */
        bool resolve (const Plugin& p, juce::String& xml, juce::String& state)
        {
            const auto description = matcher ? matcher (p) : std::nullopt;
            if (! description.has_value())
            {
                in.note ("Not installed here, left out: " + p.name + (p.format.isNotEmpty() ? " (" + p.format + ")" : juce::String()));
                return false;
            }
            if (auto e = description->createXml())
                xml = e->toString (juce::XmlElement::TextFormat().singleLine());

            juce::MemoryBlock block;
            if (description->pluginFormatName == "VST3" && p.vst3Component.getSize() > 0)
                block = juceVst3State (p.vst3Component, p.vst3Controller);
            else if (p.juceState.getSize() > 0 && description->pluginFormatName == p.format)
                block = p.juceState;

            if (block.getSize() > 0)
                state = block.toBase64Encoding();
            else if (p.vst3Component.getSize() > 0 || p.juceState.getSize() > 0)
                in.note (p.name + " opened as " + description->pluginFormatName + " with its default settings (its settings were saved for "
                         + (p.format.isNotEmpty() ? p.format : juce::String ("another format")) + ")");
            return xml.isNotEmpty();
        }

        void addInserts (const juce::String& owner, const std::vector<Plugin>& plugins)
        {
            int slot = 0;
            for (const auto& p : plugins)
            {
                juce::String xml, state;
                if (! resolve (p, xml, state))
                    continue;
                auto insert = model::addPluginInsert (model::chainFor (project, owner), xml, slot++, nullptr);
                if (state.isNotEmpty())
                    insert.setProperty (ids::pluginState, state, nullptr);
                insert.setProperty (ids::bypass, p.bypassed, nullptr);
            }
        }

        void setUpTempo()
        {
            project.setProperty (ids::tempo, juce::jlimit (20.0, 400.0, in.bpm), nullptr);

            // We count in quarter notes: 6/8 is 3 beats a bar, 7/8 rounds to 4 (noted).
            const auto quarters = [this] (int num, int den)
            {
                const auto q = num * 4.0 / juce::jmax (1, den);
                if (std::abs (q - std::round (q)) > 1.0e-6)
                    in.note ("The time signature " + juce::String (num) + "/" + juce::String (den) + " is counted here as "
                             + juce::String (juce::jmax (1, juce::roundToInt (q))) + "/4");
                return juce::jlimit (1, 16, juce::roundToInt (q));
            };
            project.setProperty (ids::timeSigNumerator, quarters (in.numerator, in.denominator), nullptr);

            std::sort (in.tempoChanges.begin(), in.tempoChanges.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
            for (const auto& t : in.tempoChanges)
                if (t.beat > 1.0e-6)
                    model::addTempoChange (project, t.beat, t.bpm, nullptr);

            TempoMap meters;
            meters.setBaseMeter (quarters (in.numerator, in.denominator));
            std::sort (in.signatureChanges.begin(), in.signatureChanges.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
            for (const auto& s : in.signatureChanges)
            {
                if (s.beat <= 1.0e-6)
                    continue;
                const auto [bar, into] = meters.barAndBeat (s.beat);
                const auto at = into > 1.0e-6 ? bar + 1 : bar;   // (changes fall on bar lines here)
                meters.addMeterChange (at, quarters (s.numerator, s.denominator));
                model::addSignatureChange (project, at + 1, quarters (s.numerator, s.denominator), nullptr);
            }
            map = model::tempoMapOf (project);
        }

        juce::ValueTree addRegion (juce::ValueTree track, const Region& r)
        {
            const auto startBeat = beats (r.start);
            const auto endBeat = r.length.beats ? startBeat + r.length.value : map.beatAt (seconds (r.start) + r.length.value);
            const auto start = map.secondsAt (startBeat);
            auto end = map.secondsAt (endBeat);
            if (end - start < model::minRegionLength)
                return {};

            // A loop: the region is one pass, repeated to the end.
            double loopEnd = 0.0;
            if (r.loopLength > 1.0e-6)
            {
                const auto passEnd = r.length.beats ? map.secondsAt (startBeat + r.loopLength) : start + r.loopLength;
                if (passEnd < end - 1.0e-4)
                {
                    loopEnd = end;
                    end = passEnd;
                }
            }

            juce::ValueTree region;
            if (r.midi)
            {
                region = model::addMidiRegion (track, start, end - start, nullptr);
                const auto origin = startBeat - r.offsetBeats;     // where its notes' beat 0 is
                const auto originSeconds = map.secondsAt (origin);
                region.setProperty (ids::offset, juce::jmax (0.0, start - originSeconds), nullptr);
                for (const auto& n : r.notes)
                {
                    const auto noteStart = map.secondsAt (origin + n.start) - originSeconds;
                    const auto noteEnd = map.secondsAt (origin + n.start + juce::jmax (0.001, n.length)) - originSeconds;
                    if (noteStart < -1.0e-6)
                        continue;
                    model::addNote (region, n.pitch, noteStart, noteEnd - noteStart, n.velocity, nullptr);
                }
            }
            else
            {
                // A file that isn't here: expected next to the project it came from (shown as missing).
                const auto missingName = (r.missingFile.isNotEmpty() ? r.missingFile : r.name).replaceCharacter ('\\', '/');
                const auto file = r.file.existsAsFile() ? r.file
                                : juce::File::isAbsolutePath (missingName) ? juce::File (missingName)
                                : (in.folder != juce::File() ? in.folder : juce::File::getCurrentWorkingDirectory()).getChildFile (missingName.fromLastOccurrenceOf ("/", false, false));
                region = model::addRegion (track, file, start, end - start, nullptr);
                if (! r.file.existsAsFile())
                    in.note ("Audio file not found (put it next to the project and open it again, or drag it in): " + file.getFileName());
                region.setProperty (ids::offset, juce::jmax (0.0, r.offsetSeconds), nullptr);
                if (r.fadeIn > 0.0)  region.setProperty (ids::fadeIn, juce::jmin (r.fadeIn, end - start), nullptr);
                if (r.fadeOut > 0.0) region.setProperty (ids::fadeOut, juce::jmin (r.fadeOut, end - start - (double) region.getProperty (ids::fadeIn, 0.0)), nullptr);
                if (r.sourceBpm > 0.0 && std::abs (r.sourceBpm - map.bpmAt (start)) > 0.05)
                {
                    // A warped clip at its own tempo: Flex follows the song's.
                    region.setProperty (ids::flex, true, nullptr);
                    region.setProperty (ids::sourceBpm, r.sourceBpm, nullptr);
                    if (track.getProperty (ids::flexMode, "off").toString() == "off")
                        track.setProperty (ids::flexMode, "polyphonic", nullptr);
                }
            }

            if (r.name.isNotEmpty())
                region.setProperty (ids::name, r.name, nullptr);
            if (std::abs (r.gain - 1.0) > 1.0e-6)
                region.setProperty (ids::gain, juce::jlimit (0.0, 8.0, r.gain), nullptr);
            if (std::abs (r.transpose) > 1.0e-6)
                region.setProperty (ids::transpose, r.transpose, nullptr);
            if (r.muted)
                region.setProperty (ids::mute, true, nullptr);
            if (loopEnd > 0.0)
            {
                region.setProperty (ids::loop, true, nullptr);
                region.setProperty (ids::loopEnd, loopEnd, nullptr);
            }
            return region;
        }

        juce::ValueTree run()
        {
            project = model::createProject (in.name);
            setUpTempo();

            std::map<juce::String, juce::ValueTree> byKey;

            for (auto& t : in.tracks)
            {
                const bool hasMidi = std::any_of (t.regions.begin(), t.regions.end(), [] (const Region& r) { return r.midi; });
                if (hasMidi && t.kind == Track::Kind::audio)
                    t.kind = Track::Kind::instrument;
                if (hasMidi && t.kind == Track::Kind::instrument)
                    for (auto& r : t.regions)
                        if (! r.midi)
                            in.note ("Audio on the instrument track \"" + t.name + "\" was left out (" + r.name + ")");

                const auto kind = t.isGroup || t.kind == Track::Kind::bus ? model::busKind
                                : t.kind == Track::Kind::instrument ? model::instrumentKind : model::audioKind;
                auto track = model::addTrack (project, t.name.isNotEmpty() ? t.name : juce::String ("Track"), nullptr, -1, kind);
                if (! t.colour.isTransparent())
                    track.setProperty (ids::colour, t.colour.withAlpha (1.0f).toString(), nullptr);
                track.setProperty (ids::volume, juce::jlimit (0.0, 4.0, t.volume), nullptr);
                track.setProperty (ids::pan, juce::jlimit (-1.0, 1.0, t.pan), nullptr);
                track.setProperty (ids::mute, t.mute, nullptr);
                track.setProperty (ids::solo, t.solo, nullptr);
                if (t.mono)
                    model::setStereoChannel (track, false, nullptr);
                if (t.isGroup)
                    track.setProperty (ids::stack, "summing", nullptr);
                if (t.key.isNotEmpty())
                    byKey[t.key] = track;


                if (t.instrumentPlugin.has_value() && kind == model::instrumentKind)
                {
                    juce::String xml, state;
                    if (resolve (*t.instrumentPlugin, xml, state))
                    {
                        track.setProperty (ids::instrument, "plugin", nullptr);
                        track.setProperty (ids::pluginDescription, xml, nullptr);
                        track.setProperty (ids::pluginState, state, nullptr);
                    }
                    else if (! t.samplerFile.existsAsFile() && ! t.drums)
                        in.note ("\"" + t.name + "\" plays our built-in synth instead");
                }
                if (kind == model::instrumentKind && track[ids::instrument].toString() != "plugin")
                {
                    if (t.samplerFile.existsAsFile())
                    {
                        track.setProperty (ids::instrument, "sampler", nullptr);
                        track.setProperty (ids::samplerFile, t.samplerFile.getFullPathName(), nullptr);
                        track.setProperty (ids::samplerRoot, t.samplerRoot, nullptr);
                    }
                    else if (t.drums)
                        track.setProperty (ids::instrument, "drums", nullptr);
                }

                if (kind != model::busKind)
                    for (const auto& r : t.regions)
                        if (r.midi == (kind == model::instrumentKind))
                            addRegion (track, r);

                // Session clips: into the Live Loops grid
                if (kind != model::busKind)
                    for (const auto& [scene, clip] : t.cells)
                        if (clip.midi == (kind == model::instrumentKind))
                        {
                            auto timed = clip;
                            timed.start = Time::inBeats (0.0);
                            timed.loopLength = 0.0;
                            if (auto region = addRegion (track, timed); region.isValid())
                            {
                                model::setLiveCell (project, track[ids::id].toString(), scene, region, nullptr);
                                track.removeChild (region, nullptr);
                            }
                        }

                addInserts (track[ids::id].toString(), t.inserts);
            }

            // Groups: members play into their group (a summing stack), which sits just above them.
            for (const auto& t : in.tracks)
            {
                if (t.groupKey.isEmpty())
                    continue;
                auto member = byKey.find (t.key);
                auto group = byKey.find (t.groupKey);
                if (member == byKey.end() || group == byKey.end() || member->second == group->second)
                    continue;
                member->second.setProperty (ids::stackParent, group->second[ids::id], nullptr);
                member->second.setProperty (ids::output, group->second[ids::id], nullptr);
            }
            for (const auto& t : in.tracks)
            {
                if (! t.isGroup)
                    continue;
                auto group = byKey.find (t.key);
                if (group == byKey.end())
                    continue;
                juce::Array<juce::ValueTree> members;
                for (const auto& child : project)
                    if (child.hasType (ids::TRACK) && child[ids::stackParent] == group->second[ids::id])
                        members.add (child);
                if (! members.isEmpty() && project.indexOf (group->second) > project.indexOf (members.getFirst()))
                    model::moveTracks (project, { group->second }, project.indexOf (members.getFirst()), nullptr);
            }

            // Sends (once every bus exists)
            for (const auto& t : in.tracks)
            {
                if (t.sends.empty())
                    continue;
                auto from = byKey.find (t.key);
                if (from == byKey.end())
                    continue;
                for (const auto& s : t.sends)
                    if (auto to = byKey.find (s.targetKey); to != byKey.end() && s.level > 1.0e-4)
                    {
                        auto send = model::addSend (model::chainFor (project, from->second[ids::id].toString()), to->second[ids::id].toString(), s.level, nullptr);
                        send.setProperty (ids::pre, s.pre, nullptr);
                    }
            }

            addInserts (model::masterOwner, in.masterInserts);
            if (in.masterVolume >= 0.0)
                project.setProperty (ids::masterGain, juce::jlimit (0.0, 2.0, in.masterVolume), nullptr);

            for (const auto& m : in.markers)
                model::addMarker (project, seconds (m.time), m.name.isNotEmpty() ? m.name : juce::String ("Marker"), nullptr);

            // The cycle over the first 4 bars, or the song if shorter.
            const auto end = model::projectEnd (project);
            if (end > 0.0)
            {
                project.setProperty (ids::cycleStart, 0.0, nullptr);
                project.setProperty (ids::cycleEnd, juce::jmin (end, map.secondsAt (map.beatOfBar (4))), nullptr);
            }
            return project;
        }
    };
}

juce::ValueTree build (Project& imported, const PluginMatcher& matcher)
{
    Builder builder { imported, matcher, {}, {} };
    return builder.run();
}

juce::ValueTree open (const juce::File& file, const PluginMatcher& matcher, juce::StringArray& notes, juce::String& error)
{
    Project imported;
    if (! read (file, imported, error))
        return {};
    auto project = build (imported, matcher);
    notes = imported.notes;
    return project;
}
} // namespace projectimport
} // namespace riff
