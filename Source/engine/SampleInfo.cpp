#include "SampleInfo.h"
#include "KeyDetect.h"
#include <cmath>

namespace riff::sampleinfo
{

juce::String nameOf (Category c)
{
    switch (c)
    {
        case Category::drums:      return "Drums";
        case Category::bass:       return "Bass";
        case Category::keys:       return "Keys";
        case Category::synth:      return "Synth";
        case Category::guitar:     return "Guitar";
        case Category::strings:    return "Strings";
        case Category::vocals:     return "Vocals";
        case Category::percussion: return "Perc";
        case Category::fx:         return "FX";
        default:                   return "Other";
    }
}

juce::var Info::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("c", (int) category);
    o->setProperty ("l", loop);
    o->setProperty ("b", bpm);
    o->setProperty ("k", keyRoot);
    o->setProperty ("m", minor);
    o->setProperty ("n", keyIsNote);
    o->setProperty ("s", seconds);
    o->setProperty ("r", bars);
    return juce::var (o);
}

Info Info::fromVar (const juce::var& v)
{
    Info i;
    i.category = (Category) juce::jlimit (0, numCategories - 1, (int) v.getProperty ("c", (int) Category::other));
    i.loop = (bool) v.getProperty ("l", false);
    i.bpm = (double) v.getProperty ("b", 0.0);
    i.keyRoot = (int) v.getProperty ("k", -1);
    i.minor = (bool) v.getProperty ("m", false);
    i.keyIsNote = (bool) v.getProperty ("n", false);
    i.seconds = (double) v.getProperty ("s", 0.0);
    i.bars = (double) v.getProperty ("r", 0.0);
    return i;
}

namespace
{
    juce::StringArray wordsOf (const juce::String& text)
    {
        // "DubBass_90bpm-Amin (wet)" -> dub bass 90bpm amin wet (split at separators and lower-to-upper case changes)
        juce::String spaced;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            if (i > 0 && juce::CharacterFunctions::isUpperCase (c) && juce::CharacterFunctions::isLowerCase (text[i - 1]))
                spaced << ' ';
            spaced << c;
        }
        auto words = juce::StringArray::fromTokens (spaced, " _-.()[]{},+&", "");
        words.removeEmptyStrings();
        return words;
    }

    bool any (const juce::StringArray& lowerWords, const juce::String& lowerAll, std::initializer_list<const char*> whole,
              std::initializer_list<const char*> inside = {})
    {
        for (const auto* w : whole)
            if (lowerWords.contains (w))
                return true;
        for (const auto* w : inside)
            if (lowerAll.contains (w))
                return true;
        return false;
    }

    int noteIndex (juce::juce_wchar letter, const juce::String& accidental)
    {
        static const int naturals[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        const auto l = juce::CharacterFunctions::toUpperCase (letter);
        if (l < 'A' || l > 'G')
            return -1;
        auto n = naturals[l - 'A'];
        if (accidental == "#" || accidental == "sharp") n += 1;
        if (accidental == "b" || accidental == "flat") n -= 1;
        return (n + 12) % 12;
    }

    /** "Amin", "F#m", "Ebmaj", "C" + next word "minor": a key in the words (the original case: "Am" is a key, "am" a word). */
    bool keyInWords (const juce::StringArray& words, int& root, bool& minor, bool& isNote)
    {
        for (int i = words.size(); --i >= 0;)
        {
            const auto w = words[i];
            if (w.isEmpty() || w.length() > 7)
                continue;
            const auto letter = w[0];
            if (! juce::CharacterFunctions::isUpperCase (letter) || noteIndex (letter, {}) < 0)
                continue;
            auto rest = w.substring (1);
            juce::String accidental;
            if (rest.startsWith ("#") || rest.startsWith ("b") && ! rest.startsWithIgnoreCase ("bpm"))
            {
                accidental = rest.substring (0, 1);
                rest = rest.substring (1);
            }
            else if (rest.startsWithIgnoreCase ("sharp") || rest.startsWithIgnoreCase ("flat"))
            {
                accidental = rest.startsWithIgnoreCase ("sharp") ? "sharp" : "flat";
                rest = rest.substring (accidental.length());
            }
            const auto next = i + 1 < words.size() ? words[i + 1].toLowerCase() : juce::String();
            const auto lower = rest.toLowerCase();
            bool found = false;
            if (lower == "m" || lower == "min" || lower == "minor" || lower == "mi")
                found = true, minor = true;
            else if (rest == "M" || lower == "maj" || lower == "major" || lower == "ma")
                found = true, minor = false;
            else if (rest.isEmpty() && (next == "minor" || next == "min" || next == "major" || next == "maj"))
                found = true, minor = next.startsWith ("min");
            else if (rest.isEmpty() && (accidental.isNotEmpty() || (i > 0 && words.size() > 1 && i == words.size() - 1)))
                found = true, minor = false, isNote = true;   // "Sub Bass C", "Vox F#": a note
            if (found)
            {
                root = noteIndex (letter, accidental);
                return root >= 0;
            }
        }
        return false;
    }

    double bpmInWords (const juce::StringArray& words)
    {
        for (int i = 0; i < words.size(); ++i)
        {
            const auto w = words[i].toLowerCase();
            const auto next = i + 1 < words.size() ? words[i + 1].toLowerCase() : juce::String();
            const auto previous = i > 0 ? words[i - 1].toLowerCase() : juce::String();
            auto number = 0.0;
            if (w.endsWith ("bpm") && w.dropLastCharacters (3).containsOnly ("0123456789."))
                number = w.dropLastCharacters (3).getDoubleValue();
            else if (w.containsOnly ("0123456789.") && (next == "bpm" || previous == "bpm"))
                number = w.getDoubleValue();
            if (number >= 40.0 && number <= 240.0)
                return number;
        }
        // a bare number that can only be a tempo (2-3 digits, 60..200: "Groove 92")
        for (const auto& word : words)
            if (word.containsOnly ("0123456789") && word.length() >= 2 && word.length() <= 3)
                if (const auto n = word.getIntValue(); n >= 60 && n <= 200 && n != 808 && ! word.startsWith ("0"))
                    return (double) n;
        return 0.0;
    }
}

