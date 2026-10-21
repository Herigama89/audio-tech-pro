#include "EffectFace.h"
#include "../engine/StockDsp.h"

namespace riff::effectface
{

juce::Colour accentFor (const juce::String& category)
{
    if (category == "EQ")              return juce::Colour (0xff4fa3ff);
    if (category == "Dynamics")        return juce::Colour (0xffff9f3a);
    if (category == "Delay")           return juce::Colour (0xff3ad1c0);
    if (category == "Reverb")          return juce::Colour (0xff9b7bff);
    if (category == "Modulation")      return juce::Colour (0xffff6fb5);
    if (category == "Distortion")      return juce::Colour (0xffff5a4a);
    if (category == "Filter")          return juce::Colour (0xffc6e04a);
    if (category == "Amps and Pedals") return juce::Colour (0xffe0b040);
    if (category == "Pitch")           return juce::Colour (0xff52e08a);
    if (category == "Imaging")         return juce::Colour (0xff5fd0ff);
    return juce::Colour (0xff9fb3c8);
}

//==============================================================================
std::vector<std::pair<Section, std::vector<int>>> resolveSections (const EffectInfo& info)
{
    std::vector<std::pair<Section, std::vector<int>>> out;
    std::vector<bool> used (info.params.size(), false);
    int next = 0;
    for (const auto& section : sectionsFor (info))
    {
        std::vector<int> indices;
        if (! section.ids.empty())
        {
            for (const auto& id : section.ids)
                for (size_t i = 0; i < info.params.size(); ++i)
                    if (info.params[i].id == id && ! used[i]) { indices.push_back ((int) i); used[i] = true; }
        }
        else
            for (int k = 0; k < section.count; ++k)
            {
                while (next < (int) info.params.size() && used[(size_t) next]) ++next;
                if (next >= (int) info.params.size()) break;
                indices.push_back (next);
                used[(size_t) next++] = true;
            }
        if (! indices.empty())
            out.push_back ({ section, indices });
    }
    std::vector<int> rest;
    for (size_t i = 0; i < used.size(); ++i)
        if (! used[i]) rest.push_back ((int) i);
    if (! rest.empty())
        out.push_back ({ Section { "More", (int) rest.size() }, rest });
    return out;
}

std::vector<Section> sectionsFor (const EffectInfo& info)
{
    // EQs laid out by band (each band's knobs stacked)
    if (! info.columns.empty())
    {
        static const std::map<juce::String, std::vector<juce::String>> bandNames {
            { "channelEq", { "Low Cut", "Low Shelf", "Low Mid", "Mid", "High Mid", "High Shelf", "High Cut", "Output" } },
            { "linearEq", { "Low Cut", "Low Shelf", "Low Mid", "Mid", "High Mid", "High Shelf", "High Cut", "Output" } },
            { "consoleEq", { "Filter", "Low", "Mid", "High", "Output" } },
            { "tubeEq", { "Low", "High Boost", "High Atten", "Output" } } };
        std::vector<Section> s;
        const auto names = bandNames.count (info.type) > 0 ? bandNames.at (info.type) : std::vector<juce::String>();
        for (size_t c = 0; c < info.columns.size(); ++c)
            s.push_back ({ c < names.size() ? names[c] : juce::String(), info.columns[c], true });
        return s;
    }

    static const std::map<juce::String, std::vector<Section>> known {
        { "studioComp",   { { "Circuit", 1 }, { "Compression", 5 }, { "Gain", 2 }, { "Side Chain", 1 }, { "Output", 4 } } },
        { "multiband",    { { "Crossover", 2 }, { "Low", 2 }, { "Mid", 2 }, { "High", 2 }, { "Timing", 2 }, { "Band Gains", 3 }, { "Output", 1 } } },
        { "deesser",      { { "Detector", 3 }, { "Mode", 2 } } },
        { "noiseGate",    { { "Gate", 6 }, { "Side Chain", 2 } } },
        { "expander",     { { "Expansion", 6 }, { "Output", 1 } } },
        { "transient",    { { "Shape", 2 }, { "Timing", 2 }, { "Output", 1 } } },
        { "brickwall",    { { "Limiter", 4 } } },
        { "compressor",   { { "Compression", 5 } } },
        { "stereoDelay",  { { "Left", 0, false, { "inputL", "timeL", "noteL", "devL", "fbL", "fbPhaseL", "cross" } },
                            { "Right", 0, false, { "inputR", "timeR", "noteR", "devR", "fbR", "fbPhaseR", "crossRL" } },
                            { "Global", 0, false, { "routing", "sync", "crossPhase", "lowCut", "highCut" } },
                            { "Output Mix", 0, false, { "outL", "outR", "width", "mix" } } } },
        { "tapeDelay",    { { "Delay", 0, false, { "sync", "time", "note", "deviation" } }, { "Character", 0, false, { "smoothing", "clip", "spread", "head", "lowCut", "highCut" } },
                            { "Feedback", 0, false, { "feedback", "freeze" } }, { "Modulation", 0, false, { "lfoRate", "wow", "flutterRate", "flutter" } },
                            { "Output", 0, false, { "dry", "mix" } } } },
        { "modDelay",     { { "Delay", 2 }, { "Modulation", 3 }, { "Output", 1 } } },
        { "echo",         { { "Time", 3 }, { "Echo", 2 }, { "Output", 0, false, { "dry", "mix" } } } },
        { "studioReverb", { { "Space", 4 }, { "Character", 3 }, { "Diffusion", 2 }, { "Output", 3 } } },
        { "platinumVerb", { { "Early Reflections", 0, false, { "predelay", "shape", "roomSize", "stereoBase", "erScale" } },
                            { "Reverb", 0, false, { "initial", "time", "spread", "density", "diffusion", "crossover", "lowRatio", "lowLevel", "highCut" } },
                            { "Output", 0, false, { "balance", "dry", "wet" } } } },
        { "enVerb",       { { "Envelope", 0, false, { "dryDelay", "attack", "decay", "sustain", "hold", "release" } },
                            { "Sound", 0, false, { "density", "spread", "highCut", "crossover", "lowLevel" } }, { "Mix", 0, false, { "dry", "wet" } } } },
        { "silverVerb",   { { "Reverb", 0, false, { "predelay", "reflectivity", "size", "densityTime" } }, { "Filter", 0, false, { "lowCut", "highCut" } },
                            { "Modulation", 0, false, { "modOn", "rate", "phase", "intensity" } }, { "Mix", 0, false, { "dry", "wet" } } } },
        { "plateReverb",  { { "Plate", 3 }, { "Tone", 3 }, { "Output", 3 } } },
        { "springReverb", { { "Tank", 4 }, { "Tone", 2 }, { "Output", 1 } } },
        { "convSpace",    { { "Impulse", 3 }, { "Color", 3 }, { "Output", 4 } } },
        { "chorus",       { { "Modulation", 3 }, { "Voices", 3 }, { "Output", 1 } } },
        { "flanger",      { { "Rate", 3 }, { "Flange", 3 }, { "Output", 2 } } },
        { "phaser",       { { "Stages", 1 }, { "Rate", 3 }, { "Sweep", 3 }, { "Output", 2 } } },
        { "tremolo",      { { "Rate", 3 }, { "Shape", 3 } } },
        { "autoPan",      { { "Rate", 3 }, { "Shape", 2 } } },
        { "rotary",       { { "Motor", 3 }, { "Cabinet", 4 } } },
        { "ringMod",      { { "Carrier", 2 }, { "LFO", 2 }, { "Output", 1 } } },
        { "vibrato",      { { "Rate", 3 }, { "Vibrato", 2 }, { "Output", 1 } } },
        { "fuzz",         { { "Fuzz", 3 }, { "Gate", 1 }, { "Output", 1 } } },
        { "bitcrusher",   { { "Crush", 4 }, { "Output", 2 } } },
        { "clipDistortion", { { "Clip", 2 }, { "Filters", 2 }, { "Output", 2 } } },
        { "saturator",    { { "Circuit", 2 }, { "Tone", 1 }, { "Output", 2 } } },
        { "autoFilter",   { { "Filter", 3 }, { "LFO", 4 }, { "Envelope", 3 }, { "Output", 3 } } },
        { "autoWah",      { { "Wah", 4 }, { "Envelope", 2 }, { "Output", 2 } } },
        { "stepFilter",   { { "Steps", 8 }, { "Sequence", 5 }, { "Output", 1 } } },
        { "delayDesigner", { { "Taps", 12 }, { "Shape", 3 }, { "Output", 1 } } },
        { "subBass",      { { "Bass", 1 }, { "Sub", 2 }, { "Mix", 1 }, { "Shape", 1 } } },
        { "spreader",     { { "Spread", 3 }, { "Output", 2 } } },
        { "guitarAmp",    { { "Amp", 1 }, { "Preamp", 1 }, { "Tone", 4 }, { "Power", 1 }, { "Cabinet", 2 }, { "Output", 2 } } },
        { "bassAmp",      { { "Preamp", 1 }, { "Tone", 4 }, { "Power", 1 }, { "Cabinet", 2 }, { "DI", 2 }, { "Output", 2 } } },
        { "pitchCorrection", { { "Scale", 2 }, { "Correction", 3 } } },
        { "vocalTransformer", { { "Voice", 2 }, { "Robot", 2 }, { "Output", 1 } } },
        { "pitchShifter", { { "Shift", 2 }, { "Grain", 1 }, { "Output", 2 } } },
        { "stereoSpread", { { "Bands", 1 }, { "Intensity", 2 }, { "Range", 2 }, { "Output", 1 } } },
        { "directionMixer", { { "Input", 1 }, { "Image", 2 }, { "Output", 1 } } },
        { "gain",         { { "Level", 1 }, { "Phase", 2 }, { "Routing", 2 }, { "Balance", 1 } } },
        { "testOsc",      { { "Signal", 2 }, { "Level", 1 }, { "Sweep", 1 }, { "Mode", 1 } } },
        { "vocoder",      { { "Bands", 3 }, { "Envelope", 2 }, { "Formant", 1 }, { "Carrier", 2 }, { "Output", 2 } } },
    };
    if (const auto it = known.find (info.type); it != known.end())
        return it->second;
    return { { {}, (int) info.params.size() } };
}

//==============================================================================
namespace
{
    float valueOf (const EffectInfo& info, const float* v, const char* id, float fallback)
    {
        for (size_t i = 0; i < info.params.size(); ++i)
            if (info.params[i].id == id)
                return v[i];
        return fallback;
    }
    bool has (const EffectInfo& info, const char* id)
    {
        for (const auto& p : info.params)
            if (p.id == id)
                return true;
        return false;
    }

