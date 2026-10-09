#include "ProjectPlayer.h"
#include "../model/Project.h"

namespace riff
{

ProjectPlayer::ProjectPlayer (AudioEngine& e, AudioFileCache& c, InstrumentRack* r, EffectRack* fx)
    : engine (e), cache (c), rack (r), effects (fx)
{
    engine.getDeviceManager().addChangeListener (this);
    cache.addChangeListener (this);   // audio finished loading in the background
}

ProjectPlayer::~ProjectPlayer()
{
    engine.getDeviceManager().removeChangeListener (this);
    cache.removeChangeListener (this);
    project.removeListener (this);
    cancelPendingUpdate();
}

void ProjectPlayer::setProject (juce::ValueTree newProject)
{
    project.removeListener (this);
    project = newProject;
    project.addListener (this);

    slots.clear();
    nextSlot = 0;
    rebuildNow();
}

void ProjectPlayer::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &cache)
    {
        requestRebuild();   // newly loaded audio joins the song
        return;
    }

    // Device settings changed: rebuild if the sample rate is different.
    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        if (std::abs (device->getCurrentSampleRate() - builtForRate) > 0.5)
            rebuildNow();
}

void ProjectPlayer::setLiveMidiTrack (const juce::String& trackId)
{
    if (trackId != liveMidiTrackId)
    {
        liveMidiTrackId = trackId;
        triggerAsyncUpdate();
    }
}

int ProjectPlayer::slotOf (const juce::String& trackId) const
{
    auto it = slots.find (trackId);
    return it != slots.end() ? it->second : -1;
}

std::vector<std::shared_ptr<Effect>> ProjectPlayer::buildChain (const juce::String& owner, double sampleRate,
                                                                juce::StringArray& insertIds,
                                                                std::map<juce::String, std::shared_ptr<Effect>>& effectsById)
{
    std::vector<std::shared_ptr<Effect>> chain;

    if (effects == nullptr)
        return chain;

    for (const auto& insert : model::insertsOf (model::findChain (project, owner)))
    {
        const auto id = insert[ids::id].toString();
        insertIds.add (id);

        if ((bool) insert[ids::bypass])
            continue;

        if (auto effect = effects->effectFor (insert, sampleRate, AudioEngine::maxChunkSize))
        {
            effectsById[id] = effect;
            chain.push_back (std::move (effect));
        }
    }

    return chain;
}

PlaybackGraph::Curve ProjectPlayer::curveFor (const juce::String& owner, const juce::String& param, double sampleRate, bool)
{
    PlaybackGraph::Curve curve;
    const auto lane = model::laneFor (project, owner, param, false);

    for (const auto& point : lane)
    {
        curve.times.push_back ((juce::int64) std::llround ((double) point[ids::time] * sampleRate));
        curve.values.push_back ((float) (double) point[ids::value]);
    }

    return curve;
}

void ProjectPlayer::addParamAutomation (PlaybackGraph::Track& t, const juce::String& owner, double sampleRate,
                                        const std::map<juce::String, std::shared_ptr<Effect>>& effectsById)
{
    // Lanes named "insert:<insert id>:<parameter id>" drive built-in effect knobs.
    for (const auto& lane : model::mixerOf (project))
    {
        if (! lane.hasType (ids::LANE) || lane[ids::owner].toString() != owner || lane.getNumChildren() == 0)
            continue;

        const auto parts = juce::StringArray::fromTokens (lane[ids::param].toString(), ":", {});
        if (parts.size() != 3 || parts[0] != "insert")
            continue;

        auto it = effectsById.find (parts[1]);
        if (it == effectsById.end())
            continue;

        auto* builtIn = dynamic_cast<BuiltInEffect*> (it->second.get());
        if (builtIn == nullptr)
            continue;

        const auto& params = builtIn->getInfo().params;
        for (size_t p = 0; p < params.size(); ++p)
            if (params[p].id == parts[2])
                t.paramAutomation.push_back ({ builtIn, (int) p, curveFor (owner, lane[ids::param].toString(), sampleRate) });
    }
}

int ProjectPlayer::slotFor (const juce::String& trackId)
{
    if (auto it = slots.find (trackId); it != slots.end())
        return it->second;

    // Reuse the lowest free slot once we run past the end.
    int slot = nextSlot < PlaybackGraph::maxTracks ? nextSlot++ : -1;

    if (slot < 0)
    {
        std::vector<bool> used (PlaybackGraph::maxTracks, false);
        for (const auto& track : project)
            if (auto it = slots.find (track[ids::id].toString()); it != slots.end())
                used[(size_t) it->second] = true;

        for (int i = 0; i < PlaybackGraph::maxTracks && slot < 0; ++i)
            if (! used[(size_t) i])
                slot = i;
    }

    if (slot >= 0)
        slots[trackId] = slot;

    return slot;
}

