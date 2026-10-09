#include "Project.h"
#include <map>
#include <algorithm>
#include "../engine/BuiltInEffects.h"
#include "../AppInfo.h"
#include <array>

namespace riff::model
{

namespace
{
    // Track colours, cycled as tracks are added.
    const juce::Colour palette[] = {
        // Console colours: gold, red, orange, green, teal, blue, purple, rose
        juce::Colour (0xffd9a72b), juce::Colour (0xffcf3b2f), juce::Colour (0xffe0702a),
        juce::Colour (0xff3f9a3c), juce::Colour (0xff2f9b8f), juce::Colour (0xff3866c4),
        juce::Colour (0xff8a4fc4), juce::Colour (0xffc9467f),
    };

    juce::String newId() { return juce::Uuid().toDashedString(); }

    // ----- JSON conversion -------------------------------------------------
    juce::var treeToVar (const juce::ValueTree& tree, const juce::File& projectDir)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("type", tree.getType().toString());

        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto name = tree.getPropertyName (i);
            auto value = tree.getProperty (name);

            if ((tree.hasType (ids::REGION) || tree.hasType (ids::TAKE)) && name == ids::file)
            {
                const juce::File f (value.toString());
                value = projectDir.exists() ? f.getRelativePathFrom (projectDir) : f.getFullPathName();
            }

            obj->setProperty (name, value);
        }

        if (tree.getNumChildren() > 0)
        {
            juce::Array<juce::var> children;
            for (const auto& child : tree)
                children.add (treeToVar (child, projectDir));
            obj->setProperty ("children", children);
        }

        return juce::var (obj);
    }

    juce::ValueTree varToTree (const juce::var& v, const juce::File& projectDir, juce::String& error)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr)
        {
            error = "Expected an object in the project file.";
            return {};
        }

        const auto type = obj->getProperty ("type").toString();
        if (type.isEmpty() || ! juce::Identifier::isValidIdentifier (type))
        {
            error = "A node in the project file has no valid type.";
            return {};
        }

        juce::ValueTree tree { juce::Identifier (type) };

        for (const auto& prop : obj->getProperties())
        {
            if (prop.name == juce::Identifier ("type") || prop.name == juce::Identifier ("children"))
                continue;

            auto value = prop.value;

            if ((tree.hasType (ids::REGION) || tree.hasType (ids::TAKE)) && prop.name == ids::file)
            {
                const auto path = value.toString();
                value = juce::File::isAbsolutePath (path) ? path
                                                          : projectDir.getChildFile (path).getFullPathName();
            }

            tree.setProperty (prop.name, value, nullptr);
        }

        if ((tree.hasType (ids::TRACK) || tree.hasType (ids::REGION) || tree.hasType (ids::TAKE) || tree.hasType (ids::INSERT))
            && ! tree.hasProperty (ids::id))
            tree.setProperty (ids::id, newId(), nullptr);

        if (auto* children = obj->getProperty ("children").getArray())
        {
            for (const auto& c : *children)
            {
                auto child = varToTree (c, projectDir, error);
                if (! child.isValid())
                    return {};
                tree.appendChild (child, nullptr);
            }
        }

        return tree;
    }

    /** Moves where a region (and every take in it) starts reading its file. */
    void shiftOffsets (juce::ValueTree region, double delta, juce::UndoManager* um)
    {
        region.setProperty (ids::offset, (double) region[ids::offset] + delta, um);

        for (auto take : region)
            if (take.hasType (ids::TAKE))
                take.setProperty (ids::offset, (double) take[ids::offset] + delta, um);

        // Comp segments are measured from the region's start, which moved by `delta`.
        juce::Array<juce::ValueTree> gone;
        for (auto seg : region)
            if (seg.hasType (ids::COMPSEG))
            {
                const auto from = (double) seg[ids::from] - delta, to = (double) seg[ids::to] - delta;
                if (to <= 0.0)
                    gone.add (seg);
                else
                {
                    seg.setProperty (ids::from, juce::jmax (0.0, from), um);
                    seg.setProperty (ids::to, to, um);
                }
            }
        for (auto& seg : gone)
            region.removeChild (seg, um);
    }
}

//==============================================================================
juce::ValueTree createProject (const juce::String& name)
{
    juce::ValueTree project { ids::PROJECT };
    project.setProperty (ids::name, name, nullptr);
    project.setProperty (ids::tempo, 120.0, nullptr);
    project.setProperty (ids::timeSigNumerator, 4, nullptr);
    project.setProperty (ids::masterGain, 0.8, nullptr);
    project.setProperty (ids::metronome, false, nullptr);
    project.setProperty (ids::countIn, false, nullptr);
    project.setProperty (ids::cycleEnabled, false, nullptr);
    project.setProperty (ids::cycleStart, 0.0, nullptr);
    project.setProperty (ids::cycleEnd, 8.0, nullptr); // 4 bars at 120 BPM
    mixerOf (project);
    return project;
}

juce::ValueTree addTrack (juce::ValueTree project, const juce::String& name, juce::UndoManager* um,
                          int index, const juce::String& kind)
{
    jassert (project.hasType (ids::PROJECT));

    const auto colour = palette[(size_t) numTracks (project) % std::size (palette)];

    juce::ValueTree track { ids::TRACK };
    track.setProperty (ids::id, newId(), nullptr);
    track.setProperty (ids::name, name, nullptr);
    track.setProperty (ids::colour, colour.toString(), nullptr);
    track.setProperty (ids::volume, 1.0, nullptr);
    track.setProperty (ids::pan, 0.0, nullptr);
    track.setProperty (ids::mute, false, nullptr);
    track.setProperty (ids::solo, false, nullptr);
    track.setProperty (ids::armed, false, nullptr);
    track.setProperty (ids::monitor, false, nullptr);
    track.setProperty (ids::input, 0, nullptr);
    track.setProperty (ids::inputStereo, false, nullptr);
    track.setProperty (ids::kind, kind, nullptr);

    if (kind == instrumentKind)
    {
        track.setProperty (ids::instrument, "synth", nullptr);
        track.setProperty (ids::synthWave, 0, nullptr);
        track.setProperty (ids::synthCutoff, 3000.0, nullptr);
        track.setProperty (ids::synthResonance, 0.2, nullptr);
        track.setProperty (ids::synthAttack, 0.005, nullptr);
        track.setProperty (ids::synthDecay, 0.3, nullptr);
        track.setProperty (ids::synthSustain, 0.6, nullptr);
        track.setProperty (ids::synthRelease, 0.3, nullptr);
        track.setProperty (ids::synthGain, 0.5, nullptr);
    }

    // Tracks always come before the MIXER node.
    const auto count = numTracks (project);
    project.addChild (track, (index < 0 || index > count) ? count : index, um);

    // With a console plugin chosen for the mixer, new tracks get it too.
    if (const auto console = consolePlugin (project); console.isNotEmpty() && kind != vcaKind)
        setStripPlugin (project, track[ids::id].toString(), console, um);

    return track;
}

juce::ValueTree addRegion (juce::ValueTree track, const juce::File& audioFile,
                           double start, double length, juce::UndoManager* um)
{
    jassert (track.hasType (ids::TRACK));

    juce::ValueTree region { ids::REGION };
    region.setProperty (ids::id, newId(), nullptr);
    region.setProperty (ids::name, audioFile.getFileNameWithoutExtension(), nullptr);
    region.setProperty (ids::file, audioFile.getFullPathName(), nullptr);
    region.setProperty (ids::start, juce::jmax (0.0, start), nullptr);
    region.setProperty (ids::offset, 0.0, nullptr);
    region.setProperty (ids::length, juce::jmax (minRegionLength, length), nullptr);
    region.setProperty (ids::gain, 1.0, nullptr);

    track.appendChild (region, um);
    return region;
}

//==============================================================================
void moveRegion (juce::ValueTree region, juce::ValueTree targetTrack, double newStart, juce::UndoManager* um)
{
    jassert (region.hasType (ids::REGION));

    auto parent = region.getParent();

    if (targetTrack.isValid() && targetTrack.hasType (ids::TRACK) && targetTrack != parent)
    {
        parent.removeChild (region, um);
        targetTrack.appendChild (region, um);
    }

    region.setProperty (ids::start, juce::jmax (0.0, newStart), um);
}

namespace
{
    /** Fades never reach past the region (after it's been shortened). */
    void clampFades (juce::ValueTree region, juce::UndoManager* um)
    {
        const double length = region[ids::length];
        const double in = region.getProperty (ids::fadeIn, 0.0), out = region.getProperty (ids::fadeOut, 0.0);
        if (in + out <= length)
            return;
        const auto scale = length / (in + out);
        if (in > 0.0)  region.setProperty (ids::fadeIn, in * scale, um);
        if (out > 0.0) region.setProperty (ids::fadeOut, out * scale, um);
    }
}

void trimRegionStart (juce::ValueTree region, double newStart, double sourceLength, juce::UndoManager* um)
{
    juce::ignoreUnused (sourceLength);

    const double start  = region[ids::start];
    const double end    = start + (double) region[ids::length];

    // Smallest offset among the region and its takes: none may go below 0.
    double offset = region[ids::offset];
    for (const auto& take : region)
        if (take.hasType (ids::TAKE))
            offset = juce::jmin (offset, (double) take[ids::offset]);

    // Can't reveal audio from before the start of the file, or go before 0.
    newStart = juce::jlimit (juce::jmax (0.0, start - offset), end - minRegionLength, newStart);

    shiftOffsets (region, newStart - start, um);
    region.setProperty (ids::start, newStart, um);
    region.setProperty (ids::length, end - newStart, um);
    clampFades (region, um);
}

void trimRegionEnd (juce::ValueTree region, double newEnd, double sourceLength, juce::UndoManager* um)
{
    const double start  = region[ids::start];
    const double offset = region[ids::offset];

    auto maxEnd = sourceLength > 0.0 ? start + (sourceLength - offset) : std::numeric_limits<double>::max();
    newEnd = juce::jlimit (start + minRegionLength, juce::jmax (start + minRegionLength, maxEnd), newEnd);

    region.setProperty (ids::length, newEnd - start, um);
    clampFades (region, um);
}

juce::ValueTree splitRegion (juce::ValueTree region, double time, juce::UndoManager* um)
{
    const double start  = region[ids::start];
    const double end    = regionEnd (region);

    if (time <= start + minRegionLength || time >= end - minRegionLength)
        return {};

    auto parent = region.getParent();
    auto right = region.createCopy();
    right.setProperty (ids::id, newId(), nullptr);
    right.setProperty (ids::start, time, nullptr);
    shiftOffsets (right, time - start, nullptr);
    right.setProperty (ids::length, end - time, nullptr);

    for (auto take : right)
        take.setProperty (ids::id, newId(), nullptr);

    // The fade in stays on the left part, the fade out and the loop go with the right.
    right.removeProperty (ids::fadeIn, nullptr);
    region.removeProperty (ids::fadeOut, um);
    region.removeProperty (ids::loop, um);
    region.removeProperty (ids::loopEnd, um);

    region.setProperty (ids::length, time - start, um);
    clampFades (region, um);
    clampFades (right, nullptr);
    parent.addChild (right, parent.indexOf (region) + 1, um);
    return right;
}

