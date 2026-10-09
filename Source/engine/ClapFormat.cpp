#include "ClapFormat.h"

#include <clap/clap.h>
#include <map>
#include <mutex>

#if JUCE_MAC
 #include <objc/runtime.h>
 #include <objc/message.h>
 #include <CoreGraphics/CoreGraphics.h>
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace riff
{

namespace
{

//==============================================================================
// The module: one per .clap file, loaded once and shared by its plugins.
//==============================================================================
struct ClapModule
{
    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    juce::String path;

    ~ClapModule()
    {
        if (entry != nullptr)
            entry->deinit();
        library.close();
    }

    /** The binary inside a macOS bundle, or the file itself elsewhere. */
    static juce::File binaryFor (const juce::File& file)
    {
        if (file.isDirectory())
        {
            const auto macos = file.getChildFile ("Contents").getChildFile ("MacOS");
            const auto name = file.getFileNameWithoutExtension();
            if (macos.getChildFile (name).existsAsFile())
                return macos.getChildFile (name);
            for (const auto& f : macos.findChildFiles (juce::File::findFiles, false))
                return f;
            return {};
        }
        return file;
    }

    static std::shared_ptr<ClapModule> load (const juce::String& path)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<ClapModule>> loaded;

        const std::lock_guard<std::mutex> lock (mutex);
        if (auto it = loaded.find (path); it != loaded.end())
            if (auto existing = it->second.lock())
                return existing;

        const juce::File file (path);
        const auto binary = binaryFor (file);
        if (! binary.existsAsFile())
            return nullptr;

        auto module = std::make_shared<ClapModule>();
        module->path = path;
        if (! module->library.open (binary.getFullPathName()))
            return nullptr;

        auto* entry = static_cast<const clap_plugin_entry_t*> (module->library.getFunction ("clap_entry"));
        if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version))
            return nullptr;
        if (! entry->init (path.toRawUTF8()))
            return nullptr;
        module->entry = entry;
        module->factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
        if (module->factory == nullptr)
            return nullptr;

        loaded[path] = module;
        return module;
    }
};

bool hasFeature (const clap_plugin_descriptor_t& d, const char* feature)
{
    if (d.features != nullptr)
        for (auto f = d.features; *f != nullptr; ++f)
            if (std::strcmp (*f, feature) == 0)
                return true;
    return false;
}

int uidFor (const char* clapId)
{
    return juce::String::fromUTF8 (clapId).hashCode();
}

