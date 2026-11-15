#include "StudioInstruments.h"

namespace riff::studio
{

namespace
{
    // The kit's pieces: their GM notes, and the part (mixer strip) each plays into
    struct Piece { const char* id; const char* name; int part; std::initializer_list<int> notes; };
    const Piece pieces[] {
        { "kick",  "Kick",      0, { 35, 36 } },
        { "snare", "Snare",     1, { 37, 38, 39, 40 } },
        { "tom",   "Rack Tom",  2, { 47, 48, 50 } },
        { "floor", "Floor Tom", 2, { 41, 43, 45 } },
        { "hat",   "Hi-Hat",    3, { 42, 44, 46 } },
        { "crash", "Crash",     4, { 49, 55, 57 } },
        { "ride",  "Ride",      4, { 51, 53, 59 } },
        { "china", "Splash",    4, { 52 } },
    };
    constexpr int numPieces = 8;

    int pieceOf (int note)
    {
        for (int i = 0; i < numPieces; ++i)
            for (const auto n : pieces[i].notes)
                if (n == note)
                    return i;
        return -1;
    }

    std::vector<Param> drumParams()
    {
        std::vector<Param> p;
        p.push_back ({ "kit", "Kit", 0, 4, 0, { "Studio Rock", "Tight Funk", "Vintage", "Big Room", "Soft Jazz" } });
        p.push_back ({ "sel", "Selected", 0, numPieces - 1, 1, { "Kick", "Snare", "Rack Tom", "Floor Tom", "Hi-Hat", "Crash", "Ride", "Splash" } });
        for (const auto& piece : pieces)
        {
            const juce::String id (piece.id), name (piece.name);
            p.push_back ({ id + "Gain", name + " Gain", -24.0f, 12.0f, 0.0f });
            p.push_back ({ id + "Tune", name + " Tune", -12.0f, 12.0f, 0.0f });
            p.push_back ({ id + "Damp", name + " Dampen", 0.0f, 1.0f, 0.0f });
            p.push_back ({ id + "Pan", name + " Pan", -1.0f, 1.0f, 0.0f });
            p.push_back ({ id + "Tone", name + " Tone", 0.0f, 1.0f, 1.0f });
            p.push_back ({ id + "Alt", name + " Model", 0.0f, 4.0f, 0.0f, { "Model 1", "Model 2", "Model 3", "Model 4", "Model 5" } });
        }
        p.push_back ({ "overheads", "Overheads", 0.0f, 1.0f, 0.55f });
        p.push_back ({ "room", "Room", 0.0f, 1.0f, 0.35f });
        p.push_back ({ "ambience", "Ambience", 0.0f, 1.0f, 0.42f });
        p.push_back ({ "reverb", "Reverb", 0.0f, 1.0f, 0.2f });
        p.push_back ({ "master", "Master", -60.0f, 6.0f, -3.0f });
        p.push_back ({ "velocity", "Velocity", 0, 3, 0, { "Linear", "Soft", "Hard", "Fixed" } });
        p.push_back ({ "artic", "Articulation", 0, 2, 0, { "Center", "Edge", "Rim" } });
        p.push_back ({ "hatPedal", "Hi-Hat Pedal (CC4)", 0, 1, 1, { "Off", "On" } });
        p.push_back ({ "groove", "Groove", 0, 6, 0, StudioDrums::grooveNames() });
        return p;
    }

