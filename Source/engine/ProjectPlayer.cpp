#include "ProjectPlayer.h"
#include "../model/Project.h"

namespace riff
{

ProjectPlayer::ProjectPlayer (AudioEngine& e, AudioFileCache& c, InstrumentRack* r)
    : engine (e), cache (c), rack (r)
{
    engine.getDeviceManager().addChangeListener (this);
}

ProjectPlayer::~ProjectPlayer()
{
    engine.getDeviceManager().removeChangeListener (this);
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

void ProjectPlayer::changeListenerCallback (juce::ChangeBroadcaster*)
{
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
    graph->secondsPerBeat = model::secondsPerBeat (project);
    graph->beatsPerBar = juce::jlimit (1, 16, (int) project.getProperty (ids::timeSigNumerator, 4));

    auto toSamples = [sampleRate] (double seconds) { return (juce::int64) std::llround (seconds * sampleRate); };

    graph->cycleEnabled = project.getProperty (ids::cycleEnabled, false);
    graph->cycleStart = toSamples (project.getProperty (ids::cycleStart, 0.0));
    graph->cycleEnd = toSamples (project.getProperty (ids::cycleEnd, 0.0));

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
        t.pan     = (float) juce::jlimit (-1.0, 1.0, (double) track.getProperty (ids::pan, 0.0));
        t.audible = ! (bool) track[ids::mute] && (! anySolo || (bool) track[ids::solo]);

        t.monitorInput = (bool) track[ids::armed] && (bool) track[ids::monitor];
        t.inputChannel = juce::jmax (0, (int) track.getProperty (ids::input, 0));
        t.inputStereo  = track.getProperty (ids::inputStereo, false);

        if (t.slot < 0)
            continue;

        t.isInstrument = model::isInstrumentTrack (track);
        t.receivesLiveMidi = t.isInstrument && track[ids::id].toString() == liveMidiTrackId;

        if (t.isInstrument)
        {
            if (rack != nullptr)
                t.instrument = rack->instrumentFor (track, sampleRate, AudioEngine::maxChunkSize);

            for (const auto& region : track)
            {
                if (! model::isMidiRegion (region))
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
            continue;
        }

        for (const auto& region : track)
        {
            PlaybackGraph::Region r;
            r.audio        = cache.get (juce::File (region[ids::file].toString()), sampleRate);
            r.start        = toSamples (region[ids::start]);
            r.sourceOffset = toSamples (region[ids::offset]);
            r.length       = toSamples (region[ids::length]);
            r.gain         = (float) (double) region.getProperty (ids::gain, 1.0);

            if (r.audio != nullptr && r.length > 0)
                t.regions.push_back (std::move (r));
        }

        graph->tracks.push_back (std::move (t));
    }

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

    if (std::abs (rate - builtForRate) > 0.5)
        cache.purge (rate);

    builtForRate = rate;
}

} // namespace riff
