#include "Project.h"
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
    if (const auto console = consolePlugin (project); console.isNotEmpty())
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
}

void trimRegionEnd (juce::ValueTree region, double newEnd, double sourceLength, juce::UndoManager* um)
{
    const double start  = region[ids::start];
    const double offset = region[ids::offset];

    auto maxEnd = sourceLength > 0.0 ? start + (sourceLength - offset) : std::numeric_limits<double>::max();
    newEnd = juce::jlimit (start + minRegionLength, juce::jmax (start + minRegionLength, maxEnd), newEnd);

    region.setProperty (ids::length, newEnd - start, um);
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

    region.setProperty (ids::length, time - start, um);
    parent.addChild (right, parent.indexOf (region) + 1, um);
    return right;
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

void flattenTakes (juce::ValueTree region, juce::UndoManager* um)
{
    for (int i = region.getNumChildren(); --i >= 0;)
        if (region.getChild (i).hasType (ids::TAKE))
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
    juce::Array<juce::ValueTree> result;
    for (const auto& c : chain)
        if (c.hasType (ids::INSERT))
            result.add (c);
    return result;
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
            end = juce::jmax (end, regionEnd (region));
    return end;
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
