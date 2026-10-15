#include "ProjectImport.h"
#include "ImportXml.h"

/*
    Ableton Live sets (.als): gzipped XML.

    <Ableton><LiveSet>
      <Tracks> AudioTrack / MidiTrack / GroupTrack / ReturnTrack, each with
        <Name><EffectiveName Value/></Name> <Color Value/> <TrackGroupId Value/>
        <DeviceChain>
          <Mixer> Volume, Pan, Speaker (on), Sends/TrackSendHolder/Send, (master: Tempo, TimeSignature) </Mixer>
          <MainSequencer> arrangement clips under ArrangerAutomation/Events, session clips in ClipSlotList </MainSequencer>
          <DeviceChain><Devices> PluginDevice (Vst3PluginInfo / VstPluginInfo / AuPluginInfo), Live's own devices </Devices></DeviceChain>
        </DeviceChain>
      <MasterTrack> (Live 12: MainTrack) with the tempo and its automation
      <Locators><Locators><Locator> Time, Name
    Times are in beats.
*/
namespace riff::interchange
{
namespace
{
    using namespace xml;

    juce::Colour liveColour (int index)
    {
        if (index < 0)
            return juce::Colours::transparentBlack;
        // Live's 70-colour palette, approximately: 14 hues in 5 rows of shades.
        static const float sat[] { 0.62f, 0.45f, 0.28f, 0.55f, 0.72f };
        static const float bri[] { 1.0f, 1.0f, 0.95f, 0.72f, 0.6f };
        const auto row = juce::jlimit (0, 4, index / 14);
        return juce::Colour::fromHSV ((float) (index % 14) / 14.0f, sat[row], bri[row], 1.0f);
    }

    /** Live's time signature number: numerator - 1 + 99 * log2 (denominator). */
    std::pair<int, int> liveSignature (int value)
    {
        return { value % 99 + 1, 1 << juce::jlimit (0, 5, value / 99) };
    }

    struct WarpMap
    {
        std::vector<std::pair<double, double>> points;   // beat, seconds

        double secondsAt (double beat) const
        {
            if (points.empty())
                return 0.0;
            if (points.size() == 1)
                return points[0].second + (beat - points[0].first) * 0.5;
            size_t i = 1;
            while (i + 1 < points.size() && points[i].first < beat)
                ++i;
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const auto slope = (b.second - a.second) / juce::jmax (1.0e-9, b.first - a.first);
            return a.second + (beat - a.first) * slope;
        }

        double bpm() const
        {
            if (points.size() < 2)
                return 0.0;
            const auto beats = points.back().first - points.front().first;
            const auto seconds = points.back().second - points.front().second;
            return seconds > 1.0e-6 ? 60.0 * beats / seconds : 0.0;
        }
    };

    struct Reader
    {
        Project& out;
        juce::File folder;
        double bpm = 120.0;

        juce::String trackName (const juce::XmlElement* t)
        {
            auto name = value (t, "Name/UserName");
            if (name.isEmpty())
                name = value (t, "Name/EffectiveName");
            return name;
        }

        juce::File sampleFile (const juce::XmlElement* clip, juce::String& shownName)
        {
            const auto* ref = at (clip, "SampleRef/FileRef");
            if (ref == nullptr)
                return {};
            auto path = value (ref, "Path");
            auto relative = value (ref, "RelativePath");
            auto name = value (ref, "Name");
            if (relative.isEmpty())   // Live 9/10: a list of folders
            {
                juce::StringArray parts;
                if (const auto* list = ref->getChildByName ("RelativePath"))
                    for (auto* el : list->getChildWithTagNameIterator ("RelativePathElement"))
                        parts.add (el->getStringAttribute ("Dir"));
                if (! parts.isEmpty())
                    relative = parts.joinIntoString ("/") + "/" + name;
            }
            if (name.isEmpty())
                name = (path.isNotEmpty() ? path : relative).replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
            shownName = name;

            for (const auto& candidate : { path, relative, name })
                if (candidate.isNotEmpty())
                    if (const auto f = findMedia (candidate, folder); f.existsAsFile())
                        return f;
            return {};
        }

