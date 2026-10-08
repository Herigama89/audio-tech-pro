#include <catch2/catch_session.hpp>
#include <JuceHeader.h>

// JUCE needs its message manager alive for the engine's timers and async
// updates, and it must be shut down before static destruction.
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
