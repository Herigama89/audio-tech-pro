#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "engine/AggregateDevice.h"
#include "engine/AudioEngine.h"

using namespace riff;

TEST_CASE ("Combined interfaces: channel order, names and the saved setup", "[devices]")
{
    juce::Array<aggregate::Interface> interfaces;
    interfaces.add ({ "ssl-uid", "SSL 2 MKII", 2, 2, true });
    interfaces.add ({ "quantum-uid", "Quantum HD 2", 4, 6, true });

    aggregate::Setup setup;
    setup.uids = { "ssl-uid", "quantum-uid" };
    setup.clockUid = "quantum-uid";
    CHECK (setup.isActive());

    // The clock comes first, so its channels are 1..n.
    CHECK (setup.orderedUids() == juce::StringArray { "quantum-uid", "ssl-uid" });
    const auto inputs = aggregate::channelLabels (setup, interfaces, true);
    REQUIRE (inputs.size() == 6);
    CHECK (inputs[0] == "Quantum HD 2 1");
    CHECK (inputs[3] == "Quantum HD 2 4");
    CHECK (inputs[4] == "SSL 2 MKII 1");
    CHECK (aggregate::channelLabels (setup, interfaces, false).size() == 8);

    const auto back = aggregate::Setup::fromString (setup.toString());
    CHECK (back.uids == setup.uids);
    CHECK (back.clockUid == "quantum-uid");
    CHECK (back.driftCorrection);

    // An unplugged interface is reported; a one-interface setup isn't a combination.
    interfaces.remove (0);
    CHECK (aggregate::missingInterfaces (setup, interfaces) == juce::StringArray { "ssl-uid" });
    CHECK_FALSE (aggregate::Setup::fromString ("").isActive());
    CHECK (aggregate::Setup::fromString (R"({"interfaces":["a"],"clock":"zzz"})").clockUid == "a");
}

TEST_CASE ("The engine opens no device without one and keeps channel labels per device", "[devices][engine]")
{
    AudioEngine engine;
    engine.setInputChannelLabels (aggregate::deviceName, { "Quantum HD 2 1", "Quantum HD 2 2" });
    CHECK (engine.getActiveInputChannelNames().isEmpty());   // no device open in tests
}
