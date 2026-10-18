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

} // namespace riff::automix
