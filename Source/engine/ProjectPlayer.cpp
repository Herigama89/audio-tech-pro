#include "ProjectPlayer.h"
#include "../model/Project.h"

namespace riff
{

ProjectPlayer::ProjectPlayer (AudioEngine& e, AudioFileCache& c, InstrumentRack* r, EffectRack* fx)
    : engine (e), cache (c), rack (r), effects (fx)
{
    engine.getDeviceManager().addChangeListener (this);
    cache.addChangeListener (this);   // audio finished loading in the background
    variants.addChangeListener (this);  // a transposed or stretched region is ready
}

ProjectPlayer::~ProjectPlayer()
{
    engine.getDeviceManager().removeChangeListener (this);
    cache.removeChangeListener (this);
    variants.removeChangeListener (this);
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
    if (source == &cache || source == &variants)
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
        if (suppressedLanes.count (owner + "|" + lane[ids::param].toString()) > 0)
            continue;   // being written right now

        const auto parts = juce::StringArray::fromTokens (lane[ids::param].toString(), ":", {});
        if (parts.size() != 3 || parts[0] != "insert")
            continue;

        auto it = effectsById.find (parts[1]);
        if (it == effectsById.end())
            continue;

        // A hosted plugin's parameter: "#<index>", normalised 0..1.
        if (parts[2].startsWith ("#"))
        {
            const auto index = parts[2].substring (1).getIntValue();
            t.paramAutomation.push_back ({ it->second.get(), index, curveFor (owner, lane[ids::param].toString(), sampleRate) });
            continue;
        }

        auto* builtIn = dynamic_cast<BuiltInEffect*> (it->second.get());
        if (builtIn == nullptr)
            continue;

        const auto& params = builtIn->getInfo().params;
        for (size_t p = 0; p < params.size(); ++p)
            if (params[p].id == parts[2])
                t.paramAutomation.push_back ({ builtIn, (int) p, curveFor (owner, lane[ids::param].toString(), sampleRate) });
    }
}

