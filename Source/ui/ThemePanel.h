#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace riff
{

/**
    View > Theme...: the skin (the picture's grey console, or a flat dark
    look in the style of FL Studio) and colour adjustments like FL Studio's
    theme settings: hue, saturation, lightness, contrast, text, accent,
    meters and grid. Changes show as you move them.
*/
class ThemePanel final : public juce::Component
{
public:
    /** `store` keeps your own presets (Save Preset...); without it the presets row is hidden. */
    ThemePanel (theme::Settings initial, std::function<void (const theme::Settings&)> onChange, juce::PropertiesFile* store = nullptr);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::String name;
        std::unique_ptr<juce::Slider> slider;
        std::function<void (theme::Settings&, double)> write;
        std::function<double (const theme::Settings&)> read;
    };

    void addRow (const juce::String& name, double min, double max, double step,
                 std::function<double (const theme::Settings&)> read, std::function<void (theme::Settings&, double)> write,
                 std::function<juce::String (double)> text);
    void changed();
    void refreshControls();

    theme::Settings settings;      // what the controls edit now: the main theme, or the editor's own
    theme::Settings whole;         // the main theme (with the editor's inside it)
    int editingScope = 0;          // 0 the mixer and the app, 1 the editor, 2 the toolbar, 3 the MultiMeter
    bool editingEditor = false;    // (scope 1)
    juce::ComboBox scopeBox;       // Theme for: the mixer and the app, the editor, the toolbar or the MultiMeter
    juce::TextButton sameButton { "Same as Mixer" };
    void editScope (int scope);
    static juce::String* scopeText (theme::Settings& s, int scope);
    std::function<void (const theme::Settings&)> onChange;
    juce::ComboBox skinBox;
    juce::ToggleButton ownAccent { "Skin's own accent colour" };
    std::vector<Row> rows;
    juce::OwnedArray<juce::TextButton> presets;
    juce::TextButton resetButton { "Reset" };

    // Meter colours: low, middle (near the top) and peak; each opens a colour picker.
    juce::TextButton meterButtons[3];
    juce::TextButton meterDefaultButton { "Skin's" };
    void chooseMeterColour (int which);   // 0-2 the meters, 3-4 the waveform's colours
    void updateMeterButtons();
    juce::uint32& colourSlot (int which);
    juce::Colour colourNow (int which) const;

    // Waveforms: the skin's own, the track's colour, one colour or a gradient; regions filled or glass.
    juce::ComboBox waveModeBox, regionBox;
    juce::TextButton waveButtons[2];

    // The cycle (locators) and marker colours
    juce::TextButton markButtons[2], markDefaultButton { "Skin's" };
    juce::TextButton editButtons[4], editDefaultButton { "Skin's" };   // the edit windows: tracks, bar lines, beat lines, piano roll
    juce::TextButton& colourButton (int which);

    // REAPER themes: load one (copied into the app's folder), or choose one loaded before
    juce::TextButton loadReaperButton { "Load REAPER Theme..." };
    juce::ComboBox reaperBox;
    std::unique_ptr<juce::FileChooser> chooser;
    void refreshReaperThemes();

    // The fader caps, knobs, channel keys and bus compressor meter, whatever the skin
    juce::ComboBox faderBox, knobBox, buttonBox, meterBox;

    // Your presets, kept in the settings file
    juce::PropertiesFile* store = nullptr;
    juce::ComboBox myPresets;
    juce::TextButton savePreset { "Save Preset..." }, deletePreset { "Delete" };
    juce::StringPairArray loadPresets() const;
    void savePresets (const juce::StringPairArray& all);
    void refreshMyPresets();
    std::unique_ptr<juce::AlertWindow> nameWindow;
};

} // namespace riff
