#include "StudioInstruments.h"

namespace riff::studio
{

namespace
{
    // The sounds a pad can make: synthesised in the style of the classic drum machines, the recorded kit, your own
    enum Src { kick808, kick909, kickTrap, bass808, snare808, snare909, clap, rim, snap, hatC, hatO, crash, ride,
               tomL, tomM, tomH, cowbell, clave, shaker, perc, riser, vox, acid, acoustic, user };

    struct PadDef { const char* name; const char* sub; Src src; float tune = 0, decay = 1, tone = 1, level = 0; int choke = 0; float pan = 0; int note = 0; };

    juce::Colour colourOf (Src s)
    {
        switch (s)
        {
            case kick808: case kick909: case kickTrap: return juce::Colour (0xffff3b54);
            case bass808: return juce::Colour (0xffff8a1f);
            case snare808: case snare909: case clap: case rim: case snap: return juce::Colour (0xffffd84a);
            case hatC: case hatO: case shaker: return juce::Colour (0xff19d3d0);
            case crash: case ride: return juce::Colour (0xff3fa8ff);
            case tomL: case tomM: case tomH: return juce::Colour (0xff5cff8f);
            case cowbell: case clave: case perc: return juce::Colour (0xffa25bff);
            case riser: case vox: return juce::Colour (0xffff4fb4);
            case acid: return juce::Colour (0xffb7ff3b);
            case acoustic: return juce::Colour (0xffd9b48a);
            case user: default: return juce::Colour (0xffeeeeee);
        }
    }

    using Bank = std::array<PadDef, 18>;

    // Each kit's bank A (bank B: its variations; C: the acid bass line across the keys; D: the recorded kit)
    struct KitDef { const char* name; Bank a; bool lofi = false; };