juce::ValueTree duplicateRegion (const juce::ValueTree& region, juce::ValueTree track, double start, juce::UndoManager* um)
{
    auto copy = region.createCopy();
    copy.setProperty (ids::id, newId(), nullptr);
    for (auto child : copy)
        if (child.hasProperty (ids::id))
            child.setProperty (ids::id, newId(), nullptr);
    copy.setProperty (ids::start, juce::jmax (0.0, start), nullptr);
    track.appendChild (copy, um);
    return copy;
}

juce::ValueTree joinRegions (const juce::Array<juce::ValueTree>& regions, juce::UndoManager* um)
{
    if (regions.size() < 2)
        return {};

    auto track = regions.getFirst().getParent();
    double from = std::numeric_limits<double>::max(), to = 0.0;
    for (const auto& r : regions)
    {
        if (! isMidiRegion (r) || r.getParent() != track)
            return {};
        from = juce::jmin (from, (double) r[ids::start]);
        to = juce::jmax (to, regionEnd (r));
    }

    auto joined = addMidiRegion (track, from, to - from, um);
    joined.setProperty (ids::name, regions.getFirst()[ids::name], um);

    // Every note that sounds inside its region's window, at its place on the timeline.
    for (const auto& r : regions)
    {
        const double start = r[ids::start], offset = r[ids::offset], length = r[ids::length];
        for (const auto& note : r)
        {
            if (! note.hasType (ids::NOTE))
                continue;
            const double noteStart = note[ids::start];
            if (noteStart < offset - 1.0e-9 || noteStart >= offset + length)
                continue;
            const auto at = start + (noteStart - offset);
            const auto noteLength = juce::jmin ((double) note[ids::length], start + length - at);
            addNote (joined, note[ids::pitch], at - from, noteLength, note[ids::velocity], nullptr);
        }

        // Controllers and pitch bend too.
        for (const auto& event : r)
        {
            if (! event.hasType (ids::CC))
                continue;
            const double t = event[ids::start];
            if (t < offset - 1.0e-9 || t >= offset + length)
                continue;
            addControllerEvent (joined, event[ids::controller], start + (t - offset) - from, event[ids::value], nullptr);
        }
    }

    for (auto r : regions)
        deleteRegion (r, um);
    return joined;
}

void clearOverlaps (const juce::ValueTree& region, std::function<double (const juce::ValueTree&)> sourceLength, juce::UndoManager* um)
{
    auto track = region.getParent();
    const double start = region[ids::start];
    const double end = regionEnd (region);

    juce::Array<juce::ValueTree> others;
    for (const auto& r : track)
        if (r.hasType (ids::REGION) && r != region && (double) r[ids::start] < end - 1.0e-9 && regionEnd (r) > start + 1.0e-9)
            others.add (r);

    for (auto other : others)
    {
        const double otherStart = other[ids::start];
        const auto otherEnd = regionEnd (other);
        const auto length = sourceLength ? sourceLength (other) : 0.0;

        if (otherStart >= start - 1.0e-9 && otherEnd <= end + 1.0e-9)
            deleteRegion (other, um);                                   // covered: gone
        else if (otherStart < start && otherEnd > end)
        {
            auto right = splitRegion (other, end, um);                  // the new one lands in the middle
            trimRegionEnd (other, start, length, um);
            juce::ignoreUnused (right);
        }
        else if (otherStart < start)
            trimRegionEnd (other, start, length, um);                   // keep its beginning
        else
            trimRegionStart (other, end, length, um);                   // keep its end
    }
}

juce::ValueTree duplicateTrackSettings (juce::ValueTree project, const juce::ValueTree& track, juce::UndoManager* um)
{
    const auto kind = track[ids::kind].toString().isNotEmpty() ? track[ids::kind].toString() : audioKind;
    auto copy = addTrack (project, track[ids::name].toString(), um, project.indexOf (track) + 1, kind);
    const auto newTrackId = copy[ids::id].toString();

    // Everything but its identity, its regions and its record state.
    for (int i = 0; i < track.getNumProperties(); ++i)
    {
        const auto name = track.getPropertyName (i);
        if (name != ids::id && name != ids::armed && name != ids::monitor)
            copy.setProperty (name, track[name], um);
    }

    // Inserts and sends (and a channel strip plugin), with new ids.
    const auto copyChain = [&] (const juce::String& fromOwner, const juce::String& toOwner)
    {
        const auto source = findChain (project, fromOwner);
        if (! source.isValid())
            return;
        auto target = chainFor (project, toOwner);
        for (const auto& item : source)
        {
            auto c = item.createCopy();
            if (c.hasProperty (ids::id))
                c.setProperty (ids::id, newId(), nullptr);
            target.appendChild (c, um);
        }
    };
    if (auto existing = findChain (project, stripOwner (newTrackId)); existing.isValid())
        existing.getParent().removeChild (existing, um);   // addTrack may have added the console plugin; take the original's instead
    copyChain (track[ids::id].toString(), newTrackId);
    copyChain (stripOwner (track[ids::id].toString()), stripOwner (newTrackId));
    return copy;
}

juce::Array<juce::ValueTree> importTracks (juce::ValueTree project, const juce::ValueTree& source,
                                           const juce::StringArray& trackIds, juce::UndoManager* um)
{
    juce::Array<juce::ValueTree> added;
    std::map<juce::String, juce::String> newIds;   // old track/insert id -> new

    const auto renumber = [] (juce::ValueTree tree, auto& self) -> void
    {
        if (tree.hasProperty (ids::id))
            tree.setProperty (ids::id, newId(), nullptr);
        for (auto child : tree)
            self (child, self);
    };

    // The tracks first, so sends know which buses came along.
    for (const auto& track : source)
    {
        if (! track.hasType (ids::TRACK) || ! trackIds.contains (track[ids::id].toString()))
            continue;
        auto copy = track.createCopy();
        renumber (copy, renumber);
        copy.setProperty (ids::armed, false, nullptr);
        copy.setProperty (ids::solo, false, nullptr);
        newIds[track[ids::id].toString()] = copy[ids::id].toString();
        project.addChild (copy, numTracks (project), um);
        added.add (copy);
    }

    const auto mapOwner = [&newIds] (const juce::String& owner) -> juce::String
    {
        const bool strip = owner.startsWith ("strip:");
        const auto trackId = strip ? owner.fromFirstOccurrenceOf ("strip:", false, false) : owner;
        const auto it = newIds.find (trackId);
        if (it == newIds.end())
            return {};
        return strip ? stripOwner (it->second) : it->second;
    };

    auto mixer = mixerOf (project);
    const auto sourceMixer = source.getChildWithName (ids::MIXER);

    for (const auto& chain : sourceMixer)
    {
        if (! chain.hasType (ids::CHAIN))
            continue;
        const auto owner = mapOwner (chain[ids::owner].toString());
        if (owner.isEmpty())
            continue;

        juce::ValueTree copy { ids::CHAIN };
        copy.setProperty (ids::owner, owner, nullptr);
        for (const auto& item : chain)
        {
            auto c = item.createCopy();
            if (c.hasType (ids::SEND))
            {
                const auto it = newIds.find (c[ids::target].toString());
                if (it == newIds.end())
                    continue;   // its bus stayed behind
                c.setProperty (ids::target, it->second, nullptr);
            }
            if (c.hasProperty (ids::id))
            {
                const auto fresh = newId();
                newIds[c[ids::id].toString()] = fresh;
                c.setProperty (ids::id, fresh, nullptr);
            }
            copy.appendChild (c, nullptr);
        }
        mixer.appendChild (copy, um);
    }

    for (const auto& lane : sourceMixer)
    {
        if (! lane.hasType (ids::LANE))
            continue;
        const auto owner = mapOwner (lane[ids::owner].toString());
        if (owner.isEmpty())
            continue;

        auto param = lane[ids::param].toString();
        if (param.startsWith ("insert:"))
        {
            const auto insertId = param.fromFirstOccurrenceOf ("insert:", false, false).upToFirstOccurrenceOf (":", false, false);
            const auto it = newIds.find (insertId);
            if (it == newIds.end())
                continue;
            param = insertParam (it->second, param.fromFirstOccurrenceOf ("insert:" + insertId + ":", false, false));
        }

        auto copy = lane.createCopy();
        copy.setProperty (ids::owner, owner, nullptr);
        copy.setProperty (ids::param, param, nullptr);
        mixer.appendChild (copy, um);
    }

    return added;
}

void resetChannelStrip (juce::ValueTree project, juce::ValueTree track, juce::UndoManager* um)
{
    const auto owner = track[ids::id].toString();
    if (auto chain = findChain (project, owner); chain.isValid())
        for (int i = chain.getNumChildren(); --i >= 0;)
            if (const auto c = chain.getChild (i); c.hasType (ids::INSERT) || c.hasType (ids::SEND))
                chain.removeChild (i, um);
    for (const auto* name : { "consoleIn", "consoleHF", "consoleHMF", "consoleLMF", "consoleLF", "consoleDynamics", "consoleDynIn",
                              "consoleInput", "consoleOutput", "consoleHPF", "consoleLPF", "consoleHFFreq", "consoleHFBell", "consoleHMFFreq",
                              "consoleHMFQ", "consoleLMFFreq", "consoleLMFQ", "consoleLFFreq", "consoleLFBell", "consoleCompRatio",
                              "consoleCompRelease", "consoleCompFast", "consoleGateThreshold", "consoleGateRange", "consoleGateRelease",
                              "consoleGateExpander", "stripIn", "panWidth" })
        track.removeProperty (juce::Identifier (name), um);
    track.setProperty (ids::volume, 1.0, um);
    track.setProperty (ids::pan, 0.0, um);
}

MidiPlayback MidiPlayback::of (const juce::ValueTree& region, const juce::ValueTree& track)
{
    MidiPlayback p;
    const bool noTranspose = track.getProperty (ids::noTranspose, false);
    p.transpose = noTranspose ? 0 : (int) region.getProperty (ids::transpose, 0) + (int) track.getProperty (ids::transpose, 0);
    p.velocity = (int) region.getProperty (ids::velocityOffset, 0) + (int) track.getProperty (ids::velocityOffset, 0);
    p.quantizeBeats = region.getProperty (ids::playQuantize, 0.0);
    p.swing = juce::jlimit (50.0, 75.0, (double) region.getProperty (ids::playSwing, 50.0));
    p.dynamics = region.getProperty (ids::playDynamics, 100.0);
    p.gate = region.getProperty (ids::playGate, 100.0);
    p.delaySeconds = ((double) region.getProperty (ids::playDelay, 0.0) + (double) track.getProperty (ids::playDelay, 0.0)) / 1000.0;
    p.keyLow = track.getProperty (ids::keyLow, 0);
    p.keyHigh = track.getProperty (ids::keyHigh, 127);
    p.velLow = track.getProperty (ids::velLow, 1);
    p.velHigh = track.getProperty (ids::velHigh, 127);
    return p;
}

