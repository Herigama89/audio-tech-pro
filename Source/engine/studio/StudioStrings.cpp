#include "StudioInstruments.h"

namespace riff::studio
{

//==============================================================================
// Bass
namespace
{
    const char* const bassLibrary[] { "finger-bass", "pick-bass", "jazz-bass", "fashion-bass", "swag-bass", "killer-bass", "upright-bass" };

    std::vector<Param> bassParams()
    {
        std::vector<Param> p;
        p.push_back ({ "model", "Model", 0, 6, 0, StudioBass::models() });
        p.push_back ({ "strings", "Strings", 0, 1, 1, { "4 strings", "5 strings (low B)" } });
        p.push_back ({ "style", "Playing", 0, 7, 0, StudioBass::styles() });
        p.push_back ({ "gain", "Gain", -24.0f, 12.0f, -0.8f });
        p.push_back ({ "tune", "Tune", -1.0f, 1.0f, 0.0f });
        p.push_back ({ "pickups", "Pickups", 0.0f, 1.0f, 0.6f });
        p.push_back ({ "hipass", "High-Pass", 20.0f, 250.0f, 40.0f });
        p.push_back ({ "tone", "Tone", 0.0f, 1.0f, 0.6f });
        p.push_back ({ "comp", "Comp", 0.0f, 1.0f, 0.3f });
        p.push_back ({ "drive", "Drive", 0.0f, 1.0f, 0.18f });
        p.push_back ({ "ambience", "Ambience", 0.0f, 1.0f, 0.3f });
        p.push_back ({ "reverb", "Reverb", 0.0f, 1.0f, 0.25f });
        p.push_back ({ "master", "Master", -60.0f, 6.0f, -3.0f });
        p.push_back ({ "velocity", "Velocity", 0, 3, 0, { "Linear", "Soft", "Hard", "Fixed" } });
        p.push_back ({ "mode", "Mode", 0, 1, 1, { "Passive", "Active" } });
        return p;
    }
}

juce::StringArray StudioBass::models() { return { "Studio Fingered", "Studio Pick", "Jazz Bass", "Mirror Bass", "Studio Bass", "Killer Fingered", "Upright Bass" }; }
juce::StringArray StudioBass::styles() { return { "Finger", "Pick", "Slap", "Mute", "Slide", "Harmonics", "Ghost", "Hold" }; }

StudioBass::StudioBass() : StudioInstrument ("bass", bassParams(), { "DI", "Amp", "Room" })
{
    for (size_t i = 0; i < std::size (bassLibrary); ++i)
        libraryIndex[i] = nemix::samples::indexOf (bassLibrary[i]);
    setDefault (partParam (1, "gain"), -4.0f);
    setDefault (partParam (2, "gain"), -10.0f);
    paramChanged (indexOf ("model"));
}

void StudioBass::paramChanged (int index) noexcept
{
    const auto& id = params()[(size_t) index].id;
    if (id == "model" || id == "style")
    {
        const auto model = juce::jlimit (0, 6, choice (indexOf ("model")));
        if (libraryIndex[(size_t) model] >= 0) nemix::samples::request (libraryIndex[(size_t) model]);
        if (libraryIndex[1] >= 0) nemix::samples::request (libraryIndex[1]);   // (pick and slap use the picked bass)
    }
}

void StudioBass::prepareVoices (double sampleRate, int maxBlockSize)
{
    voices.prepare (sampleRate);
    di.setSize (2, maxBlockSize);
    room.prepare (sampleRate);
    for (auto* f : { pickupLow, pickupHigh, hpf, toneF, cab, slapPeak })
        for (int c = 0; c < 2; ++c) f[c].reset();
    for (auto& h : harms) h = {};
}

std::pair<int, int> StudioBass::stringAndFret (int note) const noexcept
{
    const bool five = choice (indexOf ("strings")) == 1;
    const int tuning5[] { 23, 28, 33, 38, 43 };
    const int count = five ? 5 : 4;
    const int* open = five ? tuning5 : tuning5 + 1;
    // the highest string with the note low on the neck (as a player would), else the lowest that has it
    for (int s = count - 1; s >= 0; --s)
        if (const auto fret = note - open[s]; fret >= 0 && fret <= 5)
            return { s, fret };
    for (int s = 0; s < count; ++s)
        if (const auto fret = note - open[s]; fret >= 0 && fret <= 20)
            return { s, fret };
    return { -1, -1 };
}

void StudioBass::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midi) noexcept
{
    auto* dl = di.getWritePointer (0);
    auto* dr = di.getWritePointer (1);
    juce::FloatVectorOperations::clear (dl, n);
    juce::FloatVectorOperations::clear (dr, n);
    const auto style = choice (indexOf ("style"));
    const auto model = juce::jlimit (0, 6, choice (indexOf ("model")));
    const bool five = choice (indexOf ("strings")) == 1;

    int done = 0;
    float* l1[1] { dl }, *r1[1] { dr };
    auto renderTo = [&] (int until)
    {
        if (until <= done) return;
        l1[0] = dl + done; r1[0] = dr + done;
        voices.render (l1, r1, 1, until - done, &noteLevel);
        // harmonics: a pure, bell-like partial an octave up
        for (auto& h : harms)
            if (h.note >= 0)
            {
                const auto inc = 440.0 * std::pow (2.0, (h.note + 12 - 69) / 12.0) / rate;
                for (int i = done; i < until; ++i)
                {
                    const auto s = (float) std::sin (h.phase * juce::MathConstants<double>::twoPi) * h.env * 0.35f;
                    dl[i] += s; dr[i] += s;
                    h.phase += inc;
                    if (h.phase >= 1.0) h.phase -= 1.0;
                    h.env *= 0.99992f;
                }
                if (h.env < 1.0e-3f) h.note = -1;
            }
        done = until;
    };

    for (const auto m : midi)
    {
        renderTo (juce::jlimit (0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            auto note = msg.getNoteNumber();
            if (note < (five ? 23 : 28)) note += 12;   // (below the lowest string: an octave up)
            const auto vel = dsp::velocityCurve ((float) msg.getVelocity() / 127.0f, choice (indexOf ("velocity")));
            const auto previous = heldNote;
            held[(size_t) msg.getNoteNumber()] = true;
            heldNote = note;
            lastNote.store (note);
            lastVelocity.store (msg.getVelocity());
            if (style == 4 && previous >= 0 && voices.slide (previous, note, 0.08f))
                continue;   // a slide: the string bends to the new note
            if (style == 5)
            {
                for (auto& h : harms)
                    if (h.note < 0) { h = { note, 0.0, vel }; break; }
                continue;
            }
            // one note at a time on a string: the last one stops (unless held as a chord)
            if (previous >= 0 && style != 7)
                voices.noteOff (previous, false);
            SampleVoices<16>::Start s;
            s.note = note;
            s.velocity = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f));
            s.semitones = p (indexOf ("tune"));
            s.release = 0.06f;
            const bool picked = style == 1 || style == 2;
            const auto ins = nemix::samples::find (libraryIndex[(size_t) (picked && model != 6 ? 1 : model)]);
            switch (style)
            {
                case 2: s.toneHz = 0.0f; s.decay = 0.9f; s.gainDb = 2.0f; break;      // slap: bright, quick
                case 3: s.toneHz = 900.0f; s.decay = 0.22f; s.gainDb = -2.0f; break; // muted
                case 6: s.toneHz = 600.0f; s.decay = 0.07f; s.gainDb = -10.0f; break; // ghost
                default: break;
            }
            voices.start (ins, s);
        }
        else if (msg.isNoteOff())
        {
            auto note = msg.getNoteNumber();
            held[(size_t) note] = false;
            if (note < (five ? 23 : 28)) note += 12;
            if (style != 7)
            {
                voices.noteOff (note, false);
                if (note == heldNote) heldNote = -1;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            voices.allOff();
            heldNote = -1;
        }
    }
    renderTo (n);
    for (auto& level : noteLevel)
        level.store (level.load (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);

    // The pickups (neck: round and deep; bridge: the honk), the active preamp, the high pass and tone, the comp
    const auto blend = p (indexOf ("pickups"));
    const bool active = choice (indexOf ("mode")) == 1;
    const auto toneHz = 800.0f * std::pow (12.0f, p (indexOf ("tone")));
    const auto compAmt = p (indexOf ("comp"));
    const auto gain = dsp::dbToGain (p (indexOf ("gain")));
    for (int c = 0; c < 2; ++c)
    {
        pickupLow[c].set (180.0f, 0.7f, rate);
        pickupHigh[c].set (850.0f, 1.2f, rate);
        hpf[c].set (p (indexOf ("hipass")), 0.7f, rate);
        toneF[c].set (toneHz, 0.7f, rate);
        slapPeak[c].set (2600.0f, 1.0f, rate);
    }
    const auto compAttack = std::exp (-1.0f / (0.003f * (float) rate)), compRelease = std::exp (-1.0f / (0.12f * (float) rate));
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& x = c == 0 ? dl[i] : dr[i];
            auto low = pickupLow[c].run (x);
            const auto honk = pickupHigh[c].band (x);
            x = x + low.low * (1.0f - blend) * 0.8f + honk * blend * 1.2f - low.low * blend * 0.3f;
            if (active) x += low.low * 0.4f + (x - low.low) * 0.2f;
            if (style == 2) x += slapPeak[c].band (x) * 2.0f;
            x = hpf[c].high (x);
            x = toneF[c].low (x);
        }
        // a simple compressor on both sides together
        const auto level = juce::jmax (std::abs (dl[i]), std::abs (dr[i]));
        compEnv = level > compEnv ? level + (compEnv - level) * compAttack : level + (compEnv - level) * compRelease;
        const auto threshold = 0.4f - compAmt * 0.3f;
        const auto g = compEnv > threshold ? std::pow (threshold / compEnv, compAmt * 0.75f) : 1.0f;
        dl[i] *= g * gain * (1.0f + compAmt);
        dr[i] *= g * gain * (1.0f + compAmt);
    }
    const auto master = dsp::dbToGain (p (indexOf ("master")));
    const auto drive = p (indexOf ("drive"));
    for (int c = 0; c < 2; ++c) cab[c].set (3200.0f, 0.9f, rate);
    for (int i = 0; i < n; ++i)
    {
        pl[0][i] += dl[i] * master;
        pr[0][i] += dr[i] * master;
        // the amp: driven, through its cabinet
        pl[1][i] += cab[0].low (dsp::drive (dl[i] * (1.0f + drive * 3.0f), drive)) * master;
        pr[1][i] += cab[1].low (dsp::drive (dr[i] * (1.0f + drive * 3.0f), drive)) * master;
    }
    room.set (0.15f + 0.4f * p (indexOf ("ambience")), 0.55f, 5.0f);
    room.process (dl, dr, pl[2], pr[2], n, (p (indexOf ("ambience")) + p (indexOf ("reverb"))) * 0.9f * master);
}

