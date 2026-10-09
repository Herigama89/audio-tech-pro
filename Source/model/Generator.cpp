#include "Generator.h"
#include "Project.h"

namespace riff::generator
{

namespace
{
    /**
        How a genre plays. Drum patterns are one bar of 16 steps:
            x hit   o ghost (soft)   ? maybe (more often with more variation)   . rest
        Chord and bass rhythms are 16 steps too:
            x / r start a note (bass: r root, f fifth, o octave)   - hold   . rest
        A bass rhythm of "kick" follows the kick drum (808 style).
    */
    struct Style
    {
        const char* kick;
        const char* snare;
        const char* clap;
        const char* hats;
        const char* openHat;
        const char* extra;        // a fifth drum line
        int extraNote;
        bool hatRolls;            // trap-style hi-hat rolls
        bool crashes;             // crash on the first bar of each section
        bool tomFills;            // fills on toms (else snare)
        std::vector<std::vector<int>> progressions;   // scale degrees, 4 chords, one per bar
        bool sevenths;
        const char* chordRhythm;
        const char* bassRhythm;
        float melodyDensity;
        int melodyLow, melodyHigh;   // MIDI range
    };

    using namespace gm;

    const std::vector<Genre>& genreList()
    {
        static const std::vector<Genre> list {
            { "Hip-Hop",   90.0,  0.55f, true,  "Boom bap: swung drums, dusty seventh chords, a walking bass" },
            { "Trap",      140.0, 0.0f,  true,  "Half-time snare, rolling hi-hats, an 808 bass that follows the kick" },
            { "Drill",     142.0, 0.0f,  true,  "Sliding 808s, off-kilter snares, triplet hi-hats, dark minor chords" },
            { "Pop",       110.0, 0.0f,  false, "Big, simple: four-chord progression, steady drums, a catchy top line" },
            { "R&B",       75.0,  0.35f, false, "Laid-back groove, rich seventh chords, ghost notes on the rim" },
            { "Lo-fi",     80.0,  0.6f,  false, "Lazy swing, jazzy chords, soft drums with space" },
            { "House",     124.0, 0.1f,  true,  "Four on the floor, offbeat open hats, piano stabs, offbeat bass" },
            { "Techno",    130.0, 0.0f,  true,  "Driving kick, hypnotic stabs, rolling 16th bass" },
            { "Afrobeats", 105.0, 0.2f,  false, "Bouncy 3-3-2 rim pattern, shakers, sunny syncopated chords" },
            { "Reggaeton", 95.0,  0.0f,  true,  "The dembow rhythm, punchy stabs and bass on the same groove" },
            { "Rock",      120.0, 0.0f,  false, "Straight eighths, backbeat snare, crash at the top, strummed chords" },
        };
        return list;
    }

