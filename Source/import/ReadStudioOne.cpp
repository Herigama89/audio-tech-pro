#include "ProjectImport.h"
#include "ImportXml.h"

/*
    Studio One songs (.song): a zip of XML.
      Song/song.xml        tracks (MediaTrack, mediaType Audio / Music) with their events
                           (AudioEvent: start, length, offset, clipID; MusicPartEvent with notes),
                           the tempo map, time signatures and markers
      Song/mediapool.xml   the audio clips (mediaID -> the file's url)
      Devices/audiomixer.xml  the mixer

    Studio One's own format isn't published; this reads the parts that are plain
    to see, in seconds. For everything, exactly: File > Export > DAWproject in
    Studio One 6.5 or newer, and open that.
*/
namespace riff::interchange
{
namespace
{
    using namespace xml;

    double attrNumber (const juce::XmlElement* e, const char* name, double fallback = 0.0)
    {
        return e != nullptr && e->hasAttribute (name) ? e->getStringAttribute (name).getDoubleValue() : fallback;
    }

    struct Reader
    {
        Project& out;
        juce::File folder;
        std::map<juce::String, juce::String> clipUrls;   // mediaID -> url / path
        double bpm = 120.0;

        void readMediaPool (const juce::XmlElement* pool)
        {
            if (pool == nullptr)
                return;
            juce::Array<const juce::XmlElement*> clips;
            findAll (pool, "AudioClip", clips);
            for (const auto* c : clips)
            {
                juce::String url;
                if (const auto* u = find (c, "Url"))
                    url = u->getStringAttribute ("url", u->getStringAttribute ("path"));
                if (url.isEmpty())
                    url = c->getStringAttribute ("url", c->getStringAttribute ("path", c->getStringAttribute ("name")));
                for (const auto* idName : { "mediaID", "clipID", "id", "x:id" })
                    if (c->hasAttribute (idName))
                        clipUrls[c->getStringAttribute (idName)] = url;
            }
        }

        void readTempo (const juce::XmlElement* song)
        {
            juce::Array<const juce::XmlElement*> segments;
            findAll (song, "TempoMapSegment", segments);
            bool first = true;
            for (const auto* s : segments)
            {
                // tempo: seconds a beat (or BPM if large)
                auto tempo = attrNumber (s, "tempo", 0.5);
                const auto bpmHere = tempo > 10.0 ? tempo : 60.0 / juce::jmax (0.05, tempo);
                const auto at = attrNumber (s, "start", attrNumber (s, "position"));
                if (first || at <= 1.0e-6)
                {
                    out.bpm = bpm = bpmHere;
                    first = false;
                }
                else
                    out.tempoChanges.push_back ({ at * bpm / 60.0, bpmHere });   // (approximate beat)
            }
            juce::Array<const juce::XmlElement*> signatures;
            findAll (song, "TimeSignatureMapSegment", signatures);
            for (const auto* s : signatures)
            {
                const auto num = s->getIntAttribute ("numerator", 4), den = s->getIntAttribute ("denominator", 4);
                const auto at = attrNumber (s, "start");
                if (at <= 1.0e-6) { out.numerator = num; out.denominator = den; }
                else out.signatureChanges.push_back ({ at * bpm / 60.0, num, den });
            }
            juce::Array<const juce::XmlElement*> markers;
            findAll (song, "MarkerEvent", markers);
            for (const auto* m : markers)
                out.markers.push_back ({ Time::inSeconds (attrNumber (m, "start")), m->getStringAttribute ("name") });
        }

