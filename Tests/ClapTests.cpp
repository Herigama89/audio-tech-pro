#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <JuceHeader.h>
#include "engine/ClapFormat.h"
#include "engine/InstrumentRack.h"

using namespace riff;
using Catch::Matchers::WithinAbs;

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> load (ClapPluginFormat& format, const juce::String& name)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, TEST_CLAP_PATH);
        for (auto* d : found)
            if (d->name == name)
            {
                juce::String error;
                auto p = format.createInstanceFromDescription (*d, 48000.0, 256, error);
                INFO (error);
                return p;
            }
        return nullptr;
    }
}

TEST_CASE ("CLAP plugins are found in a .clap file", "[clap]")
{
    ClapPluginFormat format;
    CHECK (format.fileMightContainThisPluginType (TEST_CLAP_PATH));

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, TEST_CLAP_PATH);
    REQUIRE (found.size() == 2);
    CHECK (found[0]->name == "Test Gain");
    CHECK (found[0]->manufacturerName == "Test Vendor");
    CHECK (found[0]->version == "1.2.0");
    CHECK (found[0]->pluginFormatName == "CLAP");
    CHECK (! found[0]->isInstrument);
    CHECK (found[0]->numInputChannels == 2);
    CHECK (found[1]->name == "Test Synth");
    CHECK (found[1]->isInstrument);
    CHECK (found[1]->numInputChannels == 0);
    CHECK (found[1]->numOutputChannels == 2);
    CHECK (found[0]->uniqueId != found[1]->uniqueId);

    // The folder scan finds it too.
    const auto folder = juce::File (TEST_CLAP_PATH).getParentDirectory();
    CHECK (format.searchPathsForPlugins (juce::FileSearchPath (folder.getFullPathName()), false).contains (TEST_CLAP_PATH));

    // And the app's plugin list knows the format.
    InstrumentRack rack;
    bool hasClap = false;
    for (auto* f : rack.getFormatManager().getFormats())
        hasClap = hasClap || f->getName() == "CLAP";
    CHECK (hasClap);
}

TEST_CASE ("A CLAP effect processes audio, with parameters, latency and state", "[clap]")
{
    ClapPluginFormat format;
    auto plugin = load (format, "Test Gain");
    REQUIRE (plugin != nullptr);
    plugin->prepareToPlay (48000.0, 256);
    CHECK (plugin->getLatencySamples() == 3);

    auto& params = plugin->getParameters();
    REQUIRE (params.size() == 1);
    auto* gain = params[0];
    CHECK (gain->getName (32) == "Gain");
    CHECK_THAT (gain->getValue(), WithinAbs (0.5, 1e-6));       // 1.0 of 0..2
    CHECK (gain->getText (0.25f, 32) == "x0.50");
    CHECK_THAT (gain->getValueForText ("x1.5"), WithinAbs (0.75, 1e-6));

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    auto run = [&]
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (c, i, 0.5f);
        plugin->processBlock (buffer, midi);
    };
    run();
    CHECK_THAT (buffer.getSample (0, 100), WithinAbs (0.5, 1e-6));

    gain->setValueNotifyingHost (0.25f);   // x0.5
    run();
    CHECK_THAT (buffer.getSample (1, 0), WithinAbs (0.25, 1e-6));

    // State goes out and comes back.
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    CHECK (state.getSize() == sizeof (double));
    gain->setValueNotifyingHost (1.0f);
    run();
    CHECK_THAT (buffer.getSample (0, 0), WithinAbs (1.0, 1e-6));
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (gain->getValue(), WithinAbs (0.25, 1e-6));
    run();
    CHECK_THAT (buffer.getSample (0, 0), WithinAbs (0.25, 1e-6));

    plugin->releaseResources();

    // With no audio running, parameter changes reach the plugin straight away.
    gain->setValueNotifyingHost (0.5f);
    juce::MemoryBlock idle;
    plugin->getStateInformation (idle);
    CHECK_THAT (*static_cast<const double*> (idle.getData()), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("A CLAP instrument plays notes at the right sample", "[clap]")
{
    ClapPluginFormat format;
    auto plugin = load (format, "Test Synth");
    REQUIRE (plugin != nullptr);
    CHECK (plugin->acceptsMidi());
    plugin->prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 10);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 200);
    plugin->processBlock (buffer, midi);

    CHECK (buffer.getSample (0, 9) == 0.0f);
    CHECK_THAT (buffer.getSample (0, 10), WithinAbs (1.0, 1e-6));
    CHECK_THAT (buffer.getSample (1, 199), WithinAbs (1.0, 1e-6));
    CHECK (buffer.getSample (1, 200) == 0.0f);
    plugin->releaseResources();
}

//==============================================================================
// Multi-output instruments (Groove Agent crashed when its extra outputs were switched off).
#include "../Source/engine/PluginBuses.h"
#include "../Source/engine/PluginInstrument.h"

namespace
{
    /** An instrument with a stereo main out and three stereo extra outs, off to start with; it writes to every output channel it has. */
    struct MultiOut final : juce::AudioPluginInstance
    {
        MultiOut()
            : juce::AudioPluginInstance (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                                                          .withOutput ("Aux 1", juce::AudioChannelSet::stereo(), false)
                                                          .withOutput ("Aux 2", juce::AudioChannelSet::stereo(), false)
                                                          .withOutput ("Aux 3", juce::AudioChannelSet::stereo(), false)) {}
        void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "MultiOut"; }
        const juce::String getName() const override { return "MultiOut"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            REQUIRE (b.getNumChannels() >= getTotalNumOutputChannels());
            for (int c = 0; c < getTotalNumOutputChannels(); ++c)
                juce::FloatVectorOperations::fill (b.getWritePointer (c), 0.25f, b.getNumSamples());
        }
        double getTailLengthSeconds() const override { return 0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };
}

TEST_CASE ("A multi-output instrument keeps every output on and plays its main out", "[plugins]")
{
    auto plugin = std::make_unique<MultiOut>();
    auto* raw = plugin.get();
    riff::PluginInstrument instrument (std::move (plugin));
    for (int i = 0; i < raw->getBusCount (false); ++i)
        REQUIRE (raw->getBus (false, i)->isEnabled());
    REQUIRE (raw->getTotalNumOutputChannels() == 8);

    instrument.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    instrument.render (l.data(), r.data(), 256, {});
    REQUIRE_THAT (l[100], WithinAbs (0.25, 1.0e-6));
    REQUIRE_THAT (r[100], WithinAbs (0.25, 1.0e-6));
}
