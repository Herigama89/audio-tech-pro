#include "Project.h"

namespace riff::model
{

namespace
{
    // Track colours, cycled as tracks are added.
    const juce::Colour palette[] = {
        juce::Colour (0xff4aa3ff), juce::Colour (0xff3ddc84), juce::Colour (0xffffc043),
        juce::Colour (0xffff7a59), juce::Colour (0xffb38cff), juce::Colour (0xff36d1dc),
        juce::Colour (0xffff5fa2), juce::Colour (0xffa3d977),
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

        if ((tree.hasType (ids::TRACK) || tree.hasType (ids::REGION) || tree.hasType (ids::TAKE)) && ! tree.hasProperty (ids::id))
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
    return project;
}

juce::ValueTree addTrack (juce::ValueTree project, const juce::String& name, juce::UndoManager* um, int index)
{
    jassert (project.hasType (ids::PROJECT));

    const auto colour = palette[(size_t) project.getNumChildren() % std::size (palette)];

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

    project.addChild (track, index, um);
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
juce::ValueTree findById (const juce::ValueTree& project, const juce::String& id)
{
    if (id.isEmpty())
        return {};

    for (const auto& track : project)
    {
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
        error = "This isn't a Riff project file.";
        return {};
    }
    if (version > schemaVersion)
    {
        error = "This project was made with a newer version of Riff.";
        return {};
    }

    auto tree = varToTree (parsed.getProperty ("project", {}), projectFile.getParentDirectory(), error);

    if (tree.isValid() && ! tree.hasType (ids::PROJECT))
    {
        error = "The project file has no project in it.";
        return {};
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
