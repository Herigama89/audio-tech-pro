#include "AutomationWriter.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    constexpr double minStep = 0.02;   // seconds between written points
    constexpr double glide = 0.05;     // back to what was there after a pass

    bool isWritable (const juce::String& param) { return param == model::volumeParam || param == model::panParam; }
}

AutomationWriter::AutomationWriter (AudioEngine& e, ProjectPlayer& p, juce::UndoManager& um)
    : engine (e), player (p), undoManager (um)
{
}

AutomationWriter::~AutomationWriter()
{
    if (currentWriter == this)
        currentWriter = nullptr;
}

AutomationWriter* AutomationWriter::current() { return currentWriter; }

void AutomationWriter::setProject (juce::ValueTree newProject)
{
    for (const auto& pass : passes)
        setOverride (pass, false);
    passes.clear();
    project = newProject;
}

double AutomationWriter::now() const   { return clock ? clock() : engine.getPlayheadSeconds(); }
bool AutomationWriter::isPlaying() const { return clock ? playing : engine.isPlaying(); }

AutomationWriter::Pass* AutomationWriter::find (const juce::String& trackId, const juce::String& param)
{
    for (auto& p : passes)
        if (p.trackId == trackId && p.param == param)
            return &p;
    return nullptr;
}

AutomationWriter::Pass& AutomationWriter::startPass (const juce::String& trackId, const juce::String& param, double value)
{
    Pass pass;
    pass.trackId = trackId;
    pass.param = param;
    pass.start = pass.lastTime = now();
    pass.value = value;
    passes.push_back (pass);
    auto& added = passes.back();
    addPoint (added, added.start, true);
    setOverride (added, true);
    return added;
}

void AutomationWriter::addPoint (Pass& pass, double time, bool force)
{
    // The playhead jumped back (a cycle): what was written so far is one pass, the rest another.
    if (time < pass.lastTime - 0.01)
    {
        commit (pass);
        pass.points.clear();
        pass.start = time;
        force = true;
    }

    if (force || time - pass.lastTime >= minStep)
    {
        pass.points.emplace_back (time, pass.value);
        pass.lastTime = time;
    }
}

void AutomationWriter::setOverride (const Pass& pass, bool on)
{
    const auto slot = player.slotOf (pass.trackId);
    const bool pan = pass.param == model::panParam;
    if (on)
        engine.setAutomationOverride (slot, pan, (float) pass.value);
    else
        engine.clearAutomationOverride (slot, pan);
}

bool AutomationWriter::begin (const juce::String& trackId, const juce::String& param, double value)
{
    const auto track = model::findById (project, trackId);
    if (! track.isValid() || ! isWritable (param) || ! isPlaying() || ! writes (modeOf (track)))
        return false;

    auto* pass = find (trackId, param);
    if (pass == nullptr)
        pass = &startPass (trackId, param, value);
    pass->touching = true;
    pass->value = value;
    addPoint (*pass, now(), false);
    setOverride (*pass, true);
    return true;
}

bool AutomationWriter::move (const juce::String& trackId, const juce::String& param, double value)
{
    auto* pass = find (trackId, param);
    if (pass == nullptr)
        return begin (trackId, param, value);   // e.g. a scroll wheel, with no grab first

    pass->value = value;
    addPoint (*pass, now(), false);
    setOverride (*pass, true);
    return true;
}

void AutomationWriter::end (const juce::String& trackId, const juce::String& param)
{
    auto* pass = find (trackId, param);
    if (pass == nullptr)
        return;

    pass->touching = false;
    const auto track = model::findById (project, trackId);
    if (modeOf (track) != "touch")
        return;   // Latch and Write hold the value until playback stops

    addPoint (*pass, now(), true);
    const auto finished = *pass;
    passes.erase (passes.begin() + (pass - passes.data()));
    commit (finished);
    setOverride (finished, false);
}

void AutomationWriter::tick()
{
    if (! isPlaying())
        return;

    const auto t = now();
    for (auto& pass : passes)
        addPoint (pass, t, false);
}