bool MidiPlayback::shape (const TempoMap& map, double& start, double& length, int& pitch, int& vel) const
{
    // The limits look at the notes as recorded.
    if (pitch < keyLow || pitch > keyHigh || vel < velLow || vel > velHigh)
        return false;

    pitch = juce::jlimit (0, 127, pitch + transpose);

    // Dynamics spreads or squeezes velocities around the middle; then the offset.
    vel = juce::jlimit (1, 127, juce::roundToInt (64.0 + (vel - 64) * dynamics / 100.0) + velocity);

    // Play quantize, with swing on every second grid step.
    if (quantizeBeats > 0.0)
    {
        const auto beat = map.beatAt (start);
        const auto step = std::round (beat / quantizeBeats);
        auto target = step * quantizeBeats;
        if (((juce::int64) step) % 2 != 0)
            target += (swing - 50.0) / 50.0 * quantizeBeats;
        const auto end = start + length;
        start = map.secondsAt (target);
        length = juce::jmax (0.005, end - start);
    }

    length = juce::jmax (0.005, length * gate / 100.0);
    start = juce::jmax (0.0, start + delaySeconds);
    return true;
}

juce::ValueTree makePatch (const juce::ValueTree& project, const juce::ValueTree& track)
{
    auto patch = createProject (track[ids::name].toString());
    importTracks (patch, project, { track[ids::id].toString() }, nullptr);
    // Just the sound: no regions.
    auto copy = patch.getChild (0);
    for (int i = copy.getNumChildren(); --i >= 0;)
        copy.removeChild (i, nullptr);
    return patch;
}

void applyPatch (juce::ValueTree project, juce::ValueTree track, const juce::ValueTree& patch, juce::UndoManager* um)
{
    const auto source = patch.getChild (0);
    if (! source.hasType (ids::TRACK))
        return;

    const auto trackId = track[ids::id].toString();

    // The sound's properties (not the track's identity, place, routing or record state).
    static const juce::Identifier keep[] { ids::id, ids::name, ids::colour, ids::armed, ids::monitor, ids::input, ids::inputStereo,
                                           ids::height, ids::hidden, ids::output, ids::vca, ids::group, ids::stackParent,
                                           ids::frozen, ids::frozenFile, ids::volume, ids::pan, ids::mute, ids::solo };
    for (int i = 0; i < source.getNumProperties(); ++i)
    {
        const auto name = source.getPropertyName (i);
        if (std::find (std::begin (keep), std::end (keep), name) == std::end (keep))
            track.setProperty (name, source[name], um);
    }

    // Its inserts and channel strip plugin replace the track's (sends stay as they are).
    const auto sourceId = source[ids::id].toString();
    for (const auto& [from, to] : { std::pair { sourceId, trackId }, std::pair { stripOwner (sourceId), stripOwner (trackId) } })
    {
        if (auto chain = findChain (project, to); chain.isValid())
            for (const auto& insert : insertsOf (chain))
                removeInsert (insert, um);

        const auto patchChain = findChain (patch, from);
        if (! patchChain.isValid())
            continue;
        auto chain = chainFor (project, to);
        for (const auto& insert : insertsOf (patchChain))
        {
            auto c = insert.createCopy();
            c.setProperty (ids::id, newId(), nullptr);
            chain.appendChild (c, um);
        }
    }
}

juce::ValueTree addRecordedRegion (juce::ValueTree track, const juce::File& file, double start,
                                   double recordedLength, double latencySeconds, juce::UndoManager* um)
{
    latencySeconds = juce::jmax (0.0, latencySeconds);

    if (recordedLength - latencySeconds < minRegionLength)
        return {};

    auto region = addRegion (track, file, start, recordedLength - latencySeconds, um);
    region.setProperty (ids::offset, latencySeconds, um);
    return region;
}

//==============================================================================
std::vector<TakePiece> splitRecording (double recordStart, double recordedLength, double latency,
                                       bool cycleOn, double cycleStart, double cycleEnd)
{
    // Audio at file position f was played at wall-clock time (f - latency)
    // after the recording started, so usable wall-clock time is:
    const auto usable = recordedLength - latency;
    std::vector<TakePiece> pieces;

    if (usable <= 0.0)
        return pieces;

    const auto cycleLength = cycleEnd - cycleStart;
    const bool wraps = cycleOn && cycleLength > minRegionLength
                       && recordStart < cycleEnd && recordStart + usable > cycleEnd;

    if (! wraps)
    {
        pieces.push_back ({ recordStart, latency, usable });
        return pieces;
    }

    // First pass: from the record start up to the cycle end.
    const auto firstLength = cycleEnd - recordStart;
    pieces.push_back ({ recordStart, latency, firstLength });

    // Every later pass covers the cycle (the last one may be cut short).
    for (double wall = firstLength; wall < usable - minRegionLength; wall += cycleLength)
        pieces.push_back ({ cycleStart, latency + wall, juce::jmin (cycleLength, usable - wall) });

    return pieces;
}

juce::ValueTree addTakeFolder (juce::ValueTree track, const juce::File& file, double start, double length,
                               const std::vector<double>& takeOffsets, int activeIndex, juce::UndoManager* um)
{
    jassert (! takeOffsets.empty());

    juce::ValueTree region { ids::REGION };
    region.setProperty (ids::id, newId(), nullptr);
    region.setProperty (ids::start, start, nullptr);
    region.setProperty (ids::length, length, nullptr);
    region.setProperty (ids::gain, 1.0, nullptr);

    for (size_t i = 0; i < takeOffsets.size(); ++i)
    {
        juce::ValueTree take { ids::TAKE };
        take.setProperty (ids::id, newId(), nullptr);
        take.setProperty (ids::name, "Take " + juce::String ((int) i + 1), nullptr);
        take.setProperty (ids::file, file.getFullPathName(), nullptr);
        take.setProperty (ids::offset, takeOffsets[i], nullptr);
        region.appendChild (take, nullptr);
    }

    const auto active = juce::jlimit (0, (int) takeOffsets.size() - 1, activeIndex);
    const auto chosen = region.getChild (active);
    region.setProperty (ids::activeTake, active, nullptr);
    region.setProperty (ids::file, chosen[ids::file], nullptr);
    region.setProperty (ids::offset, chosen[ids::offset], nullptr);
    region.setProperty (ids::name, track[ids::name].toString() + ": " + chosen[ids::name].toString(), nullptr);

    track.appendChild (region, um);
    return region;
}

int numTakes (const juce::ValueTree& region)
{
    int n = 0;
    for (const auto& child : region)
        if (child.hasType (ids::TAKE))
            ++n;
    return n;
}

void setActiveTake (juce::ValueTree region, int index, juce::UndoManager* um)
{
    const auto take = region.getChild (index);
    if (! take.hasType (ids::TAKE))
        return;

    region.setProperty (ids::activeTake, index, um);
    region.setProperty (ids::file, take[ids::file], um);
    region.setProperty (ids::offset, take[ids::offset], um);

    const auto trackName = region.getParent()[ids::name].toString();
    region.setProperty (ids::name, trackName.isNotEmpty() ? trackName + ": " + take[ids::name].toString()
                                                          : take[ids::name].toString(), um);
}

void compSwipe (juce::ValueTree region, int takeIndex, double from, double to, juce::UndoManager* um)
{
    if (to < from)
        std::swap (from, to);
    from = juce::jmax (0.0, from);
    to = juce::jmin ((double) region[ids::length], to);
    if (to - from < 0.005 || ! region.getChild (takeIndex).hasType (ids::TAKE))
        return;

    // What the new segment covers is taken away from the others.
    juce::Array<juce::ValueTree> segments;
    for (const auto& s : region)
        if (s.hasType (ids::COMPSEG))
            segments.add (s);

    for (auto s : segments)
    {
        const double a = s[ids::from], b = s[ids::to];
        if (b <= from || a >= to)
            continue;
        if (a >= from && b <= to)
            region.removeChild (s, um);
        else if (a < from && b > to)
        {
            juce::ValueTree right (ids::COMPSEG);
            right.setProperty (ids::from, to, nullptr);
            right.setProperty (ids::to, b, nullptr);
            right.setProperty (ids::take, s[ids::take], nullptr);
            region.appendChild (right, um);
            s.setProperty (ids::to, from, um);
        }
        else if (a < from)
            s.setProperty (ids::to, from, um);
        else
            s.setProperty (ids::from, to, um);
    }

    juce::ValueTree seg (ids::COMPSEG);
    seg.setProperty (ids::from, from, nullptr);
    seg.setProperty (ids::to, to, nullptr);
    seg.setProperty (ids::take, takeIndex, nullptr);
    region.appendChild (seg, um);
}

std::vector<CompPiece> compPieces (const juce::ValueTree& region)
{
    std::vector<CompPiece> pieces;
    for (const auto& s : region)
        if (s.hasType (ids::COMPSEG))
            pieces.push_back ({ (double) s[ids::from], (double) s[ids::to], (int) s[ids::take] });
    std::sort (pieces.begin(), pieces.end(), [] (const CompPiece& a, const CompPiece& b) { return a.from < b.from; });

    // The gaps play the active take.
    std::vector<CompPiece> full;
    const int active = region.getProperty (ids::activeTake, 0);
    double at = 0.0;
    const double length = region[ids::length];
    for (const auto& p : pieces)
    {
        if (p.from > at + 1.0e-6)
            full.push_back ({ at, p.from, active });
        full.push_back (p);
        at = juce::jmax (at, p.to);
    }
    if (! pieces.empty() && at < length - 1.0e-6)
        full.push_back ({ at, length, active });
    return full;
}

juce::Array<juce::ValueTree> flattenComp (juce::ValueTree region, juce::UndoManager* um)
{
    juce::Array<juce::ValueTree> made;
    auto track = region.getParent();
    const auto pieces = compPieces (region);
    if (pieces.empty() || ! track.isValid())
        return made;

    const double start = region[ids::start];
    for (const auto& p : pieces)
    {
        const auto take = region.getChild (p.take);
        if (! take.hasType (ids::TAKE))
            continue;
        auto r = addRegion (track, juce::File (take[ids::file].toString()), start + p.from, p.to - p.from, um);
        r.setProperty (ids::offset, (double) take[ids::offset] + p.from, um);
        r.setProperty (ids::gain, region.getProperty (ids::gain, 1.0), um);
        r.setProperty (ids::name, region[ids::name].toString().upToFirstOccurrenceOf (":", false, false) + ": " + take[ids::name].toString(), um);
        made.add (r);
    }
    if (! made.isEmpty())
    {
        made.getFirst().setProperty (ids::fadeIn, region.getProperty (ids::fadeIn, 0.0), um);
        made.getLast().setProperty (ids::fadeOut, region.getProperty (ids::fadeOut, 0.0), um);
    }
    deleteRegion (region, um);
    return made;
}