    const KitDef kits[] {
        { "Trap Lord", { { { "KICK", "808 TRAP", kickTrap }, { "KICK 2", "PUNCH", kick909, 2, 0.7f }, { "SNARE", "TRAP", snare909, 2, 0.8f },
                           { "CLAP", "STACK", clap }, { "RIM", "808", rim }, { "SNAP", "FINGER", snap },
                           { "HAT", "CLOSED", hatC, 0, 1, 1, -2, 1 }, { "HAT", "OPEN", hatO, 0, 1, 1, -3, 1 }, { "HAT", "ROLL", hatC, 3, 0.5f, 1, -6, 1 },
                           { "CRASH", "909", crash }, { "RIDE", "606", ride, 0, 1, 1, -4 }, { "PERC", "COWBELL", cowbell, 0, 1, 1, -4 },
                           { "808", "LONG C", bass808, 0, 2.2f }, { "808", "SLIDE", bass808, 0, 1.4f }, { "TOM", "LO", tomL },
                           { "TOM", "HI", tomH }, { "FX", "RISER", riser, 0, 1, 1, -6 }, { "VOX", "HEY", vox } } } },
        { "Machine 808", { { { "KICK", "LONG", kick808, 0, 1.6f }, { "KICK", "SHORT", kick808, 0, 0.5f }, { "SNARE", "TONE", snare808 },
                             { "CLAP", "808", clap }, { "RIM", "SHOT", rim }, { "CLAVE", "808", clave },
                             { "HAT", "CLOSED", hatC, -1, 1, 0.8f, -2, 1 }, { "HAT", "OPEN", hatO, -1, 1, 0.8f, -3, 1 }, { "CYMBAL", "808", crash, -2 },
                             { "TOM", "LO", tomL }, { "TOM", "MID", tomM }, { "TOM", "HI", tomH },
                             { "CONGA", "HI", tomH, 7, 0.6f }, { "CONGA", "LO", tomM, 7, 0.6f }, { "COWBELL", "808", cowbell },
                             { "MARACA", "808", shaker }, { "808", "BASS", bass808, 0, 1.8f }, { "808", "BASS HI", bass808, 7, 1.2f } } } },
        { "Machine 909", { { { "KICK", "909", kick909 }, { "KICK", "TUNED", kick909, 3, 0.8f }, { "SNARE", "909", snare909 },
                             { "SNARE", "TIGHT", snare909, 3, 0.6f }, { "CLAP", "909", clap }, { "RIM", "909", rim },
                             { "HAT", "CLOSED", hatC, 1, 1, 1, -2, 1 }, { "HAT", "OPEN", hatO, 1, 1, 1, -3, 1 }, { "CRASH", "909", crash },
                             { "RIDE", "909", ride }, { "TOM", "LO", tomL, 2 }, { "TOM", "MID", tomM, 2 },
                             { "TOM", "HI", tomH, 2 }, { "SHAKER", "", shaker }, { "PERC", "BLIP", perc },
                             { "STAB", "VOX", vox }, { "FX", "SWEEP", riser }, { "BASS", "SUB", bass808, 0, 0.8f } } } },
        { "Machine 606", { { { "KICK", "606", kick909, -3, 0.4f, 0.6f }, { "KICK", "LOW", kick808, -2, 0.6f }, { "SNARE", "606", snare808, 4, 0.5f },
                             { "HAT", "CLOSED", hatC, 4, 0.6f, 1, -3, 1 }, { "HAT", "OPEN", hatO, 4, 0.7f, 1, -4, 1 }, { "CYMBAL", "606", crash, 3, 0.8f },
                             { "TOM", "LO", tomL, 4, 0.6f }, { "TOM", "HI", tomH, 4, 0.6f }, { "CLAP", "THIN", clap, 3, 0.6f },
                             { "RIM", "606", rim, 2 }, { "BLIP", "", perc, 5 }, { "SHAKE", "", shaker, 3 },
                             { "BASS", "606", acid, 0, 0.5f }, { "BASS", "+5", acid, 5, 0.5f }, { "BASS", "+7", acid, 7, 0.5f },
                             { "BASS", "+12", acid, 12, 0.5f }, { "FX", "ZAP", riser, 12, 0.3f }, { "CLAVE", "", clave, 2 } } } },
        { "Machine 707", { { { "KICK", "707", kick909, -1, 0.6f, 0.8f }, { "KICK", "2", kick909, -4, 0.8f, 0.7f }, { "SNARE", "707", snare909, -1, 0.8f, 0.8f },
                             { "SNARE", "2", snare909, -3, 1.0f, 0.7f }, { "CLAP", "707", clap, -1 }, { "RIM", "707", rim, -2 },
                             { "HAT", "CLOSED", hatC, 0, 0.8f, 0.85f, -2, 1 }, { "HAT", "OPEN", hatO, 0, 0.9f, 0.85f, -3, 1 }, { "CRASH", "707", crash, -1 },
                             { "RIDE", "707", ride, -1 }, { "TOM", "LO", tomL, -1 }, { "TOM", "MID", tomM, -1 },
                             { "TOM", "HI", tomH, -1 }, { "COWBELL", "707", cowbell, -2 }, { "TAMB", "707", shaker, 2, 1.4f },
                             { "CLAVE", "707", clave }, { "BASS", "", bass808, -2, 1.0f }, { "VOX", "", vox, -2 } } } },
        { "Machine 505", { { { "KICK", "505", kick909, 1, 0.5f, 0.75f }, { "SNARE", "505", snare808, 2, 0.7f, 0.8f }, { "CLAP", "505", clap, 1, 0.8f, 0.8f },
                             { "RIM", "505", rim, 1 }, { "HAT", "CLOSED", hatC, 2, 0.7f, 0.8f, -2, 1 }, { "HAT", "OPEN", hatO, 2, 0.8f, 0.8f, -3, 1 },
                             { "CRASH", "505", crash, 1 }, { "RIDE", "505", ride, 1 }, { "TOM", "LO", tomL, 3 },
                             { "TOM", "MID", tomM, 3 }, { "TOM", "HI", tomH, 3 }, { "COWBELL", "505", cowbell },
                             { "TIMBALE", "HI", tomH, 12, 0.4f }, { "TIMBALE", "LO", tomM, 12, 0.4f }, { "AGOGO", "", cowbell, 7, 0.6f },
                             { "SHAKER", "", shaker }, { "BLIP", "", perc, -5 }, { "BASS", "505", bass808, 3, 0.7f } } } },
        { "Lo-Fi Dust", { { { "KICK", "DUSTY", kick909, -2, 0.8f, 0.5f }, { "KICK", "BOOM", kick808, -1, 1.0f, 0.5f }, { "SNARE", "CRACK", snare909, -2, 0.9f, 0.55f },
                            { "SNARE", "RIM", rim, -3, 1, 0.6f }, { "CLAP", "ROOM", clap, -2, 1.2f, 0.5f }, { "SNAP", "", snap, -2, 1, 0.6f },
                            { "HAT", "TAPE", hatC, -3, 1, 0.5f, -3, 1 }, { "HAT", "OPEN", hatO, -3, 1, 0.5f, -4, 1 }, { "SHAKER", "", shaker, -2, 1, 0.5f },
                            { "RIDE", "OLD", ride, -3, 1, 0.5f }, { "PERC", "WOOD", clave, -5, 1, 0.6f }, { "PERC", "BELL", cowbell, -3, 1, 0.5f },
                            { "BASS", "SUB", bass808, -1, 1.2f, 0.5f }, { "TOM", "LO", tomL, -2, 1, 0.5f }, { "TOM", "HI", tomH, -2, 1, 0.5f },
                            { "VOX", "CHOP", vox, -3, 1, 0.5f }, { "FX", "VINYL", riser, -12, 2, 0.3f }, { "BLIP", "", perc, -7, 1, 0.5f } } }, true },
        { "Acoustic Room", { { { "KICK", "ROOM", acoustic, 0, 1, 1, 0, 0, 0, 36 }, { "KICK", "2", acoustic, 0, 1, 1, 0, 0, 0, 35 }, { "SNARE", "CENTER", acoustic, 0, 1, 1, 0, 0, 0, 38 },
                               { "SNARE", "EDGE", acoustic, 0, 1, 1, 0, 0, 0, 40 }, { "STICK", "", acoustic, 0, 1, 1, 0, 0, 0, 37 }, { "HAT", "PEDAL", acoustic, 0, 1, 1, 0, 1, 0, 44 },
                               { "HAT", "CLOSED", acoustic, 0, 1, 1, 0, 1, 0, 42 }, { "HAT", "OPEN", acoustic, 0, 1, 1, 0, 1, 0, 46 }, { "CRASH", "", acoustic, 0, 1, 1, 0, 0, 0, 49 },
                               { "RIDE", "", acoustic, 0, 1, 1, 0, 0, 0, 51 }, { "RIDE", "BELL", acoustic, 0, 1, 1, 0, 0, 0, 53 }, { "CHINA", "", acoustic, 0, 1, 1, 0, 0, 0, 52 },
                               { "TOM", "1", acoustic, 0, 1, 1, 0, 0, 0, 50 }, { "TOM", "2", acoustic, 0, 1, 1, 0, 0, 0, 48 }, { "TOM", "3", acoustic, 0, 1, 1, 0, 0, 0, 45 },
                               { "TOM", "4", acoustic, 0, 1, 1, 0, 0, 0, 43 }, { "FLOOR", "", acoustic, 0, 1, 1, 0, 0, 0, 41 }, { "CRASH", "2", acoustic, 0, 1, 1, 0, 0, 0, 57 } } } },
        { "Acid Line", { { { "C", "ACID", acid, 0 }, { "C#", "ACID", acid, 1 }, { "D", "ACID", acid, 2 }, { "D#", "ACID", acid, 3 }, { "E", "ACID", acid, 4 }, { "F", "ACID", acid, 5 },
                           { "F#", "ACID", acid, 6 }, { "G", "ACID", acid, 7 }, { "G#", "ACID", acid, 8 }, { "A", "ACID", acid, 9 }, { "A#", "ACID", acid, 10 }, { "B", "ACID", acid, 11 },
                           { "C", "+12", acid, 12 }, { "C#", "+12", acid, 13 }, { "D", "+12", acid, 14 }, { "KICK", "909", kick909 }, { "HAT", "CLOSED", hatC, 0, 1, 1, -3, 1 }, { "CLAP", "", clap } } } },
    };
    constexpr int numKits = (int) std::size (kits);

