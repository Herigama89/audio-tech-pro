#include "ProjectImport.h"

/*
    FL Studio projects (.flp): a binary list of events (as decoded by the PyFLP project).

      "FLhd" <len 6> format, channel count, PPQ        "FLdt" <len> events...
      An event is an id byte, then: ids 0-63 one byte, 64-127 two, 128-191 four,
      192-255 a variable length (7 bits a byte) and that many bytes.

    The ones read here:
      tempo 156 (bpm x 1000; old: 66 + 93), title 194, version 199
      channels: 64 new, 21 type, 22 mixer insert, 196 sample path, 201 plugin's own name, 203 name, 213 plugin data
      patterns: 65 new, 193 name, 224 notes (24 bytes each)
      arrangements: 99 new, 241 name, 233 playlist (32 or 60 bytes an item), markers 148 / 205 / 33 / 34
      playlist tracks: 238 data, 239 name; mixer: 98 slot, 147 insert end
*/
namespace riff::interchange
{
namespace
{
    struct Event
    {
        int id = 0;
        juce::uint32 value = 0;
        const juce::uint8* data = nullptr;
        size_t size = 0;
    };

    struct PluginInfo
    {
        juce::String internalName, name, vendor, path;
        bool wrapper = false;
    };

    struct Channel
    {
        int index = 0, type = 0, insert = -1;
        juce::String name, samplePath;
        PluginInfo plugin;
    };

    struct NoteEvent { juce::uint32 position = 0, length = 0; int channel = 0, key = 60, velocity = 100; };
    struct Pattern { int index = 0; juce::String name; std::vector<NoteEvent> notes; };
    struct Item { juce::uint32 position = 0, length = 0; int pattern = -1, channel = -1, track = 0; double startOffset = 0.0; bool muted = false; };

    struct Reader
    {
        Project& out;
        juce::File folder;
        bool utf16 = true;
        int ppq = 96;

        std::vector<Channel> channels;
        std::map<int, Pattern> patterns;
        std::vector<Item> items;
        std::map<int, juce::String> trackNames;
        std::map<int, std::vector<PluginInfo>> insertPlugins;

        juce::String text (const Event& e) const
        {
            if (e.data == nullptr || e.size == 0)
                return {};
            juce::String s;
            if (utf16)
            {
                for (size_t i = 0; i + 1 < e.size; i += 2)
                {
                    const auto c = (juce::juce_wchar) u16 (e.data + i);
                    if (c == 0)
                        break;
                    s += juce::String::charToString (c);
                }
                return s;
            }
            size_t n = 0;
            while (n < e.size && e.data[n] != 0)
                ++n;
            return juce::String::fromUTF8 (reinterpret_cast<const char*> (e.data), (int) n);
        }

        static juce::uint32 u32 (const juce::uint8* p) { return juce::ByteOrder::littleEndianInt (p); }
        static juce::uint16 u16 (const juce::uint8* p) { return juce::ByteOrder::littleEndianShort (p); }

        /** The wrapper's data: a type, then chunks of (id u32, size u64, data). */
        void readWrapper (const Event& e, PluginInfo& p)
        {
            if (e.size < 4)
                return;
            size_t pos = 4;
            while (pos + 12 <= e.size)
            {
                const auto id = u32 (e.data + pos);
                const auto size = (size_t) juce::ByteOrder::littleEndianInt64 (e.data + pos + 4);
                pos += 12;
                if (size > e.size - pos)
                    break;
                size_t n = 0;
                while (n < size && e.data[pos + n] != 0)
                    ++n;
                const auto str = juce::String::fromUTF8 (reinterpret_cast<const char*> (e.data + pos), (int) n);
                if (id == 54) p.name = str;
                else if (id == 56) p.vendor = str;
                else if (id == 55) p.path = str;
                pos += size;
            }
        }