    // Each piece's other models: tuning, damping and tone that make it a different drum
    struct Alt { float tune, damp, tone, gain; const char* name; };
    const Alt alts[numPieces][5] {
        { { 0, 0, 1, 0, "Studio 22\"" }, { -3, 0.3f, 0.7f, 1, "Deep 24\"" }, { 2, 0.55f, 0.85f, 0, "Punch 20\"" }, { -1, 0.75f, 0.5f, 1, "Felt Thud" }, { 4, 0.2f, 1, -1, "Jazz 18\"" } },
        { { 0, 0, 1, 0, "Steel 14x6.5" }, { 1.5f, 0.3f, 1, 0, "Maple 14x5" }, { -2, 0.1f, 0.8f, 1, "Brass Deep" }, { 4, 0.4f, 1, -1, "Piccolo 13\"" }, { -1, 0.55f, 0.6f, 0, "Vintage Wood" } },
        { { 0, 0, 1, 0, "Rack 10/12" }, { -2, 0.3f, 0.8f, 0, "Rack 12/13" }, { 2, 0.5f, 1, 0, "Fusion" }, { -1, 0.7f, 0.6f, 0, "Muffled" }, { 3, 0, 1, -1, "Jazz" } },
        { { 0, 0, 1, 0, "Floor 16" }, { -2, 0.2f, 0.8f, 1, "Floor 18" }, { 1, 0.5f, 1, 0, "Floor 14" }, { -1, 0.7f, 0.6f, 0, "Muffled" }, { 2, 0, 1, -1, "Jazz" } },
        { { 0, 0, 1, 0, "Hats 14\"" }, { -1, 0.2f, 0.7f, 0, "Dark 15\"" }, { 2, 0.3f, 1, -1, "Bright 13\"" }, { -2, 0.1f, 0.5f, 0, "Vintage" }, { 1, 0.5f, 0.9f, 0, "Tight" } },
        { { 0, 0, 1, 0, "Crash 18\"" }, { -2, 0, 0.7f, 0, "Dark 19\"" }, { 2, 0.3f, 1, -1, "Thin 16\"" }, { -3, 0, 0.6f, 1, "Big 20\"" }, { 1, 0.6f, 0.8f, 0, "Choked" } },
        { { 0, 0, 1, 0, "Ride 21\"" }, { -2, 0, 0.7f, 0, "Dark 22\"" }, { 2, 0.2f, 1, -1, "Bright 20\"" }, { -1, 0.4f, 0.6f, 0, "Dry" }, { 1, 0, 0.9f, 0, "Bell" } },
        { { 0, 0, 1, 0, "Splash 10\"" }, { -2, 0, 0.8f, 0, "China 16\"" }, { 2, 0.3f, 1, -1, "Bright 8\"" }, { -1, 0.5f, 0.6f, 0, "Trash" }, { 1, 0, 1, 0, "Stack" } },
    };