void AutomationWriter::transportChanged (bool nowPlaying)
{
    if (nowPlaying == playing)
        return;
    playing = nowPlaying;

    if (nowPlaying)
    {
        // Write mode writes every track's volume and pan from the start.
        for (const auto& track : project)
            if (track.hasType (ids::TRACK) && modeOf (track) == "write")
                for (const auto& param : { model::volumeParam, model::panParam })
                    if (find (track[ids::id].toString(), param) == nullptr)
                        startPass (track[ids::id].toString(), param, displayedValue (track, param, now()));
        return;
    }

    // Stopped: everything still being written goes into the project.
    auto finished = std::move (passes);
    passes.clear();
    juce::StringArray wroteInWriteMode;
    for (auto& pass : finished)
    {
        commit (pass);
        if (modeOf (model::findById (project, pass.trackId)) == "write")
            wroteInWriteMode.addIfNotAlreadyThere (pass.trackId);
    }

    player.rebuildNow();
    for (const auto& pass : finished)
        setOverride (pass, false);

    // As in Logic, Write turns into Latch once a pass is done (so the next play doesn't erase it).
    for (const auto& id : wroteInWriteMode)
        model::findById (project, id).setProperty (ids::automationMode, "latch", nullptr);
}

void AutomationWriter::commit (const Pass& pass)
{
    if (pass.points.empty())
        return;

    auto track = model::findById (project, pass.trackId);
    if (! track.isValid())
        return;

    const bool volume = pass.param == model::volumeParam;
    const double staticValue = volume ? (double) track.getProperty (ids::volume, 1.0) : (double) track.getProperty (ids::pan, 0.0);

    undoManager.beginNewTransaction ("Write automation");
    auto lane = model::laneFor (project, pass.trackId, pass.param, true);

    const auto start = pass.points.front().first;
    const auto end = pass.points.back().first;
    const auto before = model::valueAt (lane, start, staticValue);
    const auto after = model::valueAt (lane, end + glide, staticValue);

    // What was there during the pass goes.
    juce::Array<juce::ValueTree> old;
    for (const auto& p : lane)
        if ((double) p[ids::time] >= start - 1.0e-6 && (double) p[ids::time] <= end + glide + 1.0e-6)
            old.add (p);
    for (auto& p : old)
        model::removePoint (p, &undoManager);

    // Before the pass: what was there, up to just before it.
    if (start > 0.01)
        model::addPoint (lane, start - 0.005, before, &undoManager);

    // The pass, leaving out points in the middle of a flat stretch.
    for (size_t i = 0; i < pass.points.size(); ++i)
    {
        const auto& [time, value] = pass.points[i];
        const bool flat = i > 0 && i + 1 < pass.points.size()
                          && std::abs (pass.points[i - 1].second - value) < 1.0e-5
                          && std::abs (pass.points[i + 1].second - value) < 1.0e-5;
        if (! flat)
            model::addPoint (lane, time, value, &undoManager);
    }

    // After it: back to what was there.
    model::addPoint (lane, end + glide, after, &undoManager);
}

std::optional<double> AutomationWriter::liveValue (const juce::String& trackId, const juce::String& param) const
{
    for (const auto& p : passes)
        if (p.trackId == trackId && p.param == param)
            return p.value;
    return std::nullopt;
}

double AutomationWriter::displayedValue (const juce::ValueTree& track, const juce::String& param, double time) const
{
    const auto staticValue = param == model::volumeParam ? (double) track.getProperty (ids::volume, 1.0)
                                                         : (double) track.getProperty (ids::pan, 0.0);
    if (auto live = liveValue (track[ids::id].toString(), param))
        return *live;
    if (modeOf (track) == "off")
        return staticValue;
    const auto lane = model::laneFor (project, track[ids::id].toString(), param, false);
    return lane.isValid() && lane.getNumChildren() > 0 ? model::valueAt (lane, time, staticValue) : staticValue;
}

} // namespace riff
