#include "AssistantTools.h"
#include "../model/Project.h"
#include "../model/Generator.h"
#include "../engine/BuiltInEffects.h"
#include "../engine/MasterBusCompressor.h"
#include "../engine/ConsoleChannel.h"
#include "../AppInfo.h"

namespace riff::ai
{

namespace
{
    juce::var object() { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& o, const juce::Identifier& name, const juce::var& value)
    {
        if (auto* d = o.getDynamicObject())
            d->setProperty (name, value);
    }

    bool has (const juce::var& input, const char* name)
    {
        if (auto* d = input.getDynamicObject())
            return d->hasProperty (name) && ! d->getProperty (name).isVoid() && ! d->getProperty (name).isUndefined();
        return false;
    }

    double number (const juce::var& input, const char* name, double fallback)
    {
        if (! has (input, name))
            return fallback;
        const auto v = input.getProperty (name, fallback);
        return v.isString() ? v.toString().getDoubleValue() : (double) v;
    }

    bool flag (const juce::var& input, const char* name, bool fallback)
    {
        if (! has (input, name))
            return fallback;
        const auto v = input.getProperty (name, fallback);
        return v.isString() ? (v.toString().equalsIgnoreCase ("true") || v.toString().equalsIgnoreCase ("on")) : (bool) v;
    }

    double round1 (double x) { return std::round (x * 10.0) / 10.0; }
    double round2 (double x) { return std::round (x * 100.0) / 100.0; }

    double gainToDb (double gain) { return gain > 1.0e-5 ? 20.0 * std::log10 (gain) : -100.0; }
    double dbToGain (double db)   { return db <= -99.0 ? 0.0 : std::pow (10.0, db / 20.0); }

    ToolResult ok (const juce::String& forClaude, const juce::String& forUser) { return { forClaude, forUser, false }; }
    ToolResult fail (const juce::String& message) { return { "Error: " + message, {}, true }; }

    juce::String json (const juce::var& v)
    {
        return juce::JSON::toString (v, juce::JSON::FormatOptions().withSpacing (juce::JSON::Spacing::singleLine).withMaxDecimalPlaces (3));
    }

    juce::String pluginName (const juce::String& descriptionXml)
    {
        if (auto xml = juce::parseXML (descriptionXml))
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*xml))
                return d.name;
        }
        return "Plug-in";
    }

    const juce::String toolsJson = R"JSON([
  { "name": "get_project",
    "description": "Everything about the project: tempo, cycle, every track with its regions, volume, pan, console EQ/dynamics, inserts (effects with their settings) and sends, and the master with its bus compressor.",
    "input_schema": { "type": "object", "properties": {} } },

  { "name": "set_tempo",
    "description": "Set the project tempo in BPM. Existing regions keep their position in seconds.",
    "input_schema": { "type": "object", "properties": { "bpm": { "type": "number", "minimum": 20, "maximum": 400 } }, "required": ["bpm"] } },

  { "name": "add_tracks",
    "description": "Add tracks at the bottom. kind: 'audio' (for recording/importing audio), 'instrument' (MIDI, plays the built-in Tech Synth until the person loads a plugin), or 'bus' (aux bus for sends, e.g. a shared reverb).",
    "input_schema": { "type": "object", "properties": {
        "tracks": { "type": "array", "items": { "type": "object", "properties": {
            "kind": { "type": "string", "enum": ["audio", "instrument", "bus"] },
            "name": { "type": "string" } }, "required": ["kind"] } } }, "required": ["tracks"] } },

  { "name": "update_track",
    "description": "Change a track: name, fader (volume_db, 0 = unity, max +6), pan (-100 left .. 100 right), mute, solo, record arm. track is the track number or its name.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] },
        "name": { "type": "string" },
        "volume_db": { "type": "number", "maximum": 6 },
        "pan": { "type": "number", "minimum": -100, "maximum": 100 },
        "mute": { "type": "boolean" }, "solo": { "type": "boolean" }, "armed": { "type": "boolean" } }, "required": ["track"] } },

  { "name": "delete_tracks",
    "description": "Delete tracks (with their regions and effects). Only when the person asked for it.",
    "input_schema": { "type": "object", "properties": { "tracks": { "type": "array", "items": { "type": ["integer", "string"] } } }, "required": ["tracks"] } },

  { "name": "write_midi",
    "description": "Write a MIDI region. Use an existing instrument track (track), or give new_track_name to create one. Note start and length are in beats (quarter notes) from the region's start; pitch is a MIDI note (60 = C4); velocity 1-127. With replace_existing, MIDI regions on that track overlapping the new one are removed first.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] },
        "new_track_name": { "type": "string" },
        "region_name": { "type": "string" },
        "start_bar": { "type": "number", "minimum": 1 },
        "length_bars": { "type": "number", "minimum": 0.25 },
        "replace_existing": { "type": "boolean" },
        "notes": { "type": "array", "items": { "type": "object", "properties": {
            "pitch": { "type": "integer", "minimum": 0, "maximum": 127 },
            "start": { "type": "number", "minimum": 0 },
            "length": { "type": "number", "exclusiveMinimum": 0 },
            "velocity": { "type": "integer", "minimum": 1, "maximum": 127 } }, "required": ["pitch", "start", "length"] } } },
      "required": ["start_bar", "length_bars", "notes"] } },

  { "name": "get_midi",
    "description": "The notes of a track's MIDI regions (pitch, start and length in beats from the region start, velocity).",
    "input_schema": { "type": "object", "properties": { "track": { "type": ["integer", "string"] } }, "required": ["track"] } },

  { "name": "delete_regions",
    "description": "Delete a track's regions: all of them, or those overlapping start_bar..end_bar.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] },
        "start_bar": { "type": "number" }, "end_bar": { "type": "number" } }, "required": ["track"] } },

  { "name": "generate_pattern",
    "description": "The app's genre generator: writes drums, bass, chords and melody (any of them) as MIDI on new instrument tracks. Genres: GENRES.",
    "input_schema": { "type": "object", "properties": {
        "genre": { "type": "string" },
        "key": { "type": "string", "description": "Root note, e.g. 'C', 'F#', 'Bb'" },
        "scale": { "type": "string", "enum": ["major", "minor"] },
        "bars": { "type": "integer", "enum": [4, 8, 16] },
        "parts": { "type": "array", "items": { "type": "string", "enum": ["drums", "bass", "chords", "melody"] } },
        "variation": { "type": "number", "minimum": 0, "maximum": 1 },
        "start_bar": { "type": "number", "minimum": 1 },
        "use_genre_tempo": { "type": "boolean" } }, "required": ["genre"] } },

  { "name": "set_console",
    "description": "A track's console strip: EQ gains in dB (-15..15) for hf (8 kHz), hmf (2.5 kHz), lmf (400 Hz), lf (100 Hz), dynamics amount (0..10), and the eq_in / dynamics_in switches. In console mode this turns the person's channel strip plugin's knobs.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] },
        "hf": { "type": "number" }, "hmf": { "type": "number" }, "lmf": { "type": "number" }, "lf": { "type": "number" },
        "dynamics": { "type": "number" }, "eq_in": { "type": "boolean" }, "dynamics_in": { "type": "boolean" } }, "required": ["track"] } },

  { "name": "set_bus_compressor",
    "description": "The master bus compressor. Built-in: threshold_db (-30..0), ratio (2, 4 or 10), attack_ms (0.1, 0.3, 1, 3, 10, 30), release ('0.1', '0.3', '0.6', '1.2' seconds or 'Auto'), makeup_db (0..15), mix_percent (0..100), on. In console mode the same names set the person's own bus compressor plugin.",
    "input_schema": { "type": "object", "properties": {
        "on": { "type": "boolean" }, "threshold_db": { "type": "number" }, "ratio": { "type": "number" },
        "attack_ms": { "type": "number" }, "release": { "type": "string" }, "makeup_db": { "type": "number" },
        "mix_percent": { "type": "number" } } } },

  { "name": "set_master_volume",
    "description": "The master fader in dB (0 = unity, max +6).",
    "input_schema": { "type": "object", "properties": { "volume_db": { "type": "number", "maximum": 6 } }, "required": ["volume_db"] } },

  { "name": "add_effect",
    "description": "Add a built-in effect at the end of a track's insert chain (track number or name, or 'master'). Effects and their params (units in brackets): EFFECTS. Leave out params to use defaults.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] },
        "effect": { "type": "string", "enum": ["eq", "compressor", "delay", "reverb", "limiter"] },
        "params": { "type": "object" } }, "required": ["track", "effect"] } },

  { "name": "set_effect",
    "description": "Change an insert on a track (or 'master') by its slot number (1 = first, as in get_project). For built-in effects, params use the ids listed in add_effect. For the person's plugins, params maps the plugin's parameter names (as get_project shows them) to values as the plugin displays them, e.g. {\"Threshold\": \"-12\", \"Release\": \"Auto\"}. bypass switches it out.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] }, "slot": { "type": "integer", "minimum": 1 },
        "params": { "type": "object" }, "bypass": { "type": "boolean" } }, "required": ["track", "slot"] } },

  { "name": "remove_effect",
    "description": "Remove an insert from a track (or 'master') by slot number.",
    "input_schema": { "type": "object", "properties": { "track": { "type": ["integer", "string"] }, "slot": { "type": "integer", "minimum": 1 } }, "required": ["track", "slot"] } },

  { "name": "add_send",
    "description": "Send a track to an aux bus track (post-fader) at a level in dB. Create the bus with add_tracks first if needed, and put an effect such as reverb on it.",
    "input_schema": { "type": "object", "properties": {
        "track": { "type": ["integer", "string"] }, "bus": { "type": ["integer", "string"] }, "level_db": { "type": "number" } }, "required": ["track", "bus"] } },

  { "name": "transport",
    "description": "play, stop, or go_to_bar (with bar).",
    "input_schema": { "type": "object", "properties": {
        "action": { "type": "string", "enum": ["play", "stop", "go_to_bar"] }, "bar": { "type": "number", "minimum": 1 } }, "required": ["action"] } },

  { "name": "set_cycle",
    "description": "The cycle (loop) range in bars and whether it's on. end_bar is where the loop ends (exclusive): bars 5-8 is start_bar 5, end_bar 9.",
    "input_schema": { "type": "object", "properties": {
        "on": { "type": "boolean" }, "start_bar": { "type": "number", "minimum": 1 }, "end_bar": { "type": "number" } }, "required": ["on"] } },

  { "name": "set_metronome",
    "description": "Turn the click on or off, and the one-bar count-in before recording.",
    "input_schema": { "type": "object", "properties": { "on": { "type": "boolean" }, "count_in": { "type": "boolean" } } } },

  { "name": "run_command",
    "description": "Run one of the app's menu commands by name, acting on the current selection as if the person chose it: COMMANDS.",
    "input_schema": { "type": "object", "properties": { "command": { "type": "string" } }, "required": ["command"] } },

  { "name": "analyze_mix",
    "description": "Render the song (or start_bar..end_bar) offline and measure every track (post-fader) and the master: peak and RMS in dBFS, and the share of energy in the lows (< ~200 Hz), mids and highs (> ~4 kHz). Takes a few seconds. Muted tracks measure as silent.",
    "input_schema": { "type": "object", "properties": { "start_bar": { "type": "number", "minimum": 1 }, "end_bar": { "type": "number" } } } }
])JSON";
}