Info fromName (const juce::File& file, bool& categoryKnown, bool& loopKnown)
{
    Info info;
    const auto name = file.getFileNameWithoutExtension();
    // (the folder counts too: ".../Drums/Kicks/Deep 03.wav")
    const auto folders = file.getParentDirectory().getFileName() + " " + file.getParentDirectory().getParentDirectory().getFileName();
    const auto words = wordsOf (name);
    const auto lower = (name + " " + folders).toLowerCase();
    juce::StringArray lowerWords;
    for (const auto& w : wordsOf (name + " " + folders))
        lowerWords.add (w.toLowerCase());

    categoryKnown = true;
    // (order matters: "bass drum" is a kick, "808" is a bass, a "vocal fx" is a vocal)
    if (any (lowerWords, lower, { "kick", "kik", "kck", "bd", "snare", "snr", "clap", "clp", "hat", "hats", "hh", "hihat", "hihats", "cymbal", "cymbals",
                                  "crash", "ride", "tom", "toms", "drum", "drums", "rim", "rimshot", "break", "breaks", "breakbeat", "beat", "top", "tops", "groove", "kit" },
             { "bass drum", "kick", "snare", "hihat", "hi-hat", "hi hat", "cymbal", "drumloop", "drum loop", "drums" }))
        info.category = Category::drums;
    else if (any (lowerWords, lower, { "perc", "percs", "percussion", "shaker", "shakers", "conga", "congas", "bongo", "bongos", "tamb", "tambourine", "cowbell",
                                       "clave", "claves", "guiro", "timbale", "timbales", "cabasa", "triangle", "djembe", "woodblock", "agogo", "tabla" }, { "percussion" }))
        info.category = Category::percussion;
    else if (any (lowerWords, lower, { "vox", "vocal", "vocals", "voice", "voices", "acapella", "acappella", "chant", "choir", "adlib", "adlibs", "hook", "verse", "sing", "singer", "rap" },
                  { "vocal", "acapella" }))
        info.category = Category::vocals;
    else if (any (lowerWords, lower, { "bass", "basses", "sub", "808", "808s", "reese", "bassline" }, { "bass" }))
        info.category = Category::bass;
    else if (any (lowerWords, lower, { "fx", "sfx", "riser", "risers", "uplifter", "downlifter", "sweep", "impact", "impacts", "whoosh", "transition", "noise",
                                       "reverse", "rev", "atmos", "atmosphere", "texture", "foley", "ambience", "drone" }, { "riser", "sweep", "impact" }))
        info.category = Category::fx;
    else if (any (lowerWords, lower, { "piano", "pianos", "keys", "key", "rhodes", "organ", "organs", "epiano", "ep", "wurli", "wurlitzer", "clav", "clavinet", "harpsichord", "celesta", "marimba", "vibes", "mallet", "bells" },
                  { "piano", "rhodes", "organ" }))
        info.category = Category::keys;
    else if (any (lowerWords, lower, { "guitar", "guitars", "gtr", "gtrs", "strum", "acoustic", "electric", "banjo", "ukulele", "uke", "mandolin", "sitar", "harp" }, { "guitar" }))
        info.category = Category::guitar;
    else if (any (lowerWords, lower, { "string", "strings", "violin", "violins", "viola", "cello", "cellos", "orchestra", "orchestral", "orch", "brass", "horn", "horns",
                                       "trumpet", "trombone", "sax", "saxophone", "flute", "flutes", "woodwind", "clarinet", "oboe", "pizz", "pizzicato", "staccato", "ensemble" },
                  { "string", "violin", "orchestr" }))
        info.category = Category::strings;
    else if (any (lowerWords, lower, { "synth", "synths", "lead", "leads", "pluck", "plucks", "arp", "arps", "saw", "stab", "stabs", "pad", "pads", "seq", "sequence",
                                       "chord", "chords", "bell", "analog", "fm", "wavetable", "melody", "melodies" }, { "synth" }))
        info.category = Category::synth;
    else
        categoryKnown = false;

    // Loop or one-shot: from the file's own name (a folder called "Loops" often holds everything); a folder of
    // one-shots says so
    loopKnown = true;
    juce::StringArray nameWords;
    for (const auto& w : words)
        nameWords.add (w.toLowerCase());
    const auto lowerName = name.toLowerCase();
    const auto lowerFolders = folders.toLowerCase();
    if (any (nameWords, lowerName, { "loop", "loops", "groove", "break", "breaks", "beat", "bpm", "bar", "bars", "phrase", "riff" }, { "loop", "bpm" }))
        info.loop = true;
    else if (any (nameWords, lowerName, { "oneshot", "shot", "shots", "hit", "hits", "single" }, { "one shot", "one-shot", "oneshot", "one_shot" })
             || lowerFolders.contains ("one shot") || lowerFolders.contains ("oneshot") || lowerFolders.contains ("one-shot") || lowerFolders.contains ("one_shot"))
        info.loop = false;
    else
        loopKnown = false;

    info.bpm = bpmInWords (words);
    if (info.bpm > 0.0)
        info.loop = loopKnown = true;
    keyInWords (words, info.keyRoot, info.minor, info.keyIsNote);
    return info;
}