void flattenTakes (juce::ValueTree region, juce::UndoManager* um)
{
    for (int i = region.getNumChildren(); --i >= 0;)
        if (region.getChild (i).hasType (ids::TAKE) || region.getChild (i).hasType (ids::COMPSEG))
            region.removeChild (i, um);

    region.removeProperty (ids::activeTake, um);
}

//==============================================================================
bool isInstrumentTrack (const juce::ValueTree& track)
{
    return track[ids::kind].toString() == instrumentKind;
}

bool isMidiRegion (const juce::ValueTree& region)
{
    return (bool) region[ids::midi];
}

juce::ValueTree addMidiRegion (juce::ValueTree track, double start, double length, juce::UndoManager* um)
{
    juce::ValueTree region { ids::REGION };
    region.setProperty (ids::id, newId(), nullptr);
    region.setProperty (ids::name, track[ids::name], nullptr);
    region.setProperty (ids::midi, true, nullptr);
    region.setProperty (ids::start, juce::jmax (0.0, start), nullptr);
    region.setProperty (ids::offset, 0.0, nullptr);
    region.setProperty (ids::length, juce::jmax (minRegionLength, length), nullptr);
    region.setProperty (ids::gain, 1.0, nullptr);

    track.appendChild (region, um);
    return region;
}

juce::ValueTree addNote (juce::ValueTree region, int pitch, double start, double length, int velocity, juce::UndoManager* um)
{
    juce::ValueTree note { ids::NOTE };
    note.setProperty (ids::pitch, juce::jlimit (0, 127, pitch), nullptr);
    note.setProperty (ids::start, juce::jmax (0.0, start), nullptr);
    note.setProperty (ids::length, juce::jmax (minRegionLength, length), nullptr);
    note.setProperty (ids::velocity, juce::jlimit (1, 127, velocity), nullptr);

    region.appendChild (note, um);
    return note;
}

void moveNote (juce::ValueTree note, double newStart, int newPitch, juce::UndoManager* um)
{
    note.setProperty (ids::start, juce::jmax (0.0, newStart), um);
    note.setProperty (ids::pitch, juce::jlimit (0, 127, newPitch), um);
}

void resizeNote (juce::ValueTree note, double newLength, juce::UndoManager* um)
{
    note.setProperty (ids::length, juce::jmax (minRegionLength, newLength), um);
}

void setNoteVelocity (juce::ValueTree note, int velocity, juce::UndoManager* um)
{
    note.setProperty (ids::velocity, juce::jlimit (1, 127, velocity), um);
}

void deleteNote (juce::ValueTree note, juce::UndoManager* um)
{
    if (auto parent = note.getParent(); parent.isValid())
        parent.removeChild (note, um);
}

void quantizeNotes (juce::ValueTree region, double gridSeconds, juce::UndoManager* um)
{
    if (gridSeconds <= 0.0)
        return;

    for (auto note : region)
        if (note.hasType (ids::NOTE))
            note.setProperty (ids::start, juce::jmax (0.0, std::round ((double) note[ids::start] / gridSeconds) * gridSeconds), um);
}

void quantizeNotesTo (const juce::Array<juce::ValueTree>& notes, double grid, double strength, double swing,
                      double contentToSong, juce::UndoManager* um)
{
    if (grid <= 0.0)
        return;

    for (auto note : notes)
    {
        const auto song = (double) note[ids::start] + contentToSong;
        const auto step = std::round (song / grid);
        auto target = step * grid;
        if (((juce::int64) step) % 2 != 0)
            target += swing * grid * 0.5;
        const auto moved = song + (target - song) * juce::jlimit (0.0, 1.0, strength);
        note.setProperty (ids::start, juce::jmax (0.0, moved - contentToSong), um);
    }
}

juce::ValueTree addControllerEvent (juce::ValueTree region, int controller, double start, double value, juce::UndoManager* um)
{
    juce::ValueTree event { ids::CC };
    event.setProperty (ids::start, juce::jmax (0.0, start), nullptr);
    event.setProperty (ids::controller, controller, nullptr);
    event.setProperty (ids::value, juce::jlimit (0.0, 1.0, value), nullptr);
    region.appendChild (event, um);
    return event;
}

juce::Array<juce::ValueTree> controllerEvents (const juce::ValueTree& region, int controller)
{
    juce::Array<juce::ValueTree> events;
    for (const auto& e : region)
        if (e.hasType (ids::CC) && (int) e[ids::controller] == controller)
            events.add (e);
    std::stable_sort (events.begin(), events.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::start] < (double) b[ids::start]; });
    return events;
}

juce::String controllerName (int controller)
{
    switch (controller)
    {
        case pitchBend: return "Pitch Bend";
        case 1:  return "Modulation";
        case 2:  return "Breath";
        case 7:  return "Volume";
        case 10: return "Pan";
        case 11: return "Expression";
        case 64: return "Sustain";
        default: return "CC " + juce::String (controller);
    }
}

bool isInScale (int pitch, int keyRoot, bool minor)
{
    static constexpr bool majorSteps[12] { true, false, true, false, true, true, false, true, false, true, false, true };
    static constexpr bool minorSteps[12] { true, false, true, true, false, true, false, true, true, false, true, false };
    const auto degree = ((pitch - keyRoot) % 12 + 12) % 12;
    return minor ? minorSteps[degree] : majorSteps[degree];
}

void scaleQuantize (const juce::Array<juce::ValueTree>& notes, int keyRoot, bool minor, juce::UndoManager* um)
{
    for (auto note : notes)
    {
        const int pitch = note[ids::pitch];
        if (isInScale (pitch, keyRoot, minor))
            continue;
        // The nearest scale note (down first: a semitone either way, the one below wins a tie).
        const auto target = isInScale (pitch - 1, keyRoot, minor) ? pitch - 1 : pitch + 1;
        note.setProperty (ids::pitch, juce::jlimit (0, 127, target), um);
    }
}

void transformNotes (const juce::Array<juce::ValueTree>& notes, Transform transform, juce::UndoManager* um, int seed)
{
    if (notes.isEmpty())
        return;

    auto sorted = notes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::start] < (double) b[ids::start]; });
    const double first = sorted.getFirst()[ids::start];
    double last = first;
    for (const auto& n : sorted)
        last = juce::jmax (last, (double) n[ids::start] + (double) n[ids::length]);

    juce::Random random (seed != 0 ? seed : juce::Random::getSystemRandom().nextInt());

    for (int i = 0; i < sorted.size(); ++i)
    {
        auto note = sorted[i];
        const double start = note[ids::start], length = note[ids::length];
        const int velocity = note[ids::velocity], pitch = note[ids::pitch];

        switch (transform)
        {
            case Transform::humanize:
                note.setProperty (ids::start, juce::jmax (0.0, start + (random.nextDouble() - 0.5) * 0.02), um);   // +-10 ms
                note.setProperty (ids::velocity, juce::jlimit (1, 127, velocity + random.nextInt (21) - 10), um);
                break;
            case Transform::fixedVelocity:
                note.setProperty (ids::velocity, 100, um);
                break;
            case Transform::crescendo:
            case Transform::decrescendo:
            {
                const auto f = sorted.size() > 1 ? (double) i / (double) (sorted.size() - 1) : 1.0;
                const auto v = transform == Transform::crescendo ? 40.0 + f * 87.0 : 127.0 - f * 87.0;
                note.setProperty (ids::velocity, juce::jlimit (1, 127, juce::roundToInt (v)), um);
                break;
            }
            case Transform::legato:
            {
                // Up to the next note that starts later.
                for (int j = i + 1; j < sorted.size(); ++j)
                    if ((double) sorted[j][ids::start] > start + 1.0e-6)
                    {
                        note.setProperty (ids::length, (double) sorted[j][ids::start] - start, um);
                        break;
                    }
                break;
            }
            case Transform::doubleSpeed:
                note.setProperty (ids::start, first + (start - first) * 0.5, um);
                note.setProperty (ids::length, juce::jmax (minRegionLength, length * 0.5), um);
                break;
            case Transform::halfSpeed:
                note.setProperty (ids::start, first + (start - first) * 2.0, um);
                note.setProperty (ids::length, length * 2.0, um);
                break;
            case Transform::transposeUp:
                note.setProperty (ids::pitch, juce::jlimit (0, 127, pitch + 12), um);
                break;
            case Transform::transposeDown:
                note.setProperty (ids::pitch, juce::jlimit (0, 127, pitch - 12), um);
                break;
            case Transform::reverse:
                // Mirrored in time within the notes' span.
                note.setProperty (ids::start, juce::jmax (0.0, first + (last - (start + length) )), um);
                break;
            case Transform::randomVelocity:
                note.setProperty (ids::velocity, 40 + random.nextInt (88), um);
                break;
            case Transform::invertPitch:
            case Transform::reversePitch:
                break;   // done for all notes together below
            case Transform::noteRepeat:
            {
                // The note becomes repeats a sixteenth apart (the length of a sixteenth at 120 BPM unless set by `seed`).
                const auto step = seed > 0 ? seed / 1000.0 : 0.125;
                if (length < step * 1.5)
                    break;
                const auto count = (int) std::floor (length / step + 1.0e-6);
                note.setProperty (ids::length, step * 0.9, um);
                auto region = note.getParent();
                for (int k = 1; k < count; ++k)
                    addNote (region, pitch, start + k * step, step * 0.9, velocity, um);
                break;
            }
        }
    }

    // Pitch mirrors: around the middle of the notes' range, or the order of pitches reversed in time.
    if (transform == Transform::invertPitch || transform == Transform::reversePitch)
    {
        int lo = 127, hi = 0;
        for (const auto& n : sorted)
        {
            lo = juce::jmin (lo, (int) n[ids::pitch]);
            hi = juce::jmax (hi, (int) n[ids::pitch]);
        }
        std::vector<int> pitches;
        for (const auto& n : sorted)
            pitches.push_back (n[ids::pitch]);
        for (int i = 0; i < sorted.size(); ++i)
        {
            auto n = sorted[i];
            n.setProperty (ids::pitch, transform == Transform::invertPitch ? lo + hi - pitches[(size_t) i]
                                                                           : pitches[pitches.size() - 1 - (size_t) i], um);
        }
    }
}