juce::StringArray StudioBass::screenLines() const
{
    const auto note = lastNote.load();
    const auto [string, fret] = stringAndFret (note);
    const juce::StringArray names5 { "B", "E", "A", "D", "G" }, names4 { "E", "A", "D", "G" };
    const bool five = choice (indexOf ("strings")) == 1;
    return { models()[juce::jlimit (0, 6, choice (indexOf ("model")))] + (five ? " 5" : ""),
             styles()[choice (indexOf ("style"))].toUpperCase() + (choice (indexOf ("mode")) == 1 ? " · ACTIVE" : " · PASSIVE"),
             juce::String (juce::roundToInt (p (indexOf ("pickups")) * 100.0f)) + "/" + juce::String (100 - juce::roundToInt (p (indexOf ("pickups")) * 100.0f)),
             juce::String (juce::roundToInt (p (indexOf ("hipass")))) + "Hz",
             juce::String (juce::roundToInt (p (indexOf ("tone")) * 100.0f)) + "%",
             juce::String (juce::roundToInt (p (indexOf ("drive")) * 100.0f)) + "%",
             note >= 0 && string >= 0 ? "STRING " + (five ? names5 : names4)[string] + " · FRET " + juce::String (fret)
                                          + " · NOTE " + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                                      : juce::String ("STRINGS ") + (five ? "B E A D G" : "E A D G") };
}

