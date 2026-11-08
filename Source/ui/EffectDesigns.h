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
        // A band between two crossovers (freq empty): it sits midway between the parameters `left` and `right`
        // (empty: 20 Hz, 20 kHz) and moves only up and down
        juce::String left {}, right {};
    };

    /**
        A part of a photographed face that moves (a knob, a knob's pointer line, a fader's cap, a switch), cut from
        the photo: it is drawn turned round its pivot, or slid along, to show the parameter's value.
        Everything is in the photo's pixels (the face draws the photo at its photoScale).
    */
    struct Sprite
    {
        const char* image = nullptr;          // its picture (a BinaryData resource name); nullptr: an invisible button over the photo
        const char* offImage = nullptr;       // (a switch photographed both ways: its picture when off; nothing turns)
        juce::Rectangle<float> rect;          // where the picture sits on the photo
        juce::Point<float> pivot;             // what it turns round
        float angle = 0.0f;                   // where it points as photographed (degrees clockwise from 12 o'clock)
        std::vector<float> angles;            // where it points at the lowest .. highest value (two: smoothly between;
                                              //  more: for evenly spaced values, or one for each setting of a switch)
        bool slides = false;                  // a fader's cap: slides across instead of turning
        float from = 0.0f, to = 0.0f;         //  its pivot's x at the lowest and highest value
        float onAngle = 0.0f, offAngle = 0.0f;// a two-way switch: where it points when on and off
        int choice = -1;                      // a button for one setting of a switch: lit at it; a click picks it, or goes back to the first
        float hitRadius = 0.0f;               // takes clicks only this far from the pivot (a knob inside a ring)

        // A knob's printed scale: (angle, value) pairs, from the lowest setting; the knob turns between them
        // (logMap: in proportion on a log scale), or snaps to them (stepped)
        std::vector<std::pair<float, float>> map;
        bool stepped = false, logMap = false;
        juce::String autoParam {};            // the map's last stop switches this parameter on instead (a release knob's AUTO)
        float offBelow = -1000.0f;            // turned further left than this angle: offParam off (an attack knob's OFF)
        juce::String offParam {};
        // A key that sets its parameter to a value (a ratio key); shift-click: altValue (all the ratio keys in)
        float setValue = std::numeric_limits<float>::quiet_NaN(), altValue = std::numeric_limits<float>::quiet_NaN();
        bool radio = false;                   // (a choice key: a click always picks it)
        bool cycle = false;                   // a key that steps through its parameter's settings
        // (a face with pages of different photos: this one's photo scale and where it sits; 0: the design's)
        float scale = 0.0f;
        juce::Point<float> offset {};

        // (drawn, not cut from the photo: the photo's knob stays still, lit as photographed, and only what turns is
        //  drawn at the setting - a pointer line, a dot, or a whole bar knob - so nothing's light turns with it)
        enum class Drawn { none, line, dot, bar };
        Drawn drawn = Drawn::none;
        juce::Colour ink {};
        float radius = 0.0f;                 // the knob's radius (photo pixels)
        float inner = 0.0f, outer = 1.0f;    // a line: from and to, as fractions of the radius; a dot: where (outer)
        float thickness = 0.1f;              // a line's width, a dot's size or a bar's width, as a fraction of the radius
        bool fill = false;                   // a dot filled in (else a ring)
    };

    struct Control
    {
        juce::String id;
        Kind kind = Kind::knob;
        juce::Rectangle<int> bounds;
        juce::String label {};                  // (empty: the parameter's name; "-" for none; a toggle's "=TEXT": its caption)
        juce::Colour colour {};                 // (fields: the band's colour)
        int page = 0;                           // (0: always shown; otherwise only on that page of the face)
        Sprite sprite {};                       // (a photographed face: the moving part, cut from the photo)
    };

    enum class KnobStyle { silver, dark, light, black, white, chrome };

    struct Design
    {
        int width = 800, height = 500;
        float photoScale = 0.0f;                // a photographed face: its photo's pixels to the face's (0: a drawn face)
        std::vector<juce::Point<int>> pageSizes;  // (pages of different sizes: each page's, the window follows)
        juce::Point<int> photoOffset;           //  where the photo's top left sits on the face (a narrow module, centred in a wider window)
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

    /** A photographed knob's angle for a value, and the value at an angle (its printed scale). */
    float photoAngleOf (const Sprite&, float value);
    float photoValueAt (const Sprite&, float angle);

    /** A picture compiled into the app, by its BinaryData resource name (cached). */
    juce::Image skinImage (const char* name);

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
        /** Draws a control's photographed moving part at a position (0..1) or on/off (or at the component's "photoAngle"). */
        void drawSprite (juce::Graphics&, juce::Component&, const Sprite&, float pos, bool on, int settings);

        const Design& design;
    };
}
