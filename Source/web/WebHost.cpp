// The web version's engine: the app's own engine and project model compiled to WebAssembly, run inside the
// browser's audio thread (an AudioWorklet). JavaScript drives it through these functions; what it returns as text
// is JSON. One instance, one thread: nothing here waits or locks.
#include <JuceHeader.h>
#include <emscripten.h>
#include <deque>
#include "engine/AudioEngine.h"
#include "engine/AudioFileCache.h"
#include "engine/InstrumentRack.h"
#include "engine/EffectRack.h"
#include "engine/ProjectPlayer.h"
#include "engine/BuiltInEffects.h"
#include "model/Project.h"
#include "model/ProjectFiles.h"

//==============================================================================
// JUCE's message queue, for the browser: messages (async updates) wait here until the host runs them, after each
// call from JavaScript - there's no event loop of JUCE's own in an AudioWorklet.
namespace juce
{
    static std::deque<MessageManager::MessageBase::Ptr>& webQueue() { static std::deque<MessageManager::MessageBase::Ptr> q; return q; }
    void MessageManager::doPlatformSpecificInitialisation() {}
    void MessageManager::doPlatformSpecificShutdown() { webQueue().clear(); }
    bool MessageManager::postMessageToSystemQueue (MessageManager::MessageBase* const message)
    {
        webQueue().push_back (message);
        return true;
    }
    void MessageManager::broadcastMessage (const String&) {}
    namespace detail { bool dispatchNextMessageOnSystemQueue (bool); }
    bool detail::dispatchNextMessageOnSystemQueue (bool)
    {
        if (webQueue().empty())
            return false;
        auto m = webQueue().front();
        webQueue().pop_front();
        m->messageCallback();
        return true;
    }

    // The few native pieces JUCE has no WebAssembly version of: no memory mapping (everything's read into
    // memory), and no threads (the engine runs on the audio thread alone). Files use JUCE's POSIX code.
    void MemoryMappedFile::openInternal (const File&, AccessMode, bool) { address = nullptr; range = {}; }
    MemoryMappedFile::~MemoryMappedFile() {}

    // The browser's in-memory file system: everything lives under /home/web (projects, settings).
    File File::getSpecialLocation (SpecialLocationType type)
    {
        switch (type)
        {
            case tempDirectory:                 return File ("/tmp");
            case currentExecutableFile:
            case currentApplicationFile:
            case invokedExecutableFile:
            case hostApplicationPath:           return File ("/app/atp");
            case globalApplicationsDirectory:   return File ("/app");
            default:                            return File ("/home/web");
        }
    }
    bool Thread::createNativeThread (Priority) { return false; }
    void Thread::killThread() {}
}

namespace riff::web
{
namespace
{
    struct Host
    {
        AudioEngine engine;
        AudioFileCache cache { engine.getFormatManager(), false };
        InstrumentRack rack;
        EffectRack effects { rack.getFormatManager() };
        ProjectPlayer player { engine, cache, &rack, &effects };
        juce::ValueTree project = model::createProject();
        juce::File projectFile;
        double sampleRate = 48000.0;
        std::string text;   // the last answer given as text (valid until the next call)
    };
    Host* host = nullptr;