//==============================================================================
AssistantTools::AssistantTools (juce::ValueTree& p, juce::UndoManager& u, AppHooks h)
    : project (p), um (u), hooks (std::move (h))
{
}

juce::var AssistantTools::toolDefinitions()
{
    juce::StringArray genreNames;
    for (const auto& g : generator::genres())
        genreNames.add (g.name);

    juce::StringArray effects;
    for (const auto& e : builtInEffects())
    {
        juce::StringArray params;
        for (const auto& p : e.params)
            params.add (p.id + " [" + juce::String (p.min) + ".." + juce::String (p.max) + p.suffix.trim() + "]");
        effects.add (e.type + " (" + params.joinIntoString (", ") + ")");
    }

    const auto commands = hooks.commandNames ? hooks.commandNames() : juce::StringArray();

    auto text = toolsJson.replace ("GENRES", genreNames.joinIntoString (", "))
                         .replace ("EFFECTS", effects.joinIntoString ("; ").replace ("\"", "'"))
                         .replace ("COMMANDS", commands.joinIntoString (", ").replace ("\"", "'"));
    return juce::JSON::parse (text);
}

juce::String AssistantTools::staticContext()
{
    juce::String text;
    if (hooks.manual)
        text << hooks.manual() << "\n\n";
    if (hooks.keyCommands)
        text << "The person's key commands right now:\n" << hooks.keyCommands() << "\n";
    return text;
}

void AssistantTools::beginTurn (const juce::String& request)
{
    um.beginNewTransaction ("Claude: " + request.substring (0, 40));
}

void AssistantTools::endTurn()
{
    um.beginNewTransaction();
}

//==============================================================================
double AssistantTools::barToSeconds (double bar) const
{
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    return juce::jmax (0.0, (bar - 1.0) * beatsPerBar * model::secondsPerBeat (project));
}

double AssistantTools::secondsToBar (double seconds) const
{
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    return seconds / (beatsPerBar * model::secondsPerBeat (project)) + 1.0;
}

juce::String AssistantTools::trackLabel (const juce::ValueTree& track) const
{
    return juce::String (project.indexOf (track) + 1) + " " + track[ids::name].toString();
}

juce::String AssistantTools::instrumentName (const juce::ValueTree& track) const
{
    if (! model::isInstrumentTrack (track))
        return {};
    if (track[ids::instrument].toString() == "plugin")
        return pluginName (track[ids::pluginDescription].toString());
    return app::synthName;
}

juce::ValueTree AssistantTools::findTrack (const juce::var& reference, juce::String& error) const
{
    const auto count = model::numTracks (project);
    auto list = [this, count]
    {
        juce::StringArray names;
        for (int i = 0; i < count; ++i)
            names.add (trackLabel (project.getChild (i)));
        return names.joinIntoString (", ");
    };

    const auto text = reference.toString().trim();
    if (reference.isInt() || reference.isInt64() || reference.isDouble()
        || (text.isNotEmpty() && text.containsOnly ("0123456789")))
    {
        const auto index = text.getIntValue() - 1;
        if (juce::isPositiveAndBelow (index, count))
            return project.getChild (index);
        error = "There's no track " + text + ". Tracks: " + (count > 0 ? list() : juce::String ("none"));
        return {};
    }

    for (int i = 0; i < count; ++i)
        if (project.getChild (i)[ids::name].toString().equalsIgnoreCase (text))
            return project.getChild (i);

    juce::ValueTree found;
    int matches = 0;
    for (int i = 0; i < count; ++i)
        if (project.getChild (i)[ids::name].toString().containsIgnoreCase (text))
        {
            found = project.getChild (i);
            ++matches;
        }

    if (matches == 1)
        return found;

    error = (matches > 1 ? "More than one track matches \"" + text + "\"" : "No track is called \"" + text + "\"")
            + ". Tracks: " + (count > 0 ? list() : juce::String ("none"));
    return {};
}

