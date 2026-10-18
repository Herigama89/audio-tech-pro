#pragma once

#include <JuceHeader.h>
#include "model/Generator.h"

namespace riff
{

/**
    The MIDI Generator window: choose a genre, key, length and parts, then
    Generate. "Try another" replaces the last result with a new take.
    Writes MIDI only; you play it with your own instruments.
*/
class GeneratorPanel final : public juce::Component
{
public:
    struct Request
    {
        generator::Settings settings;
        bool useGenreTempo = true;
        bool replaceLast = false;
        bool replaceGenerated = false;   // the new take goes into the parts' tracks generated before, where their regions were
    };
    inline static bool replaceByDefault = false;   // (remembered while the app runs)

    GeneratorPanel (const generator::Settings& initial, bool useGenreTempo, std::function<void (const Request&)> onGenerate);

    /** The settings shown, with a fresh seed each time it's asked for a new take. */
    generator::Settings currentSettings() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void genreChanged();
    void send (bool replaceLast);

    juce::Label genreCaption { {}, "Genre" }, keyCaption { {}, "Key" }, barsCaption { {}, "Length" },
                partsCaption { {}, "Parts" }, variationCaption { {}, "Variation" }, about, help;
    juce::ComboBox genreBox, keyBox, scaleBox, barsBox;
    juce::ToggleButton drumsButton { "Drums" }, bassButton { "Bass" }, chordsButton { "Chords" }, melodyButton { "Melody" };
    juce::Slider variationSlider;
    juce::ToggleButton tempoButton;
    juce::ToggleButton replaceButton { "Replace what was generated before (same tracks, same place)" };
    juce::TextButton generateButton { "Generate" }, anotherButton { "Try another" };

    juce::uint32 seed = 1;
    std::function<void (const Request&)> onGenerate;
};

} // namespace riff