void describe (juce::PluginDescription& p, const clap_plugin_descriptor_t& d, const juce::String& path)
{
    p.name = juce::String::fromUTF8 (d.name != nullptr ? d.name : "");
    p.descriptiveName = juce::String::fromUTF8 (d.description != nullptr && *d.description != 0 ? d.description : (d.name != nullptr ? d.name : ""));
    p.pluginFormatName = ClapPluginFormat::formatName();
    p.manufacturerName = juce::String::fromUTF8 (d.vendor != nullptr ? d.vendor : "");
    p.version = juce::String::fromUTF8 (d.version != nullptr ? d.version : "");
    p.fileOrIdentifier = path;
    p.uniqueId = p.deprecatedUid = uidFor (d.id);
    p.isInstrument = hasFeature (d, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    p.category = p.isInstrument ? "Instrument"
               : hasFeature (d, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) ? "Effect"
               : hasFeature (d, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) ? "Note Effect"
               : hasFeature (d, CLAP_PLUGIN_FEATURE_ANALYZER) ? "Analyzer" : "Effect";
    p.lastFileModTime = juce::File (path).getLastModificationTime();
    p.lastInfoUpdateTime = juce::Time::getCurrentTime();
}

juce::AudioChannelSet channelSetFor (uint32_t channels)
{
    return channels == 1 ? juce::AudioChannelSet::mono()
         : channels == 2 ? juce::AudioChannelSet::stereo()
                         : juce::AudioChannelSet::discreteChannels ((int) channels);
}

//==============================================================================
// Events going into the plugin: a fixed pool so the audio thread never allocates.
//==============================================================================
union AnyEvent
{
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_midi_t midi;
    clap_event_param_value_t param;
};

struct InputEvents
{
    std::vector<AnyEvent> events;
    uint32_t count = 0;
    clap_input_events_t list { this, &size, &get };

    InputEvents() { events.resize (2048); }

    void clear() noexcept { count = 0; }
    AnyEvent* add() noexcept { return count < events.size() ? &events[count++] : nullptr; }

    static uint32_t size (const clap_input_events_t* l) { return static_cast<const InputEvents*> (l->ctx)->count; }
    static const clap_event_header_t* get (const clap_input_events_t* l, uint32_t i)
    {
        auto* self = static_cast<const InputEvents*> (l->ctx);
        return i < self->count ? &self->events[i].header : nullptr;
    }
};

struct ParamChange { clap_id id; void* cookie; double value; };

class ClapInstance;

//==============================================================================
// Parameters
//==============================================================================
class ClapParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    ClapParameter (ClapInstance& o, const clap_param_info_t& i) : owner (o), info (i)
    {
        value.store (info.default_value);
    }

    double toPlain (float normalised) const noexcept
    {
        auto v = info.min_value + (double) normalised * (info.max_value - info.min_value);
        if (isStepped())
            v = std::round (v);
        return v;
    }
    float toNormalised (double plain) const noexcept
    {
        const auto range = info.max_value - info.min_value;
        return range > 0.0 ? (float) juce::jlimit (0.0, 1.0, (plain - info.min_value) / range) : 0.0f;
    }
    bool isStepped() const noexcept { return (info.flags & CLAP_PARAM_IS_STEPPED) != 0; }

    float getValue() const override             { return toNormalised (value.load()); }
    void setValue (float newValue) override;
    float getDefaultValue() const override      { return toNormalised (info.default_value); }
    juce::String getName (int maximumLength) const override
    {
        return juce::String::fromUTF8 (info.name).substring (0, maximumLength);
    }
    juce::String getLabel() const override      { return {}; }
    juce::String getText (float normalised, int maximumLength) const override;
    float getValueForText (const juce::String& text) const override;
    int getNumSteps() const override
    {
        return isStepped() ? juce::jmax (2, (int) (info.max_value - info.min_value) + 1) : AudioProcessor::getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override            { return isStepped(); }
    bool isBoolean() const override             { return isStepped() && juce::approximatelyEqual (info.max_value - info.min_value, 1.0); }
    bool isAutomatable() const override         { return (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
    bool isMetaParameter() const override       { return false; }
    juce::String getParameterID() const override { return juce::String (info.id); }

    /** A value the plugin itself changed (its own knob, or a preset). */
    void fromPlugin (double plain)
    {
        value.store (plain);
        sendValueChangedMessageToListeners (toNormalised (plain));
    }

    ClapInstance& owner;
    clap_param_info_t info;
    std::atomic<double> value;
};

//==============================================================================
// The plugin instance
//==============================================================================
class ClapInstance final : public juce::AudioPluginInstance
{
public:
    static std::unique_ptr<ClapInstance> create (std::shared_ptr<ClapModule> module, const juce::PluginDescription& desc, juce::String& error)
    {
        const clap_plugin_descriptor_t* found = nullptr;
        for (uint32_t i = 0; i < module->factory->get_plugin_count (module->factory); ++i)
            if (auto* d = module->factory->get_plugin_descriptor (module->factory, i); d != nullptr && uidFor (d->id) == desc.uniqueId)
                found = d;
        if (found == nullptr)
        {
            error = "This CLAP file doesn't have that plugin any more";
            return nullptr;
        }

        auto host = std::make_unique<Host>();
        const auto* plugin = module->factory->create_plugin (module->factory, &host->host, found->id);
        if (plugin == nullptr || ! plugin->init (plugin))
        {
            if (plugin != nullptr)
                plugin->destroy (plugin);
            error = "The CLAP plugin wouldn't start";
            return nullptr;
        }

        // The main audio ports decide the buses.
        Ports ports;
        if (auto* audio = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS)))
        {
            for (int dir = 0; dir < 2; ++dir)
            {
                const bool input = dir == 0;
                auto& list = input ? ports.inputs : ports.outputs;
                for (uint32_t i = 0; i < audio->count (plugin, input); ++i)
                {
                    clap_audio_port_info_t info {};
                    if (audio->get (plugin, i, input, &info))
                        list.push_back (info.channel_count);
                }
            }
        }

        BusesProperties buses;
        if (! ports.inputs.empty() && ports.inputs[0] > 0)
            buses = buses.withInput ("Input", channelSetFor (ports.inputs[0]), true);
        if (! ports.outputs.empty() && ports.outputs[0] > 0)
            buses = buses.withOutput ("Output", channelSetFor (ports.outputs[0]), true);

        return std::unique_ptr<ClapInstance> (new ClapInstance (std::move (module), desc, *found, plugin, std::move (host), std::move (ports), buses));
    }

    ~ClapInstance() override
    {
        *alive = false;
        timers.clear();
        if (active)
            releaseResources();
        plugin->destroy (plugin);
    }

    //==========================================================================
    const juce::String getName() const override  { return description.name; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        const juce::ScopedLock lock (processLock);
        deactivateLocked();
        currentRate = sampleRate;
        currentBlock = juce::jmax (1, maximumBlockSize);

        // Scratch buffers for every port (the main ones read from JUCE's buffer).
        const auto channels = [] (const std::vector<uint32_t>& list) { uint32_t n = 0; for (auto c : list) n += c; return (int) n; };
        inScratch.setSize (juce::jmax (1, channels (ports.inputs)), currentBlock);
        outScratch.setSize (juce::jmax (1, channels (ports.outputs)), currentBlock);
        inPointers.assign ((size_t) inScratch.getNumChannels(), nullptr);
        outPointers.assign ((size_t) outScratch.getNumChannels(), nullptr);
        inBuffers.assign (ports.inputs.size(), {});
        outBuffers.assign (ports.outputs.size(), {});

        active = plugin->activate (plugin, sampleRate, 1, (uint32_t) currentBlock);
        processing = false;
        if (active)
        {
            if (latencyExt != nullptr)
                setLatencySamples ((int) latencyExt->get (plugin));
        }
    }

    void releaseResources() override
    {
        const juce::ScopedLock lock (processLock);
        deactivateLocked();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const juce::ScopedTryLock lock (processLock);
        const auto numSamples = buffer.getNumSamples();
        if (! lock.isLocked() || ! active || numSamples > currentBlock)
        {
            buffer.clear();
            return;
        }
        if (! processing)
            processing = plugin->start_processing (plugin);

        // Audio: the main input copied aside (CLAP doesn't promise in-place), other ports silent.
        const auto mainIns = getTotalNumInputChannels(), mainOuts = getTotalNumOutputChannels();
        inScratch.clear (0, numSamples);
        for (int c = 0; c < juce::jmin (mainIns, inScratch.getNumChannels()); ++c)
            inScratch.copyFrom (c, 0, buffer, c, 0, numSamples);

        int channel = 0;
        for (size_t p = 0; p < ports.inputs.size(); ++p)
        {
            inBuffers[p] = {};
            inBuffers[p].data32 = inPointers.data() + channel;
            inBuffers[p].channel_count = ports.inputs[p];
            for (uint32_t c = 0; c < ports.inputs[p]; ++c, ++channel)
                inPointers[(size_t) channel] = inScratch.getWritePointer (channel);
        }
        channel = 0;
        for (size_t p = 0; p < ports.outputs.size(); ++p)
        {
            outBuffers[p] = {};
            outBuffers[p].data32 = outPointers.data() + channel;
            outBuffers[p].channel_count = ports.outputs[p];
            for (uint32_t c = 0; c < ports.outputs[p]; ++c, ++channel)
                outPointers[(size_t) channel] = (p == 0 && (int) c < mainOuts && (int) c < buffer.getNumChannels())
                                                    ? buffer.getWritePointer ((int) c)
                                                    : outScratch.getWritePointer (channel);
        }

        // Events: parameter changes first (at time 0), then the notes in order.
        inputEvents.clear();
        pushParameterChanges();
        for (const auto metadata : midi)
            if (metadata.samplePosition < numSamples)
                pushMidi (metadata.getMessage(), (uint32_t) juce::jmax (0, metadata.samplePosition));

        // The transport.
        clap_event_transport_t transport {};
        transport.header = { sizeof (transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        if (auto* head = getPlayHead())
            if (const auto pos = head->getPosition())
            {
                if (const auto bpm = pos->getBpm())
                {
                    transport.flags |= CLAP_TRANSPORT_HAS_TEMPO;
                    transport.tempo = *bpm;
                }
                if (const auto ppq = pos->getPpqPosition())
                {
                    transport.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
                    transport.song_pos_beats = (clap_beattime) std::llround (*ppq * (double) CLAP_BEATTIME_FACTOR);
                    if (const auto bar = pos->getPpqPositionOfLastBarStart())
                        transport.bar_start = (clap_beattime) std::llround (*bar * (double) CLAP_BEATTIME_FACTOR);
                    if (const auto barCount = pos->getBarCount())
                        transport.bar_number = (int32_t) *barCount;
                }
                if (const auto seconds = pos->getTimeInSeconds())
                {
                    transport.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
                    transport.song_pos_seconds = (clap_sectime) std::llround (*seconds * (double) CLAP_SECTIME_FACTOR);
                }
                if (const auto sig = pos->getTimeSignature())
                {
                    transport.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
                    transport.tsig_num = (uint16_t) sig->numerator;
                    transport.tsig_denom = (uint16_t) sig->denominator;
                }
                if (pos->getIsPlaying())   transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
                if (pos->getIsRecording()) transport.flags |= CLAP_TRANSPORT_IS_RECORDING;
                if (pos->getIsLooping())   transport.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;
            }

        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) numSamples;
        process.transport = &transport;
        process.audio_inputs = inBuffers.data();
        process.audio_inputs_count = (uint32_t) inBuffers.size();
        process.audio_outputs = outBuffers.data();
        process.audio_outputs_count = (uint32_t) outBuffers.size();
        process.in_events = &inputEvents.list;
        process.out_events = &outputEvents;

        if (plugin->process (plugin, &process) == CLAP_PROCESS_ERROR)
            buffer.clear();
        steadyTime += numSamples;

        // Output channels the plugin doesn't have stay silent.
        for (int c = (ports.outputs.empty() ? 0 : (int) ports.outputs[0]); c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, numSamples);
        midi.clear();
    }

    using AudioProcessor::processBlock;

    double getTailLengthSeconds() const override
    {
        if (tailExt == nullptr || currentRate <= 0.0)
            return 0.0;
        const auto tail = tailExt->get (plugin);
        return tail >= (uint32_t) INT32_MAX ? std::numeric_limits<double>::infinity() : tail / currentRate;
    }

    bool acceptsMidi() const override  { return noteInputs > 0; }
    bool producesMidi() const override { return false; }
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        // Only the plugin's own main port sizes.
        const auto in = layout.getMainInputChannels(), out = layout.getMainOutputChannels();
        return in == (ports.inputs.empty() ? 0 : (int) ports.inputs[0]) && out == (ports.outputs.empty() ? 0 : (int) ports.outputs[0]);
    }

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        if (stateExt == nullptr)
            return;
        juce::MemoryOutputStream out (data, false);
        clap_ostream_t stream { &out, [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t
        {
            return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (buffer, (size_t) size) ? (int64_t) size : -1;
        } };
        stateExt->save (plugin, &stream);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (stateExt == nullptr)
            return;
        juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
        clap_istream_t stream { &in, [] (const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
        {
            return static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) juce::jmin<uint64_t> (size, (uint64_t) INT32_MAX));
        } };
        stateExt->load (plugin, &stream);
        refreshParameterValues();
    }

    //==========================================================================
    void queueParameterChange (ClapParameter& p, double plain)
    {
        {
            const juce::SpinLock::ScopedLockType writers (writeLock);   // (the UI and automation both write)
            const auto scope = changeFifo.write (1);   // (committed when this goes)
            if (scope.blockSize1 > 0)
                changeQueue[(size_t) scope.startIndex1] = { p.info.id, p.info.cookie, plain };
        }

        // With no audio running, the plugin is told on this thread.
        if (! active && paramsExt != nullptr)
        {
            const juce::ScopedLock lock (processLock);
            inputEvents.clear();
            pushParameterChanges();
            paramsExt->flush (plugin, &inputEvents.list, &outputEvents);
        }
    }

    juce::String parameterText (const ClapParameter& p, double plain) const
    {
        char text[256] = {};
        if (paramsExt != nullptr && paramsExt->value_to_text != nullptr && paramsExt->value_to_text (plugin, p.info.id, plain, text, sizeof (text)))
            return juce::String::fromUTF8 (text);
        return p.isStepped() ? juce::String ((int) plain) : juce::String (plain, 2);
    }

    bool parameterFromText (const ClapParameter& p, const juce::String& text, double& plain) const
    {
        return paramsExt != nullptr && paramsExt->text_to_value != nullptr && paramsExt->text_to_value (plugin, p.info.id, text.toRawUTF8(), &plain);
    }

    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* guiExt = nullptr;
    std::function<void (int, int)> onResizeRequest;   // set by the editor while it's open

private:
    struct Ports { std::vector<uint32_t> inputs, outputs; };

    //==========================================================================
    // What the plugin can ask of us.
    struct Host
    {
        ClapInstance* owner = nullptr;
        clap_host_t host;

        clap_host_params_t params {
            [] (const clap_host_t* h, clap_param_rescan_flags) { callOnMain (h, [] (ClapInstance& i) { i.refreshParameterValues(); }); },
            [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
            [] (const clap_host_t* h) { callOnMain (h, [] (ClapInstance& i) { i.flushIfIdle(); }); } };
        clap_host_state_t state { [] (const clap_host_t* h) { callOnMain (h, [] (ClapInstance& i) { i.updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); }); } };
        clap_host_latency_t latency { [] (const clap_host_t* h) { callOnMain (h, [] (ClapInstance& i) { if (i.latencyExt != nullptr && i.active) i.setLatencySamples ((int) i.latencyExt->get (i.plugin)); }); } };
        clap_host_tail_t tail { [] (const clap_host_t*) {} };
        clap_host_thread_check_t threadCheck {
            [] (const clap_host_t*) { return juce::MessageManager::existsAndIsCurrentThread(); },
            [] (const clap_host_t*) { return ! juce::MessageManager::existsAndIsCurrentThread(); } };
        clap_host_log_t log { [] (const clap_host_t*, clap_log_severity, const char* msg) { juce::ignoreUnused (msg); DBG ("CLAP: " << msg); } };
        clap_host_audio_ports_t audioPorts {
            [] (const clap_host_t*, uint32_t) { return false; },
            [] (const clap_host_t*, uint32_t) {} };
        clap_host_note_ports_t notePorts {
            [] (const clap_host_t*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; },
            [] (const clap_host_t*, uint32_t) {} };
        clap_host_gui_t gui {
            [] (const clap_host_t*) {},
            [] (const clap_host_t* h, uint32_t w, uint32_t hgt)
            {
                callOnMain (h, [w, hgt] (ClapInstance& i) { if (i.onResizeRequest) i.onResizeRequest ((int) w, (int) hgt); });
                return true;
            },
            [] (const clap_host_t*) { return false; },
            [] (const clap_host_t*) { return false; },
            [] (const clap_host_t*, bool) {} };
        clap_host_timer_support_t timer {
            [] (const clap_host_t* h, uint32_t period, clap_id* id)
            {
                auto* self = static_cast<Host*> (h->host_data);
                return self->owner != nullptr && self->owner->registerTimer (period, id);
            },
            [] (const clap_host_t* h, clap_id id)
            {
                auto* self = static_cast<Host*> (h->host_data);
                return self->owner != nullptr && self->owner->unregisterTimer (id);
            } };

        Host()
        {
            host.clap_version = CLAP_VERSION;
            host.host_data = this;
            host.name = "Audio Tech Pro";
            host.vendor = "Audio Tech Pro";
            host.url = "";
            host.version = "1.0";
            host.get_extension = [] (const clap_host_t* h, const char* id) -> const void*
            {
                auto* self = static_cast<Host*> (h->host_data);
                const juce::String ext (id);
                if (ext == CLAP_EXT_PARAMS)        return &self->params;
                if (ext == CLAP_EXT_STATE)         return &self->state;
                if (ext == CLAP_EXT_LATENCY)       return &self->latency;
                if (ext == CLAP_EXT_TAIL)          return &self->tail;
                if (ext == CLAP_EXT_THREAD_CHECK)  return &self->threadCheck;
                if (ext == CLAP_EXT_LOG)           return &self->log;
                if (ext == CLAP_EXT_AUDIO_PORTS)   return &self->audioPorts;
                if (ext == CLAP_EXT_NOTE_PORTS)    return &self->notePorts;
                if (ext == CLAP_EXT_GUI)           return &self->gui;
                if (ext == CLAP_EXT_TIMER_SUPPORT) return &self->timer;
                return nullptr;
            };
            host.request_restart = [] (const clap_host_t* h) { callOnMain (h, [] (ClapInstance& i) { i.restart(); }); };
            host.request_process = [] (const clap_host_t*) {};
            host.request_callback = [] (const clap_host_t* h) { callOnMain (h, [] (ClapInstance& i) { i.plugin->on_main_thread (i.plugin); }); };
        }

        template <typename Fn>
        static void callOnMain (const clap_host_t* h, Fn fn)
        {
            auto* self = static_cast<Host*> (h->host_data);
            if (self->owner == nullptr)
                return;
            juce::MessageManager::callAsync ([owner = self->owner, alive = std::weak_ptr<bool> (self->owner->alive), fn]
            {
                if (auto a = alive.lock(); a != nullptr && *a)
                    fn (*owner);
            });
        }
    };

    ClapInstance (std::shared_ptr<ClapModule> m, const juce::PluginDescription& desc, const clap_plugin_descriptor_t& d,
                  const clap_plugin_t* p, std::unique_ptr<Host> h, Ports pts, const BusesProperties& buses)
        : AudioPluginInstance (buses), plugin (p), module (std::move (m)), host (std::move (h)), ports (std::move (pts))
    {
        host->owner = this;
        description = desc;
        describe (description, d, module->path);
        description.numInputChannels = getTotalNumInputChannels();
        description.numOutputChannels = getTotalNumOutputChannels();

        paramsExt = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        stateExt = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        latencyExt = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
        tailExt = static_cast<const clap_plugin_tail_t*> (plugin->get_extension (plugin, CLAP_EXT_TAIL));
        guiExt = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
        timerExt = static_cast<const clap_plugin_timer_support_t*> (plugin->get_extension (plugin, CLAP_EXT_TIMER_SUPPORT));
        if (auto* notes = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS)))
        {
            noteInputs = notes->count (plugin, true);
            clap_note_port_info_t info {};
            if (noteInputs > 0 && notes->get (plugin, 0, true, &info))
                wantsMidiOnly = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0;
        }

        if (paramsExt != nullptr)
            for (uint32_t i = 0; i < paramsExt->count (plugin); ++i)
            {
                clap_param_info_t info {};
                if (paramsExt->get_info (plugin, i, &info) && (info.flags & CLAP_PARAM_IS_HIDDEN) == 0)
                {
                    auto param = std::make_unique<ClapParameter> (*this, info);
                    byId[info.id] = param.get();
                    addHostedParameter (std::move (param));
                }
            }
        refreshParameterValues();

        outputEvents.ctx = this;
        outputEvents.try_push = [] (const clap_output_events_t* list, const clap_event_header_t* event)
        {
            static_cast<ClapInstance*> (list->ctx)->fromPlugin (*event);
            return true;
        };
    }

    void deactivateLocked()
    {
        if (! active)
            return;
        if (processing)
            plugin->stop_processing (plugin);
        processing = false;
        plugin->deactivate (plugin);
        active = false;
    }

    void restart()
    {
        if (! active)
            return;
        prepareToPlay (currentRate, currentBlock);
    }

    void flushIfIdle()
    {
        if (active || paramsExt == nullptr)
            return;
        const juce::ScopedLock lock (processLock);
        inputEvents.clear();
        pushParameterChanges();
        paramsExt->flush (plugin, &inputEvents.list, &outputEvents);
    }

    void refreshParameterValues()
    {
        if (paramsExt == nullptr)
            return;
        for (auto& [id, param] : byId)
        {
            double v = 0.0;
            if (paramsExt->get_value (plugin, id, &v) && ! juce::exactlyEqual (v, param->value.load()))
                param->fromPlugin (v);
        }
    }

    void pushParameterChanges()
    {
        const auto ready = changeFifo.getNumReady();
        const auto scope = changeFifo.read (ready);
        auto push = [this] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
                if (auto* e = inputEvents.add())
                {
                    const auto& change = changeQueue[(size_t) i];
                    e->param = {};
                    e->param.header = { sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                    e->param.param_id = change.id;
                    e->param.cookie = change.cookie;
                    e->param.note_id = -1;
                    e->param.port_index = -1;
                    e->param.channel = -1;
                    e->param.key = -1;
                    e->param.value = change.value;
                }
        };
        push (scope.startIndex1, scope.blockSize1);
        push (scope.startIndex2, scope.blockSize2);
    }

    void pushMidi (const juce::MidiMessage& m, uint32_t time)
    {
        auto* e = inputEvents.add();
        if (e == nullptr)
            return;
        if (! wantsMidiOnly && (m.isNoteOn() || m.isNoteOff()))
        {
            e->note = {};
            e->note.header = { sizeof (clap_event_note_t), time, CLAP_CORE_EVENT_SPACE_ID, (uint16_t) (m.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0 };
            e->note.note_id = -1;
            e->note.port_index = 0;
            e->note.channel = (int16_t) (m.getChannel() - 1);
            e->note.key = (int16_t) m.getNoteNumber();
            e->note.velocity = m.getFloatVelocity();
            return;
        }
        if (m.getRawDataSize() > 3 || m.isSysEx())
        {
            --inputEvents.count;   // (SysEx isn't passed on)
            return;
        }
        e->midi = {};
        e->midi.header = { sizeof (clap_event_midi_t), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
        e->midi.port_index = 0;
        for (int i = 0; i < m.getRawDataSize(); ++i)
            e->midi.data[i] = m.getRawData()[i];
    }

    void fromPlugin (const clap_event_header_t& event)
    {
        if (event.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (event.type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto& e = reinterpret_cast<const clap_event_param_value_t&> (event);
            if (auto it = byId.find (e.param_id); it != byId.end())
                it->second->fromPlugin (e.value);
        }
        else if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event.type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            const auto& e = reinterpret_cast<const clap_event_param_gesture_t&> (event);
            if (auto it = byId.find (e.param_id); it != byId.end())
                event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? it->second->beginChangeGesture() : it->second->endChangeGesture();
        }
    }

    //==========================================================================
    struct PluginTimer final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    };

    bool registerTimer (uint32_t periodMs, clap_id* id)
    {
        if (timerExt == nullptr)
            return false;
        const auto newId = nextTimerId++;
        auto t = std::make_unique<PluginTimer>();
        t->tick = [this, newId] { timerExt->on_timer (plugin, newId); };
        t->startTimer ((int) juce::jmax (1u, periodMs));
        timers[newId] = std::move (t);
        *id = newId;
        return true;
    }

    bool unregisterTimer (clap_id id)
    {
        return timers.erase (id) > 0;
    }

    //==========================================================================
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    std::shared_ptr<ClapModule> module;
    std::unique_ptr<Host> host;
    juce::PluginDescription description;
    Ports ports;
    uint32_t noteInputs = 0;
    bool wantsMidiOnly = false;

    const clap_plugin_params_t* paramsExt = nullptr;
    const clap_plugin_state_t* stateExt = nullptr;
    const clap_plugin_latency_t* latencyExt = nullptr;
    const clap_plugin_tail_t* tailExt = nullptr;
    const clap_plugin_timer_support_t* timerExt = nullptr;
    std::map<clap_id, ClapParameter*> byId;

    juce::CriticalSection processLock;
    bool active = false, processing = false;
    double currentRate = 44100.0;
    int currentBlock = 512;
    int64_t steadyTime = 0;

    juce::AudioBuffer<float> inScratch, outScratch;
    std::vector<float*> inPointers, outPointers;
    std::vector<clap_audio_buffer_t> inBuffers, outBuffers;
    InputEvents inputEvents;
    clap_output_events_t outputEvents {};

    juce::AbstractFifo changeFifo { 1024 };
    juce::SpinLock writeLock;
    std::array<ParamChange, 1024> changeQueue {};

    std::map<clap_id, std::unique_ptr<PluginTimer>> timers;
    clap_id nextTimerId = 1;
};