double tempoOf (const juce::AudioBuffer<float>& audio, double rate)
{
    // Where its hits fall (the rises in loudness, ~10 ms steps) and how they repeat: the beat that fits best, as a
    // whole number of beats over the loop's length (a loop is cut to whole bars).
    const int hopSize = juce::jmax (1, (int) (rate * 0.01));
    const int steps = audio.getNumSamples() / hopSize;
    if (steps < 64)
        return 0.0;
    std::vector<float> energy ((size_t) steps), rise ((size_t) steps, 0.0f);
    for (int s = 0; s < steps; ++s)
    {
        double e = 0.0;
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            const auto* x = audio.getReadPointer (c) + s * hopSize;
            for (int i = 0; i < hopSize; ++i)
                e += x[i] * x[i];
        }
        energy[(size_t) s] = (float) std::log1p (1000.0 * e / hopSize);
    }
    for (int s = 1; s < steps; ++s)
        rise[(size_t) s] = juce::jmax (0.0f, energy[(size_t) s] - energy[(size_t) s - 1]);
    float mean = 0.0f;
    for (const auto r : rise) mean += r;
    mean /= (float) steps;
    for (auto& r : rise) r = juce::jmax (0.0f, r - mean);

    const auto seconds = audio.getNumSamples() / rate;
    const auto scoreOf = [&] (double bpm)
    {
        // how well a pulse at this tempo lines up with the hits (and its half-beats a little)
        const auto period = 60.0 / bpm / 0.01;
        double best = 0.0;
        for (int phaseStep = 0; phaseStep < 8; ++phaseStep)
        {
            const auto phase = period * phaseStep / 8.0;
            double score = 0.0;
            for (double t = phase; t < steps - 1; t += period)
            {
                const auto i = (int) std::lround (t);
                score += juce::jmax (rise[(size_t) juce::jlimit (0, steps - 1, i)], rise[(size_t) juce::jlimit (0, steps - 1, i + 1)]);
                const auto half = (int) std::lround (t + period * 0.5);
                if (half < steps)
                    score += 0.5 * rise[(size_t) half];
            }
            best = juce::jmax (best, score / juce::jmax (1.0, steps / period));
        }
        return best;
    };

    // Candidates: the loop is a whole number of beats (4, 8, 16... or 2, 6...) at a tempo between 60 and 200
    double bestBpm = 0.0, bestScore = 0.0;
    for (int beats = 1; beats <= 256; ++beats)
    {
        const auto bpm = beats * 60.0 / seconds;
        if (bpm < 60.0 || bpm > 200.0)
            continue;
        auto score = scoreOf (bpm);
        if (beats % 4 == 0) score *= 1.15;        // whole bars, most loops are
        if (bpm >= 80.0 && bpm <= 160.0) score *= 1.1;
        if (score > bestScore)
            bestScore = score, bestBpm = bpm;
    }
    // (the same pulse at twice or half the speed: the one in the usual range, where the other beats land too)
    if (bestBpm > 0.0 && bestBpm < 85.0 && bestBpm * 2.0 <= 200.0 && scoreOf (bestBpm * 2.0) >= 0.5 * bestScore)
        bestBpm *= 2.0;
    else if (bestBpm > 170.0 && scoreOf (bestBpm * 0.5) >= 0.8 * bestScore)
        bestBpm *= 0.5;
    // (a loop with no clear hits: no tempo)
    double total = 0.0;
    for (const auto r : rise) total += r;
    if (total <= 1.0e-6 || bestScore < 1.5 * total / steps)
        return 0.0;
    return std::round (bestBpm * 100.0) / 100.0;
}

