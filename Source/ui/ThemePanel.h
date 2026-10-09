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
    ThemePanel (theme::Settings initial, std::function<void (const theme::Settings&)> onChange);

    void paint (juce::Graphics&) override;
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
};

} // namespace riff