        bool parse (const juce::MemoryBlock& data)
        {
            const auto* bytes = static_cast<const juce::uint8*> (data.getData());
            const auto size = data.getSize();
            if (size < 22 || std::memcmp (bytes, "FLhd", 4) != 0)
                return false;
            const auto headerLength = u32 (bytes + 4);
            if (headerLength >= 6)
                ppq = juce::jmax (1, (int) u16 (bytes + 12));
            size_t pos = 8 + headerLength;
            if (pos + 8 > size || std::memcmp (bytes + pos, "FLdt", 4) != 0)
                return false;
            pos += 8;

            enum class Context { none, channel, pattern, arrangement, mixer } context = Context::none;
            int arrangement = -1, insert = 0;
            PluginInfo pending;
            Channel* channel = nullptr;
            Pattern* pattern = nullptr;
            double tempoCoarse = 0.0, tempoFine = 0.0;
            juce::uint32 markerPosition = 0;
            bool markerIsSignature = false;
            int markerNum = 4;

            while (pos < size)
            {
                Event e;
                e.id = bytes[pos++];
                if (e.id < 64)        { if (pos + 1 > size) break; e.value = bytes[pos]; pos += 1; }
                else if (e.id < 128)  { if (pos + 2 > size) break; e.value = u16 (bytes + pos); pos += 2; }
                else if (e.id < 192)  { if (pos + 4 > size) break; e.value = u32 (bytes + pos); pos += 4; }
                else
                {
                    size_t length = 0;
                    int shift = 0;
                    while (pos < size)
                    {
                        const auto b = bytes[pos++];
                        length |= (size_t) (b & 0x7f) << shift;
                        shift += 7;
                        if ((b & 0x80) == 0 || shift > 28)
                            break;
                    }
                    if (length > size - pos)
                        break;
                    e.data = bytes + pos;
                    e.size = length;
                    pos += length;
                }

                switch (e.id)
                {
                    case 199:   // version: text is UTF-16 from 11.5
                    {
                        const auto version = juce::String::fromUTF8 (reinterpret_cast<const char*> (e.data), (int) e.size);
                        const auto parts = juce::StringArray::fromTokens (version, ".", {});
                        const auto major = parts[0].getIntValue(), minor = parts[1].getIntValue();
                        utf16 = major > 11 || (major == 11 && minor >= 5);
                        break;
                    }
                    case 156: out.bpm = e.value / 1000.0; break;
                    case 66:  tempoCoarse = e.value; break;
                    case 93:  tempoFine = e.value; break;
                    case 194: if (const auto title = text (e); title.isNotEmpty()) out.name = title; break;

                    case 64:   // a channel
                        channels.push_back ({});
                        channel = &channels.back();
                        channel->index = (int) e.value;
                        context = Context::channel;
                        break;
                    case 21: if (context == Context::channel && channel != nullptr) channel->type = (int) e.value; break;
                    case 22: if (context == Context::channel && channel != nullptr) channel->insert = (int) (juce::int8) e.value; break;
                    case 196: if (context == Context::channel && channel != nullptr) channel->samplePath = text (e); break;

                    case 201: case 203: case 213:
                    {
                        auto& p = context == Context::channel && channel != nullptr ? channel->plugin : pending;
                        if (e.id == 201) { p.internalName = text (e); p.wrapper = p.internalName.equalsIgnoreCase ("Fruity Wrapper"); }
                        else if (e.id == 203) { if (context == Context::channel && channel != nullptr) channel->name = text (e); else if (p.name.isEmpty()) p.name = text (e); }
                        else if (p.wrapper) readWrapper (e, p);
                        break;
                    }

                    case 65:   // a pattern (its events may come in more than one place)
                        pattern = &patterns[(int) e.value];
                        pattern->index = (int) e.value;
                        context = Context::pattern;
                        break;
                    case 193: if (pattern != nullptr && context == Context::pattern) pattern->name = text (e); break;
                    case 224:
                        if (pattern != nullptr)
                            for (size_t i = 0; i + 24 <= e.size; i += 24)
                            {
                                const auto* n = e.data + i;
                                NoteEvent note;
                                note.position = u32 (n);
                                note.channel = u16 (n + 6);
                                note.length = u32 (n + 8);
                                note.key = (int) u16 (n + 12);
                                note.velocity = n[21];
                                pattern->notes.push_back (note);
                            }
                        break;

                    case 99: ++arrangement; context = Context::arrangement; break;
                    case 233:
                        if (arrangement <= 0)   // (the first arrangement; older files have none)
                        {
                            const size_t itemSize = e.size % 60 == 0 && e.size % 32 != 0 ? 60 : 32;
                            for (size_t i = 0; i + itemSize <= e.size; i += itemSize)
                            {
                                const auto* p = e.data + i;
                                Item item;
                                item.position = u32 (p);
                                const auto base = u16 (p + 4), index = u16 (p + 6);
                                item.length = u32 (p + 8);
                                const auto reversed = (int) u16 (p + 12);
                                item.track = reversed <= 499 ? 499 - reversed : 0;
                                const auto flags = u16 (p + 18);
                                item.muted = (flags & 0x2000) != 0;
                                if (index >= base && base > 0)
                                    item.pattern = index - base;
                                else
                                    item.channel = index;
                                juce::int32 offset;
                                std::memcpy (&offset, p + 24, 4);
                                if (item.pattern >= 0)
                                    item.startOffset = offset > 0 ? (double) offset : 0.0;
                                else
                                {
                                    float seconds;
                                    std::memcpy (&seconds, p + 24, 4);
                                    item.startOffset = std::isfinite (seconds) && seconds > 0.0f && seconds < 36000.0f ? (double) seconds : 0.0;
                                }
                                items.push_back (item);
                            }
                        }
                        break;
                    case 148:
                        markerIsSignature = e.value >= 0x08000000;
                        markerPosition = e.value & 0x07ffffff;
                        if (arrangement <= 0 && ! markerIsSignature)
                            out.markers.push_back ({ Time::inBeats (markerPosition / (double) ppq), "Marker" });
                        break;
                    case 33: markerNum = (int) e.value; break;
                    case 34:
                        if (arrangement <= 0 && markerIsSignature && e.value > 0)
                        {
                            if (markerPosition == 0) { out.numerator = markerNum; out.denominator = (int) e.value; }
                            else out.signatureChanges.push_back ({ markerPosition / (double) ppq, markerNum, (int) e.value });
                        }
                        break;
                    case 205:
                        if (arrangement <= 0 && ! markerIsSignature && ! out.markers.empty() && text (e).isNotEmpty())
                            out.markers.back().name = text (e);
                        break;
                    case 239:
                        if (arrangement <= 0)
                            trackNames[(int) trackNames.size()] = text (e);
                        break;

                    case 98:   // a mixer slot: the plugin just described is in it
                        context = Context::mixer;
                        if (pending.internalName.isNotEmpty() || pending.name.isNotEmpty())
                            insertPlugins[insert].push_back (pending);
                        pending = {};
                        break;
                    case 147:  // the end of a mixer insert
                        context = Context::mixer;
                        ++insert;
                        pending = {};
                        break;
                    default: break;
                }
            }
            if (out.bpm <= 0.0 || tempoCoarse > 0.0)
                out.bpm = tempoCoarse > 0.0 ? tempoCoarse + tempoFine / 1000.0 : 120.0;
            return true;
        }

