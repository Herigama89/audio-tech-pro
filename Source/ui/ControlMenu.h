#pragma once

#include <JuceHeader.h>
#include "../model/Project.h"
#include "../engine/AutomationWriter.h"
#include "RoutingMenu.h"

/**
    The menu you get by right-clicking (or Control-clicking) a fader or a knob,
    as in Logic: show this control's automation, put a point at the playhead,
    delete its automation, the track's automation mode, back to the default,
    and MIDI Learn. Used by the mixer strips, the Inspector, the track headers,
    the built-in effects and every parameter of a VST3, AU or CLAP plugin.
*/
namespace riff::controlmenu
{

struct Target
{
    juce::ValueTree project;
    juce::String owner;                   // the track id
    juce::String param;                   // automation key ("volume", "pan", "insert:<id>:<param>"); empty: not automatable
    juce::String name;                    // shown at the top ("Volume", "Gain")
    juce::String learnParam;              // what MIDI Learn drives; empty: same as param; "-": no learn
    std::function<double()> value;        // the value a new automation point gets
    std::function<void()> reset;          // back to the default (empty: no item)
    std::function<void (juce::PopupMenu&)> extra;   // more items at the end
    juce::UndoManager* um = nullptr;
};

/** Removes a controller assignment that drives `param` on `owner`; returns whether there was one. */
inline bool deleteAssignments (juce::ValueTree project, const juce::String& owner, const juce::String& param, bool justCheck = false)
{
    auto assignments = project.getChildWithName (ids::ASSIGNMENTS);
    bool found = false;
    for (int i = assignments.getNumChildren(); --i >= 0;)
    {
        const auto a = assignments.getChild (i);
        if (a[ids::target].toString() == owner && a[ids::param].toString() == param)
        {
            found = true;
            if (! justCheck)
                assignments.removeChild (i, nullptr);
        }
    }
    return found;
}

/** Fills `menu` with the control's items (handy for a submenu per plugin parameter). */
inline void addItems (juce::PopupMenu& menu, const Target& t)
{
    auto track = model::findById (t.project, t.owner);
    auto* um = t.um;

    if (t.param.isNotEmpty() && track.isValid())
    {
        const auto lane = model::laneFor (t.project, t.owner, t.param, false);
        const bool hasPoints = lane.isValid() && lane.getNumChildren() > 0;
        const bool shown = (bool) track[ids::showAutomation] && track[ids::automationParam].toString() == t.param;

        menu.addItem (shown ? "Hide Automation" : "Show Automation", [track, t]() mutable
        {
            const bool isShown = (bool) track[ids::showAutomation] && track[ids::automationParam].toString() == t.param;
            if (auto lane2 = model::laneFor (t.project, t.owner, t.param, ! isShown); lane2.isValid() && t.name.isNotEmpty())
                lane2.setProperty (ids::name, t.name, nullptr);
            track.setProperty (ids::automationParam, t.param, nullptr);
            track.setProperty (ids::showAutomation, ! isShown, nullptr);
        });

        menu.addItem ("Create Automation Point at Playhead", [track, t, um]() mutable
        {
            auto lane2 = model::laneFor (t.project, t.owner, t.param, true);
            if (t.name.isNotEmpty())
                lane2.setProperty (ids::name, t.name, nullptr);
            const auto time = AutomationWriter::current() != nullptr ? AutomationWriter::current()->timeNow() : 0.0;
            const auto v = t.value ? t.value() : model::rangeOf (t.project, t.param).defaultValue;
            if (um != nullptr) um->beginNewTransaction ("Create automation");
            // A lane that was empty starts with the value it has now, from the beginning.
            if (lane2.getNumChildren() == 0 && time > 0.0)
                model::addPoint (lane2, 0.0, v, um);
            model::addPoint (lane2, time, v, um);
            track.setProperty (ids::automationParam, t.param, nullptr);
            track.setProperty (ids::showAutomation, true, nullptr);
        });

        menu.addItem ("Delete Automation", hasPoints, false, [t, um]() mutable
        {
            if (um != nullptr) um->beginNewTransaction ("Delete automation");
            model::clearAutomation (t.project, t.owner, t.param, um);
        });

        juce::PopupMenu modes;
        const auto mode = AutomationWriter::modeOf (track);
        for (auto [m, label] : { std::pair { "off", "Off" }, std::pair { "read", "Read" }, std::pair { "touch", "Touch" },
                                 std::pair { "latch", "Latch" }, std::pair { "write", "Write" } })
            modes.addItem (label, true, mode == m, [track, value = juce::String (m)]() mutable { track.setProperty (ids::automationMode, value, nullptr); });
        menu.addSubMenu ("Automation Mode: " + mode.substring (0, 1).toUpperCase() + mode.substring (1), modes);
        menu.addSeparator();
    }

    if (t.reset)
        menu.addItem ("Set to Default", t.reset);

    const auto learn = t.learnParam.isNotEmpty() ? t.learnParam : t.param;
    if (learn.isNotEmpty() && learn != "-" && track.isValid() && routing::learnController)
    {
        menu.addItem ("Learn MIDI Controller", [track, learn] { routing::learnController (track, learn); });
        const bool assigned = deleteAssignments (t.project, t.owner, learn, true);
        menu.addItem ("Delete Controller Assignment", assigned, false, [t, learn]() mutable { deleteAssignments (t.project, t.owner, learn); });
    }

    if (t.extra)
        t.extra (menu);
}

inline void show (juce::Component& target, const Target& t)
{
    juce::PopupMenu menu;
    if (t.name.isNotEmpty())
        menu.addSectionHeader (t.name);
    addItems (menu, t);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition());
}

} // namespace riff::controlmenu
