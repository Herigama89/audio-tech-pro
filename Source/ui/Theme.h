#pragma once

#include <JuceHeader.h>

/**
    The Audio Tech Pro look: a dark digital console with neon-blue LED-ring
    encoders, blue LED meters, backlit keys and dark fader caps.

    Colours live here; how controls are drawn lives in ConsoleLook.
*/
namespace riff::theme
{
    // The look: a dark charcoal console, like a modern digital mixer, with
    // neon-blue LED rings and meters and backlit keys.

    // --- Dark areas (arrange window, menus, dialogs) --------------------------
    inline const juce::Colour background { 0xff1d1e20 };
    inline const juce::Colour toolbar    { 0xff2b2c2f };   // charcoal
    inline const juce::Colour toolbarDark{ 0xff1f2022 };
    inline const juce::Colour panel      { 0xff2e3033 };
    inline const juce::Colour laneA      { 0xff222326 };
    inline const juce::Colour laneB      { 0xff26272a };
    inline const juce::Colour outline    { 0xff3e4045 };
    inline const juce::Colour gridBar    { 0xff45474c };
    inline const juce::Colour gridBeat   { 0xff2f3134 };
    inline const juce::Colour text       { 0xffe8e9ec };
    inline const juce::Colour dimText    { 0xff9a9ca3 };
    inline const juce::Colour accent     { 0xff2f8cff };   // neon blue
    inline const juce::Colour selection  { 0xff26364d };
    inline const juce::Colour mute       { 0xffe2463f };   // backlit red
    inline const juce::Colour solo       { 0xfff2c94e };   // backlit yellow
    inline const juce::Colour danger     { 0xffe2463f };
    inline const juce::Colour meterGreen { 0xff2f8cff };   // meters are blue (low and mid)
    inline const juce::Colour meterAmber { 0xff7fd3ff };   // ... cyan near the top

    // --- Key and header surfaces (were cream; now dark rubber) ----------------
    inline const juce::Colour cream      { 0xff3b3d41 };
    inline const juce::Colour creamShade { 0xff313336 };
    inline const juce::Colour creamDeep  { 0xff232427 };
    inline const juce::Colour ink        { 0xffe8e9ec };   // text on those surfaces
    inline const juce::Colour inkDim     { 0xff9a9ca3 };

    // --- Highlights (selection, the selected track's number) -------------------
    inline const juce::Colour gold       { 0xff2f8cff };
    inline const juce::Colour goldDark   { 0xff1d5fbf };

    // --- Ruler ---------------------------------------------------------------
    inline const juce::Colour ruler      { 0xff3a3c40 };
    inline const juce::Colour rulerDark  { 0xff2a2b2e };

    // --- Console (mixer) -----------------------------------------------------
    inline const juce::Colour console      { 0xff2f3134 };
    inline const juce::Colour consoleShade { 0xff27282b };
    inline const juce::Colour consoleDark  { 0xff141516 };
    inline const juce::Colour slotDark     { 0xff18191b };   // insert slots, small displays

    // --- Displays ------------------------------------------------------------
    inline const juce::Colour lcd        { 0xff0b0c0e };
    inline const juce::Colour lcdText    { 0xffdfe6f2 };
    inline const juce::Colour lcdDim     { 0xff7c8392 };

    // --- Knobs: the colour of each knob's LED ring ------------------------------
    inline const juce::Colour knobCream  { 0xffd9ecff };   // white-blue
    inline const juce::Colour knobBlue   { 0xff2f8cff };
    inline const juce::Colour knobRed    { 0xffff5a4a };
    inline const juce::Colour knobBlack  { 0xff2f8cff };
    inline const juce::Colour neon       { 0xff2f8cff };

    /** Sets the colours of a LookAndFeel to this theme. */
    void apply (juce::LookAndFeel& lf);
}