    PadDef padDef (int kit, int pad)
    {
        kit = juce::jlimit (0, numKits - 1, kit);
        const auto bank = pad / BeatPad::padsPerBank, index = pad % BeatPad::padsPerBank;
        if (bank == 0)
            return kits[kit].a[(size_t) index];
        if (bank == 1)
        {
            // B: the same sounds pitched, longer and shorter
            auto d = kits[kit].a[(size_t) index];
            d.tune += index % 2 == 0 ? -3.0f : 4.0f;
            d.decay *= index % 3 == 0 ? 1.6f : 0.7f;
            d.sub = index % 2 == 0 ? "LOW" : "HIGH";
            return d;
        }
        if (bank == 2)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E", "F" };
            return { names[index], index >= 12 ? "ACID +12" : "ACID", acid, (float) index };
        }
        return kits[7].a[(size_t) index];   // D: the recorded kit
    }

    std::vector<Param> padParams()
    {
        std::vector<Param> p;
        p.push_back ({ "kit", "Kit", 0, (float) numKits - 1, 0, BeatPad::kitNames() });
        p.push_back ({ "bank", "Bank", 0, 3, 0, { "A", "B", "C", "D" } });
        p.push_back ({ "sel", "Pad", 0, (float) BeatPad::numPads - 1, 0 });
        p.push_back ({ "swing", "Swing", 50.0f, 75.0f, 58.0f });
        p.push_back ({ "repeat", "Note Repeat", 0, 7, 0, { "Off", "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1/32 T" } });
        p.push_back ({ "fullLevel", "Full Level", 0, 1, 0, { "Off", "On" } });
        p.push_back ({ "levels16", "16 Levels", 0, 4, 0, { "Off", "Velocity", "Tune", "Filter", "Decay" } });
        p.push_back ({ "seq", "Sequencer", 0, 1, 1, { "Off", "Plays with the song" } });
        p.push_back ({ "master", "Volume", -60.0f, 6.0f, -3.0f });
        for (int i = 0; i < BeatPad::numPads; ++i)
        {
            const auto pre = "pad" + juce::String (i);
            const auto name = "Pad " + juce::String (i + 1) + " ";
            p.push_back ({ pre + "Tune", name + "Tune", -24.0f, 24.0f, 0.0f });
            p.push_back ({ pre + "Decay", name + "Decay", 0.1f, 4.0f, 1.0f });
            p.push_back ({ pre + "Filter", name + "Filter", 0.0f, 1.0f, 1.0f });
            p.push_back ({ pre + "Reso", name + "Reso", 0.0f, 1.0f, 0.1f });
            p.push_back ({ pre + "Pan", name + "Pan", -1.0f, 1.0f, 0.0f });
            p.push_back ({ pre + "Level", name + "Level", -40.0f, 12.0f, 0.0f });
            p.push_back ({ pre + "Out", name + "Output", 0.0f, 15.0f, (float) (i % 16) });
            p.push_back ({ pre + "Choke", name + "Choke", 0, 4, 0, { "None", "1", "2", "3", "4" } });
            p.push_back ({ pre + "Mode", name + "Mode", 0, 1, 0, { "One shot", "Hold" } });
            p.push_back ({ pre + "Reverse", name + "Reverse", 0, 1, 0, { "Off", "On" } });
        }
        return p;
    }

    juce::StringArray outputNames()
    {
        juce::StringArray n;
        for (int i = 0; i < BeatPad::outputs; ++i)
            n.add ("Pads " + juce::String (i + 1));
        return n;
    }
}