    void pump()
    {
        for (int i = 0; i < 1000 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
    }

    const char* answer (const juce::String& s)
    {
        host->text = s.toStdString();
        return host->text.c_str();
    }

    juce::File root() { return juce::File ("/project"); }

    double projectEnd (const juce::ValueTree& project)
    {
        double end = 0.0;
        for (const auto& t : project)
            for (const auto& r : t)
                if (r.hasType (ids::REGION))
                    end = juce::jmax (end, model::regionEnd (r));
        return end;
    }

    juce::var insertsJson (const juce::String& owner)
    {
        juce::Array<juce::var> inserts;
        for (const auto& insert : model::insertsOf (model::findChain (host->project, owner)))
        {
            auto* io = new juce::DynamicObject();
            const auto builtIn = insert[ids::kind].toString() != model::pluginKind;
            const auto* info = builtIn ? findBuiltInEffect (insert[ids::effect].toString()) : nullptr;
            io->setProperty ("id", insert[ids::id].toString());
            io->setProperty ("name", info != nullptr ? info->name : insert[ids::name].toString().isNotEmpty() ? insert[ids::name].toString() : juce::String ("Plugin"));
            io->setProperty ("plugin", ! builtIn || info == nullptr);   // (a placeholder here: kept for the desktop app)
            io->setProperty ("bypass", (bool) insert[ids::bypass]);
            inserts.add (juce::var (io));
        }
        return inserts;
    }

    juce::ValueTree insertById (const juce::String& id)
    {
        for (auto chain : model::mixerOf (host->project))
            if (auto insert = chain.getChildWithProperty (ids::id, id); insert.isValid() && insert.hasType (ids::INSERT))
                return insert;
        return {};
    }

    juce::var describe()
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("name", host->project[ids::name].toString());
        p->setProperty ("tempo", host->project.getProperty (ids::tempo, 120.0));
        p->setProperty ("length", projectEnd (host->project));
        p->setProperty ("masterGain", host->project.getProperty (ids::masterGain, 0.8));
        juce::Array<juce::var> tracks;
        for (const auto& t : host->project)
        {
            if (! t.hasType (ids::TRACK))
                continue;
            auto* o = new juce::DynamicObject();
            for (const auto& id : { ids::id, ids::name, ids::kind, ids::colour })
                o->setProperty (id, t[id].toString());
            o->setProperty ("volume", t.getProperty (ids::volume, 1.0));
            o->setProperty ("pan", t.getProperty (ids::pan, 0.0));
            o->setProperty ("mute", (bool) t[ids::mute]);
            o->setProperty ("solo", (bool) t[ids::solo]);
            juce::Array<juce::var> regions;
            for (const auto& r : t)
                if (r.hasType (ids::REGION))
                {
                    auto* ro = new juce::DynamicObject();
                    ro->setProperty ("id", r[ids::id].toString());
                    ro->setProperty ("name", r[ids::name].toString());
                    ro->setProperty ("start", r.getProperty (ids::start, 0.0));
                    ro->setProperty ("length", r.getProperty (ids::length, 0.0));
                    ro->setProperty ("midi", model::isMidiRegion (r));
                    ro->setProperty ("muted", model::isRegionMuted (r));
                    ro->setProperty ("missing", ! model::isMidiRegion (r) && ! juce::File (r[ids::file].toString()).existsAsFile());
                    regions.add (juce::var (ro));
                }
            o->setProperty ("regions", regions);
            o->setProperty ("inserts", insertsJson (t[ids::id].toString()));
            tracks.add (juce::var (o));
        }
        p->setProperty ("tracks", tracks);
        p->setProperty ("masterInserts", insertsJson ("master"));
        return juce::var (p);
    }

    juce::ValueTree trackById (const char* id) { return model::findById (host->project, juce::String::fromUTF8 (id)); }
}
}

using namespace riff;
using namespace riff::web;

extern "C"
{

EMSCRIPTEN_KEEPALIVE int atp_init (double sampleRate)
{
    if (host == nullptr)
    {
        juce::MessageManager::getInstance();
        host = new Host();
    }
    host->sampleRate = sampleRate;
    host->cache.setStreamingThreshold (std::numeric_limits<juce::int64>::max());   // (everything in memory: no mapped files here)
    host->engine.setRenderThreads (0);
    host->engine.prepare (sampleRate, 128);
    host->engine.setActiveOutputs (juce::BigInteger (3));
    host->player.setProject (host->project);
    pump();
    return 1;
}

/** A project from a .zip (the desktop app's Save a Copy): unpacked into the browser's memory and opened. */
EMSCRIPTEN_KEEPALIVE const char* atp_open_zip (const void* data, int size)
{
    host->engine.stop();
    root().deleteRecursively();
    root().createDirectory();
    juce::MemoryInputStream in (data, (size_t) size, false);
    juce::ZipFile zip (in);
    if (zip.getNumEntries() == 0)
        return answer ("{\"error\":\"That isn't a zip file.\"}");
    const auto result = zip.uncompressTo (root(), true);
    if (result.failed())
        return answer ("{\"error\":" + juce::JSON::toString (result.getErrorMessage()) + "}");

    juce::File file;
    for (const auto& entry : juce::RangedDirectoryIterator (root(), true, "*" + model::fileExtension, juce::File::findFiles))
        if (! entry.getFile().getFullPathName().contains ("Project Backups") && ! entry.getFile().getFullPathName().contains ("Autosave"))
        {
            file = entry.getFile();
            break;
        }
    if (file == juce::File())
        return answer ("{\"error\":\"There's no project in that zip.\"}");

    juce::String error;
    auto loaded = model::load (file, error);
    if (! loaded.isValid())
        return answer ("{\"error\":" + juce::JSON::toString (error) + "}");
    model::files::MediaIndex index;
    index.add (root());
    model::files::relinkMissing (loaded, index, nullptr);

    host->project = loaded;
    host->projectFile = file;
    host->player.setProject (host->project);
    pump();
    host->player.rebuildNow();
    return answer (juce::JSON::toString (describe(), true));
}

EMSCRIPTEN_KEEPALIVE const char* atp_describe() { pump(); return answer (juce::JSON::toString (describe(), true)); }

/** A new, empty project. */
EMSCRIPTEN_KEEPALIVE const char* atp_new_project (const char* name)
{
    host->engine.stop();
    root().deleteRecursively();
    root().createDirectory();
    host->project = model::createProject (juce::String::fromUTF8 (name));
    host->projectFile = root().getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name)) + model::fileExtension);
    host->player.setProject (host->project);
    pump();
    return answer (juce::JSON::toString (describe(), true));
}

