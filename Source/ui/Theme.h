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
    // (colours taken from the reference picture: a dark slate with a touch of teal)
    inline const juce::Colour background { 0xff1e2829 };
    inline const juce::Colour toolbar    { 0xff2a3334 };
    inline const juce::Colour toolbarDark{ 0xff1f2829 };
    inline const juce::Colour panel      { 0xff2c3637 };
    inline const juce::Colour laneA      { 0xff212b2c };
    inline const juce::Colour laneB      { 0xff252f30 };
    inline const juce::Colour outline    { 0xff3d494a };
    inline const juce::Colour gridBar    { 0xff465253 };
    inline const juce::Colour gridBeat   { 0xff2e3839 };
    inline const juce::Colour text       { 0xffe8e9ec };
    inline const juce::Colour dimText    { 0xff9a9ca3 };
    inline const juce::Colour accent     { 0xff2f8cff };   // neon blue
    inline const juce::Colour selection  { 0xff26364d };
    inline const juce::Colour mute       { 0xffe2463f };   // backlit red
    inline const juce::Colour solo       { 0xfff2c94e };   // backlit yellow
    inline const juce::Colour danger     { 0xffe2463f };
    inline const juce::Colour meterGreen { 0xff45c25a };   // meters: green, then yellow near the top
    inline const juce::Colour meterAmber { 0xffe8c53c };

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
    inline const juce::Colour ruler      { 0xff364042 };
    inline const juce::Colour rulerDark  { 0xff283132 };

    // --- Console (mixer) -----------------------------------------------------
    inline const juce::Colour console      { 0xff2f3134 };
    inline const juce::Colour consoleShade { 0xff27282b };
    inline const juce::Colour consoleDark  { 0xff141516 };
    inline const juce::Colour slotDark     { 0xff18191b };   // insert slots, small displays

    // --- The studio-grey channel face (mixer strips, track headers) -------------
    inline const juce::Colour stripFace    { 0xff959c9c };   // the picture's strip grey
    inline const juce::Colour stripFaceLow { 0xff818888 };
    inline const juce::Colour stripEdge    { 0xff4e5556 };
    inline const juce::Colour stripInk     { 0xff101214 };   // black print on the face
    inline const juce::Colour stripInkDim  { 0xff3c4044 };
    inline const juce::Colour stripSlot    { 0xff2b3436 };   // dark insert / send / name slots
    inline const juce::Colour stripSlotOn  { 0xff253032 };
    inline const juce::Colour knobArc      { 0xff4a7aa6 };   // the thin value ring round a knob

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