    // The kits: their pieces' models and the room
    struct KitDef { int alt[numPieces]; float damp, tune, room, ambience, tone; };
    const KitDef kits[5] {
        { { 0, 0, 0, 0, 0, 0, 0, 0 }, 0.0f, 0.0f, 0.35f, 0.42f, 1.0f },   // Studio Rock
        { { 2, 1, 2, 2, 4, 2, 3, 2 }, 0.45f, 1.5f, 0.15f, 0.2f, 1.0f },   // Tight Funk
        { { 3, 4, 3, 3, 3, 1, 1, 1 }, 0.3f, -1.0f, 0.3f, 0.35f, 0.65f },  // Vintage
        { { 1, 2, 1, 1, 0, 3, 0, 1 }, 0.0f, -1.5f, 0.75f, 0.8f, 1.0f },   // Big Room
        { { 4, 3, 4, 4, 2, 2, 2, 0 }, 0.2f, 2.5f, 0.25f, 0.3f, 0.85f },   // Soft Jazz
    };
}

juce::String StudioDrums::pieceName (int piece) { return juce::isPositiveAndBelow (piece, numPieces) ? juce::String (pieces[piece].name) : juce::String(); }
juce::String StudioDrums::pieceId (int piece) { return juce::isPositiveAndBelow (piece, numPieces) ? juce::String (pieces[piece].id) : juce::String(); }
int StudioDrums::numberOfPieces() { return numPieces; }
int StudioDrums::noteOfPiece (int piece) { return juce::isPositiveAndBelow (piece, numPieces) ? *(pieces[piece].notes.begin() + (piece == 0 ? 1 : piece == 1 ? 1 : 0)) : 36; }
juce::String StudioDrums::altName (int piece, int alt)
{
    return juce::isPositiveAndBelow (piece, numPieces) && juce::isPositiveAndBelow (alt, 5) ? juce::String (alts[piece][alt].name) : juce::String();
}

std::vector<std::pair<juce::String, float>> StudioDrums::kitSettings (int kit)
{
    std::vector<std::pair<juce::String, float>> s;
    kit = juce::jlimit (0, 4, kit);
    const auto& k = kits[kit];
    s.push_back ({ "kit", (float) kit });
    for (int i = 0; i < numPieces; ++i)
    {
        const juce::String id (pieces[i].id);
        s.push_back ({ id + "Alt", (float) k.alt[i] });
        s.push_back ({ id + "Damp", k.damp });
        s.push_back ({ id + "Tune", k.tune });
        s.push_back ({ id + "Tone", k.tone });
        s.push_back ({ id + "Gain", 0.0f });
    }
    s.push_back ({ "room", k.room });
    s.push_back ({ "ambience", k.ambience });
    return s;
}

StudioDrums::StudioDrums()
    : StudioInstrument ("drums", drumParams(), { "Kick", "Snare", "Toms", "Hats", "Cymbals", "Overheads", "Room" })
{
    kitIndex = nemix::samples::indexOf ("rock-kit");
    if (kitIndex >= 0)
        nemix::samples::request (kitIndex);
}

void StudioDrums::prepareVoices (double sampleRate, int maxBlockSize)
{
    grooveMidi.ensureSize (4096);
    voices.prepare (sampleRate);
    sum.setSize (2, maxBlockSize);
    room.prepare (sampleRate);
    plate.prepare (sampleRate);
    for (auto& f : ohFilter) f.reset();
    for (auto& f : ohFilter) f.set (320.0f, 0.6f, sampleRate);
}

void StudioDrums::play (int note, int velocity) noexcept
{
    auto piece = pieceOf (note);
    if (piece < 0)
        return;
    const auto* ins = nemix::samples::find (kitIndex);
    // the snare's articulation (the bar's choice) and the hi-hat pedal (CC4: closed, the open hat plays closed)
    if (piece == 1 && note == 38)
        note = choice (indexOf ("artic")) == 1 ? 40 : choice (indexOf ("artic")) == 2 ? 37 : 38;
    if (piece == 4 && note == 46 && choice (indexOf ("hatPedal")) == 1 && hatPedal > 0.75f)
        note = 42;
    const juce::String id (pieces[piece].id);
    const auto alt = alts[piece][juce::jlimit (0, 4, choice (indexOf (id + "Alt")))];
    const auto damp = juce::jlimit (0.0f, 1.0f, p (indexOf (id + "Damp")) + alt.damp * 0.5f);
    const auto tone = juce::jlimit (0.0f, 1.0f, p (indexOf (id + "Tone")) * alt.tone);
    const auto vel = dsp::velocityCurve ((float) velocity / 127.0f, choice (indexOf ("velocity")));

    SampleVoices<48>::Start s;
    s.note = note;
    s.velocity = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f));
    s.part = pieces[piece].part;
    s.semitones = p (indexOf (id + "Tune")) + alt.tune;
    s.gainDb = p (indexOf (id + "Gain")) + alt.gain;
    s.pan = p (indexOf (id + "Pan"));
    s.decay = damp > 0.01f ? juce::jmap (damp, 2.5f, 0.12f) : 0.0f;
    s.release = 0.05f;
    s.oneShot = true;
    s.toneHz = tone >= 0.99f ? 0.0f : 1500.0f * std::pow (13.0f, tone);
    // the hi-hat: closed and pedal choke the open one
    s.group = piece == 4 ? (note == 46 ? 1 : 2) : 0;
    s.chokeGroup = (piece == 4 && note != 46) ? 1 : 0;
    if (voices.start (ins, s) != nullptr)
    {
        lastNote.store (note);
        lastVelocity.store (velocity);
        lastPart.store (piece);
    }
}