std::unique_ptr<PlaybackGraph> ProjectPlayer::buildGraph (double sampleRate)
{
    auto graph = std::make_unique<PlaybackGraph>();
    graph->sampleRate = sampleRate;
    graph->masterGain = (float) (double) project.getProperty (ids::masterGain, 0.8);
    graph->metronome = project.getProperty (ids::metronome, false);
    graph->busComp = MasterBusCompressor::fromProject (project);
    graph->secondsPerBeat = model::secondsPerBeat (project);
    graph->beatsPerBar = juce::jlimit (1, 16, (int) project.getProperty (ids::timeSigNumerator, 4));

    auto toSamples = [sampleRate] (double seconds) { return (juce::int64) std::llround (seconds * sampleRate); };

    graph->cycleEnabled = project.getProperty (ids::cycleEnabled, false);
    graph->cycleStart = toSamples (project.getProperty (ids::cycleStart, 0.0));
    graph->cycleEnd = toSamples (project.getProperty (ids::cycleEnd, 0.0));

    juce::StringArray insertIds;
    std::map<juce::String, std::shared_ptr<Effect>> effectsById;
    graph->masterInserts = buildChain (model::masterOwner, sampleRate, insertIds, effectsById);
    graph->busCompPlugins = buildChain (model::busCompOwner, sampleRate, insertIds, effectsById);
    graph->masterVolumeAutomation = curveFor (model::masterOwner, model::volumeParam, sampleRate);

    // Buses get numbers so sends can find them.
    std::map<juce::String, int> busIndex;
    for (const auto& track : project)
        if (track.hasType (ids::TRACK) && model::isBusTrack (track) && (int) busIndex.size() < PlaybackGraph::maxBuses)
            busIndex[track[ids::id].toString()] = (int) busIndex.size();

    std::vector<int> latencies; // per graph track, for delay compensation

    bool anySolo = false;
    for (const auto& track : project)
        anySolo = anySolo || (bool) track[ids::solo];

    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;

        PlaybackGraph::Track t;
        t.slot    = slotFor (track[ids::id].toString());
        t.volume  = (float) (double) track.getProperty (ids::volume, 1.0);
        t.console = ConsoleChannel::fromTrack (track);
        t.pan     = (float) juce::jlimit (-1.0, 1.0, (double) track.getProperty (ids::pan, 0.0));
        t.audible = ! (bool) track[ids::mute] && (! anySolo || (bool) track[ids::solo]);

        t.monitorInput = (bool) track[ids::armed] && (bool) track[ids::monitor];
        t.inputChannel = juce::jmax (0, (int) track.getProperty (ids::input, 0));
        t.inputStereo  = track.getProperty (ids::inputStereo, false);

        if (t.slot < 0)
            continue;

        const auto trackId = track[ids::id].toString();
        t.inserts = buildChain (trackId, sampleRate, insertIds, effectsById);
        t.stripPlugins = buildChain (model::stripOwner (trackId), sampleRate, insertIds, effectsById);
        if (model::stripPlugin (project, trackId).isValid())
            t.console.enabled = false;   // the plugin replaces the built-in strip (dry while it loads)
        t.isInstrument = model::isInstrumentTrack (track);
        t.isBus = model::isBusTrack (track);

        // Automation
        t.volumeAutomation = curveFor (trackId, model::volumeParam, sampleRate);
        t.panAutomation = curveFor (trackId, model::panParam, sampleRate);
        addParamAutomation (t, trackId, sampleRate, effectsById);

        int latency = 0;
        for (const auto& e : t.stripPlugins)
            latency += e->getLatencySamples();
        for (const auto& e : t.inserts)
            latency += e->getLatencySamples();

        if (t.isBus)
        {
            // Buses are solo-safe: soloing a track keeps its reverb bus audible.
            t.audible = ! (bool) track[ids::mute];
            auto it = busIndex.find (trackId);
            t.busIndex = it != busIndex.end() ? it->second : -1;
            graph->tracks.push_back (std::move (t));
            latencies.push_back (latency);
            continue;
        }

        for (const auto& send : model::sendsOf (model::findChain (project, trackId)))
        {
            auto it = busIndex.find (send[ids::target].toString());
            if (it != busIndex.end())
                t.sends.push_back ({ it->second, (float) (double) send.getProperty (ids::level, 1.0), (bool) send[ids::pre] });
        }
        t.receivesLiveMidi = t.isInstrument && track[ids::id].toString() == liveMidiTrackId;

        if (t.isInstrument)
        {
            if (rack != nullptr)
                t.instrument = rack->instrumentFor (track, sampleRate, AudioEngine::maxChunkSize);

            if (t.instrument != nullptr)
                latency += t.instrument->getLatencySamples();

            for (const auto& region : track)
            {
                if (! model::isMidiRegion (region) || model::isRegionMuted (region))
                    continue;

                PlaybackGraph::Region r;
                r.start  = toSamples (region[ids::start]);
                r.length = toSamples (region[ids::length]);

                const double regionStart = region[ids::start];
                const double offset = region[ids::offset];
                const double windowEnd = offset + (double) region[ids::length];

                for (const auto& note : region)
                {
                    if (! note.hasType (ids::NOTE))
                        continue;

                    // Notes play only if they start inside the region's window.
                    const double noteStart = note[ids::start];
                    if (noteStart < offset || noteStart >= windowEnd)
                        continue;

                    const double noteEnd = juce::jmin (noteStart + (double) note[ids::length], windowEnd);

                    PlaybackGraph::Note n;
                    n.start    = toSamples (regionStart + noteStart - offset);
                    n.end      = juce::jmax (n.start + 1, toSamples (regionStart + noteEnd - offset));
                    n.pitch    = (juce::uint8) juce::jlimit (0, 127, (int) note[ids::pitch]);
                    n.velocity = (juce::uint8) juce::jlimit (1, 127, (int) note[ids::velocity]);
                    r.notes.push_back (n);
                }

                std::sort (r.notes.begin(), r.notes.end(),
                           [] (const auto& a, const auto& b) { return a.start < b.start; });
                t.regions.push_back (std::move (r));
            }

            graph->tracks.push_back (std::move (t));
            latencies.push_back (latency);
            continue;
        }

        for (const auto& region : track)
        {
            if (model::isRegionMuted (region))
                continue;

            PlaybackGraph::Region r;
            r.audio        = cache.get (juce::File (region[ids::file].toString()));
            r.start        = toSamples (region[ids::start]);
            r.length       = toSamples (region[ids::length]);

            if (r.audio != nullptr)
            {
                // A file at another sample rate is converted as it plays.
                const auto fileRate = r.audio->sampleRate;
                r.resample = std::abs (fileRate - sampleRate) > 0.5;
                r.step = r.resample ? fileRate / sampleRate : 1.0;
                r.sourcePosition = (double) region[ids::offset] * fileRate;
                r.sourceOffset = (juce::int64) std::llround (r.sourcePosition);
            }
            r.gain         = (float) (double) region.getProperty (ids::gain, 1.0);

            if (r.audio != nullptr && r.length > 0)
                t.regions.push_back (std::move (r));
        }

        graph->tracks.push_back (std::move (t));
        latencies.push_back (latency);
    }

    // Delay compensation. Every ordinary track is delayed to match the slowest
    // one (D); buses add their own latency on top, so the direct sum and each
    // bus are delayed again to line up with the slowest bus (M).
    int slowestTrack = 0, slowestBus = 0;
    for (size_t i = 0; i < graph->tracks.size(); ++i)
        (graph->tracks[i].isBus ? slowestBus : slowestTrack) = juce::jmax (graph->tracks[i].isBus ? slowestBus : slowestTrack, latencies[i]);

    slowestTrack = juce::jmin (slowestTrack, DelayLine::maxDelay);
    slowestBus = juce::jmin (slowestBus, DelayLine::maxDelay - slowestTrack);

    for (size_t i = 0; i < graph->tracks.size(); ++i)
    {
        auto& t = graph->tracks[i];
        if (t.isBus)
            t.postDelay = juce::jmax (0, slowestBus - latencies[i]);
        else
            t.preDelay = juce::jmax (0, slowestTrack - latencies[i]);
    }

    graph->directDelay = slowestBus;

    int masterLatency = 0;
    for (const auto& e : graph->masterInserts)
        masterLatency += e->getLatencySamples();
    for (const auto& e : graph->busCompPlugins)
        masterLatency += e->getLatencySamples();

    graph->playbackLatency = slowestTrack + slowestBus + masterLatency;

    if (effects != nullptr)
        effects->removeAllExcept (insertIds);

    if (rack != nullptr)
    {
        juce::StringArray ids;
        for (const auto& track : project)
            ids.add (track[riff::ids::id].toString());
        rack->removeAllExcept (ids);
    }

    return graph;
}

void ProjectPlayer::rebuildNow()
{
    cancelPendingUpdate();

    const auto rate = engine.getSampleRate();
    engine.setGraph (buildGraph (rate));

    builtForRate = rate;
}

} // namespace riff