//==============================================================================
juce::var AssistantTools::describeChain (const juce::String& owner)
{
    juce::Array<juce::var> inserts;
    const auto chain = model::findChain (project, owner);
    if (! chain.isValid())
        return juce::var (inserts);

    int slot = 1;
    for (const auto& insert : model::insertsOf (chain))
    {
        auto o = object();
        set (o, "slot", slot++);
        set (o, "bypass", (bool) insert[ids::bypass]);

        if (insert[ids::kind].toString() == model::builtinKind)
        {
            const auto type = insert[ids::effect].toString();
            set (o, "effect", type);
            auto params = object();
            if (const auto* info = findBuiltInEffect (type))
                for (const auto& p : info->params)
                    set (params, p.id, round2 ((double) insert.getProperty (p.id, p.defaultValue)));
            set (o, "params", params);
        }
        else
        {
            set (o, "plugin", pluginName (insert[ids::pluginDescription].toString()));
            if (hooks.pluginInstance)
                if (auto* instance = hooks.pluginInstance (insert[ids::id].toString()))
                {
                    auto params = object();
                    int shown = 0;
                    for (auto* p : instance->getParameters())
                    {
                        if (shown++ >= 40)
                            break;
                        auto text = p->getCurrentValueAsText().trim();
                        const auto label = p->getLabel().trim();
                        if (label.isNotEmpty() && ! text.endsWithIgnoreCase (label))
                            text << " " << label;
                        set (params, p->getName (40), text);
                    }
                    set (o, "params", params);
                }
        }
        inserts.add (o);
    }
    return juce::var (inserts);
}

juce::var AssistantTools::describeTrack (const juce::ValueTree& track, int index)
{
    auto t = object();
    set (t, "number", index + 1);
    set (t, "name", track[ids::name]);
    set (t, "kind", track[ids::kind].toString().isEmpty() ? juce::String (model::audioKind) : track[ids::kind].toString());
    if (model::isInstrumentTrack (track))
        set (t, "instrument", instrumentName (track));
    set (t, "volume_db", round1 (gainToDb ((double) track.getProperty (ids::volume, 1.0))));
    set (t, "pan", juce::roundToInt ((double) track.getProperty (ids::pan, 0.0) * 100.0));
    set (t, "mute", (bool) track[ids::mute]);
    set (t, "solo", (bool) track[ids::solo]);
    if (! model::isBusTrack (track))
        set (t, "armed", (bool) track[ids::armed]);

    juce::Array<juce::var> regions;
    for (const auto& region : track)
    {
        if (! region.hasType (ids::REGION))
            continue;
        auto r = object();
        set (r, "name", region[ids::name]);
        set (r, "type", model::isMidiRegion (region) ? "midi" : "audio");
        set (r, "start_bar", round2 (secondsToBar ((double) region[ids::start])));
        set (r, "end_bar", round2 (secondsToBar (model::regionEnd (region))));
        if (model::isMidiRegion (region))
            set (r, "notes", region.getNumChildren());
        regions.add (r);
    }
    set (t, "regions", juce::var (regions));

    // The console strip
    const auto id = track[ids::id].toString();
    auto console = object();
    const auto strip = model::stripPlugin (project, id);
    if (strip.isValid())
    {
        set (console, "plugin", pluginName (strip[ids::pluginDescription].toString()));
        set (console, "plugin_in", ! (bool) strip[ids::bypass]);
    }

    const std::pair<const char*, console::StripControl> controls[] {
        { "hf", console::StripControl::hf }, { "hmf", console::StripControl::hmf }, { "lmf", console::StripControl::lmf },
        { "lf", console::StripControl::lf }, { "dynamics", console::StripControl::dynamics } };
    const std::pair<const char*, juce::Identifier> properties[] {
        { "hf", ids::consoleHF }, { "hmf", ids::consoleHMF }, { "lmf", ids::consoleLMF }, { "lf", ids::consoleLF }, { "dynamics", ids::consoleDynamics } };

    for (size_t i = 0; i < 5; ++i)
    {
        juce::AudioProcessorParameter* p = (strip.isValid() && hooks.stripParameter) ? hooks.stripParameter (id, controls[i].second) : nullptr;
        if (p != nullptr)
            set (console, controls[i].first, p->getCurrentValueAsText());
        else
            set (console, properties[i].first, round1 ((double) track.getProperty (properties[i].second, 0.0)));
    }
    if (! strip.isValid())
    {
        set (console, "eq_in", (bool) track.getProperty (ids::consoleIn, true));
        set (console, "dynamics_in", (bool) track.getProperty (ids::consoleDynIn, true));
    }
    set (t, "console", console);

    set (t, "inserts", describeChain (id));

    juce::Array<juce::var> sends;
    if (const auto chain = model::findChain (project, id); chain.isValid())
        for (const auto& send : model::sendsOf (chain))
        {
            auto s = object();
            const auto bus = model::findById (project, send[ids::target].toString());
            set (s, "bus", bus.isValid() ? trackLabel (bus) : juce::String ("(missing)"));
            set (s, "level_db", round1 (gainToDb ((double) send.getProperty (ids::level, 1.0))));
            sends.add (s);
        }
    if (! sends.isEmpty())
        set (t, "sends", juce::var (sends));

    return t;
}

juce::var AssistantTools::describeProject()
{
    auto p = object();
    set (p, "name", project[ids::name]);
    set (p, "tempo_bpm", round2 ((double) project.getProperty (ids::tempo, 120.0)));
    set (p, "time_signature", juce::String ((int) project.getProperty (ids::timeSigNumerator, 4)) + "/4");
    if (project.hasProperty (ids::keyRoot))
        set (p, "key", model::keyName (project));
    juce::Array<juce::var> markerList;
    for (const auto& m : model::sortedMarkers (project))
    {
        auto o = object();
        set (o, "name", m[ids::name]);
        set (o, "bar", round2 (secondsToBar ((double) m[ids::time])));
        markerList.add (o);
    }
    if (! markerList.isEmpty())
        set (p, "markers", juce::var (markerList));
    set (p, "song_end_bar", round2 (secondsToBar (model::projectEnd (project))));
    if (hooks.playheadSeconds)
        set (p, "playhead_bar", round2 (secondsToBar (hooks.playheadSeconds())));
    if (hooks.isPlaying)
        set (p, "playing", hooks.isPlaying());

    auto cycle = object();
    set (cycle, "on", (bool) project[ids::cycleEnabled]);
    set (cycle, "start_bar", round2 (secondsToBar ((double) project.getProperty (ids::cycleStart, 0.0))));
    set (cycle, "end_bar", round2 (secondsToBar ((double) project.getProperty (ids::cycleEnd, 0.0))));
    set (p, "cycle", cycle);
    set (p, "metronome", (bool) project[ids::metronome]);
    set (p, "count_in", (bool) project[ids::countIn]);

    juce::Array<juce::var> tracks;
    for (int i = 0; i < model::numTracks (project); ++i)
        tracks.add (describeTrack (project.getChild (i), i));
    set (p, "tracks", juce::var (tracks));

    auto master = object();
    set (master, "volume_db", round1 (gainToDb ((double) project.getProperty (ids::masterGain, 0.8))));
    set (master, "inserts", describeChain (model::masterOwner));

    auto comp = object();
    const auto plugin = model::busCompPlugin (project);
    if (plugin.isValid())
    {
        set (comp, "plugin", pluginName (plugin[ids::pluginDescription].toString()));
        set (comp, "in", ! (bool) plugin[ids::bypass]);
        if (hooks.busCompParameter)
        {
            const std::pair<const char*, console::Control> controls[] {
                { "threshold", console::Control::threshold }, { "ratio", console::Control::ratio }, { "attack", console::Control::attack },
                { "release", console::Control::release }, { "makeup", console::Control::makeup }, { "mix", console::Control::mix },
                { "sidechain_filter", console::Control::filter } };
            for (const auto& [name, control] : controls)
                if (auto* param = hooks.busCompParameter (control))
                    set (comp, name, param->getCurrentValueAsText());
        }
    }
    else
    {
        const auto s = MasterBusCompressor::fromProject (project);
        set (comp, "on", s.enabled);
        set (comp, "threshold_db", round1 (s.thresholdDb));
        set (comp, "ratio", MasterBusCompressor::ratioText (s.ratio));
        set (comp, "attack", MasterBusCompressor::attackText (s.attack));
        set (comp, "release", MasterBusCompressor::releaseText (s.release));
        set (comp, "makeup_db", round1 (s.makeupDb));
        set (comp, "mix_percent", juce::roundToInt (s.mix * 100.0f));
    }
    set (master, "bus_compressor", comp);
    set (p, "master", master);
    return p;
}

