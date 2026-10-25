#include "Automix.h"
#include <map>

namespace riff::automix
{

juce::String roleName (Role r)
{
    switch (r)
    {
        case Role::kick:         return "Kick";
        case Role::snare:        return "Snare / Clap";
        case Role::hats:         return "Hi-hats / Cymbals";
        case Role::drums:        return "Drums";
        case Role::perc:         return "Percussion";
        case Role::bass:         return "Bass / 808";
        case Role::leadVocal:    return "Lead Vocal";
        case Role::backingVocal: return "Backing Vocals";
        case Role::guitar:       return "Guitar";
        case Role::keys:         return "Keys / Piano";
        case Role::synth:        return "Synth / Lead";
        case Role::pad:          return "Pad";
        case Role::strings:      return "Strings";
        case Role::brass:        return "Brass / Horns";
        case Role::fx:           return "FX";
        case Role::other:        break;
    }
    return "Other";
}

namespace
{
    // dB below the loudest element, per role:
    //                kick  snare hats drums perc  bass  lead  bv    gtr   keys  synth pad   str   brass fx    other
    struct Row { const char* genre; double v[16]; };
    const Row table[] {
        { "Hip-Hop",   { 0,   -2,   -9,  -3,   -9,   -1,    0,   -7,   -8,   -7,   -6,   -11,  -10,  -9,   -13,  -8 } },
        { "Trap",      { -1,  -3,   -8,  -3,   -9,    0,    0,   -7,   -9,   -8,   -6,   -11,  -10,  -10,  -13,  -8 } },
        { "R&B",       { -2,  -3,   -9,  -4,   -9,   -2,    0,   -5,   -7,   -6,   -7,   -9,   -8,   -9,   -13,  -8 } },
        { "Pop",       { -2,  -3,   -9,  -4,   -9,   -4,    0,   -6,   -6,   -6,   -5,   -9,   -9,   -9,   -12,  -7 } },
        { "Rock",      { -2,  -1,   -8,  -2,   -9,   -4,    0,   -6,   -2,   -6,   -7,   -10,  -9,   -8,   -12,  -6 } },
        { "Metal",     { -1,  -1,   -8,  -2,   -9,   -4,   -1,   -6,    0,   -8,   -8,   -11,  -10,  -9,   -12,  -6 } },
        { "EDM",       { 0,   -3,   -8,  -3,   -8,   -2,   -2,   -7,   -8,   -6,   -1,   -7,   -8,   -9,   -9,   -6 } },
        { "House",     { 0,   -4,   -7,  -3,   -7,   -2,   -3,   -7,   -8,   -5,   -3,   -8,   -8,   -9,   -10,  -6 } },
        { "Reggaeton", { 0,   -1,   -9,  -2,   -6,   -2,    0,   -6,   -8,   -8,   -6,   -10,  -10,  -10,  -12,  -8 } },
        { "Afrobeats", { -1,  -3,   -8,  -3,   -4,   -2,    0,   -6,   -6,   -7,   -6,   -10,  -9,   -8,   -12,  -7 } },
        { "Country",   { -3,  -3,   -9,  -4,   -10,  -4,    0,   -6,   -3,   -5,   -9,   -9,   -7,   -9,   -13,  -6 } },
        { "Gospel",    { -3,  -3,   -9,  -4,   -10,  -3,    0,   -3,   -6,   -3,   -7,   -7,   -7,   -6,   -13,  -6 } },
        { "Jazz",      { -5,  -5,   -7,  -4,   -8,   -2,    0,   -6,   -3,   -1,   -6,   -8,   -5,   -2,   -13,  -4 } },
        { "Lo-Fi",     { -2,  -3,   -8,  -3,   -8,   -3,   -2,   -7,   -5,    0,   -5,   -6,   -6,   -8,   -9,   -5 } },
        { "Acoustic",  { -5,  -5,   -9,  -5,   -9,   -4,    0,   -5,   -1,   -3,   -8,   -8,   -5,   -8,   -14,  -5 } },
    };