        bool readClip (const juce::XmlElement* clip, bool midi, Region& r, bool session)
        {
            const auto currentStart = number (clip, "CurrentStart", clip->getDoubleAttribute ("Time"));
            const auto currentEnd = number (clip, "CurrentEnd", currentStart);
            r.midi = midi;
            r.name = value (clip, "Name");
            r.muted = flag (clip, "Disabled");
            const auto loopStart = number (clip, "Loop/LoopStart");
            const auto loopEnd = number (clip, "Loop/LoopEnd", loopStart);
            const auto startRelative = number (clip, "Loop/StartRelative");
            const bool loopOn = flag (clip, "Loop/LoopOn");

            r.start = Time::inBeats (session ? 0.0 : currentStart);
            r.length = Time::inBeats (juce::jmax (0.0, session ? (loopOn ? loopEnd - loopStart : currentEnd - currentStart) : currentEnd - currentStart));
            if (r.length.value <= 1.0e-6)
                return false;

            if (midi)
            {
                r.offsetBeats = loopStart + startRelative;
                if (loopOn && ! session)
                    r.loopLength = loopEnd - loopStart;
                if (const auto* keyTracks = at (clip, "Notes/KeyTracks"))
                    for (auto* kt : keyTracks->getChildWithTagNameIterator ("KeyTrack"))
                    {
                        const auto key = (int) number (kt, "MidiKey", 60);
                        if (const auto* notes = kt->getChildByName ("Notes"))
                            for (auto* n : notes->getChildIterator())
                            {
                                if (! n->hasTagName ("MidiNoteEvent") || n->getStringAttribute ("IsEnabled", "true") == "false")
                                    continue;
                                Note note;
                                note.pitch = juce::jlimit (0, 127, key);
                                note.start = n->getDoubleAttribute ("Time");
                                note.length = n->getDoubleAttribute ("Duration", 0.25);
                                note.velocity = juce::jlimit (1, 127, juce::roundToInt (n->getDoubleAttribute ("Velocity", 100.0)));
                                r.notes.push_back (note);
                            }
                    }
                return true;
            }

            juce::String shown;
            r.file = sampleFile (clip, shown);
            r.missingFile = shown;
            if (r.name.isEmpty())
                r.name = shown.upToLastOccurrenceOf (".", false, false);
            r.gain = number (clip, "SampleVolume", 1.0);
            r.transpose = number (clip, "PitchCoarse") + number (clip, "PitchFine") / 100.0;

            if (flag (clip, "IsWarped", true))
            {
                WarpMap warp;
                if (const auto* markers = clip->getChildByName ("WarpMarkers"))
                    for (auto* m : markers->getChildWithTagNameIterator ("WarpMarker"))
                        warp.points.emplace_back (m->getDoubleAttribute ("BeatTime"), m->getDoubleAttribute ("SecTime"));
                std::sort (warp.points.begin(), warp.points.end());
                r.offsetSeconds = warp.secondsAt (loopStart + startRelative);
                r.sourceBpm = warp.bpm();
                if (loopOn && ! session)
                    r.loopLength = loopEnd - loopStart;
            }
            else
            {
                // Unwarped: its loop and start are in seconds of the file.
                r.offsetSeconds = loopStart + startRelative;
                if (loopOn && ! session)
                    r.loopLength = (loopEnd - loopStart) * bpm / 60.0;
            }
            const auto beatSeconds = 60.0 / bpm;
            r.fadeIn = number (clip, "Fades/FadeInLength") * beatSeconds;
            r.fadeOut = number (clip, "Fades/FadeOutLength") * beatSeconds;
            return true;
        }

