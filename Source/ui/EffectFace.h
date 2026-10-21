#pragma once

#include <JuceHeader.h>
#include "../engine/BuiltInEffects.h"

/**
    How the stock effects' windows look (in the manner of Logic's plug-in windows, with our own
    artwork): a dark face with a title bar, an accent colour per family, the controls in labelled
    sections, and a display drawn from the knobs - the EQ curve, a compressor's transfer curve, a
    delay's echoes, a reverb's decay, an LFO's shape or a distortion's curve.
*/
namespace riff::effectface
{
    /** Each family's accent colour (EQ blue, Dynamics orange, Delay teal, Reverb purple...). */
    juce::Colour accentFor (const juce::String& category);

    struct Section
    {
        juce::String title;
        int count = 0;
        bool vertical = false;   // (an EQ band: frequency, gain and Q stacked)
        std::vector<juce::String> ids {};   // (named parameters, in place of the next count in order)
    };

    /** Each section's parameter indices (by ids, or the next ones in order); any left over make a last section. */
    std::vector<std::pair<Section, std::vector<int>>> resolveSections (const EffectInfo& info);

    /** The effect's controls in labelled groups, in parameter order. */
    std::vector<Section> sectionsFor (const EffectInfo& info);

    /** True if the effect has a display above its knobs. */
    bool hasDisplay (const EffectInfo& info);

    /** Draws the display for these parameter values. */
    void drawDisplay (juce::Graphics& g, juce::Rectangle<float> area, const EffectInfo& info, const float* values, juce::Colour accent);

    /** The knobs and value boxes: dark knobs with a ring in the accent colour (from the centre for +/- controls). */
    class FaceLook final : public juce::LookAndFeel_V4
    {
    public:
        explicit FaceLook (juce::Colour accentColour);
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Colour accent;
    };
}
