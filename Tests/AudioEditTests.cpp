#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/engine/AudioEdit.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::AudioBuffer<float> sine (double freq, double seconds, double rate = 48000.0)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / rate));
        return b;
    }

    /** Frequency from upward zero crossings in the middle of the buffer. */
    double frequencyOf (const juce::AudioBuffer<float>& b, double rate = 48000.0)
    {
        const auto* d = b.getReadPointer (0);
        const int from = b.getNumSamples() / 4, to = b.getNumSamples() * 3 / 4;
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            crossings += (d[i - 1] < 0.0f && d[i] >= 0.0f) ? 1 : 0;
        return crossings / ((to - from) / rate);
    }
}

TEST_CASE ("Time stretch keeps the pitch; pitch shift keeps the length", "[audioedit]")
{
    const auto a = sine (440.0, 1.0);

    const auto longer = audioedit::timeStretch (a, 1.5, 48000.0);
    CHECK (longer.getNumSamples() == 72000);
    CHECK_THAT (frequencyOf (longer), WithinAbs (440.0, 8.0));

    const auto higher = audioedit::pitchShift (a, 12.0, 48000.0);
    CHECK (higher.getNumSamples() == 48000);
    CHECK_THAT (frequencyOf (higher), WithinAbs (880.0, 16.0));
}

TEST_CASE ("Audio File Editor edits", "[audioedit]")
{
    auto a = sine (100.0, 1.0);
    audioedit::apply (a, { audioedit::Operation::normalize }, 48000.0);
    CHECK_THAT (a.getMagnitude (0, 0, a.getNumSamples()), WithinAbs (juce::Decibels::decibelsToGain (-0.1f), 1e-3));

    audioedit::apply (a, { audioedit::Operation::silence, 0.0, 0.25, 0.5 }, 48000.0);
    CHECK (a.getMagnitude (0, 12000, 12000) == 0.0f);
    CHECK (a.getMagnitude (0, 30000, 1000) > 0.5f);

    const auto before = a.getSample (0, 40000);
    audioedit::apply (a, { audioedit::Operation::invert }, 48000.0);
    CHECK (a.getSample (0, 40000) == -before);

    audioedit::apply (a, { audioedit::Operation::fadeIn, 0.0, 0.5, 1.0 }, 48000.0);
    CHECK (std::abs (a.getSample (0, 24000)) < 0.05f);
}

namespace
{
    /** Clicks at the given times (seconds), in a second or so of silence. */
    juce::AudioBuffer<float> clicks (const std::vector<double>& times, double seconds, double rate = 48000.0)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        b.clear();
        for (auto t : times)
            for (int i = 0; i < 2000; ++i)
                if (const auto at = (int) (t * rate) + i; at < b.getNumSamples())
                    b.setSample (0, at, 0.8f * std::exp (-i / 300.0f) * (float) std::sin (i * 0.3));
        return b;
    }
}

TEST_CASE ("Smart Tempo and Flex Time", "[audioedit]")
{
    // 120 BPM: a hit every half second.
    std::vector<double> beats;
    for (int i = 0; i < 16; ++i)
        beats.push_back (i * 0.5);
    const auto steady = clicks (beats, 8.2);
    const auto onsets = audioedit::detectTransients (steady, 48000.0);
    CHECK (onsets.size() == 16);
    CHECK_THAT (audioedit::detectTempo (steady, 48000.0), WithinAbs (120.0, 1.0));

    // Hits a little late: quantized onto the half-second grid.
    const auto sloppy = clicks ({ 0.0, 0.53, 1.04, 1.49 }, 2.2);
    const auto fixed = audioedit::flexQuantize (sloppy, 48000.0, [] (double t) { return std::round (t / 0.5) * 0.5; });
    const auto after = audioedit::detectTransients (fixed, 48000.0);
    REQUIRE (after.size() == 4);
    CHECK (std::abs (after[1] - 24000) < 600);
    CHECK (std::abs (after[2] - 48000) < 600);
}