        void readPlugin (const juce::XmlElement* device, std::vector<Plugin>& inserts, std::optional<Plugin>& instrument, bool midiTrack)
        {
            const auto* desc = at (device, "PluginDesc");
            if (desc == nullptr)
                return;
            Plugin p;
            p.bypassed = ! flag (device, "On/Manual", true);

            if (const auto* v3 = desc->getChildByName ("Vst3PluginInfo"))
            {
                p.format = "VST3";
                p.name = value (v3, "Name");
                juce::String uid;
                for (int i = 0; i < 4; ++i)
                    uid << juce::String::toHexString ((juce::uint32) (juce::int64) number (v3, "Uid/Fields." + juce::String (i))).paddedLeft ('0', 8);
                p.uid = uid.toUpperCase();
                p.instrument = (int) number (v3, "DeviceType", 2) == 1;
                if (const auto* preset = find (v3, "Vst3Preset"))
                {
                    // (hex, as the element's text)
                    const auto hexOf = [preset] (const char* tag)
                    {
                        const auto* e = preset->getChildByName (tag);
                        return e == nullptr ? juce::MemoryBlock() : fromHex (e->getAllSubText().isNotEmpty() ? e->getAllSubText() : e->getStringAttribute ("Value"));
                    };
                    p.vst3Component = hexOf ("ProcessorState");
                    p.vst3Controller = hexOf ("ControllerState");
                }
            }
            else if (const auto* au = desc->getChildByName ("AuPluginInfo"))
            {
                p.format = "AudioUnit";
                p.name = value (au, "Name");
                p.vendor = value (au, "Manufacturer");
                p.instrument = (int) number (au, "ComponentType") == 0x61756d75;   // 'aumu'
                if (const auto* preset = find (au, "AuPreset"))
                    if (const auto* buffer = preset->getChildByName ("Buffer"))
                        p.juceState = fromHex (buffer->getAllSubText());
            }
            else if (const auto* v2 = desc->getChildByName ("VstPluginInfo"))
            {
                p.format = "VST";
                p.name = value (v2, "PlugName");
                p.instrument = (int) number (v2, "Category") == 2;
            }
            if (p.name.isEmpty())
                return;
            if (p.format == "VST")
                out.note (p.name + " was a VST2 plugin in Live: its VST3 or AU version is used, with its default settings");

            // On a MIDI track the first instrument plugin plays the notes; the rest are effects.
            if (midiTrack && ! instrument.has_value() && (p.instrument || inserts.empty()))
            {
                p.instrument = true;
                instrument = p;
            }
            else
                inserts.push_back (p);
        }

        void readDevices (const juce::XmlElement* devices, Track& t, bool midiTrack)
        {
            if (devices == nullptr)
                return;
            for (auto* d : devices->getChildIterator())
            {
                const auto tag = d->getTagName();
                if (tag == "PluginDevice" || tag == "AuPluginDevice")
                    readPlugin (d, t.inserts, t.instrumentPlugin, midiTrack);
                else if (midiTrack && (tag == "OriginalSimpler" || tag == "MultiSampler") && ! t.instrumentPlugin.has_value()
                         && ! t.samplerFile.existsAsFile())
                {
                    // Simpler / Sampler: its (first) sample in our sampler.
                    juce::String shown;
                    if (const auto* part = find (d, "MultiSamplePart"))
                    {
                        t.samplerFile = sampleFile (part, shown);
                        t.samplerRoot = (int) number (part, "RootKey", 60);
                    }
                    out.note ("Simpler / Sampler on \"" + t.name + "\" plays in our sampler (its sample, without Live's settings)");
                }
                else if (midiTrack && tag == "DrumGroupDevice" && ! t.instrumentPlugin.has_value())
                {
                    t.drums = true;
                    out.note ("The Drum Rack on \"" + t.name + "\" plays our drum kit (its samples can't come across)");
                }
                else if (tag.endsWith ("GroupDevice"))
                {
                    // A rack: the plugins in its first chain come across.
                    if (const auto* chain = find (d, "Devices"))
                        readDevices (chain, t, midiTrack);
                    out.note ("Live racks were opened up: only the plugins inside the first chain of each came across");
                }
                else
                {
                    auto shown = value (d, "UserName");
                    if (shown.isEmpty())
                        shown = tag;
                    out.note ("Live's own devices can't come across (\"" + shown + "\" on \"" + t.name + "\")");
                }
            }
        }

        void readMixer (const juce::XmlElement* trackXml, Track& t, int numReturns)
        {
            const auto* mixer = at (trackXml, "DeviceChain/Mixer");
            if (mixer == nullptr)
                return;
            t.volume = number (mixer, "Volume/Manual", 1.0);
            t.pan = number (mixer, "Pan/Manual", 0.0);
            t.mute = ! flag (mixer, "Speaker/Manual", true);
            t.solo = flag (mixer, "SoloSink");
            if (const auto* sends = mixer->getChildByName ("Sends"))
            {
                int i = 0;
                for (auto* holder : sends->getChildWithTagNameIterator ("TrackSendHolder"))
                {
                    if (i < numReturns)
                    {
                        const auto level = number (holder, "Send/Manual", 0.0);
                        if (level > 0.0004)   // (Live's off is -70 dB)
                            t.sends.push_back ({ "return:" + juce::String (i), level, false });
                    }
                    ++i;
                }
            }
        }