struct BeatPad::Voice
{
    bool active = false, held = false, reversed = false;
    int pad = 0, part = 0, choke = 0, note = 60;
    Src src = kick808;
    double t = 0.0, phase = 0.0, phase2 = 0.0;
    std::array<double, 6> metal {};
    float tune = 0, decay = 1, level = 1, gainL = 1, gainR = 1, velocity = 1, chokeGain = 1;
    bool choking = false, lofi = false;
    dsp::Svf filter, filter2, shape;
    float crushHold = 0.0f; int crushCount = 0;
    const juce::AudioBuffer<float>* user = nullptr;
    double userPos = 0.0, userStep = 1.0;
};

BeatPad::~BeatPad() = default;

juce::StringArray BeatPad::kitNames()
{
    juce::StringArray n;
    for (const auto& k : kits) n.add (k.name);
    return n;
}

BeatPad::BeatPad() : StudioInstrument ("beatpad", padParams(), outputNames())
{
    voices.resize (48);
    acousticIndex = nemix::samples::indexOf ("rock-kit");
    if (acousticIndex >= 0)
        nemix::samples::request (acousticIndex);
    for (auto& p : userSampleNow) p.store (nullptr);
    // a beat to start with (the kit's kick, snare and hats)
    for (int s = 0; s < steps; ++s)
    {
        if (s == 0 || s == 7 || s == 10) pattern[0][(size_t) s].store (110);
        if (s == 4 || s == 12) pattern[2][(size_t) s].store (105);
        if (s % 2 == 0) pattern[6][(size_t) s].store (s % 4 == 0 ? 90 : 70);
        if (s == 0 || s == 10) pattern[12][(size_t) s].store (100);
    }
}

juce::String BeatPad::padName (int pad) const { const auto d = padDef (choice (indexOf ("kit")), pad); return d.name; }
juce::String BeatPad::padSound (int pad) const { const auto d = padDef (choice (indexOf ("kit")), pad); return userSamples[(size_t) juce::jlimit (0, numPads - 1, pad)] != nullptr ? juce::String ("SAMPLE") : juce::String (d.sub); }
juce::Colour BeatPad::padColour (int pad) const { return colourOf (padDef (choice (indexOf ("kit")), pad).src); }

int BeatPad::getStep (int pad, int step) const noexcept
{
    return juce::isPositiveAndBelow (pad, numPads) && juce::isPositiveAndBelow (step, steps) ? (int) pattern[(size_t) pad][(size_t) step].load() : 0;
}

void BeatPad::setStep (int pad, int step, int velocity) noexcept
{
    if (juce::isPositiveAndBelow (pad, numPads) && juce::isPositiveAndBelow (step, steps))
        pattern[(size_t) pad][(size_t) step].store ((juce::uint8) juce::jlimit (0, 127, velocity));
}

juce::String BeatPad::patternText() const
{
    // "pad:step=velocity,..." for the steps that play
    juce::StringArray on;
    for (int pad = 0; pad < numPads; ++pad)
        for (int s = 0; s < steps; ++s)
            if (const auto v = pattern[(size_t) pad][(size_t) s].load(); v > 0)
                on.add (juce::String (pad) + ":" + juce::String (s) + "=" + juce::String ((int) v));
    return on.joinIntoString (",");
}

void BeatPad::setPatternText (const juce::String& text)
{
    for (auto& pad : pattern) for (auto& s : pad) s.store (0);
    for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
    {
        const auto pad = part.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const auto step = part.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf ("=", false, false).getIntValue();
        setStep (pad, step, part.fromFirstOccurrenceOf ("=", false, false).getIntValue());
    }
}

void BeatPad::setSampleFile (int pad, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
    {
        const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20.0));
        buffer = std::make_shared<juce::AudioBuffer<float>> (2, length + 1);
        buffer->clear();
        reader->read (buffer.get(), 0, length, 0, true, true);
        // (resampled to the engine's rate by the voice's step: keep the file's rate in the last sample's spare slot)
        buffer->setSample (0, length, (float) reader->sampleRate);
    }
    // the old buffer stays alive in `userSamples` until replaced on the message thread; the audio thread reads the pointer
    userSampleNow[(size_t) pad].store (buffer.get());
    userSamples[(size_t) pad] = buffer;
}

void BeatPad::prepareVoices (double sampleRate, int)
{
    sampleVoices.prepare (sampleRate);
    for (auto& v : voices) v = Voice();
}

