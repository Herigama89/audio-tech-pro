#include "StarterTemplates.h"
#include "Project.h"
#include "Identifiers.h"
#include "StudioState.h"

namespace riff::starters
{
namespace
{
    struct Builder
    {
        juce::ValueTree project;

        juce::ValueTree audio (const juce::String& name, juce::ValueTree into = {})
        {
            auto t = model::addTrack (project, name, nullptr);
            route (t, into);
            return t;
        }
        juce::ValueTree studio (const juce::String& kind, const juce::String& name, juce::ValueTree into = {})
        {
            auto t = model::addTrack (project, name, nullptr, -1, model::instrumentKind);
            t.setProperty (ids::instrument, studiostate::prefix + kind, nullptr);
            studiostate::getOrCreate (t, nullptr);
            route (t, into);
            return t;
        }
        juce::ValueTree synth (const juce::String& name, juce::ValueTree into = {})
        {
            auto t = model::addTrack (project, name, nullptr, -1, model::instrumentKind);
            t.setProperty (ids::instrument, "synth", nullptr);
            route (t, into);
            return t;
        }
        juce::ValueTree bus (const juce::String& name, std::initializer_list<const char*> effects = {})
        {
            auto t = model::addTrack (project, name, nullptr, -1, model::busKind);
            insert (t, effects);
            return t;
        }
        void insert (const juce::ValueTree& track, std::initializer_list<const char*> effects)
        {
            int i = 0;
            for (const auto* e : effects)
                model::addBuiltinInsert (model::chainFor (project, track[ids::id].toString()), e, i++, nullptr);
        }
        void send (const juce::ValueTree& track, const juce::ValueTree& to, double level)
        {
            model::addSend (model::chainFor (project, track[ids::id].toString()), to[ids::id].toString(), level, nullptr);
        }
        /** Moved after the other tracks (the buses are made first so the tracks can send to them). */
        void last (const juce::ValueTree& t)
        {
            int end = -1;
            for (int i = 0; i < project.getNumChildren(); ++i)
                if (project.getChild (i).hasType (ids::TRACK))
                    end = i;
            project.moveChild (project.indexOf (t), end, nullptr);
        }
        static void route (juce::ValueTree t, const juce::ValueTree& into)
        {
            if (into.isValid())
                t.setProperty (ids::output, into[ids::id].toString(), nullptr);
        }
    };