juce::String AssistantTools::projectState()
{
    juce::String s;
    s << "Tempo " << round2 ((double) project.getProperty (ids::tempo, 120.0)) << " BPM, "
      << (int) project.getProperty (ids::timeSigNumerator, 4) << "/4"
      << (project.hasProperty (ids::keyRoot) ? ", key of " + model::keyName (project) : juce::String()) << ". ";
    if (hooks.playheadSeconds)
        s << "Playhead at bar " << round1 (secondsToBar (hooks.playheadSeconds()))
          << (hooks.isPlaying && hooks.isPlaying() ? " (playing). " : ". ");
    if ((bool) project[ids::cycleEnabled])
        s << "Cycle on: bars " << round2 (secondsToBar ((double) project[ids::cycleStart])) << " to "
          << round2 (secondsToBar ((double) project[ids::cycleEnd])) << ". ";
    s << "Song ends at bar " << round1 (secondsToBar (model::projectEnd (project))) << ".\n";

    const auto count = model::numTracks (project);
    if (count == 0)
        return s + "No tracks yet.";

    s << "Tracks:\n";
    for (int i = 0; i < count; ++i)
    {
        const auto track = project.getChild (i);
        int regions = 0;
        for (const auto& r : track)
            regions += r.hasType (ids::REGION) ? 1 : 0;

        s << (i + 1) << ". " << track[ids::name].toString() << " (" << track[ids::kind].toString();
        if (model::isInstrumentTrack (track))
            s << ", " << instrumentName (track);
        s << ", " << regions << (regions == 1 ? " region" : " regions")
          << ", " << round1 (gainToDb ((double) track.getProperty (ids::volume, 1.0))) << " dB";
        if ((bool) track[ids::mute]) s << ", muted";
        if ((bool) track[ids::solo]) s << ", solo";
        s << ")\n";
    }
    return s.trimEnd();
}

//==============================================================================
void AssistantTools::runTool (const juce::String& name, const juce::var& input, std::function<void (ToolResult)> done)
{
    if (name == "analyze_mix")
    {
        analyzeMix (input, std::move (done));
        return;
    }
    done (run (name, input));
}

ToolResult AssistantTools::run (const juce::String& name, const juce::var& input)
{
    if (name == "get_project")        return ok (json (describeProject()), {});
    if (name == "set_tempo")          return setTempo (input);
    if (name == "add_tracks")         return addTracks (input);
    if (name == "update_track")       return updateTrack (input);
    if (name == "delete_tracks")      return deleteTracks (input);
    if (name == "write_midi")         return writeMidi (input);
    if (name == "get_midi")           return getMidi (input);
    if (name == "delete_regions")     return deleteRegions (input);
    if (name == "generate_pattern")   return generatePattern (input);
    if (name == "set_console")        return setConsole (input);
    if (name == "set_bus_compressor") return setBusCompressor (input);
    if (name == "set_master_volume")  return setMasterVolume (input);
    if (name == "add_effect")         return addEffect (input);
    if (name == "set_effect")         return setEffect (input);
    if (name == "remove_effect")      return removeEffect (input);
    if (name == "add_send")           return addSend (input);
    if (name == "transport")          return transport (input);
    if (name == "set_cycle")          return setCycle (input);
    if (name == "set_metronome")      return setMetronome (input);
    if (name == "run_command")        return runCommand (input);
    return fail ("Unknown tool " + name);
}

//==============================================================================
ToolResult AssistantTools::setTempo (const juce::var& input)
{
    const auto bpm = juce::jlimit (20.0, 400.0, number (input, "bpm", 120.0));
    project.setProperty (ids::tempo, bpm, &um);
    return ok ("Tempo is now " + juce::String (round2 (bpm)) + " BPM.", "Set the tempo to " + juce::String (round2 (bpm)) + " BPM");
}

ToolResult AssistantTools::addTracks (const juce::var& input)
{
    const auto* list = input.getProperty ("tracks", {}).getArray();
    if (list == nullptr || list->isEmpty())
        return fail ("Give at least one track.");

    juce::StringArray added;
    juce::ValueTree last;
    for (const auto& t : *list)
    {
        auto kind = t.getProperty ("kind", "audio").toString().toLowerCase();
        if (kind != model::audioKind && kind != model::instrumentKind && kind != model::busKind)
            kind = model::audioKind;

        auto name = t.getProperty ("name", {}).toString().trim();
        if (name.isEmpty())
            name = (kind == model::instrumentKind ? "Inst " : kind == model::busKind ? "Bus " : "Audio ")
                   + juce::String (model::numTracks (project) + 1);

        last = model::addTrack (project, name, &um, -1, kind);
        added.add (trackLabel (last) + " (" + kind + ")");
    }

    if (hooks.selectTrack && last.isValid())
        hooks.selectTrack (last);

    return ok ("Added: " + added.joinIntoString (", "),
               "Added " + juce::String (added.size()) + (added.size() == 1 ? " track: " : " tracks: ") + added.joinIntoString (", "));
}

ToolResult AssistantTools::updateTrack (const juce::var& input)
{
    juce::String error;
    auto track = findTrack (input.getProperty ("track", {}), error);
    if (! track.isValid())
        return fail (error);

    juce::StringArray changes;
    if (has (input, "name"))
    {
        const auto name = input.getProperty ("name", {}).toString().trim();
        if (name.isNotEmpty())
        {
            track.setProperty (ids::name, name, &um);
            changes.add ("renamed to " + name);
        }
    }
    if (has (input, "volume_db"))
    {
        const auto db = juce::jlimit (-100.0, 6.0, number (input, "volume_db", 0.0));
        track.setProperty (ids::volume, dbToGain (db), &um);
        changes.add ("fader " + juce::String (round1 (db)) + " dB");
    }
    if (has (input, "pan"))
    {
        const auto pan = juce::jlimit (-100.0, 100.0, number (input, "pan", 0.0));
        track.setProperty (ids::pan, pan / 100.0, &um);
        changes.add ("pan " + (std::abs (pan) < 0.5 ? juce::String ("centre") : (pan < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (pan)))));
    }
    for (const auto& [key, id, word] : { std::tuple { "mute", ids::mute, "muted" }, std::tuple { "solo", ids::solo, "solo" },
                                         std::tuple { "armed", ids::armed, "armed" } })
    {
        if (! has (input, key))
            continue;
        if (id == ids::armed && model::isBusTrack (track))
            continue;
        const auto on = flag (input, key, false);
        track.setProperty (id, on, &um);
        changes.add (on ? juce::String (word) : "not " + juce::String (word));
    }

    if (changes.isEmpty())
        return fail ("Nothing to change: give name, volume_db, pan, mute, solo or armed.");

    return ok ("Track " + trackLabel (track) + ": " + changes.joinIntoString (", "),
               "Track " + trackLabel (track) + ": " + changes.joinIntoString (", "));
}

ToolResult AssistantTools::deleteTracks (const juce::var& input)
{
    const auto* list = input.getProperty ("tracks", {}).getArray();
    if (list == nullptr || list->isEmpty())
        return fail ("Give the tracks to delete.");

    // Find them all first: numbers refer to the tracks as they are now.
    juce::Array<juce::ValueTree> tracks;
    for (const auto& reference : *list)
    {
        juce::String error;
        const auto track = findTrack (reference, error);
        if (! track.isValid())
            return fail (error);
        tracks.addIfNotAlreadyThere (track);
    }

    juce::StringArray names;
    for (auto& track : tracks)
    {
        names.add (track[ids::name].toString());
        model::deleteTrack (track, &um);
    }
    return ok ("Deleted " + names.joinIntoString (", ") + ". The remaining tracks are renumbered.",
               "Deleted " + names.joinIntoString (", "));
}