//==============================================================================
// Guitar
namespace
{
    const char* const guitarLibrary[] { "clean-electric", "emily-guitar", "steel-guitar", "nylon-guitar" };
    // the strum patterns, in eighths: D down, U up, x a muted chop, . rest
    const char* const strumPatterns[] { "DDUUDU.U", "D.DUD.DU", "D.U.D.U.", "DxUxDxUx", "D...D...", "DDDDDDDD", "D.DU.UDU", "D..UD.U." };

    std::vector<Param> guitarParams()
    {
        std::vector<Param> p;
        p.push_back ({ "model", "Model", 0, 3, 0, StudioGuitar::models() });
        p.push_back ({ "style", "Playing", 0, 5, 0, StudioGuitar::styles() });
        p.push_back ({ "pattern", "Strum", 0, 7, 0, StudioGuitar::patterns() });
        p.push_back ({ "volume", "Volume", -24.0f, 12.0f, 0.0f });
        p.push_back ({ "tone", "Tone", 0.0f, 1.0f, 0.72f });
        p.push_back ({ "pickup", "Pickup", 0.0f, 1.0f, 0.6f });
        p.push_back ({ "drive", "Drive", 0.0f, 1.0f, 0.12f });
        p.push_back ({ "bass", "Bass", -12.0f, 12.0f, 0.0f });
        p.push_back ({ "mid", "Mid", -12.0f, 12.0f, 0.0f });
        p.push_back ({ "treble", "Treble", -12.0f, 12.0f, 0.0f });
        p.push_back ({ "reverb", "Reverb", 0.0f, 1.0f, 0.25f });
        p.push_back ({ "amp", "Amp", 0, 3, 0, { "Clean '65", "Crunch", "Rock", "High Gain" } });
        p.push_back ({ "tuning", "Tuning", 0, 2, 0, { "Standard", "Drop D", "Half step down" } });
        p.push_back ({ "capo", "Capo", 0, 7, 0 });
        p.push_back ({ "speed", "Strum Speed", 4.0f, 45.0f, 14.0f });
        p.push_back ({ "velocity", "Velocity", 0, 3, 0, { "Linear", "Soft", "Hard", "Fixed" } });
        p.push_back ({ "master", "Master", -60.0f, 6.0f, -3.0f });
        return p;
    }