void ClapParameter::setValue (float newValue)
{
    const auto plain = toPlain (newValue);
    value.store (plain);
    owner.queueParameterChange (*this, plain);
}

juce::String ClapParameter::getText (float normalised, int maximumLength) const
{
    return owner.parameterText (*this, toPlain (normalised)).substring (0, maximumLength);
}

float ClapParameter::getValueForText (const juce::String& text) const
{
    double plain = 0.0;
    if (owner.parameterFromText (*this, text, plain))
        return toNormalised (plain);
    return toNormalised (text.getDoubleValue());
}

//==============================================================================
// The plugin's own window, inside ours.
//==============================================================================
#if (JUCE_MAC || JUCE_WINDOWS) && JUCE_MODULE_AVAILABLE_juce_gui_extra
 #define RIFF_CLAP_EMBEDDED_GUI 1
class ClapEditor final : public juce::AudioProcessorEditor
{
public:
    static bool canOpen (ClapInstance& p)
    {
        return p.guiExt != nullptr && p.guiExt->is_api_supported (p.plugin, api(), false);
    }

    explicit ClapEditor (ClapInstance& p) : AudioProcessorEditor (p), instance (p)
    {
        created = instance.guiExt->create (instance.plugin, api(), false);
        uint32_t w = 600, h = 400;
        if (created)
            instance.guiExt->get_size (instance.plugin, &w, &h);
        resizable = created && instance.guiExt->can_resize (instance.plugin);
        setResizable (resizable, false);
        setSize ((int) juce::jmax (100u, w), (int) juce::jmax (60u, h));
        addAndMakeVisible (holder);

        instance.onResizeRequest = [this] (int nw, int nh)
        {
            const juce::ScopedValueSetter<bool> setter (resizingFromPlugin, true);
            setSize (nw, nh);
        };

       #if JUCE_MAC
        attach();
       #endif
    }