/** An audio file (already written into the browser's memory at `path`) on a new track, at `start` seconds. */
EMSCRIPTEN_KEEPALIVE const char* atp_add_audio_track (const char* path, double start)
{
    const juce::File file (juce::String::fromUTF8 (path));
    const auto seconds = host->cache.getLengthSeconds (file);
    if (seconds <= 0.0)
        return answer ("{\"error\":\"Couldn't read " + file.getFileName() + " (WAV, AIFF, FLAC or Ogg).\"}");
    auto track = model::addTrack (host->project, file.getFileNameWithoutExtension(), nullptr);
    model::addRegion (track, file, start, seconds, nullptr);
    pump();
    host->player.rebuildNow();
    return answer (juce::JSON::toString (describe(), true));
}

EMSCRIPTEN_KEEPALIVE void atp_set_track (const char* id, const char* key, double value)
{
    auto track = trackById (id);
    if (! track.isValid())
        return;
    const auto k = juce::String::fromUTF8 (key);
    if (k == "volume")     track.setProperty (ids::volume, juce::jmax (0.0, value), nullptr);
    else if (k == "pan")   track.setProperty (ids::pan, juce::jlimit (-1.0, 1.0, value), nullptr);
    else if (k == "mute")  track.setProperty (ids::mute, value > 0.5, nullptr);
    else if (k == "solo")  track.setProperty (ids::solo, value > 0.5, nullptr);
    pump();
}

EMSCRIPTEN_KEEPALIVE void atp_set_track_name (const char* id, const char* name)
{
    if (auto track = trackById (id); track.isValid())
        track.setProperty (ids::name, juce::String::fromUTF8 (name), nullptr);
    pump();
}

EMSCRIPTEN_KEEPALIVE void atp_set_bypass (const char* insertId, int bypass)
{
    for (auto chain : model::mixerOf (host->project))
        if (auto insert = chain.getChildWithProperty (ids::id, juce::String::fromUTF8 (insertId)); insert.isValid())
            insert.setProperty (ids::bypass, bypass != 0, nullptr);
    pump();
}

/** A region moved (`start`, seconds), muted, or its gain set (linear); "delete" removes it. */
EMSCRIPTEN_KEEPALIVE void atp_set_region (const char* id, const char* key, double value)
{
    auto region = model::findById (host->project, juce::String::fromUTF8 (id));
    if (! region.isValid() || ! region.hasType (ids::REGION))
        return;
    const auto k = juce::String::fromUTF8 (key);
    if (k == "start")       region.setProperty (ids::start, juce::jmax (0.0, value), nullptr);
    else if (k == "mute")   region.setProperty (ids::mute, value > 0.5, nullptr);
    else if (k == "gain")   region.setProperty (ids::gain, juce::jmax (0.0, value), nullptr);
    else if (k == "delete") region.getParent().removeChild (region, nullptr);
    pump();
    host->player.rebuildNow();
}

/** A region moved onto another track. */
EMSCRIPTEN_KEEPALIVE void atp_move_region_to_track (const char* regionId, const char* trackId)
{
    auto region = model::findById (host->project, juce::String::fromUTF8 (regionId));
    auto track = trackById (trackId);
    if (! region.isValid() || ! track.isValid() || region.getParent() == track)
        return;
    region.getParent().removeChild (region, nullptr);
    track.appendChild (region, nullptr);
    pump();
    host->player.rebuildNow();
}

EMSCRIPTEN_KEEPALIVE void atp_remove_track (const char* id)
{
    if (auto track = trackById (id); track.isValid())
        host->project.removeChild (track, nullptr);
    pump();
    host->player.rebuildNow();
}

