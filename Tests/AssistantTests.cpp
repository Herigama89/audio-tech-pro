#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "ai/Assistant.h"
#include "ai/AssistantTools.h"
#include "model/Project.h"
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/ProjectPlayer.h"
#include "engine/ConsolePlugins.h"

#include <deque>
#include <set>

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    /** Answers requests from a script, at once, and remembers what was sent. */
    struct ScriptedTransport final : ai::Transport
    {
        std::deque<std::pair<int, juce::String>> replies;
        juce::StringArray sent;
        bool holdReplies = false;
        std::function<void (int, const juce::String&)> pending;

        void post (const juce::String& body, std::function<void (int, const juce::String&)> done) override
        {
            sent.add (body);
            if (holdReplies)
            {
                pending = std::move (done);
                return;
            }
            REQUIRE_FALSE (replies.empty());
            auto [status, reply] = replies.front();
            replies.pop_front();
            done (status, reply);
        }
    };

    juce::String textReply (const juce::String& text)
    {
        return R"({"type":"message","role":"assistant","content":[{"type":"text","text":")" + text
               + R"("}],"stop_reason":"end_turn"})";
    }

    juce::String toolReply (const juce::String& id, const juce::String& name, const juce::String& inputJson, const juce::String& text = {})
    {
        juce::String content;
        if (text.isNotEmpty())
            content << R"({"type":"text","text":")" << text << R"("},)";
        content << R"({"type":"tool_use","id":")" << id << R"(","name":")" << name << R"(","input":)" << inputJson << "}";
        return R"({"type":"message","role":"assistant","content":[)" + content + R"(],"stop_reason":"tool_use"})";
    }

    struct Fixture
    {
        juce::ValueTree project = model::createProject ("Song");
        juce::UndoManager um;
        double playhead = 0.0;
        bool playing = false;
        ai::AssistantTools tools { project, um, makeHooks() };
        ScriptedTransport transport;
        ai::Assistant assistant { transport, tools };

        ai::AppHooks makeHooks()
        {
            ai::AppHooks h;
            h.playheadSeconds = [this] { return playhead; };
            h.setPlayhead = [this] (double t) { playhead = t; };
            h.setPlaying = [this] (bool on) { playing = on; };
            h.isPlaying = [this] { return playing; };
            h.commandNames = [] { return juce::StringArray { "Quantize", "Undo" }; };
            h.runCommand = [] (const juce::String& name) { return name == "Quantize"; };
            h.manual = [] { return juce::String ("The manual."); };
            h.keyCommands = [] { return juce::String ("Play or Stop: Space"); };
            return h;
        }

        juce::var lastRequest() const { return juce::JSON::parse (transport.sent[transport.sent.size() - 1]); }

        ai::ToolResult run (const juce::String& tool, const juce::String& inputJson)
        {
            return tools.run (tool, juce::JSON::parse (inputJson));
        }
    };

    int numNotes (const juce::ValueTree& track)
    {
        int n = 0;
        for (const auto& region : track)
            n += region.getNumChildren();
        return n;
    }
}

//==============================================================================
TEST_CASE ("Assistant: a question gets an answer, with the project and tools in the request", "[ai]")
{
    Fixture f;
    model::addTrack (f.project, "Vocals", nullptr);
    f.assistant.model = "claude-test";
    f.transport.replies.push_back ({ 200, textReply ("Press R to record.") });

    f.assistant.send ("How do I record?");

    CHECK_FALSE (f.assistant.isBusy());
    const auto& t = f.assistant.getTranscript();
    REQUIRE (t.size() == 2);
    CHECK (t[0].kind == ai::Assistant::Entry::Kind::user);
    CHECK (t[1].kind == ai::Assistant::Entry::Kind::assistant);
    CHECK (t[1].text == "Press R to record.");

    const auto request = f.lastRequest();
    CHECK (request["model"].toString() == "claude-test");
    CHECK ((int) request["max_tokens"] > 0);
    const auto system = request["system"][0];
    CHECK (system["text"].toString().contains ("The manual."));
    CHECK (system["text"].toString().contains ("Play or Stop: Space"));
    CHECK (system["cache_control"]["type"].toString() == "ephemeral");
    CHECK (request["tools"].size() > 10);

    const auto first = request["messages"][0];
    CHECK (first["role"].toString() == "user");
    CHECK (first["content"].toString().contains ("<project_state>"));
    CHECK (first["content"].toString().contains ("1. Vocals"));
    CHECK (first["content"].toString().endsWith ("How do I record?"));
}

