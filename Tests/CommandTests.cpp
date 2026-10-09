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

TEST_CASE ("No preset gives one key to two commands", "[commands]")
{
    REQUIRE (commands::presetNames() == juce::StringArray { "Logic Pro", "Pro Tools", "Ableton Live", "FL Studio", "Cubase" });

    for (const auto& preset : commands::presetNames())
    {
    INFO (preset);
    std::map<juce::String, juce::String> owner;
    int assigned = 0;

    for (const auto& c : commands::all())
        for (const auto& key : commands::keysFor (preset, c.id))
        {
            const auto description = key.getTextDescription();
            const auto existing = owner.find (description);
            INFO (description << " is on " << (existing != owner.end() ? existing->second : juce::String()) << " and " << c.name);
            CHECK (existing == owner.end());
            owner[description] = c.name;
            ++assigned;
        }

    CHECK (assigned >= 20);   // every preset covers the everyday commands
    CHECK (commands::keysFor (preset, commands::play).contains (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (commands::keysFor (preset, commands::keyCommands).size() == 1);   // the Key Commands window is always one key away
    }
}

TEST_CASE ("Each preset has its DAW's signature keys and stop behaviour", "[commands]")
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto opt = juce::ModifierKeys::altModifier;
    auto has = [] (const char* preset, juce::CommandID id, juce::KeyPress key) { return commands::keysFor (preset, id).contains (key); };

    // Pro Tools
    CHECK (has ("Pro Tools", commands::splitAtPlayhead, juce::KeyPress ('e', cmd, 0)));
    CHECK (has ("Pro Tools", commands::showMixer, juce::KeyPress ('=', cmd, 0)));
    CHECK (has ("Pro Tools", commands::toggleCycle, juce::KeyPress ('l', shift | cmd, 0)));
    CHECK (has ("Pro Tools", commands::exportSong, juce::KeyPress ('b', opt | cmd, 0)));
    CHECK (has ("Pro Tools", commands::record, juce::KeyPress (juce::KeyPress::F12Key)));

    // Ableton Live
    CHECK (has ("Ableton Live", commands::newAudioTrack, juce::KeyPress ('t', cmd, 0)));
    CHECK (has ("Ableton Live", commands::newInstrumentTrack, juce::KeyPress ('t', shift | cmd, 0)));
    CHECK (has ("Ableton Live", commands::record, juce::KeyPress (juce::KeyPress::F9Key)));
    CHECK (has ("Ableton Live", commands::quantize, juce::KeyPress ('u', cmd, 0)));
    CHECK (has ("Ableton Live", commands::exportSong, juce::KeyPress ('r', shift | cmd, 0)));

    // FL Studio
    CHECK (has ("FL Studio", commands::showMixer, juce::KeyPress (juce::KeyPress::F9Key)));
    CHECK (has ("FL Studio", commands::showPianoRoll, juce::KeyPress (juce::KeyPress::F7Key)));
    CHECK (has ("FL Studio", commands::record, juce::KeyPress ('r')));
    CHECK (has ("FL Studio", commands::exportSong, juce::KeyPress ('r', cmd, 0)));

    // Cubase
    CHECK (has ("Cubase", commands::showMixer, juce::KeyPress (juce::KeyPress::F3Key)));
    CHECK (has ("Cubase", commands::splitAtPlayhead, juce::KeyPress ('x', opt, 0)));
    CHECK (has ("Cubase", commands::toggleMetronome, juce::KeyPress ('c')));
    CHECK (has ("Cubase", commands::setLocatorsBySelection, juce::KeyPress ('p')));
    CHECK (has ("Cubase", commands::record, juce::KeyPress (juce::KeyPress::numberPadMultiply)));

    // Stop behaviour
    CHECK_FALSE (commands::stopReturnsToStart ("Logic Pro"));
    CHECK_FALSE (commands::stopReturnsToStart ("Cubase"));
    CHECK (commands::stopReturnsToStart ("Pro Tools"));
    CHECK (commands::stopReturnsToStart ("Ableton Live"));
    CHECK (commands::stopReturnsToStart ("FL Studio"));
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

#include "ui/PluginNames.h"

TEST_CASE ("Plugin names are shortened to fit a slot, as in Logic", "[ui][names]")
{
    using riff::pluginnames::abbreviate;
    auto fitsIn = [] (int chars) { return [chars] (const juce::String& s) { return s.length() <= chars; }; };

    CHECK (abbreviate ("Compressor", fitsIn (20)) == "Compressor");                  // fits: unchanged
    CHECK (abbreviate ("FabFilter Pro-Q 3", fitsIn (10)) == "Pro-Q 3");               // the maker's name goes first
    CHECK (abbreviate ("ChromaVerb (Stereo)", fitsIn (10)) == "ChromaVerb");          // then bracketed extras
    CHECK (abbreviate ("Channel EQ", fitsIn (8)) == "Chnnl EQ");                      // then vowels inside words
    CHECK (abbreviate ("Space Designer", fitsIn (8)).length() <= 8);
    CHECK (abbreviate ("Space Designer", fitsIn (8)).startsWith ("Spc"));
}