        void readClips (const juce::XmlElement* trackXml, Track& t, bool midi)
        {
            const auto* sequencer = at (trackXml, "DeviceChain/MainSequencer");
            if (sequencer == nullptr)
                return;
            const auto clipTag = midi ? "MidiClip" : "AudioClip";

            // Arrangement
            juce::Array<const juce::XmlElement*> arrangers;
            findAll (sequencer, "ArrangerAutomation", arrangers);
            for (const auto* arranger : arrangers)
                if (const auto* events = arranger->getChildByName ("Events"))
                    for (auto* clip : events->getChildWithTagNameIterator (clipTag))
                    {
                        Region r;
                        if (readClip (clip, midi, r, false))
                            t.regions.push_back (std::move (r));
                    }

            // Session view: into Live Loops
            if (const auto* slots = at (sequencer, "ClipSlotList"))
            {
                int scene = 0;
                for (auto* slot : slots->getChildWithTagNameIterator ("ClipSlot"))
                {
                    if (const auto* clip = find (slot, clipTag))
                    {
                        Region r;
                        if (readClip (clip, midi, r, true))
                            t.cells.emplace_back (scene, std::move (r));
                    }
                    ++scene;
                }
            }
        }

        /** The master's tempo (and its automation, as steps), its signature and its plugins. */
        void readMaster (const juce::XmlElement* master)
        {
            if (master == nullptr)
                return;
            const auto* mixer = at (master, "DeviceChain/Mixer");
            bpm = number (mixer, "Tempo/Manual", 120.0);
            out.bpm = bpm;
            const auto [num, den] = liveSignature ((int) number (mixer, "TimeSignature/Manual", 201));
            out.numerator = num;
            out.denominator = den;
            out.masterVolume = number (mixer, "Volume/Manual", -1.0);

            const auto tempoTarget = at (mixer, "Tempo/AutomationTarget") != nullptr ? at (mixer, "Tempo/AutomationTarget")->getStringAttribute ("Id") : juce::String();
            const auto sigTarget = at (mixer, "TimeSignature/AutomationTarget") != nullptr ? at (mixer, "TimeSignature/AutomationTarget")->getStringAttribute ("Id") : juce::String();
            if (const auto* envelopes = at (master, "AutomationEnvelopes/Envelopes"))
                for (auto* env : envelopes->getChildWithTagNameIterator ("AutomationEnvelope"))
                {
                    const auto pointee = value (env, "EnvelopeTarget/PointeeId");
                    const auto* events = at (env, "Automation/Events");
                    if (events == nullptr)
                        continue;
                    if (pointee == tempoTarget && tempoTarget.isNotEmpty())
                    {
                        // Live ramps between points; here, steps every beat along each ramp.
                        double lastBeat = -1.0, lastBpm = bpm;
                        bool lastReal = false;   // (the first event, far before the song, holds its value until the next)
                        for (auto* e : events->getChildIterator())
                        {
                            const auto time = e->getDoubleAttribute ("Time");
                            const auto v = e->getDoubleAttribute ("Value", bpm);
                            if (time < 0.0)
                            {
                                out.bpm = bpm = v;
                                lastBeat = 0.0;
                                lastBpm = v;
                                continue;
                            }
                            if (lastReal && std::abs (v - lastBpm) > 0.01 && time - lastBeat > 1.0)
                                for (double b = std::ceil (lastBeat + 1.0e-6); b < time; b += 1.0)
                                    out.tempoChanges.push_back ({ b, lastBpm + (v - lastBpm) * (b - lastBeat) / (time - lastBeat) });
                            if (std::abs (v - lastBpm) > 1.0e-6 || ! lastReal)
                                out.tempoChanges.push_back ({ time, v });
                            lastBeat = time;
                            lastBpm = v;
                            lastReal = true;
                        }
                    }
                    else if (pointee == sigTarget && sigTarget.isNotEmpty())
                    {
                        for (auto* e : events->getChildIterator())
                        {
                            const auto [n, d] = liveSignature (e->getIntAttribute ("Value", 201));
                            const auto time = e->getDoubleAttribute ("Time");
                            if (time <= 0.0)
                            {
                                out.numerator = n;
                                out.denominator = d;
                            }
                            else
                                out.signatureChanges.push_back ({ time, n, d });
                        }
                    }
                }

            Track dummy;
            dummy.name = "Main";
            readDevices (at (master, "DeviceChain/DeviceChain/Devices"), dummy, false);
            out.masterInserts = dummy.inserts;
        }