void BeatPad::startVoice (int pad, float velocity) noexcept
{
    const auto kit = choice (indexOf ("kit"));
    auto source = pad;
    float tuneShift = 0.0f, levelShift = 0.0f, filterShift = 0.0f, decayMul = 1.0f;
    // 16 levels: the selected pad across the bank's first 16 pads, stepped by velocity, tune, filter or decay
    if (const auto mode = choice (indexOf ("levels16")); mode > 0 && pad % padsPerBank < 16)
    {
        const auto step = (float) (pad % padsPerBank) / 15.0f;
        source = juce::jlimit (0, numPads - 1, choice (indexOf ("sel")));
        if (mode == 1) velocity = 0.1f + 0.9f * step;
        if (mode == 2) tuneShift = (step - 0.5f) * 24.0f;
        if (mode == 3) filterShift = step - 1.0f;
        if (mode == 4) decayMul = 0.2f + 1.8f * step;
    }
    if (choice (indexOf ("fullLevel")) == 1) velocity = 1.0f;
    const auto def = padDef (kit, source);
    const auto pre = "pad" + juce::String (source);
    const auto part = juce::jlimit (0, outputs - 1, juce::roundToInt (p (indexOf (pre + "Out"))));
    const auto choke = choice (indexOf (pre + "Choke")) > 0 ? choice (indexOf (pre + "Choke")) : def.choke;
    const auto pan = juce::jlimit (-1.0f, 1.0f, p (indexOf (pre + "Pan")) + def.pan);
    const auto level = dsp::dbToGain (p (indexOf (pre + "Level")) + def.level + levelShift) * velocity;

    // the recorded kit: through the sample voices
    if (def.src == acoustic && userSampleNow[(size_t) source].load() == nullptr)
    {
        SampleVoices<24>::Start s;
        s.note = def.note;
        s.velocity = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
        s.part = part;
        s.semitones = p (indexOf (pre + "Tune")) + tuneShift;
        s.gainDb = p (indexOf (pre + "Level")) + levelShift;
        s.pan = pan;
        s.decay = p (indexOf (pre + "Decay")) * decayMul < 0.99f ? 0.15f + 2.0f * p (indexOf (pre + "Decay")) * decayMul : 0.0f;
        s.oneShot = true;
        s.toneHz = p (indexOf (pre + "Filter")) + filterShift >= 0.99f ? 0.0f : 200.0f * std::pow (100.0f, juce::jlimit (0.0f, 1.0f, p (indexOf (pre + "Filter")) + filterShift));
        s.group = choke;
        s.chokeGroup = choke;
        sampleVoices.start (nemix::samples::find (acousticIndex), s);
        return;
    }

    // choke: the others in its group stop
    if (choke > 0)
        for (auto& v : voices)
            if (v.active && v.choke == choke && v.pad != source) v.choking = true;
    // a free voice, else the oldest
    Voice* chosen = nullptr;
    double oldest = -1.0;
    for (auto& v : voices)
    {
        if (! v.active) { chosen = &v; break; }
        if (v.t > oldest) { oldest = v.t; chosen = &v; }
    }
    auto& v = *chosen;
    v = Voice();
    v.active = true;
    v.held = true;
    v.pad = source;
    v.part = part;
    v.choke = choke;
    v.src = def.src;
    v.lofi = kits[juce::jlimit (0, numKits - 1, kit)].lofi;
    v.tune = def.tune + p (indexOf (pre + "Tune")) + tuneShift;
    v.decay = def.decay * p (indexOf (pre + "Decay")) * decayMul;
    v.velocity = velocity;
    v.level = level;
    v.gainL = pan <= 0.0f ? 1.0f : 1.0f - pan;
    v.gainR = pan >= 0.0f ? 1.0f : 1.0f + pan;
    v.reversed = choice (indexOf (pre + "Reverse")) == 1;
    const auto cutoff = 120.0f * std::pow (160.0f, juce::jlimit (0.0f, 1.0f, p (indexOf (pre + "Filter")) * def.tone + filterShift));
    v.filter.set (cutoff, 0.6f + p (indexOf (pre + "Reso")) * 8.0f, rate);
    if (auto* u = userSampleNow[(size_t) source].load())
    {
        v.src = user;
        v.user = u;
        const auto fileRate = (double) u->getSample (0, u->getNumSamples() - 1);
        v.userStep = std::pow (2.0, v.tune / 12.0) * (fileRate > 1000.0 ? fileRate : 44100.0) / rate;
        v.userPos = v.reversed ? (double) (u->getNumSamples() - 3) : 0.0;
    }
    if (v.src == acid)
        acidTarget = 36.0f + v.tune;
    lastNote.store (36 + pad);
    lastVelocity.store (juce::roundToInt (velocity * 127.0f));
    lastPart.store (pad);
}

void BeatPad::hit (int pad, float velocity) noexcept
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return;
    padHeld[(size_t) pad] = true;
    heldVelocity[(size_t) (36 + pad) & 127] = velocity;
    startVoice (pad, velocity);
    noteLevel[(size_t) ((36 + pad) & 127)].store (1.0f);
    // recording: into the pattern on the nearest step
    if (recording.load() && songPlaying)
    {
        const auto samplesPerStep = rate * 60.0 / songBpm / 4.0;
        const auto step = (int) std::llround ((double) songPosition / samplesPerStep) % steps;
        setStep (pad, step, juce::roundToInt (velocity * 127.0f));
    }
}

void BeatPad::release (int pad) noexcept
{
    if (! juce::isPositiveAndBelow (pad, numPads))
        return;
    padHeld[(size_t) pad] = false;
    heldVelocity[(size_t) (36 + pad) & 127] = 0.0f;
    if (choice (indexOf ("pad" + juce::String (pad) + "Mode")) == 1)
        for (auto& v : voices)
            if (v.active && v.pad == pad) v.held = false;
}