juce::StringArray StudioDrums::grooveNames()
{
    return { "Off", "Rock", "Funk", "Shuffle", "Half-Time", "Jazz Ride", "Four on the Floor" };
}

namespace
{
    // The grooves, a bar of 16 steps each: kick, snare, closed hat, open hat, ride, pedal hat (velocities; 0 none)
    struct Groove { juce::uint8 kick[16], snare[16], hat[16], open[16], ride[16], pedal[16]; };
    const Groove grooves[] {
        { { 120,0,0,0, 0,0,0,0, 110,0,90,0, 0,0,0,0 }, { 0,0,0,0, 118,0,0,0, 0,0,0,0, 120,0,0,0 },
          { 96,0,70,0, 92,0,70,0, 96,0,70,0, 92,0,70,0 }, {}, {}, {} },                                        // Rock
        { { 120,0,0,100, 0,0,0,0, 0,0,105,0, 0,0,0,0 }, { 0,0,0,0, 120,0,0,38, 0,40,0,0, 118,0,0,0 },
          { 90,50,75,50, 90,50,75,50, 90,50,75,50, 90,50,0,50 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,80,0 }, {}, {} }, // Funk
        { { 120,0,0,0, 0,0,0,0, 110,0,0,95, 0,0,0,0 }, { 0,0,0,0, 118,0,0,0, 0,0,0,0, 120,0,0,0 },
          { 96,0,0,70, 92,0,0,70, 96,0,0,70, 92,0,0,70 }, {}, {}, {} },                                       // Shuffle
        { { 120,0,0,0, 0,0,0,0, 0,0,100,0, 0,0,0,0 }, { 0,0,0,0, 0,0,0,0, 122,0,0,0, 0,0,0,0 },
          { 92,0,64,0, 88,0,64,0, 92,0,64,0, 88,0,64,0 }, {}, {}, {} },                                       // Half-Time
        { { 50,0,0,0, 0,0,0,0, 45,0,0,0, 0,0,0,0 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,35 },
          {}, {}, { 100,0,0,0, 92,0,0,80, 98,0,0,0, 92,0,0,80 }, { 0,0,0,0, 90,0,0,0, 0,0,0,0, 90,0,0,0 } },   // Jazz Ride
        { { 124,0,0,0, 120,0,0,0, 124,0,0,0, 120,0,0,0 }, { 0,0,0,0, 112,0,0,0, 0,0,0,0, 112,0,0,0 },
          { 80,0,0,0, 76,0,0,0, 80,0,0,0, 76,0,0,0 }, { 0,0,96,0, 0,0,96,0, 0,0,96,0, 0,0,96,0 }, {}, {} },  // Four on the Floor
    };
}