std::pair<int, bool> keyOf (const juce::AudioBuffer<float>& audio, double rate, bool& isNote, float& confidence)
{
    // Which notes sound most (the chroma), against the major and minor key profiles (keydetect: as the song's key
    // is found). One note far above the rest (with its fifth): a single note.
    const auto chroma = keydetect::chromaOf (audio, rate);
    double total = 0.0;
    for (const auto c : chroma) total += c;
    isNote = false;
    confidence = 0.0f;
    if (total <= 1.0e-12)
        return { -1, false };
    int top = 0;
    for (int i = 1; i < 12; ++i)
        if (chroma[(size_t) i] > chroma[(size_t) top]) top = i;
    double rest = 0.0;
    for (int i = 0; i < 12; ++i)
        if (i != top && i != (top + 7) % 12) rest += chroma[(size_t) i];
    if (chroma[(size_t) top] > 0.5 * total && chroma[(size_t) top] > 2.5 * rest / 10.0)
    {
        isNote = true;
        confidence = (float) (chroma[(size_t) top] / total);
        return { top, false };
    }
    const auto result = keydetect::detect (chroma);
    if (! result.found)
        return { -1, false };
    confidence = (float) juce::jlimit (0.0, 1.0, result.score);
    return { result.root, result.minor };
}

