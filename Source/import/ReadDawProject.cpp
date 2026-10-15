#include "ProjectImport.h"
#include "ImportXml.h"

/*
    DAWproject (.dawproject): the open exchange format (Bitwig, PreSonus Studio One 6.5+,
    Steinberg Cubase 14+ and others). A zip with project.xml, metadata.xml, the audio
    (audio/...) and the plugins' states (plugins/*.vstpreset, *.aupreset...).

    <Project>
      <Transport><Tempo value/><TimeSignature numerator denominator/></Transport>
      <Structure>
        <Track id name color contentType="audio|notes|tracks"> <Channel role destination>
            <Devices> Vst3Plugin / AuPlugin / ClapPlugin / Vst2Plugin (deviceName, deviceRole, <State path/>, <Enabled/>) </Devices>
            <Mute/> <Pan/> <Volume/> <Sends><Send destination><Volume/></Send></Sends>
          </Channel> (group tracks: Track children) </Track>
        <Channel role="master"/>
      </Structure>
      <Arrangement><Lanes timeUnit><Lanes track><Clips><Clip time duration playStart loopStart loopEnd ...>
          <Notes><Note time duration key vel/></Notes> | <Audio><File path/></Audio> | <Warps><Audio/><Warp time contentTime/></Warps>
        <Markers><Marker time name/></Markers> <TempoAutomation><RealPoint time value/></TempoAutomation>
        <TimeSignatureAutomation><TimeSignaturePoint time numerator denominator/></TimeSignatureAutomation>
*/
namespace riff::interchange
{
namespace
{
    using namespace xml;

    struct Reader
    {
        Project& out;
        juce::ZipFile& zip;
        juce::File projectFile;
        juce::File mediaFolder;
        std::map<juce::String, juce::String> channelToTrack;   // channel id -> track key
        std::map<juce::String, Track*> tracksById;
        std::map<juce::String, juce::File> extracted;
        double bpm = 120.0;

        /** A file the project refers to: unpacked from the zip once, or found on disk. */
        juce::File media (const juce::XmlElement* fileXml)
        {
            if (fileXml == nullptr)
                return {};
            const auto path = fileXml->getStringAttribute ("path");
            if (path.isEmpty())
                return {};
            if (fileXml->getBoolAttribute ("external", false))
                return findMedia (path, projectFile.getParentDirectory());
            if (auto it = extracted.find (path); it != extracted.end())
                return it->second;

            const auto data = zipData (zip, path);
            if (data.getSize() == 0)
                return findMedia (path, projectFile.getParentDirectory());
            mediaFolder.createDirectory();
            auto target = mediaFolder.getChildFile (path.fromLastOccurrenceOf ("/", false, false));
            if (! target.existsAsFile() || target.getSize() != (juce::int64) data.getSize())
                target.replaceWithData (data.getData(), data.getSize());
            return extracted[path] = target;
        }

        static double volumeOf (const juce::XmlElement* v, double fallback = 1.0)
        {
            if (v == nullptr)
                return fallback;
            const auto value = v->getDoubleAttribute ("value", fallback);
            return v->getStringAttribute ("unit") == "decibel" ? dbToGain (value) : value;
        }

        static double panOf (const juce::XmlElement* p)
        {
            if (p == nullptr)
                return 0.0;
            const auto value = p->getDoubleAttribute ("value", 0.5);
            return p->getStringAttribute ("unit", "normalized") == "normalized" ? value * 2.0 - 1.0 : value;
        }

        void readDevices (const juce::XmlElement* devices, Track& t, std::vector<Plugin>& inserts, bool allowInstrument)
        {
            if (devices == nullptr)
                return;
            for (auto* d : devices->getChildIterator())
            {
                const auto tag = d->getTagName();
                Plugin p;
                p.name = d->getStringAttribute ("deviceName", d->getStringAttribute ("name"));
                p.vendor = d->getStringAttribute ("deviceVendor");
                p.uid = d->getStringAttribute ("deviceID");
                p.instrument = d->getStringAttribute ("deviceRole") == "instrument";
                if (const auto* enabled = d->getChildByName ("Enabled"))
                    p.bypassed = ! enabled->getBoolAttribute ("value", true);
                const auto statePath = d->getChildByName ("State") != nullptr ? d->getChildByName ("State")->getStringAttribute ("path") : juce::String();
                const auto state = statePath.isNotEmpty() ? zipData (zip, statePath) : juce::MemoryBlock();

                if (tag == "Vst3Plugin")
                {
                    p.format = "VST3";
                    juce::String classId;
                    if (state.getSize() > 0)
                        readVstPreset (state, classId, p.vst3Component, p.vst3Controller);
                }
                else if (tag == "AuPlugin")
                {
                    p.format = "AudioUnit";
                    p.juceState = state;
                }
                else if (tag == "ClapPlugin")
                {
                    p.format = "CLAP";
                    if (state.getSize() > 0)
                        out.note (p.name + " (CLAP) opened with its default settings");
                }
                else if (tag == "Vst2Plugin")
                {
                    p.format = "VST";
                    out.note (p.name + " was a VST2 plugin: its VST3 or AU version is used, with its default settings");
                }
                else
                {
                    out.note ("Built-in devices of the other program can't come across (\"" + (p.name.isNotEmpty() ? p.name : tag) + "\")");
                    continue;
                }
                if (p.name.isEmpty())
                    continue;
                if (p.instrument && allowInstrument && ! t.instrumentPlugin.has_value())
                    t.instrumentPlugin = p;
                else if (d->getStringAttribute ("deviceRole") != "noteFX")
                    inserts.push_back (p);
            }
        }