    const Style& styleFor (int genre)
    {
        static const std::vector<Style> styles {
            // Hip-Hop
            { "x.....x...x...?.", "....x.......x...", "................", "x.x.x.x.x.x.x.?o", "................",
              "................", rim, false, false, false,
              { { 0, 3, 0, 4 }, { 0, 5, 3, 4 }, { 1, 4, 0, 0 } }, true,
              "x-------x-------", "r-----r-..f-r---", 0.35f, 67, 84 },
            // Trap
            { "x......x..x...?.", "................", "........x.......", "xxxxxxxxxxxxxxxx", "..............?.",
              "................", rim, true, false, false,
              { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 0, 5, 6 } }, false,
              "x---------------", "kick", 0.25f, 72, 91 },
            // Drill
            { "x.....x...?.x...", "........x.....x.", "................", "x..x..x.x..x..x.", "................",
              "................", rim, true, false, false,
              { { 0, 5, 3, 4 }, { 0, 6, 5, 4 }, { 0, 1, 5, 4 } }, false,
              "x---------------", "kick", 0.3f, 72, 88 },
            // Pop
            { "x.......x.?.....", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "................",
              "................", rim, false, true, true,
              { { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 5, 3, 4 } }, false,
              "x-------x-------", "r---r---r---r-f-", 0.5f, 67, 84 },
            // R&B
            { "x......x..x...?.", "....x.......x...", "................", "xoxoxoxoxoxoxoxo", "................",
              ".......o.......o", rim, false, false, false,
              { { 1, 4, 0, 0 }, { 0, 3, 1, 4 }, { 3, 4, 2, 5 } }, true,
              "x-------x-------", "r-----r-..f-r---", 0.35f, 65, 81 },
            // Lo-fi
            { "x.......?.x.....", "....x.......x...", "................", "x.x.x?x.x.x.x?x.", "................",
              "................", rim, false, false, false,
              { { 1, 4, 0, 5 }, { 3, 2, 1, 0 }, { 0, 5, 1, 4 } }, true,
              "x---------------", "r-------r-----f-", 0.3f, 67, 84 },
            // House
            { "x...x...x...x...", "................", "....x.......x...", "oxoxoxoxoxoxoxox", "..x...x...x...x.",
              "................", rim, false, true, false,
              { { 0, 5, 2, 6 }, { 0, 3, 4, 3 }, { 0, 6, 5, 6 } }, true,
              "..x-..x-..x-..x-", "..r-..r-..r-..r-", 0.3f, 67, 84 },
            // Techno
            { "x...x...x...x...", "................", "....?.......x...", "oooooooooooooooo", "..x...x...x...x.",
              "......x.......?.", rim, false, false, false,
              { { 0, 0, 5, 6 }, { 0, 0, 0, 5 }, { 0, 3, 0, 6 } }, false,
              "x-.....x-.......", "..rr..rr..rr..rr", 0.2f, 67, 84 },
            // Afrobeats
            { "x.....x...x.....", "................", "................", ".o.o.o.o.o.o.o.o", "................",
              "...x..x....x..x.", rim, false, false, false,
              { { 0, 3, 5, 4 }, { 5, 3, 0, 4 }, { 0, 4, 5, 3 } }, false,
              "x--x--x-x--x--x-", "r--r--r-..r-f-..", 0.4f, 67, 84 },
            // Reggaeton
            { "x...x...x...x...", "...x..x....x..x.", "................", "x.x.x.x.x.x.x.x.", "................",
              "................", rim, false, false, false,
              { { 0, 5, 2, 6 }, { 0, 6, 5, 6 }, { 0, 3, 6, 2 } }, false,
              "x--x--x-x--x--x-", "r--r--r-r--r--r-", 0.4f, 67, 84 },
            // Rock
            { "x.....x.x.......", "....x.......x...", "................", "x.x.x.x.x.x.x.x.", "................",
              "................", rim, false, true, true,
              { { 0, 4, 5, 3 }, { 0, 3, 4, 4 }, { 0, 3, 0, 4 } }, false,
              "x-x-x-x-x-x-x-x-", "r-r-r-r-r-r-r-r-", 0.4f, 64, 81 },
        };
        return styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, genre)];
    }

    constexpr int stepsPerBar = 16;
    constexpr double stepBeats = 0.25;

    /** Where a 16th step lands, with swing pushing the off-beat 16ths late. */
    double stepTime (int bar, int step, float swing)
    {
        auto t = bar * 4.0 + step * stepBeats;
        if (step % 2 == 1)
            t += swing * stepBeats / 3.0;
        return t;
    }

    int humanise (juce::Random& r, int velocity, int amount = 8)
    {
        return juce::jlimit (1, 127, velocity + r.nextInt ({ -amount, amount + 1 }));
    }

    /** The chord's notes (as MIDI pitches near middle C) for a scale degree. */
    std::vector<int> chordTones (const std::vector<int>& scale, int root, int degree, bool seventh)
    {
        std::vector<int> notes;
        const auto count = seventh ? 4 : 3;
        for (int k = 0; k < count; ++k)
        {
            const auto index = degree + 2 * k;
            const auto pitchClass = scale[(size_t) (index % 7)];
            notes.push_back (48 + pitchClass + 12 * (index / 7) + (pitchClass < root ? 12 : 0));
        }
        return notes;
    }

    /** Picks the inversion and octave of a chord that moves least from the last one. */
    std::vector<int> voiceLead (std::vector<int> chord, const std::vector<int>& previous)
    {
        std::vector<std::vector<int>> candidates;
        for (int inversion = 0; inversion < (int) chord.size(); ++inversion)
        {
            auto v = chord;
            for (int i = 0; i < inversion; ++i)
                v[(size_t) i] += 12;
            std::sort (v.begin(), v.end());
            for (int shift : { -12, 0, 12 })
            {
                auto s = v;
                for (auto& n : s)
                    n += shift;
                if (s.front() >= 52 && s.back() <= 79)
                    candidates.push_back (s);
            }
        }

        if (candidates.empty())
            return chord;

        auto cost = [&previous] (const std::vector<int>& v)
        {
            if (previous.empty())
                return std::abs ((v.front() + v.back()) / 2 - 62);   // the first chord sits around D4
            int c = 0;
            for (size_t i = 0; i < v.size(); ++i)
                c += std::abs (v[i] - previous[juce::jmin (i, previous.size() - 1)]);
            return c;
        };

        return *std::min_element (candidates.begin(), candidates.end(),
                                  [&] (const auto& a, const auto& b) { return cost (a) < cost (b); });
    }

    /** A bar's drum line from its pattern, with the '?' hits decided. */
    std::vector<int> drumSteps (const char* pattern, juce::Random& r, float variation, std::vector<int>& ghosts)
    {
        std::vector<int> hits;
        for (int s = 0; s < stepsPerBar && pattern[s] != 0; ++s)
        {
            const auto c = pattern[s];
            if (c == 'x')
                hits.push_back (s);
            else if (c == 'o')
                ghosts.push_back (s);
            else if (c == '?' && r.nextFloat() < 0.2f + 0.6f * variation)
                hits.push_back (s);
        }
        return hits;
    }

    struct RhythmNote { int step; int length; char symbol; };

    /** Note starts and lengths (in steps) from a rhythm string. */
    std::vector<RhythmNote> parseRhythm (const char* pattern)
    {
        std::vector<RhythmNote> notes;
        for (int s = 0; s < stepsPerBar && pattern[s] != 0; ++s)
        {
            const auto c = pattern[s];
            if (c == '-' || c == '.')
                continue;
            int length = 1;
            while (s + length < stepsPerBar && pattern[s + length] == '-')
                ++length;
            notes.push_back ({ s, length, c });
        }
        return notes;
    }

    // --- Parts ------------------------------------------------------------------

    Part makeDrums (const Settings& settings, const Genre& genre, const Style& style, std::vector<std::vector<int>>& kicksPerBar)
    {
        Part part { PartKind::drums, "Drums", {} };
        juce::Random r ((juce::int64) settings.seed * 7919 + 1);
        const auto v = settings.variation;

        auto add = [&] (int note, double start, int velocity, double length = 0.2)
        {
            part.notes.push_back ({ note, start, length, juce::jlimit (1, 127, velocity) });
        };

        for (int bar = 0; bar < settings.bars; ++bar)
        {
            const bool fillBar = bar % 4 == 3 && r.nextFloat() < 0.15f + 0.7f * v;
            const int fillFrom = fillBar ? (r.nextBool() ? 12 : 14) : stepsPerBar;

            std::vector<int> ghosts;
            auto kicks = drumSteps (style.kick, r, v, ghosts);
            kicksPerBar.push_back (kicks);
            for (auto s : kicks)
                add (kick, stepTime (bar, s, genre.swing), humanise (r, 112, 6));

            for (auto line : { std::pair { style.snare, snare }, std::pair { style.clap, clap } })
            {
                std::vector<int> soft;
                for (auto s : drumSteps (line.first, r, v, soft))
                    if (s < fillFrom)
                        add (line.second, stepTime (bar, s, genre.swing), humanise (r, 104, 6));
                for (auto s : soft)
                    add (line.second, stepTime (bar, s, genre.swing), humanise (r, 48));
            }

            // Hi-hats: accents on the beat, softer in between, and rolls in trap and drill.
            {
                std::vector<int> soft;
                auto hats = drumSteps (style.hats, r, v, soft);
                for (auto s : soft)
                    hats.push_back (-1 - s);   // marked soft

                for (auto marked : hats)
                {
                    const bool isSoft = marked < 0;
                    const auto s = isSoft ? -1 - marked : marked;
                    if (s >= fillFrom)
                        continue;

                    const auto base = isSoft ? 55 : (s % 4 == 0 ? 92 : 74);

                    if (style.hatRolls && s % 2 == 0 && s >= 8 && r.nextFloat() < 0.08f + 0.25f * v)
                    {
                        // A roll: three or four quick hits in the space of a 16th or an 8th.
                        const auto hitsInRoll = r.nextBool() ? 3 : 4;
                        const auto span = r.nextBool() ? stepBeats : stepBeats * 2.0;
                        for (int i = 0; i < hitsInRoll; ++i)
                            add (closedHat, bar * 4.0 + s * stepBeats + span * i / hitsInRoll, 60 + i * 12, span / hitsInRoll);
                        continue;
                    }

                    add (closedHat, stepTime (bar, s, genre.swing), humanise (r, base));
                }
            }

            std::vector<int> unused;
            for (auto s : drumSteps (style.openHat, r, v, unused))
                if (s < fillFrom)
                    add (openHat, stepTime (bar, s, genre.swing), humanise (r, 84), 0.4);

            {
                std::vector<int> soft;
                for (auto s : drumSteps (style.extra, r, v, soft))
                    add (style.extraNote, stepTime (bar, s, genre.swing), humanise (r, 96));
                for (auto s : soft)
                    add (style.extraNote, stepTime (bar, s, genre.swing), humanise (r, 52));
            }

            if (style.crashes && bar % 8 == 0)
                add (crash, bar * 4.0, 108, 1.5);

            // A fill into the next section
            if (fillBar)
            {
                for (int s = fillFrom; s < stepsPerBar; ++s)
                {
                    const auto note = style.tomFills ? (s < 14 ? highTom : lowTom) : snare;
                    add (note, stepTime (bar, s, genre.swing), 80 + (s - fillFrom) * 10);
                }
            }
        }

        return part;
    }

    Part makeChords (const Settings& settings, const Style& style, const std::vector<int>& scale, const std::vector<int>& degrees)
    {
        Part part { PartKind::chords, "Chords", {} };
        juce::Random r ((juce::int64) settings.seed * 104729 + 3);
        const auto rhythm = parseRhythm (style.chordRhythm);
        std::vector<int> previous;

        for (int bar = 0; bar < settings.bars; ++bar)
        {
            const auto degree = degrees[(size_t) (bar % (int) degrees.size())];
            auto voicing = voiceLead (chordTones (scale, settings.root, degree, style.sevenths), previous);
            previous = voicing;

            for (const auto& n : rhythm)
                for (auto pitch : voicing)
                    part.notes.push_back ({ pitch, bar * 4.0 + n.step * stepBeats,
                                            n.length * stepBeats * 0.95, humanise (r, n.step % 4 == 0 ? 88 : 76, 6) });
        }

        return part;
    }

    Part makeBass (const Settings& settings, const Style& style, const std::vector<int>& scale,
                   const std::vector<int>& degrees, const std::vector<std::vector<int>>& kicksPerBar, const Genre& genre)
    {
        Part part { PartKind::bass, "Bass", {} };
        juce::Random r ((juce::int64) settings.seed * 15485863 + 5);

        auto bassPitch = [&] (int degree, char symbol)
        {
            auto pitchClass = scale[(size_t) (degree % 7)];
            if (symbol == 'f')
                pitchClass = scale[(size_t) ((degree + 4) % 7)];
            auto pitch = 36 + pitchClass;          // C2 and up
            if (pitch > 43)
                pitch -= 12;                       // keep it low: G1..G2
            if (symbol == 'o')
                pitch += 12;
            return pitch;
        };

        const bool followKick = juce::String (style.bassRhythm) == "kick";
        const auto rhythm = followKick ? std::vector<RhythmNote>() : parseRhythm (style.bassRhythm);

        for (int bar = 0; bar < settings.bars; ++bar)
        {
            const auto degree = degrees[(size_t) (bar % (int) degrees.size())];

            if (followKick)
            {
                // 808: a long note on every kick, held to the next one.
                const auto& kicks = kicksPerBar[(size_t) bar];
                for (size_t i = 0; i < kicks.size(); ++i)
                {
                    const auto next = i + 1 < kicks.size() ? kicks[i + 1] : stepsPerBar;
                    const bool jump = i + 1 == kicks.size() && kicks.size() > 1 && r.nextFloat() < 0.15f + 0.5f * settings.variation;
                    part.notes.push_back ({ bassPitch (degree, jump ? 'o' : 'r'), stepTime (bar, kicks[i], genre.swing),
                                            (next - kicks[i]) * stepBeats * 0.95, humanise (r, 110, 6) });
                }
                continue;
            }

            for (const auto& n : rhythm)
            {
                auto symbol = n.symbol;
                if (symbol == 'r' && n.step > 0 && r.nextFloat() < 0.1f * settings.variation)
                    symbol = 'f';   // the odd fifth for movement
                part.notes.push_back ({ bassPitch (degree, symbol), stepTime (bar, n.step, genre.swing),
                                        n.length * stepBeats * 0.9, humanise (r, n.step % 4 == 0 ? 104 : 92, 6) });
            }
        }

        return part;
    }

    Part makeMelody (const Settings& settings, const Style& style, const std::vector<int>& scale,
                     const std::vector<int>& degrees, const Genre& genre)
    {
        Part part { PartKind::melody, "Melody", {} };
        juce::Random r ((juce::int64) settings.seed * 32452843 + 7);

        // The notes it may use: the key's pentatonic scale, in the lead's range.
        const std::vector<int> pentatonic = settings.minor ? std::vector<int> { 0, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9 };
        std::vector<int> pool;
        for (int p = style.melodyLow; p <= style.melodyHigh; ++p)
            for (auto i : pentatonic)
                if ((p - settings.root + 120) % 12 == i)
                    pool.push_back (p);

        // A rhythm for most bars (A) and an answering one (B) for the last bar of each four.
        auto makeRhythm = [&] (bool ending)
        {
            std::vector<int> starts;
            for (int s = 0; s < stepsPerBar; s += 2)
            {
                auto chance = style.melodyDensity * (s % 4 == 0 ? 1.4f : 0.8f);
                if (s == 0)
                    chance = 0.75f;
                if (ending && s >= 10)
                    chance *= 0.4f;    // breathe at the end of the phrase
                if (r.nextFloat() < chance)
                    starts.push_back (s);
                else if (r.nextFloat() < style.melodyDensity * 0.2f * (1.0f + settings.variation))
                    starts.push_back (s + 1);   // a syncopated 16th
            }
            if (starts.empty())
                starts.push_back (0);
            return starts;
        };

        const auto rhythmA = makeRhythm (false), rhythmB = makeRhythm (true);
        auto index = (int) pool.size() / 2;

        for (int bar = 0; bar < settings.bars; ++bar)
        {
            const auto degree = degrees[(size_t) (bar % (int) degrees.size())];
            const auto chord = chordTones (scale, settings.root, degree, false);
            const auto& rhythm = (bar % 4 == 3 || (settings.variation > 0.7f && r.nextFloat() < 0.2f)) ? rhythmB : rhythmA;

            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const auto s = rhythm[i];
                const auto next = i + 1 < rhythm.size() ? rhythm[i + 1] : stepsPerBar;

                // Wander by small steps; land on a chord tone on the strong beats.
                const auto move = std::array<int, 7> { -2, -1, -1, 0, 1, 1, 2 }[(size_t) r.nextInt (7)];
                index = juce::jlimit (0, (int) pool.size() - 1, index + move);

                if (s % 4 == 0)
                {
                    int best = index, bestDistance = 99;
                    for (int j = 0; j < (int) pool.size(); ++j)
                        for (auto tone : chord)
                            if ((pool[(size_t) j] - tone) % 12 == 0 && std::abs (j - index) < bestDistance)
                            {
                                best = j;
                                bestDistance = std::abs (j - index);
                            }
                    if (bestDistance <= 2)
                        index = best;
                }

                const auto length = juce::jmin ((next - s) * stepBeats, 1.5) * (r.nextFloat() < 0.3f ? 0.5 : 0.9);
                part.notes.push_back ({ pool[(size_t) index], stepTime (bar, s, genre.swing), length, humanise (r, s % 4 == 0 ? 98 : 86, 6) });
            }
        }

        return part;
    }
}