TEST_CASE ("Assistant: tool calls run, their results go back, and the turn is one undo step", "[ai][undo]")
{
    Fixture f;
    f.transport.replies.push_back ({ 200, toolReply ("call1", "set_tempo", R"({"bpm": 92})", "Setting it.") });
    f.transport.replies.push_back ({ 200, toolReply ("call2", "add_tracks", R"({"tracks":[{"kind":"instrument","name":"Keys"},{"kind":"bus","name":"Verb"}]})") });
    f.transport.replies.push_back ({ 200, textReply ("Done: 92 BPM with a Keys track and a reverb bus.") });

    f.assistant.send ("Tempo 92 and add keys and a reverb bus");

    CHECK_FALSE (f.assistant.isBusy());
    CHECK ((double) f.project[ids::tempo] == 92.0);
    REQUIRE (model::numTracks (f.project) == 2);
    CHECK (f.project.getChild (0)[ids::name].toString() == "Keys");
    CHECK (model::isInstrumentTrack (f.project.getChild (0)));
    CHECK (model::isBusTrack (f.project.getChild (1)));

    // The second request answers call1, the third answers call2.
    REQUIRE (f.transport.sent.size() == 3);
    const auto second = juce::JSON::parse (f.transport.sent[1]);
    const auto messages = second["messages"];
    REQUIRE (messages.size() == 3);
    CHECK (messages[1]["role"].toString() == "assistant");
    CHECK (messages[2]["content"][0]["type"].toString() == "tool_result");
    CHECK (messages[2]["content"][0]["tool_use_id"].toString() == "call1");

    // The transcript shows what was done.
    juce::StringArray actions;
    for (const auto& e : f.assistant.getTranscript())
        if (e.kind == ai::Assistant::Entry::Kind::action)
            actions.add (e.text);
    CHECK (actions.size() == 2);
    CHECK (actions[0].contains ("92"));

    // One undo takes it all back.
    f.um.undo();
    CHECK ((double) f.project[ids::tempo] == 120.0);
    CHECK (model::numTracks (f.project) == 0);
}

TEST_CASE ("Assistant: an error is shown and the conversation stays valid", "[ai]")
{
    Fixture f;
    f.transport.replies.push_back ({ 401, R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})" });
    f.assistant.send ("Hello");

    CHECK_FALSE (f.assistant.isBusy());
    REQUIRE (f.assistant.getTranscript().size() == 2);
    CHECK (f.assistant.getTranscript()[1].kind == ai::Assistant::Entry::Kind::error);
    CHECK (f.assistant.getTranscript()[1].text.contains ("API key"));

    // The failed message isn't left dangling: the next request starts with the new one.
    f.transport.replies.push_back ({ 200, textReply ("Hi!") });
    f.assistant.send ("Hello again");
    const auto messages = f.lastRequest()["messages"];
    REQUIRE (messages.size() == 1);
    CHECK (messages[0]["content"].toString().endsWith ("Hello again"));

    CHECK (ai::errorMessage (0, {}).contains ("internet"));
    CHECK (ai::errorMessage (529, "garbage").contains ("overloaded"));
}

TEST_CASE ("Assistant: stopping mid-turn leaves a valid conversation", "[ai]")
{
    Fixture f;
    f.transport.replies.push_back ({ 200, textReply ("First answer.") });
    f.assistant.send ("One");

    f.transport.holdReplies = true;
    f.assistant.send ("Two");
    CHECK (f.assistant.isBusy());
    f.assistant.cancel();
    CHECK_FALSE (f.assistant.isBusy());

    // A late reply to the stopped request is ignored.
    f.transport.pending (200, textReply ("Too late."));
    for (const auto& e : f.assistant.getTranscript())
        CHECK (e.text != "Too late.");

    f.transport.holdReplies = false;
    f.transport.replies.push_back ({ 200, textReply ("Three.") });
    f.assistant.send ("Three");
    const auto messages = f.lastRequest()["messages"];
    REQUIRE (messages.size() == 3);   // One, its answer, Three
    CHECK (messages[2]["content"].toString().endsWith ("Three"));
}

