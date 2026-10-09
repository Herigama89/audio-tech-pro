#include "ThemePanel.h"

namespace riff
{

ThemePanel::ThemePanel (theme::Settings initial, std::function<void (const theme::Settings&)> callback)
    : settings (initial), onChange (std::move (callback))
{
    skinBox.addItemList ({ "Studio Grey (from your picture)", "Flat Dark (FL Studio style)" }, 1);
    skinBox.onChange = [this]
    {
        settings.skin = skinBox.getSelectedItemIndex() == 1 ? theme::Skin::flat : theme::Skin::studio;
        changed();
    };
    addAndMakeVisible (skinBox);

    const auto percent = [] (double v) { return (v > 0.005 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    addRow ("Hue", -180.0, 180.0, 1.0, [] (const theme::Settings& s) { return (double) s.hue; },
            [] (theme::Settings& s, double v) { s.hue = (float) v; }, [] (double v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); });
    addRow ("Saturation", 0.0, 2.0, 0.01, [] (const theme::Settings& s) { return (double) s.saturation; },
            [] (theme::Settings& s, double v) { s.saturation = (float) v; }, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });
    addRow ("Lightness", -0.8, 1.0, 0.01, [] (const theme::Settings& s) { return (double) s.lightness; },
            [] (theme::Settings& s, double v) { s.lightness = (float) v; }, percent);
    addRow ("Contrast", -0.8, 1.0, 0.01, [] (const theme::Settings& s) { return (double) s.contrast; },
            [] (theme::Settings& s, double v) { s.contrast = (float) v; }, percent);
    addRow ("Text", -1.0, 1.0, 0.01, [] (const theme::Settings& s) { return (double) s.textBrightness; },
            [] (theme::Settings& s, double v) { s.textBrightness = (float) v; }, percent);
    addRow ("Accent", 0.0, 359.0, 1.0, [] (const theme::Settings& s) { return (double) juce::jmax (0.0f, s.accentHue); },
            [] (theme::Settings& s, double v) { s.accentHue = (float) v; }, [] (double v) { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); });
    addRow ("Meters", 0.3, 1.6, 0.01, [] (const theme::Settings& s) { return (double) s.meterBrightness; },
            [] (theme::Settings& s, double v) { s.meterBrightness = (float) v; }, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });
    addRow ("Grid", 0.0, 2.0, 0.01, [] (const theme::Settings& s) { return (double) s.gridBrightness; },
            [] (theme::Settings& s, double v) { s.gridBrightness = (float) v; }, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });

    ownAccent.onClick = [this]
    {
        settings.accentHue = ownAccent.getToggleState() ? -1.0f : (float) rows[5].slider->getValue();
        changed();
    };
    addAndMakeVisible (ownAccent);

    // Quick starting points
    struct Preset { const char* name; theme::Settings s; };
    const Preset list[] {
        { "Default",       {} },
        { "Warm",          { theme::Skin::flat, -175.0f, 1.2f, 0.0f, 0.1f, 0.0f, 30.0f, 1.0f, 1.0f } },
        { "Cool Blue",     { theme::Skin::flat, 10.0f, 1.8f, -0.1f, 0.15f, 0.1f, 195.0f, 1.0f, 1.0f } },
        { "High Contrast", { theme::Skin::flat, 0.0f, 0.6f, -0.2f, 0.8f, 0.6f, 50.0f, 1.3f, 1.4f } },
        { "Soft Light",    { theme::Skin::flat, 0.0f, 0.8f, 0.5f, -0.3f, 0.2f, 160.0f, 0.9f, 0.8f } },
    };
    for (const auto& p : list)
    {
        auto* b = presets.add (new juce::TextButton (p.name));
        b->onClick = [this, s = p.s]
        {
            settings = s;
            refreshControls();
            changed();
        };
        addAndMakeVisible (b);
    }

    resetButton.onClick = [this]
    {
        const auto skin = settings.skin;
        settings = {};
        settings.skin = skin;
        refreshControls();
        changed();
    };
    addAndMakeVisible (resetButton);

    refreshControls();
    setSize (460, 120 + (int) rows.size() * 34 + 110);
}

void ThemePanel::addRow (const juce::String& name, double min, double max, double step,
                         std::function<double (const theme::Settings&)> read, std::function<void (theme::Settings&, double)> write,
                         std::function<juce::String (double)> text)
{
    Row row;
    row.name = name;
    row.read = std::move (read);
    row.write = std::move (write);
    row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    row.slider->setRange (min, max, step);
    row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    row.slider->textFromValueFunction = std::move (text);
    auto* slider = row.slider.get();
    const auto index = rows.size();
    slider->onValueChange = [this, index]
    {
        rows[index].write (settings, rows[index].slider->getValue());
        if (rows[index].name == "Accent")
            ownAccent.setToggleState (false, juce::dontSendNotification);
        changed();
    };
    addAndMakeVisible (*slider);
    rows.push_back (std::move (row));
}

void ThemePanel::refreshControls()
{
    skinBox.setSelectedItemIndex (settings.skin == theme::Skin::flat ? 1 : 0, juce::dontSendNotification);
    for (auto& r : rows)
    {
        r.slider->setValue (r.read (settings), juce::dontSendNotification);
        r.slider->updateText();
    }
    ownAccent.setToggleState (settings.accentHue < 0.0f, juce::dontSendNotification);
}

void ThemePanel::changed()
{
    if (onChange)
        onChange (settings);
    repaint();
}

void ThemePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Skin", 16, 14, 100, 24, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.5f));
    for (const auto& r : rows)
        g.drawText (r.name, 16, r.slider->getY(), 90, r.slider->getHeight(), juce::Justification::centredLeft);

    // A sample of the colours
    auto sample = juce::Rectangle<int> (16, getHeight() - 92, getWidth() - 32, 28);
    const juce::Colour swatches[] { theme::background, theme::toolbar, theme::panel, theme::stripFace, theme::accent,
                                    theme::meterGreen, theme::meterAmber, theme::text };
    const auto w = sample.getWidth() / (int) std::size (swatches);
    for (const auto& c : swatches)
    {
        g.setColour (c);
        g.fillRect (sample.removeFromLeft (w).reduced (1));
    }

    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Most of the app changes at once; a few panels pick up the new colours the next time they open.",
                      16, getHeight() - 60, getWidth() - 32, 28, juce::Justification::centredLeft, 2);
}

void ThemePanel::resized()
{
    skinBox.setBounds (110, 14, getWidth() - 126, 24);
    auto y = 54;
    for (auto& r : rows)
    {
        r.slider->setBounds (110, y, getWidth() - 126, 26);
        y += 34;
    }
    ownAccent.setBounds (110, y - 4, 220, 22);
    y += 24;
    auto presetRow = juce::Rectangle<int> (16, y, getWidth() - 32, 26);
    const auto pw = presetRow.getWidth() / (presets.size() + 1);
    for (auto* b : presets)
        b->setBounds (presetRow.removeFromLeft (pw).reduced (2, 0));
    resetButton.setBounds (presetRow.reduced (2, 0));
}

} // namespace riff
