#include "ProjectImport.h"
#include <ptformat/ptformat.h>

/*
    Pro Tools sessions (.ptx, and the older .ptf / .pts), read with libptformat
    (LGPL, from the Ardour project): its audio tracks with their regions as placed
    and trimmed, the audio files they use, and MIDI tracks with their notes.
    Plugins, sends, automation and the tempo map aren't in what it reads.
*/
namespace riff::interchange
{
namespace
{
    /** Pro Tools keeps a stereo track as two (left and right, each its own file): one stereo file of the two. */
    juce::File stereoFileOf (const juce::File& left, const juce::File& right, const juce::File& folder)
    {
        auto target = folder.getChildFile (left.getFileNameWithoutExtension().upToLastOccurrenceOf (".", false, false) + " (stereo).wav");
        if (target.existsAsFile())
            return target;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> l { formats.createReaderFor (left) }, r { formats.createReaderFor (right) };
        if (l == nullptr || r == nullptr || l->sampleRate != r->sampleRate)
            return {};
        folder.createDirectory();
        const auto length = juce::jmax (l->lengthInSamples, r->lengthInSamples);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
        if (stream == nullptr)
            return {};
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), l->sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return {};
        stream.release();
        juce::AudioBuffer<float> block (2, 65536), one (1, 65536);
        for (juce::int64 pos = 0; pos < length; pos += block.getNumSamples())
        {
            const auto n = (int) juce::jmin ((juce::int64) block.getNumSamples(), length - pos);
            block.clear();
            one.clear();
            l->read (&one, 0, n, pos, true, false);
            block.copyFrom (0, 0, one, 0, 0, n);
            one.clear();
            r->read (&one, 0, n, pos, true, false);
            block.copyFrom (1, 0, one, 0, 0, n);
            writer->writeFromAudioSampleBuffer (block, 0, n);
        }
        writer.reset();
        return target;
    }

    void joinStereoPairs (Project& out, const juce::File& session)
    {
        const auto isSide = [] (const Region& r, const char* side)
        {
            return r.name.endsWithIgnoreCase (side) || r.missingFile.upToLastOccurrenceOf (".aif", false, true).upToLastOccurrenceOf (".wav", false, true).endsWithIgnoreCase (side);
        };
        const auto folder = session.getSiblingFile (session.getFileNameWithoutExtension() + " Stereo Files");
        for (size_t i = 0; i + 1 < out.tracks.size(); ++i)
        {
            auto& a = out.tracks[i];
            auto& b = out.tracks[i + 1];
            if (a.kind != Track::Kind::audio || b.kind != Track::Kind::audio || a.name != b.name || a.regions.size() != b.regions.size() || a.regions.empty())
                continue;
            bool pair = true;
            for (size_t k = 0; k < a.regions.size() && pair; ++k)
                pair = isSide (a.regions[k], ".L") && isSide (b.regions[k], ".R")
                       && std::abs (a.regions[k].start.value - b.regions[k].start.value) < 1.0e-6;
            if (! pair)
                continue;

            bool joined = true;
            for (size_t k = 0; k < a.regions.size() && joined; ++k)
            {
                auto& left = a.regions[k];
                const auto& right = b.regions[k];
                if (! left.file.existsAsFile() || ! right.file.existsAsFile())
                {
                    joined = false;
                    break;
                }
                if (const auto stereo = stereoFileOf (left.file, right.file, folder); stereo.existsAsFile())
                {
                    left.file = stereo;
                    left.name = left.name.dropLastCharacters (2);
                }
                else
                    joined = false;
            }
            if (joined)
            {
                out.tracks.erase (out.tracks.begin() + (std::ptrdiff_t) i + 1);
                out.note ("Stereo tracks (two files in Pro Tools) were joined into stereo files in \"" + folder.getFileName() + "\"");
            }
            else
            {
                // The files aren't here (yet): the two sides as two tracks, panned left and right.
                a.name << " L";
                b.name << " R";
                a.pan = -1.0;
                b.pan = 1.0;
                a.mono = b.mono = true;
                ++i;
            }
        }
    }
}