ToolResult AssistantTools::writeMidi (const juce::var& input)
{
    const auto* notes = input.getProperty ("notes", {}).getArray();
    if (notes == nullptr)
        return fail ("Give the notes.");

    const auto startBar = juce::jmax (1.0, number (input, "start_bar", 1.0));
    const auto lengthBars = juce::jlimit (0.25, 256.0, number (input, "length_bars", 4.0));
    const int beatsPerBar = project.getProperty (ids::timeSigNumerator, 4);
    const auto spb = model::secondsPerBeat (project);
    const auto lengthBeats = lengthBars * beatsPerBar;

    juce::ValueTree track;
    bool created = false;
    if (has (input, "track"))
    {
        juce::String error;
        track = findTrack (input.getProperty ("track", {}), error);
        if (! track.isValid())
            return fail (error);
        if (! model::isInstrumentTrack (track))
            return fail ("Track " + trackLabel (track) + " isn't an instrument track. Use an instrument track, or give new_track_name.");
    }
    else
    {
        auto name = input.getProperty ("new_track_name", {}).toString().trim();
        if (name.isEmpty())
            name = "Inst " + juce::String (model::numTracks (project) + 1);
        track = model::addTrack (project, name, &um, -1, model::instrumentKind);
        created = true;
    }

    const auto start = barToSeconds (startBar);
    const auto end = start + lengthBeats * spb;

    int removed = 0;
    if (flag (input, "replace_existing", false))
    {
        juce::Array<juce::ValueTree> overlapping;
        for (const auto& region : track)
            if (model::isMidiRegion (region) && (double) region[ids::start] < end - 1.0e-6 && model::regionEnd (region) > start + 1.0e-6)
                overlapping.add (region);
        for (auto& region : overlapping)
            model::deleteRegion (region, &um);
        removed = overlapping.size();
    }

    auto region = model::addMidiRegion (track, start, end - start, &um);
    const auto regionName = input.getProperty ("region_name", {}).toString().trim();
    region.setProperty (ids::name, regionName.isNotEmpty() ? regionName : track[ids::name].toString(), &um);

    int written = 0, skipped = 0;
    for (const auto& n : *notes)
    {
        const auto pitch = (int) number (n, "pitch", -1);
        const auto noteStart = number (n, "start", -1.0);
        auto length = number (n, "length", 0.0);
        const auto velocity = juce::jlimit (1, 127, (int) number (n, "velocity", 96));

        if (pitch < 0 || pitch > 127 || noteStart < 0.0 || noteStart >= lengthBeats || length <= 0.0)
        {
            ++skipped;
            continue;
        }
        length = juce::jmin (length, lengthBeats - noteStart);
        // Notes go in with the region: undoing the turn removes them together.
        model::addNote (region, pitch, noteStart * spb, length * spb, velocity, nullptr);
        ++written;
    }

    if (hooks.selectTrack)
        hooks.selectTrack (track);

    juce::String summary = "Wrote " + juce::String (written) + " notes on " + (created ? "new track " : "track ") + trackLabel (track)
                           + ", bars " + juce::String (round2 (startBar)) + " to " + juce::String (round2 (startBar + lengthBars));
    if (removed > 0)
        summary << " (replaced " << removed << (removed == 1 ? " region)" : " regions)");
    auto forClaude = summary;
    if (skipped > 0)
        forClaude << ". Skipped " << skipped << " notes outside the region or with bad values.";
    return ok (forClaude, summary);
}

ToolResult AssistantTools::getMidi (const juce::var& input)
{
    juce::String error;
    const auto track = findTrack (input.getProperty ("track", {}), error);
    if (! track.isValid())
        return fail (error);

    const auto spb = model::secondsPerBeat (project);
    juce::Array<juce::var> regions;
    int total = 0;
    for (const auto& region : track)
    {
        if (! model::isMidiRegion (region))
            continue;

        auto r = object();
        set (r, "name", region[ids::name]);
        set (r, "start_bar", round2 (secondsToBar ((double) region[ids::start])));
        set (r, "end_bar", round2 (secondsToBar (model::regionEnd (region))));
        const auto offsetBeats = (double) region.getProperty (ids::offset, 0.0) / spb;
        if (offsetBeats > 1.0e-6)
            set (r, "content_offset_beats", round2 (offsetBeats));

        juce::Array<juce::var> notes;
        for (const auto& note : region)
        {
            if (! note.hasType (ids::NOTE) || total >= 2000)
                continue;
            juce::Array<juce::var> n { (int) note[ids::pitch], round2 ((double) note[ids::start] / spb),
                                       round2 ((double) note[ids::length] / spb), (int) note[ids::velocity] };
            notes.add (juce::var (n));
            ++total;
        }
        set (r, "notes_pitch_start_length_velocity", juce::var (notes));
        regions.add (r);
    }

    if (regions.isEmpty())
        return ok ("Track " + trackLabel (track) + " has no MIDI regions.", {});

    auto result = object();
    set (result, "track", trackLabel (track));
    set (result, "regions", juce::var (regions));
    if (total >= 2000)
        set (result, "note", "Only the first 2000 notes are listed.");
    return ok (json (result), {});
}

ToolResult AssistantTools::deleteRegions (const juce::var& input)
{
    juce::String error;
    auto track = findTrack (input.getProperty ("track", {}), error);
    if (! track.isValid())
        return fail (error);

    const bool ranged = has (input, "start_bar") || has (input, "end_bar");
    const auto from = barToSeconds (number (input, "start_bar", 1.0));
    const auto to = has (input, "end_bar") ? barToSeconds (number (input, "end_bar", 1.0)) : std::numeric_limits<double>::max();

    juce::Array<juce::ValueTree> doomed;
    for (const auto& region : track)
        if (region.hasType (ids::REGION) && (! ranged || ((double) region[ids::start] < to - 1.0e-6 && model::regionEnd (region) > from + 1.0e-6)))
            doomed.add (region);

    for (auto& region : doomed)
        model::deleteRegion (region, &um);

    const auto text = "Deleted " + juce::String (doomed.size()) + (doomed.size() == 1 ? " region" : " regions") + " on track " + trackLabel (track);
    return ok (text, doomed.isEmpty() ? juce::String() : text);
}

ToolResult AssistantTools::generatePattern (const juce::var& input)
{
    generator::Settings s;
    const auto genreName = input.getProperty ("genre", {}).toString().trim();
    int genre = -1;
    for (int i = 0; i < (int) generator::genres().size(); ++i)
        if (genreName.equalsIgnoreCase (generator::genres()[(size_t) i].name)
            || juce::String (generator::genres()[(size_t) i].name).removeCharacters ("-& ").equalsIgnoreCase (genreName.removeCharacters ("-& ")))
            genre = i;
    if (genre < 0)
        for (int i = 0; i < (int) generator::genres().size(); ++i)
            if (juce::String (generator::genres()[(size_t) i].name).containsIgnoreCase (genreName) && genreName.isNotEmpty())
                genre = i;
    if (genre < 0)
        return fail ("Unknown genre \"" + genreName + "\". Choose one of the listed genres and describe the rest in your reply, or use write_midi.");

    s.genre = genre;
    s.minor = generator::genres()[(size_t) genre].minor;
    if (has (input, "scale"))
        s.minor = input.getProperty ("scale", {}).toString().equalsIgnoreCase ("minor");

    if (has (input, "key"))
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const auto key = input.getProperty ("key", {}).toString().trim().upToFirstOccurrenceOf (" ", false, false);
        for (int i = 0; i < 12; ++i)
            if (key.equalsIgnoreCase (names[i]) || key.equalsIgnoreCase (flats[i]))
                s.root = i;
    }

    const auto bars = (int) number (input, "bars", 8);
    s.bars = bars <= 4 ? 4 : (bars <= 8 ? 8 : 16);
    s.variation = (float) juce::jlimit (0.0, 1.0, number (input, "variation", 0.5));
    s.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;

    if (const auto* parts = input.getProperty ("parts", {}).getArray(); parts != nullptr && ! parts->isEmpty())
    {
        s.drums = s.bass = s.chords = s.melody = false;
        for (const auto& p : *parts)
        {
            const auto part = p.toString().toLowerCase();
            s.drums  = s.drums  || part == "drums";
            s.bass   = s.bass   || part == "bass";
            s.chords = s.chords || part == "chords";
            s.melody = s.melody || part == "melody";
        }
    }

    const auto result = generator::generate (s);
    const bool useTempo = flag (input, "use_genre_tempo", false);
    if (useTempo)
        project.setProperty (ids::tempo, result.tempo, &um);

    const auto startBar = juce::jmax (1.0, std::floor (number (input, "start_bar",
                                       hooks.playheadSeconds ? std::floor (secondsToBar (hooks.playheadSeconds()) + 1.0e-6) : 1.0)));
    const auto tracks = generator::apply (project, result, s, barToSeconds (startBar), false, &um);

    juce::StringArray names;
    for (const auto& t : tracks)
        names.add (trackLabel (t));
    if (hooks.selectTrack && ! tracks.isEmpty())
        hooks.selectTrack (tracks.getFirst());

    const auto summary = "Generated " + juce::String (generator::genres()[(size_t) genre].name) + " in " + generator::keyName (s.root, s.minor)
                         + ", " + juce::String (s.bars) + " bars from bar " + juce::String ((int) startBar)
                         + (useTempo ? " at " + juce::String (result.tempo) + " BPM" : juce::String());
    return ok (summary + ". New tracks: " + names.joinIntoString (", ")
               + ". Drums use GM notes; the other parts play on " + app::synthName + " until the person loads instruments.", summary);
}