//==============================================================================
const std::vector<Genre>& genres()
{
    return genreList();
}

std::vector<int> scaleOf (int root, bool minor)
{
    const std::vector<int> major { 0, 2, 4, 5, 7, 9, 11 }, naturalMinor { 0, 2, 3, 5, 7, 8, 10 };
    std::vector<int> scale;
    for (auto i : minor ? naturalMinor : major)
        scale.push_back ((root + i) % 12);
    return scale;
}

juce::String keyName (int root, bool minor)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[(root % 12 + 12) % 12]) + (minor ? " minor" : " major");
}

Result generate (const Settings& settingsIn)
{
    auto settings = settingsIn;
    settings.genre = juce::jlimit (0, (int) genres().size() - 1, settings.genre);
    settings.bars = juce::jlimit (1, 64, settings.bars);
    settings.root = (settings.root % 12 + 12) % 12;
    settings.variation = juce::jlimit (0.0f, 1.0f, settings.variation);

    const auto& genre = genres()[(size_t) settings.genre];
    const auto& style = styleFor (settings.genre);
    const auto scale = scaleOf (settings.root, settings.minor);

    juce::Random r ((juce::int64) settings.seed);
    const auto& degrees = style.progressions[(size_t) r.nextInt ((int) style.progressions.size())];

    Result result;
    result.tempo = genre.tempo;

    // Drums come first so the 808 can follow the kick.
    std::vector<std::vector<int>> kicksPerBar;
    auto drums = makeDrums (settings, genre, style, kicksPerBar);

    if (settings.drums)  result.parts.push_back (std::move (drums));
    if (settings.bass)   result.parts.push_back (makeBass (settings, style, scale, degrees, kicksPerBar, genre));
    if (settings.chords) result.parts.push_back (makeChords (settings, style, scale, degrees));
    if (settings.melody) result.parts.push_back (makeMelody (settings, style, scale, degrees, genre));

    // Keep every note inside the part.
    const auto end = settings.bars * 4.0;
    for (auto& part : result.parts)
    {
        part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(), [end] (const Note& n) { return n.start >= end; }),
                          part.notes.end());
        for (auto& n : part.notes)
            n.length = juce::jlimit (0.03, end - n.start, n.length);
        std::sort (part.notes.begin(), part.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    }

    return result;
}