    std::vector<juce::Colour> lanes (int n)
    {
        const auto& palette = model::trackPalette();
        std::vector<juce::Colour> c;
        for (int i = 0; i < n; ++i)
            c.push_back (palette[i % palette.size()]);
        return c;
    }
}

const std::vector<Starter>& all()
{
    static const std::vector<Starter> list {
        { "empty",      "Empty",          "No tracks",                                 "Blank Project",      {} },
        { "audio",      "Audio",          "1 audio track",                             "Blank Project",      lanes (1) },
        { "instrument", "Instrument",     "1 software instrument",                     "Blank Project",      lanes (1) },
        { "vocal",      "Vocal Session",  "Lead, doubles, ad-libs, verb and delay",    "Blank Project",      lanes (6) },
        { "beat",       "Beat Starter",   "Drums, beat pad, 808, keys, pad",           "Music Production",   lanes (5) },
        { "songwriter", "Song Writer",    "Guitar, keys, vocal, drums",                "Music Production",   lanes (5) },
        { "electronic", "Electronic",     "Beat pad, bass, lead, pad, FX bus",         "Music Production",   lanes (5) },
        { "band",       "Live Band",      "8 mics, a drum bus and a reverb",           "Recording",          lanes (10) },
        { "podcast",    "Podcast",        "Host, 2 guests, a dialogue bus",            "Vocals & Podcast",   lanes (4) },
        { "voiceover",  "Voice-over",     "1 voice with EQ, compressor and limiter",   "Vocals & Podcast",   lanes (1) },
        { "mixing",     "Mixing",         "16 tracks, drum/music/vocal buses, FX",     "Mixing & Mastering", lanes (8) },
        { "mastering",  "Mastering",      "1 stereo track, EQ, compressor, limiter",   "Mixing & Mastering", lanes (1) },
    };
    return list;
}

juce::StringArray categories()
{
    juce::StringArray c;
    for (const auto& s : all())
        c.addIfNotAlreadyThere (s.category);
    return c;
}

juce::ValueTree build (const juce::String& id, const juce::String& projectName)
{
    Builder b { model::createProject (projectName.isNotEmpty() ? projectName : juce::String ("Untitled")) };

    if (id == "audio")
        b.audio ("Audio 1");
    else if (id == "instrument")
        b.studio ("keys", "Keys");
    else if (id == "vocal")
    {
        auto verb = b.bus ("Vocal Verb", { "reverb" });
        auto delay = b.bus ("Vocal Delay", { "delay" });
        auto lead = b.audio ("Lead Vocal");
        b.insert (lead, { "channelEq", "compressor" });
        for (auto name : { "Double L", "Double R", "Ad-libs" })
        {
            auto t = b.audio (name);
            b.send (t, verb, 0.35);
        }
        b.send (lead, verb, 0.3);
        b.send (lead, delay, 0.2);
        b.last (verb);
        b.last (delay);
    }
    else if (id == "beat")
    {
        b.studio ("drums", "Drums");
        b.studio ("beatpad", "Beat Pad");
        b.studio ("bass", "808");
        b.studio ("keys", "Keys");
        b.synth ("Pad");
    }
    else if (id == "songwriter")
    {
        auto verb = b.bus ("Reverb", { "reverb" });
        b.send (b.audio ("Acoustic Guitar"), verb, 0.2);
        b.studio ("keys", "Keys");
        auto vocal = b.audio ("Vocal");
        b.insert (vocal, { "channelEq", "compressor" });
        b.send (vocal, verb, 0.3);
        b.studio ("drums", "Drums");
        b.last (verb);
    }
    else if (id == "electronic")
    {
        auto fx = b.bus ("FX", { "delay", "reverb" });
        b.studio ("beatpad", "Beat Pad");
        b.send (b.synth ("Bass"), fx, 0.05);
        b.send (b.synth ("Lead"), fx, 0.3);
        b.send (b.synth ("Pad"), fx, 0.4);
        b.last (fx);
    }
    else if (id == "band")
    {
        auto drums = b.bus ("Drum Bus", { "compressor" });
        auto verb = b.bus ("Room", { "reverb" });
        for (auto name : { "Kick", "Snare", "Overhead L", "Overhead R" })
            b.send (b.audio (name, drums), verb, 0.15);
        b.audio ("Bass DI");
        for (auto name : { "Guitar 1", "Guitar 2" })
            b.send (b.audio (name), verb, 0.15);
        auto vocal = b.audio ("Vocal");
        b.insert (vocal, { "channelEq", "compressor" });
        b.send (vocal, verb, 0.25);
        b.last (drums);
        b.last (verb);
    }
    else if (id == "podcast")
    {
        auto dialogue = b.bus ("Dialogue", { "compressor", "limiter" });
        for (auto name : { "Host", "Guest 1", "Guest 2" })
            b.insert (b.audio (name, dialogue), { "channelEq", "compressor" });
        b.last (dialogue);
    }
    else if (id == "voiceover")
        b.insert (b.audio ("Voice"), { "channelEq", "compressor", "limiter" });
    else if (id == "mixing")
    {
        auto drums = b.bus ("Drums", { "compressor" });
        auto music = b.bus ("Music");
        auto vocals = b.bus ("Vocals", { "compressor" });
        auto verb = b.bus ("Reverb", { "reverb" });
        auto delay = b.bus ("Delay", { "delay" });
        for (auto name : { "Kick", "Snare", "Hi-Hat", "Toms", "Overheads", "Room" })
            b.audio (name, drums);
        for (auto name : { "Bass", "Guitar L", "Guitar R", "Keys", "Synth", "Strings" })
            b.send (b.audio (name, music), verb, 0.12);
        for (auto name : { "Lead Vocal", "Double", "Harmony", "Ad-libs" })
        {
            auto t = b.audio (name, vocals);
            b.send (t, verb, 0.2);
            b.send (t, delay, 0.12);
        }
        for (auto* t : { &drums, &music, &vocals, &verb, &delay })
            b.last (*t);
    }
    else if (id == "mastering")
        b.insert (b.audio ("Mix"), { "channelEq", "compressor", "limiter" });

    return b.project;
}
}