std::vector<RecordedNote> notesFromEvents (const std::vector<RecordedMidiEvent>& events,
                                           double regionStart, double regionEnd)
{
    struct Pending { double time; int velocity; };
    std::array<std::vector<Pending>, 128> held;
    std::vector<RecordedNote> notes;

    auto finish = [&] (int pitch, const Pending& on, double end)
    {
        // Released "before" it started means the playhead wrapped round the cycle.
        if (end < on.time)
            end = regionEnd;

        const auto start = juce::jmax (on.time, regionStart);
        end = juce::jmin (end, regionEnd);

        if (end - start >= 0.001)
            notes.push_back ({ pitch, start - regionStart, juce::jmax (minRegionLength, end - start), on.velocity });
    };

    for (const auto& e : events)
    {
        const auto type = e.status & 0xf0;
        const int pitch = e.data1 & 0x7f;

        if (type == 0x90 && e.data2 > 0)
        {
            held[(size_t) pitch].push_back ({ e.time, e.data2 });
        }
        else if (type == 0x80 || (type == 0x90 && e.data2 == 0))
        {
            auto& stack = held[(size_t) pitch];
            if (! stack.empty())
            {
                finish (pitch, stack.front(), e.time);
                stack.erase (stack.begin());
            }
        }
    }

    for (int pitch = 0; pitch < 128; ++pitch)
        for (const auto& on : held[(size_t) pitch])
            finish (pitch, on, regionEnd);

    std::sort (notes.begin(), notes.end(), [] (const RecordedNote& a, const RecordedNote& b) { return a.start < b.start; });
    return notes;
}

void setCycle (juce::ValueTree project, double start, double end, bool enabled)
{
    // The cycle is a transport setting, not an edit, so it isn't undoable.
    if (end < start)
        std::swap (start, end);

    project.setProperty (ids::cycleStart, juce::jmax (0.0, start), nullptr);
    project.setProperty (ids::cycleEnd, juce::jmax (start + minRegionLength, end), nullptr);
    project.setProperty (ids::cycleEnabled, enabled, nullptr);
}

void deleteRegion (juce::ValueTree region, juce::UndoManager* um)
{
    if (auto parent = region.getParent(); parent.isValid())
        parent.removeChild (region, um);
}

void deleteTrack (juce::ValueTree track, juce::UndoManager* um)
{
    if (auto parent = track.getParent(); parent.isValid())
        parent.removeChild (track, um);
}

void moveTracks (juce::ValueTree project, const juce::Array<juce::ValueTree>& tracks, int newIndex, juce::UndoManager* um)
{
    // Take them out in order and put them back together at the new place (tracks stay before the MIXER).
    juce::Array<juce::ValueTree> moving;
    for (const auto& t : tracks)
        if (t.getParent() == project && t.hasType (ids::TRACK))
            moving.addIfNotAlreadyThere (t);
    std::sort (moving.begin(), moving.end(), [&project] (const auto& a, const auto& b) { return project.indexOf (a) < project.indexOf (b); });
    if (moving.isEmpty())
        return;

    // The order we want: the tracks that stay, with the moved ones together at the insertion point.
    juce::Array<juce::ValueTree> wanted;
    const auto count = numTracks (project);
    int insertAt = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto t = project.getChild (i);
        if (moving.contains (t))
            continue;
        if (i < newIndex)
            ++insertAt;
        wanted.add (t);
    }
    for (int i = 0; i < moving.size(); ++i)
        wanted.insert (insertAt + i, moving[i]);

    // Reach it with moves only (no track is taken out, so instruments and plugins stay loaded).
    for (int i = 0; i < wanted.size(); ++i)
    {
        const auto from = project.indexOf (wanted[i]);
        if (from != i)
            project.moveChild (from, i, um);
    }
}

const juce::Array<juce::Colour>& trackPalette()
{
    // Logic-like colours: the console ones first, then lighter and deeper versions.
    static const juce::Array<juce::Colour> colours = []
    {
        juce::Array<juce::Colour> c;
        for (const auto& p : palette)
            c.add (p);
        for (const auto& p : palette)
            c.add (p.brighter (0.45f));
        for (const auto& p : palette)
            c.add (p.darker (0.45f));
        c.add (juce::Colour (0xff8c8c8c));
        c.add (juce::Colour (0xff5a5f73));
        return c;
    }();
    return colours;
}

//==============================================================================
//==============================================================================
int numTracks (const juce::ValueTree& project)
{
    int n = 0;
    for (const auto& child : project)
        if (child.hasType (ids::TRACK))
            ++n;
    return n;
}

juce::ValueTree tempoTrackOf (juce::ValueTree project)
{
    auto tempo = project.getChildWithName (ids::TEMPO);
    if (! tempo.isValid())
    {
        tempo = juce::ValueTree (ids::TEMPO);
        project.appendChild (tempo, nullptr);
    }
    return tempo;
}

juce::ValueTree addTempoChange (juce::ValueTree project, double beat, double bpm, juce::UndoManager* um)
{
    juce::ValueTree change (ids::TEMPOCHANGE);
    change.setProperty (ids::id, newId(), nullptr);
    change.setProperty (ids::beat, juce::jmax (0.0, beat), nullptr);
    change.setProperty (ids::bpm, juce::jlimit (20.0, 400.0, bpm), nullptr);
    tempoTrackOf (project).appendChild (change, um);
    return change;
}

juce::Array<juce::ValueTree> sortedTempoChanges (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> changes;
    for (const auto& c : project.getChildWithName (ids::TEMPO))
        if (c.hasType (ids::TEMPOCHANGE))
            changes.add (c);
    std::stable_sort (changes.begin(), changes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::beat] < (double) b[ids::beat]; });
    return changes;
}

juce::ValueTree addSignatureChange (juce::ValueTree project, int bar, int beatsPerBar, juce::UndoManager* um)
{
    juce::ValueTree change (ids::SIGCHANGE);
    change.setProperty (ids::id, newId(), nullptr);
    change.setProperty (ids::bar, juce::jmax (2, bar), nullptr);
    change.setProperty (ids::timeSigNumerator, juce::jlimit (1, 16, beatsPerBar), nullptr);
    tempoTrackOf (project).appendChild (change, um);
    return change;
}

juce::Array<juce::ValueTree> sortedSignatureChanges (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> changes;
    for (const auto& c : project.getChildWithName (ids::TEMPO))
        if (c.hasType (ids::SIGCHANGE))
            changes.add (c);
    std::stable_sort (changes.begin(), changes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (int) a[ids::bar] < (int) b[ids::bar]; });
    return changes;
}

juce::ValueTree addKeyChange (juce::ValueTree project, int bar, int keyRoot, bool minor, juce::UndoManager* um)
{
    juce::ValueTree change (ids::KEYCHANGE);
    change.setProperty (ids::id, newId(), nullptr);
    change.setProperty (ids::bar, juce::jmax (2, bar), nullptr);
    change.setProperty (ids::keyRoot, ((keyRoot % 12) + 12) % 12, nullptr);
    change.setProperty (ids::keyMinor, minor, nullptr);
    tempoTrackOf (project).appendChild (change, um);
    return change;
}

juce::Array<juce::ValueTree> sortedKeyChanges (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> changes;
    for (const auto& c : project.getChildWithName (ids::TEMPO))
        if (c.hasType (ids::KEYCHANGE))
            changes.add (c);
    std::stable_sort (changes.begin(), changes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (int) a[ids::bar] < (int) b[ids::bar]; });
    return changes;
}

std::pair<int, bool> keyAt (const juce::ValueTree& project, double seconds)
{
    std::pair<int, bool> key { (int) project.getProperty (ids::keyRoot, 0), (bool) project.getProperty (ids::keyMinor, false) };
    const auto map = tempoMapOf (project);
    const auto bar = map.barAndBeat (map.beatAt (seconds)).first + 1;
    for (const auto& c : sortedKeyChanges (project))
        if ((int) c[ids::bar] <= bar)
            key = { (int) c[ids::keyRoot], (bool) c[ids::keyMinor] };
    return key;
}

juce::String keyName (int root, bool minor)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[((root % 12) + 12) % 12]) + (minor ? " minor" : " major");
}

TempoMap tempoMapOf (const juce::ValueTree& project)
{
    auto map = TempoMap::constant (secondsPerBeat (project));
    map.setBaseMeter (juce::jlimit (1, 16, (int) project.getProperty (ids::timeSigNumerator, 4)));
    for (const auto& c : sortedSignatureChanges (project))
        map.addMeterChange ((int) c[ids::bar] - 1, juce::jlimit (1, 16, (int) c[ids::timeSigNumerator]));
    for (const auto& c : sortedTempoChanges (project))
        if ((double) c[ids::beat] > 1.0e-9)
            map.addChange (c[ids::beat], 60.0 / juce::jlimit (20.0, 400.0, (double) c[ids::bpm]));
    return map;
}

void followTempo (juce::ValueTree project, const TempoMap& before, juce::UndoManager* um)
{
    const auto after = tempoMapOf (project);
    const auto move = [&] (double seconds) { return after.secondsAt (before.beatAt (seconds)); };

    for (auto track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        for (auto region : track)
        {
            if (! region.hasType (ids::REGION) || ! isMidiRegion (region))
                continue;

            // Content times are seconds from the region's content start: map each through the song.
            const double start = region[ids::start], offset = region[ids::offset], length = region[ids::length];
            const auto contentStart = start - offset;   // where content time 0 sits in the song
            const auto newStart = move (start);
            const auto newContentStart = move (contentStart);
            region.setProperty (ids::start, newStart, um);
            region.setProperty (ids::offset, newStart - newContentStart, um);
            region.setProperty (ids::length, move (start + length) - newStart, um);
            if (region.hasProperty (ids::loopEnd))
                region.setProperty (ids::loopEnd, move ((double) region[ids::loopEnd]), um);

            for (auto e : region)
            {
                if (e.hasType (ids::NOTE))
                {
                    const auto noteStart = contentStart + (double) e[ids::start];
                    const auto noteEnd = noteStart + (double) e[ids::length];
                    const auto s = move (noteStart);
                    e.setProperty (ids::start, s - newContentStart, um);
                    e.setProperty (ids::length, juce::jmax (0.001, move (noteEnd) - s), um);
                }
                else if (e.hasType (ids::CC))
                {
                    e.setProperty (ids::start, move (contentStart + (double) e[ids::start]) - newContentStart, um);
                }
            }
        }
    }

    for (auto marker : project.getChildWithName (ids::MARKERS))
        marker.setProperty (ids::time, move ((double) marker[ids::time]), um);

    for (const auto& id : { ids::cycleStart, ids::cycleEnd, ids::punchIn, ids::punchOut })
        if (project.hasProperty (id))
            project.setProperty (id, move ((double) project[id]), um);
}

void setTempo (juce::ValueTree project, double bpm, juce::UndoManager* um)
{
    const auto before = tempoMapOf (project);
    project.setProperty (ids::tempo, juce::jlimit (20.0, 400.0, bpm), um);
    followTempo (project, before, um);
}

BarPosition barPositionAt (const juce::ValueTree& project, double seconds)
{
    const auto map = tempoMapOf (project);
    const auto beats = juce::jmax (0.0, map.beatAt (seconds));
    const auto [bar, inBar] = map.barAndBeat (beats);
    const auto beatsPerBar = map.beatsPerBarAt (beats);
    const auto beat = juce::jlimit (0, beatsPerBar - 1, (int) std::floor (inBar + 1.0e-9));
    const auto quarters = (inBar - beat) * 4.0;
    const auto sixteenth = juce::jlimit (0, 3, (int) std::floor (quarters + 1.0e-9));
    return { bar + 1, beat + 1, sixteenth + 1, quarters - sixteenth };
}