    const char* const chordKinds[] { "", "m", "7", "maj7", "m7", "sus2", "sus4", "dim", "aug", "5", "6", "m6", "add9" };
    const std::vector<std::vector<int>> chordIntervals {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 7 }, { 0, 5, 7 },
        { 0, 3, 6 }, { 0, 4, 8 }, { 0, 7 }, { 0, 4, 7, 9 }, { 0, 3, 7, 9 }, { 0, 2, 4, 7 } };
}

juce::StringArray StudioGuitar::models() { return { "Clean Electric", "Hollow Electric", "Steel Acoustic", "Nylon" }; }
juce::StringArray StudioGuitar::styles() { return { "Strum", "Pick", "Mute", "Hold", "Power", "Harmonics" }; }
juce::StringArray StudioGuitar::patterns()
{
    juce::StringArray p;
    for (const auto* s : strumPatterns)
        p.add (juce::String (s).replace (".", "·").replace ("D", "↓").replace ("U", "↑").replace ("x", "×"));
    return p;
}

StudioGuitar::StudioGuitar() : StudioInstrument ("guitar", guitarParams(), { "DI", "Amp", "Room" })
{
    for (size_t i = 0; i < std::size (guitarLibrary); ++i)
        libraryIndex[i] = nemix::samples::indexOf (guitarLibrary[i]);
    for (auto& f : frets) f.store (-1);
    setDefault (partParam (0, "gain"), -12.0f);
    setDefault (partParam (2, "gain"), -8.0f);
    paramChanged (indexOf ("model"));
}

void StudioGuitar::paramChanged (int index) noexcept
{
    if (params()[(size_t) index].id == "model")
    {
        const auto model = juce::jlimit (0, 3, choice (index));
        if (libraryIndex[(size_t) model] >= 0) nemix::samples::request (libraryIndex[(size_t) model]);
    }
}

void StudioGuitar::prepareVoices (double sampleRate, int maxBlockSize)
{
    voices.prepare (sampleRate);
    di.setSize (2, maxBlockSize);
    room.prepare (sampleRate);
    numPending = 0;
    sounding.fill (-1);
}

juce::String StudioGuitar::chordName() const
{
    const auto root = chordRoot.load();
    if (root < 0) return {};
    return juce::String (juce::MidiMessage::getMidiNoteName (root, true, false, 3)) + chordKinds[juce::jlimit (0, 12, chordKind.load())];
}

