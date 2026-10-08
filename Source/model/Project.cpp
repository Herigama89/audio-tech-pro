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

            if (tree.hasType (ids::REGION) && name == ids::file)
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

            if (tree.hasType (ids::REGION) && prop.name == ids::file)
            {
                const auto path = value.toString();
                value = juce::File::isAbsolutePath (path) ? path
                                                          : projectDir.getChildFile (path).getFullPathName();
            }

            tree.setProperty (prop.name, value, nullptr);
        }

        if ((tree.hasType (ids::TRACK) || tree.hasType (ids::REGION)) && ! tree.hasProperty (ids::id))
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
}

//==============================================================================
juce::ValueTree createProject (const juce::String& name)
{
    juce::ValueTree project { ids::PROJECT };
    project.setProperty (ids::name, name, nullptr);
    project.setProperty (ids::tempo, 120.0, nullptr);
    project.setProperty (ids::timeSigNumerator, 4, nullptr);
    project.setProperty (ids::masterGain, 0.8, nullptr);
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
    const double offset = region[ids::offset];
    const double end    = start + (double) region[ids::length];

    // Can't reveal audio from before the start of the file, or go before 0.
    newStart = juce::jlimit (juce::jmax (0.0, start - offset), end - minRegionLength, newStart);

    region.setProperty (ids::offset, offset + (newStart - start), um);
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
    const double offset = region[ids::offset];
    const double end    = regionEnd (region);

    if (time <= start + minRegionLength || time >= end - minRegionLength)
        return {};

    auto parent = region.getParent();
    auto right = region.createCopy();
    right.setProperty (ids::id, newId(), nullptr);
    right.setProperty (ids::start, time, nullptr);
    right.setProperty (ids::offset, offset + (time - start), nullptr);
    right.setProperty (ids::length, end - time, nullptr);

    region.setProperty (ids::length, time - start, um);
    parent.addChild (right, parent.indexOf (region) + 1, um);
    return right;
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