TEST_CASE ("Flex Pitch: notes and their pitch", "[audioedit]")
{
    CHECK_THAT (audioedit::detectPitch (sine (220.0, 0.5), 48000.0, 12000), WithinAbs (220.0, 2.0));

    // A, then E above it.
    auto a = sine (220.0, 0.5);
    auto e = sine (329.63, 0.5);
    juce::AudioBuffer<float> both (1, 48000);
    both.clear();
    both.copyFrom (0, 0, a, 0, 0, 24000);
    both.copyFrom (0, 24000, e, 0, 0, 24000);
    both.applyGainRamp (0, 0, 200, 0.0f, 1.0f);
    // A gap so the second note has a hit.
    both.clear (0, 23000, 1000);
    const auto notes = audioedit::detectNotes (both, 48000.0);
    REQUIRE (notes.size() >= 2);
    CHECK_THAT (notes.front().midi, WithinAbs (57.0, 0.3));
    CHECK_THAT (notes.back().midi, WithinAbs (64.0, 0.3));

    // Shifting just the second note up two semitones.
    audioedit::apply (both, { audioedit::Operation::pitchShift, 2.0, 0.5, 1.0 }, 48000.0);
    CHECK_THAT (audioedit::detectPitch (both, 48000.0, 36000), WithinAbs (369.99, 6.0));
    CHECK_THAT (audioedit::detectPitch (both, 48000.0, 12000), WithinAbs (220.0, 2.0));
}

TEST_CASE ("Flex markers moved by hand stretch only the audio either side", "[audioedit][flex]")
{
    // Clicks at 0.25, 0.5 and 0.75 s; move the middle one to 0.6 s.
    const double rate = 48000.0;
    juce::AudioBuffer<float> audio (1, 48000);
    audio.clear();
    for (auto t : { 0.25, 0.5, 0.75 })
        for (int i = 0; i < 200; ++i)
            audio.setSample (0, (int) (t * rate) + i, 0.9f * (1.0f - i / 200.0f));

    const auto out = audioedit::flexWarp (audio, rate, { { 0.25, 0.25 }, { 0.5, 0.6 }, { 0.75, 0.75 } });
    REQUIRE (std::abs (out.getNumSamples() - 48000) < 64);   // same length overall

    const auto hits = audioedit::detectTransients (out, rate);
    REQUIRE (hits.size() == 3);
    CHECK (std::abs (hits[0] / rate - 0.25) < 0.01);
    CHECK (std::abs (hits[1] / rate - 0.6) < 0.015);
    CHECK (std::abs (hits[2] / rate - 0.75) < 0.01);
}

#include "../Source/ui/SpectrumAnalyzer.h"

TEST_CASE ("The analyzer puts a tone in its third-octave band", "[analyzer]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    riff::SpectrumAnalyzer analyzer ({}, {});
    std::vector<float> tone (8192);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / 48000.0);
    const auto bands = analyzer.analyse (tone.data(), (int) tone.size(), 48000.0);
    const auto& centres = riff::SpectrumAnalyzer::thirdOctaveCentres();
    const auto loudest = (size_t) std::distance (bands.begin(), std::max_element (bands.begin(), bands.end()));
    CHECK (centres[loudest] == 1000.0);
    CHECK (bands[loudest] > -10.0f);       // about -6 dB for a half-scale sine
    CHECK (bands[3] < bands[loudest] - 40.0f);   // 40 Hz: far below
}

