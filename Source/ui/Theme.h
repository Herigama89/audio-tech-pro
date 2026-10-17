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
    inline juce::Colour background { 0xff1e2829 };
    inline juce::Colour toolbar    { 0xff2a3334 };
    inline juce::Colour toolbarDark{ 0xff1f2829 };
    inline juce::Colour panel      { 0xff2c3637 };
    inline juce::Colour laneA      { 0xff212b2c };
    inline juce::Colour laneB      { 0xff252f30 };
    inline juce::Colour outline    { 0xff3d494a };
    inline juce::Colour gridBar    { 0xff465253 };
    inline juce::Colour gridBeat   { 0xff2e3839 };
    inline juce::Colour text       { 0xffe8e9ec };
    inline juce::Colour dimText    { 0xff9a9ca3 };
    inline juce::Colour accent     { 0xff2f8cff };   // neon blue
    inline juce::Colour selection  { 0xff26364d };
    inline juce::Colour mute       { 0xffe2463f };   // backlit red
    inline juce::Colour solo       { 0xfff2c94e };   // backlit yellow
    inline juce::Colour danger     { 0xffe2463f };
    inline juce::Colour meterGreen { 0xff45c25a };   // meters: green, then yellow near the top
    inline juce::Colour meterAmber { 0xffe8c53c };
    inline juce::Colour meterRed   { 0xffe2463f };   // the top of the meters (clipping)
    inline juce::Colour waveform   { 0xffeef3f8 };
    inline juce::Colour locators   { 0xffe8892a };   // the cycle (Logic's locators) in the ruler
    inline juce::Colour markers    { 0xff4b4e54 };
    inline juce::Colour rollWhite  { 0xff252f30 };   // the piano roll's rows: white keys
    inline juce::Colour rollBlack  { 0xff19201f };   // ...and black keys
    inline juce::Colour keysWhite  { 0xffe8e8e8 };   // the piano roll's keyboard
    inline juce::Colour keysBlack  { 0xff1a1a1a };   // markers in the marker track   // waveforms in regions (0: drawn in the track's colour)

    // --- Key and header surfaces (were cream; now dark rubber) ----------------
    inline juce::Colour cream      { 0xff3b3d41 };
    inline juce::Colour creamShade { 0xff313336 };
    inline juce::Colour creamDeep  { 0xff232427 };
    inline juce::Colour ink        { 0xffe8e9ec };   // text on those surfaces
    inline juce::Colour inkDim     { 0xff9a9ca3 };

    // --- Highlights (selection, the selected track's number) -------------------
    inline juce::Colour gold       { 0xff2f8cff };
    inline juce::Colour goldDark   { 0xff1d5fbf };

    // --- Ruler ---------------------------------------------------------------
    inline juce::Colour ruler      { 0xff364042 };
    inline juce::Colour rulerDark  { 0xff283132 };

    // --- Console (mixer) -----------------------------------------------------
    inline juce::Colour console      { 0xff2f3134 };
    inline juce::Colour consoleShade { 0xff27282b };
    inline juce::Colour consoleDark  { 0xff141516 };
    inline juce::Colour slotDark     { 0xff18191b };   // insert slots, small displays

    // --- The studio-grey channel face (mixer strips, track headers) -------------
    inline juce::Colour stripFace    { 0xff959c9c };   // the picture's strip grey
    inline juce::Colour stripFaceLow { 0xff818888 };
    inline juce::Colour stripEdge    { 0xff4e5556 };
    inline juce::Colour stripInk     { 0xff101214 };   // black print on the face
    inline juce::Colour stripInkDim  { 0xff3c4044 };
    inline juce::Colour stripSlot    { 0xff2b3436 };   // dark insert / send / name slots
    inline juce::Colour stripSlotOn  { 0xff253032 };
    inline juce::Colour knobArc      { 0xff4a7aa6 };   // the thin value ring round a knob

    // --- Displays ------------------------------------------------------------
    inline juce::Colour lcd        { 0xff0b0c0e };
    inline juce::Colour lcdText    { 0xffdfe6f2 };
    inline juce::Colour lcdDim     { 0xff7c8392 };

    // --- Knobs: the colour of each knob's LED ring ------------------------------
    inline juce::Colour knobCream  { 0xffd9ecff };   // white-blue
    inline juce::Colour knobBlue   { 0xff2f8cff };
    inline juce::Colour knobRed    { 0xffff5a4a };
    inline juce::Colour knobBlack  { 0xff2f8cff };
    inline juce::Colour neon       { 0xff2f8cff };

    /** Sets the colours of a LookAndFeel to this theme. */
    void apply (juce::LookAndFeel& lf);

    //==========================================================================
    // Skins and colour settings (View > Theme...), as in FL Studio's theme
    // settings: the colours above are worked out from a skin's own palette and
    // these adjustments, and change while the app runs.
    //==========================================================================
    enum class Skin { studio, flat, white, xenon, protools, ableton, reaper };   // the picture's grey console, a flat dark look, or Studio White (your photos' faders, knobs and meters)

    struct Settings
    {
        Skin skin = Skin::studio;
        float hue = 0.0f;          // degrees, -180..180: tints every surface
        float saturation = 1.0f;   // 0..2
        float lightness = 0.0f;    // -1..1: darker or lighter surfaces
        float contrast = 0.0f;     // -1..1: how far apart surfaces are
        float textBrightness = 0.0f;   // -1..1
        float accentHue = -1.0f;   // degrees 0..360, or -1 for the skin's own accent
        float meterBrightness = 1.0f;  // 0.3..1.6
        float gridBrightness = 1.0f;   // 0..2: bar and beat lines
        juce::uint32 meterLow = 0, meterMid = 0, meterPeak = 0;   // your own meter colours (ARGB); 0: the skin's

        // Waveforms: 0 the track's colour, 1 one colour (waveA), 2 a gradient across the region (waveA to waveB);
        // -1: the skin's own. Glass regions: dark see-through regions with a lit outline (instead of filled in the track's colour).
        int waveMode = -1;
        juce::uint32 waveA = 0, waveB = 0;
        int glassRegions = -1;   // -1: the skin's own, 0 off, 1 on

        // The cycle (locators) and markers: your own colours (ARGB), 0: the skin's.
        juce::uint32 locatorColour = 0, markerColour = 0;

        // Skin "REAPER theme": the theme's folder (copied in by Themes > Load REAPER Theme...).
        juce::String themePack;

        // The faders' caps, whatever the skin: -1 the skin's own, 0 classic, 1 Studio White, 2 Xenon, 3 the REAPER theme's,
        // 4..7 photo caps (see Looks.h). Knobs, channel keys and the bus compressor's meter likewise (-1: the skin's own).
        int faderStyle = -1;
        int knobStyle = -1, buttonStyle = -1, meterStyle = -1;

        // The editor (tracks area, ruler, editors) with a theme of its own: another Settings, as text;
        // empty: the same as the mixer and the rest of the app.
        juce::String editorTheme;

        // The toolbar (control bar, transport, Toolbar row) and the MultiMeter with themes of their own, the same way.
        juce::String toolbarTheme, meterTheme;

        // The edit windows (tracks area and editors): your own colours (ARGB), 0: the skin's.
        juce::uint32 editBack = 0, gridBarColour = 0, gridBeatColour = 0, rollBack = 0;

        juce::String toString() const;
        static Settings fromString (const juce::String&);
        bool operator== (const Settings&) const = default;
    };

    /** Works out every colour above from the skin and the adjustments. */
    void set (const Settings&);
    const Settings& current();

    /** True when the console pieces come from the picture (the studio skin); false draws them flat. */
    bool imageSkin();

    /** Studio White: a light console with the fader caps, knobs and LED meters cut from your photos. */
    bool whiteSkin();

    /** Xenon: the dark glass console of your picture (its knobs, caps, slots, meters and textures). */
    bool xenonSkin();

    /** How waveforms and regions are drawn now (the settings, or the skin's own). */
    int waveformMode();                 // 0 track colour, 1 one colour, 2 gradient
    juce::Colour waveformA();
    juce::Colour waveformB();
    bool glassRegions();

    /** Skin "REAPER theme": a loaded theme's pictures and colours are in use (see ThemePack.h). */
    bool reaperSkin();

    /** Which fader caps to draw now: 0 classic, 1 Studio White, 2 Xenon, 3 the REAPER theme's, 4..7 photo caps (Looks.h). */
    int faderStyle();

    /** Themes > Knobs, Buttons, Bus Comp meter: -1 the skin's own, else a choice from Looks.h. */
    int knobStyle();
    int buttonStyle();
    int meterStyle();

    /** The editor's own theme when it has one (else the main one): its waveforms, regions and colours. */
    const Settings& editorSettings();

    /** Every theme colour (to repaint controls that copied one when the theme changes). */
    std::vector<juce::Colour*> allColours();

    /** Parts of the app with a theme of their own (Themes > Theme for). While one is entered (from its paint() to its
        paintOverChildren()), every theme colour and setting is that part's; leaving puts the main ones back. */
    enum class Area { main = 0, toolbar, meter };
    void enterArea (Area);
    void leaveArea();
    bool areaOwn (Area);
    /** allColours()'s values as that part has them (the main ones when it has no theme of its own) */
    std::vector<juce::Colour> areaColours (Area);

    /** In an area's theme for as long as it lives (for code that copies theme colours into controls) */
    struct ScopedArea
    {
        explicit ScopedArea (Area a) { enterArea (a); }
        ~ScopedArea() { leaveArea(); }
        ScopedArea (const ScopedArea&) = delete;
        ScopedArea& operator= (const ScopedArea&) = delete;
    };

    /** A component whose children are painted in an area's theme */
    struct AreaScope
    {
        static void tag (juce::Component& c, Area a) { c.getProperties().set ("themeArea", (int) a); }
        static Area of (const juce::Component& c) { return (Area) (int) c.getProperties().getWithDefault ("themeArea", 0); }
    };
}