double moveByBars (const juce::ValueTree& project, double seconds, int bars)
{
    const auto map = tempoMapOf (project);
    const auto [barIndex, inBar] = map.barAndBeat (map.beatAt (seconds));
    auto bar = barIndex;
    // Back from inside a bar goes to its start first.
    if (bars < 0 && inBar > 1.0e-6)
        bar += 1;
    return juce::jmax (0.0, map.secondsAt (map.beatOfBar (juce::jmax (0, bar + bars))));
}

namespace
{
    juce::Array<juce::ValueTree> allRegions (const juce::ValueTree& project)
    {
        juce::Array<juce::ValueTree> regions;
        for (const auto& track : project)
            if (track.hasType (ids::TRACK))
                for (const auto& r : track)
                    if (r.hasType (ids::REGION))
                        regions.add (r);
        return regions;
    }

    /** Moves everything at or after `from` by `delta` seconds (regions, automation, markers). */
    void shiftAfter (juce::ValueTree project, double from, double delta, juce::UndoManager* um)
    {
        for (auto r : allRegions (project))
            if ((double) r[ids::start] >= from - 1.0e-9)
                r.setProperty (ids::start, juce::jmax (0.0, (double) r[ids::start] + delta), um);

        for (auto lane : project.getChildWithName (ids::MIXER))
            if (lane.hasType (ids::LANE))
                for (auto p : lane)
                    if ((double) p[ids::time] >= from - 1.0e-9)
                        p.setProperty (ids::time, juce::jmax (0.0, (double) p[ids::time] + delta), um);

        for (auto m : project.getChildWithName (ids::MARKERS))
            if ((double) m[ids::time] >= from - 1.0e-9)
                m.setProperty (ids::time, juce::jmax (0.0, (double) m[ids::time] + delta), um);
    }
}

void splitAtTimes (juce::ValueTree project, double a, double b, juce::UndoManager* um)
{
    for (auto r : allRegions (project))
    {
        auto right = splitRegion (r, b, um);
        juce::ignoreUnused (right);
        splitRegion (r, a, um);
    }
}

void deleteSection (juce::ValueTree project, double a, double b, juce::UndoManager* um)
{
    if (b <= a)
        return;
    splitAtTimes (project, a, b, um);

    for (auto r : allRegions (project))
        if ((double) r[ids::start] >= a - 1.0e-6 && regionEnd (r) <= b + 1.0e-6)
            deleteRegion (r, um);

    for (auto lane : project.getChildWithName (ids::MIXER))
        if (lane.hasType (ids::LANE))
            for (int i = lane.getNumChildren(); --i >= 0;)
                if (const double t = lane.getChild (i)[ids::time]; t >= a && t < b)
                    lane.removeChild (i, um);

    auto markers = project.getChildWithName (ids::MARKERS);
    for (int i = markers.getNumChildren(); --i >= 0;)
        if (const double t = markers.getChild (i)[ids::time]; t >= a && t < b)
            markers.removeChild (i, um);

    shiftAfter (project, b, -(b - a), um);
}

void insertSilence (juce::ValueTree project, double a, double b, juce::UndoManager* um)
{
    if (b <= a)
        return;
    for (auto r : allRegions (project))
        splitRegion (r, a, um);
    shiftAfter (project, a, b - a, um);
}

void duplicateSection (juce::ValueTree project, double a, double b, juce::UndoManager* um)
{
    if (b <= a)
        return;
    const auto length = b - a;
    splitAtTimes (project, a, b, um);

    // What's in the section, before anything moves.
    juce::Array<juce::ValueTree> inside;
    for (const auto& r : allRegions (project))
        if ((double) r[ids::start] >= a - 1.0e-6 && regionEnd (r) <= b + 1.0e-6)
            inside.add (r);
    struct Point { juce::ValueTree lane; double time, value; };
    std::vector<Point> points;
    for (const auto& lane : project.getChildWithName (ids::MIXER))
        if (lane.hasType (ids::LANE))
            for (const auto& p : lane)
                if (const double t = p[ids::time]; t >= a && t < b)
                    points.push_back ({ lane, t, (double) p[ids::value] });
    std::vector<std::pair<double, juce::String>> markers;
    for (const auto& m : project.getChildWithName (ids::MARKERS))
        if (const double t = m[ids::time]; t >= a && t < b)
            markers.push_back ({ t, m[ids::name].toString() });

    shiftAfter (project, b, length, um);

    for (const auto& r : inside)
        duplicateRegion (r, r.getParent(), (double) r[ids::start] + length, um);
    for (auto& p : points)
        addPoint (p.lane, p.time + length, p.value, um);
    for (const auto& [t, name] : markers)
        addMarker (project, t + length, name, um);
}

std::pair<double, double> markerSection (const juce::ValueTree& project, const juce::ValueTree& marker)
{
    const double start = marker[ids::time];
    double end = juce::jmax (start, projectEnd (project));
    for (const auto& m : sortedMarkers (project))
        if ((double) m[ids::time] > start + 1.0e-9)
        {
            end = m[ids::time];
            break;
        }
    return { start, end };
}

juce::ValueTree liveCell (const juce::ValueTree& project, const juce::String& trackId, int scene)
{
    for (const auto& c : project.getChildWithName (ids::LIVELOOPS))
        if (c[ids::track].toString() == trackId && (int) c[ids::scene] == scene)
            return c;
    return {};
}

juce::ValueTree setLiveCell (juce::ValueTree project, const juce::String& trackId, int scene, const juce::ValueTree& region, juce::UndoManager* um)
{
    auto grid = project.getChildWithName (ids::LIVELOOPS);
    if (! grid.isValid())
    {
        grid = juce::ValueTree (ids::LIVELOOPS);
        project.appendChild (grid, nullptr);
    }
    if (auto old = liveCell (project, trackId, scene); old.isValid())
        grid.removeChild (old, um);
    if (! region.isValid())
        return {};

    juce::ValueTree cell (ids::CELL);
    cell.setProperty (ids::id, newId(), nullptr);
    cell.setProperty (ids::track, trackId, nullptr);
    cell.setProperty (ids::scene, scene, nullptr);
    auto copy = region.createCopy();
    copy.setProperty (ids::id, newId(), nullptr);
    copy.setProperty (ids::start, 0.0, nullptr);
    copy.removeProperty (ids::loop, nullptr);
    copy.removeProperty (ids::loopEnd, nullptr);
    cell.appendChild (copy, nullptr);
    grid.appendChild (cell, um);
    return cell;
}

void captureScene (juce::ValueTree project, int scene, double a, double b, juce::UndoManager* um)
{
    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;
        // The first region that starts inside the stretch, trimmed to it.
        for (const auto& r : track)
            if (r.hasType (ids::REGION) && (double) r[ids::start] >= a - 1.0e-6 && (double) r[ids::start] < b)
            {
                auto copy = r.createCopy();
                copy.setProperty (ids::length, juce::jmin ((double) r[ids::length], b - (double) r[ids::start]), nullptr);
                setLiveCell (project, track[ids::id].toString(), scene, copy, um);
                break;
            }
    }
}

juce::ValueTree markersOf (juce::ValueTree project)
{
    auto markers = project.getChildWithName (ids::MARKERS);
    if (! markers.isValid())
    {
        markers = juce::ValueTree (ids::MARKERS);
        project.appendChild (markers, nullptr);   // after the tracks and the MIXER
    }
    return markers;
}

juce::ValueTree addMarker (juce::ValueTree project, double time, const juce::String& name, juce::UndoManager* um)
{
    auto markers = markersOf (project);
    juce::ValueTree marker (ids::MARKER);
    marker.setProperty (ids::id, newId(), nullptr);
    marker.setProperty (ids::name, name.isNotEmpty() ? name : "Marker " + juce::String (markers.getNumChildren() + 1), nullptr);
    marker.setProperty (ids::time, juce::jmax (0.0, time), nullptr);
    markers.appendChild (marker, um);
    return marker;
}

juce::Array<juce::ValueTree> sortedMarkers (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> list;
    for (const auto& m : project.getChildWithName (ids::MARKERS))
        list.add (m);
    std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return (double) a[ids::time] < (double) b[ids::time]; });
    return list;
}

juce::ValueTree adjacentMarker (const juce::ValueTree& project, double time, bool forwards)
{
    const auto list = sortedMarkers (project);
    if (forwards)
    {
        for (const auto& m : list)
            if ((double) m[ids::time] > time + 1.0e-6)
                return m;
    }
    else
    {
        for (int i = list.size(); --i >= 0;)
            if ((double) list[i][ids::time] < time - 1.0e-6)
                return list[i];
    }
    return {};
}

juce::String keyName (const juce::ValueTree& project)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const auto root = ((int) project.getProperty (ids::keyRoot, 0) % 12 + 12) % 12;
    return juce::String (names[root]) + ((bool) project.getProperty (ids::keyMinor, false) ? " minor" : " major");
}

juce::ValueTree mixerOf (juce::ValueTree project)
{
    auto mixer = project.getChildWithName (ids::MIXER);

    if (! mixer.isValid())
    {
        mixer = juce::ValueTree (ids::MIXER);
        project.appendChild (mixer, nullptr);
    }

    return mixer;
}

juce::ValueTree findChain (const juce::ValueTree& project, const juce::String& owner)
{
    // Only CHAIN nodes: automation lanes have an owner too.
    for (const auto& child : project.getChildWithName (ids::MIXER))
        if (child.hasType (ids::CHAIN) && child[ids::owner].toString() == owner)
            return child;

    return {};
}

juce::ValueTree chainFor (juce::ValueTree project, const juce::String& owner)
{
    auto chain = findChain (project, owner);

    if (! chain.isValid())
    {
        chain = juce::ValueTree (ids::CHAIN);
        chain.setProperty (ids::owner, owner, nullptr);
        mixerOf (project).appendChild (chain, nullptr);
    }

    return chain;
}

juce::ValueTree addBuiltinInsert (juce::ValueTree chain, const juce::String& effectType, int index, juce::UndoManager* um)
{
    juce::ValueTree insert { ids::INSERT };
    insert.setProperty (ids::id, newId(), nullptr);
    insert.setProperty (ids::kind, builtinKind, nullptr);
    insert.setProperty (ids::effect, effectType, nullptr);
    insert.setProperty (ids::bypass, false, nullptr);
    chain.addChild (insert, index, um);
    return insert;
}

juce::ValueTree addPluginInsert (juce::ValueTree chain, const juce::String& descriptionXml, int index, juce::UndoManager* um)
{
    juce::ValueTree insert { ids::INSERT };
    insert.setProperty (ids::id, newId(), nullptr);
    insert.setProperty (ids::kind, pluginKind, nullptr);
    insert.setProperty (ids::pluginDescription, descriptionXml, nullptr);
    insert.setProperty (ids::bypass, false, nullptr);
    chain.addChild (insert, index, um);
    return insert;
}

