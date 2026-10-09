// Two tiny CLAP plugins for the tests: a gain effect with one parameter and
// state, and a "synth" that outputs each held note's velocity as a constant.

#include <clap/clap.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace
{
const char* gainFeatures[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };
const char* synthFeatures[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr };

const clap_plugin_descriptor_t gainDesc = { CLAP_VERSION_INIT, "com.audiotechpro.test-gain", "Test Gain", "Test Vendor", "", "", "", "1.2.0", "A gain", gainFeatures };
const clap_plugin_descriptor_t synthDesc = { CLAP_VERSION_INIT, "com.audiotechpro.test-synth", "Test Synth", "Test Vendor", "", "", "", "1.0.0", "", synthFeatures };

constexpr clap_id gainParam = 7;

struct Plugin
{
    clap_plugin_t plugin;
    const clap_host_t* host;
    bool synth = false;
    double gain = 1.0;
    double level = 0.0;
    int held = -1;
};

Plugin* self (const clap_plugin_t* p) { return static_cast<Plugin*> (p->plugin_data); }

void handle (Plugin* s, const clap_event_header_t* e)
{
    if (e->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;
    if (e->type == CLAP_EVENT_PARAM_VALUE)
    {
        auto* pv = reinterpret_cast<const clap_event_param_value_t*> (e);
        if (pv->param_id == gainParam)
            s->gain = pv->value;
    }
    else if (e->type == CLAP_EVENT_NOTE_ON)
    {
        auto* n = reinterpret_cast<const clap_event_note_t*> (e);
        s->held = n->key;
        s->level = n->velocity;
    }
    else if (e->type == CLAP_EVENT_NOTE_OFF)
    {
        auto* n = reinterpret_cast<const clap_event_note_t*> (e);
        if (n->key == s->held)
            s->level = 0.0, s->held = -1;
    }
}

// --- audio ports -----------------------------------------------------------
uint32_t portCount (const clap_plugin_t* p, bool isInput) { return (isInput && self (p)->synth) ? 0 : 1; }
bool portGet (const clap_plugin_t*, uint32_t index, bool, clap_audio_port_info_t* info)
{
    if (index != 0)
        return false;
    std::memset (info, 0, sizeof (*info));
    info->id = 0;
    std::strcpy (info->name, "Main");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { portCount, portGet };

// --- note ports ------------------------------------------------------------
uint32_t noteCount (const clap_plugin_t* p, bool isInput) { return (isInput && self (p)->synth) ? 1 : 0; }
bool noteGet (const clap_plugin_t*, uint32_t index, bool, clap_note_port_info_t* info)
{
    if (index != 0)
        return false;
    std::memset (info, 0, sizeof (*info));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strcpy (info->name, "Notes");
    return true;
}
const clap_plugin_note_ports_t notePorts { noteCount, noteGet };

// --- params ----------------------------------------------------------------
uint32_t paramCount (const clap_plugin_t* p) { return self (p)->synth ? 0 : 1; }
bool paramInfo (const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (index != 0)
        return false;
    std::memset (info, 0, sizeof (*info));
    info->id = gainParam;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strcpy (info->name, "Gain");
    info->min_value = 0.0;
    info->max_value = 2.0;
    info->default_value = 1.0;
    return true;
}
bool paramValue (const clap_plugin_t* p, clap_id id, double* value)
{
    if (id != gainParam)
        return false;
    *value = self (p)->gain;
    return true;
}
bool paramToText (const clap_plugin_t*, clap_id, double value, char* out, uint32_t size)
{
    std::snprintf (out, size, "x%.2f", value);
    return true;
}
bool paramFromText (const clap_plugin_t*, clap_id, const char* text, double* value)
{
    if (text[0] == 'x')
        ++text;
    *value = std::atof (text);
    return true;
}
void paramFlush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
{
    for (uint32_t i = 0; i < in->size (in); ++i)
        handle (self (p), in->get (in, i));
}
const clap_plugin_params_t params { paramCount, paramInfo, paramValue, paramToText, paramFromText, paramFlush };

// --- state -----------------------------------------------------------------
bool stateSave (const clap_plugin_t* p, const clap_ostream_t* stream)
{
    return stream->write (stream, &self (p)->gain, sizeof (double)) == sizeof (double);
}
bool stateLoad (const clap_plugin_t* p, const clap_istream_t* stream)
{
    return stream->read (stream, &self (p)->gain, sizeof (double)) == sizeof (double);
}
const clap_plugin_state_t state { stateSave, stateLoad };

// --- latency ---------------------------------------------------------------
uint32_t latencyGet (const clap_plugin_t* p) { return self (p)->synth ? 0 : 3; }
const clap_plugin_latency_t latency { latencyGet };

// --- the plugin ------------------------------------------------------------
clap_process_status process (const clap_plugin_t* p, const clap_process_t* proc)
{
    auto* s = self (p);
    const auto* in = proc->in_events;
    uint32_t next = 0;
    const auto count = in->size (in);
    for (uint32_t f = 0; f < proc->frames_count; ++f)
    {
        while (next < count && in->get (in, next)->time <= f)
            handle (s, in->get (in, next++));
        for (uint32_t c = 0; c < 2; ++c)
            proc->audio_outputs[0].data32[c][f] = s->synth ? (float) s->level
                                                           : proc->audio_inputs[0].data32[c][f] * (float) s->gain;
    }
    // Tell the host when the gain has been changed from "inside".
    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension (const clap_plugin_t*, const char* id)
{
    if (! std::strcmp (id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts;
    if (! std::strcmp (id, CLAP_EXT_NOTE_PORTS))  return &notePorts;
    if (! std::strcmp (id, CLAP_EXT_PARAMS))      return &params;
    if (! std::strcmp (id, CLAP_EXT_STATE))       return &state;
    if (! std::strcmp (id, CLAP_EXT_LATENCY))     return &latency;
    return nullptr;
}

const clap_plugin_t* create (const clap_host_t* host, const clap_plugin_descriptor_t* desc, bool synth)
{
    auto* s = new Plugin();
    s->host = host;
    s->synth = synth;
    s->plugin.desc = desc;
    s->plugin.plugin_data = s;
    s->plugin.init = [] (const clap_plugin_t*) { return true; };
    s->plugin.destroy = [] (const clap_plugin_t* p) { delete self (p); };
    s->plugin.activate = [] (const clap_plugin_t*, double, uint32_t, uint32_t) { return true; };
    s->plugin.deactivate = [] (const clap_plugin_t*) {};
    s->plugin.start_processing = [] (const clap_plugin_t*) { return true; };
    s->plugin.stop_processing = [] (const clap_plugin_t*) {};
    s->plugin.reset = [] (const clap_plugin_t*) {};
    s->plugin.process = process;
    s->plugin.get_extension = getExtension;
    s->plugin.on_main_thread = [] (const clap_plugin_t*) {};
    return &s->plugin;
}

uint32_t factoryCount (const clap_plugin_factory_t*) { return 2; }
const clap_plugin_descriptor_t* factoryDesc (const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &gainDesc : i == 1 ? &synthDesc : nullptr; }
const clap_plugin_t* factoryCreate (const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (! std::strcmp (id, gainDesc.id))  return create (host, &gainDesc, false);
    if (! std::strcmp (id, synthDesc.id)) return create (host, &synthDesc, true);
    return nullptr;
}
const clap_plugin_factory_t factory { factoryCount, factoryDesc, factoryCreate };
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    [] (const char*) { return true; },
    [] () {},
    [] (const char* id) -> const void* { return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; },
};