ToolResult AssistantTools::setConsole (const juce::var& input)
{
    juce::String error;
    auto track = findTrack (input.getProperty ("track", {}), error);
    if (! track.isValid())
        return fail (error);
    if (model::isBusTrack (track))
        return fail ("Aux buses don't have a console strip.");

    const auto id = track[ids::id].toString();
    const bool pluginMode = model::stripPlugin (project, id).isValid();
    juce::StringArray changes, problems;

    const std::tuple<const char*, console::StripControl, juce::Identifier, double, double> knobs[] {
        { "hf",  console::StripControl::hf,  ids::consoleHF,  -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb },
        { "hmf", console::StripControl::hmf, ids::consoleHMF, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb },
        { "lmf", console::StripControl::lmf, ids::consoleLMF, -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb },
        { "lf",  console::StripControl::lf,  ids::consoleLF,  -ConsoleChannel::maxGainDb, ConsoleChannel::maxGainDb },
        { "dynamics", console::StripControl::dynamics, ids::consoleDynamics, 0.0, ConsoleChannel::maxDynamics } };

    for (const auto& [key, control, property, low, high] : knobs)
    {
        if (! has (input, key))
            continue;
        const auto value = juce::jlimit (low, high, number (input, key, 0.0));

        if (pluginMode)
        {
            auto* p = hooks.stripParameter ? hooks.stripParameter (id, control) : nullptr;
            const auto shown = p != nullptr ? console::setParameterFromText (*p, juce::String (value)) : juce::String();
            if (shown.isNotEmpty())
                changes.add (juce::String (key).toUpperCase() + " " + shown);
            else
                problems.add (juce::String (key).toUpperCase());
        }
        else
        {
            track.setProperty (property, value, &um);
            changes.add (juce::String (key).toUpperCase() + (juce::String (key) == "dynamics" ? " " : " " + juce::String (value > 0 ? "+" : "")) + juce::String (round1 (value))
                         + (juce::String (key) == "dynamics" ? "" : " dB"));
        }
    }

    for (const auto& [key, control, property] : { std::tuple { "eq_in", console::StripControl::eqIn, ids::consoleIn },
                                                   std::tuple { "dynamics_in", console::StripControl::dynIn, ids::consoleDynIn } })
    {
        if (! has (input, key))
            continue;
        const auto on = flag (input, key, true);

        if (pluginMode)
        {
            auto* p = hooks.stripParameter ? hooks.stripParameter (id, control) : nullptr;
            if (p == nullptr)
            {
                problems.add (key);
                continue;
            }
            const auto bypassStyle = console::isBypassParameter (p->getName (64));
            p->beginChangeGesture();
            p->setValueNotifyingHost ((on != bypassStyle) ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
        else
        {
            track.setProperty (property, on, &um);
        }
        changes.add (juce::String (juce::String (key) == "eq_in" ? "EQ " : "Dynamics ") + (on ? "in" : "out"));
    }

    if (changes.isEmpty() && problems.isEmpty())
        return fail ("Nothing to change: give hf, hmf, lmf, lf, dynamics, eq_in or dynamics_in.");

    juce::String text = "Console on " + trackLabel (track) + ": " + changes.joinIntoString (", ");
    auto forClaude = text;
    if (! problems.isEmpty())
        forClaude << ". Couldn't set " << problems.joinIntoString (", ") << " on the channel strip plugin (no matching control, or the plugin isn't loaded).";
    if (pluginMode)
        forClaude << " (These are the plugin's own knobs; they aren't part of Undo.)";
    return { forClaude, changes.isEmpty() ? juce::String() : text, changes.isEmpty() };
}

ToolResult AssistantTools::setBusCompressor (const juce::var& input)
{
    juce::StringArray changes, problems;
    const auto plugin = model::busCompPlugin (project);

    if (plugin.isValid())
    {
        const std::tuple<const char*, console::Control, const char*> controls[] {
            { "threshold_db", console::Control::threshold, "Threshold" }, { "ratio", console::Control::ratio, "Ratio" },
            { "attack_ms", console::Control::attack, "Attack" }, { "release", console::Control::release, "Release" },
            { "makeup_db", console::Control::makeup, "Make-up" }, { "mix_percent", console::Control::mix, "Mix" } };

        for (const auto& [key, control, label] : controls)
        {
            if (! has (input, key))
                continue;
            auto* p = hooks.busCompParameter ? hooks.busCompParameter (control) : nullptr;
            const auto shown = p != nullptr ? console::setParameterFromText (*p, input.getProperty (key, {}).toString()) : juce::String();
            if (shown.isNotEmpty())
                changes.add (juce::String (label) + " " + shown);
            else
                problems.add (label);
        }

        if (has (input, "on"))
        {
            const auto on = flag (input, "on", true);
            auto insert = plugin;
            insert.setProperty (ids::bypass, ! on, &um);
            changes.add (on ? "in" : "out");
        }
    }
    else
    {
        auto nearestIndex = [] (auto& values, double wanted)
        {
            int best = 0;
            for (int i = 0; i < (int) values.size(); ++i)
                if (std::abs (values[(size_t) i] - wanted) < std::abs (values[(size_t) best] - wanted))
                    best = i;
            return best;
        };

        if (has (input, "on"))
        {
            const auto on = flag (input, "on", true);
            project.setProperty (ids::busCompOn, on, &um);
            changes.add (on ? "on" : "off");
        }
        if (has (input, "threshold_db"))
        {
            const auto v = juce::jlimit ((double) MasterBusCompressor::minThresholdDb, (double) MasterBusCompressor::maxThresholdDb, number (input, "threshold_db", -10.0));
            project.setProperty (ids::busCompThreshold, v, &um);
            changes.add ("Threshold " + juce::String (round1 (v)) + " dB");
        }
        if (has (input, "ratio"))
        {
            const auto i = nearestIndex (MasterBusCompressor::ratios, number (input, "ratio", 4.0));
            project.setProperty (ids::busCompRatio, i, &um);
            changes.add ("Ratio " + MasterBusCompressor::ratioText (i));
        }
        if (has (input, "attack_ms"))
        {
            const auto i = nearestIndex (MasterBusCompressor::attacksMs, number (input, "attack_ms", 10.0));
            project.setProperty (ids::busCompAttack, i, &um);
            changes.add ("Attack " + MasterBusCompressor::attackText (i));
        }
        if (has (input, "release"))
        {
            const auto text = input.getProperty ("release", {}).toString();
            const auto i = text.containsIgnoreCase ("auto") ? MasterBusCompressor::autoRelease
                                                             : juce::jmin (nearestIndex (MasterBusCompressor::releasesS, text.getDoubleValue()), 3);
            project.setProperty (ids::busCompRelease, i, &um);
            changes.add ("Release " + MasterBusCompressor::releaseText (i));
        }
        if (has (input, "makeup_db"))
        {
            const auto v = juce::jlimit (0.0, (double) MasterBusCompressor::maxMakeupDb, number (input, "makeup_db", 0.0));
            project.setProperty (ids::busCompMakeup, v, &um);
            changes.add ("Make-up " + juce::String (round1 (v)) + " dB");
        }
        if (has (input, "mix_percent"))
        {
            const auto v = juce::jlimit (0.0, 100.0, number (input, "mix_percent", 100.0));
            project.setProperty (ids::busCompMix, v / 100.0, &um);
            changes.add ("Mix " + juce::String (juce::roundToInt (v)) + "%");
        }
    }

    if (changes.isEmpty() && problems.isEmpty())
        return fail ("Nothing to change.");

    const auto text = "Bus compressor: " + changes.joinIntoString (", ");
    auto forClaude = text;
    if (! problems.isEmpty())
        forClaude << ". Couldn't set " << problems.joinIntoString (", ") << " on the plugin (no matching control, or the plugin isn't loaded).";
    return { forClaude, changes.isEmpty() ? juce::String() : text, changes.isEmpty() };
}

ToolResult AssistantTools::setMasterVolume (const juce::var& input)
{
    const auto db = juce::jlimit (-100.0, 6.0, number (input, "volume_db", 0.0));
    project.setProperty (ids::masterGain, dbToGain (db), &um);
    return ok ("Master fader at " + juce::String (round1 (db)) + " dB.", "Master fader to " + juce::String (round1 (db)) + " dB");
}

namespace
{
    juce::String setBuiltInParams (juce::ValueTree insert, const juce::var& params, juce::UndoManager& um, juce::StringArray& unknown)
    {
        const auto* info = findBuiltInEffect (insert[ids::effect].toString());
        auto* object = params.getDynamicObject();
        if (info == nullptr || object == nullptr)
            return {};

        juce::StringArray changes;
        for (const auto& [name, value] : object->getProperties())
        {
            const auto* p = [&]() -> const ParamInfo*
            {
                for (const auto& candidate : info->params)
                    if (candidate.id.equalsIgnoreCase (name.toString()) || candidate.name.equalsIgnoreCase (name.toString()))
                        return &candidate;
                return nullptr;
            }();

            if (p == nullptr)
            {
                unknown.add (name.toString());
                continue;
            }
            const auto v = juce::jlimit ((double) p->min, (double) p->max, value.isString() ? value.toString().getDoubleValue() : (double) value);
            insert.setProperty (p->id, v, &um);
            changes.add (p->name + " " + juce::String (round2 (v)) + p->suffix);
        }
        return changes.joinIntoString (", ");
    }
}

ToolResult AssistantTools::addEffect (const juce::var& input)
{
    const auto reference = input.getProperty ("track", {});
    juce::String owner, label;
    if (reference.toString().equalsIgnoreCase ("master"))
    {
        owner = model::masterOwner;
        label = "the master";
    }
    else
    {
        juce::String error;
        const auto track = findTrack (reference, error);
        if (! track.isValid())
            return fail (error);
        owner = track[ids::id].toString();
        label = "track " + trackLabel (track);
    }

    const auto type = input.getProperty ("effect", {}).toString().toLowerCase();
    const auto* info = findBuiltInEffect (type);
    if (info == nullptr)
        return fail ("Unknown effect \"" + type + "\". Built-in effects: eq, compressor, delay, reverb, limiter.");

    auto chain = model::chainFor (project, owner);
    auto insert = model::addBuiltinInsert (chain, type, -1, &um);

    juce::StringArray unknown;
    const auto settings = setBuiltInParams (insert, input.getProperty ("params", {}), um, unknown);
    const auto slot = model::insertsOf (chain).indexOf (insert) + 1;

    juce::String text = "Added " + info->name + " to " + label + " (slot " + juce::String (slot) + ")";
    if (settings.isNotEmpty())
        text << ": " << settings;
    auto forClaude = text;
    if (! unknown.isEmpty())
        forClaude << ". Unknown params ignored: " << unknown.joinIntoString (", ");
    return ok (forClaude, text);
}

ToolResult AssistantTools::setEffect (const juce::var& input)
{
    const auto reference = input.getProperty ("track", {});
    juce::String owner, label;
    if (reference.toString().equalsIgnoreCase ("master"))
    {
        owner = model::masterOwner;
        label = "the master";
    }
    else
    {
        juce::String error;
        const auto track = findTrack (reference, error);
        if (! track.isValid())
            return fail (error);
        owner = track[ids::id].toString();
        label = trackLabel (track);
    }

    const auto chain = model::findChain (project, owner);
    const auto inserts = chain.isValid() ? model::insertsOf (chain) : juce::Array<juce::ValueTree>();
    const auto slot = (int) number (input, "slot", 0);
    if (! juce::isPositiveAndNotGreaterThan (slot, inserts.size()) || slot == 0)
        return fail (label + " has " + juce::String (inserts.size()) + " inserts; there's no slot " + juce::String (slot) + ".");

    auto insert = inserts[slot - 1];
    juce::StringArray changes, unknown;

    if (has (input, "bypass"))
    {
        const auto bypass = flag (input, "bypass", false);
        insert.setProperty (ids::bypass, bypass, &um);
        changes.add (bypass ? "bypassed" : "in");
    }

    if (insert[ids::kind].toString() == model::builtinKind)
    {
        const auto settings = setBuiltInParams (insert, input.getProperty ("params", {}), um, unknown);
        if (settings.isNotEmpty())
            changes.add (settings);
    }
    else if (auto* params = input.getProperty ("params", {}).getDynamicObject())
    {
        auto* instance = hooks.pluginInstance ? hooks.pluginInstance (insert[ids::id].toString()) : nullptr;
        if (instance == nullptr)
            return fail ("The plugin in slot " + juce::String (slot) + " isn't loaded.");

        for (const auto& [name, value] : params->getProperties())
        {
            juce::AudioProcessorParameter* match = nullptr;
            for (auto* p : instance->getParameters())
                if (p->getName (64).equalsIgnoreCase (name.toString()))
                    match = p;
            if (match == nullptr)
                for (auto* p : instance->getParameters())
                    if (p->getName (64).containsIgnoreCase (name.toString()))
                    {
                        match = p;
                        break;
                    }

            const auto shown = match != nullptr ? console::setParameterFromText (*match, value.toString()) : juce::String();
            if (shown.isNotEmpty())
                changes.add (match->getName (40) + " " + shown);
            else
                unknown.add (name.toString());
        }
    }

    if (changes.isEmpty())
        return fail (unknown.isEmpty() ? juce::String ("Nothing to change.") : "Couldn't set: " + unknown.joinIntoString (", "));

    const auto effectName = insert[ids::kind].toString() == model::builtinKind
                                ? (findBuiltInEffect (insert[ids::effect].toString()) != nullptr ? findBuiltInEffect (insert[ids::effect].toString())->name : juce::String ("Effect"))
                                : pluginName (insert[ids::pluginDescription].toString());
    const auto text = effectName + " on " + label + ": " + changes.joinIntoString (", ");
    auto forClaude = text;
    if (! unknown.isEmpty())
        forClaude << ". Couldn't set: " << unknown.joinIntoString (", ");
    return ok (forClaude, text);
}

ToolResult AssistantTools::removeEffect (const juce::var& input)
{
    const auto reference = input.getProperty ("track", {});
    juce::String owner, label;
    if (reference.toString().equalsIgnoreCase ("master"))
    {
        owner = model::masterOwner;
        label = "the master";
    }
    else
    {
        juce::String error;
        const auto track = findTrack (reference, error);
        if (! track.isValid())
            return fail (error);
        owner = track[ids::id].toString();
        label = trackLabel (track);
    }

    const auto chain = model::findChain (project, owner);
    const auto inserts = chain.isValid() ? model::insertsOf (chain) : juce::Array<juce::ValueTree>();
    const auto slot = (int) number (input, "slot", 0);
    if (slot < 1 || slot > inserts.size())
        return fail (label + " has " + juce::String (inserts.size()) + " inserts; there's no slot " + juce::String (slot) + ".");

    model::removeInsert (inserts[slot - 1], &um);
    const auto text = "Removed the insert in slot " + juce::String (slot) + " from " + label;
    return ok (text + ". Later slots move up.", text);
}

ToolResult AssistantTools::addSend (const juce::var& input)
{
    juce::String error;
    const auto track = findTrack (input.getProperty ("track", {}), error);
    if (! track.isValid())
        return fail (error);
    const auto bus = findTrack (input.getProperty ("bus", {}), error);
    if (! bus.isValid())
        return fail (error);
    if (! model::isBusTrack (bus))
        return fail (trackLabel (bus) + " isn't an aux bus. Add one with add_tracks (kind 'bus').");
    if (bus == track)
        return fail ("A bus can't send to itself.");

    const auto db = juce::jlimit (-60.0, 6.0, number (input, "level_db", -10.0));
    auto chain = model::chainFor (project, track[ids::id].toString());

    // Change an existing send to the same bus rather than adding a second.
    for (auto send : model::sendsOf (chain))
        if (send[ids::target].toString() == bus[ids::id].toString())
        {
            send.setProperty (ids::level, dbToGain (db), &um);
            const auto text = "Send from " + trackLabel (track) + " to " + trackLabel (bus) + " at " + juce::String (round1 (db)) + " dB";
            return ok (text, text);
        }

    model::addSend (chain, bus[ids::id].toString(), dbToGain (db), &um);
    const auto text = "Send from " + trackLabel (track) + " to " + trackLabel (bus) + " at " + juce::String (round1 (db)) + " dB";
    return ok (text, text);
}

ToolResult AssistantTools::transport (const juce::var& input)
{
    const auto action = input.getProperty ("action", {}).toString();
    if (action == "play" || action == "stop")
    {
        if (! hooks.setPlaying)
            return fail ("The transport isn't available.");
        hooks.setPlaying (action == "play");
        return ok (action == "play" ? "Playing." : "Stopped.", action == "play" ? "Play" : "Stop");
    }
    if (action == "go_to_bar")
    {
        if (! hooks.setPlayhead)
            return fail ("The transport isn't available.");
        const auto bar = juce::jmax (1.0, number (input, "bar", 1.0));
        hooks.setPlayhead (barToSeconds (bar));
        return ok ("Playhead at bar " + juce::String (round2 (bar)) + ".", "Moved the playhead to bar " + juce::String (round2 (bar)));
    }
    return fail ("Unknown action " + action);
}

ToolResult AssistantTools::setCycle (const juce::var& input)
{
    const bool on = flag (input, "on", true);
    auto start = has (input, "start_bar") ? barToSeconds (number (input, "start_bar", 1.0)) : (double) project.getProperty (ids::cycleStart, 0.0);
    auto end = has (input, "end_bar") ? barToSeconds (number (input, "end_bar", 1.0)) : (double) project.getProperty (ids::cycleEnd, 0.0);

    if (on && end <= start + 1.0e-3)
        return fail ("The cycle needs an end_bar after its start_bar.");

    // Set through the undo manager like the rest of the turn.
    project.setProperty (ids::cycleStart, start, &um);
    project.setProperty (ids::cycleEnd, juce::jmax (end, start), &um);
    project.setProperty (ids::cycleEnabled, on, &um);

    const auto text = on ? "Cycle on, bars " + juce::String (round2 (secondsToBar (start))) + " to " + juce::String (round2 (secondsToBar (end)))
                         : juce::String ("Cycle off");
    return ok (text, text);
}

ToolResult AssistantTools::setMetronome (const juce::var& input)
{
    juce::StringArray changes;
    if (has (input, "on"))
    {
        const auto on = flag (input, "on", true);
        project.setProperty (ids::metronome, on, nullptr);
        changes.add (on ? "Click on" : "Click off");
    }
    if (has (input, "count_in"))
    {
        const auto on = flag (input, "count_in", true);
        project.setProperty (ids::countIn, on, nullptr);
        changes.add (on ? "Count-in on" : "Count-in off");
    }
    if (changes.isEmpty())
        return fail ("Give on and/or count_in.");
    return ok (changes.joinIntoString (", "), changes.joinIntoString (", "));
}

ToolResult AssistantTools::runCommand (const juce::var& input)
{
    const auto name = input.getProperty ("command", {}).toString().trim();
    if (! hooks.runCommand)
        return fail ("Commands aren't available.");
    if (! hooks.runCommand (name))
        return fail ("Couldn't run \"" + name + "\": unknown, or not possible right now (for example nothing is selected).");
    return ok ("Ran " + name + ".", "Ran " + name.upToFirstOccurrenceOf ("...", false, false));
}

void AssistantTools::analyzeMix (const juce::var& input, std::function<void (ToolResult)> done)
{
    if (! hooks.measureMix)
    {
        done (fail ("Mix analysis isn't available."));
        return;
    }

    const auto songEnd = model::projectEnd (project);
    const auto start = has (input, "start_bar") ? barToSeconds (number (input, "start_bar", 1.0)) : 0.0;
    auto end = has (input, "end_bar") ? barToSeconds (number (input, "end_bar", 1.0)) : songEnd + 1.0;
    end = juce::jmin (end, start + 600.0);   // at most ten minutes

    if (end <= start + 0.05)
    {
        done (fail (songEnd <= 0.0 ? "The project is empty: there's nothing to measure yet." : "That range is empty."));
        return;
    }

    hooks.measureMix (start, end, [this, done, start, end] (AppHooks::Measurement m)
    {
        if (m.error.isNotEmpty())
        {
            done (fail (m.error));
            return;
        }

        auto describe = [] (const LevelStats& s)
        {
            auto o = object();
            if (s.isSilent())
            {
                set (o, "silent", true);
                return o;
            }
            double low, mid, high;
            s.bandShares (low, mid, high);
            set (o, "peak_dbfs", round1 (s.peakDb()));
            set (o, "rms_dbfs", round1 (s.rmsDb()));
            set (o, "crest_db", round1 (s.peakDb() - s.rmsDb()));
            set (o, "low_percent", juce::roundToInt (low));
            set (o, "mid_percent", juce::roundToInt (mid));
            set (o, "high_percent", juce::roundToInt (high));
            if (s.peak >= 0.999f)
                set (o, "clipping", true);
            return o;
        };

        auto result = object();
        set (result, "from_bar", round2 (secondsToBar (start)));
        set (result, "to_bar", round2 (secondsToBar (end)));
        juce::Array<juce::var> tracks;
        for (int i = 0; i < model::numTracks (project); ++i)
        {
            const auto track = project.getChild (i);
            auto o = object();
            set (o, "track", trackLabel (track));
            const auto found = m.tracks.find (track[ids::id].toString());
            auto stats = found != m.tracks.end() ? describe (found->second) : juce::var();
            if (auto* d = stats.getDynamicObject())
                for (const auto& [k, v] : d->getProperties())
                    set (o, k, v);
            else
                set (o, "silent", true);
            if ((bool) track[ids::mute])
                set (o, "muted", true);
            tracks.add (o);
        }
        set (result, "tracks", juce::var (tracks));
        set (result, "master", describe (m.master));

        done (ok (json (result), "Measured the mix (bars " + juce::String (juce::roundToInt (secondsToBar (start))) + " to "
                                 + juce::String (juce::roundToInt (secondsToBar (end))) + ")"));
    });
}

} // namespace riff::ai
