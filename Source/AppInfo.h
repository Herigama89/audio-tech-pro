#pragma once

#include <JuceHeader.h>

/**
    The product's name in one place, so renaming the app is a one-line change.
    (Internal code still uses the namespace `riff`, the project's original
    working title; users never see it.)
*/
namespace riff::app
{
    inline const juce::String name        { "Audio Tech Pro" };
    inline const juce::String shortName   { "ATP" };
    inline const juce::String synthName   { "Tech Synth" };
    inline const juce::String effectsName { "Audio Tech Pro effects" };

    inline const juce::String projectExtension    { ".atp" };
    inline const juce::String oldProjectExtension { ".riff" };   // still opens
}