void StudioDrums::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midiIn) noexcept
{
    // The GROOVE key: a beat of its own, in time with the song (and running while it's stopped)
    const juce::MidiBuffer* source = &midiIn;
    if (const auto g = choice (indexOf ("groove")); g > 0 && g <= (int) std::size (grooves))
    {
        grooveMidi.clear();
        grooveMidi.addEvents (midiIn, 0, n, 0);
        const auto stepLength = rate * 60.0 / juce::jmax (20.0, songBpm) / 4.0;
        const auto start = songPlaying ? (double) songPosition : grooveClock;
        const auto& groove = grooves[(size_t) g - 1];
        auto step = (juce::int64) std::ceil (start / stepLength);
        for (;; ++step)
        {
            const auto at = (int) std::llround ((double) step * stepLength - start);
            if (at >= n) break;
            if (at < 0) continue;
            const auto s = (size_t) (step % 16);
            const auto add = [this, at] (int note, juce::uint8 velocity)
            {
                if (velocity > 0)
                    grooveMidi.addEvent (juce::MidiMessage::noteOn (10, note, velocity), at);
            };
            add (36, groove.kick[s]);
            add (38, groove.snare[s]);
            add (42, groove.hat[s]);
            add (46, groove.open[s]);
            add (51, groove.ride[s]);
            add (44, groove.pedal[s]);
        }
        grooveClock = songPlaying ? (double) songPosition + n : grooveClock + n;
        source = &grooveMidi;
    }
    else
        grooveClock = 0.0;
    const auto& midi = *source;

    int done = 0;
    auto renderTo = [&] (int until)
    {
        if (until <= done) return;
        float* l[7], *r[7];
        for (int i = 0; i < 5; ++i) { l[i] = pl[i] + done; r[i] = pr[i] + done; }
        voices.render (l, r, 5, until - done, &noteLevel);
        done = until;
    };
    for (const auto m : midi)
    {
        renderTo (juce::jlimit (0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
            play (msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isController() && msg.getControllerNumber() == 4)
            hatPedal = (float) msg.getControllerValue() / 127.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            voices.allOff();
    }
    renderTo (n);
    for (auto& level : noteLevel)
        level.store (level.load (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);

    // Overheads and room: the kit as the overheads and the room mics hear it
    auto* sl = sum.getWritePointer (0);
    auto* sr = sum.getWritePointer (1);
    juce::FloatVectorOperations::clear (sl, n);
    juce::FloatVectorOperations::clear (sr, n);
    for (int i = 0; i < 5; ++i)
    {
        juce::FloatVectorOperations::add (sl, pl[i], n);
        juce::FloatVectorOperations::add (sr, pr[i], n);
    }
    const auto oh = p (indexOf ("overheads")) * 0.8f;
    if (oh > 0.0f)
        for (int i = 0; i < n; ++i)
        {
            pl[5][i] += ohFilter[0].high (sl[i]) * oh;
            pr[5][i] += ohFilter[1].high (sr[i]) * oh;
        }
    room.set (0.25f + 0.5f * p (indexOf ("ambience")), 0.45f, 8.0f);
    room.process (sl, sr, pl[6], pr[6], n, p (indexOf ("room")) * 1.4f);
    plate.set (0.9f, 0.25f, 22.0f);
    plate.process (sl, sr, pl[6], pr[6], n, p (indexOf ("reverb")) * 0.9f);
    // the master knob
    const auto master = dsp::dbToGain (p (indexOf ("master")));
    for (int i = 0; i < 7; ++i)
    {
        juce::FloatVectorOperations::multiply (pl[i], master, n);
        juce::FloatVectorOperations::multiply (pr[i], master, n);
    }
}

juce::StringArray StudioDrums::screenLines() const
{
    const auto sel = juce::jlimit (0, numPieces - 1, choice (indexOf ("sel")));
    const juce::String id (pieces[sel].id);
    const auto kitNames = params()[(size_t) indexOf ("kit")].choices;
    return { kitNames[juce::jlimit (0, 4, choice (indexOf ("kit")))] + " Kit",
             juce::String (pieces[sel].name).toUpperCase() + juce::String (juce::CharPointer_UTF8 (" · ")) + altName (sel, choice (indexOf (id + "Alt"))),
             juce::String (p (indexOf (id + "Gain")), 1) + " dB",
             (p (indexOf (id + "Tune")) >= 0 ? "+" : "") + juce::String (p (indexOf (id + "Tune")), 1) + " st",
             juce::String (juce::roundToInt (p (indexOf (id + "Damp")) * 100.0f)) + "%",
             params()[(size_t) indexOf ("artic")].choices[choice (indexOf ("artic"))],
             choice (indexOf ("groove")) > 0 ? "GROOVE: " + grooveNames()[choice (indexOf ("groove"))] : juce::String() };
}

} // namespace riff::studio