        bool run (const juce::XmlElement& root)
        {
            const auto* set = root.getChildByName ("LiveSet");
            if (set == nullptr)
                return false;

            const auto* master = set->getChildByName ("MasterTrack");
            if (master == nullptr)
                master = set->getChildByName ("MainTrack");
            readMaster (master);

            const auto* tracks = set->getChildByName ("Tracks");
            if (tracks == nullptr)
                return false;

            int numReturns = 0;
            for (auto* t : tracks->getChildIterator())
                if (t->hasTagName ("ReturnTrack"))
                    ++numReturns;

            std::vector<Track> returns;
            for (auto* x : tracks->getChildIterator())
            {
                const auto tag = x->getTagName();
                Track t;
                t.key = "id:" + x->getStringAttribute ("Id");
                t.name = trackName (x);
                t.colour = liveColour ((int) number (x, "Color", number (x, "ColorIndex", -1)));
                const auto group = (int) number (x, "TrackGroupId", -1);
                if (group >= 0)
                    t.groupKey = "id:" + juce::String (group);

                const bool midi = tag == "MidiTrack";
                if (tag == "AudioTrack" || midi)
                {
                    t.kind = midi ? Track::Kind::instrument : Track::Kind::audio;
                    readMixer (x, t, numReturns);
                    readClips (x, t, midi);
                    readDevices (at (x, "DeviceChain/DeviceChain/Devices"), t, midi);
                    out.tracks.push_back (std::move (t));
                }
                else if (tag == "GroupTrack")
                {
                    t.kind = Track::Kind::bus;
                    t.isGroup = true;
                    readMixer (x, t, numReturns);
                    readDevices (at (x, "DeviceChain/DeviceChain/Devices"), t, false);
                    out.tracks.push_back (std::move (t));
                }
                else if (tag == "ReturnTrack")
                {
                    t.kind = Track::Kind::bus;
                    t.key = "return:" + juce::String ((int) returns.size());
                    readMixer (x, t, 0);
                    readDevices (at (x, "DeviceChain/DeviceChain/Devices"), t, false);
                    returns.push_back (std::move (t));
                }
            }
            for (auto& r : returns)
                out.tracks.push_back (std::move (r));

            if (const auto* locators = at (set, "Locators/Locators"))
                for (auto* l : locators->getChildWithTagNameIterator ("Locator"))
                    out.markers.push_back ({ Time::inBeats (number (l, "Time")), value (l, "Name") });

            for (const auto& t : out.tracks)
                if (! t.cells.empty())
                {
                    out.note ("Session View clips are in the Live Loops grid");
                    break;
                }
            return true;
        }
    };
}

bool readAbleton (const juce::File& file, Project& out, juce::String& error)
{
    juce::FileInputStream raw (file);
    if (! raw.openedOk())
    {
        error = "The file couldn't be opened.";
        return false;
    }
    juce::MemoryBlock data;
    {
        juce::GZIPDecompressorInputStream unzipped (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
        unzipped.readIntoMemoryBlock (data);
    }
    if (data.getSize() == 0)   // (some sets are saved uncompressed)
        file.loadFileAsData (data);

    const auto root = juce::parseXML (data.toString());
    if (root == nullptr || ! root->hasTagName ("Ableton"))
    {
        error = "This doesn't look like an Ableton Live set.";
        return false;
    }
    Reader reader { out, file.getParentDirectory() };
    if (! reader.run (*root))
    {
        error = "The Live set has no tracks this version can read.";
        return false;
    }
    return true;
}

} // namespace riff::interchange
