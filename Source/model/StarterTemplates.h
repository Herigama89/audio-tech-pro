#pragma once

#include <JuceHeader.h>

/**
    The New Project window's ready-made starts (Pro Tools' and Logic's templates): an empty project, one audio or
    instrument track, a vocal session, beat and song set-ups, a band's mics, a podcast, a mix and a master - each
    with its tracks, buses and effects. The user's own templates are project files in the templates folder.
*/
namespace riff::starters
{
    struct Starter
    {
        juce::String id, name, description, category;
        std::vector<juce::Colour> lanes;   // (the card's little picture: a bar per track, in its colour)
    };

    /** Every starter, in the window's order; categories in order of first appearance. */
    const std::vector<Starter>& all();
    juce::StringArray categories();

    /** A new project made from a starter (an unknown id: an empty project). */
    juce::ValueTree build (const juce::String& id, const juce::String& projectName);
}