TEST_CASE ("Assistant: a runaway loop of tool calls stops", "[ai]")
{
    Fixture f;
    for (int i = 0; i <= ai::Assistant::maxToolRounds + 1; ++i)
        f.transport.replies.push_back ({ 200, toolReply ("c" + juce::String (i), "get_project", "{}") });

    f.assistant.send ("Loop");
    CHECK_FALSE (f.assistant.isBusy());
    CHECK (f.assistant.getTranscript().back().kind == ai::Assistant::Entry::Kind::info);
    CHECK (f.transport.sent.size() == ai::Assistant::maxToolRounds + 1);
}

//==============================================================================
TEST_CASE ("Tools: definitions are valid and every tool is handled", "[ai][tools]")
{
    Fixture f;
    const auto tools = f.tools.toolDefinitions();
    REQUIRE (tools.isArray());
    std::set<juce::String> names;
    for (const auto& tool : *tools.getArray())
    {
        const auto name = tool["name"].toString();
        INFO (name);
        CHECK (names.insert (name).second);
        CHECK (tool["description"].toString().isNotEmpty());
        CHECK (tool["input_schema"]["type"].toString() == "object");
        CHECK_FALSE (tool["description"].toString().contains ("GENRES"));
        CHECK_FALSE (tool["description"].toString().contains ("EFFECTS"));
        if (name != "analyze_mix")
            CHECK_FALSE (f.tools.run (name, juce::var (new juce::DynamicObject())).forClaude.startsWith ("Error: Unknown tool"));
    }
    CHECK (names.count ("write_midi") == 1);
    CHECK (names.count ("analyze_mix") == 1);
}

TEST_CASE ("Tools: tracks are found by number or name", "[ai][tools]")
{
    Fixture f;
    model::addTrack (f.project, "Lead Vocal", nullptr);
    model::addTrack (f.project, "Backing Vocal", nullptr);
    model::addTrack (f.project, "Drums", nullptr);

    CHECK (f.run ("update_track", R"({"track": 3, "volume_db": -6})").isError == false);
    CHECK_THAT ((double) f.project.getChild (2)[ids::volume], WithinAbs (0.501, 0.001));

    CHECK_FALSE (f.run ("update_track", R"({"track": "drums", "pan": -30, "mute": true})").isError);
    CHECK_THAT ((double) f.project.getChild (2)[ids::pan], WithinAbs (-0.3, 1e-9));
    CHECK ((bool) f.project.getChild (2)[ids::mute]);

    CHECK_FALSE (f.run ("update_track", R"({"track": "lead", "name": "Lead Vox"})").isError);
    CHECK (f.project.getChild (0)[ids::name].toString() == "Lead Vox");

    const auto ambiguous = f.run ("update_track", R"({"track": "vo", "mute": true})");
    CHECK (ambiguous.isError);
    CHECK (ambiguous.forClaude.contains ("More than one"));

    const auto missing = f.run ("update_track", R"({"track": 9, "mute": true})");
    CHECK (missing.isError);
    CHECK (missing.forClaude.contains ("1 Lead Vox"));   // lists the tracks
}