        void readChannel (const juce::XmlElement* channel, Track& t)
        {
            if (channel == nullptr)
                return;
            t.volume = volumeOf (channel->getChildByName ("Volume"));
            t.pan = panOf (channel->getChildByName ("Pan"));
            if (const auto* mute = channel->getChildByName ("Mute"))
                t.mute = mute->getBoolAttribute ("value");
            t.solo = channel->getBoolAttribute ("solo");
            t.mono = channel->getIntAttribute ("audioChannels", 2) == 1;
            channelToTrack[channel->getStringAttribute ("id")] = t.key;
            readDevices (channel->getChildByName ("Devices"), t, t.inserts, t.kind == Track::Kind::instrument);
        }

        void readTrack (const juce::XmlElement* x, const juce::String& groupKey)
        {
            const auto content = x->getStringAttribute ("contentType");
            Track t;
            t.key = x->getStringAttribute ("id");
            t.name = x->getStringAttribute ("name");
            t.colour = x->hasAttribute ("color") ? juce::Colour::fromString ("ff" + x->getStringAttribute ("color").trimCharactersAtStart ("#")) : juce::Colours::transparentBlack;
            t.groupKey = groupKey;
            const bool group = content.contains ("tracks") || x->getChildByName ("Track") != nullptr;
            t.kind = group ? Track::Kind::bus : content.contains ("notes") ? Track::Kind::instrument : Track::Kind::audio;
            t.isGroup = group;
            const auto* channel = x->getChildByName ("Channel");
            if (channel != nullptr && ! group)
            {
                const auto role = channel->getStringAttribute ("role");
                if (role == "effect" || role == "submix")
                    t.kind = Track::Kind::bus;
            }
            readChannel (channel, t);
            pendingSends.emplace_back (t.key, channel);
            out.tracks.push_back (std::move (t));

            for (auto* child : x->getChildWithTagNameIterator ("Track"))
                readTrack (child, x->getStringAttribute ("id"));
        }

        std::vector<std::pair<juce::String, const juce::XmlElement*>> pendingSends;

        void resolveSends()
        {
            for (auto& t : out.tracks)
                tracksById[t.key] = &t;
            for (const auto& [key, channel] : pendingSends)
            {
                if (channel == nullptr)
                    continue;
                auto* track = tracksById[key];
                if (const auto* sends = channel->getChildByName ("Sends"))
                    for (auto* s : sends->getChildWithTagNameIterator ("Send"))
                    {
                        if (const auto* enable = s->getChildByName ("Enable"); enable != nullptr && ! enable->getBoolAttribute ("value", true))
                            continue;
                        const auto target = channelToTrack[s->getStringAttribute ("destination")];
                        if (target.isNotEmpty() && track != nullptr)
                            track->sends.push_back ({ target, volumeOf (s->getChildByName ("Volume"), 0.0), s->getStringAttribute ("type") == "pre" });
                    }
                // A track playing into a group or effect channel (not the master): a summing stack.
                const auto destination = channelToTrack[channel->getStringAttribute ("destination")];
                if (track != nullptr && destination.isNotEmpty() && track->groupKey.isEmpty() && destination != key)
                    track->groupKey = destination;
            }
        }

        static double toSeconds (double v, const juce::String& unit, double bpm)
        {
            return unit == "seconds" ? v : v * 60.0 / bpm;
        }

