#include <catch2/catch_session.hpp>
#include <JuceHeader.h>
#include "model/Presets.h"

// JUCE needs its message manager alive for the engine's timers and async
// updates, and it must be shut down before static destruction.
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // (presets and Defaults in a folder of the tests' own, not the user's)
    const auto presetFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atp-test-presets");
    presetFolder.deleteRecursively();
    riff::presets::setRootForTests (presetFolder);
    return Catch::Session().run (argc, argv);
}