TEST_CASE ("Audio alignment lines a dub's timing up with the guide", "[audioedit]")
{
    // A "performance": notes of different pitches with gaps; the dub sings them late by varying amounts
    constexpr double rate = 48000.0;
    const double starts[] { 0.30, 0.80, 1.25, 1.90, 2.40, 3.00, 3.55 };
    const double pitches[] { 220.0, 330.0, 262.0, 392.0, 294.0, 440.0, 247.0 };
    const double lates[] { 0.03, 0.08, 0.05, 0.12, 0.02, 0.09, 0.06 };
    const auto render = [&] (bool dub)
    {
        juce::AudioBuffer<float> b (1, (int) (4.2 * rate));
        b.clear();
        for (int k = 0; k < 7; ++k)
        {
            const auto s = (int) ((starts[k] + (dub ? lates[k] : 0.0)) * rate);
            for (int i = 0; i < (int) (0.35 * rate) && s + i < b.getNumSamples(); ++i)
            {
                const auto env = std::min (1.0, i / 400.0) * std::min (1.0, (0.35 * rate - i) / 2000.0);
                const auto t = i / rate;
                b.setSample (0, s + i, (float) (0.4 * env * (std::sin (juce::MathConstants<double>::twoPi * pitches[k] * t)
                                                            + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * pitches[k] * t))));
            }
        }
        return b;
    };
    const auto guide = render (false), dub = render (true);
    const auto markers = audioedit::alignToGuide (guide, rate, dub, rate, 0.0, 1.0, 0.4);
    REQUIRE (markers.size() > 20);

    // Where each dub note's middle goes: near the guide note's middle
    const auto mapped = [&] (double t)
    {
        std::vector<std::pair<double, double>> pts { { 0.0, 0.0 } };
        pts.insert (pts.end(), markers.begin(), markers.end());
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            if (t >= pts[i].first && t <= pts[i + 1].first)
                return pts[i].second + (t - pts[i].first) / (pts[i + 1].first - pts[i].first) * (pts[i + 1].second - pts[i].second);
        return t;
    };
    for (int k = 0; k < 7; ++k)
    {
        INFO ("note " << k);
        CHECK_THAT (mapped (starts[k] + lates[k] + 0.15), WithinAbs (starts[k] + 0.15, 0.025));
    }

    // At half strength: halfway
    const auto half = audioedit::alignToGuide (guide, rate, dub, rate, 0.0, 0.5, 0.4);
    REQUIRE (! half.empty());
    for (const auto& [from, to] : half)
        CHECK (to <= from + 0.001);   // (the dub is late everywhere: it only moves earlier)
}

TEST_CASE ("Consolidate mixes regions into one file, gaps silent, gain and fades in it", "[audioedit]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("riff-consolidate-" + juce::Uuid().toString());
    dir.createDirectory();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto write = [&] (const juce::String& name, float value)
    {
        juce::AudioBuffer<float> b (1, 48000);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, value);
        auto file = dir.getChildFile (name);
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    };
    const auto a = write ("a.wav", 0.5f), b = write ("b.wav", 0.25f);

    std::vector<audioedit::ConsolidatePart> parts (2);
    parts[0].file = a; parts[0].start = 2.0; parts[0].length = 0.5; parts[0].gain = 0.5f;
    parts[1].file = b; parts[1].start = 3.0; parts[1].length = 0.5; parts[1].offset = 0.2; parts[1].fadeIn = 0.1;
    double start = 0.0, length = 0.0;
    const auto dest = dir.getChildFile ("out.wav");
    REQUIRE (audioedit::consolidate (formats, parts, dest, start, length).isEmpty());
    CHECK_THAT (start, WithinAbs (2.0, 1e-9));
    CHECK_THAT (length, WithinAbs (1.5, 1e-9));

    double rate = 0.0;
    const auto out = audioedit::readRegion (formats, dest, 0.0, 1.5, rate);
    CHECK (out.getNumChannels() == 2);
    CHECK_THAT (out.getSample (0, 12000), WithinAbs (0.25, 1e-3));   // the first, at half gain
    CHECK_THAT (out.getSample (1, 36000), WithinAbs (0.0, 1e-6));    // the gap
    CHECK (out.getSample (0, 48000 + 100) < 0.05f);                   // the fade in starting
    CHECK_THAT (out.getSample (0, 48000 + 12000), WithinAbs (0.25, 1e-3));
    dir.deleteRecursively();
}

#include "../Source/engine/PitchCorrect.h"