        void readClip (const juce::XmlElement* clip, const juce::String& unit, Track& t, double outerShift)
        {
            const auto time = clip->getDoubleAttribute ("time") + outerShift;
            const auto duration = clip->getDoubleAttribute ("duration");
            const auto contentUnit = clip->getStringAttribute ("contentTimeUnit", unit);
            const auto playStart = clip->getDoubleAttribute ("playStart");
            const bool looped = clip->hasAttribute ("loopStart") && clip->hasAttribute ("loopEnd");
            if (duration <= 0.0)
                return;

            // Clips inside a clip
            if (const auto* inner = clip->getChildByName ("Clips"))
            {
                for (auto* c : inner->getChildWithTagNameIterator ("Clip"))
                    readClip (c, contentUnit, t, time - playStart);
                return;
            }

            Region r;
            r.name = clip->getStringAttribute ("name");
            r.muted = ! clip->getBoolAttribute ("enable", true);
            r.start = unit == "seconds" ? Time::inSeconds (time) : Time::inBeats (time);
            r.length = unit == "seconds" ? Time::inSeconds (duration) : Time::inBeats (duration);
            if (looped)
            {
                const auto loopLength = clip->getDoubleAttribute ("loopEnd") - clip->getDoubleAttribute ("loopStart");
                if (contentUnit == unit)
                    r.loopLength = loopLength;
                else
                    r.loopLength = contentUnit == "seconds" ? loopLength * bpm / 60.0 : loopLength * 60.0 / bpm;
            }
            const auto fadeUnit = clip->getStringAttribute ("fadeTimeUnit", unit);
            r.fadeIn = toSeconds (clip->getDoubleAttribute ("fadeInTime"), fadeUnit, bpm);
            r.fadeOut = toSeconds (clip->getDoubleAttribute ("fadeOutTime"), fadeUnit, bpm);

            const auto* notes = find (clip, "Notes");
            if (notes != nullptr)
            {
                r.midi = true;
                const auto toBeats = [&] (double v) { return contentUnit == "seconds" ? v * bpm / 60.0 : v; };
                r.offsetBeats = toBeats (playStart);
                for (auto* n : notes->getChildWithTagNameIterator ("Note"))
                {
                    Note note;
                    note.pitch = juce::jlimit (0, 127, n->getIntAttribute ("key", 60));
                    note.start = toBeats (n->getDoubleAttribute ("time"));
                    note.length = toBeats (n->getDoubleAttribute ("duration", 0.25));
                    note.velocity = juce::jlimit (1, 127, juce::roundToInt (n->getDoubleAttribute ("vel", 0.8) * 127.0));
                    r.notes.push_back (note);
                }
                t.regions.push_back (std::move (r));
                return;
            }

            const auto* warps = find (clip, "Warps");
            const auto* audio = find (clip, "Audio");
            if (audio == nullptr)
                return;
            r.file = media (audio->getChildByName ("File"));
            r.missingFile = audio->getChildByName ("File") != nullptr ? audio->getChildByName ("File")->getStringAttribute ("path") : juce::String();
            if (r.name.isEmpty())
                r.name = r.file.getFileNameWithoutExtension();

            if (warps != nullptr)
            {
                // Warp markers: (clip time, seconds in the file)
                std::vector<std::pair<double, double>> points;
                const auto warpUnit = warps->getStringAttribute ("timeUnit", contentUnit);
                for (auto* w : warps->getChildWithTagNameIterator ("Warp"))
                    points.emplace_back (w->getDoubleAttribute ("time"), w->getDoubleAttribute ("contentTime"));
                std::sort (points.begin(), points.end());
                const auto fileSecondsAt = [&points] (double t)
                {
                    if (points.size() < 2)
                        return points.empty() ? t : points[0].second + (t - points[0].first);
                    size_t i = 1;
                    while (i + 1 < points.size() && points[i].first < t)
                        ++i;
                    const auto& a = points[i - 1];
                    const auto& b = points[i];
                    return a.second + (t - a.first) * (b.second - a.second) / juce::jmax (1.0e-9, b.first - a.first);
                };
                r.offsetSeconds = fileSecondsAt (playStart);
                if (points.size() >= 2 && warpUnit == "beats")
                {
                    const auto beats = points.back().first - points.front().first;
                    const auto secs = points.back().second - points.front().second;
                    if (secs > 1.0e-6)
                        r.sourceBpm = 60.0 * beats / secs;
                }
            }
            else
                r.offsetSeconds = toSeconds (playStart, contentUnit, bpm);

            t.regions.push_back (std::move (r));
        }

