#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <thread>
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

#include "engine/InterfaceControl.h"

namespace
{
    juce::MemoryBlock fixture (const char* name)
    {
        juce::MemoryBlock b;
        juce::File (TEST_DATA_DIR).getChildFile ("ucnet").getChildFile (name).loadFileAsData (b);
        return b;
    }
}

TEST_CASE ("Interface control: the service's frames, state and writes (from real captures)", "[device]")
{
    // The device's state as the service really sends it (a Quantum HD 8, captured by the MIT-licensed reference)
    const auto rx = fixture ("probe-device-rx.bin");
    REQUIRE (rx.getSize() > 100);
    ucnet::Decoder d;
    // (fed a byte at a time: frames split anywhere still come out whole)
    std::vector<ucnet::Message> messages;
    for (size_t i = 0; i < rx.getSize(); ++i)
        for (auto& m : d.feed (static_cast<const char*> (rx.getData()) + i, 1))
            messages.push_back (std::move (m));
    const ucnet::Message* state = nullptr;
    for (const auto& m : messages)
        if (m.code == "ZM")
            state = &m;
    REQUIRE (state != nullptr);
    const auto tree = ucnet::parseState (*state);
    CHECK (tree.getProperty ("id", {}).toString() == "Synchronize");
    std::map<juce::String, juce::var> values, ranges;
    ucnet::flatten (tree, {}, values, ranges);
    CHECK (values.count ("line/ch1/preampgain") == 1);
    CHECK (values.count ("line/ch1/48v") == 1);
    REQUIRE (ranges.count ("line/ch1/preampgain") == 1);
    CHECK ((double) ranges["line/ch1/preampgain"].getProperty ("max", 0) == 75.0);

    // A write, byte for byte as the maker's own app writes one
    const auto written = fixture ("uc-pvwrite.bin");
    const auto ours = ucnet::encode ("PV", ucnet::pvPayload ("global/mixerMode", 0.0f), { 0x6a, 0, 0x69, 0 });
    CHECK (ours == written);
    ucnet::Decoder d2;
    auto pv = d2.feed (written.getData(), written.getSize());
    REQUIRE (pv.size() == 1);
    juce::String path;
    float v = -1.0f;
    REQUIRE (ucnet::parsePv (pv[0], path, v));
    CHECK (path == "global/mixerMode");
    CHECK (v == 0.0f);
}

TEST_CASE ("Interface control: connects to the service, follows it, writes to it", "[device]")
{
    // A stand-in for the service: answers the subscribe with the real state, then takes writes and sends events
    juce::StreamingSocket server;
    int port = 0;
    for (int p = 47100; p < 47200 && port == 0; ++p)
        if (server.createListener (p, "127.0.0.1"))
            port = p;
    REQUIRE (port != 0);
    const auto rx = fixture ("probe-device-rx.bin");
    std::atomic<bool> gotGain { false }, gotSubscribe { false };
    std::atomic<float> gainWritten { -1.0f };
    std::unique_ptr<juce::StreamingSocket> client;
    std::thread service ([&]
    {
        client.reset (server.waitForNextConnection());
        if (client == nullptr) return;
        ucnet::Decoder d;
        char buffer[4096];
        const auto start = juce::Time::getMillisecondCounter();
        bool sentEvent = false;
        while (juce::Time::getMillisecondCounter() - start < 8000 && ! gotGain)
        {
            if (client->waitUntilReady (true, 50) <= 0) continue;
            const auto n = client->read (buffer, sizeof (buffer), false);
            if (n <= 0) break;
            for (const auto& m : d.feed (buffer, (size_t) n))
            {
                if (m.code == "JM" && ucnet::parseJson (m).getProperty ("id", {}).toString() == "Subscribe" && ! gotSubscribe)
                {
                    gotSubscribe = true;
                    client->write (rx.getData(), (int) rx.getSize());
                }
                if (m.code == "PV")
                {
                    juce::String path; float v;
                    if (ucnet::parsePv (m, path, v) && path == "line/ch1/preampgain")
                    {
                        gainWritten = v;
                        gotGain = true;
                    }
                }
            }
            if (gotSubscribe && ! sentEvent)
            {
                // the front panel turns 48 V on: the event the service sends
                const auto e = ucnet::encode ("PV", ucnet::pvPayload ("line/ch1/48v", 1.0f), { 0x69, 0, 0x6a, 0 });
                client->write (e.getData(), (int) e.getSize());
                sentEvent = true;
            }
        }
    });

    InterfaceControl control;
    control.findPorts = [port] { return juce::Array<int> { port }; };
    control.cacheFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atp-test-interface.json");
    control.cacheFile.replaceWithText ("{\"port\": " + juce::String (port) + ", \"session\": 105}");
    control.start();
    for (int i = 0; i < 300 && ! (control.isConnected() && control.preamp (1).phantom); ++i)
        juce::Thread::sleep (10);
    REQUIRE (control.isConnected());
    CHECK (control.preamp (1).present);
    CHECK (control.preamp (1).maxDb == 75.0f);
    CHECK (control.preamp (1).phantom);   // (the event)

    control.setGain (1, 30.0f);
    for (int i = 0; i < 300 && ! gotGain; ++i)
        juce::Thread::sleep (10);
    CHECK (gotGain);
    CHECK_THAT (gainWritten.load(), Catch::Matchers::WithinAbs (0.4, 1.0e-4));   // 30 of 0..75 dB
    control.stop();
    service.join();
    server.close();
}