//==============================================================================
juce::Array<juce::ValueTree> apply (juce::ValueTree project, const Result& result, const Settings& settings,
                                    double startSeconds, bool setTempo, juce::UndoManager* um)
{
    if (setTempo)
        model::setTempo (project, result.tempo, um);

    const auto genreName = juce::String (genres()[(size_t) juce::jlimit (0, (int) genres().size() - 1, settings.genre)].name);
    const auto secondsPerBeat = model::secondsPerBeat (project);
    const auto bassIs808 = juce::String (styleFor (settings.genre).bassRhythm) == "kick";
    juce::Array<juce::ValueTree> tracks;

    for (const auto& part : result.parts)
    {
        auto track = model::addTrack (project, genreName + " " + part.name, um, -1, model::instrumentKind);

        // A starting sound on the built-in synth, until you choose your own instrument.
        auto sound = [&] (int wave, double cutoff, double attack, double decay, double sustain, double release, double gain)
        {
            track.setProperty (ids::synthWave, wave, um);
            track.setProperty (ids::synthCutoff, cutoff, um);
            track.setProperty (ids::synthAttack, attack, um);
            track.setProperty (ids::synthDecay, decay, um);
            track.setProperty (ids::synthSustain, sustain, um);
            track.setProperty (ids::synthRelease, release, um);
            track.setProperty (ids::synthGain, gain, um);
        };

        switch (part.kind)
        {
            case PartKind::drums:  track.setProperty (ids::instrument, "drums", um);   // the built-in Drum Kit plays GM drums
                                   track.setProperty (ids::synthGain, 0.8, um); break;
            case PartKind::bass:   bassIs808 ? sound (3, 2500.0, 0.002, 1.2, 0.7, 0.25, 0.7)
                                             : sound (0, 700.0, 0.003, 0.3, 0.6, 0.12, 0.55); break;
            case PartKind::chords: sound (0, 1600.0, 0.06, 0.6, 0.7, 0.7, 0.3); break;
            case PartKind::melody: sound (2, 5000.0, 0.004, 0.25, 0.5, 0.2, 0.45); break;
        }

        auto region = model::addMidiRegion (track, startSeconds, settings.bars * 4.0 * secondsPerBeat, um);
        region.setProperty (ids::name, genreName + " " + part.name, um);

        // Notes go in without their own undo steps: undoing removes the region with them.
        for (const auto& n : part.notes)
            model::addNote (region, n.pitch, n.start * secondsPerBeat, n.length * secondsPerBeat, n.velocity, nullptr);

        tracks.add (track);
    }

    return tracks;
}

} // namespace riff::generator