        void readLanes (const juce::XmlElement* lanes, const juce::String& inherited)
        {
            const auto unit = lanes->getStringAttribute ("timeUnit", inherited);
            const auto trackRef = lanes->getStringAttribute ("track");
            for (auto* child : lanes->getChildIterator())
            {
                if (child->hasTagName ("Lanes"))
                    readLanes (child, unit);
                else if (child->hasTagName ("Clips") && trackRef.isNotEmpty())
                {
                    auto key = tracksById.count (trackRef) != 0 ? trackRef : channelToTrack[trackRef];
                    if (auto* t = tracksById[key])
                        for (auto* clip : child->getChildWithTagNameIterator ("Clip"))
                            readClip (clip, child->getStringAttribute ("timeUnit", unit), *t, 0.0);
                }
                else if (child->hasTagName ("Markers"))
                    readMarkers (child, unit);
            }
        }

        void readMarkers (const juce::XmlElement* markers, const juce::String& unit)
        {
            for (auto* m : markers->getChildWithTagNameIterator ("Marker"))
            {
                const auto time = m->getDoubleAttribute ("time");
                out.markers.push_back ({ unit == "seconds" ? Time::inSeconds (time) : Time::inBeats (time), m->getStringAttribute ("name") });
            }
        }

        bool run (const juce::XmlElement& root)
        {
            if (const auto* tempo = at (&root, "Transport/Tempo"))
                out.bpm = bpm = tempo->getDoubleAttribute ("value", 120.0);
            if (const auto* sig = at (&root, "Transport/TimeSignature"))
            {
                out.numerator = sig->getIntAttribute ("numerator", 4);
                out.denominator = sig->getIntAttribute ("denominator", 4);
            }

            const auto* structure = root.getChildByName ("Structure");
            if (structure == nullptr)
                return false;
            for (auto* x : structure->getChildIterator())
            {
                if (x->hasTagName ("Track"))
                    readTrack (x, {});
                else if (x->hasTagName ("Channel") && x->getStringAttribute ("role") == "master")
                {
                    channelToTrack[x->getStringAttribute ("id")] = {};
                    out.masterVolume = volumeOf (x->getChildByName ("Volume"), -1.0);
                    Track master;
                    readDevices (x->getChildByName ("Devices"), master, out.masterInserts, false);
                }
            }
            resolveSends();

            if (const auto* arrangement = root.getChildByName ("Arrangement"))
            {
                for (auto* child : arrangement->getChildIterator())
                {
                    if (child->hasTagName ("Lanes"))
                        readLanes (child, "beats");
                    else if (child->hasTagName ("Markers"))
                        readMarkers (child, "beats");
                    else if (child->hasTagName ("TempoAutomation"))
                    {
                        for (auto* p : child->getChildWithTagNameIterator ("RealPoint"))
                        {
                            const auto time = p->getDoubleAttribute ("time");
                            if (time <= 1.0e-6)
                                out.bpm = bpm = p->getDoubleAttribute ("value", bpm);
                            else
                                out.tempoChanges.push_back ({ time, p->getDoubleAttribute ("value", bpm) });
                        }
                    }
                    else if (child->hasTagName ("TimeSignatureAutomation"))
                    {
                        for (auto* p : child->getChildWithTagNameIterator ("TimeSignaturePoint"))
                        {
                            const auto time = p->getDoubleAttribute ("time");
                            if (time <= 1.0e-6)
                            {
                                out.numerator = p->getIntAttribute ("numerator", 4);
                                out.denominator = p->getIntAttribute ("denominator", 4);
                            }
                            else
                                out.signatureChanges.push_back ({ time, p->getIntAttribute ("numerator", 4), p->getIntAttribute ("denominator", 4) });
                        }
                    }
                }
            }

            if (const auto* app = root.getChildByName ("Application"))
                out.sourceFormat = "DAWproject from " + app->getStringAttribute ("name", "another program");
            return true;
        }
    };
}

bool readDawProject (const juce::File& file, Project& out, juce::String& error)
{
    juce::ZipFile zip (file);
    const auto text = xml::zipText (zip, "project.xml");
    const auto root = juce::parseXML (text);
    if (root == nullptr || ! root->hasTagName ("Project"))
    {
        error = "This doesn't look like a DAWproject file (no project.xml inside).";
        return false;
    }
    Reader reader { out, zip, file, file.getSiblingFile (file.getFileNameWithoutExtension() + " Audio"), {}, {}, {} };
    if (! reader.run (*root))
    {
        error = "The DAWproject has no tracks.";
        return false;
    }
    if (! reader.extracted.empty())
        out.note ("Its audio was unpacked into \"" + reader.mediaFolder.getFileName() + "\" next to the file");
    return true;
}

} // namespace riff::interchange