        std::optional<Plugin> pluginOf (const PluginInfo& info, bool instrument, const juce::String& where)
        {
            if (info.wrapper && info.name.isNotEmpty())
            {
                Plugin p;
                p.name = info.name;
                p.vendor = info.vendor;
                p.instrument = instrument;
                const auto ext = info.path.fromLastOccurrenceOf (".", false, false).toLowerCase();
                p.format = ext == "vst3" ? "VST3" : ext == "component" ? "AudioUnit" : ext == "clap" ? "CLAP" : "VST";
                out.note (p.name + " opened with its default settings (FL keeps plugin settings in its own form)");
                return p;
            }
            if (info.internalName.isNotEmpty() && ! info.wrapper)
                out.note ("FL Studio's own plugins can't come across (\"" + info.internalName + "\" on " + where + ")");
            return {};
        }

        void build()
        {
            const auto ticks = [this] (juce::uint32 t) { return t / (double) ppq; };

            // A track per channel that plays notes, an audio track per playlist track with audio clips.
            std::map<int, size_t> channelTrack;   // channel index -> out.tracks index
            for (const auto& c : channels)
            {
                bool used = false;
                for (const auto& item : items)
                    if (item.pattern >= 0)
                        if (auto it = patterns.find (item.pattern); it != patterns.end())
                            for (const auto& n : it->second.notes)
                                if (n.channel == c.index) { used = true; break; }
                if (! used)
                    continue;

                Track t;
                t.key = "channel:" + juce::String (c.index);
                t.name = c.name.isNotEmpty() ? c.name : c.plugin.name.isNotEmpty() ? c.plugin.name : "Channel " + juce::String (c.index + 1);
                t.kind = Track::Kind::instrument;
                if (c.type == 0 && c.samplePath.isNotEmpty())   // a sampler channel: our sampler
                    t.samplerFile = findMedia (c.samplePath, folder);
                else if (auto p = pluginOf (c.plugin, true, "\"" + t.name + "\""))
                    t.instrumentPlugin = *p;
                channelTrack[c.index] = out.tracks.size();
                out.tracks.push_back (std::move (t));
            }

            // Patterns on the playlist: a region on each channel's track that has notes in it.
            for (const auto& item : items)
            {
                if (item.pattern < 0)
                    continue;
                auto it = patterns.find (item.pattern);
                if (it == patterns.end())
                    continue;
                std::map<int, Region> perChannel;
                for (const auto& n : it->second.notes)
                {
                    auto& r = perChannel[n.channel];
                    if (r.notes.empty())
                    {
                        r.midi = true;
                        r.name = it->second.name.isNotEmpty() ? it->second.name : "Pattern " + juce::String (item.pattern);
                        r.start = Time::inBeats (ticks (item.position));
                        r.length = Time::inBeats (ticks (item.length));
                        r.offsetBeats = item.startOffset / ppq;
                        r.muted = item.muted;
                    }
                    Note note;
                    note.pitch = juce::jlimit (0, 127, n.key);
                    note.start = ticks (n.position);
                    note.length = juce::jmax (1.0 / 64.0, ticks (n.length));
                    note.velocity = juce::jlimit (1, 127, n.velocity);
                    r.notes.push_back (note);
                }
                for (auto& [channelIndex, region] : perChannel)
                    if (auto t = channelTrack.find (channelIndex); t != channelTrack.end())
                        out.tracks[t->second].regions.push_back (std::move (region));
            }

            // Audio clips: on audio tracks named after their playlist tracks.
            std::map<int, size_t> audioTrack;
            for (const auto& item : items)
            {
                if (item.channel < 0)
                    continue;
                const auto c = std::find_if (channels.begin(), channels.end(), [&item] (const Channel& ch) { return ch.index == item.channel; });
                if (c == channels.end() || c->samplePath.isEmpty())
                    continue;
                if (audioTrack.count (item.track) == 0)
                {
                    Track t;
                    t.key = "playlist:" + juce::String (item.track);
                    t.name = trackNames.count (item.track) != 0 && trackNames[item.track].isNotEmpty() ? trackNames[item.track] : "Track " + juce::String (item.track + 1);
                    audioTrack[item.track] = out.tracks.size();
                    out.tracks.push_back (std::move (t));
                }
                Region r;
                r.file = findMedia (c->samplePath, folder);
                r.missingFile = c->samplePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
                r.name = c->name.isNotEmpty() ? c->name : r.missingFile;
                r.start = Time::inBeats (ticks (item.position));
                r.length = Time::inBeats (ticks (item.length));
                r.offsetSeconds = item.startOffset;
                r.muted = item.muted;
                out.tracks[audioTrack[item.track]].regions.push_back (std::move (r));
            }

            // Mixer inserts' plugins: on the track of the one channel that plays into the insert,
            // or on a bus the channels sum into; insert 0 is the master.
            for (const auto& [insertIndex, plugins] : insertPlugins)
            {
                std::vector<Plugin> effects;
                for (const auto& info : plugins)
                    if (auto p = pluginOf (info, false, insertIndex == 0 ? "the master" : "insert " + juce::String (insertIndex)))
                        effects.push_back (*p);
                if (effects.empty())
                    continue;
                if (insertIndex == 0)
                {
                    out.masterInserts = effects;
                    continue;
                }
                std::vector<size_t> feeding;
                for (const auto& c : channels)
                    if (c.insert == insertIndex)
                        if (auto t = channelTrack.find (c.index); t != channelTrack.end())
                            feeding.push_back (t->second);
                if (feeding.size() == 1)
                    for (auto& p : effects)
                        out.tracks[feeding[0]].inserts.push_back (p);
                else if (feeding.size() > 1)
                {
                    Track bus;
                    bus.key = "insert:" + juce::String (insertIndex);
                    bus.name = "Insert " + juce::String (insertIndex);
                    bus.kind = Track::Kind::bus;
                    bus.isGroup = true;
                    bus.inserts = effects;
                    for (auto i : feeding)
                        out.tracks[i].groupKey = bus.key;
                    out.tracks.push_back (std::move (bus));
                }
            }

            if (channels.empty() && items.empty())
                out.note ("No channels or playlist found");
            out.note ("FL's mixer levels and automation clips don't come across yet");
        }
    };
}

bool readFlStudio (const juce::File& file, Project& out, juce::String& error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
    {
        error = "The file couldn't be opened.";
        return false;
    }
    Reader reader { out, file.getParentDirectory(), true, 96, {}, {}, {}, {}, {} };
    if (! reader.parse (data))
    {
        error = "This doesn't look like an FL Studio project.";
        return false;
    }
    reader.build();
    return true;
}

} // namespace riff::interchange