TEST_CASE ("Tools: write_midi puts notes in the right place", "[ai][tools][midi]")
{
    Fixture f;
    f.project.setProperty (ids::tempo, 120.0, nullptr);   // half a second per beat

    const auto result = f.run ("write_midi", R"({"new_track_name": "Piano", "start_bar": 3, "length_bars": 2,
        "notes": [ {"pitch": 60, "start": 0, "length": 1, "velocity": 100},
                   {"pitch": 64, "start": 1.5, "length": 0.5},
                   {"pitch": 67, "start": 7.5, "length": 4},
                   {"pitch": 72, "start": 9, "length": 1} ]})");
    CHECK_FALSE (result.isError);
    CHECK (result.forClaude.contains ("Skipped 1"));

    REQUIRE (model::numTracks (f.project) == 1);
    const auto track = f.project.getChild (0);
    CHECK (model::isInstrumentTrack (track));
    const auto region = track.getChild (0);
    CHECK (model::isMidiRegion (region));
    CHECK_THAT ((double) region[ids::start], WithinAbs (4.0, 1e-9));    // bar 3 = 8 beats = 4 s
    CHECK_THAT ((double) region[ids::length], WithinAbs (4.0, 1e-9));   // 2 bars

    REQUIRE (region.getNumChildren() == 3);
    CHECK ((int) region.getChild (0)[ids::pitch] == 60);
    CHECK ((int) region.getChild (0)[ids::velocity] == 100);
    CHECK_THAT ((double) region.getChild (1)[ids::start], WithinAbs (0.75, 1e-9));
    CHECK_THAT ((double) region.getChild (2)[ids::length], WithinAbs (0.25, 1e-9));   // cut at the region's end

    // Replacing writes over the old region; get_midi reads it back in beats.
    CHECK_FALSE (f.run ("write_midi", R"({"track": "Piano", "start_bar": 3, "length_bars": 1, "replace_existing": true,
        "notes": [ {"pitch": 48, "start": 0, "length": 2} ]})").isError);
    CHECK (track.getNumChildren() == 1);
    const auto midi = juce::JSON::parse (f.run ("get_midi", R"({"track": 1})").forClaude);
    const auto note = midi["regions"][0]["notes_pitch_start_length_velocity"][0];
    CHECK ((int) note[0] == 48);
    CHECK ((double) note[2] == 2.0);

    // Not on an audio track.
    model::addTrack (f.project, "Guitar", nullptr);
    CHECK (f.run ("write_midi", R"({"track": "Guitar", "start_bar": 1, "length_bars": 1, "notes": []})").isError);
}

TEST_CASE ("Tools: mixing settings", "[ai][tools][mixer]")
{
    Fixture f;
    model::addTrack (f.project, "Kick", nullptr);
    model::addTrack (f.project, "Verb", nullptr, -1, model::busKind);

    CHECK_FALSE (f.run ("set_console", R"({"track": "Kick", "lf": 4, "hf": -20, "dynamics": 3.5, "eq_in": false})").isError);
    const auto kick = f.project.getChild (0);
    CHECK ((double) kick[ids::consoleLF] == 4.0);
    CHECK ((double) kick[ids::consoleHF] == -15.0);   // limited to the knob's range
    CHECK ((double) kick[ids::consoleDynamics] == 3.5);
    CHECK_FALSE ((bool) kick[ids::consoleIn]);

    CHECK_FALSE (f.run ("add_effect", R"({"track": "master", "effect": "limiter", "params": {"ceiling": -1, "bogus": 3}})").isError);
    const auto master = model::insertsOf (model::findChain (f.project, model::masterOwner));
    REQUIRE (master.size() == 1);
    CHECK ((double) master[0]["ceiling"] == -1.0);

    CHECK_FALSE (f.run ("set_effect", R"({"track": "master", "slot": 1, "params": {"release": 5000}, "bypass": true})").isError);
    CHECK ((double) master[0]["release"] == 1000.0);
    CHECK ((bool) master[0][ids::bypass]);
    CHECK (f.run ("set_effect", R"({"track": "master", "slot": 2, "params": {}})").isError);

    CHECK_FALSE (f.run ("add_effect", R"({"track": "Verb", "effect": "reverb"})").isError);
    CHECK_FALSE (f.run ("add_send", R"({"track": 1, "bus": "Verb", "level_db": -12})").isError);
    CHECK_FALSE (f.run ("add_send", R"({"track": 1, "bus": "Verb", "level_db": -6})").isError);   // changes it, no second send
    const auto sends = model::sendsOf (model::findChain (f.project, kick[ids::id].toString()));
    REQUIRE (sends.size() == 1);
    CHECK_THAT ((double) sends[0][ids::level], WithinAbs (0.501, 0.001));
    CHECK (f.run ("add_send", R"({"track": 2, "bus": 1})").isError);   // Kick isn't a bus

    CHECK_FALSE (f.run ("set_bus_compressor", R"({"on": true, "threshold_db": -12, "ratio": 3.5, "attack_ms": 25, "release": "Auto", "mix_percent": 60})").isError);
    CHECK ((bool) f.project[ids::busCompOn]);
    CHECK ((double) f.project[ids::busCompThreshold] == -12.0);
    CHECK ((int) f.project[ids::busCompRatio] == 1);     // 4:1 is the nearest
    CHECK ((int) f.project[ids::busCompAttack] == 5);    // 30 ms
    CHECK ((int) f.project[ids::busCompRelease] == 4);   // Auto
    CHECK_THAT ((double) f.project[ids::busCompMix], WithinAbs (0.6, 1e-9));

    CHECK_FALSE (f.run ("remove_effect", R"({"track": "master", "slot": 1})").isError);
    CHECK (model::insertsOf (model::findChain (f.project, model::masterOwner)).isEmpty());

    // get_project describes it all as JSON.
    const auto description = juce::JSON::parse (f.run ("get_project", "{}").forClaude);
    CHECK (description["tracks"].size() == 2);
    CHECK ((double) description["tracks"][0]["console"]["lf"] == 4.0);
    CHECK (description["tracks"][0]["sends"][0]["bus"].toString() == "2 Verb");
    CHECK (description["tracks"][1]["inserts"][0]["effect"].toString() == "reverb");
    CHECK (description["master"]["bus_compressor"]["ratio"].toString() == "4");
}