    bool has (const juce::String& name, std::initializer_list<const char*> words)
    {
        const auto lower = name.toLowerCase();
        for (const auto* w : words)
            if (lower.contains (w))
                return true;
        return false;
    }
}

juce::StringArray genres()
{
    juce::StringArray names;
    for (const auto& row : table)
        names.add (row.genre);
    return names;
}

Role guessRole (const Track& t)
{
    const auto& n = t.name;
    if (has (n, { "kick", "kik", "bd " })) return Role::kick;
    if (has (n, { "snare", "snr", "clap", "rim" })) return Role::snare;
    if (has (n, { "hat", "hh", "cymbal", "crash", "ride", "overhead", "oh " })) return Role::hats;
    if (has (n, { "808", "bass", "sub" })) return Role::bass;
    if (has (n, { "perc", "shaker", "conga", "bongo", "tamb", "tom" })) return Role::perc;
    if (has (n, { "drum", "beat", "loop", "break", "kit" })) return Role::drums;
    if (has (n, { "backing", "bgv", "bv", "harm", "choir", "adlib", "ad-lib", "double", "dbl", "stack" })) return Role::backingVocal;
    if (has (n, { "vox", "vocal", "voc", "lead v", "verse", "hook", "chorus", "rap", "sing" })) return Role::leadVocal;
    if (has (n, { "gtr", "guitar", "acoustic" })) return Role::guitar;
    if (has (n, { "piano", "keys", "rhodes", "organ", "wurli", "chord" })) return Role::keys;
    if (has (n, { "pad", "atmos", "ambient" })) return Role::pad;
    if (has (n, { "string", "violin", "cello", "orch" })) return Role::strings;
    if (has (n, { "brass", "horn", "trumpet", "sax", "trombone" })) return Role::brass;
    if (has (n, { "fx", "riser", "sweep", "impact", "noise", "foley" })) return Role::fx;
    if (has (n, { "synth", "lead", "pluck", "arp", "melody", "bell" })) return Role::synth;

    // Nothing in the name: by where its energy is.
    if (t.lowShare > 65.0) return Role::bass;
    if (t.highShare > 55.0) return Role::hats;
    if (t.instrument) return Role::synth;
    return Role::other;
}

double targetDb (const juce::String& genre, Role role)
{
    for (const auto& row : table)
        if (genre.equalsIgnoreCase (row.genre))
            return row.v[(size_t) role];
    return table[0].v[(size_t) role];
}

std::vector<Result> balance (const std::vector<Track>& tracks, const juce::String& genre, double headroomDb)
{
    std::vector<Result> results;
    for (const auto& t : tracks)
    {
        if (t.activeRmsDb <= -90.0)
            continue;   // silent in the stretch measured: left as it is
        Result r;
        r.id = t.id;
        r.name = t.name;
        r.role = guessRole (t);
        r.fromDb = t.faderDb;
        const auto sourceDb = t.activeRmsDb - t.faderDb;    // its own level, before the fader
        const auto wanted = headroomDb + targetDb (genre, r.role);
        r.toDb = wanted - sourceDb;
        results.push_back (r);
    }

    // No fader above 0 dB: if one would have to go higher, everything comes down together (the balance stays),
    // by up to 12 dB; a track that would need more than that just sits at 0 dB (turn the master up if it's quiet).
    double over = 0.0;
    for (const auto& r : results)
        over = juce::jmax (over, r.toDb);
    over = juce::jmin (over, 12.0);
    for (auto& r : results)
        r.toDb = juce::jlimit (-60.0, 0.0, r.toDb - over);
    return results;
}

double targetPan (const juce::String& genre, Role role, int index, int count)
{
    // How wide the genre usually spreads things (hip-hop keeps most of it in the middle; rock and jazz spread the band)
    static const std::map<juce::String, double> widths { { "Hip-Hop", 0.6 }, { "Trap", 0.6 }, { "R&B", 0.8 }, { "Pop", 0.85 },
        { "Rock", 1.0 }, { "Metal", 1.0 }, { "EDM", 0.9 }, { "House", 0.9 }, { "Reggaeton", 0.7 }, { "Afrobeats", 0.8 },
        { "Country", 0.9 }, { "Gospel", 0.85 }, { "Jazz", 1.0 }, { "Lo-Fi", 0.7 }, { "Acoustic", 0.9 } };
    double width = 0.85;
    for (const auto& [g, w] : widths)
        if (genre.equalsIgnoreCase (g))
            width = w;
    const auto side = (index % 2 == 0) ? -1.0 : 1.0;     // the first left, the next right...
    const auto pair = count > 1;
    double pan = 0.0;
    switch (role)
    {
        case Role::kick: case Role::snare: case Role::bass: case Role::leadVocal: case Role::drums: case Role::pad:
            pan = 0.0; break;   // the centre of the mix (a pad or a drum bus is stereo already)
        case Role::hats:         pan = 0.25; break;
        case Role::perc:         pan = side * 0.45; break;
        case Role::backingVocal: pan = pair ? side * 0.55 : 0.3; break;
        case Role::guitar:       pan = pair ? side * (genre == "Rock" || genre == "Metal" ? 0.95 : 0.8) : -0.4; break;
        case Role::keys:         pan = pair ? side * 0.4 : (genre == "Jazz" ? -0.45 : -0.3); break;
        case Role::synth:        pan = pair ? side * 0.35 : 0.2; break;
        case Role::strings:      pan = pair ? side * 0.5 : -0.4; break;
        case Role::brass:        pan = pair ? side * 0.5 : 0.4; break;
        case Role::fx:           pan = side * 0.5; break;
        case Role::other:        pan = pair ? side * 0.25 : 0.0; break;
    }
    return juce::jlimit (-1.0, 1.0, pan * width);
}

std::vector<Insert> recommendedInserts (const juce::String& genre, Role role, const Track& t, int keyRoot, bool minor)
{
    std::vector<Insert> out;
    const auto in = t.activeRmsDb > -90.0 ? t.activeRmsDb - t.faderDb : -18.0;   // its level going into the plugins
    const auto is = [&genre] (std::initializer_list<const char*> names)
    {
        for (const auto* n : names)
            if (genre.equalsIgnoreCase (n))
                return true;
        return false;
    };
    const bool urban = is ({ "Hip-Hop", "Trap", "R&B", "Reggaeton", "Afrobeats" });

    // EQ: cut what each part doesn't need, a touch where it lives
    Insert eq { "channelEq", {}, "EQ" };
    auto band = [&eq] (const char* id, float v) { eq.params.emplace_back (id, v); };
    switch (role)
    {
        case Role::kick:   band ("hpFreq", 30); band ("p1Freq", 350); band ("p1Gain", -3); band ("p3Freq", 3500); band ("p3Gain", 2); eq.why = "EQ: low cut, less box, more click"; break;
        case Role::snare:  band ("hpFreq", 80); band ("p1Freq", 400); band ("p1Gain", -2); band ("hsFreq", 8000); band ("hsGain", 2); eq.why = "EQ: low cut, less ring, more snap"; break;
        case Role::hats:   band ("hpFreq", 250); band ("hsGain", 1.5f); eq.why = "EQ: low cut, a little air"; break;
        case Role::drums:  band ("hpFreq", 30); band ("p1Freq", 400); band ("p1Gain", -2); eq.why = "EQ: low cut, less boxiness"; break;
        case Role::perc:   band ("hpFreq", 150); eq.why = "EQ: low cut"; break;
        case Role::bass:   band ("hpFreq", urban ? 25.0f : 35.0f); band ("p1Freq", 250); band ("p1Gain", -2); eq.why = "EQ: sub cleaned up, less mud"; break;
        case Role::leadVocal: band ("hpFreq", 90); band ("p1Freq", 300); band ("p1Gain", -2); band ("p3Freq", 3500); band ("p3Gain", 1.5f);
                           band ("hsFreq", 10000); band ("hsGain", 2); eq.why = "EQ: low cut, less mud, presence and air"; break;
        case Role::backingVocal: band ("hpFreq", 150); band ("p1Freq", 300); band ("p1Gain", -3); band ("hsGain", 1.5f); eq.why = "EQ: thinned to sit behind the lead"; break;
        case Role::guitar: band ("hpFreq", 90); band ("p1Freq", is ({ "Metal" }) ? 500.0f : 400.0f); band ("p1Gain", is ({ "Metal" }) ? -3.0f : -2.0f); eq.why = "EQ: low cut, less mud"; break;
        case Role::keys:   band ("hpFreq", 80); band ("p1Freq", 300); band ("p1Gain", -1.5f); eq.why = "EQ: low cut, room for the bass and vocal"; break;
        case Role::synth:  band ("hpFreq", 100); eq.why = "EQ: low cut"; break;
        case Role::pad:    band ("hpFreq", 150); band ("p1Freq", 400); band ("p1Gain", -2); eq.why = "EQ: low cut, out of the vocal's way"; break;
        case Role::strings: case Role::brass: band ("hpFreq", 100); eq.why = "EQ: low cut"; break;
        case Role::fx:     band ("hpFreq", 200); eq.why = "EQ: low cut"; break;
        case Role::other:  band ("hpFreq", 60); eq.why = "EQ: low cut"; break;
    }
    if (is ({ "Lo-Fi" }) && (role == Role::keys || role == Role::drums || role == Role::synth))
        band ("lpFreq", 9000);
    out.push_back (eq);

    // Compression: about 3 to 6 dB off the loudest moments, from its own level
    const auto comp = [&] (int model, float ratio, float attack, float release, float above, float makeup, const juce::String& why)
    {
        Insert c { "studioComp", {}, why };
        c.params = { { "model", (float) model }, { "ratio", ratio }, { "attack", attack }, { "release", release },
                     { "threshold", (float) juce::jlimit (-50.0, -3.0, in + above) }, { "makeup", makeup } };
        out.push_back (c);
    };
    switch (role)
    {
        case Role::kick:  comp (2, 4.0f, 20.0f, 80.0f, 6.0f, 2.0f, "Compressor: punch (a fast circuit, the attack let through)"); break;
        case Role::snare: comp (2, 4.0f, 10.0f, 100.0f, 6.0f, 2.0f, "Compressor: snap"); break;
        case Role::drums: comp (is ({ "Rock", "Metal" }) ? 4 : 3, 3.0f, 25.0f, 150.0f, 4.0f, 2.0f, "Compressor: the kit glued together"); break;
        case Role::bass:  comp (6, 4.0f, 15.0f, 200.0f, 2.0f, 3.0f, "Compressor: an even low end (optical)"); break;
        case Role::leadVocal:
            if (urban)                          comp (5, 4.0f, 5.0f, 80.0f, 3.0f, 3.0f, "Compressor: up-front, steady vocal (vintage FET)");
            else if (is ({ "Rock", "Metal" }))  comp (2, 5.0f, 5.0f, 100.0f, 3.0f, 3.0f, "Compressor: an aggressive vocal (FET)");
            else if (is ({ "Jazz", "Acoustic" })) comp (6, 2.5f, 15.0f, 250.0f, 4.0f, 1.5f, "Compressor: gentle (optical)");
            else                                comp (6, 3.0f, 10.0f, 150.0f, 3.0f, 2.5f, "Compressor: a smooth, even vocal (optical)");
            break;
        case Role::backingVocal: comp (6, 3.0f, 10.0f, 150.0f, 3.0f, 2.0f, "Compressor: even backing vocals"); break;
        case Role::guitar: if (is ({ "Rock", "Metal", "Country" })) comp (3, 3.0f, 20.0f, 120.0f, 4.0f, 1.5f, "Compressor: even guitars"); break;
        case Role::brass: comp (6, 2.5f, 15.0f, 200.0f, 4.0f, 1.5f, "Compressor: gentle"); break;
        default: break;
    }

    // De-essing: vocals
    if (role == Role::leadVocal || role == Role::backingVocal)
        out.push_back ({ "deesser", { { "freq", 6500.0f }, { "range", 6.0f } }, "De-esser: softer S's" });

    // Saturation: an 808's harmonics (heard on small speakers); lo-fi drums
    if (role == Role::bass && is ({ "Hip-Hop", "Trap", "Reggaeton", "Afrobeats" }))
        out.push_back ({ "saturator", { { "model", 1.0f }, { "drive", 30.0f }, { "mix", 60.0f } }, "Saturator: the 808 heard on small speakers" });
    if (role == Role::drums && is ({ "Lo-Fi" }))
        out.push_back ({ "saturator", { { "model", 2.0f }, { "drive", 25.0f } }, "Saturator: tape-warm drums" });

    // Tuning: lead vocals in the styles that tune them (hard and fast in trap, natural elsewhere)
    if (role == Role::leadVocal && is ({ "Hip-Hop", "Trap", "R&B", "Pop", "Reggaeton", "Afrobeats", "EDM" }))
    {
        Insert tune { "pitchCorrection", {}, "" };
        const bool hard = is ({ "Trap" });
        tune.params = { { "response", hard ? 1.0f : 40.0f } };
        if (keyRoot >= 0)
        {
            tune.params.emplace_back ("key", (float) (keyRoot % 12));
            tune.params.emplace_back ("scale", minor ? 2.0f : 1.0f);
        }
        else
            tune.params.emplace_back ("scale", 0.0f);   // (no key set: to the nearest note)
        tune.why = juce::String ("Pitch Correction: ") + (hard ? "hard tuning (the trap sound)" : "natural tuning")
                 + (keyRoot >= 0 ? " in the song's key" : ", any note (set the song's key for in-key tuning)");
        out.push_back (tune);
    }
    return out;
}

} // namespace riff::automix
