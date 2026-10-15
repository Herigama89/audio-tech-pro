#pragma once

#include <JuceHeader.h>

/**
    REAPER themes (.ReaperThemeZip / .ReaperTheme) as skins: Themes > Load REAPER Theme...

    A theme is copied into the app's own folder (Application Support / Audio Tech Pro /
    Themes) and read from there: its colours (the .ReaperTheme file) and its pictures
    (transport buttons, fader caps, knobs, mute / solo / record buttons, insert slots...),
    the double-size ones (its "200" folder) when it has them, for Retina screens.

    REAPER's picture conventions are followed:
      - a 1-pixel frame with pink (and yellow) marks says which part stretches; the frame
        itself isn't drawn
      - buttons hold three states side by side (normal, mouse over, pressed)
      - knobs are a column of frames, one per position ("_knob_stack")
      - a picture of only a few pixels means "not provided" (the app draws its own)
*/
namespace riff::themepack
{
    /** One picture, ready to draw. */
    struct Piece
    {
        juce::Image image;                   // without REAPER's marker frame
        int states = 1;                      // 3 for buttons (normal, over, down)
        bool statesVertical = false;
        juce::Range<int> stretchX, stretchY; // the part that stretches, within one state (empty: all of it scales)
        float scale = 1.0f;                  // 2 for the double-size pictures

        bool isValid() const { return image.isValid(); }
        juce::Rectangle<int> stateBounds (int state) const;
        float logicalWidth() const  { return (float) stateBounds (0).getWidth() / scale; }
        float logicalHeight() const { return (float) stateBounds (0).getHeight() / scale; }
    };

    /** The themes copied in so far (their folders), newest first. */
    juce::Array<juce::File> installed();

    /** Copies a theme in (a .ReaperThemeZip, a .ReaperTheme, or a theme folder). Returns its folder, or an empty File with `error`. */
    juce::File install (const juce::File& source, juce::String& error);

    /** Uses this theme (one of installed()); an empty File: none. */
    void use (const juce::File& themeFolder);
    bool active();
    juce::String name();
    juce::File folder();

    /** A colour from the theme file (REAPER's names: "col_arrangebg", "col_main_text"...). */
    std::optional<juce::Colour> colour (const juce::String& key);

    /** A picture by REAPER's name ("transport_play", "mcp_volthumb"...); the first of several names that the theme has. */
    const Piece& piece (std::initializer_list<const char*> names);

    /** Draws one state of a picture into an area: scaled to fit (kept in proportion), or stretched where the theme says it may. */
    void draw (juce::Graphics& g, const Piece& p, int state, juce::Rectangle<float> area, bool stretch, float opacity = 1.0f);

    /** A knob from the theme: its background, then the frame of its stack for this position (0..1). False if the theme has none. */
    bool drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion, bool pan, bool enabled = true);

    /** A button from the theme for the mixer and track headers ("mute", "solo", "recarm", "monitor"), on or off. False if it has none. */
    bool drawButton (juce::Graphics& g, const juce::String& kind, bool on, bool over, bool down, juce::Rectangle<float> area);

    /** For the tests: reads a theme folder without copying it in. */
    void useForTesting (const juce::File& themeFolder);
}