TEST_CASE ("Tools: transport, cycle, metronome, commands and the generator", "[ai][tools]")
{
    Fixture f;
    f.project.setProperty (ids::tempo, 120.0, nullptr);

    CHECK_FALSE (f.run ("transport", R"({"action": "go_to_bar", "bar": 5})").isError);
    CHECK_THAT (f.playhead, WithinAbs (8.0, 1e-9));
    CHECK_FALSE (f.run ("transport", R"({"action": "play"})").isError);
    CHECK (f.playing);

    CHECK_FALSE (f.run ("set_cycle", R"({"on": true, "start_bar": 5, "end_bar": 9})").isError);
    CHECK ((bool) f.project[ids::cycleEnabled]);
    CHECK_THAT ((double) f.project[ids::cycleStart], WithinAbs (8.0, 1e-9));
    CHECK_THAT ((double) f.project[ids::cycleEnd], WithinAbs (16.0, 1e-9));
    CHECK (f.run ("set_cycle", R"({"on": true, "start_bar": 5, "end_bar": 5})").isError);

    CHECK_FALSE (f.run ("set_metronome", R"({"on": true, "count_in": true})").isError);
    CHECK ((bool) f.project[ids::metronome]);

    CHECK_FALSE (f.run ("run_command", R"({"command": "Quantize"})").isError);
    CHECK (f.run ("run_command", R"({"command": "Launch Rockets"})").isError);

    const auto generated = f.run ("generate_pattern", R"({"genre": "house", "key": "F#", "scale": "minor", "bars": 4, "parts": ["drums", "bass"], "start_bar": 9})");
    CHECK_FALSE (generated.isError);
    CHECK (generated.forUser.contains ("F# minor"));
    REQUIRE (model::numTracks (f.project) == 2);
    CHECK_THAT ((double) f.project.getChild (0).getChild (0)[ids::start], WithinAbs (16.0, 1e-9));
    CHECK (numNotes (f.project.getChild (0)) > 0);
    CHECK (f.run ("generate_pattern", R"({"genre": "polka"})").isError);

    CHECK_FALSE (f.run ("delete_tracks", R"({"tracks": [1, 2]})").isError);
    CHECK (model::numTracks (f.project) == 0);
}

//==============================================================================
TEST_CASE ("Plugin parameters can be set to the value a person would type", "[ai][console-mode]")
{
    juce::AudioParameterFloat gain ({ "gain", 1 }, "Gain", juce::NormalisableRange<float> (-15.0f, 15.0f), 0.0f,
                                    juce::AudioParameterFloatAttributes().withLabel ("dB"));
    CHECK (console::setParameterFromText (gain, "-6").isNotEmpty());
    CHECK_THAT (gain.get(), WithinAbs (-6.0, 0.05));
    console::setParameterFromText (gain, "4.5 dB");
    CHECK_THAT (gain.get(), WithinAbs (4.5, 0.05));

    // A skewed range (attack times)
    juce::NormalisableRange<float> attackRange (0.1f, 30.0f);
    attackRange.setSkewForCentre (3.0f);
    juce::AudioParameterFloat attack ({ "attack", 1 }, "Attack", attackRange, 1.0f);
    console::setParameterFromText (attack, "10");
    CHECK_THAT (attack.get(), WithinAbs (10.0, 0.1));

    juce::AudioParameterChoice release ({ "release", 1 }, "Release", { "0.1", "0.3", "0.6", "1.2", "Auto" }, 0);
    CHECK (console::setParameterFromText (release, "Auto") == "Auto");
    CHECK (release.getIndex() == 4);
    console::setParameterFromText (release, "0.6");
    CHECK (release.getIndex() == 2);
    CHECK (console::setParameterFromText (release, "Never").isEmpty());
}

TEST_CASE ("Mix measurement: levels and the low/mid/high split", "[ai][analysis]")
{
    auto measure = [] (double frequency, float amplitude)
    {
        LevelStats s;
        s.prepare (48000.0);
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / 48000.0);
        s.add (l.data(), r.data(), (int) l.size());
        return s;
    };

    const auto bass = measure (50.0, 0.5f);
    CHECK_THAT (bass.peakDb(), WithinAbs (-6.02, 0.05));
    CHECK_THAT (bass.rmsDb(), WithinAbs (-9.03, 0.05));
    double low, mid, high;
    bass.bandShares (low, mid, high);
    CHECK (low > 70.0);

    measure (1000.0, 0.5f).bandShares (low, mid, high);
    CHECK (mid > 50.0);

    measure (12000.0, 0.5f).bandShares (low, mid, high);
    CHECK (high > 60.0);

    LevelStats silence;
    CHECK (silence.isSilent());
}

