#pragma once

#include "../../model/NemixState.h"

/**
    Nemix's factory sounds: each with a name, what kind of sound it is (Bass, Lead, Pad...) and the styles of
    music it suits, plus the sounds you save yourself (kept as text files in your Nemix folder).
*/
namespace riff::nemix
{
    struct Preset
    {
        juce::String name, category;
        juce::StringArray genres;
        nemixstate::Sound sound;
        bool user = false;
        bool needsLibrary = false;   // (uses a recorded instrument from the Nemix Library)
        juce::File file {};
    };

    const std::vector<Preset>& factoryPresets();
    const juce::StringArray& presetCategories();
    const juce::StringArray& presetGenres();

    /** Your own sounds: the folder, reading them, saving one. */
    juce::File userPresetFolder();
    std::vector<Preset> userPresets();
    bool saveUserPreset (const juce::String& name, const juce::String& category, const nemixstate::Sound& sound);
}