void removeInsert (juce::ValueTree insert, juce::UndoManager* um)
{
    if (auto parent = insert.getParent(); parent.isValid())
        parent.removeChild (insert, um);
}

//==============================================================================
bool isBusTrack (const juce::ValueTree& track)
{
    return track[ids::kind].toString() == busKind;
}

juce::Array<juce::ValueTree> groupMembers (const juce::ValueTree& project, const juce::ValueTree& track)
{
    const int group = track.getProperty (ids::group, 0);
    if (group <= 0)
        return { track };

    juce::Array<juce::ValueTree> members;
    for (const auto& t : project)
        if (t.hasType (ids::TRACK) && (int) t.getProperty (ids::group, 0) == group)
            members.add (t);
    return members;
}

juce::String groupName (const juce::ValueTree& project, int group)
{
    const auto name = project[groupProperty (group, "Name")].toString();
    return name.isNotEmpty() ? name : "Group " + juce::String (group);
}

bool groupLinks (const juce::ValueTree& project, int group, const juce::String& what)
{
    return group > 0 && (bool) project.getProperty (groupProperty (group, what), true);
}

juce::Array<juce::ValueTree> vcaMembers (const juce::ValueTree& project, const juce::ValueTree& vcaTrack)
{
    juce::Array<juce::ValueTree> members;
    const auto id = vcaTrack[ids::id].toString();
    for (const auto& t : project)
        if (t.hasType (ids::TRACK) && t[ids::vca].toString() == id && id.isNotEmpty())
            members.add (t);
    return members;
}

namespace
{
    /** The VCAs above a track, nearest first (a VCA can itself be on a VCA; loops are cut). */
    juce::Array<juce::ValueTree> vcaChain (const juce::ValueTree& project, const juce::ValueTree& track)
    {
        juce::Array<juce::ValueTree> chain;
        auto current = track;
        for (int depth = 0; depth < 8; ++depth)
        {
            const auto vcaId = current[ids::vca].toString();
            if (vcaId.isEmpty())
                break;
            const auto vca = findById (project, vcaId);
            if (! vca.isValid() || chain.contains (vca) || vca == track)
                break;
            chain.add (vca);
            current = vca;
        }
        return chain;
    }
}

double vcaGainOf (const juce::ValueTree& project, const juce::ValueTree& track)
{
    double gain = 1.0;
    for (const auto& vca : vcaChain (project, track))
        gain *= (double) vca.getProperty (ids::volume, 1.0);
    return gain;
}

bool vcaMutes (const juce::ValueTree& project, const juce::ValueTree& track)
{
    for (const auto& vca : vcaChain (project, track))
        if ((bool) vca[ids::mute])
            return true;
    return false;
}

bool vcaSolos (const juce::ValueTree& project, const juce::ValueTree& track)
{
    for (const auto& vca : vcaChain (project, track))
        if ((bool) vca[ids::solo])
            return true;
    return false;
}

juce::ValueTree createTrackStack (juce::ValueTree project, const juce::Array<juce::ValueTree>& tracks, bool summing, juce::UndoManager* um)
{
    if (tracks.isEmpty())
        return {};

    auto ordered = tracks;
    std::sort (ordered.begin(), ordered.end(), [&project] (const juce::ValueTree& a, const juce::ValueTree& b)
               { return project.indexOf (a) < project.indexOf (b); });

    const auto index = project.indexOf (ordered.getFirst());
    auto main = addTrack (project, summing ? "Summing Stack" : "Folder Stack", um, index, summing ? busKind : vcaKind);
    main.setProperty (ids::stack, summing ? "summing" : "folder", um);
    main.setProperty (ids::colour, ordered.getFirst()[ids::colour], um);
    const auto mainId = main[ids::id].toString();

    for (auto t : ordered)
    {
        t.setProperty (ids::stackParent, mainId, um);
        t.setProperty (summing ? ids::output : ids::vca, mainId, um);
    }

    moveTracks (project, ordered, project.indexOf (main) + 1, um);
    return main;
}

void flattenStack (juce::ValueTree project, juce::ValueTree mainTrack, juce::UndoManager* um)
{
    const auto mainId = mainTrack[ids::id].toString();
    for (auto t : project)
    {
        if (! t.hasType (ids::TRACK) || t[ids::stackParent].toString() != mainId)
            continue;
        t.removeProperty (ids::stackParent, um);
        if (t[ids::output].toString() == mainId) t.removeProperty (ids::output, um);
        if (t[ids::vca].toString() == mainId)    t.removeProperty (ids::vca, um);
    }
    deleteTrack (mainTrack, um);
}

bool isInCollapsedStack (const juce::ValueTree& project, const juce::ValueTree& track)
{
    auto current = track;
    for (int depth = 0; depth < 8; ++depth)
    {
        const auto parentId = current[ids::stackParent].toString();
        if (parentId.isEmpty())
            return false;
        const auto parent = findById (project, parentId);
        if (! parent.isValid() || parent == track)
            return false;
        if ((bool) parent[ids::collapsed])
            return true;
        current = parent;
    }
    return false;
}

juce::ValueTree addSend (juce::ValueTree chain, const juce::String& busId, double level, juce::UndoManager* um)
{
    juce::ValueTree send { ids::SEND };
    send.setProperty (ids::id, newId(), nullptr);
    send.setProperty (ids::target, busId, nullptr);
    send.setProperty (ids::level, level, nullptr);
    send.setProperty (ids::pre, false, nullptr);
    chain.appendChild (send, um);
    return send;
}

juce::Array<juce::ValueTree> sendsOf (const juce::ValueTree& chain)
{
    juce::Array<juce::ValueTree> result;
    for (const auto& c : chain)
        if (c.hasType (ids::SEND))
            result.add (c);
    return result;
}

juce::Array<juce::ValueTree> insertsOf (const juce::ValueTree& chain)
{
    // In slot order (the order they process in).
    juce::Array<juce::ValueTree> result;
    for (const auto& c : chain)
        if (c.hasType (ids::INSERT))
            result.add (c);
    std::stable_sort (result.begin(), result.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b) { return insertSlot (a) < insertSlot (b); });
    return result;
}

int insertSlot (const juce::ValueTree& insert)
{
    if (insert.hasProperty (ids::slot))
        return (int) insert[ids::slot];
    // Before slots: its place among the chain's inserts.
    int index = 0;
    for (const auto& c : insert.getParent())
    {
        if (c == insert)
            return index;
        if (c.hasType (ids::INSERT))
            ++index;
    }
    return index;
}

juce::ValueTree insertAtSlot (const juce::ValueTree& chain, int slot)
{
    for (const auto& c : chain)
        if (c.hasType (ids::INSERT) && insertSlot (c) == slot)
            return c;
    return {};
}

juce::ValueTree placeInsert (juce::ValueTree chain, int slot,
                             const std::function<juce::ValueTree (juce::ValueTree chain, int index)>& make, juce::UndoManager* um)
{
    // Give every insert its slot for good (older projects had none), so nothing moves.
    for (auto c : insertsOf (chain))
        if (! c.hasProperty (ids::slot))
            c.setProperty (ids::slot, insertSlot (c), um);

    if (auto old = insertAtSlot (chain, slot); old.isValid())
        removeInsert (old, um);

    // In the chain, after the inserts in the slots above.
    int index = 0;
    for (int i = 0; i < chain.getNumChildren(); ++i)
        if (const auto c = chain.getChild (i); c.hasType (ids::INSERT) && insertSlot (c) < slot)
            index = i + 1;

    auto insert = make (chain, index);
    insert.setProperty (ids::slot, slot, um);
    return insert;
}

//==============================================================================
juce::String insertParam (const juce::String& insertId, const juce::String& paramId)
{
    return "insert:" + insertId + ":" + paramId;
}

juce::ValueTree laneFor (juce::ValueTree project, const juce::String& owner, const juce::String& param, bool create)
{
    auto mixer = mixerOf (project);

    for (const auto& lane : mixer)
        if (lane.hasType (ids::LANE) && lane[ids::owner].toString() == owner && lane[ids::param].toString() == param)
            return lane;

    if (! create)
        return {};

    juce::ValueTree lane { ids::LANE };
    lane.setProperty (ids::owner, owner, nullptr);
    lane.setProperty (ids::param, param, nullptr);
    mixer.appendChild (lane, nullptr);
    return lane;
}

juce::ValueTree busCompPlugin (const juce::ValueTree& project)
{
    const auto inserts = insertsOf (findChain (project, busCompOwner));
    return inserts.isEmpty() ? juce::ValueTree() : inserts.getFirst();
}

juce::ValueTree setBusCompPlugin (juce::ValueTree project, const juce::String& descriptionXml, juce::UndoManager* um)
{
    auto chain = chainFor (project, busCompOwner);
    for (const auto& old : insertsOf (chain))
        removeInsert (old, um);
    return addPluginInsert (chain, descriptionXml, 0, um);
}

juce::ValueTree stripPlugin (const juce::ValueTree& project, const juce::String& trackId)
{
    const auto inserts = insertsOf (findChain (project, stripOwner (trackId)));
    return inserts.isEmpty() ? juce::ValueTree() : inserts.getFirst();
}

juce::ValueTree setStripPlugin (juce::ValueTree project, const juce::String& trackId, const juce::String& descriptionXml, juce::UndoManager* um)
{
    if (auto chain = findChain (project, stripOwner (trackId)); chain.isValid())
        for (const auto& old : insertsOf (chain))
            removeInsert (old, um);

    if (descriptionXml.isEmpty())
        return {};

    return addPluginInsert (chainFor (project, stripOwner (trackId)), descriptionXml, 0, um);
}

juce::String consolePlugin (const juce::ValueTree& project)
{
    return project[ids::consolePlugin].toString();
}

void setConsolePlugin (juce::ValueTree project, const juce::String& descriptionXml, juce::UndoManager* um)
{
    project.setProperty (ids::consolePlugin, descriptionXml, um);

    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;

        const auto id = track[ids::id].toString();
        const bool wanted = descriptionXml.isNotEmpty() && ! (bool) track[ids::builtInStrip];
        const auto current = stripPlugin (project, id);

        if (! wanted && current.isValid())
            setStripPlugin (project, id, {}, um);
        else if (wanted && current[ids::pluginDescription].toString() != descriptionXml)
            setStripPlugin (project, id, descriptionXml, um);
    }
}

bool clearAutomation (juce::ValueTree project, const juce::String& owner, const juce::String& param, juce::UndoManager* um)
{
    auto lane = laneFor (project, owner, param, false);
    if (! lane.isValid())
        return false;

    const bool hadPoints = lane.getNumChildren() > 0;
    mixerOf (project).removeChild (lane, um);
    return hadPoints;
}