Info analyse (const juce::File& file, juce::AudioFormatManager& formats)
{
    bool categoryKnown = false, loopKnown = false;
    auto info = fromName (file, categoryKnown, loopKnown);

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return info;
    const auto rate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    info.seconds = (double) reader->lengthInSamples / rate;
    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (rate * 60.0));
    if (length < 64)
        return info;
    juce::AudioBuffer<float> audio ((int) juce::jlimit (1u, 2u, reader->numChannels), length);
    reader->read (&audio, 0, length, 0, true, audio.getNumChannels() > 1);

    // How it sounds: how much of it is hits (vs held), how low it sits
    double lowE = 0.0, allE = 0.0;
    {
        juce::IIRFilter low;
        low.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, 180.0));
        auto copy = std::vector<float> (audio.getReadPointer (0), audio.getReadPointer (0) + length);
        for (auto& x : copy)
        {
            allE += x * x;
            const auto y = low.processSingleSampleRaw (x);
            lowE += y * y;
        }
    }
    const auto lowShare = allE > 0.0 ? lowE / allE : 0.0;

    if (! loopKnown)
        info.loop = info.seconds >= 1.6;   // (short: a hit; longer: most likely a loop)
    if (info.loop && info.bpm <= 0.0)
        info.bpm = tempoOf (audio, rate);
    if (info.loop && info.bpm > 0.0)
        info.bars = info.seconds * info.bpm / 60.0 / 4.0;

    if (! categoryKnown)
    {
        // (only what can be told for sure: a low held sound is a bass; hits with no pitch are drums)
        bool note = false;
        float confidence = 0.0f;
        keyOf (audio, rate, note, confidence);
        if (lowShare > 0.6 && confidence > 0.5f)
            info.category = Category::bass;
        else if (confidence < 0.45f && (info.seconds < 1.0 || info.bpm > 0.0))
            info.category = Category::drums;
    }

    const bool pitched = ! (info.category == Category::drums || info.category == Category::percussion || info.category == Category::fx);
    if (pitched && info.keyRoot < 0)
    {
        bool note = false;
        float confidence = 0.0f;
        const auto [root, minor] = keyOf (audio, rate, note, confidence);
        if (root >= 0 && confidence > (note ? 0.5f : 0.55f))
        {
            info.keyRoot = root;
            info.minor = minor;
            info.keyIsNote = note || ! info.loop;
        }
    }
    if (! pitched)
        info.keyRoot = -1;
    return info;
}

juce::String keyName (const Info& info)
{
    if (info.keyRoot < 0)
        return {};
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    juce::String s (names[info.keyRoot % 12]);
    if (! info.keyIsNote)
        s << (info.minor ? " min" : " maj");
    return s;
}

int sourceRootFor (int root, bool minor, bool songMinor)
{
    // a minor sample in a major song: as its relative major (A minor -> C); a major one in a minor song: as its
    // relative minor (C major -> A)
    if (minor && ! songMinor) return (root + 3) % 12;
    if (! minor && songMinor) return (root + 9) % 12;
    return root;
}

int shiftToKey (int root, bool minor, int songRoot, bool songMinor)
{
    if (root < 0 || songRoot < 0)
        return 0;
    auto shift = ((songRoot - sourceRootFor (root, minor, songMinor)) % 12 + 12) % 12;
    if (shift > 6) shift -= 12;   // the nearest way: up to 6 up, 5 down
    return shift;
}
}