    void grid (juce::Graphics& g, juce::Rectangle<float> a, int columns, int rows)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (int c = 1; c < columns; ++c)
            g.fillRect (a.getX() + a.getWidth() * (float) c / (float) columns, a.getY(), 1.0f, a.getHeight());
        for (int r = 1; r < rows; ++r)
            g.fillRect (a.getX(), a.getY() + a.getHeight() * (float) r / (float) rows, a.getWidth(), 1.0f);
    }

    void stroke (juce::Graphics& g, const juce::Path& p, juce::Colour accent, juce::Rectangle<float> a, bool fill)
    {
        if (fill)
        {
            juce::Path f (p);
            f.lineTo (a.getRight(), a.getBottom());
            f.lineTo (a.getX(), a.getBottom());
            f.closeSubPath();
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0.0f, a.getY(), accent.withAlpha (0.02f), 0.0f, a.getBottom(), false));
            g.fillPath (f);
        }
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void caption (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& text)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (text, a.reduced (8.0f, 4.0f), juce::Justification::topRight, false);
    }

    /** An LFO shape value (matches the modulation effects): sine, triangle, square, saw, random. */
    float lfoShape (int shape, double phase)
    {
        phase -= std::floor (phase);
        switch (shape)
        {
            case 1:  return (float) (phase < 0.5 ? phase * 4.0 - 1.0 : 3.0 - phase * 4.0);
            case 2:  return std::tanh ((float) std::sin (juce::MathConstants<double>::twoPi * phase) * 6.0f);
            case 3:  return (float) (1.0 - phase * 2.0);
            case 4:  return std::sin ((float) std::floor (phase * 8.0) * 2.3f);
            default: return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
        }
    }
}