TEST_CASE ("Mix measurement renders the project offline, track by track", "[ai][analysis][engine]")
{
    AudioEngine engine;
    engine.prepare (48000.0, 512);
    AudioFileCache cache { engine.getFormatManager() };
    ProjectPlayer player { engine, cache, nullptr };
    auto project = model::createProject();
    auto loud = model::addTrack (project, "Loud", nullptr);
    auto quiet = model::addTrack (project, "Quiet", nullptr);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atp-mix-" + juce::Uuid().toString());
    dir.createDirectory();
    auto write = [&dir] (const juce::String& name, float value)
    {
        const auto file = dir.getChildFile (name);
        juce::AudioBuffer<float> b (1, 48000);
        for (int i = 0; i < 48000; ++i)
            b.setSample (0, i, value);
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (b, 0, 48000);
        return file;
    };
    model::addRegion (loud, write ("loud.wav", 0.5f), 0.0, 1.0, nullptr);
    model::addRegion (quiet, write ("quiet.wav", 0.05f), 0.0, 1.0, nullptr);
    player.setProject (project);

    engine.setPlayheadSeconds (2.5);
    engine.beginOfflineRender();
    const auto m = engine.measureMix (0.0, 1.0, {});
    engine.endOfflineRender();

    CHECK_THAT (engine.getPlayheadSeconds(), WithinAbs (2.5, 1e-6));   // put back
    CHECK_THAT (m.seconds, WithinAbs (1.0, 1e-6));
    const auto loudSlot = player.slotOf (loud[ids::id].toString());
    const auto quietSlot = player.slotOf (quiet[ids::id].toString());
    REQUIRE (juce::isPositiveAndBelow (loudSlot, (int) m.tracks.size()));
    REQUIRE (juce::isPositiveAndBelow (quietSlot, (int) m.tracks.size()));

    // Centre-panned constant signals at unity: the level is the file's level (pan law aside).
    const auto& a = m.tracks[(size_t) loudSlot];
    const auto& b = m.tracks[(size_t) quietSlot];
    CHECK (a.peakDb() > b.peakDb() + 15.0);
    CHECK_THAT (a.peakDb() - b.peakDb(), WithinAbs (20.0, 0.5));
    CHECK_THAT (m.master.peak, WithinAbs (0.8 * 0.55, 1e-3));   // both tracks through the master fader (0.8)

    dir.deleteRecursively();
}
