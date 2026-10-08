#pragma once

#include <JuceHeader.h>

/**
    The Audio Tech Pro look: a navy control bar, cream track headers, a gold
    ruler, and a console-style mixer with coloured knobs and metal fader caps.

    Colours live here; how controls are drawn lives in ConsoleLook.
*/
namespace riff::theme
{
    // --- Dark areas (arrange window, menus, dialogs) --------------------------
    inline const juce::Colour background { 0xff24262d };
    inline const juce::Colour toolbar    { 0xff2a2f4c };   // navy
    inline const juce::Colour toolbarDark{ 0xff1d2139 };
    inline const juce::Colour panel      { 0xff2d3250 };
    inline const juce::Colour laneA      { 0xff26282f };
    inline const juce::Colour laneB      { 0xff2b2d35 };
    inline const juce::Colour outline    { 0xff3d4160 };
    inline const juce::Colour gridBar    { 0xff4a4d58 };
    inline const juce::Colour gridBeat   { 0xff33363e };
    inline const juce::Colour text       { 0xffeeebe3 };
    inline const juce::Colour dimText    { 0xff9b9eb2 };
    inline const juce::Colour accent     { 0xff5a9cff };
    inline const juce::Colour selection  { 0xff3b4669 };
    inline const juce::Colour mute       { 0xff5a9cff };
    inline const juce::Colour solo       { 0xfff2c14e };
    inline const juce::Colour danger     { 0xffe2463f };
    inline const juce::Colour meterGreen { 0xff46d483 };
    inline const juce::Colour meterAmber { 0xfff2c14e };

    // --- Cream surfaces (track headers, transport keys) -----------------------
    inline const juce::Colour cream      { 0xfff1e4c1 };
    inline const juce::Colour creamShade { 0xffdccda3 };
    inline const juce::Colour creamDeep  { 0xffc9b886 };
    inline const juce::Colour ink        { 0xff24221d };   // text on cream
    inline const juce::Colour inkDim     { 0xff6e6757 };

    // --- Ruler ---------------------------------------------------------------
    inline const juce::Colour gold       { 0xffd3ae55 };
    inline const juce::Colour goldDark   { 0xffa5822f };

    // --- Console (mixer) -----------------------------------------------------
    inline const juce::Colour console      { 0xffdcd7ca };
    inline const juce::Colour consoleShade { 0xffc4bdad };
    inline const juce::Colour consoleDark  { 0xff8e887a };
    inline const juce::Colour slotDark     { 0xff2b2d35 };   // insert slots, small displays

    // --- Displays ------------------------------------------------------------
    inline const juce::Colour lcd        { 0xff0e1015 };
    inline const juce::Colour lcdText    { 0xffdfe3ec };
    inline const juce::Colour lcdDim     { 0xff7f8596 };

    // --- Knob caps -----------------------------------------------------------
    inline const juce::Colour knobCream  { 0xffece1c0 };
    inline const juce::Colour knobBlue   { 0xff2f63ab };
    inline const juce::Colour knobRed    { 0xffa3262b };
    inline const juce::Colour knobBlack  { 0xff2b2b2e };

    /** Sets the colours of a LookAndFeel to this theme. */
    void apply (juce::LookAndFeel& lf);
}