TEST_CASE ("Pitch editor: notes found, moved and straightened, length kept", "[audioedit][pitch]")
{
    // A sung-like line: A3 (with vibrato), then C4, with a breath between; a few harmonics
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> voice (1, (int) (1.6 * rate));
    voice.clear();
    double phase = 0.0;
    for (int i = 0; i < voice.getNumSamples(); ++i)
    {
        const auto t = i / rate;
        double f = 0.0;
        if (t > 0.05 && t < 0.7)       f = 220.0 * std::pow (2.0, 0.4 / 12.0 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        else if (t > 0.85 && t < 1.5)  f = 261.63;
        if (f <= 0.0)
            continue;
        phase += juce::MathConstants<double>::twoPi * f / rate;
        voice.setSample (0, i, (float) (0.3 * std::sin (phase) + 0.15 * std::sin (2.0 * phase) + 0.08 * std::sin (3.0 * phase)));
    }

    const auto analysis = pitchcorrect::analyse (voice, rate);
    REQUIRE (analysis.notes.size() == 2);
    CHECK_THAT (analysis.notes[0].centre, WithinAbs (57.0, 0.25));   // A3
    CHECK_THAT (analysis.notes[1].centre, WithinAbs (60.0, 0.25));   // C4

    // Moved up two semitones: the first note now B3, the second untouched
    std::vector<pitchcorrect::NoteEdit> edits (2);
    edits[0].shift = 2.0;
    const auto moved = pitchcorrect::render (voice, analysis, edits);
    CHECK (moved.getNumSamples() == voice.getNumSamples());
    // As loud as before (no holes or doubled grains), and the untouched note the same sound
    const auto rmsOf = [] (const juce::AudioBuffer<float>& b, double from, double to)
    {
        return b.getRMSLevel (0, (int) (from * rate), (int) ((to - from) * rate));
    };
    CHECK_THAT (juce::Decibels::gainToDecibels (rmsOf (moved, 0.2, 0.6) / rmsOf (voice, 0.2, 0.6)), WithinAbs (0.0, 1.0));
    CHECK_THAT (juce::Decibels::gainToDecibels (rmsOf (moved, 1.0, 1.4) / rmsOf (voice, 1.0, 1.4)), WithinAbs (0.0, 0.5));
    const auto again = pitchcorrect::analyse (moved, rate);
    REQUIRE (again.notes.size() == 2);
    CHECK_THAT (again.notes[0].centre, WithinAbs (59.0, 0.3));
    CHECK_THAT (again.notes[1].centre, WithinAbs (60.0, 0.25));

    // Live: only the moved note rendered into the playing audio gives the same result as rendering it all
    {
        juce::AudioBuffer<float> live (voice);
        const auto [from, to] = pitchcorrect::noteSpan (analysis, 0);
        pitchcorrect::renderInto (live, voice, analysis, edits, from, to);
        double diff = 0.0, energy = 0.0;
        for (int i = from + 2000; i < to - 2000; ++i)
        {
            diff += std::pow (live.getSample (0, i) - moved.getSample (0, i), 2.0);
            energy += std::pow (moved.getSample (0, i), 2.0);
        }
        CHECK (diff < energy * 0.02);
        // the other note untouched (the recording as it was)
        CHECK (live.getSample (0, (int) (1.2 * rate)) == voice.getSample (0, (int) (1.2 * rate)));
    }

    // Straightened: the vibrato's swing much smaller
    const auto swing = [] (const pitchcorrect::Analysis& a, const pitchcorrect::Note& n)
    {
        float lo = 200.0f, hi = 0.0f;
        for (int f = n.startFrame + 10; f < n.endFrame - 10; ++f)
            if (a.midi[(size_t) f] > 0.0f)
            {
                lo = juce::jmin (lo, a.midi[(size_t) f]);
                hi = juce::jmax (hi, a.midi[(size_t) f]);
            }
        return hi - lo;
    };
    edits[0] = {};
    edits[0].drift = 1.0;
    edits[0].vibrato = 0.0;
    const auto straight = pitchcorrect::analyse (pitchcorrect::render (voice, analysis, edits), rate);
    REQUIRE (! straight.notes.empty());
    CHECK (swing (analysis, analysis.notes[0]) > 0.6f);
    CHECK (swing (straight, straight.notes[0]) < 0.35f);

    // Pitch correction to the nearest semitone, and the edits' text
    std::vector<pitchcorrect::NoteEdit> fix (2);
    fix[1].shift = 0.4;   // sharp
    pitchcorrect::correct (fix, analysis, { 0, 1 }, 1.0);
    CHECK_THAT (analysis.notes[1].centre + fix[1].shift, WithinAbs (std::round (analysis.notes[1].centre + 0.4), 1e-9));
    const auto text = pitchcorrect::toString (fix);
    CHECK (pitchcorrect::fromString (text, 2)[1].shift == juce::String (fix[1].shift, 4).getDoubleValue());

    // Pitch Correction as an amount kept on the note; formant and vibrato and time round-trip as text
    std::vector<pitchcorrect::NoteEdit> more (2);
    more[1].shift = 0.4;
    more[1].snap = 1.0;
    more[0].formant = 3.0;
    more[0].vibrato = 1.5;
    more[0].time = 0.05;
    CHECK_THAT (pitchcorrect::centreAfter (analysis.notes[1], more[1]), WithinAbs (std::round (analysis.notes[1].centre + 0.4), 1e-9));
    const auto back = pitchcorrect::fromString (pitchcorrect::toString (more), 2);
    CHECK_THAT (back[0].formant, WithinAbs (3.0, 1e-6));
    CHECK_THAT (back[0].vibrato, WithinAbs (1.5, 1e-6));
    CHECK_THAT (back[0].time, WithinAbs (0.05, 1e-6));

    // A formant shift keeps the pitch
    std::vector<pitchcorrect::NoteEdit> bright (2);
    bright[1].formant = 4.0;
    const auto brighter = pitchcorrect::analyse (pitchcorrect::render (voice, analysis, bright), rate);
    REQUIRE (brighter.notes.size() == 2);
    CHECK_THAT (brighter.notes[1].centre, WithinAbs (60.0, 0.35));

    // Moved later in time: the note starts later, the length stays
    std::vector<pitchcorrect::NoteEdit> later (2);
    later[1].time = 0.06;
    const auto shifted = pitchcorrect::render (voice, analysis, later);
    CHECK (shifted.getNumSamples() == voice.getNumSamples());
    const auto lateAnalysis = pitchcorrect::analyse (shifted, rate);
    REQUIRE (lateAnalysis.notes.size() == 2);
    CHECK_THAT (lateAnalysis.frameToSeconds (lateAnalysis.notes[1].startFrame) - analysis.frameToSeconds (analysis.notes[1].startFrame),
                WithinAbs (0.06, 0.02));

    // Split and join
    auto cut = analysis;
    std::vector<pitchcorrect::NoteEdit> cutEdits (2);
    const auto middle = (cut.notes[0].startFrame + cut.notes[0].endFrame) / 2;
    pitchcorrect::splitNote (cut, cutEdits, 0, middle);
    REQUIRE (cut.notes.size() == 3);
    CHECK (cut.notes[0].endFrame == middle);
    const auto segments = pitchcorrect::segmentsToString (cut);
    auto again2 = analysis;
    pitchcorrect::applySegments (again2, segments);
    CHECK (again2.notes.size() == 3);
    pitchcorrect::joinNotes (cut, cutEdits, { 0, 1 });
    CHECK (cut.notes.size() == 2);
    CHECK (cutEdits.size() == 2);
}

TEST_CASE ("Flex Pitch: Keep in Scale moves notes only onto the scale's notes, keeping how they're sung", "[pitch]")
{
    using namespace riff;
    const std::vector<int> cMajor { 0, 2, 4, 5, 7, 9, 11 };
    CHECK (pitchcorrect::nearestInScale (60.9, cMajor) == 60);   // C# - 10 cents: nearer C
    CHECK (pitchcorrect::nearestInScale (61.2, cMajor) == 62);   // C# + 20 cents: nearer D
    CHECK (pitchcorrect::nearestInScale (64.3, cMajor) == 64);   // E stays
    CHECK (pitchcorrect::nextInScale (64, 1, cMajor) == 65);     // E -> F
    CHECK (pitchcorrect::nextInScale (65, 1, cMajor) == 67);     // F -> G (skips F#)
    CHECK (pitchcorrect::nextInScale (60, -1, cMajor) == 59);    // C -> B

    pitchcorrect::Analysis a;
    pitchcorrect::Note n1, n2;
    n1.centre = 61.1;   // C# + 10 cents
    n2.centre = 66.95;  // G - 5 cents
    a.notes = { n1, n2 };
    std::vector<pitchcorrect::NoteEdit> edits (2);
    edits[1].shift = -1.0;   // moved down to F# - 5 cents: outside C major
    CHECK (pitchcorrect::keepInScale (edits, a, cMajor));
    CHECK (std::abs (edits[0].shift - 1.0) < 1.0e-9);   // to D + 10 cents: by a whole semitone, the cents kept
    CHECK (std::lround (a.notes[1].centre + edits[1].shift) % 12 != 6);
    CHECK_FALSE (pitchcorrect::keepInScale (edits, a, cMajor));   // nothing left outside
}

#include "engine/KeyDetect.h"

namespace
{
    /** Chords (MIDI notes), each a second, as a piano-ish sound (harmonics), a little sharp of A = 440 if asked. */
    juce::AudioBuffer<float> chords (const std::vector<std::vector<int>>& progression, double cents = 0.0, int repeats = 2)
    {
        const double rate = 44100.0;
        const int perChord = 44100;
        juce::AudioBuffer<float> out (1, perChord * (int) progression.size() * repeats);
        out.clear();
        int at = 0;
        for (int rep = 0; rep < repeats; ++rep)
            for (const auto& chord : progression)
            {
                for (const auto note : chord)
                {
                    const auto hz = 440.0 * std::pow (2.0, (note - 69 + cents / 100.0) / 12.0);
                    for (int i = 0; i < perChord; ++i)
                    {
                        const auto env = std::exp (-2.0 * i / rate);
                        double v = 0.0;
                        for (int h = 1; h <= 5; ++h)
                            v += std::sin (juce::MathConstants<double>::twoPi * hz * h * i / rate) / (h * h);
                        out.addSample (0, at + i, (float) (0.1 * env * v));
                    }
                }
                at += perChord;
            }
        return out;
    }
}

TEST_CASE ("Key detection: hears the key and whether it's major or minor", "[key]")
{
    using namespace riff;
    // G major: G - C - D - G - Em - C - D7 - G
    const auto g = keydetect::detect (keydetect::chromaOf (chords ({ { 43, 55, 59, 62 }, { 48, 55, 60, 64 }, { 50, 54, 57, 62 }, { 43, 55, 59, 62 },
                                                                    { 40, 55, 59, 64 }, { 48, 55, 60, 64 }, { 50, 54, 60, 62 }, { 43, 55, 59, 67 } }), 44100.0));
    CHECK (g.found);
    CHECK (keydetect::name (g.root, g.minor) == "G major");

    // A minor (harmonic: E major's G#): Am - Dm - E - Am - F - Dm - E7 - Am
    const auto a = keydetect::detect (keydetect::chromaOf (chords ({ { 45, 57, 60, 64 }, { 50, 57, 62, 65 }, { 40, 56, 59, 64 }, { 45, 57, 60, 64 },
                                                                    { 41, 57, 60, 65 }, { 50, 57, 62, 65 }, { 40, 56, 62, 64 }, { 45, 57, 60, 69 } }), 44100.0));
    CHECK (keydetect::name (a.root, a.minor) == "A minor");

    // E flat major, recorded 30 cents sharp: still E flat
    const auto eb = keydetect::detect (keydetect::chromaOf (chords ({ { 51, 58, 63, 67 }, { 44, 60, 63, 68 }, { 46, 58, 62, 65 }, { 51, 58, 63, 67 } }, 30.0), 44100.0));
    CHECK (keydetect::name (eb.root, eb.minor) == "Eb major");

    // Silence: nothing found
    juce::AudioBuffer<float> quiet (1, 44100);
    quiet.clear();
    CHECK_FALSE (keydetect::detect (keydetect::chromaOf (quiet, 44100.0)).found);
}