        void readEvents (const juce::XmlElement* trackXml, Track& t)
        {
            juce::Array<const juce::XmlElement*> audio, parts;
            findAll (trackXml, "AudioEvent", audio);
            findAll (trackXml, "MusicPartEvent", parts);
            if (parts.isEmpty())
                findAll (trackXml, "MusicPart", parts);

            for (const auto* e : audio)
            {
                Region r;
                r.name = e->getStringAttribute ("name");
                r.start = Time::inSeconds (attrNumber (e, "start"));
                r.length = Time::inSeconds (attrNumber (e, "length"));
                r.offsetSeconds = attrNumber (e, "offset");
                r.gain = e->hasAttribute ("gain") ? attrNumber (e, "gain", 1.0) : 1.0;
                r.fadeIn = attrNumber (e, "fadeInLength");
                r.fadeOut = attrNumber (e, "fadeOutLength");
                r.muted = e->getBoolAttribute ("mute", false);
                juce::String url;
                for (const auto* idName : { "clipID", "mediaID" })
                    if (auto it = clipUrls.find (e->getStringAttribute (idName)); it != clipUrls.end())
                        url = it->second;
                r.missingFile = url.isNotEmpty() ? url.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false) : r.name;
                r.file = findMedia (url.isNotEmpty() ? url : r.name, folder, { "Media", "Media/Audio", "Media/Recordings" });
                t.regions.push_back (std::move (r));
            }

            for (const auto* e : parts)
            {
                Region r;
                r.midi = true;
                r.name = e->getStringAttribute ("name");
                const auto start = attrNumber (e, "start"), length = attrNumber (e, "length");
                r.start = Time::inSeconds (start);
                r.length = Time::inSeconds (length);
                juce::Array<const juce::XmlElement*> notes;
                findAll (e, "NoteEvent", notes);
                if (notes.isEmpty())
                    findAll (e, "MusicEvent", notes);
                for (const auto* n : notes)
                {
                    Note note;
                    note.pitch = juce::jlimit (0, 127, n->getIntAttribute ("pitch", n->getIntAttribute ("key", 60)));
                    note.start = attrNumber (n, "start") * bpm / 60.0;
                    note.length = juce::jmax (0.01, attrNumber (n, "length", 0.25) * bpm / 60.0);
                    const auto v = attrNumber (n, "velocity", 0.8);
                    note.velocity = juce::jlimit (1, 127, juce::roundToInt (v <= 1.0 ? v * 127.0 : v));
                    r.notes.push_back (note);
                }
                t.regions.push_back (std::move (r));
            }
        }

        bool run (const juce::XmlElement& song)
        {
            readTempo (&song);
            juce::Array<const juce::XmlElement*> tracks;
            findAll (&song, "MediaTrack", tracks);
            for (const auto* x : tracks)
            {
                Track t;
                t.key = x->getStringAttribute ("trackID", x->getStringAttribute ("name"));
                t.name = x->getStringAttribute ("name");
                const auto type = x->getStringAttribute ("mediaType");
                t.kind = type.equalsIgnoreCase ("Music") ? Track::Kind::instrument : Track::Kind::audio;
                if (x->hasAttribute ("color"))
                    t.colour = juce::Colour::fromString (x->getStringAttribute ("color").trimCharactersAtStart ("#").paddedLeft ('f', 8));
                t.mute = x->getBoolAttribute ("mute", false);
                t.solo = x->getBoolAttribute ("solo", false);
                readEvents (x, t);
                out.tracks.push_back (std::move (t));
            }
            return ! out.tracks.empty();
        }
    };
}

bool readStudioOne (const juce::File& file, Project& out, juce::String& error)
{
    juce::ZipFile zip (file);
    auto songText = xml::zipText (zip, "Song/song.xml");
    if (songText.isEmpty())
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (zip.getEntry (i)->filename.endsWithIgnoreCase ("song.xml"))
                songText = xml::zipText (zip, zip.getEntry (i)->filename);
    const auto song = juce::parseXML (songText);
    if (song == nullptr)
    {
        error = "This doesn't look like a Studio One song (no song.xml inside).";
        return false;
    }
    Reader reader { out, file.getParentDirectory(), {} };
    if (const auto pool = juce::parseXML (xml::zipText (zip, "Song/mediapool.xml")))
        reader.readMediaPool (pool.get());
    if (! reader.run (*song))
    {
        error = "No tracks were found in the song. In Studio One 6.5 or newer, File > Export > DAWproject and open that instead.";
        return false;
    }
    out.note ("Studio One: tracks, events, notes, tempo and markers came across; for plugins and the mix too, export a DAWproject from Studio One (File > Export) and open that");
    return true;
}

} // namespace riff::interchange