float BeatPad::renderVoice (Voice& v) noexcept
{
    const auto t = (float) v.t;
    const auto dt = 1.0 / rate;
    const auto ratio = std::pow (2.0f, v.tune / 12.0f);
    float out = 0.0f, env = 1.0f;
    const auto noise = [this] { return random.nextFloat() * 2.0f - 1.0f; };
    const auto sine = [&] (double& phase, float freq) { phase += freq * dt; if (phase >= 1.0) phase -= 1.0; return (float) std::sin (phase * juce::MathConstants<double>::twoPi); };
    const auto square = [&] (double& phase, float freq) { phase += freq * dt; if (phase >= 1.0) phase -= 1.0; return phase < 0.5 ? 1.0f : -1.0f; };
    const auto metallic = [&] (float scale)
    {
        const float f[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float m = 0.0f;
        for (int i = 0; i < 6; ++i) m += square (v.metal[(size_t) i], f[i] * scale * ratio);
        return m / 6.0f;
    };
    switch (v.src)
    {
        case kick808: case kickTrap: case bass808:
        {
            const auto base = (v.src == bass808 ? 43.65f : 49.0f) * ratio;
            const auto f = base + base * 2.2f * std::exp (-t / 0.03f);
            env = std::exp (-t / (0.45f * v.decay * (v.src == bass808 ? 2.2f : v.src == kickTrap ? 1.6f : 1.0f)));
            out = sine (v.phase, f);
            if (v.src != kick808) out = dsp::drive (out, v.src == kickTrap ? 0.35f : 0.2f);
            out += std::exp (-t / 0.002f) * 0.3f;
            break;
        }
        case kick909:
        {
            const auto f = 55.0f * ratio + 190.0f * std::exp (-t / 0.018f);
            env = std::exp (-t / (0.32f * v.decay));
            out = dsp::drive (sine (v.phase, f), 0.15f) + noise() * std::exp (-t / 0.0025f) * 0.45f;
            break;
        }
        case snare808:
        {
            env = 1.0f;
            const auto tone = (sine (v.phase, 180.0f * ratio) + 0.6f * sine (v.phase2, 330.0f * ratio)) * std::exp (-t / (0.11f * v.decay));
            const auto n = v.shape.high (noise()) * std::exp (-t / (0.16f * v.decay));
            if (v.t == 0.0) v.shape.set (1600.0f, 0.7f, rate);
            out = tone * 0.6f + n * 0.7f;
            env = t < 2.0f * v.decay ? 1.0f : 0.0f;
            break;
        }
        case snare909:
        {
            if (v.t == 0.0) v.shape.set (4000.0f, 0.8f, rate);
            const auto f = 185.0f * ratio + 60.0f * std::exp (-t / 0.01f);
            out = sine (v.phase, f) * std::exp (-t / (0.09f * v.decay)) * 0.6f + v.shape.band (noise()) * std::exp (-t / (0.2f * v.decay)) * 1.4f;
            env = t < 2.0f * v.decay ? 1.0f : 0.0f;
            break;
        }
        case clap:
        {
            if (v.t == 0.0) v.shape.set (1200.0f * ratio, 1.6f, rate);
            float e = 0.0f;
            for (const auto at : { 0.0f, 0.011f, 0.022f })
                if (t >= at) e = juce::jmax (e, std::exp (-(t - at) / 0.006f));
            if (t >= 0.03f) e = juce::jmax (e, std::exp (-(t - 0.03f) / (0.14f * v.decay)) * 0.8f);
            out = v.shape.band (noise()) * e * 2.2f;
            env = t < 1.5f ? 1.0f : 0.0f;
            break;
        }
        case rim:
            if (v.t == 0.0) v.shape.set (1700.0f * ratio, 4.0f, rate);
            out = sine (v.phase, 1700.0f * ratio) * std::exp (-t / 0.01f) * 0.7f + v.shape.band (noise()) * std::exp (-t / 0.006f);
            env = t < 0.3f ? 1.0f : 0.0f;
            break;
        case snap:
            if (v.t == 0.0) v.shape.set (2500.0f * ratio, 2.0f, rate);
            out = v.shape.band (noise()) * std::exp (-t / (0.03f * v.decay)) * 2.0f;
            env = t < 0.4f ? 1.0f : 0.0f;
            break;
        case hatC: case hatO: case crash: case ride:
        {
            const auto isRide = v.src == ride, isCrash = v.src == crash;
            if (v.t == 0.0) { v.shape.set (isRide ? 5200.0f : 9000.0f, 1.0f, rate); v.filter2.set (isCrash ? 5000.0f : 7000.0f, 0.7f, rate); }
            auto m = metallic (isRide ? 0.8f : 1.0f);
            if (isCrash) m = m * 0.6f + noise() * 0.5f;
            m = v.filter2.high (v.shape.band (m) * 2.0f + (isCrash ? m : 0.0f));
            const auto d = v.src == hatC ? 0.045f : v.src == hatO ? 0.38f : isCrash ? 1.4f : 1.9f;
            env = std::exp (-t / (d * v.decay));
            out = m * 1.6f;
            break;
        }
        case tomL: case tomM: case tomH:
        {
            const auto base = (v.src == tomL ? 95.0f : v.src == tomM ? 140.0f : 205.0f) * ratio;
            out = sine (v.phase, base * (1.0f + 0.55f * std::exp (-t / 0.05f))) + noise() * std::exp (-t / 0.004f) * 0.2f;
            env = std::exp (-t / (0.28f * v.decay));
            break;
        }
        case cowbell:
            if (v.t == 0.0) v.shape.set (900.0f * ratio, 1.8f, rate);
            out = v.shape.band (square (v.phase, 540.0f * ratio) + square (v.phase2, 800.0f * ratio)) * 1.4f;
            env = std::exp (-t / 0.012f) * 0.5f + std::exp (-t / (0.22f * v.decay)) * 0.5f;
            break;
        case clave:
            out = sine (v.phase, 2500.0f * ratio);
            env = std::exp (-t / (0.025f * v.decay));
            break;
        case shaker:
            if (v.t == 0.0) v.shape.set (6000.0f, 0.7f, rate);
            out = v.shape.high (noise());
            env = juce::jmin (1.0f, t / 0.01f) * std::exp (-t / (0.08f * v.decay));
            break;
        case perc:
        {
            const auto mod = sine (v.phase2, 600.0f * ratio) * 6.0f * std::exp (-t / 0.03f);
            v.phase += (400.0f * ratio) * dt;
            out = (float) std::sin ((v.phase + mod * 0.05) * juce::MathConstants<double>::twoPi);
            env = std::exp (-t / (0.12f * v.decay));
            break;
        }
        case riser:
        {
            const auto len = 1.5f * v.decay;
            v.shape.set (400.0f * std::pow (20.0f, juce::jlimit (0.0f, 1.0f, t / len)) * ratio, 2.5f, rate);
            out = v.shape.band (noise()) * 1.5f;
            env = t < len ? t / len : std::exp (-(t - len) / 0.05f);
            break;
        }
        case vox:
        {
            if (v.t == 0.0) { v.shape.set (700.0f * ratio, 5.0f, rate); v.filter2.set (1200.0f * ratio, 5.0f, rate); }
            v.phase += 180.0f * ratio * dt; if (v.phase >= 1.0) v.phase -= 1.0;
            const auto src = (float) (v.phase * 2.0 - 1.0) * 0.6f + noise() * 0.4f;
            out = (v.shape.band (src) + v.filter2.band (src)) * 2.0f;
            env = juce::jmin (1.0f, t / 0.01f) * std::exp (-t / (0.16f * v.decay));
            break;
        }
        case acid:
        {
            // the bass line synth: a saw through a resonant 24 dB filter swept by its envelope (accent: velocity)
            acidPitch += (acidTarget - acidPitch) * 0.0025f;
            const auto f = 440.0f * std::pow (2.0f, (acidPitch - 69.0f) / 12.0f);
            v.phase += f * dt; if (v.phase >= 1.0) v.phase -= 1.0;
            const auto saw = (float) (v.phase * 2.0 - 1.0);
            const auto cut = 180.0f + 2600.0f * (0.4f + v.velocity) * std::exp (-t / (0.18f * v.decay));
            v.shape.set (cut, 3.5f, rate);
            v.filter2.set (cut, 2.0f, rate);
            out = dsp::drive (v.filter2.low (v.shape.low (saw)) * 1.5f, 0.25f);
            env = v.held || t < 0.12f ? 1.0f : std::exp (-(t - 0.12f) / 0.03f);
            if (! v.held && t > 0.5f * v.decay) env *= std::exp (-(t - 0.5f * v.decay) / 0.02f);
            break;
        }
        case user:
        {
            if (v.user == nullptr) { v.active = false; return 0.0f; }
            const auto len = v.user->getNumSamples() - 2;
            const auto i0 = (int) v.userPos;
            if (i0 < 0 || i0 + 1 >= len) { v.active = false; return 0.0f; }
            const auto f = (float) (v.userPos - i0);
            out = v.user->getSample (0, i0) + (v.user->getSample (0, i0 + 1) - v.user->getSample (0, i0)) * f;
            v.userPos += v.reversed ? -v.userStep : v.userStep;
            env = v.decay < 3.9f ? std::exp (-t / (0.6f * v.decay)) : 1.0f;
            break;
        }
        case acoustic: default:
            v.active = false;
            return 0.0f;
    }
    // hold mode: the sound stops when the pad is let go
    if (! v.held && choice (indexOf ("pad" + juce::String (v.pad) + "Mode")) == 1)
        v.chokeGain *= 0.995f;
    if (v.choking) v.chokeGain *= 0.993f;
    out = v.filter.low (out) * env * v.level * v.chokeGain;
    // lo-fi: fewer bits, a lower rate
    if (v.lofi)
    {
        if (v.crushCount++ % 3 == 0) v.crushHold = std::round (out * 256.0f) / 256.0f;
        out = v.crushHold;
    }
    v.t += dt;
    if ((env < 1.0e-4f && v.t > 0.02) || v.chokeGain < 1.0e-3f || v.t > 12.0)
        v.active = false;
    return out;
}

void BeatPad::renderParts (float* const* pl, float* const* pr, int n, const juce::MidiBuffer& midi) noexcept
{
    // the sequencer's steps and the note repeat, in this block
    const auto samplesPerStep = rate * 60.0 / songBpm / 4.0;
    const auto swing = (double) (p (indexOf ("swing")) - 50.0f) / 50.0;   // (the off-beat sixteenths later)
    struct Trigger { int at, pad; float velocity; };
    std::array<Trigger, 128> triggers;
    int numTriggers = 0;
    if (songPlaying && choice (indexOf ("seq")) == 1)
    {
        const auto from = (double) songPosition, to = from + n;
        for (auto s = (juce::int64) std::floor (from / samplesPerStep) - 1; (double) s * samplesPerStep < to; ++s)
        {
            if (s < 0) continue;
            const auto when = (double) s * samplesPerStep + (s % 2 == 1 ? swing * samplesPerStep : 0.0);
            if (when < from || when >= to) continue;
            const auto step = (int) (s % steps);
            playingStep.store (step);
            for (int pad = 0; pad < numPads && numTriggers < (int) triggers.size(); ++pad)
                if (const auto vel = pattern[(size_t) pad][(size_t) step].load(); vel > 0)
                    triggers[(size_t) numTriggers++] = { (int) (when - from), pad, (float) vel / 127.0f };
        }
    }
    else if (! songPlaying)
        playingStep.store (-1);

    // note repeat: the held pads again on the repeat's grid
    const double repeatBeats[] { 0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
    if (const auto r = choice (indexOf ("repeat")); r > 0)
    {
        const auto every = repeatBeats[r] * rate * 60.0 / songBpm;
        for (int i = 0; i < n; ++i)
        {
            repeatPhase += 1.0;
            if (repeatPhase >= every)
            {
                repeatPhase -= every;
                for (int pad = 0; pad < numPads && numTriggers < (int) triggers.size(); ++pad)
                    if (padHeld[(size_t) pad])
                        triggers[(size_t) numTriggers++] = { i, pad, juce::jmax (0.3f, heldVelocity[(size_t) ((36 + pad) & 127)]) };
            }
        }
    }

    int done = 0;
    auto renderTo = [&] (int until)
    {
        for (int i = done; i < until; ++i)
            for (auto& v : voices)
                if (v.active)
                {
                    const auto s = renderVoice (v);
                    const auto part = juce::jlimit (0, outputs - 1, v.part);
                    pl[part][i] += s * v.gainL;
                    pr[part][i] += s * v.gainR;
                }
        if (until > done)
        {
            float* l[outputs], *r[outputs];
            for (int k = 0; k < outputs; ++k) { l[k] = pl[k] + done; r[k] = pr[k] + done; }
            sampleVoices.render (l, r, outputs, until - done);
        }
        done = juce::jmax (done, until);
    };
    // events in time order: MIDI, sequencer steps and repeats
    auto it = midi.begin();
    int t = 0;
    while (true)
    {
        int nextMidi = it != midi.end() ? juce::jlimit (0, n, (*it).samplePosition) : n + 1;
        int nextTrig = n + 1, which = -1;
        for (int k = 0; k < numTriggers; ++k)
            if (triggers[(size_t) k].at < nextTrig) { nextTrig = triggers[(size_t) k].at; which = k; }
        const auto next = juce::jmin (nextMidi, nextTrig);
        if (next > n) break;
        renderTo (next);
        if (nextTrig <= nextMidi && which >= 0)
        {
            startVoice (triggers[(size_t) which].pad, triggers[(size_t) which].velocity);
            noteLevel[(size_t) ((36 + triggers[(size_t) which].pad) & 127)].store (1.0f);
            triggers[(size_t) which] = triggers[(size_t) --numTriggers];
        }
        else
        {
            const auto msg = (*it).getMessage();
            const auto pad = msg.getNoteNumber() - 36;
            if (msg.isNoteOn()) hit (pad, (float) msg.getVelocity() / 127.0f);
            else if (msg.isNoteOff()) release (pad);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { for (auto& v : voices) v.choking = true; sampleVoices.allOff(); padHeld.fill (false); }
            ++it;
        }
        juce::ignoreUnused (t);
    }
    renderTo (n);
    const auto master = dsp::dbToGain (p (indexOf ("master")));
    for (int k = 0; k < outputs; ++k)
    {
        juce::FloatVectorOperations::multiply (pl[k], master, n);
        juce::FloatVectorOperations::multiply (pr[k], master, n);
    }
    for (auto& level : noteLevel)
        level.store (level.load (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
}

juce::StringArray BeatPad::screenLines() const
{
    const auto sel = juce::jlimit (0, numPads - 1, choice (indexOf ("sel")));
    const auto pre = "pad" + juce::String (sel);
    const juce::String banks = "ABCD";
    return { kitNames()[juce::jlimit (0, (int) std::size (kits) - 1, choice (indexOf ("kit")))],
             juce::String (banks[sel / padsPerBank]) + juce::String (sel % padsPerBank + 1).paddedLeft ('0', 2) + " " + padName (sel) + " " + padSound (sel),
             (p (indexOf (pre + "Tune")) >= 0 ? "+" : "") + juce::String (p (indexOf (pre + "Tune")), 1),
             juce::String (juce::roundToInt (p (indexOf (pre + "Decay")) * 100.0f)) + "%",
             "SWING " + juce::String (juce::roundToInt (p (indexOf ("swing")))) + "%",
             "REPEAT " + params()[(size_t) indexOf ("repeat")].choices[choice (indexOf ("repeat"))] };
}

} // namespace riff::studio
