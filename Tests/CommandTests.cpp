#include <catch2/catch_test_macros.hpp>
#include "ui/Commands.h"
#include "model/Project.h"

#include <set>

using namespace riff;

TEST_CASE ("Every command has a name, a menu and a unique id", "[commands]")
{
    std::set<juce::CommandID> seen;

    for (const auto& c : commands::all())
    {
        CHECK (juce::String (c.name).isNotEmpty());
        CHECK (juce::String (c.category).isNotEmpty());
        CHECK (juce::String (c.description).isNotEmpty());
        CHECK (seen.insert (c.id).second);
        CHECK (commands::find (c.id) == &c);
    }

    CHECK (commands::find (0) == nullptr);
}

TEST_CASE ("The Logic Pro preset never gives one key to two commands", "[commands]")
{
    const auto preset = commands::presetNames()[0];
    REQUIRE (preset == "Logic Pro");

    std::map<juce::String, juce::String> owner;

    for (const auto& c : commands::all())
        for (const auto& key : commands::keysFor (preset, c.id))
        {
            const auto description = key.getTextDescription();
            const auto existing = owner.find (description);
            INFO (description << " is on " << (existing != owner.end() ? existing->second : juce::String()) << " and " << c.name);
            CHECK (existing == owner.end());
            owner[description] = c.name;
        }
}

TEST_CASE ("The Logic Pro preset has Logic's main keys", "[commands]")
{
    const auto preset = commands::presetNames()[0];
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto opt = juce::ModifierKeys::altModifier;

    auto has = [&] (juce::CommandID id, juce::KeyPress key) { return commands::keysFor (preset, id).contains (key); };

    CHECK (has (commands::play,               juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (has (commands::record,             juce::KeyPress ('r')));
    CHECK (has (commands::goToStart,          juce::KeyPress (juce::KeyPress::returnKey)));
    CHECK (has (commands::toggleCycle,        juce::KeyPress ('c')));
    CHECK (has (commands::toggleMetronome,    juce::KeyPress ('k')));
    CHECK (has (commands::showMixer,          juce::KeyPress ('x')));
    CHECK (has (commands::showPianoRoll,      juce::KeyPress ('p')));
    CHECK (has (commands::showAutomation,     juce::KeyPress ('a')));
    CHECK (has (commands::quantize,           juce::KeyPress ('q')));
    CHECK (has (commands::rewindBar,          juce::KeyPress (',')));
    CHECK (has (commands::forwardBar,         juce::KeyPress ('.')));
    CHECK (has (commands::splitAtPlayhead,    juce::KeyPress ('t', cmd, 0)));
    CHECK (has (commands::exportSong,         juce::KeyPress ('b', cmd, 0)));
    CHECK (has (commands::setLocatorsBySelection, juce::KeyPress ('u', cmd, 0)));
    CHECK (has (commands::newAudioTrack,      juce::KeyPress ('a', opt | cmd, 0)));
    CHECK (has (commands::newInstrumentTrack, juce::KeyPress ('s', opt | cmd, 0)));
    CHECK (has (commands::keyCommands,        juce::KeyPress ('k', opt, 0)));
}

TEST_CASE ("Clearing automation removes a lane, undoably", "[automation]")
{
    juce::UndoManager um;
    auto project = model::createProject();
    auto track = model::addTrack (project, "Vox", &um);
    const auto owner = track[ids::id].toString();

    auto volume = model::laneFor (project, owner, model::volumeParam, true);
    model::addPoint (volume, 0.0, 0.5, &um);
    model::addPoint (volume, 2.0, 1.0, &um);
    auto pan = model::laneFor (project, owner, model::panParam, true);
    model::addPoint (pan, 1.0, -1.0, &um);

    SECTION ("one lane")
    {
        um.beginNewTransaction();
        CHECK (model::clearAutomation (project, owner, model::volumeParam, &um));
        CHECK_FALSE (model::laneFor (project, owner, model::volumeParam, false).isValid());
        CHECK (model::laneFor (project, owner, model::panParam, false).isValid());
        CHECK_FALSE (model::clearAutomation (project, owner, model::volumeParam, &um));

        um.undo();
        CHECK (model::laneFor (project, owner, model::volumeParam, false).getNumChildren() == 2);
    }

    SECTION ("every lane on the track, and only that track")
    {
        auto other = model::addTrack (project, "Gtr", &um);
        auto otherLane = model::laneFor (project, other[ids::id].toString(), model::volumeParam, true);
        model::addPoint (otherLane, 0.0, 0.3, &um);

        um.beginNewTransaction();
        CHECK (model::clearAllAutomation (project, owner, &um) == 2);
        CHECK_FALSE (model::laneFor (project, owner, model::panParam, false).isValid());
        CHECK (model::laneFor (project, other[ids::id].toString(), model::volumeParam, false).getNumChildren() == 1);

        // The mixer node stays the project's last child.
        CHECK (project.getChild (project.getNumChildren() - 1).hasType (ids::MIXER));

        um.undo();
        CHECK (model::laneFor (project, owner, model::panParam, false).getNumChildren() == 1);
    }
}