std::array<int, 6> StudioGuitar::chordFrets() const noexcept
{
    std::array<int, 6> f {};
    for (int i = 0; i < 6; ++i) f[(size_t) i] = frets[(size_t) i].load (std::memory_order_relaxed);
    return f;
}

void StudioGuitar::voiceChord() noexcept
{
    // the chord held: its lowest note is the root, its shape from the intervals above it
    int lowest = -1;
    std::array<bool, 12> classes {};
    for (int k = 0; k < 128; ++k)
        if (held[(size_t) k])
        {
            if (lowest < 0) lowest = k;
            classes[(size_t) (k % 12)] = true;
        }
    if (lowest < 0)
    {
        chordRoot.store (-1);
        for (auto& f : frets) f.store (-1);
        return;
    }
    const auto root = lowest % 12;
    int best = 0, bestScore = -100;
    for (int k = 0; k < (int) chordIntervals.size(); ++k)
    {
        int score = 0;
        std::array<bool, 12> want {};
        for (const auto i : chordIntervals[(size_t) k]) want[(size_t) ((root + i) % 12)] = true;
        for (int c = 0; c < 12; ++c)
            score += (want[(size_t) c] && classes[(size_t) c]) ? 2 : (want[(size_t) c] != classes[(size_t) c] ? -1 : 0);
        if (score > bestScore) { bestScore = score; best = k; }
    }
    chordRoot.store (lowest);
    chordKind.store (best);
    // power chords in Power style
    std::vector<int> intervals = choice (indexOf ("style")) == 4 ? std::vector<int> { 0, 7 } : chordIntervals[(size_t) best];
    std::array<bool, 12> tones {};
    for (const auto i : intervals) tones[(size_t) ((root + i) % 12)] = true;

    // the six strings (standard, drop D or half step down, with the capo); the shape where the root first sits
    // on the low E or A string, a window of four frets from there, open strings where they fit
    int open[6] { 40, 45, 50, 55, 59, 64 };
    const auto tuning = choice (indexOf ("tuning"));
    if (tuning == 1) open[0] = 38;
    if (tuning == 2) for (auto& o : open) --o;
    const auto capo = choice (indexOf ("capo"));
    int rootString = 0, start = 0;
    for (int s = 0; s < 2; ++s)
    {
        const auto f = ((root - (open[s] + capo) % 12) + 12) % 12;
        if (s == 0 || f < start) { start = f; rootString = s; }
        if (s == 0 && f <= 5) break;
    }
    for (int s = 0; s < 6; ++s)
    {
        int fret = -1;
        if (s >= rootString)
        {
            if (choice (indexOf ("style")) == 4 && s > rootString + 2) { frets[(size_t) s].store (-1); continue; }
            for (int f = 0; f <= start + 4 && fret < 0; ++f)
            {
                if (f != 0 && f < start) continue;
                if (s == rootString && f != start) continue;
                if (tones[(size_t) ((open[s] + capo + f) % 12)]) fret = f;
            }
        }
        frets[(size_t) s].store (fret < 0 ? -1 : fret + capo);
    }
}

void StudioGuitar::strum (bool down, float velocity, int offset) noexcept
{
    int open[6] { 40, 45, 50, 55, 59, 64 };
    const auto tuning = choice (indexOf ("tuning"));
    if (tuning == 1) open[0] = 38;
    if (tuning == 2) for (auto& o : open) --o;
    const auto f = chordFrets();
    const auto gap = juce::roundToInt (p (indexOf ("speed")) * 0.001f * (float) rate / 6.0f);
    int order = 0;
    for (int k = 0; k < 6; ++k)
    {
        const auto s = down ? k : 5 - k;
        if (f[(size_t) s] < 0 || (! down && s < 2)) continue;   // (an up stroke catches the top strings)
        if (numPending < (int) pending.size())
            pending[(size_t) numPending++] = { s, open[s] + f[(size_t) s], velocity * (down ? 1.0f : 0.8f) * (1.0f - 0.04f * (float) order), offset + gap * order, false };
        ++order;
    }
}

