#include "ProjectImport.h"

/* Standard MIDI files: a track per MIDI track (and per channel, for type 0 files),
   with the tempo map, time signatures and markers. Logic, Cubase, Pro Tools and the
   rest all export these. */
namespace riff::interchange
{
bool readMidiFile (const juce::File& file, Project& out, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in, true))
    {
        error = "This doesn't look like a MIDI file.";
        return false;
    }
    const auto ppq = (double) midi.getTimeFormat();
    if (ppq <= 0.0)
    {
        error = "MIDI files timed in SMPTE frames aren't supported.";
        return false;
    }

    // Tempo, signatures and markers (from any track)
    bool tempoSet = false, sigSet = false;
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* holder : *midi.getTrack (t))
        {
            const auto& m = holder->message;
            const auto beat = m.getTimeStamp() / ppq;
            if (m.isTempoMetaEvent())
            {
                const auto bpm = 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote());
                if (beat <= 1.0e-6 && ! tempoSet) { out.bpm = bpm; tempoSet = true; }
                else if (beat > 1.0e-6) out.tempoChanges.push_back ({ beat, bpm });
            }
            else if (m.isTimeSignatureMetaEvent())
            {
                int num = 4, den = 4;
                m.getTimeSignatureInfo (num, den);
                if (beat <= 1.0e-6 && ! sigSet) { out.numerator = num; out.denominator = den; sigSet = true; }
                else if (beat > 1.0e-6) out.signatureChanges.push_back ({ beat, num, den });
            }
            else if (m.isTextMetaEvent() && (m.getMetaEventType() == 6 || m.getMetaEventType() == 7))
                out.markers.push_back ({ Time::inBeats (beat), m.getTextFromTextMetaEvent() });
        }

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto* seq = midi.getTrack (t);
        juce::String name;
        for (const auto* holder : *seq)
            if (holder->message.isTrackNameEvent())
            {
                name = holder->message.getTextFromTextMetaEvent();
                break;
            }

        // One track per channel used on it
        std::map<int, Region> byChannel;
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto* holder = seq->getEventPointer (i);
            const auto& m = holder->message;
            if (! m.isNoteOn())
                continue;
            const auto start = m.getTimeStamp() / ppq;
            const auto end = holder->noteOffObject != nullptr ? holder->noteOffObject->message.getTimeStamp() / ppq : start + 0.25;
            auto& r = byChannel[m.getChannel()];
            r.midi = true;
            r.notes.push_back ({ m.getNoteNumber(), start, juce::jmax (1.0 / 64.0, end - start), juce::jlimit (1, 127, (int) m.getVelocity()) });
        }

        for (auto& [channel, region] : byChannel)
        {
            double last = 0.0;
            for (const auto& n : region.notes)
                last = juce::jmax (last, n.start + n.length);
            region.start = Time::inBeats (0.0);
            region.length = Time::inBeats (std::ceil (last / 4.0 + 1.0e-9) * 4.0);
            Track track;
            track.key = juce::String (t) + ":" + juce::String (channel);
            track.name = name.isNotEmpty() ? name : "MIDI " + juce::String (t + 1);
            if (byChannel.size() > 1)
                track.name << " (ch " << channel << ")";
            track.kind = Track::Kind::instrument;
            track.drums = channel == 10;
            region.name = track.name;
            track.regions.push_back (std::move (region));
            out.tracks.push_back (std::move (track));
        }
    }

    if (out.tracks.empty())
    {
        error = "The MIDI file has no notes.";
        return false;
    }
    return true;
}

} // namespace riff::interchange