juce::Array<juce::ValueTree> ProjectPlayer::regionsToPlay (const juce::ValueTree& track) const
{
    juce::Array<juce::ValueTree> regions;

    // A Live Loops cell playing on this track takes over from its timeline.
    if (auto it = liveCells.find (track[ids::id].toString()); it != liveCells.end())
    {
        const auto cell = project.getChildWithName (ids::LIVELOOPS).getChildWithProperty (ids::id, it->second.cellId);
        const auto source = cell.getChild (0);
        if (source.hasType (ids::REGION))
        {
            auto copy = source.createCopy();   // (no parent: nothing cuts its loop short)
            copy.setProperty (ids::start, it->second.from, nullptr);
            copy.setProperty (ids::loop, true, nullptr);
            copy.setProperty (ids::loopEnd, it->second.until > it->second.from ? it->second.until : it->second.from + 3600.0, nullptr);
            regions.add (copy);
            return regions;
        }
    }

    for (const auto& region : track)
        regions.add (region);
    return regions;
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
    graph->masterTape = TapeSaturator::fromProject (project);
    graph->secondsPerBeat = model::secondsPerBeat (project);
    graph->tempo = model::tempoMapOf (project);
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

    // A track counts as soloed when it is, or its VCA (folder stack) or the bus it plays into is.
    const auto soloed = [this] (const juce::ValueTree& track)
    {
        if ((bool) track[ids::solo] || model::vcaSolos (project, track))
            return true;
        const auto out = model::findById (project, track[ids::output].toString());
        return out.isValid() && (bool) out[ids::solo];
    };

    bool anySolo = false;
    for (const auto& track : project)
        anySolo = anySolo || (! ignoreSolo && track.hasType (ids::TRACK) && soloed (track));

    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK) || model::isVcaTrack (track))
            continue;   // a VCA has no sound of its own

        PlaybackGraph::Track t;
        t.slot    = slotFor (track[ids::id].toString());
        t.volume  = (float) (double) track.getProperty (ids::volume, 1.0);
        t.console = ConsoleChannel::fromTrack (track);
        t.tape    = TapeSaturator::fromTrack (track);
        t.pan     = (float) juce::jlimit (-1.0, 1.0, (double) track.getProperty (ids::pan, 0.0));
        t.audible = ! (bool) track[ids::mute] && ! model::vcaMutes (project, track) && (! anySolo || soloed (track));
        t.vcaGain = (float) model::vcaGainOf (project, track);
        t.width = (float) juce::jlimit (0.0, 2.0, (double) track.getProperty (ids::panWidth, 1.0));
        if (! model::isStereoChannel (track))
            t.width = 0.0f;   // a mono channel: both sides summed, then panned
        // Into a bus: any track, and buses too (a summing stack into a bus), unless that would loop back.
        if (const auto out = busIndex.find (track[ids::output].toString()); out != busIndex.end()
            && ! model::outputWouldLoop (project, track, track[ids::output].toString()))
            t.outputBus = out->second;
        else if (const auto hw = model::hardwareOutput (track[ids::output].toString()); hw.first >= 0)
        {
            t.hardwareOut = hw.first;
            t.hardwareMono = hw.second == 1;
            graph->anyHardwareOut = true;
        }

        t.monitorInput = (bool) track[ids::armed] && (bool) track[ids::monitor];
        t.inputChannel = juce::jmax (0, (int) track.getProperty (ids::input, 0));
        t.inputStereo  = track.getProperty (ids::inputStereo, false);
        if (const auto source = model::findById (project, track[ids::inputTrack].toString()); source.isValid() && source != track)
            t.inputTrackSlot = slotFor (source[ids::id].toString());

        if (t.slot < 0)
            continue;

        const auto trackId = track[ids::id].toString();
        t.inserts = buildChain (trackId, sampleRate, insertIds, effectsById);
        t.armed = (bool) track[ids::armed];
        // Printed inserts (LUNA style): on the input while armed, not on playback (the recording has them).
        if (! model::isInstrumentTrack (track) && ! model::isBusTrack (track))
            for (const auto& insert : model::insertsOf (model::findChain (project, trackId)))
                if ((bool) insert[ids::print])
                    if (auto it = effectsById.find (insert[ids::id].toString()); it != effectsById.end() && it->second != nullptr)
                    {
                        t.printInserts.push_back (it->second);
                        t.inserts.erase (std::remove (t.inserts.begin(), t.inserts.end(), it->second), t.inserts.end());
                    }
        for (const auto& insert : model::insertsOf (model::findChain (project, trackId)))
            if (const auto key = model::findById (project, insert[ids::sidechain].toString()); key.isValid() && key != track)
                if (auto it = effectsById.find (insert[ids::id].toString()); it != effectsById.end() && it->second != nullptr)
                    t.sidechains.push_back ({ it->second.get(), slotFor (key[ids::id].toString()) });
        t.stripPlugins = buildChain (model::stripOwner (trackId), sampleRate, insertIds, effectsById);
        if (model::stripPlugin (project, trackId).isValid())
            t.console.enabled = false;   // the plugin replaces the built-in strip (dry while it loads)
        if (! (bool) track.getProperty (ids::stripIn, true))
        {
            t.stripPlugins.clear();      // the strip's IN key: out of the signal, still loaded
            t.console.enabled = false;
        }
        t.isInstrument = model::isInstrumentTrack (track);
        t.isBus = model::isBusTrack (track);
        if (trackId == sourceOnlyTrack)
        {
            t.inserts.clear();
            t.stripPlugins.clear();
            t.console.enabled = false;
        }

        // A frozen track plays its frozen file: no instrument, plugins or strip (fader, pan and sends stay).
        if ((bool) track[ids::frozen] && ! t.isBus)
        {
            const juce::File frozenFile (track[ids::frozenFile].toString());
            if (auto audio = cache.get (frozenFile))
            {
                // Pre Fader: the plugins are in the frozen file. Source Only: just the source is, the plugins still run.
                if (track.getProperty (ids::freezeMode, "preFader").toString() != "sourceOnly")
                {
                    t.inserts.clear();
                    t.stripPlugins.clear();
                    t.console.enabled = false;
                }
                t.isInstrument = false;

                PlaybackGraph::Region r;
                r.audio = audio;
                r.start = 0;
                r.resample = std::abs (audio->sampleRate - sampleRate) > 0.5;
                r.step = r.resample ? audio->sampleRate / sampleRate : 1.0;
                r.length = (juce::int64) ((double) audio->numFrames() / r.step);
                t.regions.push_back (std::move (r));

                if (track.getProperty (ids::automationMode, "read").toString() != "off")
                {
                    t.volumeAutomation = curveFor (trackId, model::volumeParam, sampleRate);
                    t.panAutomation = curveFor (trackId, model::panParam, sampleRate);
                }
                for (const auto& send : model::sendsOf (model::findChain (project, trackId)))
                    if (auto it = busIndex.find (send[ids::target].toString()); it != busIndex.end())
                        t.sends.push_back ({ it->second, (float) (double) send.getProperty (ids::level, 1.0), (bool) send[ids::pre] });
                graph->tracks.push_back (std::move (t));
                latencies.push_back (0);
                continue;
            }
        }

        // Automation (not in Off mode)
        if (track.getProperty (ids::automationMode, "read").toString() != "off")
        {
            t.volumeAutomation = curveFor (trackId, model::volumeParam, sampleRate);
            t.panAutomation = curveFor (trackId, model::panParam, sampleRate);
            addParamAutomation (t, trackId, sampleRate, effectsById);
        }

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

            for (const auto& region : regionsToPlay (track))
            {
                if (! model::isMidiRegion (region) || model::isRegionMuted (region))
                    continue;

                // The Inspector's Region and Track parameters (transpose, velocity, quantize, swing, dynamics,
                // gate time, delay, key and velocity limits), applied as it plays.
                const auto shaping = model::MidiPlayback::of (region, track);
                const auto tempoMap = model::tempoMapOf (project);

                // A looped region plays again and again, up to its loop end.
                const double regionStart = region[ids::start];
                const double regionLength = region[ids::length];
                const double offset = region[ids::offset];
                const auto playEnd = model::loopEndOf (region);

                for (double repeatStart = regionStart; repeatStart < playEnd - 1.0e-6 && regionLength > 0.0; repeatStart += regionLength)
                {
                    const auto windowLength = juce::jmin (regionLength, playEnd - repeatStart);
                    const double windowEnd = offset + windowLength;

                    PlaybackGraph::Region r;
                    r.start  = toSamples (repeatStart);
                    r.length = toSamples (windowLength);

                    for (const auto& note : region)
                    {
                        if (! note.hasType (ids::NOTE) || (bool) note.getProperty (ids::mute, false))
                            continue;   // (muted notes, from the piano roll's Mute tool, don't play)

                        // Notes play only if they start inside the region's window.
                        const double noteStart = note[ids::start];
                        if (noteStart < offset || noteStart >= windowEnd)
                            continue;

                        const double noteEnd = juce::jmin (noteStart + (double) note[ids::length], windowEnd);

                        double start = repeatStart + noteStart - offset, length = noteEnd - noteStart;
                        int pitch = note[ids::pitch], velocity = note[ids::velocity];
                        if (! shaping.shape (tempoMap, start, length, pitch, velocity))
                            continue;

                        PlaybackGraph::Note n;
                        n.start    = toSamples (start);
                        n.end      = juce::jmax (n.start + 1, toSamples (start + length));
                        n.pitch    = (juce::uint8) pitch;
                        n.velocity = (juce::uint8) velocity;
                        r.notes.push_back (n);
                    }

                    std::sort (r.notes.begin(), r.notes.end(),
                               [] (const auto& a, const auto& b) { return a.start < b.start; });

                    // Controllers and pitch bend inside the window.
                    for (const auto& event : region)
                    {
                        if (! event.hasType (ids::CC))
                            continue;
                        const double time = event[ids::start];
                        if (time < offset || time >= windowEnd)
                            continue;

                        PlaybackGraph::MidiEvent m;
                        m.time = toSamples (repeatStart + time - offset);
                        const auto controller = (int) event[ids::controller];
                        const auto value = juce::jlimit (0.0, 1.0, (double) event[ids::value]);
                        if (controller == model::pitchBend)
                        {
                            const auto bend = juce::jlimit (0, 16383, juce::roundToInt (value * 16383.0));
                            m.status = 0xe0;
                            m.data1 = (juce::uint8) (bend & 0x7f);
                            m.data2 = (juce::uint8) (bend >> 7);
                        }
                        else
                        {
                            m.status = 0xb0;
                            m.data1 = (juce::uint8) juce::jlimit (0, 127, controller);
                            m.data2 = (juce::uint8) juce::roundToInt (value * 127.0);
                        }
                        r.events.push_back (m);
                    }
                    std::stable_sort (r.events.begin(), r.events.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });

                    t.regions.push_back (std::move (r));
                }
            }

            graph->tracks.push_back (std::move (t));
            latencies.push_back (latency);
            continue;
        }

        // An ARA plugin (Melodyne...) in an insert plays this track's regions itself.
        bool araPlays = false;
        if (effects != nullptr)
            for (const auto& insert : model::insertsOf (model::findChain (project, trackId)))
                araPlays = araPlays || effects->isAraInsert (insert[ids::id].toString());

        const bool araByRegion = araPlays && model::hasAraRegions (track);   // Extensions: only the marked regions are the plugin's
        for (const auto& region : (araPlays && ! araByRegion) ? juce::Array<juce::ValueTree>() : regionsToPlay (track))
        {
            if (model::isRegionMuted (region) || (araByRegion && (bool) region[ids::araEdit]))
                continue;

            // A comped take folder: each stretch plays from its own take (a few ms of fade where they meet).
            if (const auto pieces = model::compPieces (region); ! pieces.empty())
            {
                const double regionStart = region[ids::start];
                const auto boundaryFade = toSamples (0.004);
                for (size_t i = 0; i < pieces.size(); ++i)
                {
                    const auto& piece = pieces[i];
                    const auto take = region.getChild (piece.take);
                    if (! take.hasType (ids::TAKE))
                        continue;

                    PlaybackGraph::Region r;
                    r.audio = cache.get (juce::File (take[ids::file].toString()));
                    if (r.audio == nullptr)
                        continue;
                    r.start = toSamples (regionStart + piece.from);
                    r.length = toSamples (piece.to - piece.from);
                    const auto fileRate = r.audio->sampleRate;
                    r.resample = std::abs (fileRate - sampleRate) > 0.5;
                    r.step = r.resample ? fileRate / sampleRate : 1.0;
                    r.sourcePosition = ((double) take[ids::offset] + piece.from) * fileRate;
                    r.sourceOffset = (juce::int64) std::llround (r.sourcePosition);
                    r.gain = (float) (double) region.getProperty (ids::gain, 1.0);

                    const auto first = i == 0, last = i + 1 == pieces.size();
                    r.fadeIn = juce::jmin (r.length / 2, first ? toSamples ((double) region.getProperty (ids::fadeIn, 0.0)) : boundaryFade);
                    r.fadeOut = juce::jmin (r.length - r.fadeIn, last ? toSamples ((double) region.getProperty (ids::fadeOut, 0.0)) : boundaryFade);
                    r.fadeInCurve = first ? (float) (double) region.getProperty (ids::fadeInCurve, 0.0) : 0.0f;
                    r.fadeOutCurve = last ? (float) (double) region.getProperty (ids::fadeOutCurve, 0.0) : 0.0f;
                    if (r.length > 0)
                        t.regions.push_back (std::move (r));
                }
                continue;
            }

            PlaybackGraph::Region r;
            const juce::File sourceFile (region[ids::file].toString());

            // Transposed, fine-tuned or stretched to the song's tempo (Region box): its rendered copy, once ready.
            juce::File playFile = sourceFile;
            double stretch = 1.0;
            {
                auto shape = model::audioShapeOf (project, track, region, 0.0);
                if (shape.needsTempo)
                    shape = model::audioShapeOf (project, track, region, variants.tempoOf (sourceFile));
                const RegionVariants::Spec spec { shape.semitones, shape.ratio, shape.varispeed };
                if (! spec.isIdentity())
                    if (const auto rendered = variants.get (sourceFile, spec); rendered != juce::File())
                    {
                        playFile = rendered;
                        stretch = shape.ratio;
                    }
            }

            r.audio        = cache.get (playFile);
            r.start        = toSamples (juce::jmax (0.0, (double) region[ids::start] + (double) region.getProperty (ids::playDelay, 0.0) / 1000.0));   // (Delay, in the Region box)
            r.length       = toSamples (region[ids::length]);

            if (r.audio != nullptr)
            {
                // A file at another sample rate is converted as it plays.
                const auto fileRate = r.audio->sampleRate;
                r.resample = std::abs (fileRate - sampleRate) > 0.5;
                r.step = r.resample ? fileRate / sampleRate : 1.0;
                r.sourcePosition = (double) region[ids::offset] * stretch * fileRate;
                r.sourceOffset = (juce::int64) std::llround (r.sourcePosition);
            }
            r.gain         = (float) (double) region.getProperty (ids::gain, 1.0);

            // Fades, kept inside the region.
            const auto fadeIn  = juce::jmax (0.0, (double) region.getProperty (ids::fadeIn, 0.0));
            const auto fadeOut = juce::jmax (0.0, (double) region.getProperty (ids::fadeOut, 0.0));
            r.fadeIn  = juce::jmin (r.length, toSamples (fadeIn));
            r.fadeOut = juce::jmin (r.length - r.fadeIn, toSamples (fadeOut));
            r.fadeInCurve  = (float) (double) region.getProperty (ids::fadeInCurve, 0.0);
            r.fadeOutCurve = (float) (double) region.getProperty (ids::fadeOutCurve, 0.0);

            if (r.audio == nullptr || r.length <= 0)
                continue;

            if (! (bool) region.getProperty (ids::loop, false))
            {
                t.regions.push_back (std::move (r));
                continue;
            }

            // A looped region repeats up to its loop end (the last time cut short).
            const auto playEnd = juce::jmax (r.start + r.length, toSamples (model::loopEndOf (region)));
            const auto regionLength = r.length;
            for (auto repeatStart = r.start; repeatStart < playEnd; repeatStart += regionLength)
            {
                auto copy = r;
                copy.start = repeatStart;
                copy.length = juce::jmin (regionLength, playEnd - repeatStart);
                if (copy.length < regionLength)
                    copy.fadeOut = 0;
                if (copy.length > 0)
                    t.regions.push_back (std::move (copy));
            }
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

    // Buses that feed other buses play first (the deepest first).
    {
        const auto depthOf = [this] (const juce::String& id)
        {
            int depth = 0;
            auto t = model::findById (project, id);
            while (t.isValid() && t[ids::output].toString().isNotEmpty() && depth < 64)
            {
                t = model::findById (project, t[ids::output].toString());
                ++depth;
            }
            return depth;
        };
        std::vector<std::pair<int, int>> buses;   // depth, index
        for (int i = 0; i < (int) graph->tracks.size(); ++i)
            if (graph->tracks[(size_t) i].isBus)
            {
                juce::String id;
                for (const auto& [busId, index] : busIndex)
                    if (index == graph->tracks[(size_t) i].busIndex)
                        id = busId;
                buses.emplace_back (depthOf (id), i);
            }
        std::stable_sort (buses.begin(), buses.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        for (const auto& b : buses)
            graph->busOrder.push_back (b.second);
    }

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

bool ProjectPlayer::applyMixMove (const juce::ValueTree& tree, const juce::Identifier& p)
{
    // The project's master fader
    if (p == ids::masterGain && tree == project)
    {
        engine.setMasterMixValue ((float) (double) tree.getProperty (ids::masterGain, 0.8));
        return true;
    }
    // A track's fader or pan (not a VCA's: that moves other tracks, so the graph is rebuilt)
    if ((p != ids::volume && p != ids::pan) || ! tree.hasType (ids::TRACK) || model::isVcaTrack (tree) || tree.getParent() != project)
        return false;
    const auto it = slots.find (tree[ids::id].toString());
    if (it == slots.end())
        return false;
    if (p == ids::pan)
        engine.setMixValue (it->second, true, (float) juce::jlimit (-1.0, 1.0, (double) tree.getProperty (ids::pan, 0.0)));
    else
        engine.setMixValue (it->second, false, (float) (double) tree.getProperty (ids::volume, 1.0));
    return true;
}

void ProjectPlayer::rebuildNow()
{
    cancelPendingUpdate();

    const auto rate = engine.getSampleRate();
    auto graph = buildGraph (rate);

    // Long files stream from disk: read ahead of the playhead for each of their regions.
    std::vector<DiskReadAhead::Item> streamed;
    if (graph != nullptr && rate > 0.0)
        for (const auto& t : graph->tracks)
            for (const auto& r : t.regions)
                if (r.audio != nullptr && r.audio->isStreamed())
                    streamed.push_back ({ r.audio, (double) r.start / rate, (double) r.length / rate,
                                          r.resample ? r.sourcePosition : (double) r.sourceOffset });
    readAhead.setItems (std::move (streamed));

    engine.setGraph (std::move (graph));
    if (effects != nullptr)
        effects->syncAra (project);   // ARA plugins' regions follow the tracks'

    builtForRate = rate;
}

} // namespace riff