void StudioGuitar::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midi) noexcept
{
    auto* dl = di.getWritePointer (0);
    auto* dr = di.getWritePointer (1);
    juce::FloatVectorOperations::clear (dl, n);
    juce::FloatVectorOperations::clear (dr, n);
    const auto style = choice (indexOf ("style"));
    const auto model = juce::jlimit (0, 3, choice (indexOf ("model")));
    const auto* ins = nemix::samples::find (libraryIndex[(size_t) model]);
    const auto curve = choice (indexOf ("velocity"));

    const auto playString = [&] (int string, int note, float velocity, bool mute)
    {
        // a string plays one note at a time: the one it had stops
        if (sounding[(size_t) string] >= 0)
            voices.noteOff (sounding[(size_t) string], false);
        SampleVoices<32>::Start s;
        s.note = note;
        s.velocity = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
        s.release = 0.12f;
        s.pan = ((float) string - 2.5f) * 0.06f;
        if (mute || style == 2) { s.decay = 0.12f; s.toneHz = 1200.0f; }
        if (style == 5) { s.semitones = 12.0f; s.decay = 1.6f; }
        voices.start (ins, s);
        sounding[(size_t) string] = note;
        lastPart.store (string);
    };

    // the strummed chord: on each eighth of the song (or of its own clock when the song's stopped)
    const bool strumming = style == 0 || style == 4;
    bool chordHeld = false;
    for (auto h : held) chordHeld = chordHeld || h;
    if (strumming && chordHeld)
    {
        const auto samplesPerEighth = rate * 60.0 / songBpm / 2.0;
        const auto pattern = juce::String (strumPatterns[juce::jlimit (0, 7, choice (indexOf ("pattern")))]);
        const auto startPos = songPlaying ? (double) songPosition : internalBeats;
        for (int i = 0; i < n; ++i)
        {
            const auto step = (juce::int64) std::floor ((startPos + i) / samplesPerEighth);
            if (step != lastStep)
            {
                lastStep = step;
                const auto c = pattern[(int) (step % 8)];
                const auto accent = (step % 2 == 0) ? 0.9f : 0.7f;
                if (c == 'D' || c == 'U') strum (c == 'D', accent, i);
                else if (c == 'x')
                    for (int s = 0; s < 6; ++s)
                        if (const auto f = frets[(size_t) s].load(); f >= 0 && numPending < (int) pending.size())
                            pending[(size_t) numPending++] = { s, 40 + s * 5 + f, 0.6f, i, true };
            }
        }
    }
    if (! songPlaying) internalBeats += n;

    int done = 0;
    float* l1[1] { dl }, *r1[1] { dr };
    auto renderTo = [&] (int until)
    {
        if (until <= done) return;
        l1[0] = dl + done; r1[0] = dr + done;
        voices.render (l1, r1, 1, until - done, &noteLevel);
        done = until;
    };
    // MIDI and the strings due to sound, in time order
    auto it = midi.begin();
    for (int i = 0; i <= n; ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= i)
        {
            renderTo (juce::jlimit (0, n, (*it).samplePosition));
            const auto msg = (*it).getMessage();
            if (msg.isNoteOn())
            {
                const auto note = msg.getNoteNumber();
                const auto vel = dsp::velocityCurve ((float) msg.getVelocity() / 127.0f, curve);
                held[(size_t) note] = true;
                lastNote.store (note);
                lastVelocity.store (msg.getVelocity());
                if (strumming)
                {
                    voiceChord();
                    if (! songPlaying && lastStep < 0) { strum (true, vel, i); }
                }
                else
                {
                    // picked notes on the string that has them lowest on the neck
                    const int open[6] { 40, 45, 50, 55, 59, 64 };
                    int string = 0;
                    for (int s = 5; s >= 0; --s)
                        if (note >= open[s]) { string = s; break; }
                    if (style == 4)
                    {
                        playString (0, note, vel, false);
                        playString (1, note + 7, vel * 0.9f, false);
                        playString (2, note + 12, vel * 0.8f, false);
                    }
                    else
                        playString (string, note, vel, style == 2);
                }
            }
            else if (msg.isNoteOff())
            {
                held[(size_t) msg.getNoteNumber()] = false;
                if (strumming)
                {
                    voiceChord();
                    bool any = false;
                    for (auto h : held) any = any || h;
                    if (! any)
                    {
                        // the chord let go: the strings damped
                        for (auto& s : sounding)
                            if (s >= 0) { voices.noteOff (s, false); s = -1; }
                        numPending = 0;
                        lastStep = -1;
                    }
                }
                else if (style != 3)
                    voices.noteOff (msg.getNoteNumber(), false);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                voices.allOff();
                held.fill (false);
                voiceChord();
                numPending = 0;
            }
            ++it;
        }
        if (i == n) break;
        for (int k = 0; k < numPending;)
        {
            if (pending[(size_t) k].delay <= i)
            {
                renderTo (i);
                const auto pnd = pending[(size_t) k];
                playString (pnd.string, pnd.note, pnd.velocity, pnd.mute);
                pending[(size_t) k] = pending[(size_t) --numPending];
            }
            else
                ++k;
        }
    }
    renderTo (n);
    for (int k = 0; k < numPending; ++k)
        pending[(size_t) k].delay -= n;
    for (auto& level : noteLevel)
        level.store (level.load (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);

    // The pickups and tone (the DI), then the amp: its drive, its tone stack, its cabinet; the room
    const auto pickup = p (indexOf ("pickup"));
    const auto toneHz = 1500.0f * std::pow (10.0f, p (indexOf ("tone")));
    const auto ampModel = choice (indexOf ("amp"));
    const float modelDrive[] { 0.05f, 0.35f, 0.6f, 0.85f };
    const auto drive = juce::jlimit (0.0f, 1.0f, modelDrive[ampModel] + p (indexOf ("drive")) * 0.6f);
    const auto volume = dsp::dbToGain (p (indexOf ("volume")));
    const auto master = dsp::dbToGain (p (indexOf ("master")));
    for (int c = 0; c < 2; ++c)
    {
        toneF[c].set (toneHz, 0.7f, rate);
        preHp[c].set (90.0f + pickup * 120.0f, 0.7f, rate);
        ampLow[c].set (140.0f, 0.7f, rate);
        ampMid[c].set (800.0f, 0.9f, rate);
        ampHigh[c].set (3200.0f, 0.7f, rate);
        cab1[c].set (4800.0f, 0.8f, rate);
        cab2[c].set (110.0f, 0.7f, rate);
    }
    const auto lowG = dsp::dbToGain (p (indexOf ("bass"))) - 1.0f, midG = dsp::dbToGain (p (indexOf ("mid"))) - 1.0f,
               highG = dsp::dbToGain (p (indexOf ("treble"))) - 1.0f;
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            auto x = (c == 0 ? dl[i] : dr[i]) * volume;
            x = toneF[c].low (x);
            (c == 0 ? pl[0] : pr[0])[i] += x * master;
            auto a = preHp[c].high (x);
            a = dsp::drive (a * (1.0f + drive * 6.0f), drive) * (1.0f - drive * 0.45f);
            const auto lo = ampLow[c].low (a), mid = ampMid[c].band (a), hi = ampHigh[c].high (a);
            a += lo * lowG + mid * midG + hi * highG;
            a = cab2[c].high (cab1[c].low (a));
            (c == 0 ? pl[1] : pr[1])[i] += a * master;
            (c == 0 ? dl : dr)[i] = a;
        }
    room.set (0.5f, 0.4f, 12.0f);
    room.process (dl, dr, pl[2], pr[2], n, p (indexOf ("reverb")) * 1.2f * master);
}

juce::StringArray StudioGuitar::screenLines() const
{
    const auto name = chordName();
    return { models()[juce::jlimit (0, 3, choice (indexOf ("model")))],
             styles()[choice (indexOf ("style"))].toUpperCase() + " · AMP: " + params()[(size_t) indexOf ("amp")].choices[choice (indexOf ("amp"))].toUpperCase()
                 + (name.isNotEmpty() ? " · CHORD: " + name : juce::String()),
             juce::String (juce::roundToInt (p (indexOf ("pickup")) * 100.0f)) + "/" + juce::String (100 - juce::roundToInt (p (indexOf ("pickup")) * 100.0f)),
             juce::String (juce::roundToInt (p (indexOf ("tone")) * 100.0f)) + "%",
             juce::String (juce::roundToInt (p (indexOf ("drive")) * 100.0f)) + "%",
             patterns()[choice (indexOf ("pattern"))],
             "TUNING " + params()[(size_t) indexOf ("tuning")].choices[choice (indexOf ("tuning"))].toUpperCase() + " · CAPO " + juce::String (choice (indexOf ("capo"))) };
}

} // namespace riff::studio