    ~ClapEditor() override
    {
        instance.onResizeRequest = nullptr;
        if (created)
        {
            instance.guiExt->hide (instance.plugin);
            instance.guiExt->destroy (instance.plugin);
        }
       #if JUCE_MAC
        holder.setView (nullptr);
        if (view != nullptr)
            ((void (*) (id, SEL)) objc_msgSend) ((id) view, sel_registerName ("release"));
       #else
        holder.setHWND (nullptr);
        if (child != nullptr)
            DestroyWindow (child);
       #endif
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (! created)
        {
            g.setColour (juce::Colours::white);
            g.drawText ("This plugin's window couldn't open", getLocalBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        holder.setBounds (getLocalBounds());
        if (created && resizable && ! resizingFromPlugin)
        {
            auto w = (uint32_t) getWidth(), h = (uint32_t) getHeight();
            instance.guiExt->adjust_size (instance.plugin, &w, &h);
            instance.guiExt->set_size (instance.plugin, w, h);
            if ((int) w != getWidth() || (int) h != getHeight())
            {
                const juce::ScopedValueSetter<bool> setter (resizingFromPlugin, true);
                setSize ((int) w, (int) h);
            }
        }
    }

   #if JUCE_WINDOWS
    void parentHierarchyChanged() override
    {
        if (child == nullptr && getPeer() != nullptr)
            attach();
    }
   #endif

private:
    static const char* api()
    {
       #if JUCE_MAC
        return CLAP_WINDOW_API_COCOA;
       #else
        return CLAP_WINDOW_API_WIN32;
       #endif
    }

    void attach()
    {
        if (! created)
            return;
        clap_window_t window {};
        window.api = api();
       #if JUCE_MAC
        // An empty NSView of our own for the plugin to put its view in.
        const CGRect frame = CGRectMake (0, 0, getWidth(), getHeight());
        auto allocated = ((id (*) (id, SEL)) objc_msgSend) ((id) objc_getClass ("NSView"), sel_registerName ("alloc"));
        view = ((id (*) (id, SEL, CGRect)) objc_msgSend) (allocated, sel_registerName ("initWithFrame:"), frame);
        holder.setView (view);
        window.cocoa = view;
       #else
        child = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, getWidth(), getHeight(), (HWND) getPeer()->getNativeHandle(), nullptr, nullptr, nullptr);
        holder.setHWND (child);
        window.win32 = child;
       #endif
        if (instance.guiExt->set_parent (instance.plugin, &window))
            instance.guiExt->show (instance.plugin);
        resized();
    }

    ClapInstance& instance;
    bool created = false, resizable = false, resizingFromPlugin = false;
   #if JUCE_MAC
    juce::NSViewComponent holder;
    void* view = nullptr;
   #else
    juce::HWNDComponent holder;
    HWND child = nullptr;
   #endif
};
#endif

juce::AudioProcessorEditor* ClapInstance::createEditor()
{
   #if RIFF_CLAP_EMBEDDED_GUI
    if (ClapEditor::canOpen (*this))
        return new ClapEditor (*this);
   #endif
    return new juce::GenericAudioProcessorEditor (*this);
}

} // namespace