/** An audio file added as a region on an existing track (a recorded take, say). */
EMSCRIPTEN_KEEPALIVE const char* atp_add_region (const char* trackId, const char* path, double start)
{
    auto track = trackById (trackId);
    const juce::File file (juce::String::fromUTF8 (path));
    const auto seconds = host->cache.getLengthSeconds (file);
    if (! track.isValid() || seconds <= 0.0)
        return answer ("{\"error\":\"Couldn't add " + file.getFileName() + ".\"}");
    model::addRegion (track, file, juce::jmax (0.0, start), seconds, nullptr);
    pump();
    host->player.rebuildNow();
    return answer (juce::JSON::toString (describe(), true));
}

/** A new, empty audio track. */
EMSCRIPTEN_KEEPALIVE const char* atp_add_track (const char* name)
{
    model::addTrack (host->project, juce::String::fromUTF8 (name), nullptr);
    pump();
    host->player.rebuildNow();
    return answer (juce::JSON::toString (describe(), true));
}

/** The built-in effects, for the insert menu: [{ type, name, category }]. */
EMSCRIPTEN_KEEPALIVE const char* atp_effects()
{
    juce::Array<juce::var> list;
    for (const auto& e : builtInEffects())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", e.type);
        o->setProperty ("name", e.name);
        o->setProperty ("category", e.category.isNotEmpty() ? e.category : juce::String ("Other"));
        list.add (juce::var (o));
    }
    return answer (juce::JSON::toString (juce::var (list), true));
}

/** A built-in effect added last on a channel (a track's id, or "master"). */
EMSCRIPTEN_KEEPALIVE const char* atp_add_effect (const char* owner, const char* type)
{
    const auto o = juce::String::fromUTF8 (owner);
    if (findBuiltInEffect (juce::String::fromUTF8 (type)) == nullptr || (o != "master" && ! trackById (owner).isValid()))
        return answer ("{\"error\":\"There's no such effect.\"}");
    auto insert = model::addBuiltinInsert (model::chainFor (host->project, o), juce::String::fromUTF8 (type), -1, nullptr);
    pump();
    host->player.rebuildNow();
    return answer ("{\"id\":" + juce::JSON::toString (insert[ids::id].toString()) + "}");
}

EMSCRIPTEN_KEEPALIVE void atp_remove_effect (const char* insertId)
{
    if (auto insert = insertById (juce::String::fromUTF8 (insertId)); insert.isValid())
        model::removeInsert (insert, nullptr);
    pump();
    host->player.rebuildNow();
}

/** An insert's knobs: { name, params: [{ id, name, suffix, min, max, def, mid, value, choices }] }. */
EMSCRIPTEN_KEEPALIVE const char* atp_effect_params (const char* insertId)
{
    const auto insert = insertById (juce::String::fromUTF8 (insertId));
    const auto* info = insert.isValid() ? findBuiltInEffect (insert[ids::effect].toString()) : nullptr;
    if (info == nullptr)
        return answer ("{\"error\":\"That's a plugin: it opens in the desktop app.\"}");
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", info->name);
    o->setProperty ("bypass", (bool) insert[ids::bypass]);
    juce::Array<juce::var> params;
    for (const auto& p : info->params)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("id", p.id);
        po->setProperty ("name", p.name);
        po->setProperty ("suffix", p.suffix);
        po->setProperty ("min", p.min);
        po->setProperty ("max", p.max);
        po->setProperty ("def", p.defaultValue);
        po->setProperty ("mid", p.midPoint);
        po->setProperty ("value", insert.getProperty (juce::Identifier (p.id), p.defaultValue));
        juce::Array<juce::var> choices;
        for (const auto& c : p.choices)
            choices.add (c);
        po->setProperty ("choices", choices);
        params.add (juce::var (po));
    }
    o->setProperty ("params", params);
    return answer (juce::JSON::toString (juce::var (o), true));
}

EMSCRIPTEN_KEEPALIVE void atp_set_effect_param (const char* insertId, const char* paramId, double value)
{
    if (auto insert = insertById (juce::String::fromUTF8 (insertId)); insert.isValid())
        insert.setProperty (juce::Identifier (juce::String::fromUTF8 (paramId)), value, nullptr);
    pump();
}

EMSCRIPTEN_KEEPALIVE void atp_set_master (double gain) { host->project.setProperty (ids::masterGain, juce::jmax (0.0, gain), nullptr); pump(); }

EMSCRIPTEN_KEEPALIVE void atp_play()  { pump(); host->engine.play(); }
EMSCRIPTEN_KEEPALIVE void atp_stop()  { host->engine.stop(); }
EMSCRIPTEN_KEEPALIVE void atp_seek (double seconds) { host->engine.setPlayheadSeconds (juce::jmax (0.0, seconds)); }
EMSCRIPTEN_KEEPALIVE double atp_position() { return host->engine.getPlayheadSeconds(); }
EMSCRIPTEN_KEEPALIVE int atp_is_playing() { return host->engine.isPlaying() ? 1 : 0; }