bool readProTools (const juce::File& file, Project& out, juce::String& error)
{
    const auto path = file.getFullPathName().toStdString();

    // Read once to learn the session's rate, then at that rate (so positions are its own samples).
    auto session = std::make_unique<PTFFormat>();
    auto result = session->load (path, 48000);
    if (result == 0 && session->sessionrate() > 0 && session->sessionrate() != 48000)
    {
        const auto rate = session->sessionrate();
        session = std::make_unique<PTFFormat>();
        result = session->load (path, rate);
    }
    if (result != 0)
    {
        error = result == -3 ? "This Pro Tools version's sessions can't be read yet."
              : result == -1 ? "The Pro Tools session couldn't be decoded."
                             : "This doesn't look like a Pro Tools session.";
        return false;
    }

    const auto rate = (double) juce::jmax ((int64_t) 1, session->sessionrate());
    const auto folder = file.getParentDirectory();
    constexpr double ticksPerBeat = 960000.0;

    // Audio tracks: one entry per region on a track.
    std::map<int, size_t> byIndex;
    for (const auto& entry : session->tracks())
    {
        if (byIndex.count (entry.index) == 0)
        {
            Track t;
            t.key = "audio:" + juce::String (entry.index);
            t.name = juce::String::fromUTF8 (entry.name.c_str());
            byIndex[entry.index] = out.tracks.size();
            out.tracks.push_back (std::move (t));
        }
        const auto& reg = entry.reg;
        Region r;
        r.name = juce::String::fromUTF8 (reg.name.c_str());
        r.start = Time::inSeconds ((double) reg.startpos / rate);
        r.length = Time::inSeconds ((double) reg.length / rate);
        r.offsetSeconds = (double) reg.sampleoffset / rate;
        const auto wav = juce::String::fromUTF8 (reg.wave.filename.c_str());
        r.missingFile = wav;
        r.file = findMedia (wav, folder, { "Audio Files", "Fade Files" });
        // (Pro Tools names files with a .wav it may not have: try the common extensions)
        if (! r.file.existsAsFile())
            for (const auto* ext : { ".wav", ".aif", ".aiff" })
                if (const auto f = findMedia (wav.upToLastOccurrenceOf (".", false, false) + ext, folder, { "Audio Files" }); f.existsAsFile())
                {
                    r.file = f;
                    break;
                }
        out.tracks[byIndex[entry.index]].regions.push_back (std::move (r));
    }

    // MIDI tracks: positions in Pro Tools' ticks (960000 a beat).
    std::map<int, size_t> midiByIndex;
    for (const auto& entry : session->miditracks())
    {
        if (midiByIndex.count (entry.index) == 0)
        {
            Track t;
            t.key = "midi:" + juce::String (entry.index);
            t.name = juce::String::fromUTF8 (entry.name.c_str());
            t.kind = Track::Kind::instrument;
            midiByIndex[entry.index] = out.tracks.size();
            out.tracks.push_back (std::move (t));
        }
        const auto& reg = entry.reg;
        Region r;
        r.midi = true;
        r.name = juce::String::fromUTF8 (reg.name.c_str());
        r.start = Time::inBeats ((double) reg.startpos / ticksPerBeat);
        r.length = Time::inBeats (juce::jmax (0.25, (double) reg.length / ticksPerBeat));
        for (const auto& ev : reg.midi)
            r.notes.push_back ({ (int) ev.note, (double) ev.pos / ticksPerBeat, (double) ev.length / ticksPerBeat, juce::jlimit (1, 127, (int) ev.velocity) });
        out.tracks[midiByIndex[entry.index]].regions.push_back (std::move (r));
    }

    joinStereoPairs (out, file);

    if (out.tracks.empty())
    {
        error = "The session has no tracks with regions on them.";
        return false;
    }
    out.note ("Pro Tools: tracks and regions came across; plugins, sends, volume, automation and the tempo map aren't readable from the session (set the tempo to the session's)");
    if (! session->miditracks().empty())
        out.note ("MIDI was placed at 120 BPM, since the session's tempo isn't readable");
    return true;
}

} // namespace riff::interchange