//==============================================================================
// The format
//==============================================================================
void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier)
{
    auto module = ClapModule::load (fileOrIdentifier);
    if (module == nullptr)
        return;
    for (uint32_t i = 0; i < module->factory->get_plugin_count (module->factory); ++i)
        if (auto* d = module->factory->get_plugin_descriptor (module->factory, i))
        {
            auto* desc = results.add (new juce::PluginDescription());
            describe (*desc, *d, fileOrIdentifier);

            // The channel counts, from a short-lived instance.
            juce::String error;
            if (auto instance = ClapInstance::create (module, *desc, error))
            {
                desc->numInputChannels = instance->getTotalNumInputChannels();
                desc->numOutputChannels = instance->getTotalNumOutputChannels();
            }
        }
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    return f.hasFileExtension (".clap") && f.exists();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool)
{
    juce::StringArray found;
    std::function<void (const juce::File&)> search = [&] (const juce::File& dir)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.hasFileExtension (".clap"))
                found.add (f.getFullPathName());   // (a bundle is one plugin file: don't look inside)
            else if (recursive && entry.isDirectory())
                search (f);
        }
    };
    for (int i = 0; i < directories.getNumPaths(); ++i)
        search (directories[i]);
    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
   #if JUCE_MAC
    paths.add (juce::File ("~/Library/Audio/Plug-Ins/CLAP"));
    paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
   #elif JUCE_WINDOWS
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("CLAP"));
    paths.add (juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {})).getChildFile ("Programs").getChildFile ("Common").getChildFile ("CLAP"));
   #else
    paths.add (juce::File ("~/.clap"));
    paths.add (juce::File ("/usr/lib/clap"));
   #endif
    for (const auto& extra : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}),
                                                         juce::File::getSeparatorChar() == '\\' ? ";" : ":", {}))
        if (extra.isNotEmpty())
            paths.add (juce::File (extra));
    paths.removeRedundantPaths();
    paths.removeNonExistentPaths();
    return paths;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double rate, int block, PluginCreationCallback callback)
{
    auto module = ClapModule::load (desc.fileOrIdentifier);
    if (module == nullptr)
    {
        callback (nullptr, "Couldn't load " + desc.fileOrIdentifier);
        return;
    }
    juce::String error;
    auto instance = ClapInstance::create (module, desc, error);
    if (instance == nullptr)
    {
        callback (nullptr, error);
        return;
    }
    instance->setRateAndBufferSizeDetails (rate, block);
    callback (std::move (instance), {});
}

} // namespace riff