/** One block of the song, into two channels the caller gives (planar floats). */
EMSCRIPTEN_KEEPALIVE void atp_process (float* left, float* right, int frames)
{
    float* outs[] { left, right };
    host->engine.render (nullptr, 0, outs, 2, frames);
}

/** Levels since last asked: master L, R, then each track's L, R in the project's order (linear peaks). */
EMSCRIPTEN_KEEPALIVE int atp_levels (float* dest, int capacity)
{
    int n = 0;
    const auto master = host->engine.takeLevels();
    if (capacity >= 2) { dest[n++] = master.outL; dest[n++] = master.outR; }
    for (const auto& t : host->project)
        if (t.hasType (ids::TRACK) && n + 2 <= capacity)
        {
            const auto [l, r] = host->engine.takeTrackLevel (host->player.slotOf (t[ids::id].toString()));
            dest[n++] = l;
            dest[n++] = r;
        }
    return n;
}

/** A region's waveform for drawing: `points` pairs of min/max (of the left channel, or the louder), as floats. */
EMSCRIPTEN_KEEPALIVE int atp_waveform (const char* regionId, float* dest, int points)
{
    const auto region = model::findById (host->project, juce::String::fromUTF8 (regionId));
    if (! region.isValid() || model::isMidiRegion (region) || points <= 0)
        return 0;
    auto audio = host->cache.get (juce::File (region[ids::file].toString()));
    if (audio == nullptr || audio->isStreamed() || audio->buffer.getNumSamples() == 0)
        return 0;
    const auto& b = audio->buffer;
    const auto rate = audio->sampleRate > 0.0 ? audio->sampleRate : host->sampleRate;
    const auto from = (juce::int64) ((double) region.getProperty (ids::offset, 0.0) * rate);
    const auto count = (juce::int64) ((double) region.getProperty (ids::length, 0.0) * rate);
    for (int p = 0; p < points; ++p)
    {
        const auto a = from + count * p / points, e = juce::jmax (a + 1, from + count * (p + 1) / points);
        float lo = 0.0f, hi = 0.0f;
        for (auto i = a; i < e && i < b.getNumSamples(); i += juce::jmax ((juce::int64) 1, (e - a) / 64))
            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                const auto v = b.getSample (c, (int) i);
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
        dest[p * 2] = lo;
        dest[p * 2 + 1] = hi;
    }
    return points;
}

/** The project and its audio as a .zip (for the desktop app), written to `path` in the browser's memory. */
EMSCRIPTEN_KEEPALIVE const char* atp_save_zip (const char* path)
{
    const juce::File out (juce::String::fromUTF8 (path));
    juce::String error;
    const auto name = host->project[ids::name].toString().isNotEmpty() ? host->project[ids::name].toString() : juce::String ("Untitled");
    const auto tempParent = juce::File ("/export");
    tempParent.deleteRecursively();
    tempParent.createDirectory();
    const auto copy = model::files::saveCopyWithMedia (host->project, tempParent, name, {}, error);
    if (! copy.existsAsFile() || ! model::files::zipFolder (copy.getParentDirectory(), out, {}, error))
        return answer ("{\"error\":" + juce::JSON::toString (error) + "}");
    tempParent.deleteRecursively();
    return answer ("{\"ok\":true}");
}

/** The mix, rendered from `start` to `end` seconds into a 24-bit WAV at `path`. */
EMSCRIPTEN_KEEPALIVE const char* atp_export_wav (const char* path, double start, double end)
{
    const juce::File out (juce::String::fromUTF8 (path));
    host->engine.stop();
    host->engine.setPlayheadSeconds (start);
    host->engine.play();
    const auto total = (juce::int64) ((end - start) * host->sampleRate);
    juce::AudioBuffer<float> block (2, 128);
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (host->sampleRate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return answer ("{\"error\":\"Couldn't write the file.\"}");
    for (juce::int64 done = 0; done < total; done += 128)
    {
        const auto n = (int) juce::jmin ((juce::int64) 128, total - done);
        host->engine.render (nullptr, 0, block.getArrayOfWritePointers(), 2, n);
        writer->writeFromAudioSampleBuffer (block, 0, n);
    }
    writer.reset();
    host->engine.stop();
    host->engine.setPlayheadSeconds (start);
    return answer ("{\"ok\":true}");
}

}
