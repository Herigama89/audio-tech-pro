#pragma once

#include <JuceHeader.h>

/**
    Themes > Faders, Knobs, Buttons and Bus Comp meter: pieces cut from your reference photos
    (Tools/make_look_pieces.py), usable with any skin. Each choice is "the skin's own" (-1)
    or one of the pieces below; the drawing code asks here first.

    Faders (theme::faderStyle):  4 black (Studio White's photo, untinted), 5 black long-throw,
                                 6 black with a red line, 7 silver console cap
    Knobs (theme::knobStyle):    0 classic (drawn), 1 black with a white line, 2 black with a
                                 yellow pointer, 3 yellow cap, 4 red, 5 Studio White's photo knob,
                                 6 Xenon's glass knob
    Buttons (theme::buttonStyle): 0 classic (drawn), 1 square backlit (white), 2 square grey, 3 round lamp
    Meter (theme::meterStyle):   0 classic (drawn), 1 black compression meter, 2 cream VU
*/
namespace riff::looks
{
    juce::StringArray faderNames();    // for faderStyle -1, 0, 1...
    juce::StringArray knobNames();     // for knobStyle -1, 0, 1...
    juce::StringArray buttonNames();
    juce::StringArray meterNames();

    /** A photo fader cap (faderStyle 4..7) centred in `cap` (its height; the width follows the photo). False otherwise. */
    bool drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, float opacity = 1.0f);

    /** A photo cap's width for a height (for the strip's fader). */
    float capWidthForHeight (float height);

    /** A knob of the chosen kind (knobStyle 1..6), turned to the value. False: draw the skin's (or classic) knob. */
    bool drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, float startAngle, float endAngle,
                   bool fromCentre, bool enabled);

    /** A channel key (mute, solo, record, monitor, IN...) of the chosen kind (buttonStyle 1..3): lit in `onColour` when on.
        False: the skin's own key. */
    bool drawButton (juce::Graphics& g, juce::Rectangle<float> area, bool on, juce::Colour onColour, bool over, bool down);

    /** The letter's colour on such a key. */
    juce::Colour buttonTextColour (bool on, juce::Colour onColour);

    /** The bus compressor's meter of the chosen kind (meterStyle 1..2), its needle at `reductionDb`. False: the skin's own. */
    bool drawMeter (juce::Graphics& g, juce::Rectangle<float> area, float reductionDb);

    /** The Dark VU theme's amber VU meter showing a level in VU (0 VU = -18 dBFS; -20..+3), for the strips' VU meters.
        `stretch`: fills the area (as wide as a strip), else kept in proportion. */
    bool drawLevelVu (juce::Graphics& g, juce::Rectangle<float> area, float vu, bool stretch = true);
}
