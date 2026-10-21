#pragma once

#include <JuceHeader.h>
#include "../engine/BuiltInEffects.h"

/**
    Hand-made faces for the stock effects that stand in for Logic's best known plug-ins, laid out
    and styled in the manner of Logic's windows (our own drawing, not Apple's artwork):

      Studio Compressor  blue brushed panel, circuit buttons, a gain-reduction VU meter, silver knobs
      Channel EQ         a navy analyser screen, each band in its own colour, values you drag
      Tape Delay         slate blue sections, dark knobs with green values, a Mix fader
      Studio Reverb      a dark screen with the space's colour cloud over a light knob panel
      Pitch Correction   the scale on a keyboard, a correction meter, Settings and Tuning below

    Every control is bound to one of the effect's parameters by id; the window builds them.
*/
namespace riff::effectdesign
{
    enum class Kind
    {
        knob,        // a knob with its label (and value)
        field,       // just the value, dragged up or down (Channel EQ's numbers)
        fader,       // a vertical fader
        segmented,   // a row of buttons, one per setting
        toggle,      // one button: off or on
        dropdown,    // a menu of the settings
        hotspot,     // an invisible on/off over something the face draws (Channel EQ's band icons)
        fileButton   // picks a file for the insert property named by the id (Space Designer's impulse response)
    };

    /** A point on a curve display that can be dragged: frequency across, gain up and down, the wheel for Q. */
    struct GraphHandle
    {
        juce::String freq, gain, q;   // (gain empty: a cut, it only moves across)
        juce::String on;              // (the band's on/off: double-click toggles it)
    };

    struct Control
    {
        juce::String id;
        Kind kind = Kind::knob;
        juce::Rectangle<int> bounds;
        juce::String label {};                  // (empty: the parameter's name; "-" for none; a toggle's "=TEXT": its caption)
        juce::Colour colour {};                 // (fields: the band's colour)
        int page = 0;                           // (0: always shown; otherwise only on that page of the face)
    };

    enum class KnobStyle { silver, dark, light, black, white, chrome };

    struct Design
    {
        int width = 800, height = 500;
        std::vector<Control> controls;
        KnobStyle knobs = KnobStyle::dark;
        juce::Colour label, value, accent, track;
        bool valueAbove = true;                 // (silver knobs show their scale instead)
        bool showScale = false;                 // the knob's lowest and highest values under it (as Logic's newer plug-ins)
        bool lcdValues = false;                 // values in small dark windows under the knobs (older hardware-style faces)
        bool live = false;                      // a meter that follows the sound (gain reduction, correction)
        bool analyzer = false;                  // a live spectrum of the output behind the curve (as Logic's EQs)
        bool meters = false;                    // the effect's own readings (meters, loudness), refreshed while open
        /** Curve displays with points to drag: 20 Hz to 20 kHz across, each point's value over its parameter's range up the side. */
        struct Graph
        {
            juce::Rectangle<int> area;
            std::vector<GraphHandle> handles;
            int page = 0;
            bool pad = false;   // an XY pad: across is the first handle's "freq" parameter over its range (not log frequency); a click anywhere moves it
        };
        std::vector<Graph> graphs;
        juce::String pageParam;                 // (a face with pages: the parameter choosing which one shows, page = its value + 1)
        /** Whether a control applies with the current settings (e.g. a compressor circuit without a Knee); greyed out if not. */
        std::function<bool (const juce::String& id, const EffectInfo&, const float* values)> enabled;
        /** Called with each fresh set of readings: may set parameters from them (Match EQ keeps what it learned this way,
            so it is saved with the project). */
        std::function<void (const float* meters, int count, const EffectInfo&, const float* values,
                            const std::function<void (const juce::String& id, float value)>& set)> learn;
        /** Draws the face behind the controls: panels, titles, screens, meters. */
        std::function<void (juce::Graphics&, juce::Rectangle<int>, const EffectInfo&, const float* values, float live)> paint;
    };

    /** The output's spectrum for the window being painted: dB at log-spaced points from 20 Hz to 20 kHz. */
    struct Spectrum
    {
        static constexpr int points = 240;
        std::array<float, points> db {};
        bool valid = false;
        static double frequencyOf (int point) noexcept { return 20.0 * std::pow (1000.0, (double) point / (points - 1)); }
    };
    /** The effect's readings for the window being painted (see BuiltInEffect::readMeters), and how many. */
    const float* paintingMeters (int& count) noexcept;
    void setPaintingMeters (const float* values, int count) noexcept;

    /** Set by the window while its face paints (nullptr otherwise). */
    const Spectrum* paintingSpectrum() noexcept;
    void setPaintingSpectrum (const Spectrum*) noexcept;
    /** Draws the spectrum into a screen (0 dB at the top, -84 dB at the bottom), x from a frequency. */
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> screen, const std::function<float (double)>& xOf, juce::Colour colour);

    /** The hand-made face for an effect, or nullptr (it gets the standard face). */
    const Design* designFor (const juce::String& type);

    /** Knobs, faders, buttons and menus drawn the design's way. */
    class DesignLook final : public juce::LookAndFeel_V4
    {
    public:
        explicit DesignLook (const Design& d);

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        juce::Colour hardwareLamp() const;

        const Design& design;
    };
}