int clearAllAutomation (juce::ValueTree project, const juce::String& owner, juce::UndoManager* um)
{
    auto mixer = mixerOf (project);
    juce::Array<juce::ValueTree> lanes;

    for (const auto& lane : mixer)
        if (lane.hasType (ids::LANE) && lane[ids::owner].toString() == owner)
            lanes.add (lane);

    for (auto& lane : lanes)
        mixer.removeChild (lane, um);

    return lanes.size();
}

juce::ValueTree addPoint (juce::ValueTree lane, double time, double value, juce::UndoManager* um)
{
    juce::ValueTree point { ids::POINT };
    point.setProperty (ids::time, juce::jmax (0.0, time), nullptr);
    point.setProperty (ids::value, value, nullptr);

    int index = 0;
    while (index < lane.getNumChildren() && (double) lane.getChild (index)[ids::time] <= time)
        ++index;

    lane.addChild (point, index, um);
    return point;
}

void movePoint (juce::ValueTree point, double time, double value, juce::UndoManager* um)
{
    auto lane = point.getParent();
    time = juce::jmax (0.0, time);
    point.setProperty (ids::time, time, um);
    point.setProperty (ids::value, value, um);

    // Keep time order.
    auto index = lane.indexOf (point);
    while (index > 0 && (double) lane.getChild (index - 1)[ids::time] > time)
        lane.moveChild (index, index - 1, um), --index;
    while (index < lane.getNumChildren() - 1 && (double) lane.getChild (index + 1)[ids::time] < time)
        lane.moveChild (index, index + 1, um), ++index;
}

void removePoint (juce::ValueTree point, juce::UndoManager* um)
{
    if (auto lane = point.getParent(); lane.isValid())
        lane.removeChild (point, um);
}

double valueAt (const juce::ValueTree& lane, double time, double fallback)
{
    const auto n = lane.getNumChildren();
    if (n == 0)
        return fallback;

    const auto first = lane.getChild (0);
    if (time <= (double) first[ids::time])
        return first[ids::value];

    for (int i = 1; i < n; ++i)
    {
        const auto b = lane.getChild (i);
        const double tb = b[ids::time];
        if (time <= tb)
        {
            const auto a = lane.getChild (i - 1);
            const double ta = a[ids::time], va = a[ids::value], vb = b[ids::value];
            return tb > ta ? va + (vb - va) * (time - ta) / (tb - ta) : vb;
        }
    }

    return lane.getChild (n - 1)[ids::value];
}

ParamRange rangeOf (const juce::ValueTree& project, const juce::String& param)
{
    if (param == volumeParam) return { 0.0, juce::Decibels::decibelsToGain (6.0), 1.0, true, "Volume" };
    if (param == panParam)    return { -1.0, 1.0, 0.0, false, "Pan" };

    // "insert:<id>:<param>" on a built-in effect: the range comes from the effect catalogue.
    const auto parts = juce::StringArray::fromTokens (param, ":", {});
    if (parts.size() == 3 && parts[0] == "insert")
    {
        const auto mixer = project.getChildWithName (ids::MIXER);
        for (const auto& chain : mixer)
            for (const auto& insert : chain)
                if (insert[ids::id].toString() == parts[1])
                    if (const auto* info = findBuiltInEffect (insert[ids::effect].toString()))
                        for (const auto& p : info->params)
                            if (p.id == parts[2])
                                return { p.min, p.max, (double) insert.getProperty (p.id, p.defaultValue), false,
                                         info->name + " " + p.name };
    }

    // A plugin parameter ("insert:<id>:#<index>"): 0..1, named when its lane was made.
    if (parts.size() == 3 && parts[0] == "insert" && parts[2].startsWith ("#"))
        for (const auto& lane : project.getChildWithName (ids::MIXER))
            if (lane.hasType (ids::LANE) && lane[ids::param].toString() == param && lane.hasProperty (ids::name))
                return { 0.0, 1.0, 0.5, false, lane[ids::name].toString() };

    return { 0.0, 1.0, 0.5, false, param };
}

juce::ValueTree findById (const juce::ValueTree& project, const juce::String& id)
{
    if (id.isEmpty())
        return {};

    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK))
            continue;

        if (track[ids::id].toString() == id)
            return track;

        for (const auto& region : track)
            if (region[ids::id].toString() == id)
                return region;
    }

    return {};
}

juce::ValueTree trackOf (const juce::ValueTree& region)
{
    return region.getParent();
}

double regionEnd (const juce::ValueTree& region)
{
    return (double) region[ids::start] + (double) region[ids::length];
}

double projectEnd (const juce::ValueTree& project)
{
    double end = 0.0;
    for (const auto& track : project)
        if (track.hasType (ids::TRACK))
            for (const auto& region : track)
                end = juce::jmax (end, loopEndOf (region));
    return end;
}

void setFade (juce::ValueTree region, bool fadeIn, double seconds, juce::UndoManager* um)
{
    const double length = region[ids::length];
    const double other = region.getProperty (fadeIn ? ids::fadeOut : ids::fadeIn, 0.0);
    seconds = juce::jlimit (0.0, juce::jmax (0.0, length - other), seconds);
    region.setProperty (fadeIn ? ids::fadeIn : ids::fadeOut, seconds, um);
}

double loopEndOf (const juce::ValueTree& region)
{
    const auto end = regionEnd (region);
    if (! (bool) region.getProperty (ids::loop, false))
        return end;

    auto loopEnd = juce::jmax (end, (double) region.getProperty (ids::loopEnd, end));
    const double start = region[ids::start];
    for (const auto& other : region.getParent())
        if (other != region && other.hasType (ids::REGION) && (double) other[ids::start] >= end - 1.0e-9
            && (double) other[ids::start] > start)
            loopEnd = juce::jmin (loopEnd, juce::jmax (end, (double) other[ids::start]));
    return loopEnd;
}

void setLoop (juce::ValueTree region, bool shouldLoop, juce::UndoManager* um)
{
    region.setProperty (ids::loop, shouldLoop, um);
    if (! shouldLoop)
        return;

    const auto end = regionEnd (region);
    auto loopEnd = end + 3.0 * (double) region[ids::length];
    for (const auto& other : region.getParent())
        if (other != region && other.hasType (ids::REGION) && (double) other[ids::start] >= end - 1.0e-9)
            loopEnd = juce::jmin (loopEnd, (double) other[ids::start]);
    region.setProperty (ids::loopEnd, juce::jmax (end, loopEnd), um);
}

void createCrossfades (const juce::ValueTree& region, juce::UndoManager* um)
{
    auto track = region.getParent();
    if (! track.isValid())
        return;

    const double start = region[ids::start];
    const auto end = regionEnd (region);

    for (auto other : track)
    {
        if (other == region || ! other.hasType (ids::REGION))
            continue;

        const double oStart = other[ids::start];
        const auto oEnd = regionEnd (other);
        const auto overlapStart = juce::jmax (start, oStart), overlapEnd = juce::jmin (end, oEnd);
        if (overlapEnd - overlapStart < 0.001)
            continue;

        // Only an overlap at one end of each is a crossfade (not one region inside another).
        auto earlier = start < oStart ? region : other;
        auto later   = start < oStart ? other : region;
        if (regionEnd (earlier) >= regionEnd (later))
            continue;

        const auto length = overlapEnd - overlapStart;
        earlier.setProperty (ids::fadeOut, length, um);
        earlier.setProperty (ids::fadeOutCurve, 0.5, um);
        later.setProperty (ids::fadeIn, length, um);
        later.setProperty (ids::fadeInCurve, 0.5, um);
    }
}

void shuffleTrack (juce::ValueTree track, bool towardsLeft, juce::UndoManager* um)
{
    juce::Array<juce::ValueTree> regions;
    for (const auto& r : track)
        if (r.hasType (ids::REGION))
            regions.add (r);
    if (regions.size() < 2)
        return;

    std::sort (regions.begin(), regions.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
               { return (double) a[ids::start] < (double) b[ids::start]; });

    if (towardsLeft)
    {
        auto position = loopEndOf (regions.getFirst());
        for (int i = 1; i < regions.size(); ++i)
        {
            regions.getReference (i).setProperty (ids::start, position, um);
            position = loopEndOf (regions[i]);
        }
    }
    else
    {
        auto position = (double) regions.getLast()[ids::start];
        for (int i = regions.size() - 1; --i >= 0;)
        {
            const auto span = loopEndOf (regions[i]) - (double) regions[i][ids::start];
            position = juce::jmax (0.0, position - span);
            regions.getReference (i).setProperty (ids::start, position, um);
        }
    }
}

juce::Colour trackColour (const juce::ValueTree& track)
{
    return juce::Colour::fromString (track[ids::colour].toString());
}

double secondsPerBeat (const juce::ValueTree& project)
{
    const double tempo = project.getProperty (ids::tempo, 120.0);
    return 60.0 / juce::jlimit (20.0, 400.0, tempo);
}

juce::String inputName (const juce::ValueTree& track, const juce::StringArray& names)
{
    const int first = track.getProperty (ids::input, 0);
    const bool stereo = track.getProperty (ids::inputStereo, false);

    auto nameOf = [&names] (int i) { return juce::isPositiveAndBelow (i, names.size()) ? names[i] : "In " + juce::String (i + 1); };
    return stereo ? nameOf (first) + " + " + nameOf (first + 1) : nameOf (first);
}

//==============================================================================
juce::String toJson (const juce::ValueTree& project, const juce::File& projectFile)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("riffVersion", schemaVersion);
    root->setProperty ("project", treeToVar (project, projectFile.getParentDirectory()));
    return juce::JSON::toString (juce::var (root));
}

juce::ValueTree fromJson (const juce::String& json, const juce::File& projectFile, juce::String& error)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);

    if (result.failed())
    {
        error = "The project file isn't valid JSON: " + result.getErrorMessage();
        return {};
    }

    const int version = parsed.getProperty ("riffVersion", 0);
    if (version < 1)
    {
        error = "This isn't an " + app::name + " project file.";
        return {};
    }
    if (version > schemaVersion)
    {
        error = "This project was made with a newer version of " + app::name + ".";
        return {};
    }

    auto tree = varToTree (parsed.getProperty ("project", {}), projectFile.getParentDirectory(), error);

    if (tree.isValid() && ! tree.hasType (ids::PROJECT))
    {
        error = "The project file has no project in it.";
        return {};
    }

    if (tree.isValid())
    {
        // Older files have no mixer; and the mixer must come after every track.
        auto mixer = tree.getChildWithName (ids::MIXER);
        if (mixer.isValid())
            tree.removeChild (mixer, nullptr);
        else
            mixer = juce::ValueTree (ids::MIXER);
        tree.appendChild (mixer, nullptr);
    }

    return tree;
}

bool save (const juce::ValueTree& project, const juce::File& file, juce::String& error)
{
    // Write to a temp file, then swap it in, so a crash mid-save never corrupts the project.
    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (toJson (project, file)) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }

    return true;
}

juce::ValueTree load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return {};
    }

    return fromJson (file.loadFileAsString(), file, error);
}

} // namespace riff::model