bool hasDisplay (const EffectInfo& info)
{
    if (info.response != nullptr)
        return true;
    const auto& c = info.category;
    if (c == "Dynamics")
        return info.type != "transient";
    return c == "Delay" ? info.type != "sampleDelay"
         : c == "Reverb" || c == "Distortion" || c == "Filter"
           || (c == "Modulation" && info.type != "rotary");
}

void drawDisplay (juce::Graphics& g, juce::Rectangle<float> area, const EffectInfo& info, const float* v, juce::Colour accent)
{
    // The screen: a dark glass panel
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b0d10), 0.0f, area.getY(), juce::Colour (0xff15191f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (area, 6.0f, 1.5f);
    const auto a = area.reduced (10.0f, 8.0f);
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.reduced (1.0f).toNearestInt());
    const auto& c = info.category;

    if (info.response != nullptr)
    {
        // EQ: 20 Hz - 20 kHz, +-24 dB
        const auto xOf = [a] (double f) { return a.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * a.getWidth(); };
        const auto yOf = [a] (double db) { return a.getCentreY() - (float) (db / 24.0) * a.getHeight() * 0.5f; };
        g.setFont (juce::FontOptions (10.0f));
        for (const auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (xOf (f), a.getY(), 1.0f, a.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (xOf (f) + 3.0f, a.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left, false);
        }
        for (const auto db : { -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.16f : 0.05f));
            g.fillRect (a.getX(), yOf (db), a.getWidth(), 1.0f);
            if (db != 0.0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.3f));
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (a.getX() + 2.0f, yOf (db) - 6.0f, 26.0f, 12.0f),
                            juce::Justification::left, false);
            }
        }
        juce::Path p;
        for (int x = 0; x <= (int) a.getWidth(); x += 2)
        {
            const auto f = 20.0 * std::pow (1000.0, x / (double) a.getWidth());
            const auto y = juce::jlimit (a.getY(), a.getBottom(), yOf (info.response (v, f, 48000.0)));
            if (x == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + (float) x, y);
        }
        juce::Path f (p);
        f.lineTo (a.getRight(), yOf (0.0));
        f.lineTo (a.getX(), yOf (0.0));
        f.closeSubPath();
        g.setColour (accent.withAlpha (0.16f));
        g.fillPath (f);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        return;
    }

    if (c == "Dynamics")
    {
        // In against out, -60..0 dB, with the threshold marked
        grid (g, a, 6, 6);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
        const bool isGate = info.type == "noiseGate", isExpander = info.type == "expander", isLimiter = info.type == "brickwall";
        const auto threshold = has (info, "threshold") ? valueOf (info, v, "threshold", -20.0f)
                             : has (info, "midThr") ? valueOf (info, v, "midThr", -20.0f)
                             : isLimiter ? valueOf (info, v, "ceiling", -0.3f) : -20.0f;
        const auto ratio = has (info, "ratio") ? valueOf (info, v, "ratio", 4.0f) : has (info, "midRatio") ? valueOf (info, v, "midRatio", 3.0f)
                         : info.type == "deesser" ? 5.0f : 20.0f;
        const auto knee = info.type == "studioComp" ? valueOf (info, v, "knee", 0.3f) * 18.0f : valueOf (info, v, "knee", 3.0f);
        const auto makeup = valueOf (info, v, "makeup", 0.0f) + valueOf (info, v, "output", 0.0f) + valueOf (info, v, "gain", 0.0f) * 0.0f;
        const auto range = isGate ? valueOf (info, v, "range", -80.0f) : isExpander ? valueOf (info, v, "range", -40.0f) : 0.0f;
        const auto xOf = [a] (float db) { return a.getX() + (db + 60.0f) / 60.0f * a.getWidth(); };
        const auto yOf = [a] (float db) { return a.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * a.getHeight(); };
        juce::Path p;
        for (int k = 0; k <= 240; ++k)
        {
            const auto in = -60.0f + 60.0f * (float) k / 240.0f;
            float out;
            if (isGate)
                out = in < threshold ? in + range : in;
            else if (isExpander)
                out = in < threshold ? in + std::max (range, (in - threshold) * (ratio - 1.0f)) : in;
            else if (isLimiter)
                out = std::min (in + valueOf (info, v, "gain", 0.0f), threshold);
            else
                out = in + dsp::compressorGainDb (in, threshold, ratio, knee) + makeup;
            if (k == 0) p.startNewSubPath (xOf (in), yOf (out)); else p.lineTo (xOf (in), yOf (out));
        }
        g.setColour (accent.withAlpha (0.5f));
        const float dashes[] { 3.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (xOf (threshold), a.getY(), xOf (threshold), a.getBottom()), dashes, 2, 1.0f);
        stroke (g, p, accent, a, false);
        caption (g, area, isLimiter ? "Ceiling " + juce::String (threshold, 1) + " dB" : "Threshold " + juce::String (threshold, 1) + " dB");
        return;
    }

    if (info.type == "delayDesigner")
    {
        // Six taps over two seconds: height their level, above or below by their side
        grid (g, a, 8, 2);
        const auto spread = valueOf (info, v, "spread", 70.0f) * 0.01f;
        const auto mid = a.getCentreY();
        for (int t = 0; t < 6; ++t)
        {
            const auto n = juce::String (t + 1);
            const auto time = valueOf (info, v, ("time" + n).toRawUTF8(), 250.0f) * 0.001f;
            const auto db = valueOf (info, v, ("level" + n).toRawUTF8(), -12.0f);
            if (db <= -59.0f || time > 2.0f)
                continue;
            const auto x = a.getX() + time / 2.0f * a.getWidth();
            const auto h = juce::jlimit (0.05f, 1.0f, (db + 60.0f) / 60.0f) * a.getHeight() * 0.45f;
            const auto side = t % 2 == 0 ? -1.0f : 1.0f;
            g.setColour (accent);
            if (side * spread < 0.0f || spread < 0.05f)
                g.fillRoundedRectangle (x - 3.0f, mid - h, 6.0f, h, 3.0f);
            if (side * spread > 0.0f || spread < 0.05f)
                g.fillRoundedRectangle (x - 3.0f, mid, 6.0f, h, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (n, juce::Rectangle<float> (x - 8.0f, side < 0.0f ? mid - h - 14.0f : mid + h + 2.0f, 16.0f, 12.0f), juce::Justification::centred, false);
        }
        return;
    }

    if (c == "Delay")
    {
        // The echoes over two seconds (synced times at 120 BPM), left above and right below
        grid (g, a, 8, 2);
        const auto sync = valueOf (info, v, "sync", 0.0f) > 0.5f;
        const auto timeOf = [&] (const char* timeId, const char* noteId)
        {
            return sync ? (float) dsp::noteSeconds (juce::roundToInt (valueOf (info, v, noteId, 9.0f)), 120.0)
                        : valueOf (info, v, timeId, 375.0f) * 0.001f;
        };
        const bool stereo = info.type == "stereoDelay";
        const auto tl = stereo ? timeOf ("timeL", "noteL") : info.type == "modDelay" ? valueOf (info, v, "time", 10.0f) * 0.001f : timeOf ("time", "note");
        const auto tr = stereo ? timeOf ("timeR", "noteR") : tl;
        const auto fbl = (stereo ? valueOf (info, v, "fbL", 30.0f) : valueOf (info, v, info.type == "echo" ? "repeats" : "feedback", 30.0f)) * 0.01f;
        const auto fbr = stereo ? valueOf (info, v, "fbR", 30.0f) * 0.01f : fbl;
        const auto span = info.type == "modDelay" ? 0.1f : 2.0f;
        const auto mid = a.getCentreY();
        for (int side = 0; side < 2; ++side)
        {
            const auto t = side == 0 ? tl : tr;
            auto level = 1.0f;
            for (int k = 1; k <= 24 && t * (float) k <= span; ++k)
            {
                const auto x = a.getX() + t * (float) k / span * a.getWidth();
                const auto h = level * (a.getHeight() * 0.45f);
                g.setColour (accent.withAlpha (0.25f + 0.7f * level));
                if (side == 0)
                    g.fillRoundedRectangle (x - 2.0f, mid - h, 4.0f, h, 2.0f);
                else
                    g.fillRoundedRectangle (x - 2.0f, mid, 4.0f, h, 2.0f);
                level *= std::abs (fbl * (side == 0 ? 1.0f : fbr / std::max (0.001f, fbl)));
                if (level < 0.02f)
                    break;
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRoundedRectangle (a.getX() - 1.0f, mid - a.getHeight() * 0.45f, 3.0f, a.getHeight() * 0.9f, 1.5f);
        caption (g, area, juce::String (tl * 1000.0f, 0) + " ms" + (stereo ? " / " + juce::String (tr * 1000.0f, 0) + " ms" : juce::String()));
        return;
    }

    if (c == "Reverb")
    {
        // The decay: early reflections, then the tail fading away
        grid (g, a, 8, 4);
        const auto decay = info.type == "studioReverb" ? valueOf (info, v, "decay", 2.0f)
                         : info.type == "convSpace" ? valueOf (info, v, "length", 2.5f)
                         : info.type == "plateReverb" ? 0.5f + valueOf (info, v, "decay", 55.0f) * 0.06f
                         : 0.5f + valueOf (info, v, "decay", 60.0f) * 0.04f;
        const auto pre = valueOf (info, v, "predelay", 10.0f) * 0.001f;
        const auto span = std::max (1.0f, (pre + decay) * 1.2f);
        const bool reverse = valueOf (info, v, "reverse", 0.0f) > 0.5f;
        juce::Path p;
        p.startNewSubPath (a.getX(), a.getBottom());
        juce::Random random (5);
        for (int x = 0; x <= (int) a.getWidth(); ++x)
        {
            const auto t = (float) x / a.getWidth() * span;
            float level = 0.0f;
            if (t >= pre)
            {
                auto u = t - pre;
                if (reverse)
                    u = std::max (0.0f, decay - u);
                level = std::exp (-6.9f * u / decay) * (u < decay * 1.1f || ! reverse ? 1.0f : 0.0f);
                if (reverse && t - pre > decay)
                    level = 0.0f;
            }
            const auto jitter = 0.85f + 0.15f * random.nextFloat();
            p.lineTo (a.getX() + (float) x, a.getBottom() - level * jitter * a.getHeight() * 0.92f);
        }
        stroke (g, p, accent, a, true);
        caption (g, area, juce::String (decay, 1) + " s");
        return;
    }

    if (c == "Distortion")
    {
        // The curve the signal is bent through, -1..1
        grid (g, a, 4, 4);
        const auto drive = info.type == "bitcrusher" ? 0.0f
                         : dsp::dbToGain (valueOf (info, v, info.type == "fuzz" ? "fuzz" : "drive", 12.0f) * (info.type == "saturator" ? 0.12f : 0.5f));
        juce::Path p;
        for (int k = 0; k <= 200; ++k)
        {
            const auto x = -1.0f + 2.0f * (float) k / 200.0f;
            float y;
            if (info.type == "bitcrusher")
            {
                const auto steps = std::pow (2.0f, juce::jlimit (1.0f, 24.0f, valueOf (info, v, "bits", 8.0f)) - 1.0f);
                y = std::round (x * steps) / steps;
            }
            else if (info.type == "clipDistortion")
                y = juce::jlimit (-0.5f * (1.0f - valueOf (info, v, "symmetry", 0.0f) * 0.01f), 0.5f, x * drive) * 2.0f;
            else
                y = std::tanh (x * drive) / std::max (0.5f, std::tanh (drive));
            const auto px = a.getCentreX() + x * a.getWidth() * 0.5f, py = a.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * a.getHeight() * 0.48f;
            if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
        stroke (g, p, accent, a, false);
        return;
    }

    // Modulation and filters: the LFO's shape over two cycles (a filter's step sequence as bars)
    grid (g, a, 8, 4);
    if (info.type == "stepFilter")
    {
        const auto w = a.getWidth() / 8.0f;
        for (int s = 0; s < 8; ++s)
        {
            const auto h = valueOf (info, v, ("s" + juce::String (s + 1)).toRawUTF8(), 50.0f) * 0.01f * a.getHeight();
            g.setColour (accent.withAlpha (0.8f));
            g.fillRoundedRectangle (a.getX() + w * (float) s + 3.0f, a.getBottom() - h, w - 6.0f, h, 3.0f);
        }
        return;
    }
    const auto shape = juce::roundToInt (valueOf (info, v, "shape", 0.0f));
    const auto depth = juce::jlimit (0.05f, 1.0f, std::abs (valueOf (info, v, info.type == "autoFilter" ? "lfoDepth" : "depth", 60.0f)) * 0.01f);
    juce::Path p;
    for (int x = 0; x <= (int) a.getWidth(); ++x)
    {
        const auto phase = 2.0 * x / a.getWidth();
        const auto y = (info.type == "ringMod" || info.type == "autoWah") ? (float) std::sin (juce::MathConstants<double>::twoPi * phase * 3.0)
                                                                           : lfoShape (shape, phase);
        const auto py = a.getCentreY() - y * depth * a.getHeight() * 0.45f;
        if (x == 0) p.startNewSubPath (a.getX(), py); else p.lineTo (a.getX() + (float) x, py);
    }
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
    stroke (g, p, accent, a, false);
}

//==============================================================================
FaceLook::FaceLook (juce::Colour accentColour) : accent (accentColour)
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.9f));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0d1014));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1014));
    setColour (juce::TextEditor::textColourId, juce::Colours::white);
}

void FaceLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                 float startAngle, float endAngle, juce::Slider& slider)
{
    const auto size = (float) std::min (width, height) - 4.0f;
    const auto bounds = juce::Rectangle<float> (size, size).withCentre (juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre());
    const auto centre = bounds.getCentre();
    const auto radius = size * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);

    // The ring: a dark track, the value in the accent colour (from the middle for +/- controls)
    const auto ringRadius = radius - 2.0f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto from = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (accent.withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The knob: a dark cap with a soft highlight, and a white pointer
    const auto capRadius = radius - 7.0f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (cap.translated (0.0f, 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a5059), centre.x, cap.getY(), juce::Colour (0xff1c1f24), centre.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.25f, -capRadius + 3.0f, 2.5f, capRadius * 0.55f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

juce::Label* FaceLook::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (juce::FontOptions (11.5f));
    l->setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d1014));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    return l;
}

} // namespace riff::effectface
