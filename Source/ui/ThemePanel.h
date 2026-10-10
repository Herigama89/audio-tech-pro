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

    theme::Settings settings;
    std::function<void (const theme::Settings&)> onChange;
    juce::ComboBox skinBox;
    juce::ToggleButton ownAccent { "Skin's own accent colour" };
    std::vector<Row> rows;
    juce::OwnedArray<juce::TextButton> presets;
    juce::TextButton resetButton { "Reset" };

    // Meter colours: low, middle (near the top) and peak; each opens a colour picker.
    juce::TextButton meterButtons[3];
    juce::TextButton meterDefaultButton { "Skin's" };
    void chooseMeterColour (int which);
    void updateMeterButtons();

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
